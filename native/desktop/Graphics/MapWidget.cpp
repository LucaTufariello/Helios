#include "MapWidget.h"
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <rhi/qrhi.h>

namespace godeye {
struct MapWidget::GpuMesh {
  CpuMesh cpu;
  quint64 revision = 0;
  std::unique_ptr<QRhiBuffer> vertices, indices, uniform;
  std::shared_ptr<QRhiTexture> texture;
  std::unique_ptr<QRhiShaderResourceBindings> bindings;
};
namespace {
constexpr int uniformBytes = (28 + 96 * 4) * sizeof(float);
QShader shader(const QString &filename) {
  QFile file(filename);
  return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader{};
}
QImage whiteImage() {
  QImage image(1, 1, QImage::Format_RGBA8888);
  image.fill(Qt::white);
  return image;
}
} // namespace
MapWidget::MapWidget(const QString &directory, bool networking, QWidget *parent)
    : QRhiWidget(parent), tiles_(directory, networking, this) {
#if defined(Q_OS_MACOS)
  setApi(Api::Metal);
#elif defined(Q_OS_LINUX)
  setApi(Api::Vulkan);
#elif defined(Q_OS_WIN)
  setApi(Api::Direct3D11);
#endif
  setMinimumSize(480, 320);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  selectionTimer_.setSingleShot(true);
  selectionTimer_.setInterval(60);
  connect(&selectionTimer_, &QTimer::timeout, this, &MapWidget::requestTiles);
  connect(&tiles_, &TileCache::changed, this, [this] {
    update();
    emit statusChanged();
  });
  connect(&tiles_, &TileCache::terrainChanged, this, [this] {
    ++geometryRevision_;
    ensureClearance();
    update();
  });
  connect(this, &QRhiWidget::renderFailed, this, [this] {
    error_ = "The GPU map could not initialize. Check the graphics driver and selected backend.";
    emit statusChanged();
  });
  selectionTimer_.start();
}
MapWidget::~MapWidget() {
  releaseResources();
}
double MapWidget::targetHeight() const {
  return terrain_ ? std::max(0.0, tiles_.height(camera_.center).value_or(0)) : 0;
}
void MapWidget::ensureClearance() {
  if (!terrain_)
    return;
  for (int i = 0; i < 4; ++i) {
    GEGeographicPoint eye{};
    if (!ge_wgs84_from_ecef(camera_.basis(targetHeight()).eye, &eye))
      return;
    const auto ground = tiles_.height({eye.latitude, eye.longitude});
    if (!ground)
      return;
    const double deficit = std::max(0.0, *ground) + 40 - eye.height;
    if (deficit <= 0)
      return;
    camera_.range =
        std::min(60000000.0,
                 camera_.range +
                     deficit / std::max(0.17, std::cos(camera_.tilt * std::numbers::pi / 180)) + 1);
  }
}
void MapWidget::refresh() {
  ensureClearance();
  selectionTimer_.start();
  update();
  emit cameraChanged();
}
void MapWidget::setCamera(const Camera &camera) {
  if (camera.valid()) {
    camera_ = camera;
    refresh();
  }
}
void MapWidget::focus(Bounds bounds) {
  camera_.focus(bounds);
  refresh();
}
void MapWidget::zoom(double steps) {
  camera_.zoom(steps);
  refresh();
}
void MapWidget::setStreets(bool value) {
  streets_ = value;
  ++geometryRevision_;
  refresh();
}
void MapWidget::setTerrain(bool value) {
  terrain_ = value;
  ++geometryRevision_;
  refresh();
}
void MapWidget::orient(double heading, double tilt) {
  if (!std::isfinite(heading) || !std::isfinite(tilt))
    return;
  camera_.heading = std::fmod(std::fmod(heading, 360) + 360, 360);
  camera_.tilt = std::clamp(tilt, 0.0, 80.0);
  refresh();
}
void MapWidget::setAreas(const QVector<Bounds> &areas) {
  areas_ = areas;
  ++geometryRevision_;
  update();
}
void MapWidget::setMarkers(const QVector<Coordinate> &points) {
  markers_ = points;
  ++geometryRevision_;
  update();
}
void MapWidget::setOverlays(const QVector<ProductLayer> &products) {
  if (products.size() > 2 || std::any_of(products.begin(), products.end(), [](const auto &p) {
        return p.image.isNull() || !p.bounds.valid();
      }))
    return;
  overlays_ = products;
  ++geometryRevision_;
  update();
  emit statusChanged();
}
void MapWidget::setBlend(double value) {
  if (std::isfinite(value)) {
    blend_ = std::clamp(value, 0.0, 1.0);
    update();
  }
}
void MapWidget::setPixelGrid(bool value) {
  grid_ = value;
  ++geometryRevision_;
  update();
}
void MapWidget::beginRegion() {
  selecting_ = true;
  firstCorner_.reset();
  setCursor(Qt::CrossCursor);
  emit selectionChanged(true);
}
void MapWidget::cancelRegion() {
  selecting_ = false;
  firstCorner_.reset();
  unsetCursor();
  emit selectionChanged(false);
}
void MapWidget::requestTiles() {
  wantedTiles_ = visibleTiles(camera_, double(width()) / std::max(1, height()));
  // A coarse globe mosaic supplies geographic context while local detail loads.
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) {
      const TileID id{x, y, 2};
      if (!wantedTiles_.contains(id))
        wantedTiles_.prepend(id);
    }
  auto requested = wantedTiles_;
  std::stable_sort(requested.begin(), requested.end(),
                   [](auto a, auto b) { return a.zoom < b.zoom; });
  tiles_.request(requested, streets_, terrain_ && camera_.range < 200000);
  update();
}
void MapWidget::vertex(CpuMesh &mesh, Coordinate point, double height, double u, double v) const {
  const auto p = point.ecef(height);
  mesh.positions.append(p);
  const auto local = scale(subtract(p, mesh.origin), 1 / 6378137.0);
  mesh.vertices.append(
      {float(local.x), float(local.y), float(local.z), float(u), float(v), float(u), float(v)});
}
MapWidget::CpuMesh MapWidget::tileMesh(TileID id, const QImage &image) const {
  CpuMesh m;
  m.image = image;
  m.origin = id.coordinate(0.5, 0.5).ecef();
  const int side = 24;
  for (int y = 0; y <= side; ++y)
    for (int x = 0; x <= side; ++x) {
      const auto p = id.coordinate(double(x) / side, double(y) / side);
      const auto h = terrain_ ? tiles_.height(p) : std::nullopt;
      if (h)
        m.terrain = true;
      vertex(m, p, std::max(0.0, h.value_or(0)), double(x) / side, double(y) / side);
    }
  for (int y = 0; y < side; ++y)
    for (int x = 0; x < side; ++x) {
      const quint32 a = quint32(y * (side + 1) + x), b = a + 1, c = a + side + 1, d = c + 1;
      m.indices.append({a, c, b, b, c, d});
    }
  return m;
}
MapWidget::CpuMesh MapWidget::productMesh(const ProductLayer &p, TileID id) const {
  // Reuse the map's exact surface grid. A separate coarse rectangular mesh
  // would cut into mountains at close range, even with correct geographic bounds.
  CpuMesh m = tileMesh(id, p.image);
  m.overlay = true;
  m.textureKey = p.id + ":" + QString::number(p.image.cacheKey());
  const int side = 24;
  for (int y = 0; y <= side; ++y)
    for (int x = 0; x <= side; ++x) {
      const auto point = id.coordinate(double(x) / side, double(y) / side);
      const int index = y * (side + 1) + x;
      const double lat = point.latitude * std::numbers::pi / 180,
                   lon = point.longitude * std::numbers::pi / 180;
      const GEVector3 normal{std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon),
                             std::sin(lat)};
      m.positions[index] = add(m.positions[index], scale(normal, 2));
      const auto local = scale(subtract(m.positions[index], m.origin), 1 / 6378137.0);
      m.vertices[index * 7] = float(local.x);
      m.vertices[index * 7 + 1] = float(local.y);
      m.vertices[index * 7 + 2] = float(local.z);
      m.vertices[index * 7 + 3] =
          float((point.longitude - p.bounds.west) / (p.bounds.east - p.bounds.west));
      m.vertices[index * 7 + 4] =
          float((p.bounds.north - point.latitude) / (p.bounds.north - p.bounds.south));
    }
  return m;
}
MapWidget::CpuMesh MapWidget::outlineMesh(Bounds b) const {
  CpuMesh m;
  m.image = whiteImage();
  m.origin = b.center().ecef();
  m.line = true;
  m.tint = {1.0f, 0.63f, 0.23f, 1};
  const Coordinate corners[]{{b.south, b.west},
                             {b.south, b.east},
                             {b.north, b.east},
                             {b.north, b.west},
                             {b.south, b.west}};
  for (int edge = 0; edge < 4; ++edge)
    for (int i = 0; i <= 24; ++i) {
      const double t = double(i) / 24;
      const Coordinate p{std::lerp(corners[edge].latitude, corners[edge + 1].latitude, t),
                         std::lerp(corners[edge].longitude, corners[edge + 1].longitude, t)};
      vertex(m, p, (terrain_ ? std::max(0.0, tiles_.height(p).value_or(0)) : 0) + 5, 0, 0);
    }
  return m;
}
MapWidget::CpuMesh MapWidget::globeMesh() const {
  CpuMesh m;
  m.image = whiteImage();
  m.tint = {0.08f, 0.14f, 0.19f, 1};
  constexpr int rows = 64, columns = 128;
  for (int y = 0; y <= rows; ++y)
    for (int x = 0; x <= columns; ++x)
      vertex(m, {90.0 - y * 180.0 / rows, x * 360.0 / columns - 180}, -20, 0, 0);
  for (int y = 0; y < rows; ++y)
    for (int x = 0; x < columns; ++x) {
      const quint32 a = quint32(y * (columns + 1) + x), b = a + 1, c = a + columns + 1, d = c + 1;
      m.indices.append({a, c, b, b, c, d});
    }
  return m;
}
void MapWidget::initialize(QRhiCommandBuffer *) {
  releaseResources();
  error_.clear();
  backend_ = QString::fromLatin1(rhi()->backendName());
  gpu_ = QString::fromUtf8(rhi()->driverInfo().deviceName);
  white_.reset(rhi()->newTexture(QRhiTexture::RGBA8, {1, 1}));
  layoutUniform_.reset(
      rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, uniformBytes));
  linear_.reset(rhi()->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                  QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
  nearest_.reset(rhi()->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                   QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
  if (!white_->create() || !layoutUniform_->create() || !linear_->create() || !nearest_->create()) {
    error_ = "GPU map resource creation failed.";
    emit statusChanged();
    return;
  }
  layoutBindings_.reset(rhi()->newShaderResourceBindings());
  layoutBindings_->setBindings(
      {QRhiShaderResourceBinding::uniformBuffer(
           0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
           layoutUniform_.get()),
       QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                 white_.get(), linear_.get())});
  if (!layoutBindings_->create()) {
    error_ = "GPU binding creation failed.";
    emit statusChanged();
    return;
  }
  const auto vertexShader = shader(":/godeye/shaders/globe.vert.qsb"),
             fragmentShader = shader(":/godeye/shaders/globe.frag.qsb");
  auto pipeline = [&](bool line, bool overlay) {
    auto p = std::unique_ptr<QRhiGraphicsPipeline>(rhi()->newGraphicsPipeline());
    p->setShaderStages(
        {{QRhiShaderStage::Vertex, vertexShader}, {QRhiShaderStage::Fragment, fragmentShader}});
    QRhiVertexInputLayout layout;
    layout.setBindings({QRhiVertexInputBinding(7 * sizeof(float))});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                          {0, 1, QRhiVertexInputAttribute::Float2, 3 * sizeof(float)},
                          {0, 2, QRhiVertexInputAttribute::Float2, 5 * sizeof(float)}});
    p->setVertexInputLayout(layout);
    p->setTopology(line ? QRhiGraphicsPipeline::LineStrip : QRhiGraphicsPipeline::Triangles);
    p->setCullMode(QRhiGraphicsPipeline::None);
    p->setDepthTest(true);
    p->setDepthWrite(!line && !overlay);
    p->setDepthOp(QRhiGraphicsPipeline::GreaterOrEqual);
    p->setSampleCount(sampleCount());
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    p->setTargetBlends({blend});
    p->setShaderResourceBindings(layoutBindings_.get());
    p->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    if (!p->create()) {
      error_ = "GPU map shader pipeline creation failed.";
      return std::unique_ptr<QRhiGraphicsPipeline>{};
    }
    return p;
  };
  triangles_ = pipeline(false, false);
  overlayPipeline_ = pipeline(false, true);
  lines_ = pipeline(true, false);
  emit statusChanged();
}
void MapWidget::drawMesh(const QString &key, CpuMesh cpu, quint64 revision, QRhiCommandBuffer *,
                         QRhiResourceUpdateBatch *updates) {
  const float opacity = cpu.opacity;
  const auto cutouts = cpu.cutouts;
  auto &slot = meshes_[key];
  if (!slot || slot->revision != revision) {
    auto m = std::make_unique<GpuMesh>();
    m->revision = revision;
    m->cpu = std::move(cpu);
    m->vertices.reset(rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                       quint32(m->cpu.vertices.size() * sizeof(float))));
    m->uniform.reset(
        rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, uniformBytes));
    const auto cached = productTextures_.find(m->cpu.textureKey);
    const bool shared = !m->cpu.textureKey.isEmpty();
    const bool upload = !shared || cached == productTextures_.end();
    m->texture = upload ? std::shared_ptr<QRhiTexture>(
                              rhi()->newTexture(QRhiTexture::RGBA8, m->cpu.image.size()))
                        : cached->second;
    if (!m->vertices->create() || !m->uniform->create() || (upload && !m->texture->create())) {
      error_ = "A map layer exceeded available GPU resources.";
      return;
    }
    updates->uploadStaticBuffer(m->vertices.get(), m->cpu.vertices.constData());
    if (upload) {
      updates->uploadTexture(m->texture.get(), m->cpu.image);
      if (shared)
        productTextures_[m->cpu.textureKey] = m->texture;
    }
    if (!m->cpu.indices.isEmpty()) {
      m->indices.reset(rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                                        quint32(m->cpu.indices.size() * sizeof(quint32))));
      if (!m->indices->create()) {
        error_ = "A map index buffer could not be created.";
        return;
      }
      updates->uploadStaticBuffer(m->indices.get(), m->cpu.indices.constData());
    }
    m->bindings.reset(rhi()->newShaderResourceBindings());
    m->bindings->setBindings(
        {QRhiShaderResourceBinding::uniformBuffer(
             0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
             m->uniform.get()),
         QRhiShaderResourceBinding::sampledTexture(
             1, QRhiShaderResourceBinding::FragmentStage, m->texture.get(),
             grid_ && m->cpu.overlay ? nearest_.get() : linear_.get())});
    if (!m->bindings->create()) {
      error_ = "A map layer binding could not be created.";
      return;
    }
    slot = std::move(m);
  }
  if (!slot)
    return;
  slot->cpu.opacity = opacity;
  slot->cpu.cutouts = cutouts;
  const auto offset = scale(subtract(slot->cpu.origin, frameTarget_), 1 / 6378137.0);
  std::array<float, 28 + 96 * 4> uniform{};
  std::memcpy(uniform.data(), frameMatrix_.constData(), 64);
  uniform[16] = float(offset.x);
  uniform[17] = float(offset.y);
  uniform[18] = float(offset.z);
  uniform[19] = slot->cpu.opacity;
  std::copy(slot->cpu.tint.begin(), slot->cpu.tint.end(), uniform.begin() + 20);
  uniform[24] = float(std::min(qsizetype(96), cutouts.size()));
  uniform[25] = slot->cpu.overlay ? 1.0f : 0.0f;
  for (qsizetype i = 0; i < cutouts.size() && i < 96; ++i) {
    uniform[size_t(28 + i * 4)] = float(cutouts[i].left());
    uniform[size_t(29 + i * 4)] = float(cutouts[i].top());
    uniform[size_t(30 + i * 4)] = float(cutouts[i].right());
    uniform[size_t(31 + i * 4)] = float(cutouts[i].bottom());
  }
  updates->updateDynamicBuffer(slot->uniform.get(), 0, uniformBytes, uniform.data());
  drawn_.append(key);
}
void MapWidget::render(QRhiCommandBuffer *cb) {
  if (!triangles_ || !overlayPipeline_ || !lines_)
    return;
  drawn_.clear();
  const double height = targetHeight();
  frameTarget_ = camera_.basis(height).target;
  frameMatrix_ = rhi()->clipSpaceCorrMatrix() *
                 viewProjection(camera_, height, double(width()) / std::max(1, this->height()));
  auto *updates = rhi()->nextResourceUpdateBatch();
  if (!meshes_.contains("globe"))
    updates->uploadTexture(white_.get(), whiteImage());
  drawMesh("globe", meshes_.contains("globe") ? CpuMesh{} : globeMesh(), 1, cb, updates);
  renderedTiles_ = 0;
  auto tiles = wantedTiles_;
  std::stable_sort(tiles.begin(), tiles.end(), [](auto a, auto b) { return a.zoom < b.zoom; });
  QVector<TileID> loaded;
  for (const auto id : tiles)
    if (tiles_.image(id, streets_))
      loaded.append(id);
  for (const auto id : tiles)
    if (auto data = tiles_.image(id, streets_)) {
      const QString key = "tile:" + (streets_ ? QString("s:") : QString("b:")) + id.key();
      const quint64 revision = data->revision + geometryRevision_;
      const bool build =
          !meshes_.contains(key) || !meshes_[key] || meshes_[key]->revision != revision;
      auto mesh = build ? tileMesh(id, data->image) : CpuMesh{};
      mesh.cutouts = tileCutouts(id, loaded);
      drawMesh(key, std::move(mesh), revision, cb, updates);
      ++renderedTiles_;
    }
  for (qsizetype i = 0; i < overlays_.size(); ++i) {
    const auto &p = overlays_[i];
    for (const auto id : tiles) {
      const auto northWest = id.coordinate(0, 0), southEast = id.coordinate(1, 1);
      if (southEast.longitude <= p.bounds.west || northWest.longitude >= p.bounds.east ||
          northWest.latitude <= p.bounds.south || southEast.latitude >= p.bounds.north)
        continue;
      const QString key = "product:" + p.id + ":" + id.key();
      const bool build =
          !meshes_.contains(key) || !meshes_[key] || meshes_[key]->revision != geometryRevision_;
      auto mesh = build ? productMesh(p, id) : CpuMesh{};
      mesh.opacity = float(i == 0 && overlays_.size() == 2 ? 1 : blend_);
      mesh.cutouts = tileCutouts(id, tiles);
      drawMesh(key, std::move(mesh), geometryRevision_, cb, updates);
    }
  }
  for (qsizetype i = 0; i < areas_.size(); ++i) {
    const QString key = "area:" + QString::number(i);
    const bool build =
        !meshes_.contains(key) || !meshes_[key] || meshes_[key]->revision != geometryRevision_;
    drawMesh(key, build ? outlineMesh(areas_[i]) : CpuMesh{}, geometryRevision_, cb, updates);
  }
  for (qsizetype i = 0; i < markers_.size(); ++i) {
    const QString key = "marker:" + QString::number(i);
    const bool build =
        !meshes_.contains(key) || !meshes_[key] || meshes_[key]->revision != geometryRevision_;
    CpuMesh m;
    if (build) {
      const auto p = markers_[i];
      const double degrees = std::clamp(camera_.range / 111320.0 / 120, 0.00004, 0.1);
      m = outlineMesh(
          {std::max(-180.0, p.longitude - degrees), std::max(-85.0, p.latitude - degrees),
           std::min(180.0, p.longitude + degrees), std::min(85.0, p.latitude + degrees)});
      m.tint = {0.3f, 0.85f, 1.0f, 1};
    }
    drawMesh(key, std::move(m), geometryRevision_, cb, updates);
  }
  cb->beginPass(renderTarget(), QColor::fromRgbF(0.025, 0.035, 0.05), {0.0f, 0}, updates);
  const auto size = colorTexture()->pixelSize();
  cb->setViewport({0, 0, float(size.width()), float(size.height())});
  for (const auto &key : drawn_) {
    auto &m = *meshes_[key];
    cb->setGraphicsPipeline(m.cpu.line      ? lines_.get()
                            : m.cpu.overlay ? overlayPipeline_.get()
                                            : triangles_.get());
    cb->setShaderResources(m.bindings.get());
    const QRhiCommandBuffer::VertexInput input(m.vertices.get(), 0);
    cb->setVertexInput(0, 1, &input, m.indices.get(), 0, QRhiCommandBuffer::IndexUInt32);
    if (m.indices)
      cb->drawIndexed(quint32(m.cpu.indices.size()));
    else
      cb->draw(quint32(m.cpu.positions.size()));
  }
  cb->endPass();
  for (auto it = meshes_.begin(); it != meshes_.end();)
    if (!drawn_.contains(it->first))
      it = meshes_.erase(it);
    else
      ++it;
  for (auto it = productTextures_.begin(); it != productTextures_.end();)
    if (it->second.use_count() == 1)
      it = productTextures_.erase(it);
    else
      ++it;
  ++frames_;
  emit statusChanged();
}
void MapWidget::releaseResources() {
  meshes_.clear();
  productTextures_.clear();
  drawn_.clear();
  triangles_.reset();
  overlayPipeline_.reset();
  lines_.reset();
  layoutBindings_.reset();
  nearest_.reset();
  linear_.reset();
  white_.reset();
  layoutUniform_.reset();
}
std::optional<SurfaceHit> MapWidget::pick(QPointF position) const {
  GERay ray{};
  const double x = position.x() / std::max(1, width()) * 2 - 1,
               y = 1 - position.y() / std::max(1, height()) * 2;
  if (!ge_camera_ray(camera_.basis(targetHeight()), x, y, double(width()) / std::max(1, height()),
                     &ray))
    return {};
  double nearest = std::numeric_limits<double>::infinity();
  bool terrain = false;
  for (const auto &key : drawn_) {
    const auto it = meshes_.find(key);
    if (it == meshes_.end() || !it->second)
      continue;
    const auto &m = it->second->cpu;
    if (m.line || (m.overlay && m.opacity <= 0))
      continue;
    for (qsizetype i = 0; i + 2 < m.indices.size(); i += 3) {
      double d = 0;
      if (ge_ray_triangle_distance(ray, m.positions[int(m.indices[i])],
                                   m.positions[int(m.indices[i + 1])],
                                   m.positions[int(m.indices[i + 2])], &d) &&
          d < nearest) {
        bool masked = false;
        if (!m.cutouts.isEmpty() || m.overlay) {
          const auto a = m.positions[int(m.indices[i])], b = m.positions[int(m.indices[i + 1])],
                     c = m.positions[int(m.indices[i + 2])];
          const auto e1 = subtract(b, a), e2 = subtract(c, a),
                     p = subtract(add(ray.origin, scale(ray.direction, d)), a);
          const double d00 = dot(e1, e1), d01 = dot(e1, e2), d11 = dot(e2, e2), d20 = dot(p, e1),
                       d21 = dot(p, e2);
          const double denominator = d00 * d11 - d01 * d01;
          if (denominator == 0 || !std::isfinite(denominator))
            continue;
          const double v = (d11 * d20 - d01 * d21) / denominator,
                       w = (d00 * d21 - d01 * d20) / denominator;
          const auto uv = [&](int component) {
            return m.vertices[int(m.indices[i]) * 7 + component] * (1 - v - w) +
                   m.vertices[int(m.indices[i + 1]) * 7 + component] * v +
                   m.vertices[int(m.indices[i + 2]) * 7 + component] * w;
          };
          const double u = uv(5), t = uv(6);
          for (const auto &r : m.cutouts)
            if (u >= r.left() && u < r.right() && t >= r.top() && t < r.bottom()) {
              masked = true;
              break;
            }
          if (m.overlay) {
            const double imageX = uv(3), imageY = uv(4);
            if (imageX < 0 || imageX > 1 || imageY < 0 || imageY > 1)
              masked = true;
            else if (m.image
                         .pixelColor(
                             std::clamp(int(imageX * m.image.width()), 0, m.image.width() - 1),
                             std::clamp(int(imageY * m.image.height()), 0, m.image.height() - 1))
                         .alpha() == 0)
              masked = true;
          }
        }
        if (masked)
          continue;
        nearest = d;
        terrain = m.terrain;
      }
    }
  }
  if (!std::isfinite(nearest)) {
    if (!ge_ray_ellipsoid_distance(ray, &nearest))
      return {};
    terrain = false;
  }
  GEGeographicPoint p{};
  if (!ge_wgs84_from_ecef(add(ray.origin, scale(ray.direction, nearest)), &p))
    return {};
  return SurfaceHit{{p.latitude, p.longitude}, p.height, terrain};
}
void MapWidget::mousePressEvent(QMouseEvent *event) {
  press_ = previous_ = event->position();
  setFocus();
}
void MapWidget::mouseMoveEvent(QMouseEvent *event) {
  const auto delta = event->position() - previous_;
  previous_ = event->position();
  if (selecting_)
    return;
  if (event->buttons() & Qt::RightButton ||
      (event->buttons() & Qt::LeftButton && event->modifiers() & Qt::AltModifier))
    orient(camera_.heading + delta.x() * 0.25, camera_.tilt + delta.y() * 0.2);
  else if (event->buttons() & Qt::LeftButton) {
    camera_.pan(delta.x(), -delta.y());
    refresh();
  }
}
void MapWidget::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton || (event->position() - press_).manhattanLength() > 5)
    return;
  const auto hit = pick(event->position());
  if (!hit)
    return;
  if (selecting_) {
    if (!firstCorner_) {
      firstCorner_ = hit->coordinate;
      emit statusChanged();
      return;
    }
    Bounds b{std::min(firstCorner_->longitude, hit->coordinate.longitude),
             std::min(firstCorner_->latitude, hit->coordinate.latitude),
             std::max(firstCorner_->longitude, hit->coordinate.longitude),
             std::max(firstCorner_->latitude, hit->coordinate.latitude)};
    if (!b.validArea()) {
      error_ = "Choose a nonempty region spanning at most 10 degrees in each direction.";
      emit statusChanged();
      return;
    }
    error_.clear();
    cancelRegion();
    emit regionSelected(b);
  } else
    emit pointSelected(hit->coordinate, hit->height, hit->terrain);
}
void MapWidget::wheelEvent(QWheelEvent *event) {
  if (!event->pixelDelta().isNull() && !(event->modifiers() & Qt::ControlModifier)) {
    camera_.pan(event->pixelDelta().x(), event->pixelDelta().y());
    refresh();
  } else
    zoom(event->angleDelta().y() / 120.0);
  event->accept();
}
void MapWidget::resizeEvent(QResizeEvent *event) {
  QRhiWidget::resizeEvent(event);
  selectionTimer_.start();
}
void MapWidget::keyPressEvent(QKeyEvent *event) {
  switch (event->key()) {
  case Qt::Key_Left:
    camera_.pan(20, 0);
    refresh();
    break;
  case Qt::Key_Right:
    camera_.pan(-20, 0);
    refresh();
    break;
  case Qt::Key_Up:
    camera_.pan(0, -20);
    refresh();
    break;
  case Qt::Key_Down:
    camera_.pan(0, 20);
    refresh();
    break;
  case Qt::Key_Plus:
  case Qt::Key_Equal:
    zoom(1);
    break;
  case Qt::Key_Minus:
    zoom(-1);
    break;
  case Qt::Key_Home:
    orient(0, 0);
    break;
  case Qt::Key_Escape:
    cancelRegion();
    break;
  default:
    QRhiWidget::keyPressEvent(event);
    return;
  }
  event->accept();
}
bool MapWidget::event(QEvent *event) {
  if (event->type() == QEvent::NativeGesture) {
    auto *gesture = static_cast<QNativeGestureEvent *>(event);
    if (gesture->gestureType() == Qt::ZoomNativeGesture) {
      zoom(gesture->value() * 10);
      return true;
    }
  }
  return QRhiWidget::event(event);
}
} // namespace godeye

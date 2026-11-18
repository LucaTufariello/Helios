#include "MapWidget.h"
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSet>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <rhi/qrhi.h>

namespace helios {
struct MapWidget::GpuMesh {
  CpuMesh cpu;
  quint64 revision = 0;
  std::unique_ptr<QRhiBuffer> vertices, indices, uniform;
  std::shared_ptr<QRhiTexture> texture;
  std::unique_ptr<QRhiShaderResourceBindings> bindings;
  std::unique_ptr<QRhiSampler> sampler;
  GEVector3 minimum{}, maximum{};
};
namespace {
constexpr int uniformBytes = (32 + 96 * 4) * sizeof(float);
QShader shader(const QString &filename) {
  QFile file(filename);
  return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader{};
}
QImage whiteImage() {
  QImage image(1, 1, QImage::Format_RGBA8888);
  image.fill(Qt::white);
  return image;
}
bool intersectsBounds(const GERay &ray, GEVector3 minimum, GEVector3 maximum,
                      double nearest) {
  double near = 0, far = nearest;
  const std::array<double, 3> origins{ray.origin.x, ray.origin.y, ray.origin.z};
  const std::array<double, 3> directions{ray.direction.x, ray.direction.y, ray.direction.z};
  const std::array<double, 3> lows{minimum.x, minimum.y, minimum.z};
  const std::array<double, 3> highs{maximum.x, maximum.y, maximum.z};
  for (int axis = 0; axis < 3; ++axis) {
    // Expand slightly to keep shared triangle edges and grazing rays inclusive.
    const double low = lows[axis] - .01, high = highs[axis] + .01;
    if (directions[axis] == 0) {
      if (origins[axis] < low || origins[axis] > high)
        return false;
      continue;
    }
    double a = (low - origins[axis]) / directions[axis],
           b = (high - origins[axis]) / directions[axis];
    if (a > b)
      std::swap(a, b);
    near = std::max(near, a);
    far = std::min(far, b);
    if (near > far)
      return false;
  }
  return true;
}
std::optional<QPointF> projectPoint(GEVector3 world, const GECameraBasis &basis,
                                   const QMatrix4x4 &projection, int width, int height) {
  const auto delta = subtract(world, basis.eye);
  const double distance = std::sqrt(dot(delta, delta));
  if (distance <= 0)
    return {};
  double surface = 0;
  if (ge_ray_ellipsoid_distance({basis.eye, scale(delta, 1 / distance)}, &surface) &&
      surface + 100 < distance)
    return {};
  const auto relative = scale(subtract(world, basis.target), 1 / 6378137.0);
  const auto clip = projection *
                    QVector4D(float(relative.x), float(relative.y), float(relative.z), 1);
  if (clip.w() <= 0)
    return {};
  const auto ndc = clip.toVector3DAffine();
  if (std::abs(ndc.x()) > 1.05 || std::abs(ndc.y()) > 1.05)
    return {};
  return QPointF((ndc.x() + 1) * width / 2, (1 - ndc.y()) * height / 2);
}
} // namespace
MapWidget::MapWidget(const QString &directory, bool networking, QWidget *parent)
    : QRhiWidget(parent), networking_(networking), tiles_(directory, networking, this),
      daily_(directory + "/daily", networking, this) {
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
  connect(&daily_, &DailyImageryCache::changed, this, [this] {
    update();
    emit statusChanged();
  });
  connect(&tiles_, &TileCache::changed, this, [this] {
    update();
    emit statusChanged();
  });
  terrainTimer_.setSingleShot(true);
  terrainTimer_.setInterval(80);
  connect(&terrainTimer_, &QTimer::timeout, this, [this] {
    ++geometryRevision_;
    ++markerRevision_;
    ++pathRevision_;
    ensureClearance();
    update();
  });
  connect(&tiles_, &TileCache::terrainChanged, this, [this] {
    ensureClearance();
    // Bound the delay without restarting it for every downloaded terrain tile.
    // One batch rebuilds the surface instead of each decode rebuilding the globe.
    if (!terrainTimer_.isActive())
      terrainTimer_.start();
  });
  connect(this, &QRhiWidget::renderFailed, this, [this] {
    error_ = "The GPU map could not initialize. Check the graphics driver and selected backend.";
    queueStatusChanged();
  });
  selectionTimer_.start();
  sceneTimer_.setInterval(60);
  connect(&sceneTimer_, &QTimer::timeout, this, &MapWidget::updateTileset);
}
MapWidget::~MapWidget() {
  sceneTimer_.stop();
  terrainTimer_.stop();
  releaseResources();
}
void MapWidget::queueStatusChanged() {
  if (statusPending_)
    return;
  statusPending_ = true;
  // A status observer can change the widget layout and trigger a synchronous
  // resize paint. Let QRhiWidget finish its frame before notifying the UI.
  QTimer::singleShot(0, this, [this] {
    statusPending_ = false;
    emit statusChanged();
  });
}
bool MapWidget::loadTileset(const QUrl &url) {
  if (!networking_ && !url.isLocalFile()) {
    error_ = "Offline mode: remote 3D Tiles are unavailable. Choose a local tileset.json.";
    emit statusChanged();
    return false;
  }
  std::unique_ptr<GEScene, decltype(&GE_scene_destroy)> next(
      GE_scene_create(url.toEncoded().constData()), GE_scene_destroy);
  if (!next)
    return false;
  error_.clear();
  scene_ = std::move(next);
  sceneMeshes_.clear();
  sceneDrawn_ = sceneSettled_ = 0;
  sceneFocusPending_ = true;
  sceneBudgetExceeded_ = false;
  sceneTimer_.start();
  updateTileset();
  update();
  return true;
}
void MapWidget::removeTileset() {
  sceneTimer_.stop();
  sceneMeshes_.clear();
  scene_.reset();
  sceneDrawn_ = 0;
  sceneBudgetExceeded_ = false;
  update();
  emit statusChanged();
}
bool MapWidget::focusTileset() {
  GEPoint center{};
  GEGeographicPoint point{};
  double radius = 0;
  if (!scene_ || !GE_scene_center(scene_.get(), &center, &radius) ||
      !ge_wgs84_from_ecef({center.x, center.y, center.z}, &point))
    return false;
  setCamera(
      Camera{{point.latitude, point.longitude}, std::clamp(radius * 3, 500.0, 60000000.0), 0, 55});
  return true;
}
QString MapWidget::sceneStatus() const {
  if (!scene_)
    return {};
  return QString("3D Tiles: %1 meshes · %2% loaded · %3 load/format failures%4")
      .arg(sceneDrawn_)
      .arg(int(GE_scene_progress(scene_.get())))
      .arg(GE_scene_failures(scene_.get()))
      .arg(sceneBudgetExceeded_ ? " · GPU memory limit reached" : "");
}
QString MapWidget::sceneCredits() const {
  if (!scene_)
    return {};
  return QString::fromUtf8(GE_scene_credits(scene_.get()))
      .left(8000)
      .replace(QRegularExpression("<[^>]+>"), " ")
      .replace("&amp;", "&")
      .replace("&copy;", QString::fromUtf8("©"))
      .replace(QRegularExpression("\\s+"), " ")
      .trimmed();
}
void MapWidget::updateTileset() {
  if (!scene_)
    return;
  const auto basis = camera_.basis(targetHeight());
  const auto point = [](GEVector3 p) { return GEPoint{p.x, p.y, p.z}; };
  const auto count = GE_scene_update(scene_.get(), point(basis.eye), point(basis.forward),
                                     point(basis.up), width(), height());
  QVector<GEMesh> meshes;
  bool changed = count != quint32(sceneMeshes_.size());
  for (uint32_t i = 0; i < count; ++i) {
    GEMesh mesh{};
    if (!GE_scene_mesh(scene_.get(), i, &mesh))
      continue;
    if (i >= quint32(sceneMeshes_.size()) || sceneMeshes_[i].id != mesh.id)
      changed = true;
    meshes.append(mesh);
  }
  sceneMeshes_ = std::move(meshes);
  if (sceneFocusPending_ && focusTileset())
    sceneFocusPending_ = false;
  if (changed)
    update();
  if (GE_scene_progress(scene_.get()) >= 100)
    ++sceneSettled_;
  else
    sceneSettled_ = 0;
  if (sceneSettled_ >= 3)
    sceneTimer_.stop();
  emit statusChanged();
}
MapWidget::CpuMesh MapWidget::sceneMesh(const GEMesh &mesh) {
  CpuMesh cpu;
  cpu.origin = {mesh.origin.x, mesh.origin.y, mesh.origin.z};
  cpu.scene = true;
  cpu.doubleSided = mesh.doubleSided;
  cpu.blend = mesh.blend;
  cpu.alphaCutoff = mesh.alphaCutoff;
  cpu.tint = {mesh.red, mesh.green, mesh.blue, mesh.alpha};
  cpu.wrapS = mesh.wrapS;
  cpu.wrapT = mesh.wrapT;
  cpu.magFilter = mesh.magFilter;
  cpu.minFilter = mesh.minFilter;
  cpu.textureKey = "scene-texture:" + QString::number(mesh.textureID);
  cpu.vertices.reserve(qsizetype(mesh.vertexCount) * 7);
  for (uint32_t i = 0; i < mesh.vertexCount; ++i) {
    const auto &v = mesh.vertices[i];
    cpu.vertices.append({v.x, v.y, v.z, v.u, v.v, 0, 0});
  }
  cpu.indices = QVector<quint32>(mesh.indices, mesh.indices + mesh.indexCount);
  if (!sceneImages_.contains(mesh.textureID))
    sceneImages_[mesh.textureID] =
        mesh.pixels
            ? QImage(mesh.pixels, int(mesh.width), int(mesh.height), QImage::Format_RGBA8888).copy()
            : whiteImage();
  cpu.image = sceneImages_[mesh.textureID];
  return cpu;
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
  ++markerRevision_;
  if (scene_) {
    sceneSettled_ = 0;
    sceneTimer_.start();
  }
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
  ++pathRevision_;
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
  QVector<MapMarker> markers;
  for (const auto point : points)
    markers.append({{}, point, {}});
  setMarkers("mission", markers);
}
bool MapWidget::setMarkers(const QString &layer, const QVector<MapMarker> &markers, QColor color) {
  if (layer.isEmpty() || layer.size() > 80 || (!markers_.contains(layer) && markers_.size() >= 32))
    return false;
  qsizetype total = markers.size();
  for (auto i = markers_.cbegin(); i != markers_.cend(); ++i)
    if (i.key() != layer)
      total += i->markers.size();
  if (total > 10000)
    return false;
  for (const auto &marker : markers)
    if (!marker.coordinate.valid() || marker.id.size() > 512 ||
        (marker.altitude &&
         (!std::isfinite(*marker.altitude) || *marker.altitude < -2000 || *marker.altitude > 1e8)))
      return false;
  if (markers.isEmpty())
    markers_.remove(layer);
  else
    markers_[layer] = {markers, color};
  ++markerRevision_;
  update();
  return true;
}
bool MapWidget::setPaths(const QString &layer, const QVector<QVector<Coordinate>> &paths,
                         const QVector<QVector<double>> &altitudes, QColor color) {
  if (layer.isEmpty() || layer.size() > 80 || (!paths_.contains(layer) && paths_.size() >= 32) ||
      (!altitudes.isEmpty() && altitudes.size() != paths.size()))
    return false;
  qsizetype total = 0;
  for (auto i = paths_.cbegin(); i != paths_.cend(); ++i)
    if (i.key() != layer)
      for (const auto &path : i->paths)
        total += path.size();
  for (qsizetype i = 0; i < paths.size(); ++i) {
    const auto &path = paths[i];
    total += path.size();
    if (total > 10000 || (!altitudes.isEmpty() && altitudes[i].size() != path.size()))
      return false;
    for (const auto point : path)
      if (!point.valid())
        return false;
    if (!altitudes.isEmpty())
      for (const auto h : altitudes[i])
        if (!std::isfinite(h) || h < -2000 || h > 1e8)
          return false;
  }
  if (paths.isEmpty())
    paths_.remove(layer);
  else
    paths_[layer] = {paths, altitudes, color};
  ++pathRevision_;
  update();
  return true;
}
void MapWidget::setDisplayStyle(const QString &style) {
  if (!QStringList{"standard", "monochrome", "nightVision", "thermal"}.contains(style))
    return;
  displayStyle_ = style;
  update();
}
bool MapWidget::setDailyImagery(DailySource source, QDate date, double opacity) {
  if (!std::isfinite(opacity) || !daily_.configure(source, date))
    return false;
  dailyOpacity_ = std::clamp(opacity, 0.0, 1.0);
  daily_.request(wantedTiles_);
  update();
  return true;
}
std::optional<QPointF> MapWidget::screenPoint(Coordinate point,
                                              std::optional<double> altitude) const {
  if (!point.valid())
    return {};
  const auto h = targetHeight();
  const auto world = point.ecef(altitude ? *altitude
                                      : terrain_ ? std::max(0.0, tiles_.height(point).value_or(0)) + 8
                                                 : 8);
  return projectPoint(world, camera_.basis(h),
                      viewProjection(camera_, h, double(width()) / std::max(1, height())),
                      width(), height());
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
void MapWidget::beginPointCapture(QObject *owner) {
  if (!owner)
    return;
  cancelRegion();
  pointCaptureOwner_ = owner;
}
void MapWidget::endPointCapture(QObject *owner) {
  if (pointCaptureOwner_ == owner)
    pointCaptureOwner_.clear();
}
void MapWidget::beginRegion(QObject *owner) {
  pointCaptureOwner_.clear();
  selecting_ = true;
  firstCorner_.reset();
  regionOwner_ = owner;
  setCursor(Qt::CrossCursor);
  emit selectionChanged(true);
}
void MapWidget::cancelRegion() {
  selecting_ = false;
  firstCorner_.reset();
  regionOwner_.clear();
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
  daily_.request(requested);
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
  m.display = true;
  m.origin = id.coordinate(0.5, 0.5).ecef();
  // Coarse imagery triangles must stay above the fallback globe. A fixed
  // 24-cell grid at zoom 1/2 cuts kilometres into the ellipsoid and lets the
  // more finely tessellated globe show through as dark horizontal seams.
  const int side = std::max(24, 256 >> std::min(id.zoom, 8));
  m.positions.reserve((side + 1) * (side + 1));
  m.vertices.reserve((side + 1) * (side + 1) * 7);
  m.indices.reserve(side * side * 6);
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
  m.display = false;
  m.textureKey = p.id + ":" + QString::number(p.image.cacheKey());
  const int side = std::max(24, 256 >> std::min(id.zoom, 8));
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
MapWidget::CpuMesh MapWidget::pathMesh(const QVector<Coordinate> &path,
                                       const QVector<double> &altitudes) const {
  CpuMesh mesh;
  mesh.image = whiteImage();
  mesh.line = true;
  mesh.tint = {0.5f, 0.85f, 1.0f, 1};
  if (path.isEmpty())
    return mesh;
  mesh.origin = path.first().ecef();
  for (qsizetype i = 0; i + 1 < path.size(); ++i) {
    const auto a = path[i], b = path[i + 1];
    const auto longitude = std::remainder(b.longitude - a.longitude, 360.0);
    const auto segments = std::clamp(
        int(std::ceil(std::max(std::abs(b.latitude - a.latitude), std::abs(longitude)) / 0.25)), 1,
        32);
    for (int j = 0; j <= segments; ++j) {
      const double t = double(j) / segments;
      const Coordinate point{std::lerp(a.latitude, b.latitude, t),
                             std::remainder(a.longitude + longitude * t, 360.0)};
      const double h = altitudes.isEmpty()
                           ? (terrain_ ? std::max(0.0, tiles_.height(point).value_or(0)) : 0) + 8
                           : std::lerp(altitudes[i], altitudes[i + 1], t);
      vertex(mesh, point, h, 0, 0);
    }
  }
  return mesh;
}
MapWidget::CpuMesh MapWidget::markerMesh(const MarkerLayer &layer) const {
  CpuMesh mesh;
  mesh.image = whiteImage();
  mesh.line = true;
  mesh.lineList = true;
  mesh.origin = camera_.center.ecef();
  mesh.tint = {float(layer.color.redF()), float(layer.color.greenF()), float(layer.color.blueF()),
               float(layer.color.alphaF())};
  mesh.positions.reserve(layer.markers.size() * 8);
  mesh.vertices.reserve(layer.markers.size() * 8 * 7);
  const auto h = targetHeight();
  const auto basis = camera_.basis(h);
  const auto projection = viewProjection(camera_, h, double(width()) / std::max(1, height()));
  const double size = std::clamp(camera_.range / std::max(1, height()) * 0.45, 1.0, 40000.0);
  const auto right = scale(basis.right, size), up = scale(basis.up, size);
  for (const auto &marker : layer.markers) {
    const double elevation = marker.altitude ? *marker.altitude
        : (terrain_ ? std::max(0.0, tiles_.height(marker.coordinate).value_or(0)) : 0) + 12;
    const auto p = marker.coordinate.ecef(elevation);
    if (!projectPoint(p, basis, projection, width(), height()))
      continue;
    const std::array<GEVector3, 4> corners{add(add(p, right), up), add(subtract(p, right), up),
                                           subtract(subtract(p, right), up),
                                           subtract(add(p, right), up)};
    for (int edge = 0; edge < 4; ++edge)
      for (const int i : {edge, (edge + 1) % 4}) {
        mesh.positions.append(corners[i]);
        const auto v = scale(subtract(corners[i], mesh.origin), 1 / 6378137.0);
        mesh.vertices.append({float(v.x), float(v.y), float(v.z), 0, 0, 0, 0});
      }
  }
  return mesh;
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
  const auto deviceType = rhi()->driverInfo().deviceType;
  hardwareGpu_ = deviceType == QRhiDriverInfo::DiscreteDevice ||
                 deviceType == QRhiDriverInfo::IntegratedDevice ||
                 deviceType == QRhiDriverInfo::ExternalDevice;
  white_.reset(rhi()->newTexture(QRhiTexture::RGBA8, {1, 1}));
  layoutUniform_.reset(
      rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, uniformBytes));
  linear_.reset(rhi()->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                  QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
  nearest_.reset(rhi()->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                   QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
  if (!white_->create() || !layoutUniform_->create() || !linear_->create() || !nearest_->create()) {
    error_ = "GPU map resource creation failed.";
    queueStatusChanged();
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
    queueStatusChanged();
    return;
  }
  const auto vertexShader = shader(":/helios/shaders/globe.vert.qsb"),
             fragmentShader = shader(":/helios/shaders/globe.frag.qsb");
  auto pipeline = [&](bool line, bool overlay, bool scene = false, bool doubleSided = true,
                      bool list = false) {
    auto p = std::unique_ptr<QRhiGraphicsPipeline>(rhi()->newGraphicsPipeline());
    p->setShaderStages(
        {{QRhiShaderStage::Vertex, vertexShader}, {QRhiShaderStage::Fragment, fragmentShader}});
    QRhiVertexInputLayout layout;
    layout.setBindings({QRhiVertexInputBinding(7 * sizeof(float))});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                          {0, 1, QRhiVertexInputAttribute::Float2, 3 * sizeof(float)},
                          {0, 2, QRhiVertexInputAttribute::Float2, 5 * sizeof(float)}});
    p->setVertexInputLayout(layout);
    p->setTopology(line ? (list ? QRhiGraphicsPipeline::Lines : QRhiGraphicsPipeline::LineStrip)
                        : QRhiGraphicsPipeline::Triangles);
    p->setCullMode(scene && !doubleSided ? QRhiGraphicsPipeline::Back : QRhiGraphicsPipeline::None);
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
  lineList_ = pipeline(true, false, false, true, true);
  for (int i = 0; i < 4; ++i)
    scenePipelines_[i] = pipeline(false, i >= 2, true, i % 2);
  queueStatusChanged();
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
    if (!m->cpu.positions.isEmpty()) {
      m->minimum = m->maximum = m->cpu.positions.first();
      for (const auto &p : m->cpu.positions) {
        m->minimum = {std::min(m->minimum.x, p.x), std::min(m->minimum.y, p.y),
                      std::min(m->minimum.z, p.z)};
        m->maximum = {std::max(m->maximum.x, p.x), std::max(m->maximum.y, p.y),
                      std::max(m->maximum.z, p.z)};
      }
    }
    m->vertices.reset(rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                       quint32(m->cpu.vertices.size() * sizeof(float))));
    m->uniform.reset(
        rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, uniformBytes));
    const auto cached = productTextures_.find(m->cpu.textureKey);
    const bool shared = !m->cpu.textureKey.isEmpty();
    const bool upload = !shared || cached == productTextures_.end();
    m->texture = upload ? std::shared_ptr<QRhiTexture>(rhi()->newTexture(
                              QRhiTexture::RGBA8, m->cpu.image.size(), 1,
                              m->cpu.scene ? QRhiTexture::sRGB | QRhiTexture::MipMapped |
                                                 QRhiTexture::UsedWithGenerateMips
                                           : QRhiTexture::Flags{}))
                        : cached->second;
    if (!m->vertices->create() || !m->uniform->create() || (upload && !m->texture->create())) {
      error_ = "A map layer exceeded available GPU resources.";
      return;
    }
    updates->uploadStaticBuffer(m->vertices.get(), m->cpu.vertices.constData());
    if (upload) {
      updates->uploadTexture(m->texture.get(), m->cpu.image);
      if (m->cpu.scene)
        updates->generateMips(m->texture.get());
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
    if (m->cpu.scene) {
      const auto address = [](int code) {
        return code == 33071   ? QRhiSampler::ClampToEdge
               : code == 33648 ? QRhiSampler::Mirror
                               : QRhiSampler::Repeat;
      };
      m->sampler.reset(rhi()->newSampler(
          m->cpu.magFilter == 9728 ? QRhiSampler::Nearest : QRhiSampler::Linear,
          m->cpu.minFilter == 9728 || m->cpu.minFilter == 9984 || m->cpu.minFilter == 9986
              ? QRhiSampler::Nearest
              : QRhiSampler::Linear,
          m->cpu.minFilter == 9984 || m->cpu.minFilter == 9985   ? QRhiSampler::Nearest
          : m->cpu.minFilter == 9986 || m->cpu.minFilter == 9987 ? QRhiSampler::Linear
                                                                 : QRhiSampler::None,
          address(m->cpu.wrapS), address(m->cpu.wrapT)));
      if (!m->sampler->create()) {
        error_ = "A 3D Tiles sampler could not be created.";
        return;
      }
    }
    m->bindings->setBindings(
        {QRhiShaderResourceBinding::uniformBuffer(
             0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
             m->uniform.get()),
         QRhiShaderResourceBinding::sampledTexture(
             1, QRhiShaderResourceBinding::FragmentStage, m->texture.get(),
             m->sampler                                 ? m->sampler.get()
             : grid_ && m->cpu.overlay && !m->cpu.daily ? nearest_.get()
                                                        : linear_.get())});
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
  std::array<float, 32 + 96 * 4> uniform{};
  std::memcpy(uniform.data(), frameMatrix_.constData(), 64);
  uniform[16] = float(offset.x);
  uniform[17] = float(offset.y);
  uniform[18] = float(offset.z);
  uniform[19] = slot->cpu.opacity;
  std::copy(slot->cpu.tint.begin(), slot->cpu.tint.end(), uniform.begin() + 20);
  uniform[24] = float(std::min(qsizetype(96), cutouts.size()));
  uniform[25] = slot->cpu.overlay ? 1.0f : 0.0f;
  uniform[26] = slot->cpu.scene ? (slot->cpu.blend ? 2.0f : 1.0f) : 0.0f;
  uniform[27] = slot->cpu.alphaCutoff;
  uniform[28] = slot->cpu.display
                    ? float(QStringList{"standard", "monochrome", "nightVision", "thermal"}.indexOf(
                          displayStyle_))
                    : 0;
  for (qsizetype i = 0; i < cutouts.size() && i < 96; ++i) {
    uniform[size_t(32 + i * 4)] = float(cutouts[i].left());
    uniform[size_t(33 + i * 4)] = float(cutouts[i].top());
    uniform[size_t(34 + i * 4)] = float(cutouts[i].right());
    uniform[size_t(35 + i * 4)] = float(cutouts[i].bottom());
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
      if (build)
        mesh.textureKey = "basemap:" + key + ":" + QString::number(data->revision);
      mesh.cutouts = tileCutouts(id, loaded);
      drawMesh(key, std::move(mesh), revision, cb, updates);
      ++renderedTiles_;
    }
  QVector<TileID> dailyLoaded;
  for (const auto id : tiles)
    if (daily_.image(id))
      dailyLoaded.append(id);
  if (dailyOpacity_ > 0)
    for (const auto id : dailyLoaded)
      if (auto data = daily_.image(id)) {
        const auto key = "daily:" + QString::number(daily_.generation()) + ":" + id.key();
        const quint64 revision = data->revision + geometryRevision_;
        const bool build =
            !meshes_.contains(key) || !meshes_[key] || meshes_[key]->revision != revision;
        auto mesh = build ? tileMesh(id, data->image) : CpuMesh{};
        if (build) {
          mesh.overlay = true;
          mesh.daily = true;
          mesh.textureKey = "daily-texture:" + QString::number(daily_.generation()) + ":" +
                            data->id.key() + ":" + QString::number(data->revision);
          const double factor = std::ldexp(1.0, id.zoom - data->id.zoom);
          for (qsizetype i = 0; i < mesh.positions.size(); ++i) {
            mesh.vertices[i * 7 + 3] =
                float((id.x - data->id.x * factor + mesh.vertices[i * 7 + 3]) / factor);
            mesh.vertices[i * 7 + 4] =
                float((id.y - data->id.y * factor + mesh.vertices[i * 7 + 4]) / factor);
          }
        }
        mesh.opacity = float(dailyOpacity_);
        mesh.cutouts = tileCutouts(id, dailyLoaded);
        drawMesh(key, std::move(mesh), revision, cb, updates);
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
  for (auto layer = markers_.cbegin(); layer != markers_.cend(); ++layer) {
    const auto key = "marker:" + layer.key();
    const bool build =
        !meshes_.contains(key) || !meshes_[key] || meshes_[key]->revision != markerRevision_;
    auto mesh = build ? markerMesh(layer.value()) : CpuMesh{};
    if (build && mesh.positions.isEmpty())
      continue;
    drawMesh(key, std::move(mesh), markerRevision_, cb, updates);
  }
  for (auto layer = paths_.cbegin(); layer != paths_.cend(); ++layer)
    for (qsizetype i = 0; i < layer->paths.size(); ++i) {
      const auto key = "path:" + layer.key() + ":" + QString::number(i);
      const bool build =
          !meshes_.contains(key) || !meshes_[key] || meshes_[key]->revision != pathRevision_;
      auto mesh = build
                      ? pathMesh(layer->paths[i], layer->altitudes.isEmpty() ? QVector<double>{}
                                                                             : layer->altitudes[i])
                      : CpuMesh{};
      if (build) {
        const auto c = layer->color;
        mesh.tint = {float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF())};
      }
      if (build && mesh.positions.isEmpty())
        continue;
      drawMesh(key, std::move(mesh), pathRevision_, cb, updates);
    }
  // Share each scene texture across its primitives. Bound the visible GPU set
  // independently of Cesium's CPU cache and exclude unuploaded meshes from picks.
  auto sceneMeshes = sceneMeshes_;
  const auto eye = camera_.basis(height).eye;
  std::stable_sort(sceneMeshes.begin(), sceneMeshes.end(), [&](const auto &a, const auto &b) {
    if (a.blend != b.blend)
      return !a.blend;
    const auto distance = [&](const auto &m) {
      const auto d = subtract({m.origin.x, m.origin.y, m.origin.z}, eye);
      return dot(d, d);
    };
    return distance(a) > distance(b);
  });
  uint64_t sceneBytes = 0;
  QSet<uint64_t> textures;
  QVector<uint64_t> renderedScene;
  sceneBudgetExceeded_ = false;
  for (const auto &mesh : sceneMeshes) {
    const uint64_t textureBytes =
        textures.contains(mesh.textureID)
            ? 0
            : uint64_t(std::max(1u, mesh.width)) * std::max(1u, mesh.height) * 4 * 4 / 3;
    const uint64_t bytes = uint64_t(mesh.vertexCount) * 7 * sizeof(float) +
                           uint64_t(mesh.indexCount) * 4 + textureBytes;
    if (bytes > 256 * 1024 * 1024 - sceneBytes) {
      sceneBudgetExceeded_ = true;
      continue;
    }
    const auto key = "scene:" + QString::number(mesh.id);
    drawMesh(key, meshes_.contains(key) ? CpuMesh{} : sceneMesh(mesh), mesh.id, cb, updates);
    if (drawn_.contains(key)) {
      renderedScene.append(mesh.id);
      textures.insert(mesh.textureID);
      sceneBytes += bytes;
    }
  }
  sceneDrawn_ = int(renderedScene.size());
  for (auto it = sceneImages_.begin(); it != sceneImages_.end();)
    if (!textures.contains(it.key()))
      it = sceneImages_.erase(it);
    else
      ++it;
  if (scene_)
    GE_scene_rendered_meshes(scene_.get(), renderedScene.constData(),
                             uint32_t(renderedScene.size()));
  cb->beginPass(renderTarget(), QColor::fromRgbF(0.025, 0.035, 0.05), {0.0f, 0}, updates);
  const auto size = colorTexture()->pixelSize();
  cb->setViewport({0, 0, float(size.width()), float(size.height())});
  for (const auto &key : drawn_) {
    auto &m = *meshes_[key];
    cb->setGraphicsPipeline(
        m.cpu.scene     ? scenePipelines_[int(m.cpu.blend) * 2 + int(m.cpu.doubleSided)].get()
        : m.cpu.line    ? (m.cpu.lineList ? lineList_.get() : lines_.get())
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
  queueStatusChanged();
}
void MapWidget::releaseResources() {
  meshes_.clear();
  productTextures_.clear();
  sceneImages_.clear();
  drawn_.clear();
  triangles_.reset();
  overlayPipeline_.reset();
  lines_.reset();
  lineList_.reset();
  for (auto &pipeline : scenePipelines_)
    pipeline.reset();
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
    if (m.scene || m.line || (m.overlay && m.opacity <= 0))
      continue;
    if (!intersectsBounds(ray, it->second->minimum, it->second->maximum, nearest))
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
    ge_ray_ellipsoid_distance(ray, &nearest);
    terrain = false;
  }
  bool sceneHit = false;
  GEPoint hit{};
  if (scene_ && GE_scene_pick(scene_.get(), {ray.origin.x, ray.origin.y, ray.origin.z},
                              {ray.direction.x, ray.direction.y, ray.direction.z}, &hit)) {
    const auto delta = subtract({hit.x, hit.y, hit.z}, ray.origin);
    const double distance = std::sqrt(dot(delta, delta));
    if (distance < nearest) {
      nearest = distance;
      terrain = false;
      sceneHit = true;
    }
  }
  if (!std::isfinite(nearest))
    return {};
  GEGeographicPoint p{};
  if (!ge_wgs84_from_ecef(add(ray.origin, scale(ray.direction, nearest)), &p))
    return {};
  return SurfaceHit{{p.latitude, p.longitude}, p.height, terrain, sceneHit};
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
  if (!selecting_ && !pointCaptureOwner_) {
    QString selected;
    double nearest = 12;
    for (auto layer = markers_.cbegin(); layer != markers_.cend(); ++layer)
      for (const auto &marker : layer->markers)
        if (!marker.id.isEmpty())
          if (auto point = screenPoint(marker.coordinate, marker.altitude)) {
            const auto d = QLineF(*point, event->position()).length();
            if (d < nearest) {
              nearest = d;
              selected = marker.id;
            }
          }
    if (!selected.isEmpty()) {
      emit markerSelected(selected);
      return;
    }
  }
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
    auto *owner = regionOwner_.data();
    cancelRegion();
    emit regionSelected(b, owner);
  } else {
    if (pointCaptureOwner_) {
      emit pointSelected(hit->coordinate, hit->height, hit->terrain, hit->scene);
      return;
    }
    emit pointSelected(hit->coordinate, hit->height, hit->terrain, hit->scene);
  }
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
  ++markerRevision_;
  selectionTimer_.start();
  emit viewportChanged();
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
} // namespace helios

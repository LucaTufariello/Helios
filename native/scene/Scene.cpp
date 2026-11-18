#include "HeliosScene.h"
#include <Cesium3DTilesContent/registerAllTileContentTypes.h>
#include <Cesium3DTilesSelection/BoundingVolume.h>
#include <Cesium3DTilesSelection/IPrepareRendererResources.h>
#include <Cesium3DTilesSelection/Tile.h>
#include <Cesium3DTilesSelection/Tileset.h>
#include <Cesium3DTilesSelection/ViewState.h>
#include <CesiumAsync/IAssetAccessor.h>
#include <CesiumAsync/IAssetResponse.h>
#include <CesiumAsync/ITaskProcessor.h>
#include <CesiumGeometry/IntersectionTests.h>
#include <CesiumGeometry/Ray.h>
#include <CesiumGltf/AccessorView.h>
#include <CesiumGltf/ExtensionKhrTextureTransform.h>
#include <CesiumGltfContent/GltfUtilities.h>
#include <CesiumUtility/CreditSystem.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <spdlog/sinks/null_sink.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "SceneTransport.h"

namespace {
std::atomic<uint64_t> nextID{1};
glm::dvec3 vector(GEPoint p) {
  return {p.x, p.y, p.z};
}
GEPoint point(glm::dvec3 p) {
  return {p.x, p.y, p.z};
}
struct Texture {
  uint64_t id = nextID++;
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> pixels;
};
struct Primitive {
  GEMesh description{};
  std::vector<GEVertex> vertices;
  std::vector<uint32_t> indices;
  std::shared_ptr<Texture> texture;
};
struct Prepared {
  std::vector<Primitive> primitives;
};
class Prepare final : public Cesium3DTilesSelection::IPrepareRendererResources {
public:
  std::atomic<uint32_t> failures{0};
  CesiumAsync::Future<Cesium3DTilesSelection::TileLoadResultAndRenderResources>
  prepareInLoadThread(const CesiumAsync::AsyncSystem &async,
                      Cesium3DTilesSelection::TileLoadResult &&result, const glm::dmat4 &transform,
                      const std::any &) override {
    auto prepared = std::make_unique<Prepared>();
    try {
      if (auto *model = std::get_if<CesiumGltf::Model>(&result.contentKind)) {
        const auto root = CesiumGltfContent::GltfUtilities::applyGltfUpAxisTransform(
            *model, CesiumGltfContent::GltfUtilities::applyRtcCenter(*model, transform));
        std::unordered_map<int32_t, std::shared_ptr<Texture>> images;
        size_t cost = 0;
        model->forEachPrimitiveInScene(-1, [&](const CesiumGltf::Model &m, const CesiumGltf::Node &,
                                               const CesiumGltf::Mesh &,
                                               const CesiumGltf::MeshPrimitive &primitive,
                                               const glm::dmat4 &node) {
          if (primitive.mode != 4)
            throw std::runtime_error(
                "Only triangle meshes are supported by the native scene renderer");
          auto position = primitive.attributes.find("POSITION");
          if (position == primitive.attributes.end())
            throw std::runtime_error("Missing mesh positions");
          CesiumGltf::AccessorView<glm::vec3> positions(m, position->second);
          if (positions.status() != CesiumGltf::AccessorViewStatus::Valid ||
              positions.size() <= 0 || positions.size() > 2'000'000)
            throw std::runtime_error("Invalid or oversized position accessor");
          const auto world = root * node;
          glm::dvec3 anchor(world * glm::dvec4(positions[0], 1));
          if (!std::isfinite(glm::length(anchor)) || glm::length(anchor) > 1e9)
            throw std::runtime_error("Invalid georeferencing");
          Primitive mesh;
          mesh.description.id = nextID++;
          mesh.description.origin = point(anchor);
          mesh.description.red = mesh.description.green = mesh.description.blue =
              mesh.description.alpha = 1;
          mesh.description.alphaCutoff = -1;
          mesh.description.wrapS = mesh.description.wrapT = 10497;
          mesh.description.magFilter = 9729;
          mesh.description.minFilter = 9987;
          glm::dmat3 uvTransform(1);
          auto uvAttribute = primitive.attributes.find("TEXCOORD_0");
          CesiumGltf::AccessorView<glm::vec2> uv(
              m, uvAttribute == primitive.attributes.end() ? -1 : uvAttribute->second);
          if (primitive.material >= 0 && size_t(primitive.material) < m.materials.size()) {
            const auto &material = m.materials[primitive.material];
            mesh.description.doubleSided = material.doubleSided;
            mesh.description.blend = material.alphaMode == "BLEND";
            if (material.alphaMode == "MASK")
              mesh.description.alphaCutoff = float(material.alphaCutoff);
            if (material.pbrMetallicRoughness) {
              const auto &pbr = *material.pbrMetallicRoughness;
              if (pbr.baseColorFactor.size() == 4) {
                mesh.description.red = float(pbr.baseColorFactor[0]);
                mesh.description.green = float(pbr.baseColorFactor[1]);
                mesh.description.blue = float(pbr.baseColorFactor[2]);
                mesh.description.alpha = float(pbr.baseColorFactor[3]);
              }
              if (pbr.baseColorTexture) {
                const auto &info = *pbr.baseColorTexture;
                int32_t texCoord = info.texCoord;
                if (auto *extension =
                        info.getExtension<CesiumGltf::ExtensionKhrTextureTransform>()) {
                  if (extension->texCoord)
                    texCoord = *extension->texCoord;
                  if (extension->offset.size() != 2 || extension->scale.size() != 2 ||
                      !std::isfinite(extension->rotation))
                    throw std::runtime_error("Invalid texture transform");
                  const double c = cos(extension->rotation), s = sin(extension->rotation);
                  uvTransform =
                      glm::dmat3(glm::dvec3(c * extension->scale[0], s * extension->scale[0], 0),
                                 glm::dvec3(-s * extension->scale[1], c * extension->scale[1], 0),
                                 glm::dvec3(extension->offset[0], extension->offset[1], 1));
                }
                auto uvAttribute =
                    primitive.attributes.find("TEXCOORD_" + std::to_string(texCoord));
                uv = CesiumGltf::AccessorView<glm::vec2>(
                    m, uvAttribute == primitive.attributes.end() ? -1 : uvAttribute->second);
                if (uv.status() != CesiumGltf::AccessorViewStatus::Valid ||
                    uv.size() != positions.size())
                  throw std::runtime_error("Missing material texture coordinates");
                if (info.index < 0 || size_t(info.index) >= m.textures.size())
                  throw std::runtime_error("Invalid material texture");
                const auto &sourceTexture = m.textures[info.index];
                if (sourceTexture.sampler >= 0 &&
                    size_t(sourceTexture.sampler) < m.samplers.size()) {
                  const auto &sampler = m.samplers[sourceTexture.sampler];
                  mesh.description.wrapS = sampler.wrapS;
                  mesh.description.wrapT = sampler.wrapT;
                  mesh.description.magFilter = sampler.magFilter.value_or(9729);
                  mesh.description.minFilter = sampler.minFilter.value_or(9987);
                }
                int32_t imageIndex = sourceTexture.source;
                if (!images.contains(imageIndex)) {
                  if (imageIndex < 0 || size_t(imageIndex) >= m.images.size() ||
                      !m.images[imageIndex].pAsset)
                    throw std::runtime_error("Texture data unavailable");
                  const auto &image = *m.images[imageIndex].pAsset;
                  if (image.width <= 0 || image.height <= 0 || image.width > 8192 ||
                      image.height > 8192 || image.bytesPerChannel != 1 || image.channels < 1 ||
                      image.channels > 4 ||
                      image.compressedPixelFormat != CesiumImage::GpuCompressedPixelFormat::NONE)
                    throw std::runtime_error("Unsupported or oversized texture");
                  size_t count = size_t(image.width) * size_t(image.height);
                  if (count * size_t(image.channels) > image.pixelData.size() ||
                      cost + count * 4 > 64 * 1024 * 1024)
                    throw std::runtime_error("Texture memory budget exceeded");
                  auto texture = std::make_shared<Texture>();
                  texture->width = image.width;
                  texture->height = image.height;
                  texture->pixels.resize(count * 4);
                  for (size_t i = 0; i < count; ++i) {
                    const uint8_t *p = reinterpret_cast<const uint8_t *>(image.pixelData.data()) +
                                       i * image.channels;
                    auto *q = texture->pixels.data() + i * 4;
                    q[0] = p[0];
                    q[1] = image.channels > 2 ? p[1] : p[0];
                    q[2] = image.channels > 2 ? p[2] : p[0];
                    q[3] = image.channels == 2 ? p[1] : image.channels == 4 ? p[3] : 255;
                  }
                  cost += count * 4;
                  images[imageIndex] = texture;
                }
                mesh.texture = images[imageIndex];
                mesh.description.textureID = mesh.texture->id;
                mesh.description.width = mesh.texture->width;
                mesh.description.height = mesh.texture->height;
              }
            }
          }
          cost += size_t(positions.size()) * sizeof(GEVertex);
          if (cost > 64 * 1024 * 1024)
            throw std::runtime_error("Geometry memory budget exceeded");
          mesh.vertices.reserve(positions.size());
          for (int64_t i = 0; i < positions.size(); ++i) {
            const glm::dvec3 p = glm::dvec3(world * glm::dvec4(positions[i], 1)) - anchor;
            const auto rawUV = uv.status() == CesiumGltf::AccessorViewStatus::Valid && i < uv.size()
                                   ? uv[i]
                                   : glm::vec2(0);
            const auto coords = uvTransform * glm::dvec3(rawUV, 1);
            if (!std::isfinite(glm::length(p)) || glm::length(p) > 1e8 ||
                !std::isfinite(coords.x) || !std::isfinite(coords.y))
              throw std::runtime_error("Invalid vertex");
            mesh.vertices.push_back({float(p.x / 6378137), float(p.y / 6378137),
                                     float(p.z / 6378137), float(coords.x), float(coords.y)});
          }
          if (primitive.indices < 0) {
            for (uint32_t i = 0; i < mesh.vertices.size(); ++i)
              mesh.indices.push_back(i);
          } else {
            if (size_t(primitive.indices) >= m.accessors.size())
              throw std::runtime_error("Invalid mesh indices");
            const auto &accessor = m.accessors[primitive.indices];
            auto read = [&]<typename T>() {
              CesiumGltf::AccessorView<T> indices(m, primitive.indices);
              if (indices.status() != CesiumGltf::AccessorViewStatus::Valid ||
                  indices.size() > 6'000'000)
                throw std::runtime_error("Invalid index accessor");
              for (int64_t i = 0; i < indices.size(); ++i) {
                if (indices[i] >= mesh.vertices.size())
                  throw std::runtime_error("Mesh index out of range");
                mesh.indices.push_back(indices[i]);
              }
            };
            switch (accessor.componentType) {
            case 5121:
              read.operator()<uint8_t>();
              break;
            case 5123:
              read.operator()<uint16_t>();
              break;
            case 5125:
              read.operator()<uint32_t>();
              break;
            default:
              throw std::runtime_error("Unsupported index component type");
            }
          }
          if (mesh.indices.empty() || mesh.indices.size() % 3 != 0)
            throw std::runtime_error("Invalid triangle index count");
          cost += mesh.indices.size() * 4;
          if (cost > 64 * 1024 * 1024)
            throw std::runtime_error("Geometry memory budget exceeded");
          mesh.description.vertexCount = uint32_t(mesh.vertices.size());
          mesh.description.indexCount = uint32_t(mesh.indices.size());
          prepared->primitives.push_back(std::move(mesh));
        });
      }
    } catch (...) {
      ++failures;
      prepared->primitives.clear();
    }
    return async.createResolvedFuture(Cesium3DTilesSelection::TileLoadResultAndRenderResources{
        std::move(result), prepared.release()});
  }
  void *prepareInMainThread(Cesium3DTilesSelection::Tile &, void *result) override {
    return result;
  }
  void free(Cesium3DTilesSelection::Tile &, void *load, void *main) noexcept override {
    delete static_cast<Prepared *>(load);
    if (main != load)
      delete static_cast<Prepared *>(main);
  }
  void *prepareRasterInLoadThread(CesiumImage::ImageAsset &, const std::any &) override {
    return nullptr;
  }
  void *prepareRasterInMainThread(CesiumRasterOverlays::RasterOverlayTile &, void *) override {
    return nullptr;
  }
  void freeRaster(const CesiumRasterOverlays::RasterOverlayTile &, void *,
                  void *) noexcept override {}
  void attachRasterInMainThread(const Cesium3DTilesSelection::Tile &, int32_t,
                                const CesiumRasterOverlays::RasterOverlayTile &, void *,
                                const glm::dvec2 &, const glm::dvec2 &) override {}
  void detachRasterInMainThread(const Cesium3DTilesSelection::Tile &, int32_t,
                                const CesiumRasterOverlays::RasterOverlayTile &,
                                void *) noexcept override {}
};
} // namespace

struct GEScene {
  std::shared_ptr<CesiumAsync::ITaskProcessor> jobs = helios::scene::makeJobs();
  CesiumAsync::AsyncSystem async{jobs};
  std::shared_ptr<helios::scene::Assets> assets;
  std::shared_ptr<Prepare> prepare = std::make_shared<Prepare>();
  std::shared_ptr<CesiumUtility::CreditSystem> credits =
      std::make_shared<CesiumUtility::CreditSystem>();
  std::unique_ptr<Cesium3DTilesSelection::Tileset> tileset;
  std::vector<const Cesium3DTilesSelection::Tile *> visible;
  std::vector<Primitive *> meshes;
  std::unordered_set<uint64_t> renderedMeshes;
  bool hasRenderMask = false;
  std::string creditText;
};
extern "C" GEScene *GE_scene_create(const char *urlString) {
  try {
    if (!urlString)
      return nullptr;
    auto assets = helios::scene::makeAssets(urlString);
    if (!assets)
      return nullptr;
    Cesium3DTilesContent::registerAllTileContentTypes();
    auto scene = std::make_unique<GEScene>();
    scene->assets = std::move(assets);
    Cesium3DTilesSelection::TilesetExternals externals{scene->assets, scene->prepare, scene->async,
                                                       scene->credits};
    externals.pLogger = std::make_shared<spdlog::logger>(
        "HeliosTiles", std::make_shared<spdlog::sinks::null_sink_mt>());
    Cesium3DTilesSelection::TilesetOptions options;
    options.maximumCachedBytes = 128 * 1024 * 1024;
    options.maximumSimultaneousTileLoads = 4;
    options.preloadAncestors = false;
    options.preloadSiblings = false;
    options.maximumScreenSpaceError = 16;
    options.enableLodTransitionPeriod = false;
    options.enableFogCulling = false;
    options.contentOptions.applyTextureTransform = false;
    options.contentOptions.primitiveModeOptions.convertTriangleStrip = true;
    options.contentOptions.primitiveModeOptions.convertTriangleFan = true;
    auto prepare = scene->prepare;
    options.loadErrorCallback = [prepare](const auto &) { ++prepare->failures; };
    scene->tileset = std::make_unique<Cesium3DTilesSelection::Tileset>(
        externals, std::string(urlString), options);
    return scene.release();
  } catch (...) {
    return nullptr;
  }
}
extern "C" void GE_scene_destroy(GEScene *scene) {
  if (!scene)
    return;
  auto complete = scene->tileset->getAsyncDestructionCompleteEvent();
  scene->assets->cancel();
  scene->tileset.reset();
  // Keep asynchronous content/resource ownership alive until all cancelled loads drain.
  while (!complete.isReady()) {
    scene->async.dispatchMainThreadTasks();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  scene->async.dispatchMainThreadTasks();
  delete scene;
}
extern "C" uint32_t GE_scene_update(GEScene *scene, GEPoint eye, GEPoint forward, GEPoint up,
                                    double width, double height) {
  if (!scene || width <= 0 || height <= 0)
    return 0;
  try {
    auto view = Cesium3DTilesSelection::ViewState(
        vector(eye), vector(forward), vector(up), glm::dvec2(width, height),
        2 * atan(tan(glm::pi<double>() / 8) * width / height), glm::pi<double>() / 4);
    scene->async.dispatchMainThreadTasks();
    const auto &update =
        scene->tileset->updateViewGroup(scene->tileset->getDefaultViewGroup(), {view});
    scene->async.dispatchMainThreadTasks();
    scene->tileset->loadTiles();
    scene->visible.clear();
    scene->meshes.clear();
    for (const auto &tile : update.tilesToRenderThisFrame) {
      const auto *content = tile->getContent().getRenderContent();
      if (!content)
        continue;
      const auto *prepared = static_cast<Prepared *>(content->getRenderResources());
      if (!prepared || prepared->primitives.empty())
        continue;
      scene->visible.push_back(tile.get());
      for (const auto &mesh : prepared->primitives)
        scene->meshes.push_back(const_cast<Primitive *>(&mesh));
    }
    scene->creditText.clear();
    for (auto credit : scene->credits->getSnapshot().currentCredits) {
      scene->creditText += scene->credits->getHtml(credit);
      scene->creditText += "\n";
    }
    return uint32_t(scene->meshes.size());
  } catch (...) {
    ++scene->prepare->failures;
    return 0;
  }
}
extern "C" int GE_scene_mesh(GEScene *scene, uint32_t index, GEMesh *out) {
  if (!scene || !out || index >= scene->meshes.size())
    return 0;
  const auto &mesh = *scene->meshes[index];
  *out = mesh.description;
  out->vertices = mesh.vertices.data();
  out->indices = mesh.indices.data();
  out->pixels = mesh.texture ? mesh.texture->pixels.data() : nullptr;
  return 1;
}
extern "C" void GE_scene_rendered_meshes(GEScene *scene, const uint64_t *ids, uint32_t count) {
  if (!scene)
    return;
  scene->hasRenderMask = true;
  scene->renderedMeshes.clear();
  if (ids && count)
    scene->renderedMeshes.insert(ids, ids + count);
}
extern "C" int GE_scene_pick(GEScene *scene, GEPoint origin, GEPoint direction, GEPoint *out) {
  if (!scene || !out || !std::isfinite(glm::length(vector(direction))) ||
      glm::length(vector(direction)) < 1e-12)
    return 0;
  const auto o = vector(origin), d = glm::normalize(vector(direction));
  double nearest = INFINITY;
  CesiumGeometry::Ray ray(o, d);
  for (auto *tile : scene->visible) {
    if (!CesiumGeometry::IntersectionTests::rayOBB(
            ray, Cesium3DTilesSelection::getOrientedBoundingBoxFromBoundingVolume(
                     tile->getBoundingVolume())))
      continue;
    const auto *content = tile->getContent().getRenderContent();
    if (!content)
      continue;
    const auto *prepared = static_cast<Prepared *>(content->getRenderResources());
    if (!prepared)
      continue;
    for (const auto &mesh : prepared->primitives) {
      const auto &description = mesh.description;
      if (scene->hasRenderMask && !scene->renderedMeshes.contains(description.id))
        continue;
      auto position = [&](uint32_t index) {
        const auto &p = mesh.vertices[index];
        return vector(description.origin) + glm::dvec3(p.x, p.y, p.z) * 6378137.0;
      };
      for (size_t index = 0; index < mesh.indices.size(); index += 3) {
        const auto ia = mesh.indices[index], ib = mesh.indices[index + 1],
                   ic = mesh.indices[index + 2];
        const auto a = position(ia), b = position(ib), c = position(ic), e1 = b - a, e2 = c - a;
        const auto p = glm::cross(d, e2);
        const double det = glm::dot(e1, p);
        if (description.doubleSided ? fabs(det) < 1e-10 : det < 1e-10)
          continue;
        const auto t = o - a;
        const double u = glm::dot(t, p) / det;
        if (u < 0 || u > 1)
          continue;
        const auto q = glm::cross(t, e1);
        const double v = glm::dot(d, q) / det;
        if (v < 0 || u + v > 1)
          continue;
        const double distance = glm::dot(e2, q) / det;
        if (distance < 0 || distance >= nearest)
          continue;
        if (description.alphaCutoff >= 0 || description.blend) {
          double alpha = description.alpha;
          if (mesh.texture) {
            const auto &texture = *mesh.texture;
            const auto &va = mesh.vertices[ia];
            const auto &vb = mesh.vertices[ib];
            const auto &vc = mesh.vertices[ic];
            double tu = va.u * (1 - u - v) + vb.u * u + vc.u * v,
                   tv = va.v * (1 - u - v) + vb.v * u + vc.v * v;
            auto wrap = [](double value, int32_t code) {
              if (code == 33071)
                return glm::clamp(value, 0.0, 1.0);
              if (code == 33648) {
                double r = fmod(value, 2.0);
                if (r < 0)
                  r += 2;
                return r <= 1 ? r : 2 - r;
              }
              return value - floor(value);
            };
            tu = wrap(tu, description.wrapS);
            tv = wrap(tv, description.wrapT);
            auto texel = [&](int x, int y) {
              return double(texture.pixels[(size_t(glm::clamp(y, 0, int(texture.height) - 1)) *
                                                texture.width +
                                            glm::clamp(x, 0, int(texture.width) - 1)) *
                                               4 +
                                           3]) /
                     255;
            };
            if (description.magFilter == 9728)
              alpha *= texel(int(tu * texture.width), int(tv * texture.height));
            else {
              double x = tu * texture.width - 0.5, y = tv * texture.height - 0.5;
              int ix = int(floor(x)), iy = int(floor(y));
              double fx = x - ix, fy = y - iy;
              alpha *= glm::mix(glm::mix(texel(ix, iy), texel(ix + 1, iy), fx),
                                glm::mix(texel(ix, iy + 1), texel(ix + 1, iy + 1), fx), fy);
            }
          }
          if ((description.alphaCutoff >= 0 && alpha < description.alphaCutoff) || alpha <= 0)
            continue;
        }
        nearest = distance;
      }
    }
  }
  if (!std::isfinite(nearest))
    return 0;
  *out = point(o + d * nearest);
  return 1;
}
extern "C" float GE_scene_progress(GEScene *scene) {
  return scene ? scene->tileset->computeLoadProgress() : 0;
}
extern "C" uint32_t GE_scene_failures(GEScene *scene) {
  return scene ? scene->assets->failures + scene->prepare->failures : 0;
}
extern "C" const char *GE_scene_credits(GEScene *scene) {
  return scene ? scene->creditText.c_str() : "";
}
extern "C" int GE_scene_center(GEScene *scene, GEPoint *center, double *radius) {
  if (!scene || !center || !radius || !scene->tileset->getRootTile())
    return 0;
  const auto &volume = scene->tileset->getRootTile()->getBoundingVolume();
  auto box = Cesium3DTilesSelection::getOrientedBoundingBoxFromBoundingVolume(volume);
  *center = point(Cesium3DTilesSelection::getBoundingVolumeCenter(volume));
  *radius = glm::length(box.getLengths()) / 2;
  return 1;
}

#import <Foundation/Foundation.h>
#include "GodEyeScene.h"
#include <Cesium3DTilesSelection/Tileset.h>
#include <Cesium3DTilesSelection/Tile.h>
#include <Cesium3DTilesSelection/IPrepareRendererResources.h>
#include <Cesium3DTilesSelection/ViewState.h>
#include <Cesium3DTilesSelection/BoundingVolume.h>
#include <Cesium3DTilesContent/registerAllTileContentTypes.h>
#include <CesiumAsync/IAssetAccessor.h>
#include <CesiumAsync/IAssetResponse.h>
#include <CesiumAsync/ITaskProcessor.h>
#include <CesiumGltf/AccessorView.h>
#include <CesiumGltf/ExtensionKhrTextureTransform.h>
#include <CesiumGltfContent/GltfUtilities.h>
#include <CesiumGeometry/Ray.h>
#include <CesiumGeometry/IntersectionTests.h>
#include <CesiumUtility/CreditSystem.h>
#include <spdlog/sinks/null_sink.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr size_t maxBody = 32 * 1024 * 1024;
std::atomic<uint64_t> nextID{1};
glm::dvec3 vector(GEPoint p) { return {p.x,p.y,p.z}; }
GEPoint point(glm::dvec3 p) { return {p.x,p.y,p.z}; }
class Jobs final : public CesiumAsync::ITaskProcessor {
  NSOperationQueue* queue = [NSOperationQueue new];
public:
  Jobs() { queue.maxConcurrentOperationCount = 4; queue.qualityOfService = NSQualityOfServiceUserInitiated; }
  void startTask(std::function<void()> f) override {
    auto job = std::make_shared<std::function<void()>>(std::move(f));
    [queue addOperationWithBlock:^{ @autoreleasepool { (*job)(); } }];
  }
};
class Response final : public CesiumAsync::IAssetRequest, public CesiumAsync::IAssetResponse {
public:
  std::string address, verb="GET", type;
  CesiumAsync::HttpHeaders fields;
  std::vector<std::byte> body;
  uint16_t status = 200;
  const std::string& method() const override { return verb; }
  const std::string& url() const override { return address; }
  const CesiumAsync::HttpHeaders& headers() const override { return fields; }
  const CesiumAsync::IAssetResponse* response() const override { return this; }
  uint16_t statusCode() const override { return status; }
  std::string contentType() const override { return type; }
  std::span<const std::byte> data() const override { return body; }
};
}

// NSURLSession owns the bounded streaming transport; Cesium never starts a web server.
@interface GEAssetFetch : NSObject <NSURLSessionDataDelegate> {
@public
  std::function<void(NSData*, NSHTTPURLResponse*, NSError*)> complete;
  NSMutableData* bytes;
  NSHTTPURLResponse* response;
  NSURLSession* session;
  NSString* host;
  NSNumber* port;
  BOOL tooLarge;
}
@end
@implementation GEAssetFetch
- (void)URLSession:(NSURLSession*)s dataTask:(NSURLSessionDataTask*)task didReceiveResponse:(NSURLResponse*)r completionHandler:(void (^)(NSURLSessionResponseDisposition))callback {
  response = (NSHTTPURLResponse*)r;
  tooLarge = r.expectedContentLength > (long long)maxBody;
  callback(tooLarge ? NSURLSessionResponseCancel : NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession*)s dataTask:(NSURLSessionDataTask*)task didReceiveData:(NSData*)data {
  if (data.length > maxBody || bytes.length > maxBody - data.length) { tooLarge=YES; [task cancel]; return; }
  [bytes appendData:data];
}
- (void)URLSession:(NSURLSession*)s task:(NSURLSessionTask*)task willPerformHTTPRedirection:(NSHTTPURLResponse*)r newRequest:(NSURLRequest*)request completionHandler:(void (^)(NSURLRequest*))callback {
  // An asset cannot move requests or credentials to a different origin.
  BOOL allowed = [request.URL.scheme.lowercaseString isEqualToString:@"https"] && [request.URL.host.lowercaseString isEqualToString:host] && ((request.URL.port == port) || [request.URL.port isEqualToNumber:port]);
  callback(allowed ? request : nil);
}
- (void)URLSession:(NSURLSession*)s task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error {
  if (tooLarge) error = [NSError errorWithDomain:@"GodEyeScene" code:413 userInfo:nil];
  if (complete) complete(bytes, response, error);
  complete = {};
  [s finishTasksAndInvalidate];
  session = nil;
}
@end

namespace {
class Assets final : public CesiumAsync::IAssetAccessor, public std::enable_shared_from_this<Assets> {
  NSURL* root;
  NSString* directory;
  NSMutableSet<NSURLSession*>* sessions = [NSMutableSet new];
  std::mutex mutex;
  bool cancelled = false;
public:
  std::atomic<uint32_t> failures{0};
  Assets(NSURL* url) : root(url) {
    directory = [[url URLByDeletingLastPathComponent].URLByResolvingSymlinksInPath.path stringByAppendingString:@"/"];
  }
  bool allows(NSURL* url) {
    if (!url || url.user || url.password) return false;
    if (root.isFileURL) return url.isFileURL && [url.URLByResolvingSymlinksInPath.path hasPrefix:directory];
    return [url.scheme.lowercaseString isEqualToString:@"https"] && [url.host.lowercaseString isEqualToString:root.host.lowercaseString] && ((url.port == root.port) || [url.port isEqualToNumber:root.port]);
  }
  void cancel() {
    std::lock_guard lock(mutex);
    cancelled = true;
    for (NSURLSession* session in sessions) [session invalidateAndCancel];
  }
  CesiumAsync::Future<std::shared_ptr<CesiumAsync::IAssetRequest>> get(const CesiumAsync::AsyncSystem& async, const std::string& address, const std::vector<THeader>& headers) override {
    auto promise = async.createPromise<std::shared_ptr<CesiumAsync::IAssetRequest>>();
    auto future = promise.getFuture();
    NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:address.c_str()]];
    if (!allows(url)) { ++failures; promise.reject(std::runtime_error("3D tile URL is outside the selected origin/directory")); return future; }
    if (url.isFileURL) {
      auto self = shared_from_this();
      return async.runInWorkerThread([url,address,self]() mutable -> std::shared_ptr<CesiumAsync::IAssetRequest> {
        @autoreleasepool {
          NSNumber* size = nil;
          [url getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
          if (!size || size.unsignedLongLongValue > maxBody) { ++self->failures; throw std::runtime_error("Local 3D tile exceeds 32 MB or is missing"); }
          NSData* data = [NSData dataWithContentsOfURL:url options:NSDataReadingMappedIfSafe error:nil];
          if (!data || data.length > maxBody) { ++self->failures; throw std::runtime_error("Local 3D tile could not be read"); }
          auto result = std::make_shared<Response>(); result->address = address;
          auto* begin = static_cast<const std::byte*>(data.bytes);
          result->body.assign(begin, begin + data.length);
          return result;
        }
      });
    }
    GEAssetFetch* fetch = [GEAssetFetch new]; fetch->bytes = [NSMutableData new];
    fetch->host = root.host.lowercaseString; fetch->port = root.port;
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
    request.timeoutInterval = 30;
    [request setValue:@"GodEyeNative/1.0 (3D Tiles)" forHTTPHeaderField:@"User-Agent"];
    for (const auto& header : headers) [request setValue:[NSString stringWithUTF8String:header.second.c_str()] forHTTPHeaderField:[NSString stringWithUTF8String:header.first.c_str()]];
    auto self = shared_from_this();
    auto configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    configuration.HTTPMaximumConnectionsPerHost = 4;
    configuration.timeoutIntervalForResource = 45;
    configuration.HTTPCookieStorage = nil;
    fetch->session = [NSURLSession sessionWithConfiguration:configuration delegate:fetch delegateQueue:nil];
    NSURLSession* session = fetch->session;
    fetch->complete = [self,session,address,promise](NSData* data, NSHTTPURLResponse* response, NSError* error) mutable {
      { std::lock_guard lock(self->mutex); [self->sessions removeObject:session]; }
      if (error || !response || response.statusCode < 200 || response.statusCode >= 300) {
        ++self->failures;
        promise.reject(std::runtime_error(error ? "3D tile transport failed or exceeded its limit" : "3D tile server returned an unsuccessful status"));
      } else {
        auto result = std::make_shared<Response>(); result->address=address; result->status=(uint16_t)response.statusCode;
        result->type = response.MIMEType.UTF8String ?: "";
        for (NSString* name in response.allHeaderFields) result->fields[name.UTF8String] = [[response.allHeaderFields[name] description] UTF8String];
        const auto* begin = static_cast<const std::byte*>(data.bytes);
        result->body.assign(begin, begin + data.length);
        promise.resolve(result);
      }
    };
    { std::lock_guard lock(mutex); if (cancelled) { promise.reject(std::runtime_error("3D tile session stopped")); fetch->complete={}; [session invalidateAndCancel]; return future; } [sessions addObject:session]; }
    [[session dataTaskWithRequest:request] resume];
    return future;
  }
  CesiumAsync::Future<std::shared_ptr<CesiumAsync::IAssetRequest>> request(const CesiumAsync::AsyncSystem& async, const std::string&, const std::string&, const std::vector<THeader>&, const std::span<const std::byte>&) override {
    auto promise = async.createPromise<std::shared_ptr<CesiumAsync::IAssetRequest>>();
    promise.reject(std::runtime_error("3D Tiles transport is read-only")); return promise.getFuture();
  }
  void tick() noexcept override {}
};

struct Texture { uint64_t id=nextID++; uint32_t width=0,height=0; std::vector<uint8_t> pixels; };
struct Primitive { GEMesh description{}; std::vector<GEVertex> vertices; std::vector<uint32_t> indices; std::shared_ptr<Texture> texture; };
struct Prepared { std::vector<Primitive> primitives; };
class Prepare final : public Cesium3DTilesSelection::IPrepareRendererResources {
public:
  std::atomic<uint32_t> failures{0};
  CesiumAsync::Future<Cesium3DTilesSelection::TileLoadResultAndRenderResources> prepareInLoadThread(const CesiumAsync::AsyncSystem& async, Cesium3DTilesSelection::TileLoadResult&& result, const glm::dmat4& transform, const std::any&) override {
    auto prepared = std::make_unique<Prepared>();
    try {
      if (auto* model = std::get_if<CesiumGltf::Model>(&result.contentKind)) {
        const auto root = CesiumGltfContent::GltfUtilities::applyGltfUpAxisTransform(*model, CesiumGltfContent::GltfUtilities::applyRtcCenter(*model,transform));
        std::unordered_map<int32_t,std::shared_ptr<Texture>> images;
        size_t cost = 0;
        model->forEachPrimitiveInScene(-1, [&](const CesiumGltf::Model& m, const CesiumGltf::Node&, const CesiumGltf::Mesh&, const CesiumGltf::MeshPrimitive& primitive, const glm::dmat4& node) {
          if (primitive.mode != 4) throw std::runtime_error("Only triangle meshes are supported by the Metal scene renderer");
          auto position = primitive.attributes.find("POSITION");
          if (position == primitive.attributes.end()) throw std::runtime_error("Missing mesh positions");
          CesiumGltf::AccessorView<glm::vec3> positions(m,position->second);
          if (positions.status() != CesiumGltf::AccessorViewStatus::Valid || positions.size() <= 0 || positions.size() > 2'000'000) throw std::runtime_error("Invalid or oversized position accessor");
          const auto world = root * node;
          glm::dvec3 anchor(world * glm::dvec4(positions[0],1));
          if (!std::isfinite(glm::length(anchor)) || glm::length(anchor)>1e9) throw std::runtime_error("Invalid georeferencing");
          Primitive mesh;
          mesh.description.id = nextID++; mesh.description.origin = point(anchor);
          mesh.description.red=mesh.description.green=mesh.description.blue=mesh.description.alpha=1;
          mesh.description.alphaCutoff=-1;
          mesh.description.wrapS=mesh.description.wrapT=10497;
          mesh.description.magFilter=9729; mesh.description.minFilter=9987;
          glm::dmat3 uvTransform(1);
          auto uvAttribute=primitive.attributes.find("TEXCOORD_0");
          CesiumGltf::AccessorView<glm::vec2> uv(m,uvAttribute==primitive.attributes.end() ? -1 : uvAttribute->second);
          if (primitive.material>=0 && size_t(primitive.material)<m.materials.size()) {
            const auto& material=m.materials[primitive.material];
            mesh.description.doubleSided=material.doubleSided;
            mesh.description.blend=material.alphaMode=="BLEND";
            if (material.alphaMode=="MASK") mesh.description.alphaCutoff=float(material.alphaCutoff);
            if (material.pbrMetallicRoughness) {
              const auto& pbr=*material.pbrMetallicRoughness;
              if (pbr.baseColorFactor.size()==4) { mesh.description.red=float(pbr.baseColorFactor[0]); mesh.description.green=float(pbr.baseColorFactor[1]); mesh.description.blue=float(pbr.baseColorFactor[2]); mesh.description.alpha=float(pbr.baseColorFactor[3]); }
              if (pbr.baseColorTexture) {
                const auto& info=*pbr.baseColorTexture;
                int32_t texCoord=info.texCoord;
                if (auto* extension=info.getExtension<CesiumGltf::ExtensionKhrTextureTransform>()) {
                  if (extension->texCoord) texCoord=*extension->texCoord;
                  if (extension->offset.size()!=2 || extension->scale.size()!=2 || !std::isfinite(extension->rotation)) throw std::runtime_error("Invalid texture transform");
                  const double c=cos(extension->rotation), s=sin(extension->rotation);
                  uvTransform=glm::dmat3(glm::dvec3(c*extension->scale[0],s*extension->scale[0],0),glm::dvec3(-s*extension->scale[1],c*extension->scale[1],0),glm::dvec3(extension->offset[0],extension->offset[1],1));
                }
                auto uvAttribute=primitive.attributes.find("TEXCOORD_"+std::to_string(texCoord));
                uv=CesiumGltf::AccessorView<glm::vec2>(m,uvAttribute==primitive.attributes.end() ? -1 : uvAttribute->second);
                if (uv.status()!=CesiumGltf::AccessorViewStatus::Valid || uv.size()!=positions.size()) throw std::runtime_error("Missing material texture coordinates");
                if (info.index<0 || size_t(info.index)>=m.textures.size()) throw std::runtime_error("Invalid material texture");
                const auto& sourceTexture=m.textures[info.index];
                if (sourceTexture.sampler>=0 && size_t(sourceTexture.sampler)<m.samplers.size()) {
                  const auto& sampler=m.samplers[sourceTexture.sampler];
                  mesh.description.wrapS=sampler.wrapS; mesh.description.wrapT=sampler.wrapT;
                  mesh.description.magFilter=sampler.magFilter.value_or(9729); mesh.description.minFilter=sampler.minFilter.value_or(9987);
                }
                int32_t imageIndex=sourceTexture.source;
                if (!images.contains(imageIndex)) {
                  if (imageIndex<0 || size_t(imageIndex)>=m.images.size() || !m.images[imageIndex].pAsset) throw std::runtime_error("Texture data unavailable");
                  const auto& image=*m.images[imageIndex].pAsset;
                  if (image.width<=0 || image.height<=0 || image.width>8192 || image.height>8192 || image.bytesPerChannel!=1 || image.channels<1 || image.channels>4 || image.compressedPixelFormat!=CesiumImage::GpuCompressedPixelFormat::NONE) throw std::runtime_error("Unsupported or oversized texture");
                  size_t count=size_t(image.width)*size_t(image.height);
                  if (count*size_t(image.channels)>image.pixelData.size() || cost+count*4>64*1024*1024) throw std::runtime_error("Texture memory budget exceeded");
                  auto texture=std::make_shared<Texture>(); texture->width=image.width; texture->height=image.height;
                  texture->pixels.resize(count*4);
                  for (size_t i=0;i<count;++i) { const uint8_t* p=reinterpret_cast<const uint8_t*>(image.pixelData.data())+i*image.channels; auto* q=texture->pixels.data()+i*4; q[0]=p[0]; q[1]=image.channels>2?p[1]:p[0]; q[2]=image.channels>2?p[2]:p[0]; q[3]=image.channels==2?p[1]:image.channels==4?p[3]:255; }
                  cost+=count*4; images[imageIndex]=texture;
                }
                mesh.texture=images[imageIndex]; mesh.description.textureID=mesh.texture->id;
                mesh.description.width=mesh.texture->width; mesh.description.height=mesh.texture->height;
              }
            }
          }
          cost+=size_t(positions.size())*sizeof(GEVertex);
          if (cost>64*1024*1024) throw std::runtime_error("Geometry memory budget exceeded");
          mesh.vertices.reserve(positions.size());
          for (int64_t i=0;i<positions.size();++i) {
            const glm::dvec3 p=glm::dvec3(world*glm::dvec4(positions[i],1))-anchor;
            const auto rawUV=uv.status()==CesiumGltf::AccessorViewStatus::Valid && i<uv.size()?uv[i]:glm::vec2(0);
            const auto coords=uvTransform*glm::dvec3(rawUV,1);
            if (!std::isfinite(glm::length(p)) || glm::length(p)>1e8 || !std::isfinite(coords.x) || !std::isfinite(coords.y)) throw std::runtime_error("Invalid vertex");
            mesh.vertices.push_back({float(p.x/6378137),float(p.y/6378137),float(p.z/6378137),float(coords.x),float(coords.y)});
          }
          if (primitive.indices<0) { for (uint32_t i=0;i<mesh.vertices.size();++i) mesh.indices.push_back(i); }
          else {
            if (size_t(primitive.indices)>=m.accessors.size()) throw std::runtime_error("Invalid mesh indices");
            const auto& accessor=m.accessors[primitive.indices];
            auto read=[&]<typename T>() { CesiumGltf::AccessorView<T> indices(m,primitive.indices); if (indices.status()!=CesiumGltf::AccessorViewStatus::Valid || indices.size()>6'000'000) throw std::runtime_error("Invalid index accessor"); for (int64_t i=0;i<indices.size();++i) { if (indices[i]>=mesh.vertices.size()) throw std::runtime_error("Mesh index out of range"); mesh.indices.push_back(indices[i]); } };
            switch (accessor.componentType) { case 5121: read.operator()<uint8_t>(); break; case 5123: read.operator()<uint16_t>(); break; case 5125: read.operator()<uint32_t>(); break; default: throw std::runtime_error("Unsupported index component type"); }
          }
          if (mesh.indices.empty() || mesh.indices.size()%3!=0) throw std::runtime_error("Invalid triangle index count");
          cost+=mesh.indices.size()*4;
          if (cost>64*1024*1024) throw std::runtime_error("Geometry memory budget exceeded");
          mesh.description.vertexCount=uint32_t(mesh.vertices.size()); mesh.description.indexCount=uint32_t(mesh.indices.size());
          prepared->primitives.push_back(std::move(mesh));
        });
      }
    } catch (...) { ++failures; prepared->primitives.clear(); }
    return async.createResolvedFuture(Cesium3DTilesSelection::TileLoadResultAndRenderResources{std::move(result),prepared.release()});
  }
  void* prepareInMainThread(Cesium3DTilesSelection::Tile&, void* result) override { return result; }
  void free(Cesium3DTilesSelection::Tile&, void* load, void* main) noexcept override { delete static_cast<Prepared*>(load); if (main!=load) delete static_cast<Prepared*>(main); }
  void* prepareRasterInLoadThread(CesiumImage::ImageAsset&,const std::any&) override { return nullptr; }
  void* prepareRasterInMainThread(CesiumRasterOverlays::RasterOverlayTile&,void*) override { return nullptr; }
  void freeRaster(const CesiumRasterOverlays::RasterOverlayTile&,void*,void*) noexcept override {}
  void attachRasterInMainThread(const Cesium3DTilesSelection::Tile&,int32_t,const CesiumRasterOverlays::RasterOverlayTile&,void*,const glm::dvec2&,const glm::dvec2&) override {}
  void detachRasterInMainThread(const Cesium3DTilesSelection::Tile&,int32_t,const CesiumRasterOverlays::RasterOverlayTile&,void*) noexcept override {}
};
}

struct GEScene {
  std::shared_ptr<Jobs> jobs=std::make_shared<Jobs>();
  CesiumAsync::AsyncSystem async{jobs};
  std::shared_ptr<Assets> assets;
  std::shared_ptr<Prepare> prepare=std::make_shared<Prepare>();
  std::shared_ptr<CesiumUtility::CreditSystem> credits=std::make_shared<CesiumUtility::CreditSystem>();
  std::unique_ptr<Cesium3DTilesSelection::Tileset> tileset;
  std::vector<const Cesium3DTilesSelection::Tile*> visible;
  std::vector<Primitive*> meshes;
  std::unordered_set<uint64_t> renderedMeshes;
  bool hasRenderMask=false;
  std::string creditText;
};
extern "C" GEScene* GE_scene_create(const char* urlString) {
  try {
    NSURL* url=[NSURL URLWithString:[NSString stringWithUTF8String:urlString]];
    if (!url || (!url.isFileURL && ![url.scheme.lowercaseString isEqualToString:@"https"]) || (!url.isFileURL && !url.host.length) || url.user || url.password) return nullptr;
    Cesium3DTilesContent::registerAllTileContentTypes();
    auto scene=std::make_unique<GEScene>(); scene->assets=std::make_shared<Assets>(url);
    Cesium3DTilesSelection::TilesetExternals externals{scene->assets,scene->prepare,scene->async,scene->credits};
    externals.pLogger=std::make_shared<spdlog::logger>("GodEyeTiles",std::make_shared<spdlog::sinks::null_sink_mt>());
    Cesium3DTilesSelection::TilesetOptions options;
    options.maximumCachedBytes=128*1024*1024; options.maximumSimultaneousTileLoads=4;
    options.preloadAncestors=false; options.preloadSiblings=false; options.maximumScreenSpaceError=16;
    options.enableLodTransitionPeriod=false; options.enableFogCulling=false;
    options.contentOptions.applyTextureTransform=false;
    options.contentOptions.primitiveModeOptions.convertTriangleStrip=true;
    options.contentOptions.primitiveModeOptions.convertTriangleFan=true;
    auto prepare=scene->prepare;
    options.loadErrorCallback=[prepare](const auto&) { ++prepare->failures; };
    scene->tileset=std::make_unique<Cesium3DTilesSelection::Tileset>(externals,std::string(urlString),options);
    return scene.release();
  } catch (...) { return nullptr; }
}
extern "C" void GE_scene_destroy(GEScene* scene) {
  if (!scene) return;
  auto complete=scene->tileset->getAsyncDestructionCompleteEvent();
  scene->assets->cancel(); scene->tileset.reset();
  // Keep asynchronous content/resource ownership alive until all cancelled loads drain.
  while (!complete.isReady()) { scene->async.dispatchMainThreadTasks(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
  scene->async.dispatchMainThreadTasks(); delete scene;
}
extern "C" uint32_t GE_scene_update(GEScene* scene,GEPoint eye,GEPoint forward,GEPoint up,double width,double height) {
  if (!scene || width<=0 || height<=0) return 0;
  try {
    auto view=Cesium3DTilesSelection::ViewState(vector(eye),vector(forward),vector(up),glm::dvec2(width,height),2*atan(tan(glm::pi<double>()/8)*width/height),glm::pi<double>()/4);
    scene->async.dispatchMainThreadTasks();
    const auto& update=scene->tileset->updateViewGroup(scene->tileset->getDefaultViewGroup(),{view});
    scene->async.dispatchMainThreadTasks();
    scene->tileset->loadTiles();
    scene->visible.clear(); scene->meshes.clear();
    for (const auto& tile:update.tilesToRenderThisFrame) {
      const auto* content=tile->getContent().getRenderContent();
      if (!content) continue;
      const auto* prepared=static_cast<Prepared*>(content->getRenderResources());
      if (!prepared || prepared->primitives.empty()) continue;
      scene->visible.push_back(tile.get());
      for (const auto& mesh:prepared->primitives) scene->meshes.push_back(const_cast<Primitive*>(&mesh));
    }
    scene->creditText.clear();
    for (auto credit:scene->credits->getSnapshot().currentCredits) { scene->creditText+=scene->credits->getHtml(credit); scene->creditText+="\n"; }
    return uint32_t(scene->meshes.size());
  } catch (...) { ++scene->prepare->failures; return 0; }
}
extern "C" int GE_scene_mesh(GEScene* scene,uint32_t index,GEMesh* out) {
  if (!scene || !out || index>=scene->meshes.size()) return 0;
  const auto& mesh=*scene->meshes[index]; *out=mesh.description;
  out->vertices=mesh.vertices.data(); out->indices=mesh.indices.data();
  out->pixels=mesh.texture?mesh.texture->pixels.data():nullptr; return 1;
}
extern "C" void GE_scene_rendered_meshes(GEScene* scene,const uint64_t* ids,uint32_t count) {
  if (!scene) return;
  scene->hasRenderMask=true; scene->renderedMeshes.clear();
  if (ids && count) scene->renderedMeshes.insert(ids,ids+count);
}
extern "C" int GE_scene_pick(GEScene* scene,GEPoint origin,GEPoint direction,GEPoint* out) {
  if (!scene || !out || !std::isfinite(glm::length(vector(direction))) || glm::length(vector(direction))<1e-12) return 0;
  const auto o=vector(origin), d=glm::normalize(vector(direction));
  double nearest=INFINITY;
  CesiumGeometry::Ray ray(o,d);
  for (auto* tile:scene->visible) {
    if (!CesiumGeometry::IntersectionTests::rayOBB(ray,Cesium3DTilesSelection::getOrientedBoundingBoxFromBoundingVolume(tile->getBoundingVolume()))) continue;
    const auto* content=tile->getContent().getRenderContent(); if (!content) continue;
    const auto* prepared=static_cast<Prepared*>(content->getRenderResources()); if (!prepared) continue;
    for (const auto& mesh:prepared->primitives) {
      const auto& description=mesh.description;
      if (scene->hasRenderMask && !scene->renderedMeshes.contains(description.id)) continue;
      auto position=[&](uint32_t index) { const auto& p=mesh.vertices[index]; return vector(description.origin)+glm::dvec3(p.x,p.y,p.z)*6378137.0; };
      for (size_t index=0;index<mesh.indices.size();index+=3) {
        const auto ia=mesh.indices[index], ib=mesh.indices[index+1], ic=mesh.indices[index+2];
        const auto a=position(ia), b=position(ib), c=position(ic), e1=b-a, e2=c-a;
        const auto p=glm::cross(d,e2); const double det=glm::dot(e1,p);
        if (description.doubleSided?fabs(det)<1e-10:det<1e-10) continue;
        const auto t=o-a; const double u=glm::dot(t,p)/det;
        if (u<0 || u>1) continue;
        const auto q=glm::cross(t,e1); const double v=glm::dot(d,q)/det;
        if (v<0 || u+v>1) continue;
        const double distance=glm::dot(e2,q)/det;
        if (distance<0 || distance>=nearest) continue;
        if (description.alphaCutoff>=0 || description.blend) {
          double alpha=description.alpha;
          if (mesh.texture) {
            const auto& texture=*mesh.texture;
            const auto& va=mesh.vertices[ia]; const auto& vb=mesh.vertices[ib]; const auto& vc=mesh.vertices[ic];
            double tu=va.u*(1-u-v)+vb.u*u+vc.u*v, tv=va.v*(1-u-v)+vb.v*u+vc.v*v;
            auto wrap=[](double value,int32_t code) { if (code==33071) return glm::clamp(value,0.0,1.0); if (code==33648) { double r=fmod(value,2.0); if(r<0) r+=2; return r<=1?r:2-r; } return value-floor(value); };
            tu=wrap(tu,description.wrapS); tv=wrap(tv,description.wrapT);
            auto texel=[&](int x,int y) { return double(texture.pixels[(size_t(glm::clamp(y,0,int(texture.height)-1))*texture.width+glm::clamp(x,0,int(texture.width)-1))*4+3])/255; };
            if (description.magFilter==9728) alpha*=texel(int(tu*texture.width),int(tv*texture.height));
            else {
              double x=tu*texture.width-0.5, y=tv*texture.height-0.5; int ix=int(floor(x)), iy=int(floor(y)); double fx=x-ix, fy=y-iy;
              alpha*=glm::mix(glm::mix(texel(ix,iy),texel(ix+1,iy),fx),glm::mix(texel(ix,iy+1),texel(ix+1,iy+1),fx),fy);
            }
          }
          if ((description.alphaCutoff>=0 && alpha<description.alphaCutoff) || alpha<=0) continue;
        }
        nearest=distance;
      }
    }
  }
  if (!std::isfinite(nearest)) return 0;
  *out=point(o+d*nearest); return 1;
}
extern "C" float GE_scene_progress(GEScene* scene) { return scene?scene->tileset->computeLoadProgress():0; }
extern "C" uint32_t GE_scene_failures(GEScene* scene) { return scene?scene->assets->failures+scene->prepare->failures:0; }
extern "C" const char* GE_scene_credits(GEScene* scene) { return scene?scene->creditText.c_str():""; }
extern "C" int GE_scene_center(GEScene* scene,GEPoint* center,double* radius) {
  if (!scene || !center || !radius || !scene->tileset->getRootTile()) return 0;
  const auto& volume=scene->tileset->getRootTile()->getBoundingVolume();
  auto box=Cesium3DTilesSelection::getOrientedBoundingBoxFromBoundingVolume(volume);
  *center=point(Cesium3DTilesSelection::getBoundingVolumeCenter(volume));
  *radius=glm::length(box.getLengths())/2; return 1;
}

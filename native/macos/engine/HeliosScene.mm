#import <Foundation/Foundation.h>
#include "../../scene/SceneTransport.h"
#include <CesiumAsync/IAssetRequest.h>
#include <CesiumAsync/IAssetResponse.h>
#include <CesiumAsync/AsyncSystem.h>
#include <CesiumAsync/Future.h>
#include <mutex>

namespace {
constexpr size_t maxBody = 32 * 1024 * 1024;
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
  if (tooLarge) error = [NSError errorWithDomain:@"HeliosScene" code:413 userInfo:nil];
  if (complete) complete(bytes, response, error);
  complete = {};
  [s finishTasksAndInvalidate];
  session = nil;
}
@end

namespace {
class MacAssets final : public helios::scene::Assets, public std::enable_shared_from_this<MacAssets> {
  NSURL* root;
  NSString* directory;
  NSMutableSet<NSURLSession*>* sessions = [NSMutableSet new];
  std::mutex mutex;
  bool cancelled = false;
public:
  MacAssets(NSURL* url) : root(url) {
    directory = [[url URLByDeletingLastPathComponent].URLByResolvingSymlinksInPath.path stringByAppendingString:@"/"];
  }
  bool allows(NSURL* url) {
    if (!url || url.user || url.password) return false;
    if (root.isFileURL) return url.isFileURL && [url.URLByResolvingSymlinksInPath.path hasPrefix:directory];
    return [url.scheme.lowercaseString isEqualToString:@"https"] && [url.host.lowercaseString isEqualToString:root.host.lowercaseString] && ((url.port == root.port) || [url.port isEqualToNumber:root.port]);
  }
  void cancel() override {
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
    [request setValue:@"HeliosNative/1.0 (3D Tiles)" forHTTPHeaderField:@"User-Agent"];
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

} // namespace
namespace helios::scene {
std::shared_ptr<CesiumAsync::ITaskProcessor> makeJobs() { return std::make_shared<Jobs>(); }
std::shared_ptr<Assets> makeAssets(const char* address) {
  if (!address) return {};
  NSURL* url=[NSURL URLWithString:[NSString stringWithUTF8String:address]];
  if (!url || (!url.isFileURL && ![url.scheme.lowercaseString isEqualToString:@"https"]) || (!url.isFileURL && !url.host.length) || url.user || url.password) return {};
  return std::make_shared<MacAssets>(url);
}
} // namespace helios::scene

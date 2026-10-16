#pragma once
#include <CesiumAsync/IAssetAccessor.h>
#include <CesiumAsync/ITaskProcessor.h>
#include <atomic>
#include <memory>

namespace godeye::scene {
// Platform transport owns cancellation and URL admission. The shared scene
// engine has no Qt, Foundation, Metal or Vulkan dependency.
class Assets : public CesiumAsync::IAssetAccessor {
public:
  std::atomic<uint32_t> failures{0};
  virtual void cancel() = 0;
};
std::shared_ptr<Assets> makeAssets(const char *rootURL);
std::shared_ptr<CesiumAsync::ITaskProcessor> makeJobs();
} // namespace godeye::scene

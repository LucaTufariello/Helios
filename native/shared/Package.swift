// swift-tools-version: 6.0
import PackageDescription

let package = Package(
  name: "GodEyeShared",
  products: [.library(name: "GodEyeSpatial", targets: ["GodEyeSpatial"])],
  targets: [
    .target(name: "GodEyeSpatial", path: "Sources", publicHeadersPath: "include")
  ],
  cxxLanguageStandard: .cxx20
)

// swift-tools-version: 6.0
import PackageDescription

let package = Package(
  name: "HeliosShared",
  products: [.library(name: "HeliosSpatial", targets: ["HeliosSpatial"])],
  targets: [
    .target(name: "HeliosSpatial", path: "Sources", publicHeadersPath: "include")
  ],
  cxxLanguageStandard: .cxx20
)

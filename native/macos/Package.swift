import Foundation
// swift-tools-version: 6.0
import PackageDescription

let sceneLibraries = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
  .appendingPathComponent(".build/scene").path

let package = Package(
  name: "HeliosNative",
  platforms: [.macOS(.v14)],
  products: [.executable(name: "HeliosMac", targets: ["HeliosMac"])],
  dependencies: [.package(path: "../shared")],
  targets: [
    .target(name: "CSGP4", exclude: ["UPSTREAM.md"], publicHeadersPath: "include"),
    .target(
      name: "CScene", publicHeadersPath: "include",
      linkerSettings: [
        .unsafeFlags(["-L", sceneLibraries, "-lHeliosSceneAll", "-Xlinker", "-dead_strip"]),
        .linkedLibrary("c++"), .linkedFramework("Foundation"), .linkedFramework("Security"),
        .linkedFramework("SystemConfiguration"),
      ]),
    .target(
      name: "HeliosCore",
      dependencies: ["CSGP4", "CScene", .product(name: "HeliosSpatial", package: "shared")],
      resources: [.copy("Resources")]),
    .executableTarget(
      name: "HeliosMac", dependencies: ["HeliosCore"], resources: [.copy("Resources")]),
    .testTarget(
      name: "HeliosCoreTests", dependencies: ["HeliosCore"], resources: [.copy("Fixtures")]),
  ]
)

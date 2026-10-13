import Foundation
// swift-tools-version: 6.0
import PackageDescription

let sceneLibraries = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
  .appendingPathComponent(".build/scene").path

let package = Package(
  name: "GodEyeNative",
  platforms: [.macOS(.v14)],
  products: [.executable(name: "GodEyeMac", targets: ["GodEyeMac"])],
  dependencies: [.package(path: "../shared")],
  targets: [
    .target(name: "CSGP4", exclude: ["UPSTREAM.md"], publicHeadersPath: "include"),
    .target(
      name: "CScene", publicHeadersPath: "include",
      linkerSettings: [
        .unsafeFlags(["-L", sceneLibraries, "-lGodEyeSceneAll", "-Xlinker", "-dead_strip"]),
        .linkedLibrary("c++"), .linkedFramework("Foundation"), .linkedFramework("Security"),
        .linkedFramework("SystemConfiguration"),
      ]),
    .target(
      name: "GodEyeCore",
      dependencies: ["CSGP4", "CScene", .product(name: "GodEyeSpatial", package: "shared")],
      resources: [.copy("Resources")]),
    .executableTarget(
      name: "GodEyeMac", dependencies: ["GodEyeCore"], resources: [.copy("Resources")]),
    .testTarget(
      name: "GodEyeCoreTests", dependencies: ["GodEyeCore"], resources: [.copy("Fixtures")]),
  ]
)

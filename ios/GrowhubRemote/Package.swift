// swift-tools-version: 5.9
import PackageDescription
let package = Package(name: "GrowhubCore", platforms: [.macOS(.v13), .iOS(.v17)], products: [.library(name: "GrowhubCore", targets: ["GrowhubCore"])], dependencies: [.package(url: "https://github.com/scinfu/SwiftSoup.git", from: "2.6.0")], targets: [.target(name: "GrowhubCore", dependencies: ["SwiftSoup"]), .testTarget(name: "GrowhubCoreTests", dependencies: ["GrowhubCore"], resources: [.process("Fixtures")])])

import Foundation
import CryptoKit

public struct WiFiNetwork: Decodable, Identifiable, Sendable {
    public let ssid: String
    public let rssi: Int
    public var id: String { ssid }
}
public struct FirmwareProgress: Decodable, Sendable {
    public let stage: String
    public let bytes: Int
}
public struct ReleaseUpdateStatus: Decodable, Sendable {
    public let v: Int
    public let checks_enabled: Bool?
    public let current_version: String?
    public let tag: String?
    public let stage: String
    public let error: String?
    public let available: Bool?
    public let bytes: Int?
    public let size: Int?
    public let release_url: String?
}
public struct FirmwareImage: Sendable {
    public let data: Data
    public let version: String
    public let sha256: String
    public init(data: Data) throws {
        // CE-to-CE app image only; reject merged first-flash images and bootloaders.
        guard data.count >= 256, data.count <= 0x12c000, data[0] == 0xe9,
              data[12] == 0, data[13] == 0,
              Array(data[32..<36]) == [0x32, 0x54, 0xcd, 0xab] else {
            throw ControllerSettingsError.invalid("Choose the ESP32 firmware.bin application image, not a bootloader or merged first-flash image.")
        }
        let versionBytes = data[48..<80].prefix { $0 != 0 }
        guard let version = String(bytes: versionBytes, encoding: .utf8), !version.isEmpty else {
            throw ControllerSettingsError.invalid("The firmware image has no readable version.")
        }
        self.data = data; self.version = version
        sha256 = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }
}
extension HubAPI {
    public func networks(_ hub: RememberedHub) async throws -> [WiFiNetwork] {
        _ = try await status(host: hub.host, expectedID: hub.id)
        let (data, response) = try await read(hub, path: "scan", timeout: 20)
        guard response.statusCode == 200 else { throw ControllerSettingsError.rejected("Wi-Fi scan failed.") }
        let networks = try JSONDecoder().decode([WiFiNetwork].self, from: data)
        var seen: Set<String> = []
        return networks.sorted { $0.rssi > $1.rssi }.filter { !$0.ssid.isEmpty && seen.insert($0.ssid).inserted }
    }
    public func syncTime(_ hub: RememberedHub) async throws {
        _ = try await status(host: hub.host, expectedID: hub.id)
        var components = URLComponents(url: try Self.baseURL(hub.host).appendingPathComponent("savetime"), resolvingAgainstBaseURL: false)!
        components.queryItems = [URLQueryItem(name: "epoch", value: String(Int(Date().timeIntervalSince1970)))]
        let (_, response) = try await session.data(for: URLRequest(url: components.url!))
        guard let response = response as? HTTPURLResponse, (200...299).contains(response.statusCode) else { throw ControllerSettingsError.unconfirmed }
        let after = try await status(host: hub.host, expectedID: hub.id)
        guard after.time_warning?.isEmpty != false else { throw ControllerSettingsError.unconfirmed }
    }
    public func updates(_ hub: RememberedHub) async throws -> ReleaseUpdateStatus? {
        _ = try await status(host: hub.host, expectedID: hub.id)
        let (data, response) = try await read(hub, path: "updates")
        if response.statusCode == 404 { return nil }
        guard response.statusCode == 200 else { throw ControllerSettingsError.rejected("Update status unavailable.") }
        return try JSONDecoder().decode(ReleaseUpdateStatus.self, from: data)
    }
    public func updateAction(_ hub: RememberedHub, op: String, tag: String = "", enabled: Bool? = nil) async throws {
        _ = try await status(host: hub.host, expectedID: hub.id)
        guard ["check", "settings", "later", "skip", "install"].contains(op) else { throw ControllerSettingsError.unsupported }
        var body: [String: Any] = ["v": 1, "id": UUID().uuidString, "op": op, "tag": tag]
        if let enabled { body["enabled"] = enabled }
        if op == "install" { body["confirmed"] = true }
        var request = URLRequest(url: try Self.baseURL(hub.host).appendingPathComponent("updates"))
        request.httpMethod = "POST"; request.httpBody = try JSONSerialization.data(withJSONObject: body)
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        let (_, response) = try await session.data(for: request)
        guard (response as? HTTPURLResponse)?.statusCode == 202 else { throw ControllerSettingsError.rejected("Update request was not accepted.") }
    }
    public func uploadFirmware(_ hub: RememberedHub, image: FirmwareImage) async throws {
        _ = try await status(host: hub.host, expectedID: hub.id)
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 120; config.timeoutIntervalForResource = 180
        let upload = URLSession(configuration: config)
        defer { upload.finishTasksAndInvalidate() }
        var request = URLRequest(url: try Self.baseURL(hub.host).appendingPathComponent("ota_upload"))
        request.httpMethod = "POST"
        request.setValue("application/octet-stream", forHTTPHeaderField: "Content-Type")
        request.setValue("close", forHTTPHeaderField: "Connection")
        let (data, response) = try await upload.upload(for: request, from: image.data)
        guard (response as? HTTPURLResponse)?.statusCode == 200, String(data: data, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) == "OK" else { throw ControllerSettingsError.unconfirmed }
    }
    public func firmwareProgress(_ hub: RememberedHub) async throws -> FirmwareProgress {
        _ = try await status(host: hub.host, expectedID: hub.id)
        let (data, response) = try await read(hub, path: "ota_status")
        guard response.statusCode == 200 else { throw ControllerSettingsError.unsupported }
        return try JSONDecoder().decode(FirmwareProgress.self, from: data)
    }
}

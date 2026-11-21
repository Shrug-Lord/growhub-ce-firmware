import Foundation

public struct Outlet: Decodable, Identifiable, Sendable {
    public let id: Int
    public let state: String
    public let summary: String
    public let mode: String?
    public var isOn: Bool { state == "on" }
}
public struct HubStatus: Decodable, Sendable {
    public let mac: String
    public let fw: String
    public let name: String
    public let mode: String
    public let temp: Double
    public let rh: Double
    public let unit: String
    public let outlet_status: [Outlet]
    public let time_warning: String?
    public let sensor_warning: String?
    public let co2: Int?
    public let co2_valid: Bool?
    public let temp_valid: Bool?
    public let light: Int?
    public let rssi: Int?
    public let uptime_s: Double?
    public let wifi: Bool?
    public let mqtt: Bool?
    public let time: String?
    public let time_source: String?
    public let sntp_status: String?
    public let ap_active: Bool?
    public let ap_reason: String?
    public var hasTemperatureHumidityReading: Bool {
        guard temp.isFinite, rh.isFinite, (0...100).contains(rh) else { return false }
        if let valid = temp_valid { return valid }
        // Earlier CE releases omit validity and send zero-filled readings when
        // no sensor has replied. Fahrenheit conversion turns that zero into 32.
        guard sensor_warning?.isEmpty != false else { return false }
        return rh != 0 || temp != (unit == "F" ? 32 : 0)
    }
    public var lightLevel: Int? {
        guard hasTemperatureHumidityReading, let light, (0...255).contains(light) else { return nil }
        return light
    }
    public var id: String { mac.uppercased().replacingOccurrences(of: ":", with: "") }
    public var hasOutletModes: Bool { outlet_status.allSatisfy { $0.mode == "auto" || $0.mode == "manual" } }
    public var isValidHub: Bool {
        id.count == 12 && id.allSatisfy { $0.isHexDigit } && !fw.isEmpty &&
        Set(outlet_status.map(\.id)) == Set(1...4) && outlet_status.count == 4 &&
        outlet_status.allSatisfy { $0.state == "on" || $0.state == "off" }
    }
}
public struct RememberedHub: Codable, Identifiable, Sendable {
    public var id: String
    public var host: String
    public var name: String
    public var alias: String = ""
    public var outletNames: [Int: String] = [:]
    public var title: String { alias.isEmpty ? name : alias }
    public var deviceID: String { "GrowHub_\(id.suffix(4))" }
    public func outletName(_ outlet: Int) -> String { outletNames[outlet] ?? "Outlet \(outlet)" }
    public mutating func setOutletName(_ name: String, outlet: Int) {
        guard (1...4).contains(outlet) else { return }
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        outletNames[outlet] = trimmed.isEmpty ? nil : trimmed
    }
    public mutating func applyOutletLabels(_ settings: ControllerSettings) {
        guard settings.mac == id else { return }
        for outlet in 1...4 {
            if let label = settings.fields["label_\(outlet)"] { setOutletName(label, outlet: outlet) }
        }
    }
    public init(status: HubStatus, host: String) {
        id = status.id; self.host = host; name = status.name
    }
    private enum CodingKeys: String, CodingKey { case id, host, name, alias, outletNames }
    public init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        id = try values.decode(String.self, forKey: .id)
        host = try values.decode(String.self, forKey: .host)
        name = try values.decode(String.self, forKey: .name)
        alias = try values.decodeIfPresent(String.self, forKey: .alias) ?? ""
        // Preserve controllers remembered by earlier app builds.
        outletNames = try values.decodeIfPresent([Int: String].self, forKey: .outletNames) ?? [:]
    }
}

public enum HubError: LocalizedError {
    case invalidAddress, notGrowhub, identityChanged, manualRequired, commandNotConfirmed
    public var errorDescription: String? {
        switch self {
        case .invalidAddress: return "Enter a local IP address or hostname, without a path."
        case .notGrowhub: return "This address did not return a Growhub status."
        case .identityChanged: return "A different device now uses this address. Refresh discovery before controlling it."
        case .manualRequired: return "Switch to Manual before changing this outlet."
        case .commandNotConfirmed: return "The controller did not confirm the change. Refresh its status before retrying."
        }
    }
}

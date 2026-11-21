import Foundation
import SwiftSoup

public struct SettingChoice: Codable, Hashable, Sendable {
    public var value: String
    public var label: String
    public init(value: String, label: String) { self.value = value; self.label = label }
}
public struct ControllerSettings: Codable, Sendable {
    public var api: Int
    public var mac: String
    public var fw: String
    public var fields: [String: String]
    public var choices: [String: [SettingChoice]]
    public var capabilities: [String: Bool]
    public init(api: Int, mac: String, fw: String, fields: [String: String], choices: [String: [SettingChoice]], capabilities: [String: Bool]) {
        self.api = api; self.mac = mac; self.fw = fw; self.fields = fields; self.choices = choices; self.capabilities = capabilities
    }
    public var usesNativeAPI: Bool { api == 1 }
    public func value(_ key: String) -> String { fields[key] ?? "" }
    public func enabled(_ key: String) -> Bool { value(key) == "1" }
    public func outletName(_ id: Int) -> String {
        let label = value("label_\(id)").trimmingCharacters(in: .whitespacesAndNewlines)
        return label.isEmpty ? "Outlet \(id)" : label
    }
    public func outletLabelParameters(_ name: String, outlet: Int) throws -> [String: String] {
        guard (1...4).contains(outlet), fields["label_\(outlet)"] != nil else {
            throw ControllerSettingsError.invalid("This firmware does not support a shared equipment label for this outlet.")
        }
        let label = name.trimmingCharacters(in: .whitespaces)
        guard label.utf8.count <= 32, !label.unicodeScalars.contains(where: { $0.value < 32 || $0.value == 127 }) else {
            throw ControllerSettingsError.invalid("Equipment labels can use up to 32 bytes and cannot contain control characters.")
        }
        return ["label_\(outlet)": label]
    }
    public static let equipment = ["", "Light", "Fan", "Humidifier", "Dehumidifier", "Water Pump", "Heater", "AC Controller"]

    // Only known CE settings are interpreted. HTML is never rendered or executed.
    public static func legacy(html: String, diagnostics: String = "", status: HubStatus) throws -> Self {
        let page = try SwiftSoup.parse(html)
        var fields: [String: String] = [:]
        var choices: [String: [SettingChoice]] = [:]
        for element in try page.select("input[name], select[name]") {
            let name = try element.attr("name")
            guard allowedField(name) else { continue }
            if element.tagName() == "select" {
                let options = try element.select("option").map { SettingChoice(value: try $0.attr("value"), label: try $0.text()) }
                choices[name] = options
                fields[name] = try element.select("option[selected]").first()?.attr("value") ?? options.first?.value ?? ""
            } else {
                let type = try element.attr("type").lowercased()
                guard type != "password" else { continue }
                if type == "checkbox" { fields[name] = element.hasAttr("checked") ? "1" : "" }
                else if type == "radio" { if element.hasAttr("checked") { fields[name] = try element.attr("value") } }
                else { fields[name] = try element.attr("value") }
            }
        }
        if !diagnostics.isEmpty {
            let diagnosticPage = try SwiftSoup.parse(diagnostics)
            for element in try diagnosticPage.select("input[name=operation_led][checked]") {
                fields["operation_led"] = try element.attr("value")
            }
            let text = try diagnosticPage.text()
            for (label, key) in [("Operation LED pin", "operation_led_pin"), ("Red malfunction LED pin", "error_led_pin")] {
                let expression = try NSRegularExpression(pattern: NSRegularExpression.escapedPattern(for: label) + ":\\s*GPIO\\s+(\\d+)")
                if let match = expression.firstMatch(in: text, range: NSRange(text.startIndex..., in: text)), let range = Range(match.range(at: 1), in: text) { fields[key] = String(text[range]) }
            }
        }
        guard fields["dev_name"] != nil, (1...4).allSatisfy({ fields["outlet_\($0)"] != nil }) else { throw ControllerSettingsError.unsupported }
        choices["outlet_type"] = choices["outlet_1"]
        return Self(api: 0, mac: status.id, fw: status.fw, fields: fields, choices: choices,
                    capabilities: ["outlet_modes": status.hasOutletModes, "schedule_v3": fields["sched_v3"] != nil,
                                   "file_ota": !(try page.select("#fw-file")).isEmpty(),
                                   "release_updates": !(try page.select("#release-updates")).isEmpty()])
    }
    public static func allowedField(_ key: String) -> Bool {
        if ["dev_name", "timezone", "temp_unit", "time_src", "sntp_primary", "sntp_secondary", "ssid", "mqtt_host", "mqtt_port", "keep_ap", "operation_led", "sched_v3"].contains(key) { return true }
        for id in 1...4 {
            if key == "outlet_\(id)" || key == "label_\(id)" { return true }
            if ["disabled", "always", "time_en", "start", "end", "rh_en", "rh_low", "rh_high", "temp_en", "temp_low", "temp_high", "interval_en", "run", "every", "win_en", "wstart", "wend"].contains(where: { key == "s\(id)_\($0)" }) { return true }
        }
        return false
    }
    public static func isCheckbox(_ key: String) -> Bool {
        key.hasSuffix("_en") || key.hasSuffix("_always")
    }
    public func scheduleParameters() -> [String: String] {
        var result = ["sched_v3": "1"]
        for id in 1...4 {
            let type = value("outlet_\(id)"), p = "s\(id)_"
            guard !type.isEmpty else { continue }
            if let disabled = fields[p + "disabled"] { result[p + "disabled"] = disabled }
            if ["Light", "Fan"].contains(type), enabled(p + "always") { result[p + "always"] = "1"; continue }
            var groups: [[String]] = []
            if ["Light", "Fan"].contains(type) { groups.append(["time_en", "start", "end"]) }
            if ["Fan", "Humidifier", "Dehumidifier"].contains(type) { groups.append(["rh_en", "rh_low", "rh_high"]) }
            if ["Fan", "Heater", "AC Controller"].contains(type) { groups.append(["temp_en", "temp_low", "temp_high"]) }
            if type == "Water Pump" { groups.append(["interval_en", "run", "every"]); if enabled(p + "interval_en") { groups.append(["win_en", "wstart", "wend"]) } }
            for group in groups where enabled(p + group[0]) {
                for suffix in group {
                    var value = value(p + suffix)
                    if ["run", "every"].contains(suffix), let number = Double(value), number.rounded() == number { value = String(Int(number)) }
                    result[p + suffix] = value
                }
            }
        }
        return result
    }
    public static func equivalent(_ a: String?, _ b: String?) -> Bool {
        let a = a ?? "", b = b ?? ""
        if let x = Double(a), let y = Double(b) { return abs(x - y) < 0.051 }
        return a == b
    }
}
public enum ControllerSettingsError: LocalizedError {
    case unsupported, changed, rejected(String), invalid(String), unconfirmed
    public var errorDescription: String? {
        switch self {
        case .unsupported: return "This controller did not return compatible CE settings."
        case .changed: return "The controller’s settings changed while you were editing. Reload and try again."
        case .rejected(let detail): return "The controller rejected the change. \(detail)"
        case .invalid(let detail): return detail
        case .unconfirmed: return "The request was sent, but the saved settings could not be confirmed. Reconnect and refresh before trying again."
        }
    }
}

extension HubAPI {
    func read(_ hub: RememberedHub, path: String, timeout: TimeInterval = 5) async throws -> (Data, HTTPURLResponse) {
        var request = URLRequest(url: try Self.baseURL(hub.host).appendingPathComponent(path))
        request.timeoutInterval = timeout
        request.setValue("close", forHTTPHeaderField: "Connection")
        let (data, response) = try await session.data(for: request)
        guard let response = response as? HTTPURLResponse, data.count <= 262144 else { throw ControllerSettingsError.unsupported }
        return (data, response)
    }
    public func settings(_ hub: RememberedHub) async throws -> ControllerSettings {
        let status = try await status(host: hub.host, expectedID: hub.id)
        let (data, response) = try await read(hub, path: "api/v1/settings")
        if response.statusCode == 200 {
            let settings = try JSONDecoder().decode(ControllerSettings.self, from: data)
            guard settings.api == 1 else { throw ControllerSettingsError.unsupported }
            guard settings.mac == hub.id else { throw HubError.identityChanged }
            guard settings.fields["dev_name"] != nil, (1...4).allSatisfy({ settings.fields["outlet_\($0)"] != nil }) else { throw ControllerSettingsError.unsupported }
            return settings
        }
        guard [404, 405, 501].contains(response.statusCode) else { throw ControllerSettingsError.unsupported }
        let (page, pageResponse) = try await read(hub, path: "")
        guard pageResponse.statusCode == 200, let html = String(data: page, encoding: .utf8) else { throw ControllerSettingsError.unsupported }
        let diagnostic = try? await read(hub, path: "diagnostics")
        let text = diagnostic.flatMap { $0.1.statusCode == 200 ? String(data: $0.0, encoding: .utf8) : nil } ?? ""
        return try ControllerSettings.legacy(html: html, diagnostics: text, status: status)
    }
    public static func formBody(_ params: [String: String]) -> Data {
        var components = URLComponents()
        components.queryItems = params.sorted { $0.key < $1.key }.map { URLQueryItem(name: $0.key, value: $0.value) }
        // '+' must stay literal under application/x-www-form-urlencoded decoding.
        return Data((components.percentEncodedQuery ?? "").replacingOccurrences(of: "+", with: "%2B").utf8)
    }
    @discardableResult public func writeSettings(_ hub: RememberedHub, params: [String: String], baseline: ControllerSettings, mayDisconnect: Bool = false) async throws -> ControllerSettings? {
        let before = try await settings(hub)
        if params["reset"] != nil, before.fields["operation_led"] == "0" {
            throw ControllerSettingsError.invalid("Factory reset would remove the disabled operation-LED hardware override. Keep the override on this controller and use individual settings instead.")
        }
        let relevant = params.keys.filter { $0 != "password" }
        for key in relevant where !ControllerSettings.equivalent(before.fields[key], baseline.fields[key]) { throw ControllerSettingsError.changed }
        if params["sched_v3"] != nil {
            for key in Set(before.fields.keys).union(baseline.fields.keys) where key.hasPrefix("s") || key.hasPrefix("outlet_") {
                guard ControllerSettings.equivalent(before.fields[key], baseline.fields[key]) else { throw ControllerSettingsError.changed }
            }
        }
        _ = try await status(host: hub.host, expectedID: hub.id)
        var request = URLRequest(url: try Self.baseURL(hub.host).appendingPathComponent(before.usesNativeAPI ? "api/v1/settings" : "save"))
        request.httpMethod = "POST"; request.httpBody = Self.formBody(params)
        request.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        request.setValue(hub.id, forHTTPHeaderField: "X-Growhub-ID")
        request.setValue("close", forHTTPHeaderField: "Connection")
        request.timeoutInterval = 15
        let (data, response) = try await session.data(for: request)
        guard let response = response as? HTTPURLResponse, (200...299).contains(response.statusCode) else {
            throw ControllerSettingsError.rejected(String(data: data.prefix(256), encoding: .utf8) ?? "")
        }
        if mayDisconnect { return nil }
        let after = try await settings(hub)
        for key in relevant where ControllerSettings.allowedField(key) {
            let desired = params[key] ?? ""
            guard ControllerSettings.equivalent(after.fields[key], desired) else { throw ControllerSettingsError.unconfirmed }
        }
        if params["sched_v3"] != nil {
            for key in after.fields.keys where ControllerSettings.isCheckbox(key) && params[key] == nil {
                guard after.value(key).isEmpty else { throw ControllerSettingsError.unconfirmed }
            }
        }
        if params["sched_clear"] != nil, after.fields.contains(where: { ControllerSettings.isCheckbox($0.key) && !$0.value.isEmpty }) { throw ControllerSettingsError.unconfirmed }
        return after
    }
}

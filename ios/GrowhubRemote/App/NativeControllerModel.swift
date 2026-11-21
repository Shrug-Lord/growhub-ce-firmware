import Foundation
import GrowhubCore

@MainActor final class NativeControllerModel: ObservableObject {
    let hub: RememberedHub
    let demo: Bool
    @Published var settings: ControllerSettings?
    @Published var status: HubStatus?
    @Published var updates: ReleaseUpdateStatus?
    @Published var progress: FirmwareProgress?
    @Published var busy = false
    @Published var error: String?
    @Published var notice: String?
    init(hub: RememberedHub, demo: Bool, settings: ControllerSettings? = nil) { self.hub = hub; self.demo = demo; self.settings = settings }
    func load() async {
        await perform {
            if self.demo {
                if self.settings == nil { self.settings = Self.demoSettings(self.hub) }
                self.status = DemoController.status(index: self.hub.id.hasSuffix("2") ? 1 : 0)
            } else {
                self.settings = try await HubAPI.shared.settings(self.hub)
                self.status = try await HubAPI.shared.status(host: self.hub.host, expectedID: self.hub.id)
            }
        }
    }
    @discardableResult func save(_ params: [String: String], baseline: ControllerSettings, disconnects: Bool = false) async -> Bool {
        var success = false
        await perform {
            try Self.validate(params)
            if self.demo {
                var next = self.settings ?? baseline
                if params["sched_v3"] != nil {
                    for key in next.fields.keys where ControllerSettings.isCheckbox(key) { next.fields[key] = "" }
                }
                for (key, value) in params { next.fields[key] = value }
                self.settings = next
                self.notice = "Demo settings saved. No hardware was changed."
            } else if let next = try await HubAPI.shared.writeSettings(self.hub, params: params, baseline: baseline, mayDisconnect: disconnects) {
                self.settings = next; self.notice = "Settings saved and confirmed."
            } else {
                self.notice = "Request accepted. The controller may restart or change networks. Return to your Growhubs and reconnect to confirm the result."
            }
            success = true
        }
        return success
    }
    func maintenance(_ action: String, value: String = "1", disconnects: Bool = false) async {
        guard let settings else { return }
        _ = await save([action: value], baseline: settings, disconnects: disconnects)
    }
    func syncClock() async {
        await perform {
            if !self.demo { try await HubAPI.shared.syncTime(self.hub) }
            self.notice = self.demo ? "Demo clock synchronized." : "Clock synchronization sent."
        }
    }
    func refreshUpdates() async {
        await perform {
            if !self.demo {
                self.updates = try await HubAPI.shared.updates(self.hub)
                self.progress = try? await HubAPI.shared.firmwareProgress(self.hub)
            }
        }
    }
    func update(_ op: String, enabled: Bool? = nil) async {
        await perform {
            if !self.demo {
                try await HubAPI.shared.updateAction(self.hub, op: op, tag: self.updates?.tag ?? "", enabled: enabled)
                self.updates = try await HubAPI.shared.updates(self.hub)
            }
            self.notice = self.demo ? "Demo only. No update was requested." : "Update request accepted. Refresh to check progress."
        }
    }
    func upload(_ image: FirmwareImage) async {
        await perform {
            if !self.demo { try await HubAPI.shared.uploadFirmware(self.hub, image: image) }
            self.notice = self.demo ? "Demo only. No firmware was uploaded." : "Firmware transfer completed. Wait for the Growhub to restart, then reconnect and confirm its firmware version."
        }
    }
    func perform(_ action: () async throws -> Void) async {
        guard !busy else { return }
        busy = true; error = nil; notice = nil
        defer { busy = false }
        do { try await action() } catch { self.error = error.localizedDescription }
    }
    static func validate(_ fields: [String: String]) throws {
        for (key, value) in fields {
            let limit = ["ssid", "dev_name"].contains(key) || key.hasPrefix("label_") ? 32 : key == "ota_url" ? 255 : 64
            guard value.utf8.count <= limit, !value.unicodeScalars.contains(where: { $0.value < 32 }) else { throw ControllerSettingsError.invalid("A setting is too long or contains an invalid character.") }
            if key == "dev_name", value.trimmingCharacters(in: .whitespaces).isEmpty { throw ControllerSettingsError.invalid("Enter a device name.") }
            if key == "ssid", value.isEmpty { throw ControllerSettingsError.invalid("Enter a Wi-Fi network name.") }
            if key == "mqtt_host", value.isEmpty { throw ControllerSettingsError.invalid("Enter a Command Center server, or use Disconnect Command Center.") }
            if key.hasPrefix("outlet_"), !ControllerSettings.equipment.contains(value) { throw ControllerSettingsError.invalid("Choose a supported equipment type.") }
            if key == "mqtt_port", !(Int(value).map { (1...65535).contains($0) } ?? false) { throw ControllerSettingsError.invalid("MQTT port must be between 1 and 65535.") }
            if key.hasSuffix("_start") || key.hasSuffix("_end") || key.hasSuffix("_wstart") || key.hasSuffix("_wend") {
                let parts = value.split(separator: ":").compactMap { Int($0) }
                guard value.count == 5, parts.count == 2, (0...23).contains(parts[0]), (0...59).contains(parts[1]) else { throw ControllerSettingsError.invalid("Enter times as HH:mm.") }
            }
        }
        for id in 1...4 {
            let prefix = "s\(id)_"
            for family in ["rh", "temp"] where fields[prefix + family + "_en"] != nil {
                guard let low = Double(fields[prefix + family + "_low"] ?? ""), let high = Double(fields[prefix + family + "_high"] ?? ""), low.isFinite, high.isFinite, low < high,
                      family != "rh" || (10...95).contains(low) && (10...95).contains(high) else { throw ControllerSettingsError.invalid("Outlet \(id): enter valid lower and upper thresholds, with the lower value first.") }
            }
            if fields[prefix + "interval_en"] != nil {
                guard let run = Int(fields[prefix + "run"] ?? ""), (1...240).contains(run), let hours = Int(fields[prefix + "every"] ?? ""), (1...168).contains(hours) else { throw ControllerSettingsError.invalid("Outlet \(id): watering needs 1–240 minutes every 1–168 hours.") }
            }
        }
    }
    static func demoSettings(_ hub: RememberedHub) -> ControllerSettings {
        var fields = ["dev_name": hub.name, "timezone": "EST5EDT,M3.2.0,M11.1.0", "temp_unit": "0", "time_src": "0", "sntp_primary": "pool.ntp.org", "sntp_secondary": "time.nist.gov", "ssid": "Garden Wi-Fi", "mqtt_host": "", "mqtt_port": "1883", "keep_ap": "1", "operation_led": "1", "sched_v3": "1"]
        for id in 1...4 {
            fields["outlet_\(id)"] = ["Light", "Fan", "Humidifier", "Water Pump"][id - 1]
            fields["label_\(id)"] = ""
            for (suffix, value) in ["disabled":"0", "always":"", "time_en":id == 1 ? "1" : "", "start":"06:00", "end":"22:00", "rh_en":"", "rh_low":"50", "rh_high":"60", "temp_en":"", "temp_low":"24", "temp_high":"27", "interval_en":id == 4 ? "1" : "", "run":"15", "every":"4", "win_en":"", "wstart":"06:00", "wend":"22:00"] { fields["s\(id)_\(suffix)"] = value }
        }
        return ControllerSettings(api: 1, mac: hub.id, fw: "Demo", fields: fields, choices: ["timezone": [SettingChoice(value: "EST5EDT,M3.2.0,M11.1.0", label: "US/Eastern"), SettingChoice(value: "UTC0", label: "UTC")], "outlet_type": ControllerSettings.equipment.map { SettingChoice(value: $0, label: $0.isEmpty ? "None" : $0) }], capabilities: ["schedule_v3": true, "file_ota": true, "release_updates": true])
    }
}

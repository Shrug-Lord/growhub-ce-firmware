import SwiftUI
import GrowhubCore

struct NativeControllerView: View {
    @StateObject private var model: NativeControllerModel
    @EnvironmentObject private var store: HubStore
    init(hub: RememberedHub, demo: Bool, settings: ControllerSettings? = nil) { _model = StateObject(wrappedValue: NativeControllerModel(hub: hub, demo: demo, settings: settings)) }
    var body: some View {
        Group {
            if let settings = model.settings {
                List {
                    Section {
                        LabeledContent("Controller", value: model.hub.deviceID)
                        LabeledContent("Firmware", value: model.status?.fw ?? settings.fw)
                        if model.demo { Label("Demo · no hardware connected", systemImage: "play.circle") }
                    }
                    Section("Control and settings") {
                        NavigationLink { NativeSettingsEditor(kind: .schedule, baseline: settings).environmentObject(model) } label: { Label("Schedules", systemImage: "calendar.badge.clock") }
                        NavigationLink { NativeSettingsEditor(kind: .outlets, baseline: settings).environmentObject(model) } label: { Label("Outlet assignments", systemImage: "powerplug") }
                        NavigationLink { NativeSettingsEditor(kind: .device, baseline: settings).environmentObject(model) } label: { Label("Device and time", systemImage: "clock") }
                        NavigationLink { NativeSettingsEditor(kind: .wifi, baseline: settings).environmentObject(model) } label: { Label("Wi-Fi", systemImage: "wifi") }
                        NavigationLink { NativeSettingsEditor(kind: .mqtt, baseline: settings).environmentObject(model) } label: { Label("Command Center", systemImage: "network") }
                    }
                    Section {
                        NavigationLink { NativeFirmwareView().environmentObject(model) } label: { Label("Firmware updates", systemImage: "arrow.down.circle") }
                        NavigationLink { NativeDiagnosticsView().environmentObject(model) } label: { Label("Diagnostics and actions", systemImage: "stethoscope") }
                    }
                }
            } else if model.busy { ProgressView("Reading controller settings…") }
            else { ContentUnavailableView { Label("Couldn’t load settings", systemImage: "wifi.exclamationmark") } description: { Text(model.error ?? "Check your Wi-Fi and try again.") } actions: { Button("Try again") { Task { await model.load() } } } }
        }
        .task { await model.load() }
        .onChange(of: model.settings?.fields, initial: true) { _, _ in
            if let settings = model.settings { store.receiveSettings(settings) }
        }
        .refreshable { await model.load() }
        .safeAreaInset(edge: .bottom) { NativeMessage().environmentObject(model) }
    }
}
private struct NativeMessage: View {
    @EnvironmentObject var model: NativeControllerModel
    var body: some View {
        if model.busy { ProgressView("Working…").padding().frame(maxWidth: .infinity).background(.regularMaterial) }
        else if let text = model.error ?? model.notice { Text(text).font(.footnote).foregroundStyle(model.error == nil ? Color.secondary : Color.orange).padding().frame(maxWidth: .infinity).background(.regularMaterial) }
    }
}
enum NativeSettingsKind: String { case schedule = "Schedules", outlets = "Outlet assignments", device = "Device and time", wifi = "Wi-Fi", mqtt = "Command Center" }
struct NativeSettingsEditor: View {
    let kind: NativeSettingsKind
    @State private var baseline: ControllerSettings
    @EnvironmentObject private var model: NativeControllerModel
    @Environment(\.dismiss) private var dismiss
    @State private var fields: [String: String]
    @State private var confirm = false
    @State private var action: String?
    @State private var actionValue = "1"
    @State private var networks: [WiFiNetwork] = []
    init(kind: NativeSettingsKind, baseline: ControllerSettings) { self.kind = kind; _baseline = State(initialValue: baseline); _fields = State(initialValue: baseline.fields) }
    var body: some View {
        Form {
            switch kind {
            case .schedule: schedule
            case .outlets: outlets
            case .device: device
            case .wifi: wifi
            case .mqtt: mqtt
            }
            Section { Button("Save changes") { action = nil; confirm = true }.disabled(model.busy || (kind == .schedule && baseline.fields["sched_v3"] == nil)) }
        }
        .navigationTitle(kind.rawValue)
        .onAppear { if let latest = model.settings { baseline = latest; fields = latest.fields } }
        .toolbar { Button("Save") { action = nil; confirm = true }.disabled(model.busy || (kind == .schedule && baseline.fields["sched_v3"] == nil)) }
        .disabled(model.busy)
        .safeAreaInset(edge: .bottom) { NativeMessage() }
        .confirmationDialog(actionTitle, isPresented: $confirm, titleVisibility: .visible) {
            Button(actionTitle, role: action == "wifi_forget" || action == "sched_clear" ? .destructive : nil) { Task { await save() } }
        } message: { Text(confirmationText) }
    }
    private var actionTitle: String {
        switch action { case "wifi_forget": return "Forget Wi-Fi"; case "sched_clear": return "Clear all schedules"; case "pump_run_now": return "Run outlet \(actionValue) pump now"; case "cc_disconnect": return "Disconnect Command Center"; case "cc_reconnect": return "Reconnect Command Center"; default: return "Save \(kind.rawValue.lowercased())" }
    }
    private var confirmationText: String {
        if action == "wifi_forget" { return "This removes the saved Wi-Fi network. You may need to join the Growhub’s setup Wi-Fi to reconnect." }
        if kind == .outlets { return "Changing an equipment type clears that outlet’s existing schedule. Equipment labels are saved on the controller." }
        if kind == .schedule { return "This changes automation on the controller and may switch equipment immediately when in Auto. Existing manual outlets keep manual control." }
        if kind == .wifi { return "The Growhub may disconnect while joining this network. An empty password is used for an open network." }
        if kind == .mqtt { return "Saving a server or reconnecting Command Center restarts the Growhub. Relay outputs start off after restart." }
        return "Apply these settings to the controller? Time changes can affect active schedules." }
    private func text(_ title: String, _ key: String, numeric: Bool = false, secure: Bool = false) -> some View {
        LabeledContent(title) {
            Group {
                if secure { SecureField(title, text: binding(key)) }
                else { TextField(title, text: binding(key)).keyboardType(numeric ? .numbersAndPunctuation : .default) }
            }.multilineTextAlignment(.trailing).textInputAutocapitalization(.never).autocorrectionDisabled()
        }
    }
    private func binding(_ key: String) -> Binding<String> { Binding(get: { fields[key] ?? "" }, set: { fields[key] = $0 }) }
    private func toggle(_ title: String, _ key: String, zero: String = "") -> some View {
        Toggle(title, isOn: Binding(get: { fields[key] == "1" }, set: { fields[key] = $0 ? "1" : zero }))
    }
    private func picker(_ title: String, _ key: String, choices: [SettingChoice]) -> some View {
        Picker(title, selection: binding(key)) {
            if let value = fields[key], !choices.contains(where: { $0.value == value }) { Text(value).tag(value) }
            ForEach(Array(choices.enumerated()), id: \.offset) { _, choice in Text(choice.label).tag(choice.value) }
        }
    }
    @ViewBuilder private var schedule: some View {
        if baseline.fields["sched_v3"] == nil { Section { Text("This firmware does not expose a compatible schedule editor.") } }
        else {
            ForEach(1...4, id: \.self) { id in
                let type = fields["outlet_\(id)"] ?? ""
                if !type.isEmpty { Section("\(baseline.outletName(id)) · \(type)") { scheduleFields(id, type: type) } }
            }
            if !(1...4).contains(where: { !(fields["outlet_\($0)"] ?? "").isEmpty }) { Section { Text("Choose equipment types in Outlet assignments to create schedules.") } }
            Section { Button("Clear all schedules", role: .destructive) { action = "sched_clear"; confirm = true } }
        }
    }
    @ViewBuilder private func scheduleFields(_ id: Int, type: String) -> some View {
        let p = "s\(id)_"
        if fields[p + "disabled"] != nil { toggle("Pause this schedule", p + "disabled", zero: "0") }
        if ["Light", "Fan"].contains(type) { toggle("Always on", p + "always") }
        if fields[p + "always"] != "1" {
            if ["Light", "Fan"].contains(type) {
                toggle("Time control", p + "time_en")
                if fields[p + "time_en"] == "1" { text("On at (HH:mm)", p + "start"); text("Off at (HH:mm)", p + "end") }
            }
            if ["Fan", "Humidifier", "Dehumidifier"].contains(type) {
                toggle("Humidity control", p + "rh_en")
                if fields[p + "rh_en"] == "1" { text(type == "Humidifier" ? "On below (%)" : "Off below (%)", p + "rh_low", numeric: true); text(type == "Humidifier" ? "Off above (%)" : "On above (%)", p + "rh_high", numeric: true) }
            }
            if ["Fan", "Heater", "AC Controller"].contains(type) {
                toggle("Temperature control", p + "temp_en")
                if fields[p + "temp_en"] == "1" {
                    let unit = baseline.value("temp_unit") == "1" ? "°F" : "°C"
                    text(type == "Heater" ? "On below (\(unit))" : "Off below (\(unit))", p + "temp_low", numeric: true)
                    text(type == "Heater" ? "Off above (\(unit))" : "On above (\(unit))", p + "temp_high", numeric: true)
                }
            }
            if type == "Water Pump" {
                toggle("Watering control", p + "interval_en")
                if fields[p + "interval_en"] == "1" {
                    text("Every (hours)", p + "every", numeric: true); text("Run for (minutes)", p + "run", numeric: true)
                    toggle("Restrict to allowed hours", p + "win_en")
                    if fields[p + "win_en"] == "1" { text("From (HH:mm)", p + "wstart"); text("Until (HH:mm)", p + "wend") }
                    Button("Run pump now") { action = "pump_run_now"; actionValue = String(id); confirm = true }
                }
            }
        }
    }
    private var outlets: some View {
        ForEach(1...4, id: \.self) { id in
            Section(baseline.outletName(id)) {
                picker("Equipment", "outlet_\(id)", choices: baseline.choices["outlet_type"] ?? ControllerSettings.equipment.map { SettingChoice(value: $0, label: $0.isEmpty ? "None" : $0) })
                if fields["label_\(id)"] != nil { text("Equipment label", "label_\(id)")
                    Text("Also used as this outlet’s name throughout the app.").font(.footnote).foregroundStyle(.secondary) }
                Text("Physical outlet \(id)").font(.caption).foregroundStyle(.secondary)
            }
        }
    }
    private var device: some View {
        Group {
            Section("Controller") { text("Device name", "dev_name"); picker("Temperature", "temp_unit", choices: [.init(value: "0", label: "Celsius"), .init(value: "1", label: "Fahrenheit")]) }
            Section("Time") {
                picker("Timezone", "timezone", choices: baseline.choices["timezone"] ?? [])
                picker("Time source", "time_src", choices: [.init(value: "0", label: "Internet time (SNTP)"), .init(value: "1", label: "Manual")])
                if fields["sntp_primary"] != nil { text("Primary time server", "sntp_primary"); text("Secondary time server", "sntp_secondary") }
                Button("Sync time from iPhone") { Task { await model.syncClock() } }
            }
        }
    }
    private var wifi: some View {
        Group {
            Section("Network") {
                text("Network name", "ssid"); text("Password", "password", secure: true)
                Button("Find Wi-Fi networks") {
                    Task { await model.perform { if !model.demo { networks = try await HubAPI.shared.networks(model.hub) } } }
                }
                ForEach(networks) { network in Button(network.ssid) { fields["ssid"] = network.ssid } }
            }
            if fields["keep_ap"] != nil { Section("Setup Wi-Fi") { toggle("Keep setup Wi-Fi available", "keep_ap", zero: "0"); Text("When disabled, setup Wi-Fi returns after five minutes without a network connection.").font(.footnote) } }
            Section { Button("Forget Wi-Fi", role: .destructive) { action = "wifi_forget"; confirm = true } }
        }
    }
    private var mqtt: some View {
        Group {
            Section { text("Server address", "mqtt_host"); text("Port", "mqtt_port", numeric: true); Text("Optional local MQTT server. Growhub Remote does not need Command Center.").font(.footnote) }
            Section {
                Button("Disconnect Command Center") { action = "cc_disconnect"; confirm = true }
                Button("Reconnect Command Center") { action = "cc_reconnect"; confirm = true }
            }
        }
    }
    private func save() async {
        if let action {
            await model.maintenance(action, value: action == "pump_run_now" ? actionValue : "1", disconnects: ["wifi_forget", "cc_reconnect"].contains(action))
            return
        }
        let keys: [String]
        switch kind {
        case .schedule: keys = fields.keys.filter { $0 == "sched_v3" || $0.range(of: "^s[1-4]_", options: .regularExpression) != nil }
        case .outlets: keys = fields.keys.filter { $0.hasPrefix("outlet_") || $0.hasPrefix("label_") }
        case .device: keys = ["dev_name", "timezone", "temp_unit", "time_src", "sntp_primary", "sntp_secondary"].filter { fields[$0] != nil }
        case .wifi: keys = ["ssid", "password", "keep_ap"].filter { fields[$0] != nil || $0 == "password" }
        case .mqtt: keys = ["mqtt_host", "mqtt_port"]
        }
        var params = Dictionary(uniqueKeysWithValues: keys.compactMap { key -> (String, String)? in
            let value = fields[key] ?? ""
            if ControllerSettings.isCheckbox(key), value != "1" { return nil }
            return (key, value)
        })
        if kind == .schedule {
            var edited = baseline; edited.fields = fields
            params = edited.scheduleParameters()
        }
        if kind == .outlets {
            do {
                for id in 1...4 {
                    if let label = params["label_\(id)"] {
                        params.merge(try baseline.outletLabelParameters(label, outlet: id)) { _, new in new }
                    }
                }
            } catch { model.error = error.localizedDescription; return }
        }
        if kind == .wifi, params["ssid"] == baseline.value("ssid"), (params["password"] ?? "").isEmpty {
            params["ssid"] = nil; params["password"] = nil
        }
        if await model.save(params, baseline: baseline, disconnects: kind == .wifi || kind == .mqtt) { dismiss() }
    }
}

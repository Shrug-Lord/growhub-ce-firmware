import SwiftUI
import UniformTypeIdentifiers
import GrowhubCore

struct NativeFirmwareView: View {
    @EnvironmentObject private var model: NativeControllerModel
    @Environment(\.scenePhase) private var phase
    @State private var filePicker = false
    @State private var image: FirmwareImage?
    @State private var fileName = ""
    @State private var firmwareURL = ""
    @State private var pending: String?
    @State private var confirm = false
    var body: some View {
        Form {
            Section("Installed") { LabeledContent("Controller", value: model.hub.deviceID); LabeledContent("Firmware", value: model.status?.fw ?? model.settings?.fw ?? "Unknown") }
            Section("Official releases") {
                if let updates = model.updates {
                    if let enabled = updates.checks_enabled {
                        Toggle("Check for updates periodically", isOn: Binding(get: { enabled }, set: { value in Task { await model.update("settings", enabled: value) } }))
                    }
                    LabeledContent("Status", value: updates.stage)
                    if let error = updates.error, !error.isEmpty { Text(error).foregroundStyle(.orange) }
                    if updates.available == true, let tag = updates.tag {
                        LabeledContent("Available", value: tag)
                        if let url = updates.release_url.flatMap(URL.init(string:)), url.scheme == "https", url.host == "github.com" { Link("Release notes", destination: url) }
                        Button("Install \(tag)") { pending = "official"; confirm = true }
                        Button("Remind me later") { Task { await model.update("later") } }
                        Button("Skip this version") { Task { await model.update("skip") } }
                    }
                } else { Text(model.demo ? "Official updates are simulated in demo mode." : "Automatic release checking is available on firmware that supports it.").font(.footnote) }
                if model.settings?.capabilities["release_updates"] == true { Button("Check for updates") { Task { await model.update("check") } } }
                Button("Refresh update status") { Task { await model.refreshUpdates() } }
            }
            Section("Install a firmware file") {
                Button("Choose firmware.bin") { filePicker = true }
                if let image {
                    LabeledContent("File", value: fileName)
                    LabeledContent("Version", value: image.version)
                    LabeledContent("Size", value: ByteCountFormatter.string(fromByteCount: Int64(image.data.count), countStyle: .file))
                    Text("SHA-256 \(image.sha256)").font(.caption.monospaced()).textSelection(.enabled)
                    Button("Install selected firmware") { pending = "file"; confirm = true }
                }
                Text("Use the firmware.bin application image for CE-to-CE updates. A local file must come from a source you trust.").font(.footnote)
            }
            Section("Install from a URL") {
                TextField("Firmware URL", text: $firmwareURL).keyboardType(.URL).textInputAutocapitalization(.never).autocorrectionDisabled()
                Button("Install from URL") { pending = "url"; confirm = true }.disabled(firmwareURL.isEmpty)
            }
            if let progress = model.progress { Section("Transfer status") { LabeledContent("Stage", value: progress.stage); LabeledContent("Written", value: ByteCountFormatter.string(fromByteCount: Int64(progress.bytes), countStyle: .file)) } }
        }
        .navigationTitle("Firmware updates")
        .disabled(model.busy)
        .task(id: phase) {
            guard phase == .active else { return }
            repeat {
                await model.refreshUpdates()
                do { try await Task.sleep(for: .seconds(5)) } catch { break }
            } while !Task.isCancelled
        }
        .safeAreaInset(edge: .bottom) { NativeMaintenanceMessage() }
        .fileImporter(isPresented: $filePicker, allowedContentTypes: [.data]) { result in
            do {
                let url = try result.get()
                guard url.pathExtension.lowercased() == "bin" else { throw ControllerSettingsError.invalid("Choose a .bin firmware application image.") }
                let accessed = url.startAccessingSecurityScopedResource()
                defer { if accessed { url.stopAccessingSecurityScopedResource() } }
                let attributes = try FileManager.default.attributesOfItem(atPath: url.path)
                guard (attributes[.size] as? NSNumber)?.intValue ?? Int.max <= 0x12c000 else { throw ControllerSettingsError.invalid("The file is too large for the Growhub OTA partition.") }
                image = try FirmwareImage(data: Data(contentsOf: url)); fileName = url.lastPathComponent
            } catch { model.error = error.localizedDescription }
        }
        .confirmationDialog("Install firmware and restart this Growhub?", isPresented: $confirm, titleVisibility: .visible) {
            Button("Install firmware", role: .destructive) { Task { await install() } }
        } message: { Text("\(model.hub.deviceID) will restart. Relay outputs start off after restart. Keep power connected until it finishes, then confirm the installed version before relying on automation.") }
    }
    private func install() async {
        if pending == "file", let image { await model.upload(image) }
        else if pending == "official" { await model.update("install") }
        else if pending == "url" {
            guard let url = URL(string: firmwareURL), ["http", "https"].contains(url.scheme?.lowercased() ?? ""), url.host != nil, url.user == nil, url.password == nil else { model.error = "Enter a complete HTTP or HTTPS firmware URL."; return }
            await model.maintenance("ota_url", value: firmwareURL, disconnects: true)
        }
    }
}
struct NativeDiagnosticsView: View {
    @EnvironmentObject private var model: NativeControllerModel
    @State private var pending: String?
    @State private var confirm = false
    var body: some View {
        Form {
            Section("Controller") {
                LabeledContent("ID", value: model.hub.deviceID)
                LabeledContent("MAC", value: model.hub.id).textSelection(.enabled)
                LabeledContent("Address", value: model.hub.host)
                if let status = model.status {
                    LabeledContent("Firmware", value: status.fw)
                    if let uptime = status.uptime_s { LabeledContent("Uptime", value: Duration.seconds(uptime).formatted(.units(allowed: [.days, .hours, .minutes]))) }
                }
            }
            if let status = model.status {
                Section("Connectivity") {
                    if let wifi = status.wifi { LabeledContent("Wi-Fi", value: wifi ? "Connected" : "Disconnected") }
                    if let rssi = status.rssi { LabeledContent("Signal", value: "\(rssi) dBm") }
                    if let mqtt = status.mqtt { LabeledContent("Command Center", value: mqtt ? "Connected" : "Not connected") }
                    if let ap = status.ap_active { LabeledContent("Setup Wi-Fi", value: ap ? "Active" : "Inactive") }
                    if let reason = status.ap_reason, !reason.isEmpty { LabeledContent("Setup reason", value: reason) }
                }
                Section("Sensors") {
                    if status.hasTemperatureHumidityReading {
                        LabeledContent("Temperature", value: "\(status.temp.formatted(.number.precision(.fractionLength(1))))°\(status.unit)")
                        LabeledContent("Humidity", value: "\(status.rh.formatted(.number.precision(.fractionLength(0))))%")
                    } else { Text("Temperature and humidity are unavailable.") }
                    LabeledContent("Light level", value: status.lightLevel.map { "\($0) / 255" } ?? "Unavailable")
                    LabeledContent("CO₂", value: status.co2_valid == true && status.co2 != nil ? "\(status.co2!) ppm" : "Unavailable")
                    if let warning = status.sensor_warning, !warning.isEmpty { Text(warning).foregroundStyle(.orange) }
                }
                Section("Time") {
                    if let time = status.time { LabeledContent("Controller time", value: time) }
                    if let source = status.time_source { LabeledContent("Source", value: source) }
                    if let sntp = status.sntp_status { LabeledContent("Internet sync", value: sntp) }
                    if let warning = status.time_warning, !warning.isEmpty { Text(warning).foregroundStyle(.orange) }
                }
            }
            if model.settings?.fields["operation_led"] != nil {
                Section("Hardware override") {
                    LabeledContent("Operation LED", value: model.settings?.enabled("operation_led") == true ? "Enabled" : "Disabled")
                    if let pin = model.settings?.fields["operation_led_pin"] { LabeledContent("Operation LED pin", value: "GPIO \(pin)") }
                    if let pin = model.settings?.fields["error_led_pin"] { LabeledContent("Malfunction LED pin", value: "GPIO \(pin)") }
                    Button(model.settings?.enabled("operation_led") == true ? "Disable operation LED" : "Enable operation LED") { pending = "operation_led"; confirm = true }
                    Text("For a suspected LED circuit fault. Changing this restarts the controller; the red malfunction LED remains active.").font(.footnote)
                }
            }
            Section("Actions") {
                Button("Refresh diagnostics") { Task { await model.load() } }
                Button("Restart Growhub") { pending = "reboot"; confirm = true }
                Button("Factory reset", role: .destructive) { pending = "reset"; confirm = true }.disabled(model.settings?.fields["operation_led"] == "0")
                if model.settings?.fields["operation_led"] == "0" { Text("Factory reset is unavailable while the operation-LED hardware override is disabled. Reset would remove that protection.").font(.footnote) }
            }
        }
        .navigationTitle("Diagnostics")
        .disabled(model.busy)
        .safeAreaInset(edge: .bottom) { NativeMaintenanceMessage() }
        .confirmationDialog(pending == "reset" ? "Erase all controller settings?" : "Apply change and restart?", isPresented: $confirm, titleVisibility: .visible) {
            Button(pending == "reset" ? "Erase settings and restart" : "Restart and apply", role: .destructive) {
                Task { await model.maintenance(pending ?? "reboot", value: pending == "operation_led" ? (model.settings?.enabled("operation_led") == true ? "0" : "1") : "1", disconnects: true) }
            }
        } message: { Text(pending == "reset" ? "Wi-Fi credentials, schedules, and controller settings will be erased. Any hardware override returns to its default. Use setup Wi-Fi to configure it again." : "This restarts \(model.hub.deviceID). Relay outputs start off after restart.") }
    }
}
private struct NativeMaintenanceMessage: View {
    @EnvironmentObject var model: NativeControllerModel
    var body: some View {
        if model.busy { ProgressView("Working…").padding().frame(maxWidth: .infinity).background(.regularMaterial) }
        else if let text = model.error ?? model.notice { Text(text).font(.footnote).foregroundStyle(model.error == nil ? Color.secondary : Color.orange).padding().frame(maxWidth: .infinity).background(.regularMaterial) }
    }
}

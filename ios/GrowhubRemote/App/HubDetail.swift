import SwiftUI
import GrowhubCore

struct HubDetail: View {
    let id: String
    @EnvironmentObject private var store: HubStore
    @Environment(\.scenePhase) private var phase
    @State private var fullControls = false
    @State private var confirmManual = false
    @State private var alias = ""
    @State private var rename = false
    @State private var editedOutlet: Int?
    @State private var renameBaseline: ControllerSettings?
    var hub: RememberedHub? { store.hubs.first { $0.id == id } }
    var status: HubStatus? { store.statuses[id] }
    var body: some View {
        Group {
            if let hub {
                List {
                    if store.demoMode { Section { Label("Demo · no hardware connected", systemImage: "play.circle") } }
                    Section {
                        CompactHubReadings(status: status)
                    } header: {
                        CompactHubHeader(hub: hub, status: status, error: store.controllerErrors[id], firmware: store.controllerSettings[id]?.fw) {
                            editedOutlet = nil; alias = hub.alias; rename = true
                        }
                    } footer: {
                        if let status {
                            if !status.hasTemperatureHumidityReading {
                                Text("No valid temperature or humidity reading from this Growhub.")
                            }
                            ForEach([status.time_warning, status.sensor_warning].compactMap { $0 }.filter { !$0.isEmpty }, id: \.self) { Text($0).foregroundStyle(.orange) }
                        }
                    }
                    if let status {
                        if !status.hasOutletModes {
                            Section {
                                HStack {
                                    Text("All outlets")
                                    Spacer()
                                    Text(status.mode == "manual" ? "Manual" : "Auto").foregroundStyle(.secondary)
                                }
                                Button(status.mode == "manual" ? "Resume schedules" : "Use manual control") {
                                    if status.mode == "auto" { confirmManual = true }
                                    else { Task { await store.setMode(hub, outlet: nil, manual: false) } }
                                }
                            } footer: { Text("This firmware shares one mode across all four outlets.") }
                        }
                        Section("Outlets") {
                            ForEach(status.outlet_status) { outlet in
                                VStack(alignment: .leading, spacing: 10) {
                                    let manual = outlet.mode == "manual" || (!status.hasOutletModes && status.mode == "manual")
                                    HStack {
                                        Image(systemName: "powerplug").foregroundStyle(outlet.isOn ? Color.green : Color.secondary)
                                        Button {
                                            Task {
                                                await store.refreshDetails(hub)
                                                guard let settings = store.controllerSettings[id], settings.fields["label_\(outlet.id)"] != nil else {
                                                    store.message = "This controller did not return a shared equipment label for this outlet."
                                                    store.controllerErrors[id] = store.message
                                                    return
                                                }
                                                editedOutlet = outlet.id
                                                renameBaseline = settings
                                                alias = settings.value("label_\(outlet.id)")
                                                rename = true
                                            }
                                        } label: {
                                            HStack(spacing: 6) {
                                                Text(hub.outletName(outlet.id)).font(.headline)
                                                Image(systemName: "pencil").font(.caption).foregroundStyle(.secondary)
                                            }.foregroundStyle(.primary)
                                        }
                                        .buttonStyle(.plain)
                                        .accessibilityLabel("Rename \(hub.outletName(outlet.id))")
                                        .accessibilityIdentifier("rename-outlet-\(outlet.id)")
                                        Spacer()
                                        Toggle("Outlet \(outlet.id) power", isOn: Binding(get: { outlet.isOn }, set: { on in
                                            Task { await store.setOutlet(hub, outlet: outlet.id, on: on) }
                                        })).labelsHidden().disabled(!manual)
                                            .accessibilityLabel("\(hub.outletName(outlet.id)) power")
                                            .accessibilityIdentifier("outlet-\(outlet.id)-power")
                                    }
                                    if hub.outletNames[outlet.id] != nil {
                                        Text("Outlet \(outlet.id)").font(.caption).foregroundStyle(.secondary)
                                    }
                                    if status.hasOutletModes {
                                        Picker("Mode", selection: Binding(get: { outlet.mode ?? "auto" }, set: { value in
                                            Task { await store.setMode(hub, outlet: outlet.id, manual: value == "manual") }
                                        })) { Text("Auto").tag("auto"); Text("Manual").tag("manual") }.pickerStyle(.segmented)
                                    }
                                    if !outlet.summary.isEmpty { Text(outlet.summary).font(.caption).foregroundStyle(.secondary) }
                                }.padding(.vertical, 6)
                            }
                        }.disabled(store.busy.contains(id))
                    } else {
                        Section { Text("Check that this Growhub is powered on and connected to the same Wi-Fi.").foregroundStyle(.secondary) }
                    }
                    Section {
                        Button {
                            Task {
                                if store.demoMode { fullControls = true; return }
                                do {
                                    _ = try await HubAPI.shared.status(host: hub.host, expectedID: hub.id)
                                    fullControls = true
                                } catch { store.message = error.localizedDescription; store.controllerErrors[id] = store.message }
                            }
                        } label: { Label("Full controls", systemImage: "slider.horizontal.3") }
                        Text("Native schedules, assignments, network settings and firmware updates.").font(.caption).foregroundStyle(.secondary)
                    }
                    if let message = store.controllerErrors[id] { Section { Text(message).font(.footnote).foregroundStyle(.orange) } }
                }
                .disabled(store.busy.contains(id))
                .navigationTitle("")
                .navigationBarTitleDisplayMode(.inline)
                .task(id: "\(id)-\(phase)") { if phase == .active { await store.refreshDetails(hub) } }
                .refreshable { await store.refreshDetails(hub) }
                .sheet(isPresented: $fullControls, onDismiss: { Task { await store.refreshDetails(hub) } }) {
                    NavigationStack {
                        NativeControllerView(hub: hub, demo: store.demoMode, settings: store.controllerSettings[id])
                            .navigationTitle(hub.title).navigationBarTitleDisplayMode(.inline)
                            .toolbar { Button("Done") { fullControls = false } }
                    }
                }
                .confirmationDialog("Pause schedules for all four outlets?", isPresented: $confirmManual, titleVisibility: .visible) {
                    Button("Switch all to Manual") { Task { await store.setMode(hub, outlet: nil, manual: true) } }
                } message: { Text("Outputs keep their current state. You can resume schedules at any time.") }
                .alert(editedOutlet == nil ? "Growhub Name" : "Outlet Name", isPresented: $rename) {
                    TextField(editedOutlet == nil ? "Growhub Name" : "Outlet Name", text: $alias)
                    Button("Save") {
                        if let outlet = editedOutlet, let baseline = renameBaseline {
                            Task { await store.renameOutlet(hub, name: alias, outlet: outlet, baseline: baseline) }
                        } else { store.rename(id, name: alias) }
                    }
                    Button("Cancel", role: .cancel) {}
                } message: {
                    Text(editedOutlet.map { "Choose a name for outlet \($0), such as Grow lights or Fan. This is the equipment label saved on the Growhub and shared with its other controls. Leave blank to reset." } ?? "Choose a name for this Growhub. Leave blank to use its original name.")
                }
            } else { ContentUnavailableView("Growhub removed", systemImage: "powerplug") }
        }
    }
}

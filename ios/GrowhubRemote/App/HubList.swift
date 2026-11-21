import SwiftUI
import GrowhubCore

struct HubList: View {
    @EnvironmentObject private var store: HubStore
    @Environment(\.scenePhase) private var phase
    @State private var path: [String] = []
    @State private var showAdd = false
    @State private var address = ""
    @State private var adding = false
    @State private var autoOpened = false
    @State private var showAbout = false
    var body: some View {
        NavigationStack(path: $path) {
            List {
                if store.demoMode {
                    Section {
                        Label("Demo · no hardware connected", systemImage: "play.circle")
                        Button("Exit demo") { path = []; store.setDemo(false); Task { await store.scan() } }
                    }
                }
                Section {
                    ForEach(store.hubs) { hub in
                        Button { autoOpened = true; path = [hub.id] } label: {
                            HStack(spacing: 14) {
                                Image(systemName: "powerplug.fill").font(.title2).foregroundStyle(.green)
                                VStack(alignment: .leading, spacing: 4) {
                                    Text(hub.title).font(.headline).foregroundStyle(.primary)
                                    Text(hub.deviceID).font(.caption).foregroundStyle(.secondary)
                                    Text(store.statuses[hub.id] == nil ? "Not connected" : "Connected").font(.subheadline).foregroundStyle(.secondary)
                                }
                                Spacer()
                                Circle().fill(store.statuses[hub.id] == nil ? Color.gray : Color.green).frame(width: 8, height: 8)
                                Image(systemName: "chevron.right").font(.caption).foregroundStyle(.tertiary)
                            }.padding(.vertical, 8)
                        }
                        .swipeActions { Button("Forget", role: .destructive) { store.forget(hub.id) } }
                    }
                    if store.hubs.isEmpty {
                        ContentUnavailableView("Your Growhubs", systemImage: "leaf", description: Text("Controllers on this Wi-Fi will appear here. Once found, they’re remembered."))
                    }
                } footer: {
                    if store.scanning { HStack { ProgressView(); Text("Finding Growhubs…") } }
                    else { Text("On the same Wi-Fi. No account or cloud required.") }
                }
                if let message = store.message { Section { Text(message).font(.footnote).foregroundStyle(.secondary) } }
            }
            .navigationTitle("Growhub Remote")
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button { Task { await store.scan() } } label: { Image(systemName: "arrow.clockwise") }.disabled(store.scanning).accessibilityLabel("Find Growhubs")
                }
                ToolbarItemGroup(placement: .topBarTrailing) {
                    Button { showAbout = true } label: { Image(systemName: "info.circle") }.accessibilityLabel("Help and demo")
                    Button { showAdd = true } label: { Image(systemName: "plus") }.disabled(store.demoMode).accessibilityLabel("Add address")
                }
            }
            .sheet(isPresented: $showAbout) { AboutView().environmentObject(store) }
            .onChange(of: store.demoMode) { _, _ in path = []; autoOpened = true }
            .onAppear { openSingleGrowhub() }
            .onChange(of: store.scanning) { _, scanning in if !scanning { openSingleGrowhub() } }
            .navigationDestination(for: String.self) { id in HubDetail(id: id) }
            .sheet(isPresented: $showAdd) {
                NavigationStack {
                    Form {
                        TextField("IP address or hostname", text: $address).textInputAutocapitalization(.never).autocorrectionDisabled().keyboardType(.URL)
                        Text("Use the address shown by your router or Growhub. This also works while connected to the Growhub’s setup Wi-Fi.").font(.footnote)
                        Button(adding ? "Connecting…" : "Add Growhub") {
                            adding = true
                            Task {
                                do { try await store.add(address); showAdd = false; address = "" }
                                catch { store.message = error.localizedDescription }
                                adding = false
                            }
                        }.disabled(adding || address.isEmpty)
                        if let message = store.message { Text(message).foregroundStyle(.secondary) }
                    }.navigationTitle("Add address").toolbar { Button("Done") { showAdd = false } }
                }.presentationDetents([.medium])
            }
            .task(id: "\(phase)-\(store.demoMode)") {
                guard phase == .active, !store.demoMode else { return }
                await store.scan()
                openSingleGrowhub()
                var cycles = 0
                while !Task.isCancelled {
                    do { try await Task.sleep(for: .seconds(10)) } catch { break }
                    for hub in store.hubs { await store.refresh(hub) }
                    cycles += 1
                    if cycles % 6 == 0 { await store.scan() }
                }
            }
            .refreshable { await store.scan() }
        }
    }
    private func openSingleGrowhub() {
        guard !autoOpened, path.isEmpty, store.hubs.count == 1, let hub = store.hubs.first else { return }
        autoOpened = true
        path = [hub.id]
    }
}

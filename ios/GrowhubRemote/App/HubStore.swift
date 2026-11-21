import Foundation
import SwiftUI
import GrowhubCore

@MainActor final class HubStore: ObservableObject {
    @Published var hubs: [RememberedHub] = []
    @Published var statuses: [String: HubStatus] = [:]
    @Published var controllerSettings: [String: ControllerSettings] = [:]
    @Published var controllerErrors: [String: String] = [:]
    @Published var scanning = false
    @Published var message: String?
    @Published var demoMode = false
    @Published var busy: Set<String> = []
    private let key = "rememberedGrowhubs.v1"
    private var modeGeneration = 0
    private var bonjour: BonjourDiscovery?
    init() {
        if ProcessInfo.processInfo.arguments.contains("--demo") {
            setDemo(true)
            if ProcessInfo.processInfo.arguments.contains("--single-remembered-demo") { hubs = Array(hubs.prefix(1)) }
            if ProcessInfo.processInfo.arguments.contains("--invalid-sensor-demo") {
                for hub in hubs { statuses[hub.id] = DemoController.status(index: hub.id.hasSuffix("1") ? 0 : 1, sensorAvailable: false) }
            }
            if ProcessInfo.processInfo.arguments.contains("--offline-demo") { statuses = [:] }
            if ProcessInfo.processInfo.arguments.contains("--request-error-demo") {
                for hub in hubs { controllerErrors[hub.id] = "Demo request failed. Refresh to try again." }
            }
            return
        }
        loadRemembered()
    }
    private func loadRemembered() {
        if let data = UserDefaults.standard.data(forKey: key),
           let saved = try? JSONDecoder().decode([RememberedHub].self, from: data) { hubs = saved }
    }
    func persist() {
        guard !demoMode else { return }
        if let data = try? JSONEncoder().encode(hubs) { UserDefaults.standard.set(data, forKey: key) }
    }
    func remember(_ status: HubStatus, host: String) {
        // A DHCP reassignment must not leave two remembered identities sharing an address.
        for hub in hubs where hub.host == host && hub.id != status.id { statuses[hub.id] = nil }
        if let index = hubs.firstIndex(where: { $0.id == status.id }) {
            hubs[index].host = host; hubs[index].name = status.name
        } else { hubs.append(RememberedHub(status: status, host: host)) }
        statuses[status.id] = status
        controllerErrors[status.id] = nil
        persist()
    }
    func refresh(_ hub: RememberedHub) async {
        guard !demoMode else { return }
        guard !busy.contains(hub.id) else { return }
        let generation = modeGeneration
        do {
            let result = try await HubAPI.shared.status(host: hub.host, expectedID: hub.id)
            guard generation == modeGeneration else { return }
            remember(result, host: hub.host)
        } catch { if generation == modeGeneration { statuses[hub.id] = nil } }
    }
    func add(_ host: String) async throws {
        guard !demoMode else { return }
        _ = try HubAPI.baseURL(host)
        let normalized = try HubAPI.baseURL(host).absoluteString.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        remember(try await HubAPI.shared.status(host: normalized), host: normalized)
    }
    func forget(_ id: String) { hubs.removeAll { $0.id == id }; statuses[id] = nil; controllerSettings[id] = nil; controllerErrors[id] = nil; persist() }
    func rename(_ id: String, name: String) {
        guard let index = hubs.firstIndex(where: { $0.id == id }) else { return }
        hubs[index].alias = name.trimmingCharacters(in: .whitespacesAndNewlines)
        persist()
    }
    func receiveSettings(_ settings: ControllerSettings) {
        guard let index = hubs.firstIndex(where: { $0.id == settings.mac }) else { return }
        controllerSettings[settings.mac] = settings
        hubs[index].applyOutletLabels(settings)
        persist()
    }
    func refreshDetails(_ hub: RememberedHub) async {
        if demoMode {
            if controllerSettings[hub.id] == nil { receiveSettings(NativeControllerModel.demoSettings(hub)) }
            return
        }
        await refresh(hub)
        let generation = modeGeneration
        do {
            let settings = try await HubAPI.shared.settings(hub)
            guard generation == modeGeneration else { return }
            receiveSettings(settings)
        } catch { if generation == modeGeneration { message = error.localizedDescription; controllerErrors[hub.id] = message } }
    }
    func renameOutlet(_ hub: RememberedHub, name: String, outlet: Int, baseline: ControllerSettings) async {
        guard !busy.contains(hub.id) else { return }
        busy.insert(hub.id); message = nil; controllerErrors[hub.id] = nil
        let generation = modeGeneration
        defer { busy.remove(hub.id) }
        do {
            let params = try baseline.outletLabelParameters(name, outlet: outlet)
            if demoMode {
                var settings = controllerSettings[hub.id] ?? baseline
                settings.fields.merge(params) { _, new in new }
                receiveSettings(settings)
            } else if let settings = try await HubAPI.shared.writeSettings(hub, params: params, baseline: baseline) {
                guard generation == modeGeneration else { return }
                receiveSettings(settings)
            }
        } catch { if generation == modeGeneration { message = error.localizedDescription; controllerErrors[hub.id] = message } }
    }
    func scan() async {
        guard !demoMode, !scanning else { return }
        if bonjour == nil {
            bonjour = BonjourDiscovery { [weak self] host in
                Task { @MainActor in
                    guard let status = try? await HubAPI.shared.status(host: host) else { return }
                    guard self?.demoMode == false else { return }
                    self?.remember(status, host: host)
                }
            }
            bonjour?.start()
        }
        scanning = true; message = nil
        defer { scanning = false }
        let network = LAN.candidates()
        let candidates = Array(Set(hubs.map(\.host) + network.hosts + ["192.168.4.1"]))
        await withTaskGroup(of: (String, HubStatus?).self) { group in
            var next = 0
            func enqueue(_ host: String) {
                group.addTask {
                    guard !Task.isCancelled else { return (host, nil) }
                    return (host, try? await HubAPI.shared.status(host: host))
                }
            }
            for host in candidates.prefix(24) { enqueue(host); next += 1 }
            while let (host, status) = await group.next() {
                if Task.isCancelled { group.cancelAll(); break }
                if demoMode { group.cancelAll(); break }
                if let status { remember(status, host: host) }
                if next < candidates.count { enqueue(candidates[next]); next += 1 }
            }
        }
        if network.hosts.isEmpty { message = "Connect to Wi-Fi and allow Local Network access in Settings. You can also add an address." }
        else if network.limited { message = "This is a large network. Discovery searched nearby addresses; add other addresses manually." }
        else if statuses.isEmpty { message = "No controllers responded. Check Wi-Fi and Local Network access, or add an address." }
    }
    func setDemo(_ enabled: Bool) {
        modeGeneration += 1
        demoMode = enabled; message = nil; statuses = [:]; controllerSettings = [:]; controllerErrors = [:]; hubs = []
        if enabled {
            bonjour?.stop(); bonjour = nil
            for i in 0...1 {
                let s = DemoController.status(index: i, manual: i == 0)
                let h = RememberedHub(status: s, host: "Demo controller")
                hubs.append(h); statuses[h.id] = s
            }
        } else { loadRemembered() }
    }
    func setOutlet(_ hub: RememberedHub, outlet: Int, on: Bool) async {
        if demoMode, let status = statuses[hub.id] {
            guard status.mode == "manual" else { return }
            statuses[hub.id] = DemoController.changing(status, outlet: outlet, on: on)
        } else { await run(hub) { try await HubAPI.shared.setOutlet(hub, outlet: outlet, on: on) } }
    }
    func setMode(_ hub: RememberedHub, outlet: Int?, manual: Bool) async {
        if demoMode, let status = statuses[hub.id] { statuses[hub.id] = DemoController.changing(status, manual: manual) }
        else { await run(hub) { try await HubAPI.shared.setMode(hub, outlet: outlet, manual: manual) } }
    }
    func run(_ hub: RememberedHub, action: @escaping () async throws -> HubStatus) async {
        guard !busy.contains(hub.id) else { return }
        busy.insert(hub.id)
        controllerErrors[hub.id] = nil
        let generation = modeGeneration
        defer { busy.remove(hub.id) }
        do {
            let result = try await action()
            guard generation == modeGeneration else { return }
            statuses[hub.id] = result
        } catch {
            guard generation == modeGeneration else { return }
            message = error.localizedDescription
            controllerErrors[hub.id] = message
            await refreshAfterFailure(hub)
        }
    }
    private func refreshAfterFailure(_ hub: RememberedHub) async {
        statuses[hub.id] = try? await HubAPI.shared.status(host: hub.host, expectedID: hub.id)
    }
}

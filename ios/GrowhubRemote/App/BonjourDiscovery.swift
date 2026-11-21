import Foundation

// Optional fast path for controllers advertising a Growhub-named HTTP service.
// Existing CE releases are also found through LAN probes.
@MainActor final class BonjourDiscovery: NSObject, NetServiceBrowserDelegate, NetServiceDelegate {
    private let browser = NetServiceBrowser()
    private var services: [NetService] = []
    private let found: (String) -> Void
    init(found: @escaping (String) -> Void) { self.found = found; super.init() }
    func start() { browser.delegate = self; browser.searchForServices(ofType: "_http._tcp.", inDomain: "local.") }
    func stop() { browser.stop(); services.forEach { $0.stop() }; services = [] }
    nonisolated func netServiceBrowser(_ browser: NetServiceBrowser, didFind service: NetService, moreComing: Bool) {
        Task { @MainActor in
            guard service.name.lowercased().contains("growhub") else { return }
            self.services.append(service); service.delegate = self; service.resolve(withTimeout: 3)
        }
    }
    nonisolated func netServiceDidResolveAddress(_ sender: NetService) {
        Task { @MainActor in
            guard let host = sender.hostName, sender.port > 0 else { return }
            self.found("\(host):\(sender.port)")
        }
    }
}

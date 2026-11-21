import Foundation

public struct HubAPI: Sendable {
    public static let shared = HubAPI()
    let session: URLSession
    public init(session: URLSession) { self.session = session }
    public init() {
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 2
        config.timeoutIntervalForResource = 30
        config.httpMaximumConnectionsPerHost = 1
        config.requestCachePolicy = .reloadIgnoringLocalCacheData
        session = URLSession(configuration: config)
    }
    public static func baseURL(_ host: String) throws -> URL {
        let input = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let c = URLComponents(string: input.contains("://") ? input : "http://" + input),
              c.scheme == "http", let name = c.host, !name.isEmpty,
              c.user == nil, c.password == nil, c.query == nil, c.fragment == nil,
              c.path.isEmpty || c.path == "/", let url = c.url else { throw HubError.invalidAddress }
        return url
    }
    public func status(host: String, expectedID: String? = nil) async throws -> HubStatus {
        let url = try Self.baseURL(host).appendingPathComponent("status")
        var request = URLRequest(url: url)
        request.setValue("close", forHTTPHeaderField: "Connection")
        let (data, response) = try await session.data(for: request)
        guard (response as? HTTPURLResponse)?.statusCode == 200, data.count <= 65536,
              let status = try? JSONDecoder().decode(HubStatus.self, from: data), status.isValidHub else { throw HubError.notGrowhub }
        if let expectedID, status.id != expectedID { throw HubError.identityChanged }
        return status
    }
    private func send(_ hub: RememberedHub, params: [String: String]) async throws -> HubStatus {
        var c = URLComponents(url: try Self.baseURL(hub.host).appendingPathComponent("save"), resolvingAgainstBaseURL: false)!
        c.queryItems = params.sorted { $0.key < $1.key }.map { URLQueryItem(name: $0.key, value: $0.value) }
        var request = URLRequest(url: c.url!)
        request.setValue("close", forHTTPHeaderField: "Connection")
        let (_, response) = try await session.data(for: request)
        guard let response = response as? HTTPURLResponse, (200...399).contains(response.statusCode) else { throw HubError.commandNotConfirmed }
        return try await status(host: hub.host, expectedID: hub.id)
    }
    public func setMode(_ hub: RememberedHub, outlet: Int?, manual: Bool) async throws -> HubStatus {
        let before = try await status(host: hub.host, expectedID: hub.id)
        let mode = manual ? "manual" : "auto"
        let result: HubStatus
        if let outlet {
            guard before.hasOutletModes, (1...4).contains(outlet) else { throw HubError.commandNotConfirmed }
            result = try await send(hub, params: ["control_outlet": "\(outlet)", "outlet_mode": mode])
            guard result.outlet_status.first(where: { $0.id == outlet })?.mode == mode else { throw HubError.commandNotConfirmed }
        } else {
            result = try await send(hub, params: ["mode": mode])
            guard result.hasOutletModes ? result.outlet_status.allSatisfy({ $0.mode == mode }) : result.mode == mode else { throw HubError.commandNotConfirmed }
        }
        return result
    }
    public func setOutlet(_ hub: RememberedHub, outlet: Int, on: Bool) async throws -> HubStatus {
        let before = try await status(host: hub.host, expectedID: hub.id)
        guard (1...4).contains(outlet) else { throw HubError.commandNotConfirmed }
        let result: HubStatus
        if before.hasOutletModes {
            guard before.outlet_status.first(where: { $0.id == outlet })?.mode == "manual" else { throw HubError.manualRequired }
            result = try await send(hub, params: ["control_outlet": "\(outlet)", "outlet_state": on ? "on" : "off"])
        } else {
            guard before.mode == "manual" else { throw HubError.manualRequired }
            var mask = 0
            for o in before.outlet_status where o.isOn { mask |= 1 << (o.id == 1 ? 3 : o.id - 2) }
            let bit = 1 << (outlet == 1 ? 3 : outlet - 2)
            mask = on ? mask | bit : mask & ~bit
            result = try await send(hub, params: ["relay_mask": "\(mask)"])
        }
        guard result.outlet_status.first(where: { $0.id == outlet })?.isOn == on else { throw HubError.commandNotConfirmed }
        return result
    }
}

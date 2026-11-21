import XCTest
@testable import GrowhubCore

private let legacy = """
{"mac":"AABBCCDDEEFF","fw":"1.2.0C","name":"Greenhouse","mode":"manual","temp":22,"rh":55,"unit":"C","outlet_status":[{"id":1,"state":"on","summary":""},{"id":2,"state":"off","summary":""},{"id":3,"state":"off","summary":""},{"id":4,"state":"off","summary":""}]}
"""
private final class FakeHTTP: URLProtocol {
    static var responses: [Data] = []
    static var requests: [URLRequest] = []
    static var codes: [Int] = []
    static let lock = NSLock()
    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        var captured = request
        if captured.httpBody == nil, let stream = captured.httpBodyStream {
            stream.open(); defer { stream.close() }
            var data = Data(), buffer = [UInt8](repeating: 0, count: 4096)
            while stream.hasBytesAvailable {
                let count = stream.read(&buffer, maxLength: buffer.count)
                if count <= 0 { break }
                data.append(buffer, count: count)
            }
            captured.httpBody = data
        }
        Self.lock.lock()
        Self.requests.append(captured)
        let data = Self.responses.isEmpty ? Data() : Self.responses.removeFirst()
        let code = Self.codes.isEmpty ? 200 : Self.codes.removeFirst()
        Self.lock.unlock()
        let response = HTTPURLResponse(url: request.url!, statusCode: code, httpVersion: nil, headerFields: nil)!
        client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: data)
        client?.urlProtocolDidFinishLoading(self)
    }
    override func stopLoading() {}
}
final class HubTests: XCTestCase {
    private func api(_ replies: [String], codes: [Int] = []) -> HubAPI {
        FakeHTTP.responses = replies.map { Data($0.utf8) }; FakeHTTP.requests = []
        FakeHTTP.codes = codes
        let config = URLSessionConfiguration.ephemeral; config.protocolClasses = [FakeHTTP.self]
        return HubAPI(session: URLSession(configuration: config))
    }
    private func decode(_ text: String = legacy) throws -> HubStatus { try JSONDecoder().decode(HubStatus.self, from: Data(text.utf8)) }
    private func settings(fields: [String: String] = [:], mac: String = "AABBCCDDEEFF") -> ControllerSettings {
        let required = ["dev_name":"Garden", "outlet_1":"", "outlet_2":"", "outlet_3":"", "outlet_4":""]
        return ControllerSettings(api: 1, mac: mac, fw: "1.2.5C", fields: required.merging(fields) { _, new in new }, choices: [:], capabilities: [:])
    }
    private func json(_ settings: ControllerSettings) throws -> String { String(data: try JSONEncoder().encode(settings), encoding: .utf8)! }
    func testReadOnlyControllerIntegration() async throws {
        guard let host = ProcessInfo.processInfo.environment["GROWHUB_TEST_HOST"] else { throw XCTSkip("Set GROWHUB_TEST_HOST for read-only hardware compatibility validation") }
        let client = HubAPI()
        let status = try await client.status(host: host)
        let settings = try await client.settings(RememberedHub(status: status, host: host))
        XCTAssertEqual(settings.mac, status.id)
        XCTAssertEqual(settings.value("dev_name"), status.name)
        XCTAssertTrue((1...4).allSatisfy { settings.fields["outlet_\($0)"] != nil })
        XCTAssertNil(settings.fields["password"])
        print("READ_ONLY_COMPATIBILITY firmware=\(status.fw) nativeAPI=\(settings.usesNativeAPI) fields=\(settings.fields.count)")
    }
    func testFirmwareProducedSnapshotDecodesAndPreservesUnitsAndOutletMapping() throws {
        let data = try Data(contentsOf: XCTUnwrap(Bundle.module.url(forResource: "api-v1-settings", withExtension: "json")))
        let snapshot = try JSONDecoder().decode(ControllerSettings.self, from: data)
        XCTAssertEqual(snapshot.value("outlet_1"), "Light")
        XCTAssertEqual(snapshot.value("outlet_2"), "Water Pump")
        XCTAssertEqual(snapshot.value("s3_temp_low"), "64.4")
        XCTAssertEqual(snapshot.scheduleParameters()["s2_run"], "12")
        XCTAssertEqual(snapshot.capabilities["co2"], false)
        XCTAssertNil(snapshot.fields["password"])
    }
    func testFirmwareImageRejectsFirstFlashAndBootloaderAndExtractsAppVersion() throws {
        XCTAssertThrowsError(try FirmwareImage(data: Data(repeating: 0xff, count: 512)))
        var bytes = Data(repeating: 0, count: 512)
        bytes[0] = 0xe9
        XCTAssertThrowsError(try FirmwareImage(data: bytes))
        bytes.replaceSubrange(32..<36, with: [0x32, 0x54, 0xcd, 0xab])
        bytes.replaceSubrange(48..<55, with: Data("1.2.5C".utf8) + Data([0]))
        let image = try FirmwareImage(data: bytes)
        XCTAssertEqual(image.version, "1.2.5C")
        XCTAssertEqual(image.sha256.count, 64)
        bytes[12] = 9
        XCTAssertThrowsError(try FirmwareImage(data: bytes))
    }
    func testLegacySettingsSaveUsesOldEndpointAndVerifiesReadback() async throws {
        let page = "<input name='dev_name' value='Garden'>" + (1...4).map { "<select name='outlet_\($0)'><option value=''>None</select>" }.joined()
        let changed = page.replacingOccurrences(of: "value='Garden'", with: "value='Kitchen'")
        let hub = RememberedHub(status: try decode(), host: "192.168.1.5")
        let baseline = try ControllerSettings.legacy(html: page, status: decode())
        let replies = [legacy,"",page,"",legacy,"OK",legacy,"",changed,""]
        let client = api(replies, codes: [200,404,200,404,200,200,200,404,200,404])
        let result = try await client.writeSettings(hub, params: ["dev_name":"Kitchen"], baseline: baseline)
        XCTAssertEqual(result?.value("dev_name"), "Kitchen")
        XCTAssertEqual(FakeHTTP.requests[5].url?.path, "/save")
        XCTAssertEqual(FakeHTTP.requests[5].httpMethod, "POST")
    }
    func testLegacySettingsAdapterReadsValuesWithoutPasswordsOrScripts() throws {
        let page = """
        <input name='dev_name' value='Garden &amp; lights'><input name='ssid' value='Garden Wi-Fi'>
        <input type='password' name='password' value='must-not-be-retained'>
        <input type='hidden' name='sched_v3' value='1'>
        <input type='checkbox' name='s1_time_en' value='1' checked><input type='checkbox' name='s2_always' value='1'>
        <input type='radio' name='temp_unit' value='0'><input type='radio' name='temp_unit' value='1' checked>
        <select name='outlet_1'><option value=''>None<option value='Light' selected>Light</select>
        <select name='outlet_2'><option value='Fan'>Fan</select><select name='outlet_3'><option value=''>None</select><select name='outlet_4'><option value='Water Pump'>Pump</select>
        <select name='timezone'><option value='UTC0'>UTC<option value='EST5EDT' selected>Eastern</select>
        <input name='label_1' value='Light &quot;A&quot;'>
        <script>document.write("<input name='dev_name' value='evil'>")</script>
        """
        let result = try ControllerSettings.legacy(html: page, diagnostics: "<input type='radio' name='operation_led' value='0' checked>", status: decode())
        XCTAssertFalse(result.usesNativeAPI)
        XCTAssertEqual(result.value("dev_name"), "Garden & lights")
        XCTAssertEqual(result.value("label_1"), "Light \"A\"")
        XCTAssertNil(result.fields["password"])
        XCTAssertEqual(result.value("temp_unit"), "1")
        XCTAssertTrue(result.enabled("s1_time_en")); XCTAssertFalse(result.enabled("s2_always"))
        XCTAssertEqual(result.value("operation_led"), "0")
        XCTAssertEqual(result.value("timezone"), "EST5EDT")
    }
    func testSettingsFallbackIsLimitedToUnsupportedAPIStatus() async throws {
        let html = "<input name='dev_name' value='Garden'>" + (1...4).map { "<select name='outlet_\($0)'><option value=''>None</select>" }.joined()
        let hub = RememberedHub(status: try decode(), host: "192.168.1.5")
        let client = api([legacy, "", html, ""], codes: [200,404,200,404])
        let result = try await client.settings(hub)
        XCTAssertFalse(result.usesNativeAPI)
        XCTAssertEqual(FakeHTTP.requests.map { $0.url!.path }, ["/status", "/api/v1/settings", "/", "/diagnostics"])
    }
    func testNativeSettingsRejectIdentityMismatch() async throws {
        let hub = RememberedHub(status: try decode(), host: "192.168.1.5")
        let client = api([legacy, try json(settings(mac: "112233445566"))])
        do { _ = try await client.settings(hub); XCTFail("Reject another controller's settings") } catch HubError.identityChanged {} catch { XCTFail("Unexpected error: \(error)") }
        XCTAssertEqual(FakeHTTP.requests.count, 2)
    }
    func testSettingsSaveDetectsConcurrentEditsBeforeSending() async throws {
        let hub = RememberedHub(status: try decode(), host: "192.168.1.5")
        let original = settings(fields: ["label_1":"Old"])
        let client = api([legacy, try json(settings(fields: ["label_1":"Changed elsewhere"]))])
        do { _ = try await client.writeSettings(hub, params: ["label_1":"Mine"], baseline: original); XCTFail("Must reject stale configuration") } catch ControllerSettingsError.changed {} catch { XCTFail("Unexpected error: \(error)") }
        XCTAssertTrue(FakeHTTP.requests.allSatisfy { $0.httpMethod == "GET" })
    }
    func testSettingsSaveUsesIdentityHeaderEncodedBodyAndReadback() async throws {
        let hub = RememberedHub(status: try decode(), host: "192.168.1.5")
        let original = settings(fields: ["label_1":"Old", "label_2":"Fan"])
        let after = settings(fields: ["label_1":"A+B & C", "label_2":"Fan"])
        let client = api([legacy, try json(original), legacy, "{\"accepted\":true}", legacy, try json(after)])
        let result = try await client.writeSettings(hub, params: ["label_1":"A+B & C"], baseline: original)
        XCTAssertEqual(result?.value("label_2"), "Fan")
        let command = FakeHTTP.requests[3]
        XCTAssertEqual(command.url?.path, "/api/v1/settings")
        XCTAssertEqual(command.httpMethod, "POST")
        XCTAssertEqual(command.value(forHTTPHeaderField: "X-Growhub-ID"), hub.id)
        XCTAssertEqual(String(data: command.httpBody!, encoding: .utf8), "label_1=A%2BB%20%26%20C")
    }
    func testScheduleSerializationPreservesOtherOutletsAndOmitsInactiveConditions() {
        var config = settings(fields: ["outlet_1":"Light", "s1_always":"1", "s1_time_en":"1", "s1_start":"05:00", "s1_end":"23:00", "outlet_2":"Water Pump", "s2_interval_en":"1", "s2_run":"15.0", "s2_every":"4.0", "s2_win_en":"1", "s2_wstart":"07:00", "s2_wend":"19:00"])
        let params = config.scheduleParameters()
        XCTAssertEqual(params["s1_always"], "1"); XCTAssertNil(params["s1_time_en"]); XCTAssertNil(params["s1_start"])
        XCTAssertEqual(params["s2_run"], "15"); XCTAssertEqual(params["s2_wend"], "19:00")
        config.fields["s1_always"] = ""
        XCTAssertEqual(config.scheduleParameters()["s1_start"], "05:00")
    }
    func testFactoryResetCannotRemoveHardwareFaultOverride() async throws {
        let hub = RememberedHub(status: try decode(), host: "192.168.1.5")
        let before = settings(fields: ["operation_led":"0"])
        let client = api([legacy, try json(before)])
        do { _ = try await client.writeSettings(hub, params: ["reset":"1"], baseline: before, mayDisconnect: true); XCTFail("Must preserve hardware override") } catch {}
        XCTAssertTrue(FakeHTTP.requests.allSatisfy { $0.httpMethod == "GET" })
    }
    func testRememberedNamesMigrateAndRemainPerController() throws {
        let previous = """
        [{"id":"AABBCCDDEEFF","host":"192.168.1.5","name":"GrowHub_EEFF","alias":"My greenhouse"}]
        """
        var hubs = try JSONDecoder().decode([RememberedHub].self, from: Data(previous.utf8))
        XCTAssertEqual(hubs[0].title, "My greenhouse")
        XCTAssertEqual(hubs[0].deviceID, "GrowHub_EEFF")
        XCTAssertEqual(hubs[0].outletName(1), "Outlet 1")
        hubs[0].setOutletName(" Grow lights ", outlet: 1)
        hubs.append(RememberedHub(status: DemoController.status(index: 1), host: "192.168.1.6"))
        let restored = try JSONDecoder().decode([RememberedHub].self, from: JSONEncoder().encode(hubs))
        XCTAssertEqual(restored[0].outletName(1), "Grow lights")
        XCTAssertEqual(restored[0].title, "My greenhouse")
        XCTAssertEqual(restored[0].deviceID, "GrowHub_EEFF")
        XCTAssertEqual(restored[1].outletName(1), "Outlet 1")
        hubs[0].setOutletName(" \n ", outlet: 1)
        XCTAssertEqual(hubs[0].outletName(1), "Outlet 1")
    }
    func testControllerLabelsReplaceCachedAppNamesAndClearAcrossRefresh() throws {
        var hub = RememberedHub(status: try decode(), host: "192.168.1.5")
        hub.setOutletName("Old app-only name", outlet: 1)
        let shared = settings(fields: ["label_1":"Grow lights", "label_2":"Exhaust fan"])
        hub.applyOutletLabels(shared)
        XCTAssertEqual(hub.outletName(1), shared.outletName(1))
        XCTAssertEqual(hub.outletName(2), "Exhaust fan")
        hub.applyOutletLabels(settings(fields: ["label_1":"Other controller"], mac: "112233445566"))
        XCTAssertEqual(hub.outletName(1), "Grow lights")
        hub.applyOutletLabels(settings(fields: ["label_1":""]))
        XCTAssertEqual(hub.outletName(1), "Outlet 1")
        XCTAssertEqual(hub.outletName(2), "Exhaust fan")
        let restored = try JSONDecoder().decode(RememberedHub.self, from: JSONEncoder().encode(hub))
        XCTAssertEqual(restored.outletName(2), "Exhaust fan")
    }
    func testOutletRenameOnlyWritesTheSharedLabelWithFirmwareLimits() throws {
        let shared = settings(fields: ["label_1":"Before", "label_2":"Fan", "outlet_1":"Light", "s1_time_en":"1"])
        XCTAssertEqual(try shared.outletLabelParameters(" Grow lights ", outlet: 1), ["label_1":"Grow lights"])
        XCTAssertEqual(try shared.outletLabelParameters("  ", outlet: 2), ["label_2":""])
        XCTAssertThrowsError(try shared.outletLabelParameters("Name", outlet: 3))
        XCTAssertThrowsError(try shared.outletLabelParameters("Name", outlet: 0))
        XCTAssertThrowsError(try shared.outletLabelParameters(String(repeating: "é", count: 17), outlet: 1))
        XCTAssertThrowsError(try shared.outletLabelParameters("Bad\nname", outlet: 1))
        XCTAssertEqual(shared.value("s1_time_en"), "1")
    }
    func testDemoCommandsAreLocalAndKeepOtherOutlets() {
        let before = DemoController.status(index: 0)
        let changed = DemoController.changing(before, outlet: 1, on: true)
        XCTAssertTrue(changed.isValidHub)
        XCTAssertEqual(changed.id, before.id)
        XCTAssertTrue(changed.outlet_status[0].isOn)
        XCTAssertEqual(changed.outlet_status[1].isOn, before.outlet_status[1].isOn)
        XCTAssertEqual(DemoController.changing(changed, manual: false).mode, "auto")
        XCTAssertNotEqual(DemoController.status(index: 1).id, before.id)
    }
    func testIdentityAndCapabilities() throws {
        let old = try decode(); XCTAssertTrue(old.isValidHub); XCTAssertFalse(old.hasOutletModes)
        let new = try decode(legacy.replacingOccurrences(of: "\"summary\":\"\"", with: "\"summary\":\"\",\"mode\":\"manual\""))
        XCTAssertTrue(new.hasOutletModes)
        XCTAssertFalse(try decode(legacy.replacingOccurrences(of: "AABBCCDDEEFF", with: "not-a-mac")).isValidHub)
        XCTAssertFalse(try decode(legacy.replacingOccurrences(of: "\"id\":4", with: "\"id\":3")).isValidHub)
    }
    func testInvalidSensorReadingsAreNotDisplayedAsZeroHumidity() throws {
        XCTAssertTrue(try decode().hasTemperatureHumidityReading)
        let empty = legacy.replacingOccurrences(of: "\"temp\":22,\"rh\":55", with: "\"temp\":0,\"rh\":0")
        XCTAssertFalse(try decode(empty).hasTemperatureHumidityReading)
        let fahrenheit = empty.replacingOccurrences(of: "\"temp\":0", with: "\"temp\":32").replacingOccurrences(of: "\"unit\":\"C\"", with: "\"unit\":\"F\"")
        XCTAssertFalse(try decode(fahrenheit).hasTemperatureHumidityReading)
        XCTAssertFalse(try decode(legacy.replacingOccurrences(of: "\"temp\":22", with: "\"temp_valid\":false,\"temp\":22")).hasTemperatureHumidityReading)
        XCTAssertTrue(try decode(empty.replacingOccurrences(of: "\"temp\":0", with: "\"temp_valid\":true,\"temp\":0")).hasTemperatureHumidityReading)
        XCTAssertEqual(DemoController.status(index: 0).lightLevel, 180)
        XCTAssertNil(DemoController.status(index: 0, sensorAvailable: false).lightLevel)
        XCTAssertEqual(try decode(legacy.replacingOccurrences(of: "\"temp\":22", with: "\"light\":0,\"temp\":22")).lightLevel, 0)
    }
    func testAddressValidation() throws {
        XCTAssertEqual(try HubAPI.baseURL("192.168.1.5").host, "192.168.1.5")
        XCTAssertEqual(try HubAPI.baseURL("growhub.local:8080").port, 8080)
        for bad in ["https://example.com", "http://user:password@host", "host/save?reset=1", "host/#bad", ""] { XCTAssertThrowsError(try HubAPI.baseURL(bad)) }
    }
    func testReassignedAddressDoesNotReceiveCommands() async throws {
        let hub = RememberedHub(status: try decode(), host: "192.168.1.5")
        let client = api([legacy.replacingOccurrences(of: "AABBCCDDEEFF", with: "112233445566")])
        do { _ = try await client.setOutlet(hub, outlet: 2, on: true); XCTFail("Must reject a different controller") } catch {}
        XCTAssertEqual(FakeHTTP.requests.count, 1)
        XCTAssertEqual(FakeHTTP.requests.first?.url?.path, "/status")
    }
    func testLegacyAutoDoesNotSilentlyPauseSchedules() async throws {
        let hub = RememberedHub(status: try decode(), host: "192.168.1.5")
        let client = api([legacy.replacingOccurrences(of: "\"mode\":\"manual\"", with: "\"mode\":\"auto\"")])
        do { _ = try await client.setOutlet(hub, outlet: 2, on: true); XCTFail("AUTO must reject manual control") } catch {}
        XCTAssertEqual(FakeHTTP.requests.count, 1)
    }
    func testLegacyTogglePreservesOtherOutletBits() async throws {
        let hub = RememberedHub(status: try decode(), host: "192.168.1.5")
        let after = legacy.replacingOccurrences(of: "\"id\":2,\"state\":\"off\"", with: "\"id\":2,\"state\":\"on\"")
        let client = api([legacy, "", after])
        let result = try await client.setOutlet(hub, outlet: 2, on: true)
        XCTAssertTrue(result.outlet_status[0].isOn); XCTAssertTrue(result.outlet_status[1].isOn)
        XCTAssertEqual(URLComponents(url: FakeHTTP.requests[1].url!, resolvingAgainstBaseURL: false)?.queryItems?.first?.value, "9")
    }
    func testNewFirmwareUsesTargetedCommand() async throws {
        let before = legacy.replacingOccurrences(of: "\"summary\":\"\"", with: "\"summary\":\"\",\"mode\":\"manual\"")
        let after = before.replacingOccurrences(of: "\"id\":2,\"state\":\"off\"", with: "\"id\":2,\"state\":\"on\"")
        let hub = RememberedHub(status: try decode(before), host: "192.168.1.5")
        let client = api([before, "", after])
        _ = try await client.setOutlet(hub, outlet: 2, on: true)
        let items = URLComponents(url: FakeHTTP.requests[1].url!, resolvingAgainstBaseURL: false)!.queryItems!
        XCTAssertEqual(Set(items.map(\.name)), Set(["control_outlet", "outlet_state"]))
        XCTAssertEqual(items.first(where: { $0.name == "control_outlet" })?.value, "2")
    }
    func testUnconfirmedCommandIsAnError() async throws {
        let hub = RememberedHub(status: try decode(), host: "192.168.1.5")
        let client = api([legacy, "", legacy])
        do { _ = try await client.setOutlet(hub, outlet: 2, on: true); XCTFail("Must verify the result") } catch {}
    }
}

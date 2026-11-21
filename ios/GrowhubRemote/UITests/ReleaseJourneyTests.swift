import XCTest

final class ReleaseJourneyTests: XCTestCase {
    func testCompactHeaderKeepsDetailsBehindInfoAndNamesEditable() {
        let app = XCUIApplication()
        app.launchArguments = ["--demo", "--single-remembered-demo"]
        app.launch()
        XCTAssertTrue(app.staticTexts["growhub-name"].waitForExistence(timeout: 8))
        XCTAssertEqual(app.staticTexts["growhub-name"].label, "Greenhouse")
        XCTAssertTrue(app.staticTexts["GrowHub_0001"].exists)
        XCTAssertTrue(app.buttons["Rename Growhub"].exists)
        XCTAssertFalse(app.staticTexts["Demo controller"].exists)
        XCTAssertFalse(app.descendants(matching: .any).matching(NSPredicate(format: "label CONTAINS %@ OR value CONTAINS %@", "CE 1.2.0C", "CE 1.2.0C")).firstMatch.exists)
        capture("07-compact-header")
        app.buttons["Device details"].tap()
        XCTAssertTrue(app.descendants(matching: .any)["device-ip-address"].waitForExistence(timeout: 3))
        XCTAssertTrue(app.descendants(matching: .any).matching(NSPredicate(format: "label CONTAINS %@ OR value CONTAINS %@", "CE 1.2.0C", "CE 1.2.0C")).firstMatch.exists)
        capture("08-device-details")
        app.buttons["Close device details"].tap()
        XCTAssertFalse(app.descendants(matching: .any).matching(NSPredicate(format: "label CONTAINS %@ OR value CONTAINS %@", "CE 1.2.0C", "CE 1.2.0C")).firstMatch.exists)
        app.buttons["Rename Growhub"].tap()
        XCTAssertTrue(app.alerts["Growhub Name"].exists)
        app.alerts.buttons["Cancel"].tap()
    }
    func testConnectionIndicatorShowsConnectedOfflineAndRequestError() {
        for (flag, label) in [("", "Connected"), ("--offline-demo", "Not connected"), ("--request-error-demo", "Request error")] {
            let app = XCUIApplication()
            app.launchArguments = ["--demo", "--single-remembered-demo"] + (flag.isEmpty ? [] : [flag])
            app.launch()
            let indicator = app.images["growhub-connection"]
            XCTAssertTrue(indicator.waitForExistence(timeout: 8))
            XCTAssertEqual(indicator.label, label)
            app.terminate()
        }
    }
    func testInvalidSensorReadingsShowUnavailable() {
        let app = XCUIApplication()
        app.launchArguments = ["--demo", "--invalid-sensor-demo"]
        app.launch()
        app.buttons.matching(NSPredicate(format: "label CONTAINS %@", "Greenhouse")).firstMatch.tap()
        XCTAssertTrue(app.staticTexts["No valid temperature or humidity reading from this Growhub."].waitForExistence(timeout: 3))
        XCTAssertGreaterThanOrEqual(app.descendants(matching: .any).matching(NSPredicate(format: "label CONTAINS[c] %@ OR value CONTAINS[c] %@", "Unavailable", "Unavailable")).count, 3)
        XCTAssertFalse(app.staticTexts["0% humidity"].exists)
        XCTAssertFalse(app.staticTexts["0.0°C"].exists)
    }
    func testLightLevelAppearsOnMainControls() {
        let app = XCUIApplication()
        app.launchArguments = ["--demo"]
        app.launch()
        app.buttons.matching(NSPredicate(format: "label CONTAINS %@", "Greenhouse")).firstMatch.tap()
        XCTAssertTrue(app.descendants(matching: .any).matching(NSPredicate(format: "label CONTAINS %@ OR value CONTAINS %@", "180 / 255", "180 / 255")).firstMatch.waitForExistence(timeout: 3))
    }
    func testSingleRememberedGrowhubOpensDirectlyAndBackStillWorks() {
        let app = XCUIApplication()
        app.launchArguments = ["--demo", "--single-remembered-demo"]
        app.launch()
        XCTAssertTrue(app.staticTexts["growhub-name"].waitForExistence(timeout: 8))
        XCTAssertTrue(app.buttons["rename-outlet-1"].exists)
        app.navigationBars.buttons.element(boundBy: 0).tap()
        XCTAssertTrue(app.navigationBars["Growhub Remote"].exists)
        XCTAssertFalse(app.buttons["rename-outlet-1"].exists)
        app.terminate()
        app.launch()
        XCTAssertTrue(app.staticTexts["growhub-name"].waitForExistence(timeout: 8))
        XCTAssertTrue(app.buttons["rename-outlet-1"].exists)
    }
    func testEquipmentLabelAndOutletNameAreSharedInBothDirections() {
        let app = XCUIApplication()
        app.launchArguments = ["--demo"]
        app.launch()
        app.buttons.matching(NSPredicate(format: "label CONTAINS %@", "Greenhouse")).firstMatch.tap()
        app.buttons["rename-outlet-1"].tap()
        XCTAssertTrue(app.alerts["Outlet Name"].waitForExistence(timeout: 3))
        app.alerts.textFields.firstMatch.tap()
        app.alerts.textFields.firstMatch.typeText("Grow lights")
        app.alerts.buttons["Save"].tap()
        XCTAssertTrue(app.buttons["Rename Grow lights"].waitForExistence(timeout: 3))
        app.swipeUp()
        app.buttons["Full controls"].tap()
        app.buttons["Outlet assignments"].tap()
        let label = app.textFields["Equipment label"].firstMatch
        XCTAssertTrue(label.waitForExistence(timeout: 3))
        XCTAssertEqual(label.value as? String, "Grow lights")
        label.coordinate(withNormalizedOffset: CGVector(dx: 0.98, dy: 0.5)).tap()
        label.typeText(String(repeating: XCUIKeyboardKey.delete.rawValue, count: "Grow lights".count) + "Seedling lights")
        app.navigationBars.buttons["Save"].tap()
        app.buttons["Save outlet assignments"].tap()
        XCTAssertTrue(app.buttons["Schedules"].waitForExistence(timeout: 3))
        app.buttons["Schedules"].tap()
        XCTAssertTrue(app.staticTexts["Seedling lights · Light"].waitForExistence(timeout: 3))
        app.navigationBars["Schedules"].buttons.element(boundBy: 0).tap()
        app.buttons["Done"].tap()
        app.swipeDown()
        XCTAssertTrue(app.buttons["Rename Seedling lights"].waitForExistence(timeout: 3))
        XCTAssertEqual(app.switches["outlet-1-power"].label, "Seedling lights power")
    }
    func testGrowhubAndOutletNamesKeepDeviceIdentity() {
        let app = XCUIApplication()
        app.launchArguments = ["--demo"]
        app.launch()
        XCTAssertTrue(app.staticTexts["Greenhouse"].waitForExistence(timeout: 8))
        app.buttons.matching(NSPredicate(format: "label CONTAINS %@", "Greenhouse")).firstMatch.tap()
        app.buttons["Rename Growhub"].tap()
        XCTAssertTrue(app.alerts["Growhub Name"].exists)
        app.alerts.textFields.firstMatch.tap()
        app.alerts.textFields.firstMatch.typeText("Kitchen garden")
        app.alerts.buttons["Save"].tap()
        XCTAssertEqual(app.staticTexts["growhub-name"].label, "Kitchen garden")
        XCTAssertTrue(app.staticTexts["GrowHub_0001"].exists)
        app.buttons["rename-outlet-1"].tap()
        app.alerts.textFields.firstMatch.tap()
        app.alerts.textFields.firstMatch.typeText("Grow lights")
        app.alerts.buttons["Save"].tap()
        XCTAssertTrue(app.buttons["Rename Grow lights"].exists)
        XCTAssertEqual(app.switches["outlet-1-power"].label, "Grow lights power")
        app.navigationBars.buttons.element(boundBy: 0).tap()
        XCTAssertTrue(app.staticTexts["GrowHub_0001"].exists)
        XCTAssertTrue(app.staticTexts["Kitchen garden"].exists)
        app.buttons.matching(NSPredicate(format: "label CONTAINS %@", "Indoor garden")).firstMatch.tap()
        XCTAssertTrue(app.buttons["Rename Outlet 1"].exists)
        app.navigationBars.buttons.element(boundBy: 0).tap()
        app.buttons.matching(NSPredicate(format: "label CONTAINS %@", "Kitchen garden")).firstMatch.tap()
        XCTAssertTrue(app.buttons["Rename Grow lights"].exists)
        capture("03-named-outlets")
    }
    func testDemoNavigationAndControls() {
        let app = XCUIApplication()
        app.launchArguments = ["--demo"]
        app.launch()
        XCTAssertTrue(app.staticTexts["Greenhouse"].waitForExistence(timeout: 8))
        XCTAssertTrue(app.staticTexts["Indoor garden"].exists)
        capture("01-controllers")
        app.buttons.matching(NSPredicate(format: "label CONTAINS %@", "Greenhouse")).firstMatch.tap()
        XCTAssertTrue(app.buttons["Resume schedules"].waitForExistence(timeout: 3))
        let power = app.switches["Outlet 1 power"]
        XCTAssertTrue(power.exists)
        power.tap()
        XCTAssertEqual(power.value as? String, "1")
        capture("02-outlets")
        app.buttons["Resume schedules"].tap()
        XCTAssertTrue(app.buttons["Use manual control"].waitForExistence(timeout: 3))
        app.buttons["Use manual control"].tap()
        app.buttons["Switch all to Manual"].tap()
        XCTAssertTrue(app.buttons["Resume schedules"].waitForExistence(timeout: 3))
        app.swipeUp()
        app.buttons["Full controls"].tap()
        XCTAssertTrue(app.buttons["Schedules"].waitForExistence(timeout: 5))
        XCTAssertEqual(app.webViews.count, 0)
        app.buttons["Done"].tap()
        app.navigationBars.buttons.element(boundBy: 0).tap()
        app.buttons["Help and demo"].tap()
        let privacy = app.buttons["Privacy policy"]
        for _ in 0..<5 {
            if privacy.exists && privacy.isHittable { break }
            app.swipeUp()
        }
        XCTAssertTrue(privacy.waitForExistence(timeout: 3))
        privacy.tap()
        XCTAssertTrue(app.staticTexts["Growhub Remote Privacy Policy"].waitForExistence(timeout: 3))
    }
    func testNativeControllerPagesAndScheduleSave() {
        let app = XCUIApplication()
        app.launchArguments = ["--demo"]
        app.launch()
        XCTAssertTrue(app.staticTexts["Greenhouse"].waitForExistence(timeout: 8))
        app.buttons.matching(NSPredicate(format: "label CONTAINS %@", "Greenhouse")).firstMatch.tap()
        app.swipeUp()
        app.buttons["Full controls"].tap()
        XCTAssertTrue(app.buttons["Schedules"].waitForExistence(timeout: 5))
        app.buttons["Schedules"].tap()
        XCTAssertTrue(app.switches["Always on"].firstMatch.waitForExistence(timeout: 3))
        app.switches["Always on"].firstMatch.coordinate(withNormalizedOffset: CGVector(dx: 0.9, dy: 0.5)).tap()
        XCTAssertEqual(app.switches["Always on"].firstMatch.value as? String, "1")
        capture("05-edited-schedule")
        app.navigationBars.buttons["Save"].tap()
        app.buttons["Save schedules"].tap()
        capture("06-after-schedule-save")
        XCTAssertTrue(app.buttons["Schedules"].waitForExistence(timeout: 3))
        app.buttons["Schedules"].tap()
        XCTAssertEqual(app.switches["Always on"].firstMatch.value as? String, "1")
        app.navigationBars["Schedules"].buttons.element(boundBy: 0).tap()
        for (link, title) in [("Outlet assignments", "Outlet assignments"), ("Device and time", "Device and time"), ("Wi-Fi", "Wi-Fi"), ("Command Center", "Command Center"), ("Firmware updates", "Firmware updates"), ("Diagnostics and actions", "Diagnostics")] {
            let button = app.buttons[link]
            if !button.isHittable { app.swipeUp() }
            button.tap()
            XCTAssertTrue(app.navigationBars[title].waitForExistence(timeout: 3))
            XCTAssertEqual(app.webViews.count, 0)
            if title == "Diagnostics" { app.swipeUp(); XCTAssertTrue(app.staticTexts.containing(NSPredicate(format: "label CONTAINS %@", "Unavailable")).firstMatch.exists) }
            app.navigationBars[title].buttons.element(boundBy: 0).tap()
        }
        capture("04-native-settings")
    }
    private func capture(_ name: String) {
        let attachment = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }
}

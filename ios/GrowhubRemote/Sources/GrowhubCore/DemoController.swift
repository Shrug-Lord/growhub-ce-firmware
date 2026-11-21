import Foundation

/// A completely local demonstration. Never passed to the network client.
public struct DemoController {
    public static func status(index: Int, manual: Bool = true, states: [Bool] = [false, true, false, false], sensorAvailable: Bool? = nil) -> HubStatus {
        var object: [String: Any] = [
            "mac": index == 0 ? "000000000001" : "000000000002", "fw": "1.2.0C",
            "name": index == 0 ? "Greenhouse" : "Indoor garden", "mode": manual ? "manual" : "auto",
            "temp": index == 0 ? 23.4 : 21.8, "rh": index == 0 ? 58 : 52, "unit": "C", "light": index == 0 ? 180 : 92,
            "outlet_status": (1...4).map { id -> [String: Any] in
                ["id": id, "state": states[id - 1] ? "on" : "off", "summary": manual ? "" : (id == 2 ? "Light schedule · 6 AM–10 PM" : "")]
            }, "time_warning": "", "sensor_warning": ""
        ]
        if let available = sensorAvailable {
            object["temp_valid"] = available
            if !available { object["temp"] = 0; object["rh"] = 0 }
        }
        return try! JSONDecoder().decode(HubStatus.self, from: JSONSerialization.data(withJSONObject: object))
    }
    public static func changing(_ status: HubStatus, manual: Bool? = nil, outlet: Int? = nil, on: Bool? = nil) -> HubStatus {
        var states = status.outlet_status.sorted { $0.id < $1.id }.map(\.isOn)
        if let outlet, let on, (1...4).contains(outlet) { states[outlet - 1] = on }
        return self.status(index: status.id.hasSuffix("1") ? 0 : 1, manual: manual ?? (status.mode == "manual"), states: states, sensorAvailable: status.temp_valid)
    }
}

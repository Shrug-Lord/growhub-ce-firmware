import SwiftUI
import GrowhubCore

struct CompactHubHeader: View {
    let hub: RememberedHub
    let status: HubStatus?
    let error: String?
    let firmware: String?
    let rename: () -> Void
    @State private var showDetails = false

    private var connection: String { status == nil ? "Not connected" : error == nil ? "Connected" : "Request error" }
    private var connectionColor: Color { status == nil ? .red : error == nil ? .green : .orange }

    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            HStack(spacing: 8) {
                HStack(alignment: .firstTextBaseline, spacing: 6) {
                    Text(hub.title).font(.title.bold()).foregroundStyle(Color.primary)
                        .accessibilityAddTraits(.isHeader)
                        .accessibilityIdentifier("growhub-name")
                    Button(action: rename) {
                        Image(systemName: "pencil").font(.caption).foregroundStyle(.secondary)
                            .padding(.vertical, 12).padding(.horizontal, 3).contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel("Rename Growhub")
                }.frame(maxWidth: .infinity, alignment: .leading)
                HStack(spacing: 2) {
                    Image(systemName: status == nil ? "wifi.slash" : "wifi")
                        .font(.title3.weight(.semibold)).foregroundStyle(connectionColor)
                        .accessibilityLabel(connection)
                        .accessibilityIdentifier("growhub-connection")
                    Button { showDetails = true } label: {
                        Image(systemName: "info.circle").font(.title3).foregroundStyle(.secondary)
                            .frame(width: 44, height: 44).contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel("Device details")
                    .popover(isPresented: $showDetails) {
                        deviceDetails
                            .presentationCompactAdaptation(.popover)
                    }
                }.fixedSize()
            }
            Text(hub.deviceID).font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
        }
        .textCase(nil)
        .padding(.bottom, 6)
    }

    private var deviceDetails: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Text("Device details").font(.headline)
                Spacer()
                Button { showDetails = false } label: {
                    Image(systemName: "xmark").font(.caption).foregroundStyle(.secondary)
                        .frame(width: 32, height: 32).contentShape(Rectangle())
                }.buttonStyle(.plain).accessibilityLabel("Close device details")
            }
            LabeledContent("IP address", value: (try? HubAPI.baseURL(hub.host).host) ?? hub.host)
                .textSelection(.enabled)
                .accessibilityIdentifier("device-ip-address")
            LabeledContent("Device ID", value: hub.deviceID).textSelection(.enabled)
            LabeledContent("Firmware", value: (status?.fw ?? firmware).map { "CE \($0)" } ?? "Unavailable")
            LabeledContent("Connection", value: connection)
            LabeledContent("Signal", value: status?.rssi.map { "\($0) dBm" } ?? "Unavailable")
            if let error { Text(error).font(.footnote).foregroundStyle(.orange) }
        }
        .font(.subheadline)
        .padding(16)
        .frame(idealWidth: 300, maxWidth: 320)
        .fixedSize(horizontal: false, vertical: true)
    }
}

struct CompactHubReadings: View {
    let status: HubStatus?
    @Environment(\.dynamicTypeSize) private var typeSize

    var body: some View {
        let layout = typeSize.isAccessibilitySize
            ? AnyLayout(VStackLayout(spacing: 12))
            : AnyLayout(HStackLayout(alignment: .top, spacing: 8))
        layout {
            reading("Temperature", value: temperature)
            reading("Humidity", value: humidity)
            reading("Light", value: status?.lightLevel.map { String($0) }, suffix: " / 255")
        }
        .padding(.vertical, 4)
        .accessibilityIdentifier("compact-sensor-card")
    }

    private var temperature: String? {
        guard let status, status.hasTemperatureHumidityReading else { return nil }
        return "\(status.temp.formatted(.number.precision(.fractionLength(1))))°\(status.unit)"
    }
    private var humidity: String? {
        guard let status, status.hasTemperatureHumidityReading else { return nil }
        return "\(status.rh.formatted(.number.precision(.fractionLength(0))))%"
    }
    private func reading(_ title: String, value: String?, suffix: String = "") -> some View {
        VStack(spacing: 6) {
            Text(title).font(.caption2).foregroundStyle(.secondary)
            if let value {
                (Text(value).font(.title2.weight(.semibold)) + Text(suffix).font(.caption).foregroundStyle(.secondary))
                    .monospacedDigit().lineLimit(1).minimumScaleFactor(0.8)
            } else { Text("Unavailable").font(.caption).foregroundStyle(.secondary) }
        }.frame(maxWidth: .infinity)
    }
}

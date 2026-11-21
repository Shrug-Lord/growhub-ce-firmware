import SwiftUI

struct AboutView: View {
    @EnvironmentObject private var store: HubStore
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        NavigationStack {
            List {
                Section {
                    Label("Growhub Remote", systemImage: "leaf.fill").font(.title2.bold())
                    Text("Free local control for Growhub Community Edition firmware.")
                    Text("Version \(Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "1.0.0") (\(Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "")) · Plongee6 LLC").font(.footnote).foregroundStyle(.secondary)
                }
                Section("Try without hardware") {
                    Button(store.demoMode ? "Exit demo" : "Explore demo") { store.setDemo(!store.demoMode); dismiss() }
                    Text("Demo controllers are simulated. No network requests or real outlet changes are made in demo mode.").font(.footnote)
                }
                Section("Connect your controller") {
                    Text("Use a Growhub or Growhub+ already running CE firmware. Connect your phone to the same Wi-Fi, allow Local Network access, then refresh the device list. If discovery is blocked, use + to add its address.")
                    Button("Open iOS Settings") { if let url = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(url) } }
                    Text("Stock NIWA cloud firmware is not supported. This app does not perform the first stock-to-CE firmware installation.")
                }
                Section("Control modes") {
                    Text("Outlet names are the equipment labels saved on the Growhub, shared with Outlet assignments and its web controls. A single remembered Growhub opens directly when you start the app; use Back to reach the list.")
                    Text("Released CE firmware may share one Auto/Manual mode across all outlets. Switching to Manual preserves outputs and pauses schedules. Resume schedules to restore Auto. Independent outlet modes appear only if supported by the installed firmware.")
                    Text("Full controls provides native schedules, assignments, device settings, Wi-Fi, Command Center, firmware updates and diagnostics. Recheck equipment after any changes; the app is not a safety monitoring service.")
                }
                Section("Privacy and support") {
                    NavigationLink("Privacy policy") { PrivacyView() }
                    NavigationLink("Open-source acknowledgements") { ScrollView { Text(Bundle.main.url(forResource: "SwiftSoup-LICENSE", withExtension: "txt").flatMap { try? String(contentsOf: $0) } ?? "SwiftSoup · MIT License").font(.footnote).padding() }.navigationTitle("Acknowledgements") }
                    Link("Email support", destination: URL(string: "mailto:support@deep6gear.com?subject=Growhub%20Remote")!)
                    Text("No ads, analytics, account, subscription or developer cloud service. Saved controllers stay on this device. Swipe left on a controller to forget it.").font(.footnote)
                }
                Section {
                    Text("Independent companion app. Not affiliated with or endorsed by NIWA or the CE firmware maintainers. The CE firmware is a separate open-source project.").font(.footnote).foregroundStyle(.secondary)
                }
            }.navigationTitle("Help and demo").toolbar { Button("Done") { dismiss() } }
        }
    }
}
struct PrivacyView: View {
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                Text("Growhub Remote Privacy Policy").font(.title.bold())
                Text("Effective November 21, 2026 · Plongee6 LLC")
                Link("View published policy", destination: URL(string: "https://github.com/LandonL82/growhub-ce-firmware/blob/codex/growhub-remote-support/docs/growhub-remote/privacy.md")!)
                Text("Growhub Remote does not collect data for its developer. It has no accounts, advertising, analytics, tracking SDKs, or developer-operated cloud service.")
                Text("Local operation").font(.headline)
                Text("The app uses Local Network permission to discover controllers and send status and control requests directly to the devices you select. Controller hardware IDs, network addresses and names are saved on your device to remember them. They are not uploaded to us. Native status readings are held in memory. Controller data remains subject to the firmware you installed.")
                Text("Full controls").font(.headline)
                Text("Native settings communicate directly with your controller. For older CE firmware, the app reads its existing settings page as data without displaying it or running its scripts. Actions you initiate, such as firmware updates, may contact the firmware sources you choose. Links opened outside the app and support email are handled by those services under their own policies.")
                Text("Your choices").font(.headline)
                Text("You can deny Local Network permission, use demo mode without hardware, or swipe left on a saved controller to forget its identity and address. Earlier app builds used an embedded controller page; deleting the app also removes any website data those builds stored. Data on the controller itself is not removed when you forget it. Device backups are governed by your Apple backup settings. Support email includes only what you choose to send and is used to respond to your request.")
                Text("Contact").font(.headline)
                Link("support@deep6gear.com", destination: URL(string: "mailto:support@deep6gear.com")!)
            }.padding()
        }.navigationTitle("Privacy")
    }
}

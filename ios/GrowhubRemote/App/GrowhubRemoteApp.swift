import SwiftUI

@main struct GrowhubRemoteApp: App {
    @StateObject private var store = HubStore()
    var body: some Scene {
        WindowGroup { HubList().environmentObject(store).tint(.green) }
    }
}

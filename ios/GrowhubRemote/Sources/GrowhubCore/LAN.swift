import Foundation
import Darwin

public enum LAN {
    // Probe the directly attached Wi-Fi subnet. Large routed LANs need Bonjour
    // or an explicit address instead of an unbounded subnet sweep.
    public static func candidates() -> (hosts: [String], limited: Bool) {
        var head: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&head) == 0 else { return ([], false) }
        defer { freeifaddrs(head) }
        var node = head
        while let n = node {
            defer { node = n.pointee.ifa_next }
            guard String(cString: n.pointee.ifa_name) == "en0",
                  let addr = n.pointee.ifa_addr, Int32(addr.pointee.sa_family) == AF_INET,
                  let maskAddr = n.pointee.ifa_netmask else { continue }
            let ip = addr.withMemoryRebound(to: sockaddr_in.self, capacity: 1) { UInt32(bigEndian: $0.pointee.sin_addr.s_addr) }
            var mask = maskAddr.withMemoryRebound(to: sockaddr_in.self, capacity: 1) { UInt32(bigEndian: $0.pointee.sin_addr.s_addr) }
            let limited = (~mask) > 4095
            if limited { mask = 0xFFFFFF00 }
            let network = ip & mask, broadcast = network | ~mask
            guard broadcast > network + 1 else { return ([], limited) }
            return (((network + 1)..<broadcast).filter { $0 != ip }.map { v in
                "\((v >> 24) & 255).\((v >> 16) & 255).\((v >> 8) & 255).\(v & 255)"
            }, limited)
        }
        return ([], false)
    }
}

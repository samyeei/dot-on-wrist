import AppKit
import ApplicationServices

struct ChromeCall {
    let url: URL

    init?(configuration: URL) {
        guard let data = try? Data(contentsOf: configuration), data.count < 1024,
              let address = String(data: data, encoding: .utf8)?
                .trimmingCharacters(in: .whitespacesAndNewlines),
              let url = URL(string: address), url.scheme == "https",
              url.host == "chatgpt.com", url.port == nil, url.user == nil,
              url.password == nil, url.query == nil, url.fragment == nil,
              url.path.range(of: #"^/dots/[a-zA-Z0-9~-]+$"#, options: .regularExpression) != nil else {
            return nil
        }
        self.url = url
    }

    func page() -> AXUIElement? {
        guard let process = NSRunningApplication.runningApplications(withBundleIdentifier: "com.google.Chrome")
            .first(where: { $0.bundleURL?.path == "/Applications/Google Chrome.app" }) else { return nil }
        let root = AXUIElementCreateApplication(process.processIdentifier)
        AXUIElementSetMessagingTimeout(root, 0.2)
        _ = AXUIElementSetAttributeValue(root, "AXEnhancedUserInterface" as CFString, kCFBooleanTrue)
        guard let windows = read(root, kAXWindowsAttribute) as? [AXUIElement] else { return nil }
        let pages = windows.prefix(12).compactMap { window -> AXUIElement? in
            let area = descendants(window, limit: 240).first {
                (read($0, kAXRoleAttribute) as? String) == "AXWebArea"
            }
            guard let area else { return nil }
            let address = read(area, kAXURLAttribute)
            let pageURL = (address as? URL) ?? (address as? String).flatMap(URL.init(string:))
            return pageURL == url ? area : nil
        }
        return pages.count == 1 ? pages[0] : nil
    }

    func button(_ element: AXUIElement, named names: Set<String>) -> Bool {
        let role = read(element, kAXRoleAttribute) as? String
        guard ["AXButton", "AXToggleButton", "AXCheckBox"].contains(role ?? ""),
              (read(element, kAXEnabledAttribute) as? Bool) != false else { return false }
        return named(element, names) && isPressable(element)
    }

    func named(_ element: AXUIElement, _ names: Set<String>) -> Bool {
        return [kAXTitleAttribute, kAXDescriptionAttribute, kAXHelpAttribute].contains {
            guard let label = read(element, $0) as? String else { return false }
            return names.contains(label.lowercased())
        }
    }

    func perform(_ action: String, expires: TimeInterval, request: URL, allowAlreadyEnded: Bool) -> Bool {
        guard let area = page() else { return false }
        let nodes = descendants(area, limit: 1200)
        let hangups = nodes.filter { button($0, named: ["挂断", "hang up", "end call"]) }
        let dialButtons = nodes.filter {
            button($0, named: ["发起通话", "start call", "start a call"])
        }
        let candidate: AXUIElement?
        if action == "dial" {
            // The same ANNA page has one start button in the header and one in its profile card.
            guard hangups.isEmpty, (1...2).contains(dialButtons.count) else { return false }
            candidate = dialButtons.first
        } else {
            // A remote hangup can restore the start buttons before the watch sends OFF.
            if hangups.isEmpty { return allowAlreadyEnded && !dialButtons.isEmpty }
            guard hangups.count == 1 else { return false }
            candidate = hangups[0]
        }
        guard let candidate, Date().timeIntervalSince1970 < expires,
              FileManager.default.fileExists(atPath: request.path) else { return false }
        return AXUIElementPerformAction(candidate, kAXPressAction as CFString) == .success
    }
}

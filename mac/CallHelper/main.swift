import AppKit
import ApplicationServices

private let targetBundle = "com.openai.codex"
private let targetPath = "/Applications/ChatGPT.app"
private let attributes = [kAXIdentifierAttribute, kAXTitleAttribute, kAXDescriptionAttribute,
                          kAXHelpAttribute, "AXDOMIdentifier"]
private let app = NSApplication.shared

struct Button: Codable, Equatable {
    let role: String
    let labels: [String]
    let annaAncestor: Int
}

struct Request: Decodable {
    let action: String
    let lease: UUID
    let expires: TimeInterval
    let text: String?
}

struct CalibrationError: Error {
    let reason: String
    let detail: String

    init(_ reason: String, _ detail: String = "") {
        self.reason = reason
        self.detail = detail
    }
}

func read(_ element: AXUIElement, _ key: String) -> AnyObject? {
    var value: CFTypeRef?
    guard AXUIElementCopyAttributeValue(element, key as CFString, &value) == .success else { return nil }
    return value
}

func parent(_ element: AXUIElement) -> AXUIElement? {
    guard let value = read(element, kAXParentAttribute),
          CFGetTypeID(value) == AXUIElementGetTypeID() else { return nil }
    return (value as! AXUIElement)
}

func descendants(_ root: AXUIElement, limit: Int) -> [AXUIElement] {
    var nodes = [root]
    var index = 0
    while index < nodes.count && nodes.count < limit {
        if let children = read(nodes[index], kAXChildrenAttribute) as? [AXUIElement] {
            nodes.append(contentsOf: children.prefix(limit - nodes.count))
        }
        index += 1
    }
    return nodes
}

func namesAnna(_ element: AXUIElement) -> Bool {
    [kAXTitleAttribute, kAXDescriptionAttribute, kAXValueAttribute, kAXHelpAttribute].contains {
        guard let label = read(element, $0) as? String, label.count <= 140 else { return false }
        return label.range(of: #"(?i)(?<![a-z0-9])ANNA(?![a-z0-9])"#,
                           options: .regularExpression) != nil
    }
}

func isPressable(_ node: AXUIElement) -> Bool {
    var actions: CFArray?
    return AXUIElementCopyActionNames(node, &actions) == .success &&
        (actions as? [String])?.contains(kAXPressAction) == true
}

func bounds(_ node: AXUIElement) -> CGRect? {
    guard let position = read(node, kAXPositionAttribute),
          let size = read(node, kAXSizeAttribute),
          CFGetTypeID(position) == AXValueGetTypeID(),
          CFGetTypeID(size) == AXValueGetTypeID() else { return nil }
    var origin = CGPoint.zero
    var extent = CGSize.zero
    guard AXValueGetValue(position as! AXValue, .cgPoint, &origin),
          AXValueGetValue(size as! AXValue, .cgSize, &extent),
          extent.width > 0, extent.height > 0 else { return nil }
    return CGRect(origin: origin, size: extent)
}

func target() -> (NSRunningApplication, AXUIElement)? {
    guard let process = NSRunningApplication.runningApplications(withBundleIdentifier: targetBundle)
        .first(where: { $0.bundleURL?.path == targetPath }) else { return nil }
    let element = AXUIElementCreateApplication(process.processIdentifier)
    AXUIElementSetMessagingTimeout(element, 0.2)
    return (process, element)
}

func matchesAnna(_ button: AXUIElement, distance: Int) -> Bool {
    var scope = button
    for _ in 0..<distance {
        guard let next = parent(scope) else { return false }
        scope = next
    }
    if namesAnna(scope) { return true }
    let role = read(scope, kAXRoleAttribute) as? String
    if role == kAXWindowRole || role == kAXApplicationRole || role == "AXWebArea" { return false }
    return descendants(scope, limit: 110).contains { node in
        let role = read(node, kAXRoleAttribute) as? String
        return [kAXStaticTextRole, "AXHeading"].contains(role ?? "") && namesAnna(node)
    }
}

final class Helper: NSObject, NSApplicationDelegate {
    private let directory: URL
    private let chrome: ChromeCall?
    private let status = NSTextField(wrappingLabelWithString: "")
    private var window: NSWindow!
    private var timer: Timer?
    private var selecting = false
    private var lease: UUID?
    private var dialedAt = Date.distantPast
    private var chat: (name: String, turn: ChromeText)?
    private var dial: Button?
    private var hangup: Button?
    private var processed = Set<String>()
    private var accessibilityPID: pid_t?

    override init() {
        let support = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support", isDirectory: true)
        let chromeDirectory = support.appendingPathComponent("ANNA Chrome Call Helper", isDirectory: true)
        let chrome = ChromeCall(configuration: chromeDirectory.appendingPathComponent("target"))
        self.chrome = chrome
        self.directory = chrome == nil ? support.appendingPathComponent("ANNA Call Helper", isDirectory: true)
                                       : chromeDirectory
        super.init()
    }

    func applicationDidFinishLaunching(_ note: Notification) {
        do {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true,
                                                    attributes: [.posixPermissions: 0o700])
        } catch { NSApp.presentError(error); NSApp.terminate(nil); return }
        let defaults = UserDefaults.standard
        dial = defaults.data(forKey: "dial").flatMap { try? JSONDecoder().decode(Button.self, from: $0) }
        hangup = defaults.data(forKey: "hangup").flatMap { try? JSONDecoder().decode(Button.self, from: $0) }
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 510, height: 270),
                          styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "ANNA 通话助手"
        let intro = NSTextField(wrappingLabelWithString:
            chrome == nil
            ? "允许辅助功能后，打开 ChatGPT 的 ANNA 对话。点校准，再切回 ChatGPT，把鼠标停在对应按钮上，等 8 秒。校准挂断键时请先手动接通 ANNA。"
            : "允许辅助功能后，在 Chrome 保留已登录的 ANNA 对话页。蓝键拨号，屏幕红键挂断。")
        intro.preferredMaxLayoutWidth = 454
        status.preferredMaxLayoutWidth = 454
        var views: [NSView] = [
            intro,
            NSButton(title: "申请辅助功能权限", target: self, action: #selector(authorize)),
        ]
        if chrome == nil {
            views.append(NSButton(title: "8 秒后校准拨号键", target: self, action: #selector(learnDial)))
            views.append(NSButton(title: "8 秒后校准挂断键", target: self, action: #selector(learnHangup)))
        }
        views += [
            NSButton(title: "已手动挂断，清除助手通话状态", target: self, action: #selector(clearLease)),
            status,
        ]
        let stack = NSStackView(views: views)
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 13
        stack.translatesAutoresizingMaskIntoConstraints = false
        window.contentView!.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: window.contentView!.leadingAnchor, constant: 24),
            stack.trailingAnchor.constraint(equalTo: window.contentView!.trailingAnchor, constant: -24),
            stack.topAnchor.constraint(equalTo: window.contentView!.topAnchor, constant: 20),
        ])
        updateStatus()
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        timer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in self?.poll() }
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows: Bool) -> Bool {
        window.makeKeyAndOrderFront(nil)
        return true
    }

    @objc func authorize() {
        let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary
        _ = AXIsProcessTrustedWithOptions(options)
        updateStatus()
    }

    @objc func learnDial() { learn("dial") }
    @objc func learnHangup() { learn("hangup") }
    @objc func clearLease() {
        lease = nil
        updateStatus()
    }

    private func updateStatus() {
        let permission = AXIsProcessTrusted() ? "已开启" : "待开启"
        if chrome != nil {
            status.stringValue = "辅助功能 \(permission)   ·   Chrome 模式"
            return
        }
        let dialing = dial == nil ? "待校准" : "已校准"
        let ending = hangup == nil ? "待校准" : "已校准"
        status.stringValue = "权限 \(permission)   ·   拨号 \(dialing)   ·   挂断 \(ending)"
    }

    private func learn(_ kind: String) {
        guard !selecting, lease == nil, AXIsProcessTrusted() else { updateStatus(); return }
        if let (process, root) = target() {
            enableApplicationAccessibility(process, root: root)
        }
        selecting = true
        status.stringValue = "请切到 ChatGPT，把鼠标移到要校准的按钮上。"
        DispatchQueue.main.asyncAfter(deadline: .now() + 8) { [weak self] in
            guard let self else { return }
            self.selecting = false
            do {
                let signature = try self.captureButton()
                guard signature.labels != (kind == "dial" ? self.hangup : self.dial)?.labels else {
                    throw CalibrationError("dial_and_hangup_match", "两个操作读到了同一个按钮。请接通 ANNA 后再校准挂断键。")
                }
                if kind == "dial" { self.dial = signature } else { self.hangup = signature }
                UserDefaults.standard.set(try JSONEncoder().encode(signature), forKey: kind)
                self.write(["button": kind, "result": "calibrated"], "calibration.json")
                self.updateStatus()
            } catch let failure as CalibrationError {
                self.write(["button": kind, "result": failure.reason, "detail": failure.detail], "calibration.json")
                self.status.stringValue = failure.detail
            } catch {
                self.status.stringValue = "无法保存校准结果。"
            }
        }
    }

    private func enableApplicationAccessibility(_ process: NSRunningApplication, root: AXUIElement) {
        guard accessibilityPID != process.processIdentifier else { return }
        // Electron creates its renderer accessibility tree on demand.
        let result = AXUIElementSetAttributeValue(root, "AXManualAccessibility" as CFString, kCFBooleanTrue)
        if result == .success { accessibilityPID = process.processIdentifier }
        write(["result": result.rawValue], "app-accessibility.json")
    }

    private func captureButton() throws -> Button {
        guard let (process, _) = target() else {
            throw CalibrationError("app_missing", "没有找到 /Applications 里的 ChatGPT 原生 App。")
        }
        guard NSWorkspace.shared.frontmostApplication?.processIdentifier == process.processIdentifier,
              let point = CGEvent(source: nil)?.location else {
            throw CalibrationError("not_frontmost", "倒计时结束时 ChatGPT 不在前台。请切到 ChatGPT 后再停住鼠标。")
        }
        var hit: AXUIElement?
        let hitResult = AXUIElementCopyElementAtPosition(AXUIElementCreateSystemWide(),
                                                        Float(point.x), Float(point.y), &hit)
        guard hitResult == .success, let node = hit else {
            throw CalibrationError("hit_test", "macOS 没有读到鼠标下的控件（\(hitResult.rawValue)）。")
        }
        var pid: pid_t = 0
        guard AXUIElementGetPid(node, &pid) == .success, pid == process.processIdentifier else {
            throw CalibrationError("pointer_outside_app", "鼠标下的控件不属于 ChatGPT。")
        }
        let hitRole = read(node, kAXRoleAttribute) as? String ?? "未知"
        if isPressable(node) { return try signature(for: node) }
        let buttons = descendants(node, limit: 220).dropFirst().filter { child in
            bounds(child)?.contains(point) == true && isPressable(child)
        }
        if buttons.count == 1 { return try signature(for: buttons[0]) }
        if buttons.count > 1 {
            throw CalibrationError("multiple_buttons", "鼠标位置有多个可点击控件。请把鼠标移到电话图标中心再试。")
        }
        var ancestor = node
        for _ in 0..<7 {
            guard let next = parent(ancestor) else { break }
            if isPressable(next) { return try signature(for: next) }
            ancestor = next
        }
        throw CalibrationError("no_press_action", "鼠标下读到的是 \(hitRole)，周围没有可通过辅助功能点击的按钮。")
    }

    private func signature(for node: AXUIElement) throws -> Button {
        let labels = attributes.map { read(node, $0) as? String ?? "" }
        guard let role = read(node, kAXRoleAttribute) as? String,
              labels.contains(where: { !$0.isEmpty }) else {
            throw CalibrationError("button_unlabelled", "读到了可点击控件，但 ChatGPT 没提供按钮名称或标识。")
        }
        guard let distance = (0...8).first(where: { matchesAnna(node, distance: $0) }) else {
            throw CalibrationError("anna_scope_missing", "读到了按钮，但附近没有可识别的 ANNA 名称。")
        }
        return Button(role: role, labels: labels, annaAncestor: distance)
    }

    private func press(_ signature: Button, requestPath: URL, expires: TimeInterval) -> Bool {
        guard let (process, root) = target() else { return false }
        enableApplicationAccessibility(process, root: root)
        guard
              let window = read(root, kAXFocusedWindowAttribute) ?? read(root, kAXMainWindowAttribute),
              CFGetTypeID(window) == AXUIElementGetTypeID() else { return false }
        let candidates = descendants(window as! AXUIElement, limit: 700).filter { element in
            (read(element, kAXRoleAttribute) as? String) == signature.role &&
            zip(signature.labels, attributes).allSatisfy { label, key in
                label.isEmpty || (read(element, key) as? String) == label
            } &&
            (read(element, kAXEnabledAttribute) as? Bool) != false &&
            isPressable(element) && matchesAnna(element, distance: signature.annaAncestor)
        }
        return candidates.count == 1 && Date().timeIntervalSince1970 < expires &&
            FileManager.default.fileExists(atPath: requestPath.path) &&
            AXUIElementPerformAction(candidates[0], kAXPressAction as CFString) == .success
    }

    private func write(_ value: [String: Any], _ name: String) {
        guard let data = try? JSONSerialization.data(withJSONObject: value) else { return }
        try? data.write(to: directory.appendingPathComponent(name), options: .atomic)
    }

    private func poll() {
        let ready = !selecting && AXIsProcessTrusted() && (chrome != nil || (dial != nil && hangup != nil))
        if ready { write(["at": Date().timeIntervalSince1970], "ready.json") }
        guard let entries = try? FileManager.default.contentsOfDirectory(
            at: directory, includingPropertiesForKeys: nil) else { return }
        let requests = entries.filter { $0.pathExtension == "request" }
        if let chat, !requests.contains(where: { $0.lastPathComponent == chat.name }) {
            self.chat = nil
        }
        processed.formIntersection(requests.map(\.lastPathComponent))
        guard ready else { return }
        for path in requests.sorted(by: { $0.lastPathComponent < $1.lastPathComponent }) {
            let key = path.deletingPathExtension().lastPathComponent
            guard UUID(uuidString: key) != nil, !processed.contains(path.lastPathComponent),
                  let data = try? Data(contentsOf: path), data.count < 4096,
                  let request = try? JSONDecoder().decode(Request.self, from: data),
                  request.expires > Date().timeIntervalSince1970 else { continue }
            func operate(_ button: Button?) -> Bool {
                if let chrome {
                    return chrome.perform(request.action, expires: request.expires, request: path,
                                          allowAlreadyEnded: Date().timeIntervalSince(dialedAt) > 2)
                }
                guard let button else { return false }
                return press(button, requestPath: path, expires: request.expires)
            }
            var message = chrome == nil ? "未找到唯一的 ANNA 按钮，请打开 ANNA 对话或重新校准。"
                                        : "请在 Chrome 打开你的 ANNA 对话，并切到该标签页。"
            var ok = false
            switch request.action {
            case "text":
                guard let chrome, lease == nil, let body = request.text,
                      !body.isEmpty, body.utf8.count <= 1800 else {
                    processed.insert(path.lastPathComponent)
                    write(["ok": false, "message": "请先结束通话再发送消息"], key + ".response")
                    continue
                }
                if chat == nil {
                    guard let turn = ChromeText(text: body, chrome: chrome) else {
                        processed.insert(path.lastPathComponent)
                        write(["ok": false, "message": "请打开 Chrome 的 ANNA 聊天并清空草稿"], key + ".response")
                        continue
                    }
                    chat = (path.lastPathComponent, turn)
                    continue
                }
                guard let chat, chat.name == path.lastPathComponent,
                      let update = chat.turn.step(chrome: chrome, expires: request.expires) else { continue }
                let reply: String
                switch update {
                case .progress(let progress):
                    write(["ok": true, "partial": true, "reply": String(progress.prefix(1800))], key + ".response")
                    continue
                case .complete(let complete):
                    reply = complete
                }
                self.chat = nil
                processed.insert(path.lastPathComponent)
                write(["ok": true, "reply": String(reply.prefix(1800))], key + ".response")
                status.stringValue = "已收到 ANNA 的文字回复。"
                continue
            case "dial":
                if lease == nil, chat == nil, operate(dial) {
                    lease = request.lease
                    dialedAt = Date()
                    ok = true
                } else if lease != nil {
                    message = "已有手表通话，请先结束原通话。"
                }
            case "hangup":
                if lease != request.lease {
                    ok = true
                } else if operate(hangup) {
                    lease = nil
                    ok = true
                } else {
                    continue // The call may still be dialing. Retry before this request expires.
                }
            default:
                message = "未知的通话命令。"
            }
            processed.insert(path.lastPathComponent)
            write(["ok": ok, "message": ok ? "" : message], key + ".response")
            status.stringValue = ok ? (request.action == "dial" ? "已请求拨给 ANNA。" : "已处理挂断请求。") : message
        }
    }
}

let helper = Helper()
app.setActivationPolicy(.regular)
app.delegate = helper
app.run()

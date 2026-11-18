import AppKit
import ApplicationServices

final class ChromeText {
    enum Update {
        case progress(String)
        case complete(String)
    }

    private struct Message {
        let text: String
        let outgoing: Bool
    }

    let text: String
    private let baseline: Int
    private var sent = false
    private var observed = ""
    private var changedAt = Date()
    private var published = ""

    private static func content(_ node: AXUIElement) -> String {
        (read(node, kAXValueAttribute) as? String ?? "")
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private static func messages(_ nodes: [AXUIElement], chrome: ChromeCall) -> [Message] {
        nodes.compactMap { actions in
            guard chrome.named(actions, ["消息操作", "message actions"]),
                  let row = parent(actions),
                  let children = read(row, kAXChildrenAttribute) as? [AXUIElement] else { return nil }
            let parts = children.filter { !CFEqual($0, actions) }.flatMap { child in
                descendants(child, limit: 350).compactMap { node -> String? in
                    guard (read(node, kAXRoleAttribute) as? String) == kAXStaticTextRole else { return nil }
                    let value = content(node)
                    return value.isEmpty ? (read(node, kAXTitleAttribute) as? String) : value
                }
            }
            let message = parts.joined(separator: "\n").trimmingCharacters(in: .whitespacesAndNewlines)
            let buttons = read(actions, kAXChildrenAttribute) as? [AXUIElement] ?? []
            let outgoing = buttons.first.map { chrome.named($0, ["回复", "reply"]) } ?? false
            return message.isEmpty ? nil : Message(text: message, outgoing: outgoing)
        }
    }

    private static func editor(_ nodes: [AXUIElement], chrome: ChromeCall) -> AXUIElement? {
        let editors = nodes.filter {
            let role = read($0, kAXRoleAttribute) as? String
            return (role == kAXTextAreaRole || role == kAXTextFieldRole) &&
                chrome.named($0, ["消息", "message"])
        }
        return editors.count == 1 ? editors[0] : nil
    }

    init?(text: String, chrome: ChromeCall) {
        guard let page = chrome.page() else { return nil }
        let nodes = descendants(page, limit: 1800)
        guard let input = Self.editor(nodes, chrome: chrome),
              ["", "发送消息", "Message", "Send a message"].contains(Self.content(input)),
              !nodes.contains(where: { chrome.button($0, named: ["挂断", "hang up", "end call"]) }) else { return nil }
        self.text = text
        baseline = Self.messages(nodes, chrome: chrome).filter { $0.outgoing && $0.text == text }.count
        _ = AXUIElementSetAttributeValue(input, kAXFocusedAttribute as CFString, kCFBooleanTrue)
        guard AXUIElementSetAttributeValue(input, kAXValueAttribute as CFString, text as CFString) == .success else {
            return nil
        }
    }

    private func isProgress(_ text: String) -> Bool {
        text.count <= 180 && text.range(
            of: #"稍等|稍候|等我|正在处理|正在查询|正在核对|我先(?:查|看|核|去)|处理完(?:后|再)|稍后(?:发|回|告)|give me a moment|one moment|working on it"#,
            options: [.regularExpression, .caseInsensitive]) != nil
    }

    func step(chrome: ChromeCall, expires: TimeInterval) -> Update? {
        guard let page = chrome.page() else { return nil }
        let nodes = descendants(page, limit: 1800)
        if !sent {
            guard let input = Self.editor(nodes, chrome: chrome), Self.content(input) == text else { return nil }
            let buttons = nodes.filter { chrome.button($0, named: ["发送", "send", "send message"]) }
            guard buttons.count == 1 else { return nil }
            sent = AXUIElementPerformAction(buttons[0], kAXPressAction as CFString) == .success
            return nil
        }
        let messages = Self.messages(nodes, chrome: chrome)
        let matching = messages.indices.filter { messages[$0].outgoing && messages[$0].text == text }
        guard matching.count > baseline, let anchor = matching.last else { return nil }
        let replies = messages.dropFirst(anchor + 1).prefix { !$0.outgoing }.map(\.text)
        guard let last = replies.last else { return nil }
        let reply = replies.joined(separator: "\n\n")
        if reply != observed {
            observed = reply
            changedAt = Date()
        }
        let now = Date()
        if expires - now.timeIntervalSince1970 < 2 { return .complete(reply) }
        if isProgress(last) {
            if reply != published && now.timeIntervalSince(changedAt) >= 1.25 {
                published = reply
                return .progress(reply)
            }
            return nil
        }
        return now.timeIntervalSince(changedAt) >= 2 ? .complete(reply) : nil
    }
}

import AVFoundation
import Foundation
import SwiftUI
import WatchKit

@MainActor final class Conversation: ObservableObject {
    enum Phase: Equatable { case idle, listening, waiting, speaking }

    @Published private(set) var phase: Phase = .idle
    @Published private(set) var connected = false
    @Published private(set) var calling = false
    @Published private(set) var status = ""
    @Published private(set) var reply = ""
    @Published private(set) var notice: Gateway.Notice?

    private lazy var announcement = AVSpeechSynthesizer()
    private let receiptKey = "anna.notice.pendingReceipt"

    private let gateway = Pairing.load().map(Gateway.init)
    private lazy var audio = VoiceAudio()
    private var operation: Task<Void, Never>?
    private var pressed = false
    private var generation = UUID()
    private var noticesSince: Date?

    func monitor() async {
        if noticesSince == nil { noticesSince = Date() }
        while !Task.isCancelled {
            if operation == nil {
                connected = await gateway?.health() ?? false
                if connected, operation == nil, !Task.isCancelled {
                    await checkNotices()
                }
            }
            try? await Task.sleep(for: .seconds(3))
        }
    }

    private func checkNotices() async {
        guard let gateway else { return }
        do {
            if let saved = UserDefaults.standard.string(forKey: receiptKey),
               let receipt = Int64(saved) {
                try await gateway.acknowledge(receipt)
                UserDefaults.standard.removeObject(forKey: receiptKey)
            }
            guard notice == nil, operation == nil, let since = noticesSince,
                  let incoming = try await gateway.notice(since: since),
                  !Task.isCancelled, noticesSince == since,
                  notice == nil, operation == nil else { return }
            notice = incoming
            UserDefaults.standard.set(String(incoming.id), forKey: receiptKey)
            WKInterfaceDevice.current().play(.notification)
            let utterance = AVSpeechUtterance(string: incoming.text)
            let chinese = incoming.text.range(of: #"[一-鿿]"#, options: .regularExpression) != nil
            utterance.voice = AVSpeechSynthesisVoice(language: chinese ? "zh-CN" : "en-US")
            announcement.speak(utterance)
            try await gateway.acknowledge(incoming.id)
            UserDefaults.standard.removeObject(forKey: receiptKey)
        } catch {
            // Keep an unacknowledged receipt for the next poll.
        }
    }

    func dismissNotice() {
        announcement.stopSpeaking(at: .immediate)
        notice = nil
    }

    func press() {
        guard notice == nil, !calling, operation == nil, phase == .idle else { return }
        guard gateway != nil else { status = String(localized: "Pair with your Mac first"); return }
        pressed = true
        let id = generation
        operation = Task {
            let allowed = await AVAudioApplication.requestRecordPermission()
            guard !Task.isCancelled, generation == id else { return }
            guard allowed else { reset(status: String(localized: "Allow microphone access in Settings")); return }
            guard pressed else { reset(); return }
            do {
                try audio.start(calling: false)
                phase = .listening
                status = String(localized: "Listening")
            } catch {
                reset(status: message(for: error))
            }
        }
    }

    func release() {
        guard pressed else { return }
        pressed = false
        guard phase == .listening, !calling else { return }
        let pcm = audio.finishRecording()
        guard pcm.count >= 16 * 640 else {
            reset(status: String(localized: "Hold while speaking"))
            return
        }
        reply = ""
        phase = .waiting
        status = String(localized: "Waiting for reply")
        begin(calling: false, recording: pcm)
    }

    func dial() {
        guard notice == nil, phase == .idle, operation == nil else { return }
        guard gateway != nil else { status = String(localized: "Pair with your Mac first"); return }
        calling = true
        reply = ""
        phase = .waiting
        status = String(localized: "Calling")
        begin(calling: true)
    }

    private func begin(calling: Bool, recording: Data = Data()) {
        guard let gateway else { return }
        let id = generation
        operation = Task {
            var opened: String?
            var failure = ""
            do {
                if calling {
                    let allowed = await AVAudioApplication.requestRecordPermission()
                    try Task.checkCancellation()
                    guard allowed else { throw AnnaError.message(String(localized: "Allow microphone access in Settings")) }
                    try audio.start(calling: true)
                }
                let remote = try await gateway.open(mode: calling ? "call" : "single")
                opened = remote
                try Task.checkCancellation()
                connected = true
                var completed = false
                var canListen = false
                let start = ContinuousClock.now
                var initial = true
                while !Task.isCancelled {
                    if completed && !audio.isPlaying { break }
                    if !calling, ContinuousClock.now - start > .seconds(210) {
                        throw AnnaError.message(String(localized: "Reply timed out"))
                    }
                    let captured = calling ? audio.take() : Data()
                    let input = calling ? (canListen && !audio.isPlaying ? captured : Data())
                                        : (initial ? recording : Data())
                    let batch = try await gateway.exchange(remote, pcm: input, release: !calling && initial)
                    initial = false
                    try Task.checkCancellation()
                    for event in batch.events {
                        let parts = event.split(separator: ":", maxSplits: 1, omittingEmptySubsequences: false)
                        let detail = parts.count == 2 ? String(parts[1]) : ""
                        switch parts.first {
                        case "connected", "listening": canListen = true
                        case "speaking": canListen = false
                        case "done": completed = !calling
                        case "reply", "reply-view": reply = detail
                        case "error": throw AnnaError.message(detail)
                        default: break
                        }
                    }
                    for pcm in batch.audio { audio.play(pcm) }
                    if audio.isPlaying {
                        phase = .speaking
                        status = String(localized: "Dot is speaking")
                    } else if calling && canListen {
                        phase = .listening
                        status = String(localized: "Listening")
                    }
                    try await Task.sleep(for: .milliseconds(140))
                }
            } catch is CancellationError {
                // Hangup and background transitions own the local state.
            } catch {
                failure = message(for: error)
                if generation == id, error is URLError { connected = false }
            }
            if let opened { await Task { await gateway.close(opened) }.value }
            if generation == id { reset(status: failure) }
        }
    }

    func hangup() {
        reset()
    }

    func pause() {
        if operation != nil { hangup() }
        noticesSince = nil
        if notice != nil { dismissNotice() }
    }

    private func reset(status message: String = "") {
        operation?.cancel()
        operation = nil
        pressed = false
        generation = UUID()
        audio.stop()
        calling = false
        phase = .idle
        status = message
    }

    private func message(for error: Error) -> String {
        if let error = error as? AnnaError { return error.localizedDescription }
        if error is URLError { return String(localized: "Cannot reach Mac. Check the network.") }
        return String(localized: "Voice is unavailable. Try again.")
    }
}

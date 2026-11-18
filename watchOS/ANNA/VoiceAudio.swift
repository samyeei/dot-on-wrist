import AVFoundation
import Foundation

private final class AudioCapture: @unchecked Sendable {
    private let lock = NSLock()
    private var bytes = Data()
    private let capacity: Int
    private let keepLatest: Bool

    init(capacity: Int, keepLatest: Bool) {
        self.capacity = capacity
        self.keepLatest = keepLatest
    }

    func append(_ data: Data) {
        lock.lock()
        defer { lock.unlock() }
        if keepLatest {
            bytes.append(data)
            if bytes.count > capacity { bytes.removeFirst(bytes.count - capacity) }
        } else {
            bytes.append(data.prefix(max(0, capacity - bytes.count)))
        }
    }

    func take(final: Bool = false) -> Data {
        lock.lock()
        defer { lock.unlock() }
        if final, bytes.count % 640 != 0 {
            bytes.append(Data(count: 640 - bytes.count % 640))
        }
        let count = bytes.count / 640 * 640
        let output = Data(bytes.prefix(count))
        bytes.removeFirst(count)
        return output
    }
}

@MainActor final class VoiceAudio {
    private let engine = AVAudioEngine()
    private let player = AVAudioPlayerNode()
    private let wire = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: 16000,
                                     channels: 1, interleaved: true)!
    private let playback = AVAudioFormat(standardFormatWithSampleRate: 16000, channels: 1)!
    private var capture: AudioCapture?
    private(set) var pending = 0
    private var playbackID = UUID()
    var isPlaying: Bool { pending > 0 }

    init() {
        engine.attach(player)
        engine.connect(player, to: engine.mainMixerNode, format: playback)
    }

    func start(calling: Bool) throws {
        let session = AVAudioSession.sharedInstance()
        do {
            try session.setCategory(.playAndRecord, mode: .default)
            try session.setActive(true)
            let input = engine.inputNode
            let format = input.outputFormat(forBus: 0)
            guard format.sampleRate > 0, format.channelCount > 0,
                  let converter = AVAudioConverter(from: format, to: wire) else {
                throw AnnaError.message(String(localized: "Microphone unavailable"))
            }
            let capture = AudioCapture(capacity: calling ? 640 * 50 : 640 * 700, keepLatest: calling)
            let outputFormat = wire
            input.installTap(onBus: 0, bufferSize: 1024, format: format) { buffer, _ in
                let frames = AVAudioFrameCount(ceil(Double(buffer.frameLength) * 16000 / format.sampleRate)) + 16
                guard let output = AVAudioPCMBuffer(pcmFormat: outputFormat, frameCapacity: frames) else { return }
                var supplied = false
                let status = converter.convert(to: output, error: nil) { _, state in
                    if supplied {
                        state.pointee = .noDataNow
                        return nil
                    }
                    supplied = true
                    state.pointee = .haveData
                    return buffer
                }
                if status != .error, let samples = output.int16ChannelData?[0] {
                    capture.append(Data(bytes: samples, count: Int(output.frameLength) * 2))
                }
            }
            self.capture = capture
            try engine.start()
        } catch {
            stop()
            throw error
        }
    }

    func take() -> Data { capture?.take() ?? Data() }

    func finishRecording() -> Data {
        guard let capture else { return Data() }
        engine.inputNode.removeTap(onBus: 0)
        self.capture = nil
        return capture.take(final: true)
    }

    func play(_ data: Data) {
        let frames = data.count / 2
        guard frames > 0,
              let buffer = AVAudioPCMBuffer(pcmFormat: playback, frameCapacity: AVAudioFrameCount(frames)),
              let samples = buffer.floatChannelData?[0] else { return }
        buffer.frameLength = AVAudioFrameCount(frames)
        data.withUnsafeBytes { (bytes: UnsafeRawBufferPointer) in
            for index in 0..<frames {
                let word = bytes.loadUnaligned(fromByteOffset: index * 2, as: Int16.self)
                samples[index] = Float(Int16(littleEndian: word)) / 32768
            }
        }
        pending += 1
        let id = playbackID
        player.scheduleBuffer(buffer, completionCallbackType: .dataPlayedBack) { [weak self] _ in
            Task { @MainActor in
                guard let self, self.playbackID == id else { return }
                self.pending -= 1
            }
        }
        if !player.isPlaying { player.play() }
    }

    func stop() {
        if capture != nil { engine.inputNode.removeTap(onBus: 0) }
        capture = nil
        playbackID = UUID()
        pending = 0
        player.stop()
        engine.stop()
        try? AVAudioSession.sharedInstance().setActive(false)
    }
}

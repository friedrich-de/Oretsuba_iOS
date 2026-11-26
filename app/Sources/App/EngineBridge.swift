import AVFoundation
import Foundation

/// Owns the engine instance and its audio output.
final class EngineBridge {
    let engine: OpaquePointer
    private var audio = AVAudioEngine()
    private var source: AVAudioSourceNode?
    private var diagnosticSaves: URL?
    /// The app is in the background: the sound stays off until it is back.
    private var paused = false
    private var observers: [NSObjectProtocol] = []

    init(gameFolder: URL) throws {
        var saves = GamePaths.saves
        #if DEBUG
        let movieTest = ProcessInfo.processInfo.environment["ORE_TEST_MOVIE"].flatMap(Int.init)
        if let index = movieTest, (0...1).contains(index) {
            saves = FileManager.default.temporaryDirectory.appendingPathComponent("OreMovieTest-\(UUID().uuidString)")
            try FileManager.default.createDirectory(at: saves, withIntermediateDirectories: true)
            diagnosticSaves = saves
        }
        #endif
        var err = [CChar](repeating: 0, count: 512)
        let created: OpaquePointer? = gameFolder.path.withCString { data in
            saves.path.withCString { save in
                let keepPlace = GamePaths.keepsPlace
                var cfg = OreConfig(data_dir: data, save_dir: save,
                                    autosave_lines: keepPlace ? GamePaths.autosaveLines : 0,
                                    resume: keepPlace ? 1 : 0)
                return ore_create(&cfg, &err, Int32(err.count))
            }
        }
        guard let created else {
            throw NSError(domain: "Oretsuba", code: 1, userInfo: [NSLocalizedDescriptionKey: String(cString: err)])
        }
        #if DEBUG
        if let index = movieTest, diagnosticSaves != nil, ore_debug_start_movie(created, Int32(index)) == 0 {
            ore_destroy(created)
            throw NSError(domain: "Oretsuba", code: 2,
                          userInfo: [NSLocalizedDescriptionKey: "The opening film test could not be started."])
        }
        #endif
        engine = created
        startAudio()
    }

    deinit {
        observers.forEach(NotificationCenter.default.removeObserver)
        audio.stop()
        ore_destroy(engine)
        if let diagnosticSaves {
            do { try FileManager.default.removeItem(at: diagnosticSaves) }
            catch { fputs("movie diagnostic cleanup: \(error)\n", stderr) }
        }
    }

    private func startAudio() {
        connectAudio()
        observeAudio()
        resumeAudio()
    }

    /// The game's sound: a node that asks the engine for its samples, on the output's mixer.
    private func connectAudio() {
        let rate = Double(ore_audio_rate())
        guard let format = AVAudioFormat(standardFormatWithSampleRate: rate, channels: 2) else { return }
        let engine = self.engine
        var scratch = [Float](repeating: 0, count: 4096 * 2)
        let node = AVAudioSourceNode(format: format) { _, _, frameCount, bufferList -> OSStatus in
            let frames = Int(frameCount)
            if scratch.count < frames * 2 { scratch = [Float](repeating: 0, count: frames * 2) }
            scratch.withUnsafeMutableBufferPointer { ore_audio_mix(engine, $0.baseAddress, Int32(frames)) }
            // non-interleaved stereo: one buffer per channel
            let count = Int(bufferList.pointee.mNumberBuffers)
            guard count >= 2 else { return noErr }
            withUnsafeMutablePointer(to: &bufferList.pointee.mBuffers) { first in
                let buffers = UnsafeMutableBufferPointer(start: first, count: count)
                guard let left = buffers[0].mData?.assumingMemoryBound(to: Float.self),
                      let right = buffers[1].mData?.assumingMemoryBound(to: Float.self) else { return }
                for i in 0..<frames {
                    left[i] = scratch[i * 2]
                    right[i] = scratch[i * 2 + 1]
                }
            }
            return noErr
        }
        audio.attach(node)
        audio.connect(node, to: audio.mainMixerNode, format: format)
        source = node
    }

    /// Takes the device's sound for the game and starts the output again if it stopped, unless the
    /// app is in the background. Cheap when all is well, so it is also called to make sure.
    func resumeAudio() {
        guard !paused else { return }
        let session = AVAudioSession.sharedInstance()
        // Playback: the game is heard even with the silent switch on.
        if session.category != .playback || session.mode != .default {
            try? session.setCategory(.playback, mode: .default)
        }
        try? session.setActive(true)
        guard !audio.isRunning else { return }
        do {
            try audio.start()
            fputs("audio: the output runs\n", stderr)
        } catch {
            fputs("audio: \(error)\n", stderr)
        }
    }

    /// The game's sound comes back after whatever took it: a call, Siri, an alarm, another app's
    /// player, headphones put in or taken out, the system's sound starting anew. The output stops
    /// at each of them, and before this it stayed off until the game was started again.
    private func observeAudio() {
        let center = NotificationCenter.default
        observers.append(center.addObserver(forName: AVAudioSession.interruptionNotification, object: nil,
                                            queue: .main) { [weak self] note in
            guard let raw = note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt,
                  AVAudioSession.InterruptionType(rawValue: raw) == .ended else { return }
            self?.resumeAudio()
        })
        observers.append(center.addObserver(forName: .AVAudioEngineConfigurationChange, object: nil,
                                            queue: .main) { [weak self] note in
            guard let self, (note.object as AnyObject?) === self.audio else { return }
            self.resumeAudio()
        })
        observers.append(center.addObserver(forName: AVAudioSession.mediaServicesWereResetNotification, object: nil,
                                            queue: .main) { [weak self] _ in self?.rebuildAudio() })
    }

    /// After the system's sound started anew, nothing of the old output works: a new one.
    private func rebuildAudio() {
        audio.stop()
        source = nil
        audio = AVAudioEngine()
        connectAudio()
        resumeAudio()
    }

    func setPaused(_ paused: Bool) {
        self.paused = paused
        ore_set_paused(engine, paused ? 1 : 0)
        if paused { audio.pause() } else { resumeAudio() }
    }

    /// A sound of the app's own over the game's, through the same output: the word the dictionary
    /// popup plays (docs/MINING.md). 16-bit samples, `channels` (1 or 2) a frame, interleaved, at
    /// `rate`; nil stops it. False when it does not play.
    @discardableResult
    func playAppSound(_ samples: Data?, channels: Int, rate: Int) -> Bool {
        guard let samples, channels > 0, !samples.isEmpty else {
            ore_play_app_sound(engine, nil, 0, 0, 0)
            return samples == nil
        }
        // A word is only heard when the output runs.
        resumeAudio()
        let frames = samples.count / 2 / channels
        return samples.withUnsafeBytes { raw in
            ore_play_app_sound(engine, raw.baseAddress?.assumingMemoryBound(to: Int16.self), Int32(frames),
                               Int32(channels), Int32(rate)) == 1
        }
    }
}

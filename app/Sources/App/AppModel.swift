import Foundation
import SwiftUI

/// The app's state: where the files are and whether the game is running.
@MainActor
final class AppModel: ObservableObject {
    @Published var gameFolder: URL?
    @Published var playing = false
    @Published var errorMessage: String?
    @Published var importing = false
    @Published var importProgress = ""
    /// Mining (docs/MINING.md): what the game folder's mining.txt leads to.
    /// `nil` until the server was asked, `0` without a mining.txt, `1` when
    /// lines will arrive, `-1` when something is wrong; the text says what.
    @Published var miningState: Int32?
    @Published var miningText = ""

    init() {
        refresh()
        if ProcessInfo.processInfo.environment["ORE_AUTOSTART"] != nil, gameFolder != nil {
            playing = true
        }
    }

    func refresh() {
        gameFolder = GamePaths.findGameFolder()
        checkMining()
    }

    /// Asks the mining server named in mining.txt whether it takes the key,
    /// away from the main thread: the answer can take some seconds.
    private func checkMining() {
        guard let path = gameFolder?.path else {
            miningState = nil
            miningText = ""
            return
        }
        Task.detached {
            var text = [CChar](repeating: 0, count: 512)
            let size = Int32(text.count)
            let state = ore_mining_check(path, &text, size)
            let message = String(cString: text)
            await MainActor.run {
                guard self.gameFolder?.path == path else { return }
                self.miningState = state
                self.miningText = message
            }
        }
    }

    func start() {
        errorMessage = nil
        guard gameFolder != nil else { return }
        playing = true
    }

    func stop(error: String?) {
        playing = false
        errorMessage = error
    }

    #if targetEnvironment(macCatalyst)
    func choose(gameFolder url: URL) {
        GamePaths.chosenGameFolder = url
        refresh()
        if gameFolder == nil { errorMessage = "There is no SCRIPT.LPK in the chosen folder." }
    }
    #else
    /// Copies a folder picked in the Files app into Documents.
    func importFolder(_ url: URL) {
        importing = true
        importProgress = "Copying \(url.lastPathComponent)…"
        let documents = GamePaths.documents
        Task.detached {
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            let failure: String?
            do {
                try FolderImport.copy(url, into: documents)
                failure = nil
            } catch {
                failure = "Copying failed: \(error.localizedDescription)"
            }
            await MainActor.run {
                self.importing = false
                self.importProgress = ""
                self.errorMessage = failure
                self.refresh()
            }
        }
    }
    #endif
}

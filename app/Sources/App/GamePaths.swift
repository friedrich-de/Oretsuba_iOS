import Foundation

/// Where the port finds the player's game files and keeps its own.
///
/// * iPhone, iPad, Vision Pro: the game folder (the DVD's SET_UP folder, or
///   the whole disc) is copied into the app's Documents (Finder file sharing,
///   the Files app, AirDrop, or the in-app import). Saves live in Application
///   Support.
/// * Mac: the player points at the folder where it is; nothing is copied.
enum GamePaths {
    static let fileManager = FileManager.default

    static var documents: URL {
        fileManager.urls(for: .documentDirectory, in: .userDomainMask)[0]
    }

    static var applicationSupport: URL {
        var url = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        #if targetEnvironment(macCatalyst)
        url = url.appendingPathComponent("Oretsuba", isDirectory: true)
        #endif
        try? fileManager.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    /// Saves and settings. The folder keeps the name earlier versions gave
    /// the Japanese original's saves, so those are still found.
    static var saves: URL {
        let url = applicationSupport.appendingPathComponent("Saves/jpn", isDirectory: true)
        try? fileManager.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    /// Keeping the reader's place (docs/AUTOSAVE.md): the launcher's switch,
    /// how often the engine saves, and the save it writes.
    static let keepsPlaceKey = "keepsPlace"
    static var keepsPlace: Bool { UserDefaults.standard.bool(forKey: keepsPlaceKey) }
    static let autosaveLines: Int32 = 5
    static var hasAutosave: Bool {
        fileManager.fileExists(atPath: saves.appendingPathComponent("DATA1200.SAV").path)
    }

    /// The folder the player picked on the Mac (a security-scoped bookmark is
    /// not needed: Catalyst apps here are not sandboxed).
    static var chosenGameFolder: URL? {
        get { UserDefaults.standard.string(forKey: "gameFolderPath").map { URL(fileURLWithPath: $0, isDirectory: true) } }
        set { UserDefaults.standard.set(newValue?.path, forKey: "gameFolderPath") }
    }

    /// The folder holding SCRIPT.LPK, searched a few levels deep.
    static func findGameFolder() -> URL? {
        var roots: [URL] = []
        if let chosen = chosenGameFolder { roots.append(chosen) }
        #if !targetEnvironment(macCatalyst)
        roots.append(documents)
        #endif
        for root in roots {
            if let found = search(root, depth: 3, where: { hasFile($0, named: "SCRIPT.LPK") }) { return found }
        }
        return nil
    }

    private static func search(_ dir: URL, depth: Int, where match: (URL) -> Bool) -> URL? {
        if match(dir) { return dir }
        guard depth > 0, let items = try? fileManager.contentsOfDirectory(
            at: dir, includingPropertiesForKeys: [.isDirectoryKey], options: [.skipsHiddenFiles]) else { return nil }
        for item in items.sorted(by: { $0.lastPathComponent < $1.lastPathComponent }) {
            let isDir = (try? item.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) ?? false
            if isDir, let found = search(item, depth: depth - 1, where: match) { return found }
        }
        return nil
    }

    private static func names(in dir: URL) -> [String] {
        ((try? fileManager.contentsOfDirectory(atPath: dir.path)) ?? []).map { $0.uppercased() }
    }

    private static func hasFile(_ dir: URL, named name: String) -> Bool {
        names(in: dir).contains(name)
    }
}

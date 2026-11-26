import Foundation
import Security

extension Notification.Name {
    /// Something the mining kit tells without having been asked (docs/MINING.md).
    /// Posted on the main thread; `userInfo` holds `"name"` (String) and
    /// `"payload"` (String, the JSON text).
    static let oreKitEvent = Notification.Name("OreKitEvent")
}

/// What the kit answered in place of a result: a sentence for the player and its kind.
struct KitError: LocalizedError {
    let message: String
    let kind: String
    var errorDescription: String? { message }
}

/// The mining kit for the app (docs/MINING.md): looking words up and making
/// Anki cards inside the game. There is one for the app's whole life; the
/// launcher's settings and the popup over the game both speak to it. The kit
/// itself is C++ (engine/src/mining); this carries its calls and keeps the two
/// secrets that are not among the kit's settings, the AnkiConnect's key and
/// the audio source's address (`Secrets`).
final class MiningKit {
    /// Nil when the kit could not be opened; `problem` then says why.
    static let shared: MiningKit? = MiningKit()
    static private(set) var problem = ""

    let kit: OpaquePointer
    /// Calls may wait for the network, so they run here, several at a time.
    private let work = DispatchQueue(label: "ore.mining.kit", qos: .userInitiated, attributes: .concurrent)

    private init?() {
        let folder = GamePaths.applicationSupport.appendingPathComponent("Mining", isDirectory: true)
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        guard let resources = Bundle.main.url(forResource: "mining", withExtension: nil) else {
            MiningKit.problem = "The app's mining files are missing."
            return nil
        }
        var err = [CChar](repeating: 0, count: 512)
        guard let opened = ore_kit_open(folder.path, resources.path, &err, Int32(err.count)) else {
            MiningKit.problem = String(cString: err)
            return nil
        }
        kit = opened
        // Dictionaries are large and can be imported again: they stay out of backups.
        var dictionaries = folder.appendingPathComponent("dictionaries", isDirectory: true)
        var values = URLResourceValues()
        values.isExcludedFromBackup = true
        try? dictionaries.setResourceValues(values)

        ore_kit_set_events(opened, nil) { _, name, payload in
            guard let name else { return }
            let info = ["name": String(cString: name), "payload": payload.map { String(cString: $0) } ?? "null"]
            DispatchQueue.main.async {
                NotificationCenter.default.post(name: .oreKitEvent, object: nil, userInfo: info)
            }
        }
        _ = callRaw("secrets.set", json: MiningKit.jsonText([
            "ankiKey": Secrets.get(.ankiKey),
            "audioAddress": Secrets.get(.audioAddress),
        ]))
    }

    /// Whether a tap on the text looks a word up.
    var direct: Bool { ore_kit_direct(kit) != 0 }

    /// The kit's answer to a method as JSON text. Waits for the answer: not for the main thread.
    func callRaw(_ method: String, json params: String) -> String {
        guard let answer = ore_kit_call(kit, method, params) else {
            return #"{"ok":false,"error":"The kit did not answer.","kind":"internal"}"#
        }
        defer { ore_kit_free(answer) }
        return String(cString: answer)
    }

    /// The same, away from the caller's thread.
    func callRaw(_ method: String, json params: String, then: @escaping (String) -> Void) {
        work.async { then(self.callRaw(method, json: params)) }
    }

    /// The result of a method, or the kit's sentence as an error.
    func call(_ method: String, _ params: [String: Any] = [:]) async throws -> Any {
        let text = await withCheckedContinuation { (continuation: CheckedContinuation<String, Never>) in
            callRaw(method, json: MiningKit.jsonText(params)) { continuation.resume(returning: $0) }
        }
        guard let answer = try? JSONSerialization.jsonObject(with: Data(text.utf8)) as? [String: Any] else {
            throw KitError(message: "The kit did not answer.", kind: "internal")
        }
        guard answer["ok"] as? Bool == true else {
            throw KitError(message: answer["error"] as? String ?? "The kit did not answer.",
                           kind: answer["kind"] as? String ?? "internal")
        }
        return answer["result"] ?? NSNull()
    }

    /// A file of the kit's for the popup's page. Waits like a call.
    func data(_ method: String, json params: String) -> (data: Data, type: String)? {
        var bytes: UnsafeMutablePointer<UInt8>?
        var length: Int64 = 0
        var type = [CChar](repeating: 0, count: 128)
        guard ore_kit_data(kit, method, params, &bytes, &length, &type, Int32(type.count)) != 0, let bytes else { return nil }
        defer { ore_kit_free(bytes) }
        return (Data(bytes: bytes, count: Int(length)), String(cString: type))
    }

    func data(_ method: String, json params: String, then: @escaping ((data: Data, type: String)?) -> Void) {
        work.async { then(self.data(method, json: params)) }
    }

    /// Tells the kit a secret and keeps it. False when it could not be kept:
    /// the kit then has the secret until the app ends.
    @discardableResult
    func setSecret(_ secret: Secrets.Name, _ value: String) async throws -> Bool {
        _ = try await call("secrets.set", [secret.kitName: value])
        return Secrets.set(secret, value)
    }

    static func jsonText(_ value: Any) -> String {
        guard JSONSerialization.isValidJSONObject(value),
              let data = try? JSONSerialization.data(withJSONObject: value) else { return "{}" }
        return String(decoding: data, as: UTF8.self)
    }
}

/// The two things of the kit's that are secrets: an AnkiConnect's key, and the
/// audio source's address, which often holds a key of its own. They are kept in
/// the Keychain of this device only. Where the Keychain refuses, as it does for
/// a Mac build that is signed without a team, they are kept in a file of the
/// app's own that no other user can read.
enum Secrets {
    enum Name: String {
        case ankiKey = "anki-key"
        case audioAddress = "audio-address"

        var kitName: String { self == .ankiKey ? "ankiKey" : "audioAddress" }
    }

    private static let service = "oretsuba.mining"

    private static func query(_ name: Name) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service,
         kSecAttrAccount as String: name.rawValue]
    }

    static func get(_ name: Name) -> String {
        var wanted = query(name)
        wanted[kSecReturnData as String] = true
        wanted[kSecMatchLimit as String] = kSecMatchLimitOne
        var found: CFTypeRef?
        if SecItemCopyMatching(wanted as CFDictionary, &found) == errSecSuccess, let data = found as? Data {
            return String(decoding: data, as: UTF8.self)
        }
        return besideKeychain()[name.rawValue] ?? ""
    }

    /// Keeps a secret, or forgets it when it is empty. False when it could not be kept at all.
    @discardableResult
    static func set(_ name: Name, _ value: String) -> Bool {
        SecItemDelete(query(name) as CFDictionary)
        var inKeychain = false
        if !value.isEmpty {
            var item = query(name)
            item[kSecValueData as String] = Data(value.utf8)
            item[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
            inKeychain = SecItemAdd(item as CFDictionary, nil) == errSecSuccess
        }
        var beside = besideKeychain()
        let wanted = value.isEmpty || inKeychain ? nil : value
        if beside[name.rawValue] == wanted { return true }
        beside[name.rawValue] = wanted
        return keepBesideKeychain(beside)
    }

    // MARK: - where the Keychain refuses

    private static var file: URL {
        GamePaths.applicationSupport.appendingPathComponent("Mining/secrets.json")
    }

    private static func besideKeychain() -> [String: String] {
        guard let data = try? Data(contentsOf: file) else { return [:] }
        return (try? JSONSerialization.jsonObject(with: data)) as? [String: String] ?? [:]
    }

    private static func keepBesideKeychain(_ values: [String: String]) -> Bool {
        let manager = FileManager.default
        if values.isEmpty {
            try? manager.removeItem(at: file)
            return !manager.fileExists(atPath: file.path)
        }
        guard let data = try? JSONSerialization.data(withJSONObject: values) else { return false }
        do {
            try manager.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
            try data.write(to: file, options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
            try manager.setAttributes([.posixPermissions: 0o600], ofItemAtPath: file.path)
            // Like the Keychain's items of this device only, the file stays out of backups.
            var kept = file
            var values = URLResourceValues()
            values.isExcludedFromBackup = true
            try? kept.setResourceValues(values)
            return true
        } catch {
            return false
        }
    }
}

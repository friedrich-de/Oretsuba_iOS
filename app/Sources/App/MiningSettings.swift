import Foundation
import SwiftUI

/// What the launcher's settings for mining in the game show and change
/// (docs/MINING.md). All of it lives in the mining kit; this asks and tells.
@MainActor
final class MiningSettingsModel: ObservableObject {
    /// One thing a field of a card can hold: the word, the sentence, the picture ...
    struct Role: Identifiable, Hashable {
        let role: String
        let label: String
        let detail: String
        var id: String { role }
    }

    struct Field: Identifiable, Hashable {
        let field: String
        var role: String
        var id: String { field }
    }

    struct Installed: Identifiable, Hashable {
        let id: String
        let title: String
        let revision: String
        let terms: Int
        let frequencies: Int
        let pitches: Int
        var enabled: Bool

        /// What it holds, in a few words.
        var holds: String {
            func counted(_ count: Int, _ one: String, _ many: String) -> String? {
                count > 0 ? "\(count.formatted()) \(count == 1 ? one : many)" : nil
            }
            let parts = [counted(terms, "word", "words"), counted(frequencies, "frequency", "frequencies"),
                         counted(pitches, "pitch accent", "pitch accents")].compactMap { $0 }
            return parts.isEmpty ? "empty" : parts.joined(separator: ", ")
        }
    }

    /// How asking a server went.
    enum Check: Equatable {
        case idle
        case working
        case good(String)
        case bad(String)
    }

    static let textSizes = [70, 80, 90, 100, 115, 130]

    let kit: MiningKit?

    @Published var direct = false
    @Published var address = ""
    /// A key typed in and not yet told; the kit never gives a key back.
    @Published var key = ""
    @Published var keySet = false
    @Published var deck = ""
    @Published var noteType = ""
    @Published var fields: [Field] = []
    @Published var tags = ""
    @Published var decks: [String] = []
    @Published var noteTypes: [String] = []
    @Published var roles: [Role] = []
    @Published var anki = Check.idle
    /// An address typed in and not yet told; like the key it is not given back.
    @Published var audioAddress = ""
    @Published var audioSet = false
    @Published var audio = Check.idle
    @Published var dictionaries: [Installed] = []
    @Published var importState = "idle"
    @Published var importProgress = 0.0
    @Published var importTitle = ""
    @Published var importError = ""
    @Published var textSize = 100
    @Published var hideTags = false
    @Published var compact = false
    /// Why no card can be made as things are; empty when one can.
    @Published var cardProblem = ""
    @Published var problem: String?
    @Published var loaded = false

    /// The tags as the kit holds them; `tags` is what stands in the text field.
    private var keptTags = ""
    private var watching: Task<Void, Never>?

    init(kit: MiningKit? = MiningKit.shared) {
        self.kit = kit
    }

    var importRunning: Bool { importState == "downloading" || importState == "importing" }

    /// The launcher's line about all this.
    var summary: String {
        guard kit != nil else { return MiningKit.problem }
        guard loaded else { return "" }
        let count = dictionaries.filter(\.enabled).count
        let words = count == 1 ? "1 dictionary" : "\(count) dictionaries"
        if !direct { return "Off: a tap on the text reads on." }
        if count == 0 { return "On, but without a dictionary no word is found." }
        if cardProblem.isEmpty { return "On: \(words), cards go to the deck \(deck)." }
        return "On: \(words), words are looked up. No cards yet: \(cardProblem)"
    }

    // MARK: - reading what the kit holds

    func load() async {
        guard let kit else { return }
        do {
            if roles.isEmpty, let list = try await kit.call("anki.roleChoices") as? [[String: Any]] {
                roles = list.map {
                    Role(role: $0["role"] as? String ?? "", label: $0["label"] as? String ?? "", detail: $0["detail"] as? String ?? "")
                }
            }
            apply(try await kit.call("settings.get"), typed: true)
            applyDictionaries(try await kit.call("dict.list"))
            problem = nil
        } catch {
            problem = error.localizedDescription
        }
        await refreshCards()
        loaded = true
        if importRunning { watchImport() }
    }

    /// Takes the settings as the kit holds them. What the player may be typing, the address and
    /// the tags, is only replaced with `typed`.
    private func apply(_ value: Any, typed: Bool = false) {
        guard let settings = value as? [String: Any] else { return }
        direct = settings["direct"] as? Bool ?? false
        keySet = settings["ankiKeySet"] as? Bool ?? false
        audioSet = settings["audioSet"] as? Bool ?? false
        let anki = settings["anki"] as? [String: Any] ?? [:]
        keptTags = (anki["tags"] as? [String] ?? []).joined(separator: " ")
        if typed {
            address = anki["address"] as? String ?? ""
            tags = keptTags
        }
        deck = anki["deck"] as? String ?? ""
        noteType = anki["noteType"] as? String ?? ""
        fields = (anki["fields"] as? [[String: Any]] ?? []).map {
            Field(field: $0["field"] as? String ?? "", role: $0["role"] as? String ?? "none")
        }
        let popup = settings["popup"] as? [String: Any] ?? [:]
        textSize = popup["textSize"] as? Int ?? 100
        hideTags = popup["hideTags"] as? Bool ?? false
        compact = popup["compact"] as? Bool ?? false
    }

    private func applyDictionaries(_ value: Any) {
        guard let answer = value as? [String: Any] else { return }
        dictionaries = (answer["dictionaries"] as? [[String: Any]] ?? []).map {
            Installed(id: $0["id"] as? String ?? "", title: $0["title"] as? String ?? "", revision: $0["revision"] as? String ?? "",
                      terms: $0["terms"] as? Int ?? 0, frequencies: $0["frequencies"] as? Int ?? 0, pitches: $0["pitches"] as? Int ?? 0,
                      enabled: $0["enabled"] as? Bool ?? true)
        }
        let state = answer["import"] as? [String: Any] ?? [:]
        importState = state["state"] as? String ?? "idle"
        importProgress = state["progress"] as? Double ?? 0
        importTitle = state["title"] as? String ?? ""
        importError = state["error"] as? String ?? ""
    }

    /// Whether a card could be made as things are.
    private func refreshCards() async {
        guard let kit, let page = try? await kit.call("page.init") as? [String: Any] else { return }
        let cards = page["cards"] as? [String: Any] ?? [:]
        cardProblem = cards["possible"] as? Bool == true ? "" : cards["problem"] as? String ?? ""
    }

    /// A call that answers with the settings as they are now.
    private func change(_ method: String, _ params: [String: Any]) async {
        guard let kit else { return }
        do {
            apply(try await kit.call(method, params))
            problem = nil
        } catch {
            problem = error.localizedDescription
        }
        await refreshCards()
    }

    /// Says so when a secret could not be kept.
    private func kept(_ taken: Bool) {
        if !taken { problem = "This device could not keep it: it has to be entered again after the app was closed." }
    }

    // MARK: - the game and the popup

    func setDirect(_ on: Bool) async {
        direct = on
        await change("settings.set", ["direct": on])
    }

    func setPopup(textSize: Int? = nil, hideTags: Bool? = nil, compact: Bool? = nil) async {
        var popup: [String: Any] = [:]
        if let textSize { popup["textSize"] = textSize }
        if let hideTags { popup["hideTags"] = hideTags }
        if let compact { popup["compact"] = compact }
        await change("settings.set", ["popup": popup])
    }

    // MARK: - Anki

    /// Asks the AnkiConnect at the address for its decks and note types. With
    /// `adopt` the address and the key become the kit's, and where nothing was
    /// chosen yet the Lapis note type and its deck are.
    func connect(adopt: Bool) async {
        guard let kit else { return }
        let typed = address.trimmingCharacters(in: .whitespacesAndNewlines)
        if typed.isEmpty {
            decks = []
            noteTypes = []
            anki = .idle
            if adopt { await change("settings.set", ["anki": ["address": ""]]) }
            return
        }
        anki = .working
        var params: [String: Any] = ["address": typed, "adopt": adopt]
        if !key.isEmpty { params["key"] = key }
        do {
            let answer = try await kit.call("anki.check", params) as? [String: Any] ?? [:]
            if adopt, !key.isEmpty {
                kept(Secrets.set(.ankiKey, key))
                key = ""
            }
            decks = answer["decks"] as? [String] ?? []
            noteTypes = answer["noteTypes"] as? [String] ?? []
            if let settings = answer["settings"] { apply(settings, typed: adopt) }
            anki = .good(answer["lapis"] is [String: Any]
                ? "Connected. The Lapis note type is there."
                : "Connected. There is no Lapis note type: choose a note type and what goes into its fields.")
        } catch {
            anki = .bad(error.localizedDescription)
        }
        await refreshCards()
    }

    func removeKey() async {
        guard let kit else { return }
        do {
            try await kit.setSecret(.ankiKey, "")
            key = ""
            keySet = false
        } catch {
            problem = error.localizedDescription
        }
    }

    func choose(deck: String) async {
        await change("anki.choose", ["deck": deck])
    }

    func choose(noteType: String) async {
        await change("anki.choose", ["noteType": noteType])
    }

    func setRole(_ field: String, _ role: String) async {
        await change("anki.setRole", ["field": field, "role": role])
    }

    /// Tells the kit the tags in the text field, when they are not the ones it holds.
    func setTags() async {
        let wanted = tags.split(separator: " ").map(String.init)
        guard wanted.joined(separator: " ") != keptTags else { return }
        await change("settings.set", ["anki": ["tags": wanted]])
        tags = keptTags
    }

    // MARK: - word audio

    /// Asks the audio source for the sounds of a word every source has, and keeps an address that answers.
    func checkAudio() async {
        guard let kit else { return }
        let typed = audioAddress.trimmingCharacters(in: .whitespacesAndNewlines)
        audio = .working
        do {
            let answer = try await kit.call("audio.check", typed.isEmpty ? [:] : ["address": typed]) as? [String: Any] ?? [:]
            let count = (answer["names"] as? [String] ?? []).count
            if !typed.isEmpty {
                kept(try await kit.setSecret(.audioAddress, typed))
                audioAddress = ""
                audioSet = true
            }
            audio = .good(count == 1 ? "It answers, with one sound for 食べる." : "It answers, with \(count) sounds for 食べる.")
        } catch {
            audio = .bad(error.localizedDescription)
        }
        await refreshCards()
    }

    func removeAudio() async {
        guard let kit else { return }
        do {
            try await kit.setSecret(.audioAddress, "")
            audioAddress = ""
            audioSet = false
            audio = .idle
        } catch {
            problem = error.localizedDescription
        }
    }

    // MARK: - dictionaries

    private func dictionaryCall(_ method: String, _ params: [String: Any] = [:]) async -> Bool {
        guard let kit else { return false }
        do {
            applyDictionaries(try await kit.call(method, params))
            problem = nil
            if importRunning { watchImport() }
            return true
        } catch {
            problem = error.localizedDescription
            return false
        }
    }

    /// Imports a dictionary picked in the Files app. The kit reads a copy of it: the picked file is only lent.
    func importFile(_ url: URL) async {
        let name = url.lastPathComponent
        let copy = FileManager.default.temporaryDirectory.appendingPathComponent("ore-dictionary-\(UUID().uuidString).zip")
        let copied = await Task.detached { () -> Bool in
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            return (try? FileManager.default.copyItem(at: url, to: copy)) != nil
        }.value
        guard copied else {
            problem = "\(name) could not be read."
            return
        }
        let taken = await dictionaryCall("dict.import", ["path": copy.path, "name": name, "remove": true])
        if !taken { try? FileManager.default.removeItem(at: copy) }
    }

    func download(_ address: String) async {
        _ = await dictionaryCall("dict.download", ["url": address.trimmingCharacters(in: .whitespacesAndNewlines)])
    }

    func cancelImport() async {
        _ = await dictionaryCall("dict.cancel")
    }

    func enable(_ id: String, _ on: Bool) async {
        _ = await dictionaryCall("dict.enable", ["id": id, "enabled": on])
        await refreshCards()
    }

    func remove(_ id: String) async {
        _ = await dictionaryCall("dict.remove", ["id": id])
        await refreshCards()
    }

    /// The order is the order the popup shows a word's meanings in.
    func move(from: IndexSet, to: Int) {
        dictionaries.move(fromOffsets: from, toOffset: to)
        let order = dictionaries.map(\.id)
        let disabled = dictionaries.filter { !$0.enabled }.map(\.id)
        Task {
            let arranged = await dictionaryCall("dict.arrange", ["order": order, "disabled": disabled])
            if !arranged { _ = await dictionaryCall("dict.list") }
        }
    }

    /// Follows an import until it is over.
    private func watchImport() {
        guard watching == nil else { return }
        watching = Task { [weak self] in
            while true {
                try? await Task.sleep(nanoseconds: 300_000_000)
                guard let self, let kit = self.kit else { return }
                if let list = try? await kit.call("dict.list") { self.applyDictionaries(list) }
                if !self.importRunning {
                    self.watching = nil
                    await self.refreshCards()
                    return
                }
            }
        }
    }
}

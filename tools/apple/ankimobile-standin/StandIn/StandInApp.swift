// A stand-in for AnkiMobile, for tests in the iPhone Simulator. It does what
// AnkiMobile's manual (docs.ankiweb.net, "URL Schemes") says AnkiMobile does
// with the two links other apps open it with, and no more:
//
//   anki://x-callback-url/infoForAdding?x-success=LINK
//       leaves its decks and note types on the clipboard as JSON of the type
//       net.ankimobile.json, then opens LINK
//   anki://x-callback-url/addnote?type=..&deck=..&fldNAME=..&tags=..&dupes=1&x-success=LINK
//       fetches every field whose whole value is a web link that ends like a
//       picture or a sound, keeps the note, then opens LINK. A first field it
//       has already is refused unless dupes=1: then it stays in front and
//       opens nothing, as AnkiMobile does when it shows its message.
//
// What it did goes into Documents/standin.log, a line of JSON for each link.
import SwiftUI
import UIKit

@main
struct StandInApp: App {
    @StateObject private var model = StandIn()

    var body: some Scene {
        WindowGroup {
            VStack(spacing: 16) {
                Text("Anki stand-in").font(.title)
                Text(model.status).accessibilityIdentifier("status")
            }
            .padding()
            .onOpenURL { model.opened($0) }
        }
    }
}

@MainActor
final class StandIn: ObservableObject {
    @Published var status = "waiting"
    private var lists = 0, added = 0, refused = 0
    private var words = Set<String>()

    /// The note type and deck a test should use come last.
    private let answer = """
    {"profiles":[{"name":"User 1"}],"decks":[{"name":"Default"},{"name":"日本語::Mining"}],\
    "notetypes":[{"name":"Basic","fields":[{"name":"Front"},{"name":"Back"}]},\
    {"name":"Check","fields":[{"name":"Word"},{"name":"Sentence"},{"name":"Picture"},{"name":"Sound"}]}]}
    """

    /// The endings Anki takes for a picture or a sound.
    private let media: Set<String> = ["jpg", "jpeg", "png", "gif", "webp", "svg", "avif", "tif", "tiff", "ico",
                                      "mp3", "m4a", "ogg", "oga", "opus", "wav", "flac", "aac", "mp4", "webm", "3gp", "spx"]

    func opened(_ link: URL) {
        guard let parts = URLComponents(url: link, resolvingAgainstBaseURL: false),
              parts.scheme == "anki", parts.host == "x-callback-url" else {
            log(["link": link.absoluteString, "did": "not understood"])
            return
        }
        let items = parts.queryItems ?? []
        func value(_ name: String) -> String? { items.first { $0.name == name }?.value }
        let back = value("x-success").flatMap { URL(string: $0) }

        switch parts.path {
        case "/infoForAdding":
            UIPasteboard.general.setData(Data(answer.utf8), forPasteboardType: "net.ankimobile.json")
            lists += 1
            log(["did": "lists", "back": back?.absoluteString ?? ""])
            finish(back)
        case "/addnote":
            let fields = items.filter { $0.name.hasPrefix("fld") }.map { (String($0.name.dropFirst(3)), $0.value ?? "") }
            let word = fields.first?.1 ?? ""
            if words.contains(word), value("dupes") != "1" {
                refused += 1
                log(["did": "refused", "word": word])
                show()
                return
            }
            let start: [String: Any] = ["did": "added", "type": value("type") ?? "", "deck": value("deck") ?? "",
                                        "tags": value("tags") ?? "", "dupes": value("dupes") ?? "",
                                        "profile": value("profile") ?? "", "back": back?.absoluteString ?? ""]
            Task {
                var record = start
                var text: [String: String] = [:]
                var files: [[String: Any]] = []
                for (name, content) in fields {
                    if let address = URL(string: content), let scheme = address.scheme, scheme == "http" || scheme == "https",
                       media.contains(address.pathExtension.lowercased()) {
                        files.append(await fetch(address, field: name))
                    } else {
                        text[name] = content
                    }
                }
                record["fields"] = text
                record["files"] = files
                words.insert(word)
                added += 1
                log(record)
                finish(back)
            }
        default:
            log(["link": link.absoluteString, "did": "not understood"])
        }
    }

    private func fetch(_ address: URL, field: String) async -> [String: Any] {
        var file: [String: Any] = ["field": field, "name": address.lastPathComponent]
        do {
            let (data, response) = try await URLSession.shared.data(from: address)
            let answer = response as? HTTPURLResponse
            file["status"] = answer?.statusCode ?? 0
            file["type"] = answer?.value(forHTTPHeaderField: "Content-Type") ?? ""
            file["bytes"] = data.count
            file["sum"] = Int(data.reduce(UInt32(0)) { $0 &+ UInt32($1) })
        } catch {
            file["status"] = 0
            file["error"] = error.localizedDescription
        }
        return file
    }

    private func show() {
        status = "lists \(lists) added \(added) refused \(refused)"
    }

    private func finish(_ back: URL?) {
        show()
        if let back { UIApplication.shared.open(back) }
    }

    private func log(_ record: [String: Any]) {
        guard let line = try? JSONSerialization.data(withJSONObject: record, options: [.sortedKeys]) else { return }
        let file = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("standin.log")
        if let handle = try? FileHandle(forWritingTo: file) {
            handle.seekToEndOfFile()
            handle.write(line)
            handle.write(Data("\n".utf8))
            try? handle.close()
        } else {
            try? (line + Data("\n".utf8)).write(to: file)
        }
    }
}

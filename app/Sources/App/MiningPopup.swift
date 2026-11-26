import UIKit
import WebKit

/// The dictionary popup over the game (docs/MINING.md): a web view that lies
/// over the game's picture and shows nothing but the popup and the marked
/// word. Its page (engine/resources/mining/page) is the same on every system;
/// this is the glue: it serves the page's files, carries the page's calls to
/// the mining kit, and tells the page which character was tapped.
///
/// While the view is shown it takes every touch, so nothing else reacts until
/// the popup is closed.
final class MiningPopup: NSObject, WKScriptMessageHandlerWithReply, WKURLSchemeHandler, WKNavigationDelegate {
    private static let scheme = "ore-mining"
    private static let page = URL(string: "ore-mining://page/popup.html")!

    let webView: WKWebView
    /// The popup was closed, by the page or because the page broke: let the game go on.
    var onClose: (() -> Void)?
    /// Plays a word's sound with the game's (16-bit samples, so many channels a frame, at a rate);
    /// nil stops it. False when it does not play. The page reads the sound file and hands over its
    /// samples: a sound the web view played itself took the device's sound from the game, which
    /// stayed silent after it.
    var onSound: ((_ samples: Data?, _ channels: Int, _ rate: Int) -> Bool)?

    private let kit: MiningKit
    private let files: URL?
    private var ready = false
    private var waiting: String?
    private var running = Set<ObjectIdentifier>()
    private var observer: NSObjectProtocol?

    init(kit: MiningKit) {
        self.kit = kit
        files = Bundle.main.url(forResource: "mining", withExtension: nil)?.appendingPathComponent("page", isDirectory: true)
        let configuration = WKWebViewConfiguration()
        configuration.allowsInlineMediaPlayback = true
        // A word's sound plays after it was fetched, which is no longer "during" the tap.
        configuration.mediaTypesRequiringUserActionForPlayback = []
        configuration.websiteDataStore = .nonPersistent()
        // Both have to be there before the web view is made, which works on a copy of all this.
        let relay = PopupRelay()
        configuration.setURLSchemeHandler(relay, forURLScheme: MiningPopup.scheme)
        configuration.userContentController.addScriptMessageHandler(relay, contentWorld: .page, name: "oreKit")
        webView = WKWebView(frame: .zero, configuration: configuration)
        super.init()
        relay.target = self
        webView.navigationDelegate = self
        webView.isOpaque = false
        webView.backgroundColor = .clear
        webView.scrollView.backgroundColor = .clear
        webView.scrollView.isScrollEnabled = false
        webView.scrollView.bounces = false
        webView.scrollView.contentInsetAdjustmentBehavior = .never
        webView.allowsLinkPreview = false
        webView.isHidden = true
        #if DEBUG
        if #available(iOS 16.4, macCatalyst 16.4, *) { webView.isInspectable = true }
        #endif
        observer = NotificationCenter.default.addObserver(forName: .oreKitEvent, object: nil, queue: .main) { [weak self] note in
            guard let name = note.userInfo?["name"] as? String, let payload = note.userInfo?["payload"] as? String else { return }
            self?.run("window.oreMining && window.oreMining.event(\(MiningPopup.quoted(name)), \(payload))")
        }
        webView.load(URLRequest(url: MiningPopup.page))
    }

    deinit {
        if let observer { NotificationCenter.default.removeObserver(observer) }
    }

    // MARK: - what the game tells the page

    /// Shows the popup for a tapped character. `tap` is the engine's JSON
    /// (ore_text_tap); `picture` is where the game's picture is in the web
    /// view, in points.
    func show(tap: String, picture: CGRect) {
        let place = "[\(picture.minX),\(picture.minY),\(picture.width),\(picture.height)]"
        let script = "window.oreMining.show(Object.assign(\(tap), {picture: \(place)}))"
        webView.isHidden = false
        if ready { run(script) } else { waiting = script }
    }

    /// Closes the popup from the app's side, as the page's own close does.
    func close() {
        guard isOpen else { return }
        waiting = nil
        webView.isHidden = true
        run("window.oreMining && window.oreMining.hide()")
        _ = onSound?(nil, 0, 0)
        onClose?()
    }

    var isOpen: Bool { !webView.isHidden }

    private func run(_ script: String) {
        webView.evaluateJavaScript(script, completionHandler: nil)
    }

    /// A text as JavaScript writes it.
    private static func quoted(_ text: String) -> String {
        String(MiningKit.jsonText([text]).dropFirst().dropLast())
    }

    // MARK: - the page's calls

    func userContentController(_ controller: WKUserContentController, didReceive message: WKScriptMessage,
                               replyHandler: @escaping (Any?, String?) -> Void) {
        guard let body = message.body as? [String: Any], let method = body["method"] as? String else {
            replyHandler(#"{"ok":false,"error":"The call is not one.","kind":"internal"}"#, nil)
            return
        }
        let params = body["params"] as? String ?? "{}"
        // What starts with "popup." is for the app itself.
        if method.hasPrefix("popup.") {
            var result = "null"
            switch method {
            case "popup.ready":
                ready = true
                if let script = waiting {
                    waiting = nil
                    run(script)
                }
                // A word's sound is the app's to play.
                result = #"{"sound":true}"#
            case "popup.close" where isOpen:
                waiting = nil
                webView.isHidden = true
                _ = onSound?(nil, 0, 0)
                onClose?()
            case "popup.sound":
                if let problem = playSound(params) {
                    replyHandler(#"{"ok":false,"error":\#(MiningPopup.quoted(problem)),"kind":"internal"}"#, nil)
                    return
                }
            default:
                break
            }
            replyHandler(#"{"ok":true,"result":\#(result)}"#, nil)
            return
        }
        kit.callRaw(method, json: params) { answer in
            DispatchQueue.main.async { replyHandler(answer, nil) }
        }
    }

    /// popup.sound: {rate, channels, frames, pcm} plays the samples (pcm, in base64), {} stops what
    /// plays. Nil when that went well, else a sentence for the player.
    private func playSound(_ params: String) -> String? {
        guard let object = (try? JSONSerialization.jsonObject(with: Data(params.utf8))) as? [String: Any] else {
            return "The sound is not one."
        }
        guard let pcm = object["pcm"] as? String else {
            _ = onSound?(nil, 0, 0)
            return nil
        }
        guard let channels = object["channels"] as? Int, let rate = object["rate"] as? Int, (1...2).contains(channels),
              let samples = Data(base64Encoded: pcm), !samples.isEmpty, samples.count % (2 * channels) == 0 else {
            return "The sound is not one."
        }
        return onSound?(samples, channels, rate) == true ? nil : "The game could not play the sound."
    }

    // MARK: - a page that broke

    /// Without its page the popup would hold the game for good: it closes and the page is loaded again.
    private func broken() {
        ready = false
        close()
        webView.load(URLRequest(url: MiningPopup.page))
    }

    func webViewWebContentProcessDidTerminate(_ webView: WKWebView) {
        broken()
    }

    func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
        fputs("mining popup: \(error.localizedDescription)\n", stderr)
        ready = false
        close()
    }

    func webView(_ webView: WKWebView, decidePolicyFor action: WKNavigationAction,
                 decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        // The page stays where it is: a link in a definition leads nowhere.
        decisionHandler(action.request.url?.scheme == MiningPopup.scheme ? .allow : .cancel)
    }

    // MARK: - the page's files

    private static let types = ["html": "text/html; charset=utf-8", "js": "text/javascript; charset=utf-8",
                                "css": "text/css; charset=utf-8", "json": "application/json"]

    func webView(_ webView: WKWebView, start task: WKURLSchemeTask) {
        guard let url = task.request.url, url.host == "page" else { return fail(task, 404) }
        let path = url.path
        if path.hasPrefix("/kit/data/") {
            // A file of the kit's: a dictionary's picture, the line's picture or voice, a word's sound.
            let method = String(path.dropFirst("/kit/data/".count))
            let params = URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems?.first { $0.name == "p" }?.value ?? "{}"
            let id = ObjectIdentifier(task)
            running.insert(id)
            kit.data(method, json: params) { [weak self] found in
                DispatchQueue.main.async {
                    // A task the page gave up must not be answered.
                    guard let self, self.running.remove(id) != nil else { return }
                    if let found { self.answer(task, url, found.data, found.type) } else { self.fail(task, 404) }
                }
            }
            return
        }
        // A file of the page's own, and nothing outside its folder.
        guard let files, !path.contains(".."), let name = path.split(separator: "/").last else { return fail(task, 404) }
        let file = files.appendingPathComponent(String(path.dropFirst()))
        guard let data = try? Data(contentsOf: file),
              let type = MiningPopup.types[String(name.split(separator: ".").last ?? "")] else { return fail(task, 404) }
        answer(task, url, data, type)
    }

    func webView(_ webView: WKWebView, stop task: WKURLSchemeTask) {
        running.remove(ObjectIdentifier(task))
    }

    private func answer(_ task: WKURLSchemeTask, _ url: URL, _ data: Data, _ type: String) {
        let headers = ["Content-Type": type, "Content-Length": String(data.count), "Cache-Control": "no-store"]
        guard let response = HTTPURLResponse(url: url, statusCode: 200, httpVersion: "HTTP/1.1", headerFields: headers) else { return }
        task.didReceive(response)
        task.didReceive(data)
        task.didFinish()
    }

    private func fail(_ task: WKURLSchemeTask, _ status: Int) {
        guard let url = task.request.url,
              let response = HTTPURLResponse(url: url, statusCode: status, httpVersion: "HTTP/1.1", headerFields: [:]) else { return }
        task.didReceive(response)
        task.didFinish()
    }
}

/// The web view keeps the handlers of its scheme and of its messages alive,
/// and the popup keeps the web view: this stands between them so that neither
/// holds the other for good.
private final class PopupRelay: NSObject, WKURLSchemeHandler, WKScriptMessageHandlerWithReply {
    weak var target: MiningPopup?

    func webView(_ webView: WKWebView, start task: WKURLSchemeTask) {
        guard let target else {
            task.didFailWithError(URLError(.cancelled))
            return
        }
        target.webView(webView, start: task)
    }

    func webView(_ webView: WKWebView, stop task: WKURLSchemeTask) {
        target?.webView(webView, stop: task)
    }

    func userContentController(_ controller: WKUserContentController, didReceive message: WKScriptMessage,
                               replyHandler: @escaping (Any?, String?) -> Void) {
        guard let target else {
            replyHandler(nil, "The popup is gone.")
            return
        }
        target.userContentController(controller, didReceive: message, replyHandler: replyHandler)
    }
}

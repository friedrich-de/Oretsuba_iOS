#if DEBUG
import SwiftUI
import UIKit

/// Device smoke test of the cards for AnkiMobile, for debug builds in the
/// Simulator (tools/apple/ankimobile-standin/run.sh). The environment variable
/// ORE_TEST_ANKIMOBILE starts it and names the report's file in the app's
/// Documents; the engine writes the report (ore_debug_ankimobile_check), and
/// its last line is shown on screen so that a UI test can wait for "done".
@MainActor
final class AnkiCheck: ObservableObject {
    @Published var last = ""
    private var timer: Timer?
    private var waiting: NSObjectProtocol?

    init() {
        guard let name = ProcessInfo.processInfo.environment["ORE_TEST_ANKIMOBILE"], !name.isEmpty else { return }
        let report = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent(name)
        try? FileManager.default.removeItem(at: report)
        // iOS opens a link to another app only for the app in front, so the
        // check begins a moment after this one has come there.
        waiting = NotificationCenter.default.addObserver(
            forName: UIApplication.didBecomeActiveNotification, object: nil, queue: .main
        ) { [weak self] _ in
            Task { @MainActor in self?.begin(report) }
        }
        if UIApplication.shared.applicationState == .active { begin(report) }
    }

    private func begin(_ report: URL) {
        guard let waiting else { return }
        NotificationCenter.default.removeObserver(waiting)
        self.waiting = nil
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
            ore_debug_ankimobile_check(NSTemporaryDirectory(), report.path)
        }
        timer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            let text = (try? String(contentsOf: report, encoding: .utf8)) ?? ""
            let line = text.split(separator: "\n").last.map(String.init) ?? ""
            Task { @MainActor in self?.last = line }
        }
    }
}

struct AnkiCheckLine: View {
    @ObservedObject var check: AnkiCheck

    var body: some View {
        if !check.last.isEmpty {
            Text(check.last)
                .font(.caption)
                .padding(6)
                .background(.black.opacity(0.7))
                .accessibilityIdentifier("ore-anki-check")
        }
    }
}
#endif

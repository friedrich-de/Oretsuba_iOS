// Plays the player while the app's own check of the cards for AnkiMobile runs
// (ore_debug_ankimobile_check, started by the environment variable
// ORE_TEST_ANKIMOBILE): allows the app to read what the stand-in left on the
// clipboard, and goes back to the app by hand when the stand-in keeps a card
// to itself. The app shows the last line of its report; the test waits for
// the line that begins with "done". run.sh reads the report afterwards.
import XCTest

final class RoundTripTests: XCTestCase {
    func testCardsReachTheStandIn() {
        continueAfterFailure = true
        let bundle = ProcessInfo.processInfo.environment["ORE_APP_BUNDLE"] ?? "org.local.oretsuba.player"
        let app = XCUIApplication(bundleIdentifier: bundle)
        let standIn = XCUIApplication(bundleIdentifier: "org.local.ankistandin")
        let system = XCUIApplication(bundleIdentifier: "com.apple.springboard")
        app.launchEnvironment["ORE_TEST_ANKIMOBILE"] = "anki-check.txt"
        app.launch()

        let allow = NSPredicate(format: "label BEGINSWITH[c] 'Allow'")
        // Should the system ask before it opens the other app.
        let open = NSPredicate(format: "label ==[c] 'Open'")
        let end = Date().addingTimeInterval(240)
        var last = ""
        var wentBack = false
        var shot = 0
        while Date() < end {
            // "Oretsuba would like to paste from Anki stand-in": the system asks, or the app's own window does.
            var answered = false
            for owner in [system, app] {
                var button = owner.buttons.matching(allow).firstMatch
                if !button.exists { button = owner.alerts.buttons.matching(open).firstMatch }
                if button.exists {
                    keep("paste question \(shot)")
                    shot += 1
                    button.tap()
                    answered = true
                    break
                }
            }
            if answered { continue }
            if standIn.state == .runningForeground {
                let status = standIn.staticTexts["status"]
                if !wentBack, status.exists, status.label.contains("refused 1") {
                    // AnkiMobile shows why it took no card and stays in front; the player goes back by hand.
                    keep("stand-in refused")
                    sleep(1)
                    app.activate()
                    wentBack = true
                }
            } else if app.state == .runningForeground {
                let line = app.staticTexts["ore-anki-check"]
                if line.exists {
                    last = line.label
                    if last.hasPrefix("done") { break }
                }
            }
            usleep(300_000)
        }
        keep("end")
        XCTAssertTrue(wentBack, "the stand-in refused the second card and the app was brought back by hand")
        XCTAssertEqual(last, "done: three cards", "the app's check ran to its end")
    }

    private func keep(_ name: String) {
        let picture = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        picture.name = name
        picture.lifetime = .keepAlways
        add(picture)
    }
}

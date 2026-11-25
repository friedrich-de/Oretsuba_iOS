import SwiftUI

@main
struct OreApp: App {
    @StateObject private var model = AppModel()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(model)
                .preferredColorScheme(.dark)
        }
        #if os(visionOS)
        .defaultSize(width: 1600, height: 1200)
        #endif
    }
}

struct RootView: View {
    @EnvironmentObject private var model: AppModel
    @StateObject private var controls = GameControls()

    var body: some View {
        if model.playing, let game = model.gameFolder {
            GameView(gameFolder: game, controls: controls) { error in
                model.stop(error: error)
            }
            .ignoresSafeArea()
            .persistentSystemOverlays(.hidden)
            #if os(visionOS)
            .ornament(attachmentAnchor: .scene(.bottom), contentAlignment: .top) {
                VisionGameControls(controls: controls)
            }
            #endif
        } else {
            LauncherView()
        }
    }
}

/// Hosts the game's view controller.
struct GameView: UIViewControllerRepresentable {
    let gameFolder: URL
    let controls: GameControls
    let onExit: (String?) -> Void

    func makeUIViewController(context: Context) -> GameViewController {
        let controller = GameViewController(gameFolder: gameFolder, onExit: onExit)
        controls.controller = controller
        return controller
    }

    func updateUIViewController(_ controller: GameViewController, context: Context) {}
}

/// Weak ownership lets native controls target the currently displayed game.
final class GameControls: ObservableObject {
    weak var controller: GameViewController?

    func send(_ command: GameCommand) { controller?.sendCommand(command) }
}

#if os(visionOS)
private struct VisionGameControls: View {
    let controls: GameControls

    var body: some View {
        HStack(spacing: 12) {
            action("Save", .save)
            action("Load", .load)
            action("Quick save", .quickSave)
            action("Quick load", .quickLoad)
            action("History", .history)
            action("Menu", .menu)
        }
        .padding(16)
        .glassBackgroundEffect()
    }

    private func action(_ title: String, _ command: GameCommand) -> some View {
        Button { controls.send(command) } label: {
            Text(title).font(.title3).frame(minWidth: 72, minHeight: 60)
        }
        .buttonStyle(.borderless)
        .accessibilityLabel(title)
    }
}
#endif

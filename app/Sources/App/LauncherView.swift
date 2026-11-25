import SwiftUI
import UniformTypeIdentifiers

/// The first screen: whether the game files are there and how to bring them
/// onto this device.
struct LauncherView: View {
    @EnvironmentObject private var model: AppModel
    @State private var showingImporter = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 24) {
                VStack(alignment: .leading, spacing: 6) {
                    Text("俺たちに翼はない").font(.largeTitle.bold())
                    Text("Oretsuba · a port for Apple devices").foregroundStyle(.secondary)
                }
                statusCard
                if model.gameFolder != nil {
                    Button {
                        model.start()
                    } label: {
                        Text("Start game").font(.title2.bold()).frame(maxWidth: .infinity).padding(.vertical, 8)
                    }
                    .buttonStyle(.borderedProminent)
                }
                importCard
                if let error = model.errorMessage {
                    Text(error).foregroundStyle(.red)
                }
                controlsHelp
            }
            .padding(.horizontal, 40)
            .padding(.vertical, 36)
            .frame(maxWidth: 860, alignment: .leading)
            .frame(maxWidth: .infinity)
        }
        .onAppear { model.refresh() }
        .fileImporter(isPresented: $showingImporter,
                      allowedContentTypes: [.folder]) { result in
            guard case .success(let url) = result else { return }
            #if targetEnvironment(macCatalyst)
            model.choose(gameFolder: url)
            #else
            model.importFolder(url)
            #endif
        }
    }

    private var statusCard: some View {
        GroupBox("Game files") {
            VStack(alignment: .leading, spacing: 8) {
                if let folder = model.gameFolder {
                    Label("Game files found", systemImage: "checkmark.circle.fill").foregroundStyle(.green)
                    Text(folder.path).font(.footnote).foregroundStyle(.secondary).lineLimit(2)
                } else {
                    Label("No game files found yet", systemImage: "exclamationmark.triangle.fill").foregroundStyle(.orange)
                    Text("Bring in the SET_UP folder from the disc of your own copy of the game (the folder with SCRIPT.LPK, PIC.LPK and the other files).")
                        .foregroundStyle(.secondary)
                }
                if model.gameFolder != nil, let mining = model.miningState {
                    if mining > 0 {
                        Label(model.miningText, systemImage: "antenna.radiowaves.left.and.right")
                            .foregroundStyle(Color.green)
                    } else if mining < 0 {
                        Label(model.miningText, systemImage: "exclamationmark.triangle.fill")
                            .foregroundStyle(Color.orange)
                    } else {
                        Label("Mining is off (optional: put mining.txt next to SCRIPT.LPK)",
                              systemImage: "antenna.radiowaves.left.and.right")
                            .foregroundStyle(.secondary)
                    }
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private var importCard: some View {
        GroupBox(model.gameFolder == nil ? "Import the game files" : "Manage files") {
            VStack(alignment: .leading, spacing: 12) {
                #if targetEnvironment(macCatalyst)
                Text("Choose the game folder on this Mac (the one with SCRIPT.LPK). Nothing is copied.")
                Button("Choose game folder…") { showingImporter = true }
                #else
                Text("Option 1: connect this device to a Mac with a cable, select it in the Finder, open Files › Oretsuba and drag the SET_UP folder in.")
                Text("Option 2: save the folder to the Files app first, by AirDrop or any other way, then import it with the button below.")
                Button("Import a folder from Files…") { showingImporter = true }
                    .disabled(model.importing)
                if model.importing {
                    ProgressView(model.importProgress)
                }
                #endif
                Button("Check again") { model.refresh() }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private var controlsHelp: some View {
        VStack(alignment: .leading, spacing: 4) {
            #if targetEnvironment(macCatalyst)
            Text("Controls as on the PC: left button, right button (menu), wheel (history), hold Ctrl to skip.")
            #else
            Text("Controls: tap = left button · tap with two fingers = right button (menu) · swipe up or down = wheel (history) · long press = skip.")
            Text("A mouse, trackpad or keyboard works too, as on the PC.")
            #endif
        }
        .font(.footnote)
        .foregroundStyle(.secondary)
    }
}

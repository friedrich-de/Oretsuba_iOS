import SwiftUI
import UniformTypeIdentifiers

/// The launcher's sheet for mining in the game (docs/MINING.md): whether the
/// text looks words up, where cards go, the dictionaries, the word audio and
/// how the popup looks.
struct MiningSettingsView: View {
    @ObservedObject var model: MiningSettingsModel
    @Environment(\.dismiss) private var dismiss
    @State private var pickingFile = false
    @State private var askingAddress = false
    @State private var downloadAddress = ""

    var body: some View {
        NavigationStack {
            Form {
                gameSection
                ankiSection
                dictionarySection
                audioSection
                popupSection
            }
            .navigationTitle("Mining in the game")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    if model.dictionaries.count > 1 { EditButton() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .task {
            await model.load()
            // The decks and note types to choose from, when an AnkiConnect is set already.
            if !model.address.isEmpty { await model.connect(adopt: false) }
        }
        .onDisappear {
            Task { await model.setTags() }
        }
        .fileImporter(isPresented: $pickingFile, allowedContentTypes: [.zip]) { result in
            guard case .success(let url) = result else { return }
            Task { await model.importFile(url) }
        }
        .alert("Download a dictionary", isPresented: $askingAddress) {
            TextField("https://…", text: $downloadAddress)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .keyboardType(.URL)
            Button("Cancel", role: .cancel) {}
            Button("Download") {
                let address = downloadAddress
                Task { await model.download(address) }
            }
        } message: {
            Text("The address of a dictionary in Yomitan's format (a zip file).")
        }
    }

    // MARK: - the game

    private var gameSection: some View {
        Section {
            Toggle("Look words up in the game", isOn: Binding(get: { model.direct }, set: { on in
                Task { await model.setDirect(on) }
            }))
            if let problem = model.problem {
                Text(problem).foregroundStyle(.red)
            }
        } footer: {
            Text("A tap on a word of the text then shows the dictionary, and the game waits until the dictionary is closed. The text box no longer reads on: tap the picture above it, or use a key, the wheel or a controller.")
        }
    }

    // MARK: - Anki

    private var ankiSection: some View {
        Section {
            TextField("Address of your AnkiConnect", text: $model.address)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .keyboardType(.URL)
                .onSubmit { Task { await model.connect(adopt: true) } }
            SecureField(model.keySet ? "Key (one is kept; type to replace it)" : "Key, if it asks for one", text: $model.key)
            HStack {
                Button("Connect") { Task { await model.connect(adopt: true) } }
                    .disabled(model.anki == .working)
                if model.anki == .working {
                    Spacer()
                    ProgressView()
                }
            }
            checkLine(model.anki)
            if model.keySet {
                Button("Forget the key", role: .destructive) { Task { await model.removeKey() } }
            }
            if !model.decks.isEmpty {
                Picker("Deck", selection: Binding(get: { model.deck }, set: { deck in
                    Task { await model.choose(deck: deck) }
                })) {
                    if !model.decks.contains(model.deck) {
                        Text(model.deck.isEmpty ? "None chosen" : model.deck).tag(model.deck)
                    }
                    ForEach(model.decks, id: \.self) { Text($0).tag($0) }
                }
                Picker("Note type", selection: Binding(get: { model.noteType }, set: { noteType in
                    Task { await model.choose(noteType: noteType) }
                })) {
                    if !model.noteTypes.contains(model.noteType) {
                        Text(model.noteType.isEmpty ? "None chosen" : model.noteType).tag(model.noteType)
                    }
                    ForEach(model.noteTypes, id: \.self) { Text($0).tag($0) }
                }
            } else if !model.deck.isEmpty || !model.noteType.isEmpty {
                LabeledContent("Deck", value: model.deck)
                LabeledContent("Note type", value: model.noteType)
            }
            if !model.fields.isEmpty {
                NavigationLink("What goes into the fields") { MiningFieldsView(model: model) }
            }
            LabeledContent("Tags") {
                TextField("parted by blanks", text: $model.tags)
                    .multilineTextAlignment(.trailing)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .onSubmit { Task { await model.setTags() } }
            }
            if !model.cardProblem.isEmpty, !model.address.isEmpty {
                Label(model.cardProblem, systemImage: "exclamationmark.triangle.fill")
                    .font(.footnote)
                    .foregroundStyle(.orange)
            }
        } header: {
            Text("Anki")
        } footer: {
            Text("Cards go to the AnkiConnect you name here: the one of Anki on your computer, or a server of your own. With the Lapis note type there is nothing more to choose. An address in your own network may start with http; any other has to start with https. The key is kept in this device's Keychain.")
        }
    }

    // MARK: - dictionaries

    private var dictionarySection: some View {
        Section {
            ForEach(model.dictionaries) { dictionary in
                Toggle(isOn: Binding(get: { dictionary.enabled }, set: { on in
                    Task { await model.enable(dictionary.id, on) }
                })) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(dictionary.title)
                        Text(dictionary.holds).font(.footnote).foregroundStyle(.secondary)
                    }
                }
            }
            .onDelete { offsets in
                let gone = offsets.map { model.dictionaries[$0].id }
                Task {
                    for id in gone { await model.remove(id) }
                }
            }
            .onMove { from, to in model.move(from: from, to: to) }
            if model.importRunning {
                VStack(alignment: .leading, spacing: 6) {
                    Text(model.importState == "downloading" ? "Downloading…"
                        : model.importTitle.isEmpty ? "Importing…" : "Importing \(model.importTitle)…")
                    ProgressView(value: min(max(model.importProgress, 0), 1))
                }
                Button("Stop", role: .destructive) { Task { await model.cancelImport() } }
            } else {
                Button("Import from Files…") { pickingFile = true }
                Button("Download from an address…") {
                    downloadAddress = ""
                    askingAddress = true
                }
            }
            if model.importState == "failed", !model.importError.isEmpty {
                Label(model.importError, systemImage: "exclamationmark.triangle.fill")
                    .font(.footnote)
                    .foregroundStyle(.orange)
            }
        } header: {
            Text("Dictionaries")
        } footer: {
            Text("Dictionaries in Yomitan's format (zip files) are kept on this device. A word's meanings are shown in this order; swipe one away to remove it. Dictionaries of frequencies and of pitch accents count too.")
        }
    }

    // MARK: - word audio

    private var audioSection: some View {
        Section {
            TextField(model.audioSet ? "An address is kept; type to replace it" : "Address of an audio source", text: $model.audioAddress)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .keyboardType(.URL)
                .onSubmit { Task { await model.checkAudio() } }
            HStack {
                Button(model.audioAddress.isEmpty && model.audioSet ? "Check" : "Check and keep") {
                    Task { await model.checkAudio() }
                }
                .disabled(model.audio == .working || (model.audioAddress.isEmpty && !model.audioSet))
                if model.audio == .working {
                    Spacer()
                    ProgressView()
                }
            }
            checkLine(model.audio)
            if model.audioSet {
                Button("Forget the address", role: .destructive) { Task { await model.removeAudio() } }
            }
        } header: {
            Text("Word audio")
        } footer: {
            Text("The address of an audio source as Yomitan takes a custom one, with {term} and {reading} where the word and its reading go. The popup plays its sounds, and a card gets the one chosen. The address is kept in this device's Keychain, as it often holds a key.")
        }
    }

    // MARK: - the popup's look

    private var popupSection: some View {
        Section("The dictionary") {
            Picker("Text size", selection: Binding(get: { model.textSize }, set: { size in
                Task { await model.setPopup(textSize: size) }
            })) {
                ForEach(MiningSettingsModel.textSizes, id: \.self) { Text("\($0) %").tag($0) }
            }
            Toggle("Hide tags", isOn: Binding(get: { model.hideTags }, set: { on in
                Task { await model.setPopup(hideTags: on) }
            }))
            Toggle("Compact meanings", isOn: Binding(get: { model.compact }, set: { on in
                Task { await model.setPopup(compact: on) }
            }))
        }
    }

    @ViewBuilder
    private func checkLine(_ check: MiningSettingsModel.Check) -> some View {
        switch check {
        case .idle, .working:
            EmptyView()
        case .good(let text):
            Label(text, systemImage: "checkmark.circle.fill").font(.footnote).foregroundStyle(.green)
        case .bad(let text):
            Label(text, systemImage: "exclamationmark.triangle.fill").font(.footnote).foregroundStyle(.orange)
        }
    }
}

/// What each field of the chosen note type gets when a card is made.
struct MiningFieldsView: View {
    @ObservedObject var model: MiningSettingsModel

    var body: some View {
        Form {
            Section {
                ForEach(model.fields) { field in
                    Picker(field.field, selection: Binding(get: { field.role }, set: { role in
                        Task { await model.setRole(field.field, role) }
                    })) {
                        ForEach(model.roles) { Text($0.label).tag($0.role) }
                    }
                }
            } header: {
                Text("The fields of \(model.noteType)")
            } footer: {
                Text("The Lapis note type's fields are filled as its makers mean them to be. In any other note type, choose what each field gets.")
            }
            Section("What a field can get") {
                ForEach(model.roles.filter { $0.role != "none" }) { role in
                    LabeledContent(role.label, value: role.detail)
                }
            }
        }
        .navigationTitle("Fields")
        .navigationBarTitleDisplayMode(.inline)
    }
}

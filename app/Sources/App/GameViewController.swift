import GameController
import MetalKit
import UIKit

/// The game screen: an MTKView showing the engine's picture, and every kind
/// of input turned into the mouse and keyboard the game expects.
///
///   touch (iPhone, iPad, Vision Pro gaze + pinch)
///     tap                 left click
///     two-finger tap      right click (the game's menu / hides the text window)
///     swipe down / up     mouse wheel (the backlog)
///     press and hold      Ctrl while held (skip)
///   mouse or trackpad     move, left / right click and scroll as on a PC
///   keyboard              Return, Space, Escape, arrows, Ctrl ...
///   game controller       A = left click, B = right click, right trigger = skip,
///                         d-pad up / down = wheel
///
/// With mining in the game on (docs/MINING.md) a tap or click on a word of the
/// text shows the dictionary popup instead; until the popup is closed the game
/// stands still and nothing above reaches it.
final class GameViewController: UIViewController, UIGestureRecognizerDelegate {
    private let gameFolder: URL
    private let onExit: (String?) -> Void

    private var renderer: Renderer!
    private var bridge: EngineBridge?
    private var mtkView: MTKView { view as! MTKView }
    private var wheelAccumulator: CGFloat = 0
    private var holdingCtrl = false
    private var observers: [NSObjectProtocol] = []
    private var popup: MiningPopup?
    private var laidOut = CGSize.zero

    init(gameFolder: URL, onExit: @escaping (String?) -> Void) {
        self.gameFolder = gameFolder
        self.onExit = onExit
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    override func loadView() {
        view = MTKView(frame: .zero)
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        guard let renderer = Renderer(view: mtkView) else {
            onExit("Metal is not available.")
            return
        }
        self.renderer = renderer
        mtkView.delegate = renderer
        mtkView.isMultipleTouchEnabled = true
        do {
            let bridge = try EngineBridge(gameFolder: gameFolder)
            self.bridge = bridge
            renderer.bridge = bridge
        } catch {
            onExit(error.localizedDescription)
            return
        }
        renderer.onStop = { [weak self] error in self?.onExit(error) }
        installDialogs()
        installMining()
        installGestures()
        installControllers()
        let center = NotificationCenter.default
        observers.append(center.addObserver(forName: UIApplication.didEnterBackgroundNotification, object: nil,
                                            queue: .main) { [weak self] _ in self?.bridge?.setPaused(true) })
        observers.append(center.addObserver(forName: UIApplication.willEnterForegroundNotification, object: nil,
                                            queue: .main) { [weak self] _ in self?.bridge?.setPaused(false) })
    }

    deinit {
        observers.forEach(NotificationCenter.default.removeObserver)
        if let engine { ore_set_kit(engine, nil) }
    }

    override var prefersStatusBarHidden: Bool { true }
    override var prefersHomeIndicatorAutoHidden: Bool { true }
    override var preferredScreenEdgesDeferringSystemGestures: UIRectEdge { .all }

    private var engine: OpaquePointer? { bridge?.engine }

    func sendCommand(_ command: GameCommand) {
        guard let engine else { return }
        ore_game_command(engine, command.rawValue)
    }

    // MARK: - message boxes and text entry asked for by the game

    private func installDialogs() {
        guard let engine else { return }
        let ctx = Unmanaged.passUnretained(self).toOpaque()
        ore_set_ui_callbacks(engine, ctx, { ctx, text, kind in
            guard let ctx, let text else { return }
            let controller = Unmanaged<GameViewController>.fromOpaque(ctx).takeUnretainedValue()
            let message = String(cString: text)
            DispatchQueue.main.async { controller.presentDialog(message, yesNo: kind == 1) }
        }, { ctx, initial in
            guard let ctx else { return }
            let controller = Unmanaged<GameViewController>.fromOpaque(ctx).takeUnretainedValue()
            let value = initial.map { String(cString: $0) } ?? ""
            DispatchQueue.main.async { controller.presentTextInput(value) }
        })
    }

    private func presentDialog(_ message: String, yesNo: Bool) {
        let alert = UIAlertController(title: nil, message: message, preferredStyle: .alert)
        let answer: (Int32) -> Void = { [weak self] r in
            guard let engine = self?.engine else { return }
            ore_answer_dialog(engine, r)
        }
        if yesNo {
            alert.addAction(UIAlertAction(title: "No", style: .cancel) { _ in answer(0) })
            alert.addAction(UIAlertAction(title: "Yes", style: .default) { _ in answer(1) })
        } else {
            alert.addAction(UIAlertAction(title: "OK", style: .default) { _ in answer(1) })
        }
        present(alert, animated: true)
    }

    private func presentTextInput(_ initial: String) {
        let alert = UIAlertController(title: "Comment", message: nil, preferredStyle: .alert)
        alert.addTextField { $0.text = initial }
        alert.addAction(UIAlertAction(title: "Cancel", style: .cancel) { [weak self] _ in
            guard let engine = self?.engine else { return }
            ore_answer_text_input(engine, 0, nil)
        })
        alert.addAction(UIAlertAction(title: "OK", style: .default) { [weak self, weak alert] _ in
            guard let engine = self?.engine else { return }
            let text = alert?.textFields?.first?.text ?? ""
            text.withCString { ore_answer_text_input(engine, 1, $0) }
        })
        present(alert, animated: true)
    }

    // MARK: - mining in the game

    /// Gives the engine the mining kit and lays the popup's web view over the picture.
    private func installMining() {
        guard let engine, let kit = MiningKit.shared else { return }
        ore_set_kit(engine, kit.kit)
        let popup = MiningPopup(kit: kit)
        popup.onClose = { [weak self] in self?.popupClosed() }
        popup.webView.frame = view.bounds
        popup.webView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        view.addSubview(popup.webView)
        self.popup = popup
    }

    private var popupOpen: Bool { popup?.isOpen == true }

    /// Asks the engine what a tap at a place of the game's screen is. True when
    /// it is not the game's: it hit a word, which the popup now shows, or the
    /// text box beside the words, where nothing happens.
    private func lookUp(_ engine: OpaquePointer, at p: (Int32, Int32)) -> Bool {
        guard let popup else { return false }
        if popup.isOpen { return true }
        var json: UnsafeMutablePointer<CChar>?
        let what = ore_text_tap(engine, p.0, p.1, &json)
        guard what == 1, let json else { return what != 0 }
        defer { ore_kit_free(json) }
        setCtrl(false)
        ore_set_reading_hold(engine, 1)
        popup.show(tap: String(cString: json), picture: renderer.contentRect)
        return true
    }

    private func popupClosed() {
        // Keys go to the game again, not to the page.
        popup?.webView.endEditing(true)
        guard let engine else { return }
        ore_set_reading_hold(engine, 0)
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        // The picture moved under the popup: its marks no longer lie on the words.
        if laidOut != view.bounds.size {
            laidOut = view.bounds.size
            popup?.close()
        }
    }

    /// The gestures below are the game's: while the popup is open they stay out of it.
    func gestureRecognizerShouldBegin(_ r: UIGestureRecognizer) -> Bool { !popupOpen }

    func gestureRecognizer(_ r: UIGestureRecognizer, shouldReceive touch: UITouch) -> Bool { !popupOpen }

    // MARK: - touch, pointer, scroll

    private func installGestures() {
        let tap = UITapGestureRecognizer(target: self, action: #selector(tap(_:)))
        view.addGestureRecognizer(tap)

        let twoFinger = UITapGestureRecognizer(target: self, action: #selector(rightTap(_:)))
        twoFinger.numberOfTouchesRequired = 2
        view.addGestureRecognizer(twoFinger)

        let hold = UILongPressGestureRecognizer(target: self, action: #selector(hold(_:)))
        hold.minimumPressDuration = 0.6
        view.addGestureRecognizer(hold)

        let swipe = UIPanGestureRecognizer(target: self, action: #selector(swipe(_:)))
        swipe.maximumNumberOfTouches = 1
        view.addGestureRecognizer(swipe)
        tap.require(toFail: swipe)

        // A mouse or trackpad is handled as a real mouse in the touches methods
        // below, so these gestures take fingers and the pencil only. Without
        // this a pointer click is also seen as a finger tap.
        let fingers = [UITouch.TouchType.direct, .pencil].map { NSNumber(value: $0.rawValue) }
        for r in [tap, twoFinger, hold, swipe] as [UIGestureRecognizer] {
            r.allowedTouchTypes = fingers
        }
        let scroll = UIPanGestureRecognizer(target: self, action: #selector(swipe(_:)))
        scroll.allowedScrollTypesMask = .all
        scroll.allowedTouchTypes = []
        view.addGestureRecognizer(scroll)

        let hover = UIHoverGestureRecognizer(target: self, action: #selector(hover(_:)))
        view.addGestureRecognizer(hover)
        for r in [tap, twoFinger, hold, swipe, scroll, hover] as [UIGestureRecognizer] {
            r.delegate = self
        }
    }

    private func gamePoint(_ location: CGPoint) -> (Int32, Int32) {
        let p = renderer.gamePoint(from: location)
        return (Int32(p.x), Int32(p.y))
    }

    @objc private func tap(_ r: UITapGestureRecognizer) {
        guard r.state == .ended, let engine else { return }
        let point = gamePoint(r.location(in: view))
        if lookUp(engine, at: point) { return }
        click(engine, button: 0, at: point)
    }

    @objc private func rightTap(_ r: UITapGestureRecognizer) {
        guard r.state == .ended, !popupOpen, let engine else { return }
        click(engine, button: 1, at: gamePoint(r.location(in: view)))
    }

    @objc private func hold(_ r: UILongPressGestureRecognizer) {
        switch r.state {
        case .began where popupOpen: break
        case .began: setCtrl(true)
        case .ended, .cancelled, .failed: setCtrl(false)
        default: break
        }
    }

    /// Vertical swipes and scrolls become wheel notches: 40 points per notch.
    /// Pulling down (content follows the finger) = wheel up = backlog.
    @objc private func swipe(_ r: UIPanGestureRecognizer) {
        guard !popupOpen, let engine else { return }
        if r.state == .began { wheelAccumulator = 0 }
        let dy = r.translation(in: view).y
        r.setTranslation(.zero, in: view)
        wheelAccumulator += dy
        while abs(wheelAccumulator) >= 40 {
            ore_mouse_wheel(engine, wheelAccumulator > 0 ? 120 : -120)
            wheelAccumulator -= wheelAccumulator > 0 ? 40 : -40
        }
    }

    @objc private func hover(_ r: UIHoverGestureRecognizer) {
        // Ended/cancelled hover events can report (0, 0) during a click.
        // Moving there before the release is polled cancels the pressed button.
        guard r.state == .began || r.state == .changed, !popupOpen, let engine else { return }
        let (x, y) = gamePoint(r.location(in: view))
        ore_mouse_move(engine, x, y)
    }

    // MARK: - mouse and trackpad: real presses

    private var pointerButton: Int32?

    private func pointerTouch(_ touches: Set<UITouch>) -> UITouch? {
        touches.first { $0.type == .indirectPointer }
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let t = pointerTouch(touches), pointerButton == nil, !popupOpen, let engine else {
            return super.touchesBegan(touches, with: event)
        }
        let button: Int32 = event?.buttonMask.contains(.secondary) == true ? 1 : 0
        let (x, y) = gamePoint(t.location(in: view))
        // A click on a word of the text is the popup's, and the game hears nothing of it.
        if button == 0, lookUp(engine, at: (x, y)) { return }
        ore_mouse_move(engine, x, y)
        ore_mouse_button(engine, button, 1)
        pointerButton = button
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let t = pointerTouch(touches), pointerButton != nil, let engine else {
            return super.touchesMoved(touches, with: event)
        }
        let (x, y) = gamePoint(t.location(in: view))
        ore_mouse_move(engine, x, y)
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let t = pointerTouch(touches), let button = pointerButton, let engine else {
            return super.touchesEnded(touches, with: event)
        }
        let (x, y) = gamePoint(t.location(in: view))
        ore_mouse_move(engine, x, y)
        ore_mouse_button(engine, button, 0)
        pointerButton = nil
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        touchesEnded(touches, with: event)
    }

    /// A tap is a press now and a release two frames later, so the game sees both.
    private func click(_ engine: OpaquePointer, button: Int32, at p: (Int32, Int32)) {
        ore_mouse_move(engine, p.0, p.1)
        ore_mouse_button(engine, button, 1)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) { [weak self] in
            guard let engine = self?.engine else { return }
            ore_mouse_button(engine, button, 0)
        }
    }

    private func setCtrl(_ down: Bool) {
        guard down != holdingCtrl, let engine else { return }
        holdingCtrl = down
        ore_key(engine, VK.control, down ? 1 : 0)
    }

    // MARK: - game controllers

    private func installControllers() {
        observers.append(NotificationCenter.default.addObserver(forName: .GCControllerDidConnect, object: nil,
                                                                queue: .main) { [weak self] note in
            if let c = note.object as? GCController { self?.attach(c) }
        })
        GCController.controllers().forEach(attach)
    }

    private func attach(_ controller: GCController) {
        guard let pad = controller.extendedGamepad else { return }
        pad.buttonA.pressedChangedHandler = { [weak self] _, _, pressed in self?.button(0, pressed) }
        pad.buttonB.pressedChangedHandler = { [weak self] _, _, pressed in self?.button(1, pressed) }
        pad.rightTrigger.pressedChangedHandler = { [weak self] _, _, pressed in self?.setCtrl(pressed) }
        pad.dpad.up.pressedChangedHandler = { [weak self] _, _, pressed in
            if pressed, let e = self?.engine { ore_mouse_wheel(e, 120) }
        }
        pad.dpad.down.pressedChangedHandler = { [weak self] _, _, pressed in
            if pressed, let e = self?.engine { ore_mouse_wheel(e, -120) }
        }
    }

    private func button(_ b: Int32, _ down: Bool) {
        if popupOpen {
            // B closes the popup, as it closes the game's own menus.
            if b == 1, down { popup?.close() }
            return
        }
        guard let engine else { return }
        ore_mouse_button(engine, b, down ? 1 : 0)
    }

    // MARK: - keyboard

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        if popupOpen {
            // Escape closes the popup; no other key is the game's while it is open.
            if presses.contains(where: { $0.key?.keyCode == .keyboardEscape }) { popup?.close() }
            return
        }
        var handled = false
        for press in presses {
            if let key = press.key, let vk = VK.from(key.keyCode), let engine {
                ore_key(engine, vk, 1)
                handled = true
            }
        }
        if !handled { super.pressesBegan(presses, with: event) }
    }

    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var handled = false
        for press in presses {
            if let key = press.key, let vk = VK.from(key.keyCode), let engine {
                ore_key(engine, vk, 0)
                handled = true
            }
        }
        if !handled { super.pressesEnded(presses, with: event) }
    }

    override func pressesCancelled(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        pressesEnded(presses, with: event)
    }
}

/// Script commands shared by the original button bar and native controls.
enum GameCommand: Int32 {
    case quickSave = 0, quickLoad = 1, save = 3, load = 4, history = 7, menu = 8
}

/// Windows virtual-key codes of the keys a player may press.
enum VK {
    static let control: Int32 = 0x11

    static func from(_ code: UIKeyboardHIDUsage) -> Int32? {
        switch code {
        case .keyboardReturnOrEnter, .keypadEnter: return 0x0D
        case .keyboardSpacebar: return 0x20
        case .keyboardEscape: return 0x1B
        case .keyboardTab: return 0x09
        case .keyboardDeleteOrBackspace: return 0x08
        case .keyboardLeftArrow: return 0x25
        case .keyboardUpArrow: return 0x26
        case .keyboardRightArrow: return 0x27
        case .keyboardDownArrow: return 0x28
        case .keyboardPageUp: return 0x21
        case .keyboardPageDown: return 0x22
        case .keyboardHome: return 0x24
        case .keyboardEnd: return 0x23
        case .keyboardLeftControl, .keyboardRightControl: return 0x11
        case .keyboardLeftShift, .keyboardRightShift: return 0x10
        case .keyboardLeftAlt, .keyboardRightAlt: return 0x12
        default: break
        }
        let raw = code.rawValue
        if raw >= UIKeyboardHIDUsage.keyboardA.rawValue && raw <= UIKeyboardHIDUsage.keyboardZ.rawValue {
            return Int32(0x41 + raw - UIKeyboardHIDUsage.keyboardA.rawValue)
        }
        if raw >= UIKeyboardHIDUsage.keyboard1.rawValue && raw <= UIKeyboardHIDUsage.keyboard9.rawValue {
            return Int32(0x31 + raw - UIKeyboardHIDUsage.keyboard1.rawValue)
        }
        if code == .keyboard0 { return 0x30 }
        if raw >= UIKeyboardHIDUsage.keyboardF1.rawValue && raw <= UIKeyboardHIDUsage.keyboardF12.rawValue {
            return Int32(0x70 + raw - UIKeyboardHIDUsage.keyboardF1.rawValue)
        }
        return nil
    }
}

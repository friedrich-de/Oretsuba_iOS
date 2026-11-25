// Keeping the reader's place (docs/AUTOSAVE.md): a silent save into a slot of
// its own every few lines and when the app leaves the screen, and a start that
// goes straight back to that line, past the logo and the title.
//
// Nothing here asks the script to save or load. A quick save of the game is one
// call of the save native while the script waits inside a line (FUNC_LIB
// L+1410), so the engine may write the same file itself at that moment. A load
// from the title ends with G[910] = 2 and MAIN does the rest (10_lifecycle §6.5),
// so the engine may answer MAIN's call of the title with that result.
#include <sys/stat.h>

#include "core/engine.h"
#include "mining/mining_tap.h"
#include "text/message.h"
#include "text/windows.h"
#include "util/log.h"

namespace ore {
namespace {

// Script variables of this game (USER_VAR.SOB), as the notes number them.
constexpr int kSceneName = 805;     // G[805]: name of the scene that runs
constexpr int kButtonStates = 818;  // G[818][0]: 0 while the game takes no quick save (a scene replayed from the gallery)
constexpr int kLoadedFlag = 855;    // G[855]: set by the load screen with a load; GameReset then clears the screen without a fade
constexpr int kTitleResult = 910;   // G[910]: what the title ended with; 2 = a game was loaded

// The least time between two saves, so that skipping does not write one a few times a second.
constexpr int64_t kLeastGapMs = 1000;

char upper(char c) {
	return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c;
}

bool same_name(const std::string &a, const std::string &b) {
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (upper(a[i]) != upper(b[i])) return false;
	return true;
}

}  // namespace

// The scene itself is the running script: no menu, history or settings screen lies above it.
bool Engine::autosave_in_scene() {
	if (vm.global_count() <= kTitleResult) return false;
	const auto &stack = vm.script_stack();
	if (stack.size() != 2) return false;
	uint32_t name = vm.global_data(kSceneName, 0);
	if (!name || !same_name(vm.mem().cstr(name), stack.back().name)) return false;
	return vm.mem().rd32(vm.global_addr(kButtonStates)) != 0;
}

void Engine::autosave_line_started(MessageWindow &window) {
	if (config.autosave_lines <= 0) return;
	autosave_window_ = &window;
	autosave_fresh_ = true;
	++autosave_count_;
	log_debug("autosave: line %d begun", window.mm.line);
}

void Engine::autosave_window_gone(MessageWindow &window) {
	if (autosave_window_ == &window) autosave_window_ = nullptr;
}

// After each step of the script: the line that step began is the moment the game
// itself would take a quick save, the script waits in the line's first frame.
void Engine::autosave_after_step() {
	if (!autosave_fresh_) return;
	autosave_fresh_ = false;
	if (autosave_count_ < config.autosave_lines) return;
	if (autosave_last_ >= 0 && now - autosave_last_ < kLeastGapMs) return;
	if (!autosave_in_scene()) return;
	autosave_write();
}

// When the app leaves the screen or the engine ends: the line the reader has in front of them.
void Engine::autosave_on_leaving() {
	if (config.autosave_lines <= 0 || !booted_ || autosave_count_ == 0) return;
	// States 0 to 2: the script still waits inside the line it began (3 = ended, 4 = skipped).
	if (!autosave_window_ || autosave_window_->state() > 2 || !autosave_in_scene()) return;
	autosave_write();
}

void Engine::autosave_write() {
	int r = write_save_file(kAutosaveBank, kAutosaveSlot);
	if (r != 0) {
		log_warn("autosave: not written (%d)", r);
		return;
	}
	// What the reader has read so far goes with it, as with a save of the game's own.
	write_gamedata();
	write_textlog();
	autosave_count_ = 0;
	autosave_last_ = now;
	log_debug("autosave: written in %s at line %d", vm.current_script()->name.c_str(),
	          autosave_window_ ? autosave_window_->mm.line : -1);
}

// MAIN calls the logo and then the title (10_lifecycle §2.9). With a place to go
// back to, the logo is left out and the title answered with "a game was loaded".
bool Engine::resume_instead_of(const std::string &script) {
	if (!resume_pending_) return false;
	if (vm.global_count() <= kTitleResult) return resume_pending_ = false;
	if (same_name(script, "Logo")) {
		struct stat st;
		if (stat(save_path(autosave_file_name()).c_str(), &st) == 0) return true;
		return resume_pending_ = false;
	}
	if (!same_name(script, "Title") && !same_name(script, "Title2")) return false;
	resume_pending_ = false;
	int r = load_game(kAutosaveBank, kAutosaveSlot);
	if (r != 0) {
		// None, or one that cannot be read: the title, as always.
		if (r > 0) log_warn("autosave: cannot be read (%d), going to the title", r);
		return false;
	}
	// What the load screen leaves behind after a load (saveload.sob @6c14).
	vm.mem().wr32(vm.global_addr(kLoadedFlag), 1);
	vm.mem().wr32(vm.global_addr(kTitleResult), 2);
	// The reader had that line when the save was written: it is not mined a second time.
	if (mining) mining->resumed();
	log_info("autosave: going back to the saved line");
	return true;
}

}  // namespace ore

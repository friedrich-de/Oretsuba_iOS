// Mining in the game: the part of the mining kit that looks at the engine.
#include "mining/kit_tap.h"
#if ORE_MINING_KIT

#include <cstring>

#include "core/engine.h"
#include "mining/mining.h"
#include "mining/mining_kit.h"
#include "text/message.h"
#include "text/windows.h"
#include "ui/ui_system.h"
#include "util/json.h"
#include "util/log.h"
#include "util/utf8.h"

namespace ore {

namespace {

// A tap this close to a character counts for it: a finger is not a mouse.
const int kNear = 6;

int distance(const TextGlyph &g, int x, int y) {
	int dx = x < g.x ? g.x - x : x >= g.x + g.w ? x - (g.x + g.w - 1) : 0;
	int dy = y < g.y ? g.y - y : y >= g.y + g.h ? y - (g.y + g.h - 1) : 0;
	return dx > dy ? dx : dy;
}

}  // namespace

void KitTap::line_shown(MessageWindow &window) {
	window_ = &window;
	const ScriptBlob *script = engine_.vm.current_script();
	shown_in_ = script ? script->name : std::string();
}

void KitTap::window_gone(MessageWindow &window) {
	if (window_ == &window) window_ = nullptr;
}

MessageWindow *KitTap::standing() const {
	MessageWindow *w = window_;
	if (!w || !w->visible() || !w->enabled()) return nullptr;
	// While the text still appears, a click completes it as ever; while it is skipped, nothing is read.
	int state = w->state();
	if (state != 2 && state != 3) return nullptr;
	// A menu or the history over the line runs a script of its own.
	const ScriptBlob *script = engine_.vm.current_script();
	if (!script || script->name != shown_in_) return nullptr;
	return w;
}

int KitTap::tap(int x, int y, std::string &json) {
	json.clear();
	if (!kit_.direct()) return 0;
	MessageWindow *w = standing();
	if (!w) return 0;
	int lx = x - w->x(), ly = y - w->y();
	if (lx < 0 || ly < 0 || lx >= w->width() || ly >= w->height()) return 0;
	// A button of the game that stands in the text box stays a button.
	if (engine_.ui)
		for (const auto &c : engine_.ui->controls())
			if (c->in_group && c->ui_type() >= 0 && c->ui_type() < 2 && c->visible() && c->hit_test(x, y)) return 0;

	const std::vector<TextGlyph> &glyphs = w->glyphs();
	int hit = -1, best = kNear + 1;
	for (size_t i = 0; i < glyphs.size(); ++i) {
		int d = distance(glyphs[i], lx, ly);
		if (d < best) {
			best = d;
			hit = int(i);
		}
	}
	if (hit < 0) return 2;

	json = describe(*w, capture(*w), hit);
	return 1;
}

bool KitTap::standing_text(std::string &json) {
	json.clear();
	MessageWindow *w = standing();
	if (!w) return false;
	json = describe(*w, 0, -1);
	return true;
}

std::string KitTap::describe(MessageWindow &window, int64_t line, int index) const {
	Platform &platform = engine_.platform;
	std::string text;
	json::Value rects = json::Value::array();
	for (const TextGlyph &g : window.glyphs()) {
		utf8::append(text, platform.decode_char(g.code, engine_.encoding));
		json::Value rect = json::Value::array();
		rect.push(window.x() + g.x);
		rect.push(window.y() + g.y);
		rect.push(g.w);
		rect.push(g.h);
		rects.push(std::move(rect));
	}
	const MessageManager &mm = window.mm;
	json::Value out = json::Value::object();
	out.set("line", line);
	out.set("text", text);
	out.set("index", index);
	out.set("rects", std::move(rects));
	out.set("sentence", mining_trim(platform.to_utf8(mining_plain_text(mm.body, engine_.encoding), engine_.encoding)));
	out.set("speaker", mining_trim(platform.to_utf8(mm.name_segment(1), engine_.encoding)));
	json::Value &box = out.set("box", json::Value::array());
	box.push(window.x());
	box.push(window.y());
	box.push(window.width());
	box.push(window.height());
	json::Value &screen = out.set("screen", json::Value::array());
	screen.push(Engine::kWidth);
	screen.push(Engine::kHeight);
	return json::write(out);
}

int64_t KitTap::capture(MessageWindow &window) {
	const MessageManager &mm = window.mm;
	std::string key = std::to_string(mm.type) + ":" + std::to_string(mm.line) + ":" + mm.name + ":" + mm.body;
	// The same line tapped again is the same line: its voice and picture are there.
	if (captured_id_ && key == captured_) return captured_id_;
	Platform &platform = engine_.platform;
	KitLine line;
	line.sentence = mining_trim(platform.to_utf8(mining_plain_text(mm.body, engine_.encoding), engine_.encoding));
	line.speaker = mining_trim(platform.to_utf8(mm.name_segment(1), engine_.encoding));
	std::string voice = mm.name_segment(2);
	if (!voice.empty()) {
		const std::string &ending = engine_.sound.voice_info().ext;
		std::vector<uint8_t> file;
		if (engine_.vfs.read("VOICE\\" + voice + "." + ending, file) && !file.empty()) {
			line.voice.assign(reinterpret_cast<const char *>(file.data()), file.size());
			for (char c : ending) line.voice_ending += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
		}
	}
	const Image &frame = engine_.framebuffer();
	std::vector<uint32_t> pixels(size_t(frame.width()) * size_t(frame.height()));
	for (int y = 0; y < frame.height(); ++y)
		memcpy(&pixels[size_t(y) * size_t(frame.width())], frame.row32(y), size_t(frame.width()) * 4);
	std::vector<uint8_t> picture = mining_jpeg(pixels.data(), frame.width(), frame.height(), 85);
	line.picture.assign(reinterpret_cast<const char *>(picture.data()), picture.size());
	log_debug("mining: line %d held for the popup, voice %s (%zu bytes), picture %zu bytes", mm.line,
	          voice.empty() ? "-" : voice.c_str(), line.voice.size(), line.picture.size());
	captured_ = std::move(key);
	captured_id_ = kit_.set_line(std::move(line));
	return captured_id_;
}

}  // namespace ore

#endif  // ORE_MINING_KIT

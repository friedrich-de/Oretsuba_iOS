// Mining (docs/MINING.md): the part that listens to the engine.
#include "mining/mining_tap.h"

#include <cstring>

#include "core/engine.h"
#include "text/message.h"
#include "text/windows.h"
#include "util/log.h"

namespace ore {

std::unique_ptr<MiningTap> MiningTap::open(Engine &engine) {
	MiningSettings settings;
	std::string problem;
	int found = load_mining_settings(engine.config.data_dir, settings, problem);
	if (found == 0) return nullptr;
	if (found < 0) {
		log_warn("mining: %s", problem.c_str());
		return nullptr;
	}
	HttpSend send = engine.platform.http_sender();
	if (!send) {
		log_warn("mining: this build cannot send lines");
		return nullptr;
	}
	if (settings.game.empty())
		settings.game = engine.config.chinese ? "\xE6\x88\x91\xE4\xBB\xAC\xE6\xB2\xA1\xE6\x9C\x89\xE7\xBF\x85\xE8\x86\x80"  // 我们没有翅膀
		                                      : "\xE4\xBF\xBA\xE3\x81\x9F\xE3\x81\xA1\xE3\x81\xAB\xE7\xBF\xBC\xE3\x81\xAF\xE3\x81\xAA\xE3\x81\x84";  // 俺たちに翼はない
	log_info("mining: lines go to %s", settings.server.c_str());
	return std::unique_ptr<MiningTap>(new MiningTap(engine, std::move(settings), std::move(send)));
}

MiningTap::MiningTap(Engine &engine, MiningSettings settings, HttpSend send)
    : engine_(engine), settings_(settings), queue_(std::move(settings), std::move(send)) {}

LineGate::Record MiningTap::record() const {
	const MessageManager &mm = window_->mm;
	LineGate::Record r;
	r.type = mm.type;
	r.line = mm.line;
	r.name = &mm.name;
	r.body = &mm.body;
	return r;
}

void MiningTap::line_shown(MessageWindow &window) {
	window_ = &window;
	gate_.shown(record(), engine_.now);
	log_debug("mining: line %d shown at %lld ms%s", window.mm.line, (long long)engine_.now, gate_.armed() ? "" : ", sent before");
}

void MiningTap::window_gone(MessageWindow &window) {
	if (window_ != &window) return;
	window_ = nullptr;
	gate_.reset();
}

void MiningTap::frame_drawn() {
	if (!window_ || !gate_.armed()) return;
	if (gate_.frame(window_->state(), record(), engine_.now)) send();
}

void MiningTap::leaving() {
	// Whoever leaves in the middle of a line most likely goes to look up a word of it.
	if (window_ && gate_.leaving(window_->state(), record())) send();
}

void MiningTap::send() {
	const MessageManager &mm = window_->mm;
	Platform &platform = engine_.platform;
	MiningLine line;
	line.text = mining_trim(platform.to_utf8(mining_plain_text(mm.body, engine_.encoding), engine_.encoding));
	if (line.text.empty()) return;
	line.name = mining_trim(platform.to_utf8(mm.name_segment(1), engine_.encoding));
	line.game = settings_.game;
	std::string voice = mm.name_segment(2);
	if (!voice.empty()) {
		line.voice = voice;
		if (settings_.voice) {
			const std::string &ext = engine_.sound.voice_info().ext;
			if (engine_.vfs.read("VOICE\\" + voice + "." + ext, line.audio) && !line.audio.empty()) {
				line.audio_name = voice + ".";
				for (char c : ext) line.audio_name += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
			} else {
				line.audio.clear();
			}
		}
	}
	if (settings_.screenshot) {
		const Image &frame = engine_.framebuffer();
		line.width = frame.width();
		line.height = frame.height();
		line.pixels.resize(size_t(line.width) * size_t(line.height));
		for (int y = 0; y < line.height; ++y)
			memcpy(&line.pixels[size_t(y) * size_t(line.width)], frame.row32(y), size_t(line.width) * 4);
	}
	log_debug("mining: line %d sent at %lld ms, voice %s (%zu bytes), screen %dx%d", mm.line, (long long)engine_.now,
	          line.voice.empty() ? "-" : line.voice.c_str(), line.audio.size(), line.width, line.height);
	queue_.push(std::move(line));
}

}  // namespace ore

// Headless test host: runs the engine without a window, feeding scripted
// input and writing PNG screenshots. It is built for macOS (Makefile), for
// Windows (Makefile.win), where it stands next to the original game, and for
// Linux (Makefile.linux), where no Mac is at hand.
//
//   orehost --data DIR [--save DIR] [--frames N]
//           [--input FILE] [--shot F,F,...] [--shot-every N] [--out DIR]
//           [--trace HEX,HEX,...] [--autosave LINES] [--resume]
//
//           [--kit FOLDER [--kit-resources DIR]] [--control]
//
// --autosave and --resume keep the reader's place as the app's switch does
// (docs/AUTOSAVE.md): a silent save every LINES lines, and a start at that save.
// --kit opens the mining kit (docs/MINING.md) with its settings and
// dictionaries in FOLDER; DIR is the folder with transforms.json
// (engine/resources/mining, the default when run from the repository).
//   orehost --font-report     (Windows only) what GDI answers for the game's two
//                             faces at 8 to 48 px, to check the port's metrics
//
// The input file holds one command per line, keyed by frame number:
//   <frame> click X Y        left click (press now, release 3 frames later)
//   <frame> rclick X Y
//   <frame> move X Y
//   <frame> wheel D           +120 up / -120 down
//   <frame> key VK [down|up]  (no state = press and release)
//   <frame> command N        native toolbar command (0 quick save, 1 quick load, ...)
//   <frame> pause N           the app goes to the background for N frames
//   <frame> shot NAME
//   <frame> quit
// Times are logical frames (60 per second); the clock advances 16.67 ms per
// frame regardless of real time, so runs are deterministic and fast.
//
// With --control the host takes its commands from standard input instead, one
// per line, and answers each with one line of JSON on standard output, so a
// test can drive the game and the mining kit together:
//   run N                    N frames
//   click X Y | rclick X Y   press, three frames, release
//   tap X Y                  a finger's tap as the app handles it: the kit is asked first
//                            ({"tap":0} the game got a click, {"tap":1,"popup":{...}} a word
//                            was hit and the game is held, {"tap":2} nothing happens)
//   hold 0|1                 the game stands still while the popup is open
//   move X Y | wheel D | key VK | command N
//   call METHOD [JSON]       a call to the kit; the answer is the kit's own
//   data METHOD [JSON]       a file of the kit's: {"ok":true,"type":"...","base64":"..."}
//   shot NAME                NAME.png in the --out folder
//   state                    the frame, the script, the hold
//   quit
// What the kit tells without having been asked comes as {"event":"...","payload":...}.
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "api/ore_kit_internal.h"
#include "core/engine.h"
#include "mining/kit_tap.h"
#include "mining/mining_tap.h"
#if ORE_MINING_KIT
#include "mining/anki_connect.h"
#endif
#if defined(_WIN32)
#include <direct.h>

#include "win/win_platform.h"
#elif defined(__APPLE__)
#include "platform/apple/apple_platform.h"
#else
#include "linux/linux_platform.h"
#endif
#include "stb_image_write.h"
#include "util/log.h"

namespace {

struct Cmd {
	std::string op;
	int a = 0, b = 0;
	std::string s;
};

#if defined(_WIN32)
using BasePlatform = ore::WinPlatform;
void make_dir(const std::string &path) { _mkdir(path.c_str()); }
#elif defined(__APPLE__)
using BasePlatform = ore::ApplePlatform;
void make_dir(const std::string &path) { mkdir(path.c_str(), 0755); }
#else
using BasePlatform = ore::LinuxPlatform;
void make_dir(const std::string &path) { mkdir(path.c_str(), 0755); }
#endif

class HostPlatform : public BasePlatform {
public:
	using BasePlatform::BasePlatform;
	int64_t clock = 0;
	int64_t now_ms() override { return clock; }
	void begin_dialog(const std::string &text, int kind) override {
		fprintf(stderr, "[dialog kind=%d] %s\n", kind, text.c_str());
	}
	int poll_dialog() override { return 1; }
};

void write_png(const ore::Image &img, const std::string &path) {
	std::vector<uint8_t> rgba(size_t(img.width()) * img.height() * 4);
	for (int y = 0; y < img.height(); ++y)
		for (int x = 0; x < img.width(); ++x) {
			uint32_t p = img.row32(y)[x];
			uint8_t *d = &rgba[(size_t(y) * img.width() + x) * 4];
			d[0] = uint8_t(p >> 16), d[1] = uint8_t(p >> 8), d[2] = uint8_t(p), d[3] = 255;
		}
	stbi_write_png(path.c_str(), img.width(), img.height(), 4, rgba.data(), img.width() * 4);
}

// One line of JSON for the test that drives the host; the kit's events come from other threads.
std::mutex g_out;
void answer_line(const std::string &line) {
	std::lock_guard<std::mutex> lock(g_out);
	fputs(line.c_str(), stdout);
	fputc('\n', stdout);
	fflush(stdout);
}

void kit_event(void *, const char *name, const char *payload) {
	answer_line(std::string("{\"event\":\"") + name + "\",\"payload\":" + (payload && *payload ? payload : "null") + "}");
}

}  // namespace

int main(int argc, char **argv) {
	std::string data, save = "/tmp/orehost_save", input, out = "/tmp/orehost";
	std::string trace;  // comma-separated hex native indices
	std::string kit_folder, kit_resources = "resources/mining";
	int frames = 600, shot_every = 0, autosave = 0;
	bool resume = false, control = false;
	std::set<int> shots;
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (a == "--data") data = next();
		else if (a == "--save") save = next();
		else if (a == "--frames") frames = atoi(next().c_str());
		else if (a == "--input") input = next();
		else if (a == "--out") out = next();
		else if (a == "--shot-every") shot_every = atoi(next().c_str());
		else if (a == "--shot") {
			std::stringstream ss(next());
			std::string t;
			while (std::getline(ss, t, ',')) shots.insert(atoi(t.c_str()));
		} else if (a == "--debug") ore::set_log_level(ore::LogLevel::Debug);
		else if (a == "--trace") trace = next();
		else if (a == "--autosave") autosave = atoi(next().c_str());
		else if (a == "--resume") resume = true;
		else if (a == "--kit") kit_folder = next();
		else if (a == "--kit-resources") kit_resources = next();
		else if (a == "--control") control = true;
#ifdef _WIN32
		else if (a == "--font-report") {
			// "ＭＳ ゴシック" and "ＭＳ 明朝" in the game's encoding
			const char *faces[] = {"\x82\x6c\x82\x72 \x83\x53\x83\x56\x83\x62\x83\x4e",
			                       "\x82\x6c\x82\x72 \x96\xbe\x92\xa9"};
			ore::WinPlatform p(".");
			for (const char *face : faces) fputs(p.font_report(face, 8, 48).c_str(), stdout);
			return 0;
		}
#endif
		else {
			fprintf(stderr, "unknown option %s\n", a.c_str());
			return 2;
		}
	}
	if (data.empty()) {
		fprintf(stderr, "usage: orehost --data DIR [...]\n");
		return 2;
	}
	std::multimap<int, Cmd> cmds;
	if (!input.empty()) {
		std::ifstream f(input);
		std::string line;
		while (std::getline(f, line)) {
			if (line.empty() || line[0] == '#') continue;
			std::stringstream ss(line);
			int frame;
			Cmd c;
			ss >> frame >> c.op;
			if (c.op == "key") {
				ss >> c.a >> c.s;
			} else if (c.op == "shot") {
				ss >> c.s;
			} else {
				ss >> c.a >> c.b;
			}
			cmds.emplace(frame, c);
		}
	}
	make_dir(out);
	HostPlatform platform(save);
	ore::EngineConfig cfg;
	cfg.data_dir = data;
	cfg.autosave_lines = autosave;
	cfg.resume = resume;
	ore::Engine engine(platform, cfg);
	std::string err;
	if (!engine.boot(&err)) {
		fprintf(stderr, "boot failed: %s\n", err.c_str());
		return 1;
	}
	{
		std::stringstream ss(trace);
		std::string t;
		while (std::getline(ss, t, ','))
			if (!t.empty()) engine.vm.trace_natives[size_t(strtol(t.c_str(), nullptr, 16)) & 1023] = true;
	}
	OreKit *kit = nullptr;
	if (!kit_folder.empty()) {
		char problem[512] = "";
		kit = ore_kit_open(kit_folder.c_str(), kit_resources.c_str(), problem, sizeof problem);
		if (!kit) {
			fprintf(stderr, "the mining kit does not open: %s\n", problem);
			return 1;
		}
#if ORE_MINING_KIT
		engine.set_kit(kit->kit.get());
#endif
	}
	std::vector<float> audio(2 * 800);
	std::multimap<int, Cmd> releases;
	int f = 0;
	auto fire = [&](const Cmd &c) {
		if (c.op == "click" || c.op == "rclick") {
			int b = c.op == "click" ? 0 : 1;
			engine.input.on_mouse_move(c.a, c.b);
			engine.mouse_button(b, true);
			Cmd r;
			r.op = "release";
			r.a = b;
			releases.emplace(f + 3, r);
		} else if (c.op == "release") {
			engine.mouse_button(c.a, false);
		} else if (c.op == "move") {
			engine.input.on_mouse_move(c.a, c.b);
		} else if (c.op == "wheel") {
			engine.input.on_wheel(c.a);
		} else if (c.op == "command") {
			const auto *script = engine.vm.current_script();
			fprintf(stderr, "frame %d: command %d in script %s\n", f, c.a, script ? script->name.c_str() : "-");
			engine.game_command(c.a);
		} else if (c.op == "key") {
			if (c.s == "down") engine.input.on_key(c.a, true);
			else if (c.s == "up") engine.input.on_key(c.a, false);
			else {
				engine.input.on_key(c.a, true);
				Cmd r;
				r.op = "keyup";
				r.a = c.a;
				releases.emplace(f + 3, r);
			}
		} else if (c.op == "keyup") {
			engine.input.on_key(c.a, false);
		} else if (c.op == "pause") {
			engine.set_paused(true);
			Cmd r;
			r.op = "resume";
			releases.emplace(f + (c.a > 0 ? c.a : 60), r);
		} else if (c.op == "resume") {
			engine.set_paused(false);
		} else if (c.op == "shot") {
			write_png(engine.framebuffer(), out + "/" + c.s + ".png");
		} else if (c.op == "quit") {
			return false;
		}
		return true;
	};
	if (control) {
		// Runs frames; what was pressed is let go three frames later. The engine's answer when it stops.
		auto run = [&](int count) {
			int r = 0;
			for (int i = 0; i < count && r == 0; ++i, ++f) {
				platform.clock = int64_t(f) * 1000 / 60;
				for (auto range = releases.equal_range(f); range.first != range.second; ++range.first) fire(range.first->second);
				releases.erase(f);
				r = engine.frame(platform.clock);
				engine.mixer.mix(audio.data(), 735);
				if (engine.mining && engine.mining->queue().stats().pending >= 8) engine.mining->queue().flush(30000);
			}
			return r;
		};
		auto done = [&](int r, const std::string &more = std::string()) {
			if (r != 0) fprintf(stderr, "frame %d: engine stopped (%d) %s\n", f, r, engine.error().c_str());
			answer_line(std::string("{\"ok\":") + (r == 0 ? "true" : "false") + ",\"frame\":" + std::to_string(f) + more + "}");
		};
		if (kit) ore_kit_set_events(kit, nullptr, kit_event);
		std::string line;
		while (std::getline(std::cin, line)) {
			std::stringstream ss(line);
			Cmd c;
			ss >> c.op;
			if (c.op.empty() || c.op[0] == '#') continue;
			if (c.op == "quit") break;
			if (c.op == "run") {
				int count = 1;
				ss >> count;
				done(run(count));
			} else if (c.op == "click" || c.op == "rclick") {
				ss >> c.a >> c.b;
				fire(c);
				done(run(5));
			} else if (c.op == "tap") {
				ss >> c.a >> c.b;
				int what = 0;
				std::string popup;
#if ORE_MINING_KIT
				if (engine.kit_tap) what = engine.kit_tap->tap(c.a, c.b, popup);
#endif
				int r = 0;
				if (what == 0) {
					c.op = "click";
					fire(c);
					r = run(5);
				} else if (what == 1) {
					engine.set_reading_hold(true);
				}
				done(r, ",\"tap\":" + std::to_string(what) + (what == 1 ? ",\"popup\":" + popup : std::string()));
			} else if (c.op == "hold") {
				ss >> c.a;
				engine.set_reading_hold(c.a != 0);
				done(0);
			} else if (c.op == "call" || c.op == "data") {
				std::string method, params;
				ss >> method;
				std::getline(ss, params);
				if (!kit) {
					answer_line("{\"ok\":false,\"error\":\"The host runs without --kit.\",\"kind\":\"internal\"}");
				} else if (c.op == "call") {
					char *text = ore_kit_call(kit, method.c_str(), params.c_str());
					answer_line(text ? text : "{\"ok\":false}");
					ore_kit_free(text);
				} else {
					uint8_t *bytes = nullptr;
					int64_t length = 0;
					char type[128] = "";
					std::string encoded;
					int found = ore_kit_data(kit, method.c_str(), params.c_str(), &bytes, &length, type, sizeof type);
#if ORE_MINING_KIT
					if (found) encoded = ore::base64(std::string(reinterpret_cast<const char *>(bytes), size_t(length)));
#endif
					ore_kit_free(bytes);
					answer_line(found ? std::string("{\"ok\":true,\"type\":\"") + type + "\",\"base64\":\"" + encoded + "\"}" : "{\"ok\":false}");
				}
			} else if (c.op == "state") {
				const ore::ScriptBlob *script = engine.vm.current_script();
				done(0, std::string(",\"script\":\"") + (script ? script->name : "") + "\",\"hold\":" + (engine.reading_hold() ? "true" : "false"));
			} else if (c.op == "shot") {
				ss >> c.s;
				fire(c);
				done(0);
			} else {
				if (c.op == "key") ss >> c.a >> c.s;
				else ss >> c.a >> c.b;
				fire(c);
				done(run(c.op == "key" && c.s.empty() ? 5 : 1));
			}
		}
		engine.set_kit(nullptr);
		if (kit) ore_kit_close(kit);
		return 0;
	}
	for (; f < frames; ++f) {
		platform.clock = int64_t(f) * 1000 / 60;
		bool go = true;
		for (auto range = cmds.equal_range(f); range.first != range.second; ++range.first)
			go = fire(range.first->second) && go;
		for (auto range = releases.equal_range(f); range.first != range.second; ++range.first) fire(range.first->second);
		if (!go) break;
		int r = engine.frame(platform.clock);
		engine.mixer.mix(audio.data(), 735);
		// The host's clock runs ahead of the real one: let a mining server keep up.
		if (engine.mining && engine.mining->queue().stats().pending >= 8) engine.mining->queue().flush(30000);
		if (shots.count(f) || (shot_every && f % shot_every == 0)) {
			char name[64];
			snprintf(name, sizeof name, "/f%05d.png", f);
			write_png(engine.framebuffer(), out + name);
		}
		if (r != 0) {
			fprintf(stderr, "frame %d: engine stopped (%d) %s\n", f, r, engine.error().c_str());
			break;
		}
	}
	write_png(engine.framebuffer(), out + "/last.png");
	if (engine.mining) {
		engine.mining->queue().flush(30000);
		ore::MiningQueue::Stats mined = engine.mining->queue().stats();
		fprintf(stderr, "mining: %d lines sent, %d dropped, %d left, last answer %d%s\n", mined.sent, mined.dropped,
		        mined.pending, mined.last_status, mined.refused ? ", key refused" : "");
	}
	const ore::ScriptBlob *s = engine.vm.current_script();
	fprintf(stderr, "ran %d frames, script %s at %s, steps %llu\n", f, s ? s->name.c_str() : "-",
	        engine.vm.where(engine.vm.pc()).c_str(), (unsigned long long)engine.vm.steps());
	engine.set_kit(nullptr);
	if (kit) ore_kit_close(kit);
	return 0;
}

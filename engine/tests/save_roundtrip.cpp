// Requires player-owned data, but uses only synthetic values in a fresh save directory.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <unistd.h>
#include "core/engine.h"
#include "platform/apple/apple_platform.h"
#include "text/message.h"
#include "text/text_system.h"

static bool check(bool ok, const char *message) {
	if (!ok) fprintf(stderr, "FAIL: %s\n", message);
	return ok;
}

int main(int argc, char **argv) {
	if (argc != 2) { fprintf(stderr, "usage: save_roundtrip DATA_DIR\n"); return 2; }
	char path[] = "/tmp/ore-save-test-XXXXXX";
	if (!mkdtemp(path)) return 1;
	bool ok = true;
	{
		ore::ApplePlatform platform(path);
		ore::EngineConfig config;
		config.data_dir = argv[1];
		ore::Engine engine(platform, config);
		std::string error;
		if (!engine.boot(&error)) { fprintf(stderr, "%s\n", error.c_str()); return 1; }
		uint32_t scene = 0, global = 0;
		for (const auto &g : engine.vm.globals()) {
			if (g.type != 0 || g.extra < 4) continue;
			if (g.flags == 1 && !scene) scene = g.addr[0];
			if (g.flags == 2 && !global) global = g.addr[0];
		}
		if (!check(scene && global, "fixture has saved and global variables")) return 1;
		engine.vm.mem().wr32(scene, 123456);
		engine.copy_vars(0, 2, 2, 0);
		ok &= check(engine.save_game(0, 0) == 0, "save synthetic scene");
		engine.vm.mem().wr32(scene, 0);
		ok &= check(engine.load_game(0, 0) == 0, "read save from filesystem");
		ok &= check(engine.vm.mem().rd32(scene) == 123456, "restore saved scene value");
		engine.gamedata_name = "GAMEDATA.DAT";
		engine.vm.mem().wr32(global, 654321);
		ok &= check(engine.write_gamedata() == 0, "write global data");
		engine.vm.mem().wr32(global, 0);
		ok &= check(engine.read_gamedata() == 0, "read global data from filesystem");
		ok &= check(engine.vm.mem().rd32(global) == 654321, "restore global value");
		engine.textlog_name = "TEXT_LOG.DAT";
		auto &history = engine.vm.init_table();
		size_t historyIndex = 0;
		while (historyIndex < history.size() && history[historyIndex].bits_a.empty()) ++historyIndex;
		if (!check(historyIndex < history.size(), "fixture has read-history bits")) return 1;
		history[historyIndex].bits_a[0] = 1;
		ok &= check(engine.write_textlog() == 0, "write read history");
		history[historyIndex].bits_a[0] = 0;
		ok &= check(engine.read_textlog() == 0, "read history from filesystem");
		ok &= check(history[historyIndex].bits_a[0] == 1, "restore read-history bit");

		// The backlog travels in the save, laid out as the original game lays it out
		// (docs/re/10_lifecycle_system.md §6.1), so that each reads the other's saves.
		ore::MessageManager &mm = engine.text->msg();
		engine.set_opt8(0xe5b, 1);
		mm.set_capacity(8);
		mm.log_enabled = true;
		auto push = [&mm](const char *name, const char *body, int line) {
			mm.name = name;
			mm.body = body;
			mm.type = 0;
			mm.line = uint16_t(line);
			mm.push_backlog();
		};
		push("Taka\\TAK0001", "one$", 10);
		push("", "two$", 11);
		if (ore::MessageManager::Entry *asked = mm.backlog_at(1)) {
			asked->has_choices = true;
			asked->choices = {"left", "right"};
			asked->selected = 1;
			asked->choice_type = 2;
			asked->choice_base = 7;
		}
		ok &= check(engine.save_game(0, 1) == 0, "save with a backlog");
		const unsigned char expected[] = {
			2, 0, 0, 0,                                                               // two entries
			13, 0, 0, 0, 'T', 'a', 'k', 'a', '\\', 'T', 'A', 'K', '0', '0', '0', '1', 0,  // name and voice, with the NUL
			5, 0, 0, 0, 'o', 'n', 'e', '$', 0,                                        // body, with the NUL
			0, 10, 0,                                                                 // type, line
			0, 0, 0, 0,                                                               // no choices
			0, 0, 0, 0,                                                               // no name
			5, 0, 0, 0, 't', 'w', 'o', '$', 0,
			0, 11, 0,
			15, 0, 0, 0, 7, 0, 'l', 'e', 'f', 't', 0, 8, 0, 'r', 'i', 'g', 'h', 't', 0,  // the choices as script records
			2, 0, 0, 0, 1, 2, 7, 0,                                                   // their count, the one taken, type, base
			0, 0, 0, 0,                                                               // no state of an earlier choice
		};
		std::ifstream in(std::string(path) + "/DATA0001.SAV", std::ios::binary);
		std::vector<unsigned char> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		ok &= check(file.size() > sizeof expected && std::equal(std::begin(expected), std::end(expected), file.end() - sizeof expected),
		            "the backlog ends the save in the original's layout");
		mm.set_capacity(8);
		ok &= check(engine.load_game(0, 1) == 0 && mm.used == 2, "read the backlog back");
		const ore::MessageManager::Entry *first = mm.backlog_at(0), *second = mm.backlog_at(1);
		ok &= check(first && second && first->name == "Taka\\TAK0001" && first->body == "one$" && first->line == 10 &&
		                !first->has_choices && second->name.empty() && second->body == "two$" && second->line == 11,
		            "restore the backlog's entries");
		ok &= check(second && second->has_choices && second->choices == std::vector<std::string>({"left", "right"}) &&
		                second->selected == 1 && second->choice_type == 2 && second->choice_base == 7,
		            "restore the choices of an entry");
	}
	std::filesystem::remove_all(path);
	if (ok) puts("save_roundtrip: scene, global data, read history and backlog passed");
	return ok ? 0 : 1;
}

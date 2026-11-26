// The small button that waits for a click, and the click region behind the
// text, as measured on the original:
//  - the animation shows its picture in the frame it is shown again, also
//    when that picture is the one it showed before it was hidden;
//  - a hide does not move its phase;
//  - an endless animation takes the end of each round as its new start, so
//    at 16 ms a frame six pictures of 100 ms take 38 frames: four of them
//    last 6 frames, the last two 7;
//  - N_2b4 with -1 as its fifth argument makes a region that is switched
//    off, otherwise the seventh argument switches it on.
// Needs no game files.
#include <cstdio>
#include <filesystem>
#include <memory>
#include <unistd.h>
#include <vector>

#include "core/engine.h"
#include "platform/apple/apple_platform.h"
#include "ui/controls.h"
#include "ui/frame_anim.h"
#include "ui/ui_system.h"

namespace ore { void register_ui_natives(Engine &); }

namespace {

const int kFrames = 6, kSide = 4, kX = 2, kY = 3;
const uint32_t kBack = 0xff102030;

uint32_t colour(int frame) { return 0xff000000u | uint32_t(frame + 1) * 0x202020u; }

}  // namespace

int main() {
	char saves[] = "/tmp/ore-wait-cursor-XXXXXX";
	if (!mkdtemp(saves)) return 2;
	bool ok = true;
	auto check = [&](bool value, const char *message) {
		if (!value) { fprintf(stderr, "FAIL: %s\n", message); ok = false; }
	};
	{
		ore::ApplePlatform platform(saves);
		ore::Engine engine(platform, {});

		// Six pictures side by side, drawn into a page at (kX, kY).
		auto strip = std::make_shared<ore::Image>(kFrames * kSide, kSide, 32);
		for (int f = 0; f < kFrames; ++f)
			for (int y = 0; y < kSide; ++y)
				for (int x = 0; x < kSide; ++x) strip->row32(y)[f * kSide + x] = colour(f);
		auto page = std::make_shared<ore::Image>(16, 16, 32);
		page->fill(kBack);
		ore::FrameAnim anim(engine);
		anim.image = strip;
		anim.frames = kFrames;
		anim.interval = 100;
		anim.mode = 0;
		anim.target = page;
		anim.x = kX;
		anim.y = kY;
		anim.end_mode = 2;  // hiding puts the background back
		anim.frame_w = kSide;
		anim.saved = std::make_shared<ore::Image>(kSide, kSide, 32);
		// The picture on the page: 0..5, or -1 for the background.
		auto shown = [&]() {
			uint32_t p = page->pixel(kX, kY);
			for (int f = 0; f < kFrames; ++f)
				if (p == colour(f)) return f;
			return -1;
		};

		// As the message window does it: the task is made paused, shown by
		// resuming it, hidden by restoring the background and pausing it.
		auto made = std::make_unique<ore::FrameAnimTask>(&anim, 0);
		ore::Task *task = made.get();
		engine.tasks.set_now(0);
		engine.tasks.add(std::move(made));
		engine.tasks.pause(task);
		int64_t now = 1000;
		auto tick = [&]() {
			engine.tasks.tick(now);
			now += 16;
			return shown();
		};
		auto show = [&]() {
			engine.tasks.set_now(now);
			engine.tasks.resume(task);
		};
		auto hide = [&]() {
			engine.tasks.set_now(now);
			anim.finish();
			engine.tasks.pause(task);
		};
		check(tick() == -1, "a paused animation draws nothing");

		show();
		std::vector<int> frames;
		const int rounds = 4;
		for (int i = 0; i < 39 + 38 * (rounds - 1); ++i) frames.push_back(tick());
		check(frames.front() == 0, "the first picture is on the page in the frame the animation is shown");
		// Lengths of the runs of one picture. The first round has one frame
		// more, the frame at its very start.
		std::vector<int> runs, order;
		for (size_t i = 0; i < frames.size(); ++i) {
			if (i && frames[i] == frames[i - 1]) {
				++runs.back();
			} else {
				runs.push_back(1);
				order.push_back(frames[i]);
			}
		}
		const int steady[kFrames] = {6, 6, 6, 6, 7, 7};
		bool as_measured = runs.size() == size_t(kFrames * rounds);
		for (size_t i = 0; as_measured && i < runs.size(); ++i) {
			int want = steady[i % kFrames] + (i == 0 ? 1 : 0);
			if (runs[i] != want || order[i] != int(i % kFrames)) as_measured = false;
		}
		if (!as_measured) {
			fprintf(stderr, "runs:");
			for (size_t i = 0; i < runs.size(); ++i) fprintf(stderr, " %d:%d", order[i], runs[i]);
			fprintf(stderr, "\n");
		}
		check(as_measured, "a round of six pictures takes 38 frames: 6, 6, 6, 6, 7, 7");

		// The last tick showed picture 5 for the 7th time, so a new round
		// starts now: picture 0 for 6 frames, then picture 1 for 6.
		for (int i = 0; i < 6; ++i) check(tick() == 0, "a new round starts with the first picture");
		check(tick() == 1, "the second picture follows after 6 frames");
		// Hidden for one frame, as by a click on a line that is complete in the next frame.
		hide();
		check(tick() == -1, "hiding puts the background back");
		show();
		check(tick() == 1, "shown again, the animation draws at once, also the picture it showed before");
		for (int i = 0; i < 3; ++i) check(tick() == 1, "the picture stays for the rest of its time");
		check(tick() == 2, "the hide did not move the animation's phase");

		// The click region.
		ore::register_ui_natives(engine);
		engine.ui = std::make_unique<ore::UiSystem>(engine);
		auto region = [&](const std::vector<int32_t> &values) {
			uint32_t args = engine.vm.mem().alloc(uint32_t(values.size() * 4), "region arguments");
			for (size_t i = 0; i < values.size(); ++i) engine.vm.mem().wr32(args + uint32_t(i * 4), uint32_t(values[i]));
			return ore::Object::as<ore::HitRegion>(uint32_t(engine.vm.native(0x2b4)(engine.vm, args)));
		};
		ore::HitRegion *screen = region({0, 0, 800, 600, -1});
		check(screen && !screen->hit_test(400, 300), "the region made with -1 is switched off and takes no click");
		ore::HitRegion *entry = region({119, 50, 634, 113, 436, 106, 1, 1});
		check(entry && entry->hit_test(200, 100) && !entry->hit_test(100, 100), "a region made switched on takes the pointer inside it");
		ore::HitRegion *off = region({119, 50, 634, 113, 436, 106, 0, 1});
		check(off && !off->hit_test(200, 100), "a region made switched off does not");
	}
	std::filesystem::remove_all(saves);
	if (ok) puts("wait_cursor: the wait button's round, its return after a hide and the click region are as in the original");
	return ok ? 0 : 1;
}

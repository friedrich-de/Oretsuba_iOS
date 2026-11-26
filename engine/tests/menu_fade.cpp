// Closing a menu must not show the menu again after its fade is over.
// Opens the load screen from the title, closes it with the right button and
// follows every frame. Requires player-owned archives.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>
#include "core/engine.h"
#include "platform/apple/apple_platform.h"

namespace {

// The engine's clock advances with the frames, not with the wall clock.
struct Clock : ore::ApplePlatform {
    explicit Clock(const std::string &saves) : ore::ApplePlatform(saves) {}
    int64_t clock = 0;
    int64_t now_ms() override { return clock; }
};

std::vector<uint32_t> picture(const ore::Engine &engine) {
    const ore::Image &f = engine.framebuffer();
    std::vector<uint32_t> out(size_t(f.width()) * size_t(f.height()));
    for (int y = 0; y < f.height(); ++y) memcpy(&out[size_t(y) * size_t(f.width())], f.row32(y), size_t(f.width()) * 4);
    return out;
}

// Share of pixels (in thousandths) whose colour differs.
int differing(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
    size_t n = 0;
    for (size_t i = 0; i < a.size(); ++i) n += ((a[i] ^ b[i]) & 0xffffff) != 0;
    return int(n * 1000 / a.size());
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    char saves[] = "/tmp/ore-menu-fade-XXXXXX";
    if (!mkdtemp(saves)) return 2;
    bool ok = true;
    {
        Clock platform(saves);
        ore::EngineConfig config;
        config.data_dir = argv[1];
        ore::Engine engine(platform, config);
        std::string error;
        if (!engine.boot(&error)) { fprintf(stderr, "%s\n", error.c_str()); return 1; }
        auto check = [&](bool value, const char *message) {
            if (!value) { fprintf(stderr, "FAIL: %s\n", message); ok = false; }
        };
        int frame = 0;
        // 44100 samples at 60 frames a second, so sound and frames advance alike.
        std::vector<float> audio(2 * 735);
        auto step = [&]() {
            platform.clock = int64_t(frame++) * 1000 / 60;
            bool running = engine.frame(platform.clock) == 0;
            engine.mixer.mix(audio.data(), 735);
            return running;
        };
        auto press = [&](int button, int x, int y) {
            engine.input.on_mouse_move(x, y);
            engine.mouse_button(button, true);
            bool running = step() && step() && step();
            engine.mouse_button(button, false);
            return running;
        };

        // Logo, then the title with its four entries.
        while (frame < 700) check(step(), "the game reaches its title");
        const std::vector<uint32_t> title = picture(engine);

        // LOAD opens the load screen, which covers the whole title.
        check(press(0, 730, 423), "LOAD is pressed");
        while (frame < 880) check(step(), "the load screen opens");
        check(differing(picture(engine), title) > 950, "the load screen is shown");

        // The right button closes it: the screen fades to the title.
        engine.input.on_mouse_move(400, 300);
        engine.mouse_button(1, true);
        check(step(), "the right button is pressed");
        const std::vector<uint32_t> menu = picture(engine);  // first frame of the fade
        int left = -1, back = -1;
        for (int i = 1; i <= 90; ++i) {
            if (i == 3) engine.mouse_button(1, false);
            check(step(), "the load screen closes");
            int share = differing(picture(engine), menu);
            if (getenv("MENU_FADE_TRACE")) fprintf(stderr, "frame +%d: %d\n", i, share);
            if (left < 0 && share > 900) left = i;
            if (left >= 0 && back < 0 && share < 100) back = i;
        }
        check(left > 0, "the load screen fades away");
        if (back >= 0) fprintf(stderr, "the menu is on screen again %d frames after the fade began\n", back);
        check(back < 0, "the menu does not show again once its fade is over");
    }
    if (ok) printf("menu_fade: a closed menu stays closed\n");
    return ok ? 0 : 1;
}

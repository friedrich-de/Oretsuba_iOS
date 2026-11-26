// A menu fades in over the game's picture and fades away with all its
// controls, and it must not show again after its fade is over. Opens the load
// screen from the title, closes it with the right button and follows every
// frame. Requires player-owned archives.
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

// Every fourth pixel of every fourth row: enough to follow a fade, small enough to keep per frame.
std::vector<uint32_t> sample(const std::vector<uint32_t> &p, int width) {
    std::vector<uint32_t> out;
    int height = int(p.size()) / width;
    for (int y = 0; y < height; y += 4)
        for (int x = 0; x < width; x += 4) out.push_back(p[size_t(y) * size_t(width) + size_t(x)]);
    return out;
}

// Mean difference of the colour channels, in thousandths of the full range.
int distance(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
    uint64_t sum = 0;
    for (size_t i = 0; i < a.size(); ++i)
        for (int shift = 0; shift < 24; shift += 8)
            sum += uint64_t(abs(int((a[i] >> shift) & 0xff) - int((b[i] >> shift) & 0xff)));
    return int(sum * 1000 / (a.size() * 3 * 255));
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
        const int width = engine.framebuffer().width();
        check(press(0, 730, 423), "LOAD is pressed");
        std::vector<std::vector<uint32_t>> opening;
        while (frame < 880) {
            check(step(), "the load screen opens");
            opening.push_back(sample(picture(engine), width));
        }
        const std::vector<uint32_t> screen = picture(engine);
        check(differing(screen, title) > 950, "the load screen is shown");

        // It fades in over the title's picture (300 ms, 18 frames); it does not
        // appear at once. Frames between a tenth and nine tenths of the way
        // belong to the fade, and each of them is nearer to the screen than
        // the one before.
        const int far = distance(opening.front(), opening.back());
        int fading = 0, before = far;
        bool nearer = true;
        for (const auto &p : opening) {
            int d = distance(p, opening.back());
            if (getenv("MENU_FADE_TRACE")) fprintf(stderr, "opening: %d of %d\n", d, far);
            if (d <= far / 10 || d >= far * 9 / 10) continue;
            ++fading;
            if (d > before) nearer = false;
            before = d;
        }
        if (fading < 10) fprintf(stderr, "the load screen took %d frames between a tenth and nine tenths\n", fading);
        check(fading >= 10, "the load screen fades in");
        check(nearer, "the fade goes one way");

        // The right button closes it: the screen fades to the title.
        engine.input.on_mouse_move(400, 300);
        engine.mouse_button(1, true);
        check(step(), "the right button is pressed");
        const std::vector<uint32_t> menu = picture(engine);  // first frame of the fade
        // The fade starts from the whole screen: its slots and buttons do not vanish first.
        check(differing(menu, screen) < 20, "the load screen fades away with all its controls");
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
    if (ok) printf("menu_fade: the load screen fades in, fades away whole and stays closed\n");
    return ok ? 0 : 1;
}

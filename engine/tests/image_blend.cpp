// N_16a composes text and labels into transparent menu images.
#include <cstdio>
#include <filesystem>
#include <unistd.h>

#include "core/engine.h"
#include "gfx/blend.h"
#include "gfx/surface.h"
#include "platform/apple/apple_platform.h"

namespace ore { void register_image_op_natives(Engine &); }

int main() {
	char saves[] = "/tmp/ore-image-blend-XXXXXX";
	if (!mkdtemp(saves)) return 2;
	ore::ApplePlatform platform(saves);
	ore::Engine engine(platform, {});
	ore::register_image_op_natives(engine);
	auto dst = std::make_shared<ore::Image>(4, 1, 32);
	auto src = std::make_shared<ore::Image>(4, 1, 32);
	dst->fill(0);
	src->row32(0)[0] = 0xffffffff;
	src->row32(0)[1] = 0x80ffffff;
	src->row32(0)[2] = 0x00000000;
	src->row32(0)[3] = 0x80ff0000;
	dst->row32(0)[2] = 0x80445566;
	dst->row32(0)[3] = 0x800000ff;
	uint32_t dh = engine.own_image(std::make_unique<ore::ImageObj>(dst))->handle();
	uint32_t sh = engine.own_image(std::make_unique<ore::ImageObj>(src))->handle();
	uint32_t args = engine.vm.mem().alloc(16, "blend arguments");
	engine.vm.mem().wr32(args, dh);
	engine.vm.mem().wr32(args + 4, sh);
	engine.vm.mem().wr32(args + 8, 255);
	engine.vm.mem().wr32(args + 12, 0xffff);
	bool ok = true;
	auto check = [&](bool value, const char *message) {
		if (!value) { fprintf(stderr, "FAIL: %s\n", message); ok = false; }
	};
	check(engine.vm.native(0x16a)(engine.vm, args) == 0, "menu composition succeeds");
	check(dst->pixel(0, 0) == 0xffffffff, "opaque text remains visible on a transparent panel");
	check(dst->pixel(1, 0) == 0x80ffffff, "antialiasing retains straight colour and coverage");
	check(dst->pixel(2, 0) == 0x80445566, "transparent source leaves the panel unchanged");
	uint32_t p = dst->pixel(3, 0);
	check((p >> 24) >= 191 && (p >> 24) <= 192, "overlapping coverage accumulates");
	check(((p >> 16) & 255) >= 169 && (p & 255) >= 84, "overlap combines both colours");
	dst->fill(0);
	engine.vm.mem().wr32(args + 8, 128);
	engine.vm.native(0x16a)(engine.vm, args);
	check(dst->pixel(0, 0) == 0x80ffffff, "extra opacity scales coverage without darkening straight colour");
	ore::Image screen(1, 1, 32), glyph(1, 1, 32);
	screen.fill(0xff000000); glyph.fill(0x80ffffff);
	ore::blit(screen, glyph, 0, 0, glyph.rect(), ore::kBlendAlpha, 255, false);
	check(screen.pixel(0, 0) == 0x007f7f7f, "ordinary sprite blend keeps its existing arithmetic");
	// Full opacity, source alpha: opaque pixels are copied pair by pair, the rest is mixed.
	{
		ore::Image row(6, 2, 32), ink(4, 1, 32);
		auto draw = [&](int x, int y) {
			row.fill(0xff000000);
			ore::blit(row, ink, x, y, ink.rect(), ore::kBlendAlpha, 255, false);
		};
		ink.row32(0)[0] = ink.row32(0)[1] = ink.row32(0)[2] = 0xffffffff;
		ink.row32(0)[3] = 0x80ffffff;
		draw(0, 0);
		check(row.pixel(0, 0) == 0x00ffffff && row.pixel(1, 0) == 0x00ffffff, "an opaque pair is copied, white stays 255");
		check(row.pixel(2, 0) == 0x00fefefe && row.pixel(3, 0) == 0x007f7f7f,
		      "an opaque pixel beside a soft one is mixed with it");
		draw(1, 0);
		check(row.pixel(1, 0) == 0x00ffffff, "an opaque pixel alone before the first pair is copied");
		check(row.pixel(2, 0) == 0x00ffffff && row.pixel(3, 0) == 0x00ffffff && row.pixel(4, 0) == 0x007f7f7f,
		      "the pairs follow the destination, not the source");
		ore::Image last(3, 1, 32);
		last.fill(0xffffffff);
		row.fill(0xff000000);
		ore::blit(row, last, 0, 0, last.rect(), ore::kBlendAlpha, 255, false);
		check(row.pixel(2, 0) == 0x00ffffff, "an opaque pixel alone after the last pair is copied");
		ore::Image odd(5, 2, 32);  // rows without padding: in an odd width the pairs shift by one on every other row
		odd.fill(0xff000000);
		ink.row32(0)[0] = 0x80ffffff;
		ink.row32(0)[1] = ink.row32(0)[2] = ink.row32(0)[3] = 0xffffffff;
		ore::blit(odd, ink, 0, 1, ink.rect(), ore::kBlendAlpha, 255, false);
		check(odd.pixel(0, 1) == 0x007f7f7f && odd.pixel(1, 1) == 0x00ffffff && odd.pixel(2, 1) == 0x00ffffff &&
		      odd.pixel(3, 1) == 0x00ffffff,
		      "in a picture of odd width the second row pairs from its second pixel");
	}
	// The small picture of a save: 8:1, a tent of 15 pixels, rows first, each pass rounded.
	{
		ore::Image big(32, 24, 32), small(4, 3, 32);
		big.fill(0xff000000);
		big.row32(16)[16] = 0xffffffff;  // on a centre: (8*255+32)>>6 = 32, then (8*32+32)>>6 = 4
		check(ore::reduce_tent(small, big), "a whole multiple is reduced");
		check(small.pixel(2, 2) == 0xff040404 && small.pixel(1, 2) == 0xff000000 && small.pixel(3, 2) == 0xff000000,
		      "a point on a centre stays in its own pixel");
		big.fill(0xff000000);
		big.row32(16)[17] = 0xff0000ff;  // one to the right: weight 7 here, weight 1 for the next pixel
		ore::reduce_tent(small, big);
		check(small.pixel(2, 2) == 0xff000004 && small.pixel(3, 2) == 0xff000001,
		      "a point between two centres is shared, the channels apart");
		big.fill(0xff000000);
		big.row32(0)[3] = 0xffffffff;  // the left and upper edges mirror: 2*5 across, 8 down counted once
		ore::reduce_tent(small, big);
		check(small.pixel(0, 0) == 0xff050505, "the pixels beyond the left edge are the mirrored ones");
		big.fill(0xff336699);
		ore::reduce_tent(small, big);
		check(small.pixel(0, 0) == 0xff336699 && small.pixel(3, 2) == 0xff336699, "an even picture stays as it is");
		ore::Image odd(33, 24, 32);
		check(!ore::reduce_tent(small, odd), "a size that is no whole multiple is refused");
	}
	if (ok) puts("image_blend: transparent menu text and sprite blending passed");
	std::filesystem::remove_all(saves);
	return ok ? 0 : 1;
}

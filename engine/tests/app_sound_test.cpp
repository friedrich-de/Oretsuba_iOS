// The app's own sound over the game's (audio/app_sound.h): the word the
// dictionary popup plays goes through the game's mixer, beside what the game
// plays, and one replaces the one before. Needs no game files:
//
//   c++ -std=c++17 -Isrc -Ithird_party tests/app_sound_test.cpp src/audio/app_sound.cpp src/audio/mixer.cpp src/third_party_impl/stb_vorbis_impl.c -lpthread
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "audio/app_sound.h"
#include "audio/mixer.h"

using namespace ore;

namespace {

bool ok = true;
int checks = 0;

void check(bool value, const char *message) {
	++checks;
	if (!value) {
		fprintf(stderr, "FAIL: %s\n", message);
		ok = false;
	}
}

// A tone of `frames` frames at `rate`, `channels` (1 or 2) samples a frame; the right channel is half as loud.
std::vector<int16_t> tone(size_t frames, int channels, int rate, double hz = 440.0) {
	std::vector<int16_t> out(frames * size_t(channels));
	for (size_t i = 0; i < frames; ++i) {
		double v = std::sin(2.0 * M_PI * hz * double(i) / rate) * 16000.0;
		out[i * channels] = int16_t(v);
		if (channels == 2) out[i * 2 + 1] = int16_t(v / 2);
	}
	return out;
}

// Mixes `frames` output frames and gives the loudest sample of each side.
void loudest(AudioMixer &mixer, int frames, float &left, float &right) {
	std::vector<float> out(size_t(frames) * 2);
	mixer.mix(out.data(), frames);
	left = right = 0;
	for (int i = 0; i < frames; ++i) {
		left = std::max(left, std::fabs(out[i * 2]));
		right = std::max(right, std::fabs(out[i * 2 + 1]));
	}
}

void test_buffer() {
	std::vector<int16_t> mono = tone(100, 1, 22050);
	std::shared_ptr<PcmBuffer> pcm = pcm_of(mono.data(), 100, 1, 22050);
	check(pcm && pcm->frames() == 100 && pcm->rate == 22050, "one channel becomes a stereo buffer of as many frames");
	bool same = pcm != nullptr;
	for (size_t i = 0; same && i < 100; ++i) same = pcm->samples[i * 2] == mono[i] && pcm->samples[i * 2 + 1] == mono[i];
	check(same, "heard the same on both sides");

	std::vector<int16_t> stereo = tone(50, 2, 48000);
	pcm = pcm_of(stereo.data(), 50, 2, 48000);
	check(pcm && pcm->samples == stereo, "two channels stay as they are");

	check(!pcm_of(mono.data(), 0, 1, 22050), "no frames are no sound");
	check(!pcm_of(nullptr, 10, 1, 22050), "nor are no samples");
	check(!pcm_of(mono.data(), 10, 3, 22050), "three channels are refused");
	check(!pcm_of(mono.data(), 10, 1, 4000) && !pcm_of(mono.data(), 10, 1, 400000), "and rates no sound has");
}

void test_playing() {
	AudioMixer mixer;
	AppSound sound(mixer);
	float left = 0, right = 0;

	// The game's own sound, quiet and on the left only.
	std::vector<int16_t> game(44100 * 4);
	for (size_t i = 0; i < game.size() / 2; ++i) game[i * 2] = int16_t(std::sin(i * 0.05) * 3000);
	auto game_pcm = std::make_shared<PcmBuffer>();
	game_pcm->samples = game;
	AudioMixer::PlayParams loop;
	loop.loop = true;
	int game_voice = mixer.play_pcm(game_pcm, loop);
	loudest(mixer, 512, left, right);
	check(left > 0.05f && left < 0.1f && right == 0.0f, "the game sounds on its own");

	// A word at 22,050 Hz on one channel: heard on both sides over the game.
	std::vector<int16_t> word = tone(22050 / 2, 1, 22050);
	check(sound.play(word.data(), word.size(), 1, 22050), "a word's sound plays");
	check(sound.playing(), "and is playing");
	loudest(mixer, 2048, left, right);
	check(right > 0.4f && left > 0.4f, "the word is heard on both sides");
	check(mixer.playing(game_voice), "and the game's sound goes on beside it");

	// Half a second at 22,050 Hz is 22,050 output frames at 44,100 Hz: the mixer converts the rate.
	for (int i = 0; i < 9; ++i) loudest(mixer, 2048, left, right);
	check(sound.playing(), "it is still playing before its end");
	loudest(mixer, 2048, left, right);
	loudest(mixer, 512, left, right);
	check(!sound.playing(), "and has ended after half a second");
	check(right == 0.0f && left > 0.0f, "then the game is heard alone again");

	// A second word replaces the first; nothing stops the game.
	std::vector<int16_t> long_word = tone(44100 * 2, 2, 44100);
	check(sound.play(long_word.data(), 44100 * 2, 2, 44100), "a second word plays");
	std::vector<int16_t> next = tone(44100, 1, 44100, 880.0);
	check(sound.play(next.data(), 44100, 1, 44100), "a third word plays in its place");
	loudest(mixer, 1024, left, right);
	// The second alone would be half as loud on the right, both together louder than either.
	check(right > 0.4f && right < 0.55f, "only the third is heard");

	sound.stop();
	loudest(mixer, 512, left, right);
	check(!sound.playing() && right == 0.0f && mixer.playing(game_voice), "stopping it leaves the game's sound");

	check(!sound.play(word.data(), 0, 1, 22050) && !sound.playing(), "a sound without frames plays nothing");
	check(sound.play(word.data(), word.size(), 1, 22050), "a word plays again");
	check(!sound.play(word.data(), word.size(), 5, 22050) && !sound.playing(), "a sound that is none stops the one before");
}

}  // namespace

int main() {
	test_buffer();
	test_playing();
	if (!ok) return 1;
	printf("app_sound_test: %d checks passed\n", checks);
	return 0;
}

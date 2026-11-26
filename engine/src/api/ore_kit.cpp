// C interface of the mining kit: the part that needs no engine. The three
// functions that take one stand with the engine's own (ore_api.mm).
#include <cstdlib>
#include <cstring>
#include <string>

#include "api/ore_kit_internal.h"

namespace {

void say(char *err, int err_len, const std::string &text) {
	if (!err || err_len <= 0) return;
	strncpy(err, text.c_str(), size_t(err_len) - 1);
	err[err_len - 1] = 0;
}

}  // namespace

#if ORE_MINING_KIT

#include "mining/kit_host.h"

namespace {

char *given(const std::string &text) {
	char *out = static_cast<char *>(malloc(text.size() + 1));
	if (!out) return nullptr;
	memcpy(out, text.data(), text.size());
	out[text.size()] = 0;
	return out;
}

}  // namespace

extern "C" {

OreKit *ore_kit_open(const char *folder, const char *resources, char *err, int err_len) {
	ore::MiningKit::Options options;
	options.folder = folder ? folder : "";
	options.resources = resources ? resources : "";
	options.fetch = ore::host_http_fetcher();
	options.encode_voice = ore::host_voice_encoder();
	std::string problem;
	std::unique_ptr<ore::MiningKit> kit = ore::MiningKit::open(std::move(options), problem);
	if (!kit) {
		say(err, err_len, problem);
		return nullptr;
	}
	OreKit *out = new OreKit;
	out->kit = std::move(kit);
	return out;
}

void ore_kit_close(OreKit *kit) {
	delete kit;
}

char *ore_kit_call(OreKit *kit, const char *method, const char *params_json) {
	return given(kit->kit->call(method ? method : "", params_json ? params_json : ""));
}

int ore_kit_data(OreKit *kit, const char *method, const char *params_json, uint8_t **bytes, int64_t *length, char *type,
                 int type_len) {
	*bytes = nullptr;
	*length = 0;
	std::string data, media_type;
	if (!kit->kit->data(method ? method : "", params_json ? params_json : "", data, media_type)) return 0;
	uint8_t *out = static_cast<uint8_t *>(malloc(data.size() ? data.size() : 1));
	if (!out) return 0;
	memcpy(out, data.data(), data.size());
	*bytes = out;
	*length = int64_t(data.size());
	say(type, type_len, media_type);
	return 1;
}

void ore_kit_free(void *given) {
	free(given);
}

int ore_kit_direct(OreKit *kit) {
	return kit->kit->direct() ? 1 : 0;
}

void ore_kit_set_events(OreKit *kit, void *ctx, OreKitEventCallback callback) {
	if (!callback) {
		kit->kit->set_events(nullptr);
		return;
	}
	kit->kit->set_events([ctx, callback](const std::string &name, const std::string &payload) { callback(ctx, name.c_str(), payload.c_str()); });
}

}  // extern "C"

#else  // a build without the kit: it cannot be opened

extern "C" {

OreKit *ore_kit_open(const char *, const char *, char *err, int err_len) {
	say(err, err_len, "Mining in the game is not part of this build.");
	return nullptr;
}
void ore_kit_close(OreKit *) {}
char *ore_kit_call(OreKit *, const char *, const char *) {
	return nullptr;
}
int ore_kit_data(OreKit *, const char *, const char *, uint8_t **bytes, int64_t *length, char *, int) {
	*bytes = nullptr;
	*length = 0;
	return 0;
}
void ore_kit_free(void *given) {
	free(given);
}
int ore_kit_direct(OreKit *) {
	return 0;
}
void ore_kit_set_events(OreKit *, void *, OreKitEventCallback) {}

}  // extern "C"

#endif

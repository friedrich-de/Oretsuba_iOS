// What the mining kit needs of the system it runs on. Every host that builds
// the kit in defines these three once, and the kit is opened with them
// (api/ore_kit.cpp): Apple devices in platform/apple/apple_kit_host.mm, the
// Linux test host in host/linux/linux_kit_host.cpp, Android in its own code.
#pragma once

#include "dict/kit_config.h"
#if ORE_MINING_KIT

#include "mining/anki_app.h"
#include "mining/http_fetch.h"
#include "mining/mining_kit.h"

namespace ore {

// HTTP as mining/http_fetch.h describes it. An empty function when the host
// has none: then the kit reaches nothing.
HttpFetch host_http_fetcher();

// Makes a line's voice a file every Anki plays. An empty function when the
// host cannot: then the voice goes on the card as the game has it.
VoiceEncoder host_voice_encoder();

// The Anki app on the same device (mining/anki_app.h): links and the
// clipboard for AnkiMobile, requests for AnkiDroid. Empty functions for what
// the host has not: then the kit does not offer that app.
AnkiAppHost host_anki_app();

}  // namespace ore

#endif  // ORE_MINING_KIT

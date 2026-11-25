// What stands behind an OreKit, for the files that carry the C interface.
#pragma once

#include <memory>

#include "api/ore_kit.h"
#include "dict/kit_config.h"

#if ORE_MINING_KIT
#include "mining/mining_kit.h"

struct OreKit {
	std::unique_ptr<ore::MiningKit> kit;
};

#else

struct OreKit {};

#endif

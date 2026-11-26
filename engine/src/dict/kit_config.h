// Whether this build holds the mining kit's dictionary, which needs the
// system's SQLite and zlib. The Apple app and the Linux test host have them;
// the Windows test host is built without (-DORE_MINING_KIT=0 does the same).
#pragma once

#if !defined(ORE_MINING_KIT)
#if defined(_WIN32)
#define ORE_MINING_KIT 0
#else
#define ORE_MINING_KIT 1
#endif
#endif

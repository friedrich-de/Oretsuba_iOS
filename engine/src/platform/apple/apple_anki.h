// The Anki app beside this one on an iPhone or iPad: AnkiMobile
// (mining/anki_app.h). On a Mac's test host there is none.
#pragma once

#include "mining/anki_app.h"

namespace ore {

// Opens links in other apps, keeping this app running while it is away, and
// takes AnkiMobile's answer off the clipboard. It also tells AppReturns when
// this app is in front again after it was away. Empty functions on a Mac.
AnkiAppHost apple_anki_host();

}  // namespace ore

// HTTP for the mining tap (mining/mining.h) through NSURLSession.
#pragma once

#include "mining/mining.h"

namespace ore {

// Sends one request and waits for its answer. Needs nothing but the system,
// so it may be called from any thread for as long as the app runs.
HttpSend apple_http_sender();

}  // namespace ore

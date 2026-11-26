/* C interface of the mining kit (mining/mining_kit.h, docs/MINING.md):
   looking words up and making Anki cards inside the game. The launcher's
   settings and the popup's page speak to the kit in JSON by the name of a
   method, so an app needs no more than these functions to carry the calls.

   Every text is UTF-8. Apart from the three functions that take the engine,
   everything here may be called from any thread. */
#ifndef ORE_KIT_H
#define ORE_KIT_H

#include <stdint.h>

#include "api/ore_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OreKit OreKit;

/* Opens the kit. `folder` is where it keeps its settings and dictionaries
   (writable, made when missing); `resources` is the folder with
   transforms.json (engine/resources/mining). NULL on failure with a sentence
   in err. An app opens one kit and keeps it until it ends. */
OreKit *ore_kit_open(const char *folder, const char *resources, char *err, int err_len);
/* Closes it. No engine may still hold it (ore_set_kit with NULL first). */
void ore_kit_close(OreKit *kit);

/* Does what `method` names with `params_json` (NULL or "" for none) and
   returns {"ok":true,"result":...} or {"ok":false,"error":"a sentence for the
   player","kind":"..."}. The answer is the caller's: ore_kit_free. A call may
   wait for the network, so it does not belong on the thread that draws. */
char *ore_kit_call(OreKit *kit, const char *method, const char *params_json);

/* A file for the popup's page: "dict.media", "line.picture", "line.voice",
   "audio.word". Returns 1 with the bytes (ore_kit_free) and their media type,
   0 when there is none. */
int ore_kit_data(OreKit *kit, const char *method, const char *params_json, uint8_t **bytes, int64_t *length, char *type,
                 int type_len);

/* Frees what ore_kit_call, ore_kit_data and ore_text_tap gave. */
void ore_kit_free(void *given);

/* Whether a tap on the text looks a word up (the setting "direct"). */
int ore_kit_direct(OreKit *kit);

/* What the settings and the page are told without having asked: a name and a
   JSON payload ("card.added", "anki.lists", ...). The callback may come from
   any thread. NULL takes it away. */
typedef void (*OreKitEventCallback)(void *ctx, const char *name, const char *payload_json);
void ore_kit_set_events(OreKit *kit, void *ctx, OreKitEventCallback callback);

/* ---- with the engine; on the thread that runs ore_frame ---- */

/* Gives the engine the kit, or takes it away with NULL. The kit must outlive
   the engine or be taken away first. */
void ore_set_kit(OreEngine *engine, OreKit *kit);

/* What a tap at a place of the game's screen (0..799, 0..599) is, to be asked
   before the tap is given to the game as a click:
     0  nothing of the text's: give it to the game.
     1  on a character of the line that stands in the text box. *json (the
        caller's: ore_kit_free) is what the popup's page takes to show itself:
        {"line","text","index","rects","sentence","speaker","box","screen"}.
        Do not give the tap to the game; show the popup and hold the game.
     2  in the text box but on no character: do nothing at all.
   Without a kit, or while its setting "direct" is off, the answer is 0. */
int ore_text_tap(OreEngine *engine, int x, int y, char **json);

/* While the popup is open the game stands still: its script and animations
   wait and no input reaches it; what sounds goes on sounding. */
void ore_set_reading_hold(OreEngine *engine, int hold);

#ifdef __cplusplus
}
#endif

#endif

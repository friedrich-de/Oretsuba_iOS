# Mining

The game can send every line you read to a [VN Mining Server](https://github.com/friedrich-de/VN-Mining-Server):
the text, who speaks, the voice and a screenshot. The server's page shows the lines as they come, looks words up
and makes Anki cards of them, on the same phone or on any other device.

Nothing is sent unless a file named `mining.txt` lies in the game folder.

## Setting it up

1. Open your mining server's page, go to its settings, and under "The game" press **Download mining.txt**.
2. Put the file into the game folder, next to `SCRIPT.LPK`. One of the two folders above it works as well.
   * **iPhone / iPad**: in the Files app, move `mining.txt` to "On My iPhone" › Oretsuba (or into the
     `SET_UP` folder inside it). With a cable, Finder's "Files" tab of the device does the same.
   * **Mac**: put it into the game directory you chose in the launcher.
   * **Android**: press "Import mining.txt…" in the launcher and choose the downloaded file
     ([ANDROID.md](ANDROID.md#mining)).
3. Open the app. The launcher says where the lines go and whether the server takes the key:
   "Mining: lines go to vn.example.org (VN Mining Server 0.1.0)."

Then read. Each line appears on the server's page a moment after its text stands complete on the screen.

## mining.txt

```text
server = https://vn.example.org
key = the-api-key
```

| Line | |
| --- | --- |
| `server` | The address of the mining server. Needed. |
| `key` | The server's API key. Needed. |
| `game` | The title the server shows and puts on cards. Without it: 俺たちに翼はない. |
| `voice` | `no` sends lines without their voice. |
| `screenshot` | `no` sends lines without a screenshot. |

Lines that begin with `#` are comments. The file is UTF-8. It holds the key to your server, so keep it to yourself;
the repository's `.gitignore` names it.

An address in the internet has to begin with `https://`. Plain `http://` is for a server in your own network,
given by its IP address, a name without dots or one ending in `.local` (`http://192.168.1.20:8733`); iOS then
asks once for access to the local network.

## What is sent, and when

* A line is sent once its whole text stands on the screen and waits for you, 0.3 seconds after it began at
  the earliest. Lines you click through faster than that, and lines the skip function runs over, are not sent.
  The question before a choice does not wait for a click; it is sent when it has ended. The choices themselves
  are not sent.
* Switching to another app in the middle of a line sends that line at once, since the next thing you do is most
  likely to look up a word of it. The app asks iOS for the time to finish sending before it is put to sleep.
  The text is the whole line; the screenshot shows the screen as it was, so a line you leave while it is
  still appearing has only its first words in the picture.
* The text goes without the engine's tags and line breaks. The voice is the game's own Ogg file, as it is. The
  screenshot is the 800x600 screen at that moment, with the text box, as a JPEG of about 100 KB.
* The history screen sends nothing. A line shown again after a menu or a load is not sent twice, and neither is
  the line the game starts at with [auto save and continue](AUTOSAVE.md).

Sending happens beside the game, which never waits for the network. Without a connection up to 64 lines are kept
and sent in their order once the server answers again; beyond that the oldest go. If the server does not take the
key, the game stops sending until it is started again, because repeated wrong keys get an address locked out
for a while.

## When no lines arrive

The launcher's line under the game files says what is wrong:

| The launcher says | |
| --- | --- |
| Mining is off | No `mining.txt` was found in the game folder or the two folders above it. |
| mining.txt has no line "server = ..." (or "key = ...") | The file is there but incomplete. |
| ... could not be reached | The address is wrong, the server is down, or the device has no connection to it. |
| ... does not take the key in mining.txt | The key was changed on the server: download the file again. |

Press Check again in the launcher to ask again.

## For developers

The code is in `engine/src/mining/`:

* `mining.h`, `mining.cpp`: the part without the engine. The settings file, the text of a record, `LineGate`
  (which lines count as read), the multipart request and `MiningQueue` (one thread that sends in order, tries
  again and drops what the server will never take).
* `mining_tap.h`, `mining_tap.cpp`: `MiningTap` listens to the engine. `MessageWindow::show` (native 0x2fa)
  tells it of every line the ADV loop begins, `Engine::frame` lets it look once per frame, `Engine::set_paused`
  tells it that the player leaves.
* `engine/src/platform/apple/apple_http.mm`: the requests, through `NSURLSession`. Another host gives its own
  through `Platform::http_sender`; the Android app's go through `HttpURLConnection` (`android/.../Http.kt`).

`engine/tests/mining_test.cpp` tests the first part and needs neither game files nor an Apple system:

```sh
make -C engine tests && build/host/mining_test

# anywhere else
cd engine && c++ -std=c++17 -Isrc -Ithird_party tests/mining_test.cpp src/mining/mining.cpp src/util/log.cpp -lpthread -o /tmp/mining_test && /tmp/mining_test

# and against a running VN Mining Server at a plain http:// address: three lines are sent and read back
/tmp/mining_test --server http://127.0.0.1:8733 --key THE-KEY --voice some.ogg
```

The test host mines too: with a `mining.txt` in the folder given to `orehost --data`, every line its scripted
input reads goes to the server, and the run ends by saying how many arrived. `--debug` prints each line the
ADV loop begins and each line that is sent, with the game's clock, and the input command `pause N` puts the
app into the background for N frames. `engine/tests/inputs/mining_read.input` reads the opening of the
Japanese original that way:

```sh
build/host/orehost --data SET_UP --save /tmp/ore-save --frames 31970 --debug \
    --input engine/tests/inputs/mining_read.input 2>&1 | grep mining:
```

Of the 634 lines it runs through, the 61 that are read at a normal pace are sent, 28 of them with a voice. The
lines clicked away after 0.2 seconds and the lines the skip key runs over are not. Line 11 of the first script
goes out 0.5 seconds after it began, when the app is paused.

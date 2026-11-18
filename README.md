# Bedside Clock

A dark bedside clock that logs paper reading sessions to [Readwise Books](https://readwise.io/books_api) by voice.

The clock stays black until you tap it, then shows the time in red. To log a session:

1. Tap the plus and say the book and the page, like "The Blue Castle, page 39".
2. Check the title and page it heard (tap minus or plus to fix the page), then tap the tick.
3. Read.
4. Tap the clock, tap stop, and say the page you stopped on.
5. Tap the tick. The session goes to Readwise Books with its pages, times and progress, and to your feed.

If Wi-Fi is down at the end, the clock keeps the session and sends it with a later one.

The clock face and the low-power design come from [bedroom-clock](https://github.com/brianfakhoury/bedroom-clock).

## Parts

- The board: a [Waveshare ESP32-C6 Touch AMOLED 2.16](https://www.amazon.com/dp/B0HD761BL6) (480×480 touch screen, two microphones, real-time clock, Wi-Fi). [Waveshare's page](https://www.waveshare.com/esp32-c6-touch-amoled-2.16.htm) has the specs.
- A [battery](https://www.amazon.com/dp/B0DPZVBKMY) for the board's battery connector.
- A USB-C cable and a Mac for flashing and setup.

## Accounts and keys

- A Readwise access token from [readwise.io/access_token](https://readwise.io/access_token).
- An [OpenRouter](https://openrouter.ai) key with a low spending cap. Each spoken request costs about $0.0002 with `google/gemini-3.5-flash-lite`.

The keys are stored on the clock unencrypted. If the clock is lost, make a new Readwise token and a new OpenRouter key.

## Build and flash

You need [ESP-IDF v5.5.6](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32c6/get-started/).

```bash
. ~/esp/esp-idf/export.sh
cd firmware
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash
```

The host tests for the pure logic run without hardware:

```bash
cd firmware
make test
```

## Setup

`tools/provision.py` sends the settings to the clock over USB. It reads the Wi-Fi password and the OpenRouter key from the macOS Keychain and the Readwise token from the environment.

```bash
security add-generic-password -a "$USER" -s reading-clock-wifi -w
security add-generic-password -a "$USER" -s openrouter-api-key -w
export READWISE_API_TOKEN=...   # from readwise.io/access_token
python tools/provision.py --real --ssid "YourWiFi"
```

The board's Wi-Fi is 2.4 GHz only. The time zone is taken from the Mac.

Over USB the clock answers four commands: `CFG` and `TIME` (used by the setup script), and `STATUS` and `FLOW`, which print its state. `tools/monitor.py` prints the clock's log and picks its port by the Espressif USB vendor id.

## How it talks to Readwise Books

- `GET /books` fetches the books you are currently reading, so a spoken title can be matched to a real book. The list is cached on the clock for a day.
- `PATCH /books` sets a book's total page count from the catalog when it is missing. Paper progress is the page divided by the book's total, and a session logged before the total is set never gets progress.
- `POST /sessions` logs the session with `format: "paper"`, the start and end pages, UTC times, the time zone and `publish_to_feed: true`.

The API does not detect duplicates. The clock marks a session as sending before the request leaves. If it never hears back, it shows "Check Readwise" at the next wake and never sends that session again.

## Voice

The clock records up to 8 seconds at 16 kHz (tap to stop early), keeps the clip in a small flash partition, and sends it to OpenRouter with the titles on your shelf. The model answers with the title and page it heard. The clock matches the title against your shelf itself and always shows what it heard before starting anything. The clip is erased from flash after each request.

## Layout

- `firmware/components/core` is pure logic (session rules, title matching, the flow, JSON scanning, request bodies) with host tests in `firmware/tests`.
- `firmware/main` is the hardware and network code (display, touch, microphones, Wi-Fi, Readwise, OpenRouter, storage).
- `tools` has the setup, log and recovery scripts.

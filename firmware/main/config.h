#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Settings kept in NVS (namespace "cfg"), set over USB by tools/provision.py.
 * Keys: wifi_ssid, wifi_pass, rw_token, or_key, tz_posix, tz_iana.
 * Not encrypted yet (a later task decides on NVS encryption). */

/* Every USB line that is not a CFG line goes here, on the caller's task. */
typedef void (*config_command_fn)(const char *line);

/* Opens NVS (erasing it only if its layout is unusable), loads the settings
 * and applies tz_posix (UTC until one is set). Call once, early. */
void config_init(config_command_fn other_commands);

/* The stored value, or NULL when unset. Call from the task that calls
 * config_usb_poll() (the main loop): a CFG line replaces the value. */
const char *config_get(const char *key);

/* Copies the value into out (cap bytes) under the settings lock; safe from
 * any task. False, with out empty, when unset or too long for out. */
bool config_copy(const char *key, char *out, size_t cap);

/* All six keys are set. */
bool config_complete(void);

/* Reads whatever has arrived on USB without blocking and handles each full
 * line: "CFG <key> <value>" is answered "OK <key>" / "ERR <key>" (never the
 * value) and is accepted only while KEY stay-awake is on, or while the
 * clock idles awake with a USB host attached. Other lines go to the
 * command function. Call on every main-loop pass. */
void config_usb_poll(void);

/* True once after any setting changed (then the face should be redrawn). */
bool config_changed(void);

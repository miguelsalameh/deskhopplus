/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/*
 * The stored configuration's layout, and the two constants that identify it.
 *
 * Split out of structs.h (#74) so that the validation decision in
 * config_store.h can be compiled and tested on the host. structs.h itself
 * cannot: device_t reaches into the Pico SDK, and the defect this split
 * exists to prevent was pure arithmetic sitting behind flash I/O, where no
 * test could reach it.
 *
 * Pure C11 — stdint, stddef, and headers that are themselves pure. Keep it
 * that way, or the host test goes with it.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "constants.h" /* NUM_SCREENS */
#include "dh_p256.h"   /* DH_P256_SHARED_SIZE, DH_KEY_ID_SIZE */
#include "dh_hotkey.h"
#include "screen.h"    /* output_t */

/* What marks these bytes as a configuration at all. An erased sector reads as
   0xFF everywhere and a fresh one as 0x00, and neither is this. */
#define CONFIG_MAGIC_HEADER 0xB00B1E5u

/*
 * Bumped whenever the field layout changes, so a config written by another
 * firmware is refused rather than read with the wrong shape.
 *
 * Bumping it is not free: every stored configuration stops validating and
 * every device falls back to defaults on the next boot. That costs the user
 * every setting *and* the pairing secret, so each bump is a chord press on
 * both boards before the helpers work again. Weigh that before changing it.
 *
 *   9: the channel pairing secret joined config_t (#46). An older
 *      configuration is not read as a newer one — it falls back to defaults,
 *      which is also how a device with no secret ends up needing one chord
 *      press.
 *  10: the bearer secret became a registration — the paired helper's key id
 *      and the shared secret one ECDH produced (#111, ADR-0008). The bump
 *      costs every board its pairing, and that is not a side effect: v2 does
 *      not migrate a v1 pairing, because a migration path would have to accept
 *      the bearer token, which is the thing being removed. One chord press.
 *  11: the single configurable toggle key became the complete thirteen-action
 *      hotkey table (#27), which necessarily extends the stored layout.
 *  12: each output gained its Ctrl/GUI swap setting (#22). The byte occupies
 *      former alignment padding, but old configurations cannot express the
 *      macOS-on default, so accepting them would silently choose the wrong
 *      behavior.
 *  13: each output gained its fixed-size remap profile (#26): 32 overrides
 *      and 16 passthrough usages. The profile changes the stored layout.
 *  14: each output's single screen position became independent chain and
 *      border directions (#24). Old bytes cannot express the split.
 *  15: each output gained four keyed, normalized seam ranges (#29).
 */
#define CURRENT_CONFIG_VERSION 15

_Static_assert(sizeof(dh_seam_range_t) == 6,
               "dh_seam_range_t must contain no unnamed persisted padding");

/* #312's per-output fields took padding: the byte after swap_ctrl_gui and the
   word after the keymap. If either moved, the output would grow, every offset
   after it would shift, and every stored config would read as garbage. */
_Static_assert(offsetof(output_t, pointer_speed) == offsetof(output_t, swap_ctrl_gui) + 1 &&
                   offsetof(output_t, seam_ranges) == offsetof(output_t, pointer_speed) + 1,
               "output_t: pointer_speed must fill the padding after swap_ctrl_gui");
_Static_assert(offsetof(output_t, monitor_width) ==
                       offsetof(output_t, keymap) + sizeof(dh_keymap_profile_t) &&
                   offsetof(output_t, screensaver) == offsetof(output_t, monitor_height) + 2 &&
                   sizeof(output_t) == 168,
               "output_t: the monitor size must fill the padding before the screensaver");

typedef struct {
    uint32_t magic_header;
    uint32_t version;

    uint8_t force_mouse_boot_mode;
    uint8_t force_kbd_boot_protocol;

    uint8_t kbd_led_as_indicator;
    dh_hotkey_t hotkeys[DH_HOTKEY_ACTION_COUNT];
    uint8_t enable_acceleration;

    uint8_t enforce_ports;
    uint16_t jump_threshold;

    output_t output[NUM_SCREENS];

    /*
     * The registration: the one helper key this board has paired with (#111).
     * `channel_helper_key_id` names that key without carrying it, and
     * `channel_shared_secret` is what the single pairing-time ECDH produced —
     * every session key is derived from it, and it never crosses the wire.
     *
     * `channel_shared_secret` is deliberately *not* in api_field_map: config is
     * only ever exposed field by field, so material kept out of that map is
     * unreadable through the API and never syncs to the peer board.
     *
     * `channel_helper_key_id` is in the map, read-only, as fields 86 and 87 —
     * it is not a secret (every hello carries it in clear) and the config page
     * showing it is the only answer anywhere to "what is paired to this board?"
     * (#114). Read-only is what keeps the property above: the page writes a
     * changed field to both boards, so a *writable* registration field would
     * let a re-pairing here evict the other computer's helper.
     *
     * Wiped with the rest of the configuration, because wiping is how a user
     * unpairs. The board's *identity* is not here: it lives in its own flash
     * sector (flash_layout.h) precisely so that a wipe unpairs without
     * changing who this board is.
     */
    uint8_t channel_helper_key_id[DH_KEY_ID_SIZE];
    uint8_t channel_shared_secret[DH_P256_SHARED_SIZE];
    uint8_t channel_paired;

    /*
     * Clipboard sharing, one toggle per direction (#52). Stored as **blocks**,
     * so that zero means allowed: these two bytes were padding until now, and
     * every configuration already written has zeros in them. A field meaning
     * "enabled" would have read as off on every existing board and cost a
     * CURRENT_CONFIG_VERSION bump to avoid — which costs every user their
     * settings and their pairing. The sense is inverted here so that nobody
     * has to press a chord for a feature they have not asked for yet.
     *
     * The config page shows them as blocks too, so the stored sense and the
     * shown sense cannot drift apart.
     *
     * dh_clip_policy_for (dh_session.h) is the only place these are turned
     * into what a helper acts on.
     */
    uint8_t clip_block_a_to_b;
    uint8_t clip_block_b_to_a;

    /*
     * The largest clipboard payload a helper may accept, in megabytes (#56).
     * Ten by default, and up to DH_CLIP_CAP_MB_MAX; the board states it in
     * CLIP_POLICY, because the device is the single source of truth for
     * settings and a helper holds no cap of its own (#42).
     *
     * **Zero means the default**, which is what makes this free: the byte it
     * occupies was already here as `_pad`, so every configuration already
     * written reads as the default and no CURRENT_CONFIG_VERSION bump is owed
     * — the same move the two toggles above made, and for the same reason.
     * (The byte itself is here because _reserved needs 4-byte alignment. As
     * padding its contents were indeterminate, so a config_t built by
     * aggregate initialisation would be sealed over undefined bytes and
     * refused on the next boot — #74's failure mode with a different cause.
     * A named member is zeroed by initialisation and copied deterministically,
     * which removes it, and that is as true of a member carrying a setting.)
     */
    uint8_t clip_cap_mb;

    /*
     * When the Status LED goes dark on its own (#283): DH_STATUS_LED_* and a
     * time in seconds, read by dh_status_led_dark. Both boards hold the same
     * pair; the page writes it to both.
     *
     * These took the first word of _reserved, which every configuration
     * already written holds as zero — and zero is Never, so an old config
     * keeps the LED lit and no CURRENT_CONFIG_VERSION bump is owed.
     * Under Never, load_config turns a stored zero time into
     * DH_STATUS_LED_SEC_DEFAULT, in memory only, so the page shows a time
     * from its list.
     */
    uint8_t led_off_mode;
    uint8_t _led_pad; /* named so it is zeroed and sealed, not indeterminate */
    uint16_t led_off_sec;

    /*
     * Sleep sync (#287): non-zero makes the other computer sleep when the
     * active output's computer does (dh_sleep_sync.h). Both boards hold the
     * same value; the page writes it to both.
     *
     * It took the first byte of the second reserved word, which every
     * configuration already written holds as zero — and zero is off, so no
     * CURRENT_CONFIG_VERSION bump is owed.
     */
    uint8_t sleep_sync;

    /*
     * Sleep when idle (#303): minutes with no input before the active output
     * sleeps its own computer while the other one sleeps; 0 is Never, and
     * DH_SLEEP_SYNC_IMMEDIATELY (255) sleeps it as soon as the other one
     * goes to sleep, #306 (dh_sleep_sync_idle). Both boards hold the same value; the page writes
     * it to both. It took the byte after sleep_sync, which every
     * configuration already written holds as zero, so no
     * CURRENT_CONFIG_VERSION bump is owed.
     */
    uint8_t sleep_idle_min;

    /* The rest of the bytes that make the struct end exactly on the checksum.
       #46's 17 bytes rounded config_t up to 160 and left four bytes of padding
       *behind* the checksum, which is what broke persistence (#74); absorbing
       them here keeps the tail intentional rather than whatever alignment
       happens to leave over. */
    uint8_t _reserved[2];

    // Keep checksum at the end of the struct
    uint32_t checksum;
} config_t;

/*
 * The checksum must be the last field *and* leave no trailing padding, because
 * both save_config and load_config checksum everything ahead of it (#74).
 *
 * This is asserted rather than trusted: config_t inherits 8-byte alignment from
 * the uint64_t timers in screensaver_t, so adding a field can round the struct
 * up and silently leave padding behind the checksum. #46 did exactly that —
 * sizeof went 136 -> 160 with the checksum at 152 — and the CRC then covered
 * the checksum field itself, so no stored config ever validated again and every
 * boot fell back to defaults.
 */
_Static_assert(offsetof(config_t, checksum) == sizeof(config_t) - sizeof(uint32_t),
               "config_t: checksum must be last, with no trailing padding (see #74)");

#define CONFIG_FLASH_PAGE_SIZE 256u
#define CONFIG_FLASH_SECTOR_SIZE 4096u
#define CONFIG_FLASH_PAGE_COUNT \
    ((sizeof(config_t) + CONFIG_FLASH_PAGE_SIZE - 1u) / CONFIG_FLASH_PAGE_SIZE)
#define CONFIG_FLASH_BYTES (CONFIG_FLASH_PAGE_COUNT * CONFIG_FLASH_PAGE_SIZE)
_Static_assert(CONFIG_FLASH_BYTES <= CONFIG_FLASH_SECTOR_SIZE,
               "config_t must fit its dedicated flash sector");

/*
 * The clipboard toggles came out of padding that was already there, so the
 * layout is unchanged and no stored configuration stops validating (#52). That
 * claim is only true while those three bytes still sit between `channel_paired`
 * and the Status LED fields (once the first `_reserved` word) — a fourth field
 * here would push them along, change every offset after it, and silently
 * invalidate every board's stored config without the version bump that is
 * supposed to announce exactly that.
 */
_Static_assert(offsetof(config_t, led_off_mode) == offsetof(config_t, channel_paired) + 4,
               "config_t: the clipboard toggles must fit the padding, not extend the struct");

/* The size cap took the padding byte the toggles left behind, so the same
   claim holds for it and for the same reason (#56). Spelled separately because
   the two arrived years apart and a single assertion would not say which of
   them a future field had displaced. */
_Static_assert(offsetof(config_t, clip_cap_mb) == offsetof(config_t, clip_block_b_to_a) + 1,
               "config_t: the clipboard size cap must fit the padding, not extend the struct");

/* The Status LED fields took the first reserved word, so they sit where it
   sat and the checksum does not move (#283). A field added ahead of them
   would push them past the bytes old configs hold as zero. */
_Static_assert(offsetof(config_t, led_off_mode) == offsetof(config_t, clip_cap_mb) + 1,
               "config_t: the Status LED fields must take the first reserved word");
_Static_assert(offsetof(config_t, led_off_sec) == offsetof(config_t, led_off_mode) + 2,
               "config_t: led_off_sec must stay aligned inside the first reserved word");
_Static_assert(offsetof(config_t, checksum) == offsetof(config_t, led_off_mode) + 8,
               "config_t: the Status LED fields must not extend the struct");

/* Sleep sync took the first byte of the second reserved word, so the checksum
   does not move (#287). */
_Static_assert(offsetof(config_t, sleep_sync) == offsetof(config_t, led_off_sec) + 2,
               "config_t: Sleep sync must take the first byte of the second reserved word");
_Static_assert(offsetof(config_t, checksum) == offsetof(config_t, sleep_sync) + 4,
               "config_t: Sleep sync must not extend the struct");
_Static_assert(offsetof(config_t, sleep_idle_min) == offsetof(config_t, sleep_sync) + 1,
               "config_t: Sleep when idle must take the byte after Sleep sync");

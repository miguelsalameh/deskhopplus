/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 * Copyright (c) 2025 Hrvoje Cavrak
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * See the file LICENSE for the full license text.
 * Modified by Derek Reynolds, 2026, for deskhopplus.
 */
#include "hid_report.h"
#include "main.h"

/* Given a value struct with size and offset in bits, find and return a value from the HID report */
int32_t get_report_value(uint8_t *report, int len, report_val_t *val) {
    /* Calculate the bit offset within the byte */
    uint16_t offset_in_bits = val->offset % 8;

    /* Calculate the remaining bits in the first byte */
    uint16_t remaining_bits = 8 - offset_in_bits;

    /* Calculate the byte offset in the array */
    uint16_t byte_offset = val->offset >> 3;

    if (byte_offset >= len)
        return 0;

    /* Create a mask for the specified number of bits */
    uint32_t mask = (1u << val->size) - 1;

    /* Initialize the result value with the bits from the first byte */
    int32_t result = report[byte_offset] >> offset_in_bits;

    /* Move to the next byte and continue fetching bits until the desired length is reached */
    while (val->size > remaining_bits && byte_offset + 1 < len) {
        result |= report[++byte_offset] << remaining_bits;
        remaining_bits += 8;
    }

    /* Apply the mask to retain only the desired number of bits */
    result = result & mask;

    /* Special case if our result is negative.
       Check if the most significant bit of 'val' is set */
    if (result & ((mask >> 1) + 1)) {
        /* If it is set, sign-extend 'val' by filling the higher bits with 1s */
        result |= (0xFFFFFFFFU << val->size);
    }

    return result;
}

/* Which keyboard on this interface owns this report ID? An unknown ID gets the
   primary keyboard, so a stray leading byte never claims a slot. */
keyboard_t *get_keyboard(hid_interface_t *iface, uint8_t report_id) {
    /* Without report IDs there is only one keyboard */
    if (!iface->uses_report_id)
        return &iface->keyboards[PRIMARY_KEYBOARD];

    for (int i = 0; i < iface->num_keyboards && i < MAX_KEYBOARDS; i++) {
        if (iface->keyboards[i].report_id == report_id)
            return &iface->keyboards[i];
    }

    return &iface->keyboards[PRIMARY_KEYBOARD];
}

/* Parse-time get_keyboard: an unknown report ID takes the next free slot, so each
   keyboard collection keeps its own layout. handle_keyboard_descriptor_values
   counts the slot once it holds a keyboard. NULL once every slot is taken. */
static keyboard_t *get_or_add_keyboard(hid_interface_t *iface, uint8_t report_id) {
    keyboard_t *keyboard = get_keyboard(iface, report_id);

    if (!iface->uses_report_id || keyboard->report_id == report_id)
        return keyboard;

    if (iface->num_keyboards >= MAX_KEYBOARDS)
        return NULL;

    keyboard = &iface->keyboards[iface->num_keyboards];
    keyboard->report_id = report_id;
    return keyboard;
}

/* After processing the descriptor, assign the values so we can later use them to interpret reports.
   Also keeps the first array control in dst, which extract_consumer_report uses as a length bound. */
void handle_consumer_control_values(report_val_t *src, report_val_t *dst, hid_interface_t *iface) {
    keyboard_t *keyboard = get_keyboard(iface, src->report_id);

    /* Keep the first array control, so a report cut off inside it is dropped */
    if (src->data_type == ARRAY && src->item_type == DATA && dst->size == 0)
        *dst = *src;

    if (src->offset >= MAX_CC_BUTTONS) {
        return;
    }

    if (src->data_type == VARIABLE) {
        keyboard->cc_array[src->offset] = src->usage;
        iface->consumer.is_variable = true;
    }

    iface->consumer.is_array |= (src->data_type == ARRAY);
}

/* After processing the descriptor, assign the values so we can later use them to interpret reports */
void handle_system_control_values(report_val_t *src, report_val_t *dst, hid_interface_t *iface) {
    keyboard_t *keyboard = get_keyboard(iface, src->report_id);

    if (src->offset >= MAX_SYS_BUTTONS) {
        return;
    }

    if (src->data_type == VARIABLE) {
        keyboard->sys_array[src->offset] = src->usage;
        iface->system.is_variable = true;
    }

    iface->system.is_array |= (src->data_type == ARRAY);
}

/* After processing the descriptor, assign the values so we can later use them to interpret reports */
void handle_keyboard_descriptor_values(report_val_t *src, report_val_t *dst, hid_interface_t *iface) {
    const int LEFT_CTRL = 0xE0;
    keyboard_t *keyboard = get_or_add_keyboard(iface, src->report_id);

    /* A keyboard collection past MAX_KEYBOARDS has no slot, so its reports are
       dropped rather than decoded with another collection's layout. */
    if (keyboard == NULL) {
        iface->report_receiver[src->report_id] = RECEIVER_NONE;
        return;
    }

    /* Constants are normally used for padding, so skip'em */
    if (src->item_type == CONSTANT)
        return;

    /* Detect and handle modifier keys. To make sure this really is the modifier key,
       we expect e.g. left control to be within the usage interval. */
    bool is_modifier = src->size <= MODIFIER_BIT_LENGTH && src->data_type == VARIABLE &&
                       LEFT_CTRL >= src->usage_min && LEFT_CTRL <= src->usage_max;
    if (is_modifier)
        keyboard->modifier = *src;

    /* If we have an array member, that's most likely a key (0x00 - 0xFF, 1 byte) */
    if (src->offset_idx < MAX_KEYS) {
        keyboard->key_array[src->offset_idx] = (src->data_type == ARRAY);
    }

    /* Handle NKRO. The bitmap may come in several sections (a Wooting has four, one
       only 8 bits), so a section is any run of usages one per bit, other than the
       modifier. The total width decides NKRO, so one stray narrow bit field leaves a
       6KRO keyboard alone.
       The Keychron Ultra-Link declares 153 usages over 152 bits, so a section at
       least NKRO_MIN_BITS wide may also declare more usages than bits; the surplus
       is never read. Narrower fields keep the exact test, or a lazy range such as
       0x00-0xFF would pass any stray field off as a section. The span is 64-bit so
       a 4-byte usage range cannot overflow it.
       ponytail: a fifth section is dropped; raise MAX_NKRO_BLOCKS if a keyboard needs it */
    int64_t span = (int64_t)src->usage_max - src->usage_min + 1;
    bool usage_per_bit = src->usage_max > src->usage_min && span >= src->size &&
                         (span == src->size || src->size >= NKRO_MIN_BITS);
    if (usage_per_bit && !is_modifier && src->data_type == VARIABLE &&
        keyboard->nkro_count < MAX_NKRO_BLOCKS) {
        keyboard->nkro[keyboard->nkro_count++] = (nkro_block_t){
            .offset    = src->offset,
            .size      = src->size,
            .usage_min = (uint8_t)src->usage_min,
        };
        keyboard->nkro_bits += src->size;
        keyboard->is_nkro = keyboard->nkro_bits > NKRO_MIN_BITS;
    }

    /* We found a keyboard on this interface for a specific report id. */
    if (!keyboard->is_found) {
        keyboard->is_found = true;
        iface->num_keyboards++;
    }
}

void handle_buttons(report_val_t *src, report_val_t *dst, hid_interface_t *iface) {
    /* Constant is normally used for padding with mouse buttons, aggregate to simplify things */
    if (src->item_type == CONSTANT) {
        iface->mouse.buttons.size += src->size;
        return;
    }

    iface->mouse.buttons = *src;

    /* We found a mouse on this interface. */
    iface->mouse.is_found = true;
}

void _store(report_val_t *src, report_val_t *dst, hid_interface_t *iface) {
    if (src->item_type != CONSTANT)
        *dst = *src;
}

static uint8_t *get_mouse_id(hid_interface_t *iface) {
    return &iface->mouse.report_id;
}

static uint8_t *get_consumer_id(hid_interface_t *iface) {
    return &iface->consumer.report_id;
}

static uint8_t *get_system_id(hid_interface_t *iface) {
    return &iface->system.report_id;
}


void extract_data(hid_interface_t *iface, report_val_t *val) {
    const usage_map_t map[] = {
        {.usage_page   = HID_USAGE_PAGE_BUTTON,
         .global_usage = HID_USAGE_DESKTOP_MOUSE,
         .handler      = handle_buttons,
         .receiver     = RECEIVER_MOUSE,
         .dst          = &iface->mouse.buttons,
         .get_id       = get_mouse_id},

        {.usage_page   = HID_USAGE_PAGE_DESKTOP,
         .global_usage = HID_USAGE_DESKTOP_MOUSE,
         .usage        = HID_USAGE_DESKTOP_X,
         .handler      = _store,
         .receiver     = RECEIVER_MOUSE,
         .dst          = &iface->mouse.move_x,
         .get_id       = get_mouse_id},

        {.usage_page   = HID_USAGE_PAGE_DESKTOP,
         .global_usage = HID_USAGE_DESKTOP_MOUSE,
         .usage        = HID_USAGE_DESKTOP_Y,
         .handler      = _store,
         .receiver     = RECEIVER_MOUSE,
         .dst          = &iface->mouse.move_y,
         .get_id       = get_mouse_id},

        {.usage_page   = HID_USAGE_PAGE_DESKTOP,
         .global_usage = HID_USAGE_DESKTOP_MOUSE,
         .usage        = HID_USAGE_DESKTOP_WHEEL,
         .handler      = _store,
         .receiver     = RECEIVER_MOUSE,
         .dst          = &iface->mouse.wheel,
         .get_id       = get_mouse_id},

        {.usage_page   = HID_USAGE_PAGE_CONSUMER,
         .global_usage = HID_USAGE_DESKTOP_MOUSE,
         .usage        = HID_USAGE_CONSUMER_AC_PAN,
         .handler      = _store,
         .receiver     = RECEIVER_MOUSE,
         .dst          = &iface->mouse.pan,
         .get_id       = get_mouse_id},

        {.usage_page   = HID_USAGE_PAGE_KEYBOARD,
         .global_usage = HID_USAGE_DESKTOP_KEYBOARD,
         .handler      = handle_keyboard_descriptor_values,
         .receiver     = RECEIVER_KEYBOARD},  /* get_or_add_keyboard records the ID */

        {.usage_page   = HID_USAGE_PAGE_CONSUMER,
         .global_usage = HID_USAGE_CONSUMER_CONTROL,
         .handler      = handle_consumer_control_values,
         .receiver     = RECEIVER_CONSUMER,
         .dst          = &iface->consumer.val,
         .get_id       = get_consumer_id},

        {.usage_page   = HID_USAGE_PAGE_DESKTOP,
         .global_usage = HID_USAGE_DESKTOP_SYSTEM_CONTROL,
         .handler      = _store,
         .receiver     = RECEIVER_SYSTEM,
         .dst          = &iface->system.val,
         .get_id       = get_system_id},
    };

    /* We extracted all we could find in the descriptor to report_values, now go through them and
       match them up with the values in the table above, then store those values for later reference */

    for (const usage_map_t *hay = map; hay != &map[ARRAY_SIZE(map)]; hay++) {
        /* ---> If any condition is not defined, we consider it as matched <--- */
        bool global_usages_match = (val->global_usage == hay->global_usage) || (hay->global_usage == 0);
        bool usages_match        = (val->usage == hay->usage) || (hay->usage == 0);
        bool usage_pages_match   = (val->usage_page == hay->usage_page) || (hay->usage_page == 0);

        if (global_usages_match && usages_match && usage_pages_match) {
            if (hay->get_id != NULL)
                *(hay->get_id(iface)) = val->report_id;

            /* Before the handler, which may take the registration back */
            iface->report_receiver[val->report_id] = hay->receiver;

            hay->handler(val, hay->dst, iface);
        }
    }
}

/* Sends a report from an interface that uses report IDs (or declares no boot
   protocol) to the receiver its descriptor declared for that ID. A report
   with an undeclared ID, or too short to hold its ID, is dropped. */
void route_report(uint8_t *report, int len, uint8_t device_idx, hid_interface_t *iface) {
    static const process_report_f receivers[] = {
        [RECEIVER_NONE]     = NULL,
        [RECEIVER_MOUSE]    = process_mouse_report,
        [RECEIVER_KEYBOARD] = process_keyboard_report,
        [RECEIVER_CONSUMER] = process_consumer_report,
        [RECEIVER_SYSTEM]   = process_system_report,
    };
    if (iface->uses_report_id && len < 1)
        return;

    uint8_t report_id = iface->uses_report_id ? report[0] : 0;
    process_report_f receiver = receivers[iface->report_receiver[report_id]];

    if (receiver != NULL)
        receiver(report, len, device_idx, iface);
}

/* Decodes a consumer-control report into the CONSUMER_CONTROL_LENGTH bytes sent
   to the computer. The payload follows the report ID only on an interface that
   declares IDs. Returns false for a report with no payload, or one that ends
   inside its array control. */
bool extract_consumer_report(uint8_t *raw_report, int len, hid_interface_t *iface, uint8_t *out) {
    int data_len = len - iface->uses_report_id;

    if (data_len <= 0)
        return false;

    uint8_t *data = raw_report + iface->uses_report_id;
    keyboard_t *keyboard = get_keyboard(iface, raw_report[0]);

    memset(out, 0, CONSUMER_CONTROL_LENGTH);

    /* A variable report is a bitmap; send the usage of the last bit set. */
    if (iface->consumer.is_variable) {
        for (int i = 0; i < MAX_CC_BUTTONS && i < 8 * data_len; i++) {
            if ((data[i >> 3] >> (i % 8)) & 1) {
                out[0] = keyboard->cc_array[i] & 0xFF;
                out[1] = keyboard->cc_array[i] >> 8;
            }
        }
    }
    else {
        report_val_t *val = &iface->consumer.val;
        uint8_t report_id = iface->uses_report_id ? raw_report[0] : 0;

        /* ponytail: bounds only the first array's report ID; store one per ID if a
           device with two consumer arrays sends short reports */
        if (val->report_id == report_id && 8 * data_len < val->offset + val->size)
            return false;

        for (int i = 0; i < data_len && i < CONSUMER_CONTROL_LENGTH; i++)
            out[i] = data[i];
    }
    return true;
}

/* Decodes a system-control report into the SYSTEM_CONTROL_LENGTH byte sent to
   the computer. The payload follows the report ID only on an interface that
   declares IDs. Returns false for a report with no payload. */
bool extract_system_report(uint8_t *raw_report, int len, hid_interface_t *iface, uint8_t *out) {
    if (len - iface->uses_report_id < SYSTEM_CONTROL_LENGTH)
        return false;

    *out = raw_report[iface->uses_report_id];
    return true;
}

/* Appends the usage of each set bit in one bitmap section to dst, up to max_keys.
   payload excludes the report ID; reading stops at its len bytes. */
static int extract_bit_variable(nkro_block_t *block, uint8_t *payload, int len, uint8_t *dst, int max_keys) {
    int key_count = 0;

    for (int bit = 0; bit < block->size && key_count < max_keys; bit++) {
        int j = block->offset + bit;

        if ((j >> 3) >= len)
            break;

        if (payload[j >> 3] & (1 << (j & 0b111)))
            dst[key_count++] = block->usage_min + bit;
    }

    return key_count;
}

int32_t _extract_kbd_boot(uint8_t *raw_report, int len, hid_keyboard_report_t *report) {
    uint8_t *src = raw_report;

    /* In case keyboard still uses report ID in this, just pick the last 8 bytes */
    if (len == KBD_REPORT_LENGTH + 1)
        src++;

    memcpy(report, src, KBD_REPORT_LENGTH);
    return KBD_REPORT_LENGTH;
}

int32_t _extract_kbd_other(uint8_t *raw_report, int len, hid_interface_t *iface, hid_keyboard_report_t *report) {
    keyboard_t *kb = get_keyboard(iface, raw_report[0]);
    uint8_t *src = raw_report + iface->uses_report_id;

    len -= iface->uses_report_id;
    if (kb->modifier.offset_idx >= len)
        return -1;

    report->modifier = src[kb->modifier.offset_idx];
    for (int i=0, j=0; i < MAX_KEYS && i < len && j < KEYS_IN_USB_REPORT; i++) {
        if(kb->key_array[i])
            report->keycode[j++] = src[i];
    }

    return KBD_REPORT_LENGTH;
}

int32_t _extract_kbd_nkro(uint8_t *raw_report, int len, hid_interface_t *iface, hid_keyboard_report_t *report) {
    keyboard_t *kb = get_keyboard(iface, raw_report[0]);
    uint8_t *payload = raw_report + iface->uses_report_id;
    int key_count = 0;

    len -= iface->uses_report_id;

    /* We expect modifier to be 8 bits long, otherwise we'll fallback to boot mode */
    if (kb->modifier.size != MODIFIER_BIT_LENGTH || kb->modifier.offset_idx >= len)
        return -1;

    report->modifier = payload[kb->modifier.offset_idx];

    /* Collect keys from every section until the outgoing report is full */
    for (int i = 0; i < kb->nkro_count && key_count < KEYS_IN_USB_REPORT; i++)
        key_count += extract_bit_variable(&kb->nkro[i], payload, len, &report->keycode[key_count],
                                          KEYS_IN_USB_REPORT - key_count);

    return key_count;
}

int32_t extract_kbd_data(
    uint8_t *raw_report, int len, uint8_t itf, hid_interface_t *iface, hid_keyboard_report_t *report) {
    keyboard_t *keyboard = get_keyboard(iface, raw_report[0]);

    /* Clear the report to start fresh */
    memset(report, 0, KBD_REPORT_LENGTH);

    /* If we're in boot protocol mode, then it's easy to decide. */
    if (iface->protocol == HID_PROTOCOL_BOOT)
        return _extract_kbd_boot(raw_report, len, report);

    /* NKRO is a special case. If extraction fails (descriptor parsed as NKRO but the
       actual report layout doesn't match — e.g. wireless dongles that advertise an NKRO
       collection but transmit standard boot-style reports), fall through to other extractors. */
    if (keyboard->is_nkro) {
        int32_t ret = _extract_kbd_nkro(raw_report, len, iface, report);
        if (ret >= 0)
            return ret;
        memset(report, 0, KBD_REPORT_LENGTH);
    }

    /* If we're getting 8 bytes of report, it's safe to assume standard modifier + reserved + keys */
    if (!iface->uses_report_id && (len == KBD_REPORT_LENGTH || len == KBD_REPORT_LENGTH + 1))
        return _extract_kbd_boot(raw_report, len, report);

    /* This is something completely different, look at the report  */
    return _extract_kbd_other(raw_report, len, iface, report);
}

/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 * Copyright (c) 2025 Hrvoje Cavrak
 *
 * Based on the TinyUSB HID parser routine and the amazing USB2N64
 * adapter (https://github.com/pdaxrom/usb2n64-adapter)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * See the file LICENSE for the full license text.
 * Modified by Derek Reynolds, 2026, for deskhopplus.
 */
#include "main.h"

#define IS_BLOCK_END (parser->collection.start == parser->collection.end)

enum { SIZE_0_BIT = 0, SIZE_8_BIT = 1, SIZE_16_BIT = 2, SIZE_32_BIT = 3 };
const uint8_t SIZE_LOOKUP[4] = {0, 1, 2, 4};

/* Size is 0, 1, 2, or 3, describing cases of no data, 8-bit, 16-bit,
  or 32-bit data. */
uint32_t get_descriptor_value(uint8_t const *report, int size) {
    switch (size) {
        case SIZE_8_BIT:
            return report[0];
        case SIZE_16_BIT:
            return tu_u16(report[1], report[0]);
        case SIZE_32_BIT:
            return tu_u32(report[3], report[2], report[1], report[0]);
        default:
            return 0;
    }
}

uint32_t *get_or_create_report_offset(parser_state_t *parser, uint8_t report_id) {
    for (int i = 0; i < parser->num_report_offsets; i++) {
        if (parser->report_offsets[i].report_id == report_id) {
            return &parser->report_offsets[i].offset_in_bits;
        }
    }

    if (parser->num_report_offsets < MAX_REPORT_LAYOUTS) {
        parser->report_offsets[parser->num_report_offsets].report_id = report_id;
        parser->report_offsets[parser->num_report_offsets].offset_in_bits = 0;
        return &parser->report_offsets[parser->num_report_offsets++].offset_in_bits;
    }

    return NULL;
}

uint32_t get_current_offset(parser_state_t *parser) {
    uint32_t *offset = get_or_create_report_offset(parser, parser->report_id);
    return offset ? *offset : 0;
}

/* Usage for element i of the current main item. Past the declared usages the last one
   repeats (HID spec); with none declared, the usage carried from the previous main item
   in usages[0] applies. Never reads past usage_count, however large the report count. */
uint16_t get_usage(parser_state_t *parser, uint32_t i) {
    if (parser->usage_count == 0)
        return parser->usages[0];

    return parser->usages[i < parser->usage_count ? i : parser->usage_count - 1];
}

void store_element(parser_state_t *parser, report_val_t *val, uint16_t usage, uint32_t data, uint16_t size, hid_interface_t *iface) {
    uint32_t current_offset = get_current_offset(parser);

    *val = (report_val_t){
        .offset     = current_offset,
        .offset_idx = current_offset >> 3,
        .size       = size,

        .usage_max = parser->locals[RI_LOCAL_USAGE_MAX].val,
        .usage_min = parser->locals[RI_LOCAL_USAGE_MIN].val,

        .item_type   = (data & 0x01) ? CONSTANT : DATA,
        .data_type   = (data & 0x02) ? VARIABLE : ARRAY,

        .usage        = usage,
        .usage_page   = parser->globals[RI_GLOBAL_USAGE_PAGE].val,
        .global_usage = parser->global_usage,
        .report_id    = parser->report_id
    };

    iface->uses_report_id |= (parser->report_id != 0);
}

void handle_global_item(parser_state_t *parser, item_t *item) {
    if (item->hdr.tag == RI_GLOBAL_REPORT_ID) {
        parser->report_id = item->val;
    }

    parser->globals[item->hdr.tag] = *item;
}

void handle_local_item(parser_state_t *parser, item_t *item) {
    /* There are just 16 possible tags, store any one that comes along to an array
        instead of doing switch and 16 cases */
    parser->locals[item->hdr.tag] = *item;

    if (item->hdr.tag == RI_LOCAL_USAGE) {
        if(IS_BLOCK_END)
            parser->global_usage = item->val;

        /* Every main item starts again at usages[0]; usages past the storage are dropped */
        else if (parser->usage_count < HID_MAX_USAGES)
            parser->usages[parser->usage_count++] = item->val;
    }
}

void handle_main_input(parser_state_t *parser, item_t *item, hid_interface_t *iface) {
    uint32_t size  = parser->globals[RI_GLOBAL_REPORT_SIZE].val;
    uint32_t count = parser->globals[RI_GLOBAL_REPORT_COUNT].val;
    report_val_t val = {0};

    /* Swap count and size for 1-bit variables, it makes sense to process e.g. NKRO with
       size = 1 and count = 240 in one go instead of doing 240 iterations
       Don't do this if there are usages in the queue, though.
       */
    if (size == 1 && parser->usage_count <= 1) {
        size  = count;
        count = 1;
    }

    uint32_t *current_offset = get_or_create_report_offset(parser, parser->report_id);
    if (!current_offset)
        return;

    /* report_val_t keeps bit offsets in 16 bits, so nothing past that is readable. This
       also ends a zero-size field with a huge count, which would never advance. */
    for (uint32_t i = 0; i < count && size > 0 && *current_offset <= UINT16_MAX; i++) {
        store_element(parser, &val, get_usage(parser, i), item->val, size, iface);

        /* Use the parsed data to populate internal device structures */
        extract_data(iface, &val);

        /* Iterate <count> times and increase offset by <size> amount, moving by <count> x <size> bits */
        *current_offset += size;
    }

    /* Carry this item's last usage to a next main item that declares none */
    if (parser->usage_count > 0)
        parser->usages[0] = parser->usages[parser->usage_count - 1];
}

void handle_main_item(parser_state_t *parser, item_t *item, hid_interface_t *iface) {
    switch (item->hdr.tag) {
        case RI_MAIN_COLLECTION:
            parser->collection.start++;
            break;

        case RI_MAIN_COLLECTION_END:
            parser->collection.end++;
            break;

        case RI_MAIN_INPUT:
            handle_main_input(parser, item, iface);
            break;
    }

    parser->usage_count = 0;

    /* Local items do not carry over to the next Main item (HID spec v1.11, section 6.2.2.8) */
    memset(parser->locals, 0, sizeof(parser->locals));
}


/* This method is sub-optimal and far from a generalized HID descriptor parsing, but should
 * hopefully work well enough to find the basic values we care about to move the mouse around.
 * Your descriptor for a mouse with 2 wheels and 264 buttons might not parse correctly.
 * */
parser_state_t parser_state = {0};  // Avoid placing it on the stack, it's large

void parse_report_descriptor(hid_interface_t *iface,
                            uint8_t const *report,
                            int desc_len
                            ) {
    item_t item = {0};

    /* Wipe parser_state clean */
    memset(&parser_state, 0, sizeof(parser_state_t));

    while (desc_len > 0) {
        item.hdr = *(header_t *)report++;
        item.val = get_descriptor_value(report, item.hdr.size);

        switch (item.hdr.type) {
            case RI_TYPE_MAIN:
                handle_main_item(&parser_state, &item, iface);
                break;

            case RI_TYPE_GLOBAL:
                handle_global_item(&parser_state, &item);
                break;

            case RI_TYPE_LOCAL:
                handle_local_item(&parser_state, &item);
                break;
        }
        /* Move to the next position and decrement size by header length + data length */
        report += SIZE_LOOKUP[item.hdr.size];
        desc_len -= (SIZE_LOOKUP[item.hdr.size] + 1);
    }
}

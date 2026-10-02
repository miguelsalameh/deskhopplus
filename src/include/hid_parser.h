/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 * Copyright (c) 2024 Hrvoje Cavrak
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * See the file LICENSE for the full license text.
 * Modified by Derek Reynolds, 2026, for deskhopplus.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "tusb.h"

/*==============================================================================
 *  Constants
 *==============================================================================*/

#define HID_DEFAULT_NUM_COLLECTIONS 16
#define HID_MAX_USAGES              128
#define MAX_CC_BUTTONS              16
#define MAX_DEVICES                 4
#define MAX_INTERFACES              12  // Per device; allows for complex devices like QMK
#define MAX_KEYS                    32
#define MAX_REPORT_LAYOUTS          24  // Distinct report IDs whose layout one interface parses
#define MAX_KEYBOARDS               5
#define MAX_NKRO_BLOCKS             4   // Bitmap sections per keyboard; a Wooting declares 4
#define NKRO_MIN_BITS               32  // Total bitmap width a keyboard must exceed to be NKRO
#define MAX_SYS_BUTTONS             8
#define PRIMARY_KEYBOARD            0
/*==============================================================================
 *  Data Structures
 *==============================================================================*/

/* Counts how many collection starts and ends we've seen, when they equalize
   (and not zero), we are at the end of a block */
typedef struct {
    uint8_t start;
    uint8_t end;
} collection_t;

/* Maps a report ID to its current bit offset */
typedef struct {
    uint8_t report_id;
    uint32_t offset_in_bits;
} report_offset_map_t;

/* Header byte is unpacked to size/type/tag using this struct */
typedef struct TU_ATTR_PACKED {
    uint8_t size : 2;
    uint8_t type : 2;
    uint8_t tag : 4;
} header_t;

/* We store a header block and corresponding data in an array of these
   to avoid having to use numerous switch-case checks */
typedef struct {
    header_t hdr;
    uint32_t val;
} item_t;

typedef enum {
    DATA = 0,
    CONSTANT,
    ARRAY,
    VARIABLE,
    ABSOLUTE_DATA,
    RELATIVE_DATA,
    NO_WRAP,
    WRAP,
    LINEAR,
    NONLINEAR,
} data_type_e;

// Extended precision mouse movement information
typedef struct {
    int32_t move_x;
    int32_t move_y;
    int32_t wheel;
    int32_t pan;
    int32_t buttons;
} mouse_values_t;

/* Describes where can we find a value in a HID report */
typedef struct TU_ATTR_PACKED {
    uint16_t offset;     // In bits
    uint16_t offset_idx; // In bytes
    uint16_t size;       // In bits

    int32_t usage_min;
    int32_t usage_max;

    uint8_t item_type;
    uint8_t data_type;

    uint8_t report_id;
    uint16_t global_usage;
    uint16_t usage_page;
    uint16_t usage;
} report_val_t;

/* Defines information about HID report format for the mouse. */
typedef struct {
    report_val_t buttons;
    report_val_t move_x;
    report_val_t move_y;
    report_val_t wheel;
    report_val_t pan;

    uint8_t report_id;

    bool is_found;
    bool uses_report_id;
} mouse_t;

typedef struct hid_interface_t hid_interface_t;
typedef void (*process_report_f)(uint8_t *, int, uint8_t, hid_interface_t *);

/* Which receiver a report ID goes to. One byte each, so an interface can map
   all 256 report IDs; route_report turns it into the receiver function. */
typedef enum {
    RECEIVER_NONE = 0,
    RECEIVER_MOUSE,
    RECEIVER_KEYBOARD,
    RECEIVER_CONSUMER,
    RECEIVER_SYSTEM,
} receiver_e;

/* One section of an NKRO bitmap: usage_min + i is pressed when bit offset + i
   of the payload (report ID excluded) is set, for i below size. */
typedef struct {
    uint16_t offset; // In bits
    uint16_t size;   // In bits
    uint8_t usage_min;
} nkro_block_t;

/* Defines information about HID report format for the keyboard. */
typedef struct {
    report_val_t modifier;
    nkro_block_t nkro[MAX_NKRO_BLOCKS];
    uint8_t nkro_count;
    uint16_t nkro_bits; // Sum of nkro[].size, which decides is_nkro
    uint16_t cc_array[MAX_CC_BUTTONS];
    uint16_t sys_array[MAX_SYS_BUTTONS];
    bool key_array[MAX_KEYS];

    uint8_t report_id;
    uint8_t key_array_idx;

    bool uses_report_id;
    bool is_found;
    bool is_nkro;
} keyboard_t;

typedef struct {
    report_val_t val;
    uint8_t report_id;
    bool is_variable;
    bool is_array;
} report_t;

struct hid_interface_t {
    keyboard_t keyboards[MAX_KEYBOARDS];
    uint8_t num_keyboards;
    mouse_t mouse;
    report_t consumer;
    report_t system;
    uint8_t report_receiver[UINT8_MAX + 1]; // receiver_e, indexed by report ID
    uint8_t protocol;
    bool uses_report_id;
};

typedef struct {
    report_val_t *map;
    int map_index; /* Index of the current element we've found */
    int report_id; /* Report ID of the current section we're parsing */

    uint32_t usage_count;
    uint16_t usages[HID_MAX_USAGES];
    uint16_t global_usage;

    collection_t collection;

    report_offset_map_t report_offsets[MAX_REPORT_LAYOUTS];
    uint8_t num_report_offsets;

    /* as tag is 4 bits, there can be 16 different tags in global header type */
    item_t globals[16];

    /* as tag is 4 bits, there can be 16 different tags in local header type */
    item_t locals[16];
} parser_state_t;

void route_report(uint8_t *report, int len, uint8_t device_idx, hid_interface_t *iface);

///////////////

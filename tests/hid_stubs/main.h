/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/* Stands in for the firmware's main.h so hid_parser.c and hid_report.c build
   on the host. The report receivers are the test's own recorders (#240). */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "tusb.h"
#include "constants.h"
#include "packet.h"
#include "hid_parser.h"
#include "hid_report.h"

/* Restated from mouse.h and keyboard.h, which pull in the Pico SDK. */
void parse_report_descriptor(hid_interface_t *, uint8_t const *, int);
void extract_data(hid_interface_t *, report_val_t *);
int32_t get_report_value(uint8_t *, int, report_val_t *);
int32_t extract_kbd_data(uint8_t *, int, uint8_t, hid_interface_t *, hid_keyboard_report_t *);
keyboard_t *get_keyboard(hid_interface_t *, uint8_t);

void process_mouse_report(uint8_t *, int, uint8_t, hid_interface_t *);
void process_keyboard_report(uint8_t *, int, uint8_t, hid_interface_t *);
void process_consumer_report(uint8_t *, int, uint8_t, hid_interface_t *);
void process_system_report(uint8_t *, int, uint8_t, hid_interface_t *);

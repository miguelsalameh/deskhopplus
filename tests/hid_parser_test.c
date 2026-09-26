/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/* Host seam for attached-device input (#240): feed a report descriptor to the
   real parser, then input reports to the receivers it registered, and check
   what would reach the selected computer. Built with ASan where available. */

#include <stdio.h>
#include <stdlib.h>

#include "main.h"

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

/* ---- Recording receivers --------------------------------------------- */

static int keyboard_reports, mouse_reports, other_reports;
static hid_keyboard_report_t last_keys;
static int32_t last_x, last_y, last_buttons;

void process_keyboard_report(uint8_t *raw, int len, uint8_t itf, hid_interface_t *iface) {
    if (len < KBD_REPORT_LENGTH)
        return;
    extract_kbd_data(raw, len, itf, iface, &last_keys);
    keyboard_reports++;
}

/* The same reads mouse.c's extract_report_values makes. */
void process_mouse_report(uint8_t *raw, int len, uint8_t itf, hid_interface_t *iface) {
    (void)itf;
    if (iface->uses_report_id)
        raw++, len--;
    last_x       = get_report_value(raw, len, &iface->mouse.move_x);
    last_y       = get_report_value(raw, len, &iface->mouse.move_y);
    last_buttons = get_report_value(raw, len, &iface->mouse.buttons);
    mouse_reports++;
}

void process_consumer_report(uint8_t *raw, int len, uint8_t itf, hid_interface_t *iface) {
    (void)raw, (void)len, (void)itf, (void)iface;
    other_reports++;
}

void process_system_report(uint8_t *raw, int len, uint8_t itf, hid_interface_t *iface) {
    (void)raw, (void)len, (void)itf, (void)iface;
    other_reports++;
}

/* ---- Helpers ----------------------------------------------------------- */

static hid_interface_t iface;

static void mount(const uint8_t *desc, int len) {
    memset(&iface, 0, sizeof(iface));
    keyboard_reports = mouse_reports = other_reports = 0;
    last_x = last_y = last_buttons = 0;
    parse_report_descriptor(&iface, desc, len);
}

/* Dispatch as usb.c's tuh_hid_report_received_cb does for a report-ID interface. */
static void feed(uint8_t *report, int len) {
    uint8_t id = iface.uses_report_id ? report[0] : 0;
    if (id < MAX_REPORTS && iface.report_handler[id])
        iface.report_handler[id](report, len, 0, &iface);
}

/* Appends bytes to a descriptor being built. */
typedef struct {
    uint8_t buf[1024];
    int len;
} desc_t;

static void put(desc_t *d, const uint8_t *bytes, int n) {
    if (d->len + n > (int)sizeof(d->buf)) {
        fprintf(stderr, "descriptor buffer too small\n");
        exit(2);
    }
    memcpy(d->buf + d->len, bytes, n);
    d->len += n;
}
#define PUT(d, ...) put((d), (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))

/* A boot-layout keyboard behind report ID 2; its main items declare no usages. */
static void put_keyboard_id2(desc_t *d) {
    PUT(d, 0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x02,
           0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
           0x75, 0x01, 0x95, 0x08, 0x81, 0x02,            /* modifiers */
           0x95, 0x01, 0x75, 0x08, 0x81, 0x01,            /* reserved */
           0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
           0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, /* keys */
           0xC0);
}

/* The keyboard still decodes: Left Shift + A, B. */
static void check_keyboard_id2(void) {
    uint8_t report[] = {0x02, 0x02, 0x00, 0x04, 0x05, 0, 0, 0, 0};
    keyboard_reports = 0;
    feed(report, sizeof(report));
    CHECK(keyboard_reports == 1);
    CHECK(last_keys.modifier == 0x02);
    CHECK(last_keys.keycode[0] == 0x04);
    CHECK(last_keys.keycode[1] == 0x05);
    CHECK(last_keys.keycode[2] == 0x00);
}

/* A vendor collection behind report ID 1 that fills 100 usage slots first. */
static void put_vendor_id1_prefix(desc_t *d) {
    PUT(d, 0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x01, 0x75, 0x08);
    for (int i = 0; i < 100; i++)
        PUT(d, 0x09, 0x02);
    PUT(d, 0x95, 100, 0x81, 0x02);
}

/* ---- Tests ------------------------------------------------------------- */

/* A main item with no usage of its own takes the last usage declared before
   it, so the third byte here is a second Y, not a second X. */
static void test_elements_past_the_usages_repeat_the_last_usage(void) {
    desc_t d = {0};
    PUT(&d, 0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
            0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
            0x95, 0x03, 0x75, 0x01, 0x81, 0x02,             /* 3 buttons */
            0x95, 0x01, 0x75, 0x05, 0x81, 0x01,             /* padding */
            0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F,
            0x75, 0x08, 0x95, 0x02, 0x81, 0x06,             /* X, Y */
            0x95, 0x01, 0x81, 0x06,                         /* no usage */
            0xC0, 0xC0);
    mount(d.buf, d.len);

    uint8_t report[] = {0x01, 5, 7, 9};
    feed(report, sizeof(report));
    CHECK(mouse_reports == 1);
    CHECK(last_buttons == 1);
    CHECK(last_x == 5);
    CHECK(last_y == 9);
}

/* One main item with three fields but two usages: the third field is Y too. */
static void test_fields_past_one_items_usages_repeat_its_last_usage(void) {
    desc_t d = {0};
    PUT(&d, 0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
            0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
            0x95, 0x03, 0x75, 0x01, 0x81, 0x02,             /* 3 buttons */
            0x95, 0x01, 0x75, 0x05, 0x81, 0x01,             /* padding */
            0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F,
            0x75, 0x08, 0x95, 0x03, 0x81, 0x06,             /* X, Y, Y */
            0xC0, 0xC0);
    mount(d.buf, d.len);

    uint8_t report[] = {0x01, 5, 7, 9};
    feed(report, sizeof(report));
    CHECK(mouse_reports == 1);
    CHECK(last_x == 5);
    CHECK(last_y == 9);
}

/* A report count far past usage storage, after the storage is mostly used. */
static void test_report_count_past_usage_storage_is_contained(void) {
    desc_t d = {0};
    put_vendor_id1_prefix(&d);
    PUT(&d, 0x09, 0x03, 0x96, 0x00, 0x08, 0x81, 0x02, 0xC0); /* count 2048 */
    put_keyboard_id2(&d);
    mount(d.buf, d.len);

    uint8_t vendor[] = {0x01, 0xFF, 0xFF, 0xFF};
    feed(vendor, sizeof(vendor));
    CHECK(keyboard_reports + mouse_reports + other_reports == 0);
    check_keyboard_id2();
}

/* More local usages on one main item than usage storage has left. */
static void test_usages_past_usage_storage_are_contained(void) {
    desc_t d = {0};
    put_vendor_id1_prefix(&d);
    for (int i = 0; i < 200; i++)
        PUT(&d, 0x09, 0x04);
    PUT(&d, 0x95, 0x01, 0x81, 0x02, 0xC0);
    put_keyboard_id2(&d);
    mount(d.buf, d.len);

    uint8_t vendor[] = {0x01, 0xFF, 0xFF, 0xFF};
    feed(vendor, sizeof(vendor));
    CHECK(keyboard_reports + mouse_reports + other_reports == 0);
    check_keyboard_id2();
}

/* Usages used up by earlier items do not starve a later collection. */
static void test_earlier_usages_do_not_starve_a_later_collection(void) {
    desc_t d = {0};
    put_vendor_id1_prefix(&d);
    for (int i = 0; i < 100; i++)
        PUT(&d, 0x09, 0x04);
    PUT(&d, 0x95, 100, 0x81, 0x02, 0xC0);
    PUT(&d, 0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x03, 0x09, 0x01, 0xA1, 0x00,
            0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F,
            0x75, 0x08, 0x95, 0x02, 0x81, 0x06,             /* X, Y */
            0xC0, 0xC0);
    mount(d.buf, d.len);

    uint8_t report[] = {0x03, 5, 7};
    feed(report, sizeof(report));
    CHECK(mouse_reports == 1);
    CHECK(last_x == 5);
    CHECK(last_y == 7);
}

/* A zero-size field with a 32-bit count ends the parse at once, not after
   four billion elements. */
static void test_zero_size_huge_count_parses_promptly(void) {
    desc_t d = {0};
    PUT(&d, 0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x01,
            0x75, 0x00, 0x97, 0xFF, 0xFF, 0xFF, 0xFF, 0x81, 0x02, 0xC0);
    put_keyboard_id2(&d);
    mount(d.buf, d.len);
    check_keyboard_id2();
}

/* A report cut off inside a multi-byte field is not read past its end. */
static void test_truncated_field_is_not_read_past_the_report(void) {
    desc_t d = {0};
    PUT(&d, 0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
            0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x95, 0x08, 0x75, 0x01, 0x81, 0x02,
            0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F,
            0x75, 0x10, 0x95, 0x02, 0x81, 0x06,             /* 16-bit X, Y */
            0xC0, 0xC0);
    mount(d.buf, d.len);

    uint8_t *report = malloc(2);                    /* buttons, X low byte */
    report[0] = 0x01, report[1] = 0x05;
    feed(report, 2);
    free(report);
    CHECK(mouse_reports == 1);
}

/* A descriptor whose first main items declare no usages at all. */
static void test_main_items_without_usages_decode(void) {
    desc_t d = {0};
    put_keyboard_id2(&d);
    mount(d.buf, d.len);
    check_keyboard_id2();
}

int main(void) {
    test_elements_past_the_usages_repeat_the_last_usage();
    test_fields_past_one_items_usages_repeat_its_last_usage();
    test_report_count_past_usage_storage_is_contained();
    test_usages_past_usage_storage_are_contained();
    test_main_items_without_usages_decode();
    test_earlier_usages_do_not_starve_a_later_collection();
    test_zero_size_huge_count_parses_promptly();
    test_truncated_field_is_not_read_past_the_report();

    if (failures) {
        fprintf(stderr, "hid_parser_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("hid_parser_test: ok\n");
    return 0;
}

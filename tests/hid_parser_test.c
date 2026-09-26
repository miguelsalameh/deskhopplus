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

static int keyboard_reports, mouse_reports, consumer_reports, system_reports;
static hid_keyboard_report_t last_keys;
static int32_t last_x, last_y, last_buttons;
static uint16_t last_consumer;
static uint8_t last_system;

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
    uint8_t out[CONSUMER_CONTROL_LENGTH];
    (void)itf;
    if (!extract_consumer_report(raw, len, iface, out))
        return;
    last_consumer = (uint16_t)(out[0] | out[1] << 8);
    consumer_reports++;
}

void process_system_report(uint8_t *raw, int len, uint8_t itf, hid_interface_t *iface) {
    (void)itf;
    if (!extract_system_report(raw, len, iface, &last_system))
        return;
    system_reports++;
}

/* ---- Helpers ----------------------------------------------------------- */

static hid_interface_t iface;

static void mount(const uint8_t *desc, int len) {
    memset(&iface, 0, sizeof(iface));
    keyboard_reports = mouse_reports = consumer_reports = system_reports = 0;
    last_x = last_y = last_buttons = 0;
    last_consumer = 0;
    last_system = 0;
    parse_report_descriptor(&iface, desc, len);
}

/* Hands a report to the firmware's router, as usb.c does for a report-ID interface. */
static void feed(uint8_t *report, int len) {
    route_report(report, len, 0, &iface);
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

/* A boot-layout keyboard behind a report ID; its main items declare no usages. */
static void put_keyboard(desc_t *d, uint8_t id) {
    PUT(d, 0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, id,
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

/* A relative mouse behind a report ID: 3 buttons, 8-bit X and Y. */
static void put_mouse(desc_t *d, uint8_t id) {
    PUT(d, 0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, id, 0x09, 0x01, 0xA1, 0x00,
           0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
           0x95, 0x03, 0x75, 0x01, 0x81, 0x02,             /* 3 buttons */
           0x95, 0x01, 0x75, 0x05, 0x81, 0x01,             /* padding */
           0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F,
           0x75, 0x08, 0x95, 0x02, 0x81, 0x06,             /* X, Y */
           0xC0, 0xC0);
}

/* Power, sleep and wake, then 5 bits of padding, closing the collection. */
static void put_system_body(desc_t *d) {
    PUT(d, 0x19, 0x81, 0x29, 0x83, 0x15, 0x00, 0x25, 0x01,
           0x75, 0x01, 0x95, 0x03, 0x81, 0x02,
           0x95, 0x05, 0x81, 0x01, 0xC0);
}

/* A 16-bit consumer-control array behind a report ID. */
static void put_consumer(desc_t *d, uint8_t id) {
    PUT(d, 0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, id,
           0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
           0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0);
}

/* System power, sleep and wake bits behind a report ID. */
static void put_system(desc_t *d, uint8_t id) {
    PUT(d, 0x05, 0x01, 0x09, 0x80, 0xA1, 0x01, 0x85, id);
    put_system_body(d);
}

/* The same system bits on an interface that declares no report ID. */
static void put_system_no_id(desc_t *d) {
    PUT(d, 0x05, 0x01, 0x09, 0x80, 0xA1, 0x01);
    put_system_body(d);
}

/* The Cherry KC 6000 Slim's consumer collection: nine 1-bit controls, then
   7 bits of padding. Its report has no ID; id != 0 adds one for comparison. */
static void put_consumer_bits(desc_t *d, uint8_t id) {
    PUT(d, 0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01);
    if (id)
        PUT(d, 0x85, id);
    PUT(d, 0x15, 0x00, 0x25, 0x01,
           0x75, 0x01, 0x95, 0x09,
           0x09, 0xCD, 0x09, 0xB5, 0x09, 0xB6, 0x09, 0xB8, 0x09, 0xE2,
           0x09, 0xEA, 0x09, 0xE9, 0x0A, 0x23, 0x02, 0x0A, 0x92, 0x01,
           0x81, 0x02, 0x95, 0x07, 0x81, 0x01, 0xC0);
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
    put_keyboard(&d, 0x02);
    mount(d.buf, d.len);

    uint8_t vendor[] = {0x01, 0xFF, 0xFF, 0xFF};
    feed(vendor, sizeof(vendor));
    CHECK(keyboard_reports + mouse_reports + consumer_reports + system_reports == 0);
    check_keyboard_id2();
}

/* More local usages on one main item than usage storage has left. */
static void test_usages_past_usage_storage_are_contained(void) {
    desc_t d = {0};
    put_vendor_id1_prefix(&d);
    for (int i = 0; i < 200; i++)
        PUT(&d, 0x09, 0x04);
    PUT(&d, 0x95, 0x01, 0x81, 0x02, 0xC0);
    put_keyboard(&d, 0x02);
    mount(d.buf, d.len);

    uint8_t vendor[] = {0x01, 0xFF, 0xFF, 0xFF};
    feed(vendor, sizeof(vendor));
    CHECK(keyboard_reports + mouse_reports + consumer_reports + system_reports == 0);
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
    put_keyboard(&d, 0x02);
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
    put_keyboard(&d, 0x02);
    mount(d.buf, d.len);
    check_keyboard_id2();
}

/* Report IDs at both ends of the eight-bit range, and at 23 and 24, reach the
   receiver their descriptor declared. */
static void test_report_ids_across_the_eight_bit_range_route(void) {
    desc_t d = {0};
    put_system(&d, 1);
    put_consumer(&d, 23);
    put_keyboard(&d, 24);
    put_mouse(&d, 255);
    mount(d.buf, d.len);

    uint8_t sys[] = {1, 0x04};
    feed(sys, sizeof(sys));
    CHECK(system_reports == 1);
    CHECK(last_system == 0x04);

    uint8_t cc[] = {23, 0x23, 0x02};
    feed(cc, sizeof(cc));
    CHECK(consumer_reports == 1);
    CHECK(last_consumer == 0x223);

    uint8_t kbd[] = {24, 0x02, 0x00, 0x04, 0, 0, 0, 0, 0};
    feed(kbd, sizeof(kbd));
    CHECK(keyboard_reports == 1);
    CHECK(last_keys.modifier == 0x02);
    CHECK(last_keys.keycode[0] == 0x04);

    uint8_t mouse[] = {255, 0x01, 5, 7};
    feed(mouse, sizeof(mouse));
    CHECK(mouse_reports == 1);
    CHECK(last_x == 5);
    CHECK(last_y == 7);

    CHECK(keyboard_reports + mouse_reports + consumer_reports + system_reports == 4);
}

/* A report whose ID the descriptor never declared produces no input. */
static void test_undeclared_report_id_is_dropped(void) {
    desc_t d = {0};
    put_keyboard(&d, 24);
    put_mouse(&d, 255);
    mount(d.buf, d.len);

    uint8_t low[] = {3, 0x02, 0x00, 0x04, 0, 0, 0, 0, 0};
    uint8_t high[] = {200, 0x02, 0x00, 0x04, 0, 0, 0, 0, 0};
    feed(low, sizeof(low));
    feed(high, sizeof(high));
    CHECK(keyboard_reports + mouse_reports + consumer_reports + system_reports == 0);
}

/* An interface parses at most MAX_REPORT_LAYOUTS distinct report IDs; the
   next one is dropped whatever its value, and the earlier ones still route. */
static void test_report_layouts_past_the_limit_are_dropped(void) {
    desc_t d = {0};
    for (int id = 1; id <= MAX_REPORT_LAYOUTS; id++)
        put_system(&d, (uint8_t)id);
    put_keyboard(&d, 255);
    mount(d.buf, d.len);

    uint8_t sys[] = {MAX_REPORT_LAYOUTS, 0x01};
    feed(sys, sizeof(sys));
    CHECK(system_reports == 1);

    uint8_t kbd[] = {255, 0x02, 0x00, 0x04, 0, 0, 0, 0, 0};
    feed(kbd, sizeof(kbd));
    CHECK(keyboard_reports == 0);
}

/* An empty report on a report-ID interface has no ID byte to read. */
static void test_empty_report_is_dropped(void) {
    desc_t d = {0};
    put_keyboard(&d, 0x02);
    mount(d.buf, d.len);

    uint8_t *report = malloc(1);
    feed(report + 1, 0);                            /* one past the end, as ASan sees it */
    free(report);
    CHECK(keyboard_reports == 0);
}

/* A consumer report without an ID starts its payload at byte 0. */
static void test_consumer_bits_without_id_decodes(void) {
    desc_t d = {0};
    put_consumer_bits(&d, 0);
    mount(d.buf, d.len);

    uint8_t play[] = {0x01, 0x00};
    feed(play, sizeof(play));
    CHECK(consumer_reports == 1);
    CHECK(last_consumer == 0xCD);

    uint8_t calculator[] = {0x00, 0x01};
    feed(calculator, sizeof(calculator));
    CHECK(consumer_reports == 2);
    CHECK(last_consumer == 0x192);

    uint8_t volume_up[] = {0x40};                   /* cut after byte 0: its bits still count */
    feed(volume_up, sizeof(volume_up));
    CHECK(consumer_reports == 3);
    CHECK(last_consumer == 0xE9);
}

/* The same bitmap behind a report ID reads its bits after the ID byte. */
static void test_consumer_bits_with_id_decode_after_the_id(void) {
    desc_t d = {0};
    put_consumer_bits(&d, 5);
    mount(d.buf, d.len);

    uint8_t calculator[] = {5, 0x00, 0x01};
    feed(calculator, sizeof(calculator));
    CHECK(consumer_reports == 1);
    CHECK(last_consumer == 0x192);
}

/* A 16-bit consumer array without an ID is its two-byte payload. */
static void test_consumer_array_without_id_decodes(void) {
    desc_t d = {0};
    PUT(&d, 0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01,
            0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
            0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0);
    mount(d.buf, d.len);

    uint8_t home[] = {0x23, 0x02};
    feed(home, sizeof(home));
    CHECK(consumer_reports == 1);
    CHECK(last_consumer == 0x223);
}

/* A system report without an ID is its one-byte payload. */
static void test_system_without_id_decodes(void) {
    desc_t d = {0};
    put_system_no_id(&d);
    mount(d.buf, d.len);

    uint8_t sleep[] = {0x02};
    feed(sleep, sizeof(sleep));
    CHECK(system_reports == 1);
    CHECK(last_system == 0x02);
}

/* A report with no payload, or cut off inside its control, makes no control. */
static void test_empty_and_truncated_controls_are_dropped(void) {
    desc_t d = {0};
    put_consumer_bits(&d, 0);
    mount(d.buf, d.len);
    uint8_t *report = malloc(1);
    feed(report + 1, 0);                            /* one past the end, as ASan sees it */
    CHECK(consumer_reports == 0);

    d.len = 0;
    put_system_no_id(&d);
    mount(d.buf, d.len);
    feed(report + 1, 0);
    CHECK(system_reports == 0);

    d.len = 0;
    put_system(&d, 1);
    put_consumer(&d, 23);
    mount(d.buf, d.len);
    report[0] = 1;
    feed(report, 1);                                /* ID only */
    report[0] = 23;
    feed(report, 1);
    CHECK(system_reports + consumer_reports == 0);

    uint8_t half[] = {23, 0xE9};                    /* 16-bit control, one byte */
    feed(half, sizeof(half));
    CHECK(consumer_reports == 0);
    free(report);
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
    test_report_ids_across_the_eight_bit_range_route();
    test_undeclared_report_id_is_dropped();
    test_report_layouts_past_the_limit_are_dropped();
    test_empty_report_is_dropped();
    test_consumer_bits_without_id_decodes();
    test_consumer_bits_with_id_decode_after_the_id();
    test_consumer_array_without_id_decodes();
    test_system_without_id_decodes();
    test_empty_and_truncated_controls_are_dropped();

    if (failures) {
        fprintf(stderr, "hid_parser_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("hid_parser_test: ok\n");
    return 0;
}

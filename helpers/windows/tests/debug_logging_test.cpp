/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

/*
 * Debug logging's decisions (#271): the setting as a marker file, and what the
 * helper does with helper.log at start. The tray tick and the real file open
 * stay a hardware check; this is the part that runs on any machine.
 *
 * House style: an assertion macro, a main, a printed failure line, a non-zero
 * exit (ADR-0006).
 */

#include <cstdio>
#include <filesystem>

#include "debug_logging.h"

using namespace deskhop::debug_logging;
namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond, what)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++failures;                                                      \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, (what));      \
        }                                                                    \
    } while (0)

static void the_setting_round_trips_through_its_marker(const fs::path &dir) {
    CHECK(!is_on(dir), "off with no marker");
    CHECK(set(dir, true), "turning on is saved");
    CHECK(is_on(dir), "on after turning on");
    CHECK(set(dir, true), "turning on twice is fine");
    CHECK(set(dir, false), "turning off is saved");
    CHECK(!is_on(dir), "off after turning off");
    CHECK(set(dir, false), "turning off twice is fine");
}

static void the_log_is_trimmed_only_when_on_and_over_5_mb() {
    const std::uintmax_t five_mb = 5u * 1024u * 1024u;
    CHECK(at_start(false, 0) == LogStart::DoNotOpen, "off opens nothing");
    CHECK(at_start(false, five_mb + 1) == LogStart::DoNotOpen, "off never trims");
    CHECK(at_start(true, 0) == LogStart::Append, "on with no log appends");
    CHECK(at_start(true, five_mb) == LogStart::Append, "on at exactly 5 MB keeps the log");
    CHECK(at_start(true, five_mb + 1) == LogStart::Empty, "on over 5 MB empties the log");
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "deskhop_debug_logging_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    the_setting_round_trips_through_its_marker(dir);
    the_log_is_trimmed_only_when_on_and_over_5_mb();

    fs::remove_all(dir);
    if (failures == 0) std::printf("debug_logging_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}

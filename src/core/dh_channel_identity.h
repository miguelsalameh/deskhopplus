/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Derek Reynolds */

#pragma once

/* USB identity shared by firmware and helpers: pid.codes/1209/D35C (#14).
   Config mode reboots under the same identity with a different interface set,
   so the helpers tell the modes apart by usage, never by VID/PID (#20). */
#define DH_CHANNEL_VENDOR_ID 0x1209
#define DH_CHANNEL_PRODUCT_ID 0xD35C
#define DH_CHANNEL_USAGE_PAGE 0xFF00
/* Normal-mode channel n uses this base usage + n. */
#define DH_CHANNEL_USAGE 0x20
/* Config mode only: the web config API, and the one helper channel beside it. */
#define DH_CHANNEL_CONFIG_API_USAGE 0x10
#define DH_CHANNEL_CONFIG_USAGE 0x30
/* No report ID: one report is one full-speed interrupt packet. */
#define DH_CHANNEL_REPORT_SIZE 64

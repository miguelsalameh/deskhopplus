// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Derek Reynolds

import DHCore

/*
 * How the helper finds the device — by USB identifier, serial and vendor
 * usage page, then channel usage. Paths are not stable across reconnects, and
 * the interface disappearing and returning is normal operation here: entering
 * config mode reboots the device with a *different* interface set for up to
 * five minutes, then reboots back (ADR-0001). Both modes share one USB
 * identity, so the usage says which mode a collection belongs to (#20).
 */
public enum ChannelIdentity {
    /* pid.codes/1209/D35C, in both modes. */
    public static let vendorID = Int(DH_CHANNEL_VENDOR_ID)
    public static let productID = Int(DH_CHANNEL_PRODUCT_ID)

    /*
     * Normal mode has two channels, at `usage` and `usage + 1`. Config mode
     * has its config API and one helper channel, each at its own usage.
     */
    public static let usagePage = Int(DH_CHANNEL_USAGE_PAGE)
    public static let usage = Int(DH_CHANNEL_USAGE)
    public static let configApiUsage = Int(DH_CHANNEL_CONFIG_API_USAGE)
    public static let configUsage = Int(DH_CHANNEL_CONFIG_USAGE)

    public enum Collection: Equatable {
        case normalChannel(UInt8)
        case configApi
        case configChannel

        /// The mode a collection belongs to.
        public var mode: DeviceIdentity {
            if case .normalChannel = self { return .normal }
            return .configMode
        }
    }

    /// What a vendor-page collection is, from its usage; nil for anything else.
    public static func collection(usage value: Int?) -> Collection? {
        guard let value else { return nil }
        if (usage..<usage + Int(DH_SESSION_CHANNEL_COUNT)).contains(value) {
            return .normalChannel(UInt8(value - usage))
        }
        if value == configApiUsage { return .configApi }
        if value == configUsage { return .configChannel }
        return nil
    }

    /* One report is one full-speed packet, and the framing layer owns every
       byte of it: no report ID, so no byte is spent on one. */
    public static let reportSize = Int(DH_CHANNEL_REPORT_SIZE)

    /* What this helper asks for in its hello. The device answers with the
       effective values, which are what the session actually runs with. */
    public static let requestedChannelCount = UInt8(DH_SESSION_CHANNEL_COUNT)
    public static let requestedMaxChunk = UInt16(DH_SESSION_MAX_CHUNK)
}

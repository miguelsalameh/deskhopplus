// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Derek Reynolds

import DeskhopChannel

/* Both modes share one USB identity (#20), so the usage alone says which. */
let channelIdentityTests: [(String, () throws -> Void)] = [
    ("the usage names the mode and channel", {
        Check.equal(ChannelIdentity.collection(usage: 0x20), .normalChannel(0), "normal channel 0")
        Check.equal(ChannelIdentity.collection(usage: 0x21), .normalChannel(1), "normal channel 1")
        Check.equal(ChannelIdentity.collection(usage: 0x10), .configApi, "config API")
        Check.equal(ChannelIdentity.collection(usage: 0x30), .configChannel, "config channel")
        Check.equal(ChannelIdentity.collection(usage: 0x22), nil, "past the channel count")
        Check.equal(ChannelIdentity.collection(usage: nil), nil, "no usage")
        Check.equal(ChannelIdentity.Collection.normalChannel(1).mode, .normal, "normal mode")
        Check.equal(ChannelIdentity.Collection.configApi.mode, .configMode, "config API mode")
        Check.equal(ChannelIdentity.Collection.configChannel.mode, .configMode, "config channel mode")
    }),
]

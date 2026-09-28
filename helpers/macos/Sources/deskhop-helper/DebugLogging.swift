// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Derek Reynolds

import Foundation

/*
 * Debug logging (#269, #270): whether this helper writes its log at all.
 *
 * Off by default, and off writes nothing. A machine-local preference like
 * start at login, kept in this machine's user defaults; absent means off, so
 * a helper updated from an earlier release starts off too. The menu's tick
 * reads and sets it; `HelperRuntime.note` writes through it.
 *
 * A class, so that the menu and the log writer share one state and a toggle
 * takes effect on the next line.
 */
final class DebugLogging {
    private static let key = "DebugLogging"
    /// The log trim's threshold.
    private static let trimAbove: UInt64 = 5 * 1024 * 1024

    private let defaults: UserDefaults
    private let sink: FileHandle

    /// `sink` is stderr in the helper: launchd opens the log file and hands it
    /// over there, so the path is never reopened here.
    init(defaults: UserDefaults = .standard, sink: FileHandle = .standardError) {
        self.defaults = defaults
        self.sink = sink
        isEnabled = defaults.bool(forKey: Self.key)
    }

    var isEnabled: Bool {
        didSet { defaults.set(isEnabled, forKey: Self.key) }
    }

    /// One finished log line, newline included; dropped when off.
    func write(_ line: String) {
        guard isEnabled else { return }
        sink.write(Data(line.utf8))
    }

    /*
     * The log trim, at start: empty the log when debug logging is on and it
     * has grown past 5 MB. Only a regular file is trimmed — under `swift run`
     * stderr is a terminal and there is nothing to trim. Safe because launchd
     * opens the file with O_APPEND (checked on the running job, #270): the
     * next write lands at the new end, not at the old offset.
     */
    func trimLog() {
        guard isEnabled else { return }
        var info = stat()
        let fd = sink.fileDescriptor
        guard fstat(fd, &info) == 0, (info.st_mode & S_IFMT) == S_IFREG,
              UInt64(info.st_size) > Self.trimAbove else { return }
        ftruncate(fd, 0)
    }
}

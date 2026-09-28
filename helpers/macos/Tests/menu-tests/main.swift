// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Derek Reynolds

import AppKit
import DeskhopChannel
import Foundation

func check(_ condition: @autoclosure () -> Bool, _ message: String) {
    if !condition() { fatalError(message) }
}

let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
defer { try? FileManager.default.removeItem(at: directory) }
let plist = directory.appendingPathComponent("com.deskhopplus.helper.plist")
let login = LaunchAtLogin(plist: plist, executable: "/Applications/My Helper/deskhop-helper")
check(!login.isEnabled, "a foreground launch does not opt into login startup")
try login.setEnabled(true)
check(login.isEnabled, "enabling registers the helper for login")
let registered = try PropertyListSerialization.propertyList(from: Data(contentsOf: plist), format: nil) as! [String: Any]
check(registered["ProgramArguments"] as? [String] == ["/Applications/My Helper/deskhop-helper"], "the executable path is one argument, including spaces")
check(registered["RunAtLoad"] as? Bool == true, "login starts the helper")
// The documented install and the job Start at login writes must agree on when
// launchd brings the helper back: after a crash, never after Quit (#190). The
// relative path holds because tools/macos-checks/menu-tests.sh runs from the
// repo root.
let shipped = try PropertyListSerialization.propertyList(
    from: Data(contentsOf: URL(fileURLWithPath: "helpers/macos/LaunchAgent/com.deskhopplus.helper.plist")),
    format: nil) as! [String: Any]
for (name, job) in [("shipped", shipped), ("written", registered)] {
    check(job["KeepAlive"] as? [String: Bool] == ["SuccessfulExit": false],
          "the \(name) job restarts the helper after a crash, not after Quit")
}
let original = try Data(contentsOf: plist)
try login.setEnabled(false)
check(!login.isEnabled, "disabling removes the login registration")
try login.setEnabled(true)
let restored = try Data(contentsOf: plist)
check(restored == original, "a toggle preserves the installed job")
print("Login registration checks passed")

let menuBar = MenuBar(login: login)
let menu = NSMenu()
for state in HelperState.allCases {
    menuBar.show(state: state)
    menuBar.menuNeedsUpdate(menu)
    // The literal, not DH_VERSION_MAJOR spelled back out: this is the release
    // the helper claims to be, and it moves with src/core/dh_version.h (#199).
    check(menu.items[0].title == "DeskHopPlus Helper 1.1" && !menu.items[0].isEnabled,
          "the first row names the helper and its release, greyed")
    check(menu.items[1].isSeparatorItem, "the release row stands apart from the state")
    let words = menu.items.dropFirst(2).prefix { !$0.isSeparatorItem }.map(\.title).joined(separator: " ")
    check(words == state.message ?? "Waiting for the device", "the menu must preserve the shared state's remedy")
    check(MenuBar.tooltip(state: state, placementProblem: nil, notice: nil).hasPrefix("DeskHopPlus Helper"),
          "with an icon-only title, the tooltip is what names the helper")
    check(MenuBar.tooltip(state: state, placementProblem: nil, notice: nil).contains(state.message ?? "Waiting for the device"),
          "the words moved out of the title into the tooltip, not out of sight")
    check(menu.items.last?.title == "Quit DeskHopPlus Helper", "the Quit row names the product (#232)")
}
// The three looks (#208): the shape carries the state, and the words stay one
// hover away, so #38's "in words, not a colour" still holds.
check(MenuBar.look(for: .connected, questionWaiting: false) == .paired, "paired is the solid glyph")
check(MenuBar.look(for: .connectedConfigMode, questionWaiting: false) == .paired,
      "a live config-mode session is paired")
for state in [HelperState.quiet, .deviceAbsent, .deviceInConfigMode] {
    check(MenuBar.look(for: state, questionWaiting: false) == .off, "\(state) is off, not a fault")
}
for state in [HelperState.notPaired, .versionIncompatible, .listenerDetected, .boardIdentityChanged,
              .reconnectingRepeatedly] {
    check(MenuBar.look(for: state, questionWaiting: false) == .attention, "\(state) names something to do")
}
check(MenuBar.look(for: .connected, questionWaiting: true) == .attention, "a waiting file question is attention")
check(MenuBar.look(for: .quiet, questionWaiting: true) == .attention, "a question outranks off")
check(HelperState.versionIncompatible.message!.contains("update the helper"), "incompatible versions name the remedy")
// The suffix beside the icon, by priority: the question, the receive, a
// complaint, good news, a send. Empty when nothing is happening.
check(MenuBar.suffix(question: false, progress: nil, warning: false, news: false, sending: false) == "", "an idle title is the icon alone")
check(MenuBar.suffix(question: true, progress: (25, 100), warning: true, news: false, sending: true) == "⬇ files?", "a question outranks everything")
check(MenuBar.suffix(question: false, progress: (25, 100), warning: true, news: false, sending: true) == "⬇ 25%", "a receive shows its percent")
check(MenuBar.suffix(question: false, progress: (0, 0), warning: false, news: false, sending: false) == "", "a zero total is not a receive")
check(MenuBar.suffix(question: false, progress: nil, warning: true, news: false, sending: true) == "⚠", "a complaint outranks a send")
check(MenuBar.suffix(question: false, progress: nil, warning: false, news: false, sending: true) == "⬆", "a send is visible while it runs")
// "Paired" is good news, not a complaint (#268).
check(MenuBar.suffix(question: false, progress: nil, warning: false, news: true, sending: true) == "✓", "good news outranks a send")
check(MenuBar.suffix(question: false, progress: nil, warning: true, news: true, sending: false) == "⚠", "a complaint outranks good news")
// Good news stays up for minutes, so it reads as an event with its time,
// not as a live status (#275).
let at1506 = ISO8601DateFormatter().date(from: "2026-09-27T15:06:00Z")!
check(MenuBar.newsRow("Paired", at: at1506, timeZone: TimeZone(identifier: "UTC")!,
                      locale: Locale(identifier: "en_GB")) == "Paired at 15:06",
      "a news row carries the local time it happened")
menuBar.show(state: .listenerDetected)
menuBar.menuNeedsUpdate(menu)
check(menu.items.filter { !$0.isSeparatorItem }.allSatisfy { $0.title.count <= 65 }, "long remedies wrap into readable lines")
menuBar.show(progress: (25, 100))
menuBar.menuNeedsUpdate(menu)
check(menu.items.contains { $0.title.contains("25%") }, "receiving progress is displayed")
// An open menu is never rebuilt, so the line itself must follow the transfer (#262).
menuBar.show(progress: (50, 100))
check(menu.items.contains { $0.title.contains("50%") }, "an open menu's progress line follows the transfer")
menuBar.show(progress: nil)
check(menu.items.contains { $0.title == "No longer receiving" }, "a transfer that ends under the open menu says so")
check(menu.items.contains { $0.title == "Cancel this transfer" && !$0.isEnabled }, "and its Cancel greys out")
menuBar.show(progress: (10, 100))
check(menu.items.contains { $0.title == "Cancel this transfer" && $0.isEnabled },
      "a new transfer under the same open menu can be cancelled again")
menuBar.show(progress: (25, 100))
menuBar.menuNeedsUpdate(menu)
check(menu.items.contains { $0.title == "Cancel this transfer" && $0.isEnabled }, "receiving can be cancelled")
check(menu.items.contains { $0.title == "Start at login" }, "the login action is discoverable")
print("Menu state and action checks passed")

let startup = menu.items.first { $0.title == "Start at login" }!
_ = (startup.target as! NSObject).perform(startup.action!)
check(!login.isEnabled, "the menu action disables login registration")
menuBar.menuNeedsUpdate(menu)
check(menu.items.first { $0.title == "Start at login" }!.state == .off, "the menu reads back login registration")
_ = (startup.target as! NSObject).perform(startup.action!)
check(login.isEnabled, "the menu action enables login registration")
// A conflicting backup must not destroy either job or report success.
let backup = plist.appendingPathExtension("disabled")
try original.write(to: backup)
var refusedConflict = false
do { try login.setEnabled(false) } catch { refusedConflict = true }
check(refusedConflict && login.isEnabled, "a failed toggle preserves the enabled job")
let unchanged = try Data(contentsOf: plist)
check(unchanged == original, "a failed toggle does not damage registration")
menuBar.show(placementProblem: "Cursor placement unavailable — check the display layout.")
menuBar.menuNeedsUpdate(menu)
check(menu.items.contains { $0.title.contains("Cursor placement unavailable") }, "placement degradation has a visible remedy")
check(menu.items[2].title.contains("Another program"), "placement status does not replace the listener remedy")
menuBar.show(placementProblem: nil)
menuBar.menuNeedsUpdate(menu)
check(!menu.items.contains { $0.title.contains("Cursor placement unavailable") }, "successful placement clears its warning")
// The other computer's helper, as the board last said, while this helper is
// connected; nothing once it is not (#275).
func rows() -> [String] { menuBar.menuNeedsUpdate(menu); return menu.items.map(\.title) }
menuBar.show(state: .connected)
menuBar.show(peerConnected: false)
check(rows().contains("Other computer not connected"), "a missing peer helper is said")
menuBar.show(peerConnected: true)
check(rows().contains("Other computer connected"), "a connected peer helper is said")
menuBar.show(state: .listenerDetected)
check(rows().contains("Other computer connected"), "a live session with a warning keeps the status")
check(!rows().contains("Other computer not connected"), "the status replaces itself")
menuBar.show(state: .deviceAbsent)
check(!rows().contains { $0.hasPrefix("Other computer") }, "without a board there is no peer status")
menuBar.show(state: .connected)
check(!rows().contains { $0.hasPrefix("Other computer") }, "a new session waits to be told")
check(MenuBar.tooltip(state: .connected, placementProblem: nil, notice: nil,
                      peer: MenuBar.peerRow(true)).contains("Other computer connected"),
      "the tooltip carries the peer status")
menuBar.show(news: "Paired")
menuBar.menuNeedsUpdate(menu)
check(menu.items.contains { $0.title.hasPrefix("Paired at ") }, "the news row in the menu carries its time (#275)")
menuBar.clearNotice()
print("Login actions, failure preservation, and placement checks passed")

// A downloaded .app never moved with Finder runs from a hidden, randomised
// copy (App Translocation, #206). A job written with that path starts nothing
// at the next login, so it is refused before anything is written, and the
// menu says what to do instead of pointing at ~/Library/LaunchAgents.
let translocatedPlist = directory.appendingPathComponent("translocated.plist")
let translocated = LaunchAtLogin(
    plist: translocatedPlist,
    executable: "/private/var/folders/by/T/AppTranslocation/286E9AE6/d/deskhopplus-helper.app/Contents/MacOS/deskhopplus-helper")
var refusedTranslocation = false
do { try translocated.setEnabled(true) } catch is LaunchAtLogin.Translocated { refusedTranslocation = true }
check(refusedTranslocation && !translocated.isEnabled, "a translocated app is refused a login job, and none is written")
let translocatedMenu = NSMenu()
let translocatedBar = MenuBar(login: translocated)
translocatedBar.menuNeedsUpdate(translocatedMenu)
let translocatedStartup = translocatedMenu.items.first { $0.title == "Start at login" }!
_ = (translocatedStartup.target as! NSObject).perform(translocatedStartup.action!)
translocatedBar.menuNeedsUpdate(translocatedMenu)
let translocatedWords = translocatedMenu.items.map(\.title).joined(separator: " ")
check(translocatedWords.contains("In Finder, move deskhopplus-helper.app to your Applications folder"), "the refusal names the remedy")
check(!translocatedWords.contains("LaunchAgents"), "the refusal does not send the user to the plist folder")
print("Translocation refusal checks passed")

// Debug logging (#270): a tick on the presence, off by default, remembered
// across restarts in this machine's user defaults.
let suite = "menu-tests.\(UUID().uuidString)"
defer { UserDefaults().removePersistentDomain(forName: suite) }
let logFile = directory.appendingPathComponent("helper.log")
FileManager.default.createFile(atPath: logFile.path, contents: nil)
let debug = DebugLogging(defaults: UserDefaults(suiteName: suite)!,
                         sink: try FileHandle(forWritingTo: logFile))
let debugBar = MenuBar(login: login, debug: debug)
let debugMenu = NSMenu()
func debugItem() -> NSMenuItem { debugBar.menuNeedsUpdate(debugMenu); return debugMenu.items.first { $0.title == "Debug logging" }! }
check(debugItem().state == .off && !debug.isEnabled, "debug logging is off by default")
let tick = debugItem()
_ = (tick.target as! NSObject).perform(tick.action!)
check(debug.isEnabled && debugItem().state == .on, "selecting the tick turns debug logging on at once")
check(DebugLogging(defaults: UserDefaults(suiteName: suite)!, sink: .nullDevice).isEnabled,
      "a restarted helper reads back the choice")
_ = (tick.target as! NSObject).perform(tick.action!)
check(!debug.isEnabled && debugItem().state == .off, "selecting it again turns debug logging off")
check(!DebugLogging(defaults: UserDefaults(suiteName: suite)!, sink: .nullDevice).isEnabled,
      "a restarted helper reads back off")
print("Debug logging tick checks passed")

// Off writes nothing; on writes the line as given.
func logSize() -> UInt64 { (try? FileManager.default.attributesOfItem(atPath: logFile.path)[.size] as? UInt64) ?? 0 }
debug.write("dropped\n")
check(logSize() == 0, "debug logging off writes nothing")
debug.isEnabled = true
debug.write("kept\n")
check((try? String(contentsOf: logFile)) == "kept\n", "debug logging on writes the line")
// The log trim: over 5 MB at start with debug logging on empties the file;
// 5 MB or less, or debug logging off, leaves it alone.
func fill(_ bytes: UInt64) throws { try Data(count: Int(bytes)).write(to: logFile) }
func trimmed(_ bytes: UInt64, on: Bool) throws -> UInt64 {
    try fill(bytes)
    let handle = try FileHandle(forWritingTo: logFile)
    defer { try? handle.close() }
    let trimming = DebugLogging(defaults: UserDefaults(suiteName: suite)!, sink: handle)
    trimming.isEnabled = on
    trimming.trimLog()
    return logSize()
}
check((try? trimmed(5 * 1024 * 1024 + 1, on: true)) == 0, "a log over 5 MB is emptied at start")
check((try? trimmed(5 * 1024 * 1024, on: true)) == 5 * 1024 * 1024, "a log of 5 MB is kept")
check((try? trimmed(6 * 1024 * 1024, on: false)) == 6 * 1024 * 1024, "debug logging off never touches the log")
// Under `swift run` stderr is a terminal, not a file: the tick still gates,
// and there is nothing to trim.
let pipe = Pipe()
let foreground = DebugLogging(defaults: UserDefaults(suiteName: suite)!, sink: pipe.fileHandleForWriting)
foreground.isEnabled = true
foreground.trimLog()
foreground.write("line\n")
foreground.isEnabled = false
foreground.write("dropped\n")
try pipe.fileHandleForWriting.close()
check(String(decoding: pipe.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self) == "line\n",
      "a stderr that is not a file is gated and never trimmed")
print("Debug logging gate and trim checks passed")

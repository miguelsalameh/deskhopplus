# What tells a Windows app that a Modern Standby PC went to sleep

Research for [issue #292](https://github.com/myn/deskhopplus/issues/292), under
[#286](https://github.com/myn/deskhopplus/issues/286). Feeds
[#293](https://github.com/myn/deskhopplus/issues/293). Researched 2026-09-29.

**Sourcing.** Claims cite Microsoft Learn or the source code of real apps, at a
fixed commit. A claim with no mark is stated by the cited source.
**[INFERENCE]** marks a conclusion drawn from sources, with the reasoning.
**[UNVERIFIED]** marks something no primary source settles. Forum posts are
pointers only and are marked as such.

---

## Answer

**Signal: `RegisterSuspendResumeNotification(hwnd, DEVICE_NOTIFY_WINDOW_HANDLE)`.**
The helper then gets `WM_POWERBROADCAST` with `PBT_APMSUSPEND` on standby
entry, and `PBT_APMRESUMEAUTOMATIC` on exit. On a Modern Standby PC, Windows
sends these only to a process that registered. That is why `PBT_APMSUSPEND`
"does not arrive" for most apps.

What it tells us:

- **Entry.** The Desktop Activity Moderator (DAM) sends `PBT_APMSUSPEND` just
  before it suspends desktop apps. It suspends each process "after a few
  seconds delay". The DAM phase comes after the screen is off, after the sleep
  timeout, and after all power requests have cleared. So a display-off timeout
  alone does **not** send it. Start > Sleep, the power button, and an idle
  sleep timeout **do**.
- **Exit.** `PBT_APMRESUMEAUTOMATIC` arrives when DAM releases desktop apps,
  that is, when the screen comes back on. Microsoft does not guarantee when it
  arrives relative to the actual exit. Maintenance wakes during standby do not
  release desktop apps, so they send no resume.
- **Time to act.** The helper can send one HID report inside the handler. On
  S3, Microsoft gives about 2 s per app. Under DAM, the timeout is global and
  lasts "a few seconds". One interrupt OUT report needs about 1 ms.

What it does not tell us:

- It does not fire while a power request blocks standby (for example audio
  playback, or an app holding a system request on AC power). The PC stays in
  the *Screen Off* state. For Sleep sync this is probably correct behaviour,
  because that PC is not asleep.
- It does not say S3, S4 or Modern Standby. It does not need to: on an S3 PC
  the board also sees the USB suspend.

**USB.** Modern Standby is S0, so Windows never sends a system sleep IRP to
the USB stack. A USB device suspends only if its driver idles it. Windows turns
off selective suspend for HID devices by default. **[INFERENCE]** The board's
HID interfaces stay in D0, so the board sees no bus suspend. That matches what
Derek saw on 2026-09-29.

**Probe.** A separate throwaway probe is not needed. Real apps confirm the API
works (see the table below). Put the registration and a debug-log line into the
helper as the first step of #293. Then run #292's five cases once, with Debug
logging on. That run must confirm on Derek's PC:

1. `PBT_APMSUSPEND` arrives for Start > Sleep, the power button, and an idle
   sleep timeout. Log its delay after the screen goes off.
2. It does **not** arrive for a display-off timeout alone.
3. The "asleep" report sent inside the handler reaches the board (board log),
   so DAM does not freeze the helper before the write ends.
4. `PBT_APMRESUMEAUTOMATIC` arrives on a key wake and on a mouse wake.
5. The board still sees no USB suspend, also after a long sleep (more than
   10 minutes, in DRIPS).

---

## 1. The Windows signals, compared

| Signal | Fires on Modern Standby sleep? | Fires on display-off only? | Use it? |
| --- | --- | --- | --- |
| `RegisterSuspendResumeNotification` + `WM_POWERBROADCAST` `PBT_APMSUSPEND` / `PBT_APMRESUMEAUTOMATIC` | Yes, at DAM entry | No | **Yes** |
| `PowerRegisterSuspendResumeNotification` (`DEVICE_NOTIFY_CALLBACK`) | Yes, same events, callback instead of window | No | Equal choice. The helper already has a window. |
| `WM_POWERBROADCAST` with no registration | No (only on S3/S4) | No | No |
| `GUID_CONSOLE_DISPLAY_STATE`, `GUID_SESSION_DISPLAY_STATUS`, `GUID_MONITOR_POWER_ON` | Yes, as display off | **Yes** | No: cannot tell sleep from display-off |
| Kernel-Power events 506 / 507 (System log) | Yes, at screen off | **[INFERENCE]** Yes | No |
| `_DSM` Sleep Entry / Exit (Functions 7/8), "lowest power state" (5/6) | Yes | No | No: firmware only, not visible to apps |

### 1.1 RegisterSuspendResumeNotification

- It "Registers to receive notification when the system is suspended or
  resumed." With `DEVICE_NOTIFY_WINDOW_HANDLE`, events go to the window. It
  needs Windows 8, desktop apps, User32.
  ([RegisterSuspendResumeNotification](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registersuspendresumenotification))
- `PowerRegisterSuspendResumeNotification` is the same, with a callback
  (`DEVICE_NOTIFY_CALLBACK` only), in Powrprof.dll. Events: `PBT_APMSUSPEND`,
  `PBT_APMRESUMESUSPEND`, `PBT_APMRESUMEAUTOMATIC`.
  ([PowerRegisterSuspendResumeNotification](https://learn.microsoft.com/en-us/windows/win32/api/powerbase/nf-powerbase-powerregistersuspendresumenotification))
- Neither page names an elevation requirement. Chromium and LGSTrayEx call it
  from an ordinary user process. ModernStandbyFix's README says it needs admin
  "to receive notifications about sleep modes". **[UNVERIFIED]** That README
  claim has no source, and the other apps contradict it.
- The key text is in the DAM compatibility page
  ([Desktop Activity Moderator](https://learn.microsoft.com/en-us/windows/win32/w8cookbook/desktop-activity-moderator)):
  - "When DAM suspension is engaged or disengaged, DAM triggers delivery of a
    WM\_POWERBROADCAST message to those processes subject to suspension that
    have opted-in to message delivery (via API call or compatibility shim
    …). After a few seconds delay, DAM suspends the process."
  - The opt-in is `RegisterSuspendResumeNotification` (window) or
    `PowerRegisterSuspendResumeNotification` (callback).
  - "The suspend message is WM\_POWERBROADCAST with wParam=PBT\_APMSUSPEND;
    this message is broadcast concurrently to all opted in processes … The
    timeout after the broadcast notification is global, not per process."
  - "The resume message … is broadcast concurrently to all opted in processes
    after a resume. Relative time of delivery to the system exit from
    connected standby is not guaranteed."
  - "When the screen is on, the DAM is disengaged." Processes in session 1 or
    higher (the helper) are *suspended*. Session 0 services are *throttled*,
    and get no notification.
- On resume, `PBT_APMRESUMEAUTOMATIC` is "sent every time the system resumes".
  `PBT_APMRESUMESUSPEND` follows only for a wake from user input. Use
  `PBT_APMRESUMEAUTOMATIC`.
  ([WM_POWERBROADCAST](https://learn.microsoft.com/en-us/windows/win32/power/wm-powerbroadcast))
- Pointer only (forum): a Microsoft Q&A thread on Windows 10 2004 recommends
  `RegisterSuspendResumeNotification` after Modern Standby became the default
  ([Q&A 48395](https://learn.microsoft.com/en-us/answers/questions/48395/cannot-receive-wm-powerbroadcast-message-on-win10)).
  The thread #292 cites is about a *service*, which DAM throttles instead of
  suspending
  ([Q&A 5614622](https://learn.microsoft.com/en-us/answers/questions/5614622/not-getting-pbt-apmsuspend-event-on-putting-window)).

### 1.2 Where the signal falls in the standby sequence

From [Prepare software for modern standby](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/prepare-software-for-modern-standby):

- "A system enters modern standby when the display turns off." The causes are
  the power button, lid close, *Sleep* from the power menu, and "the system
  idling out".
- The phases run in order. Each has a typical duration:
  1. **No-CS**: "the phase where the device waits for the sleep timeout to
     elapse and power requests to expire". Power requests block it
     "indefinitely on AC power, and for up to 5 minutes on DC power."
  2. **Connection**: waits while a remote desktop session is connected.
  3. **PLM**: suspends Store apps. Typically under 5 s.
  4. **Maintenance**: typically under 1 s on DC. It can block on AC.
  5. **DAM**: "The system suspends desktop applications." Typically under 1 s.
  6. **Low-power**: "This is where the system conceptually exits its *Screen
     Off* state and enters *Sleep*." Typically 5 s.
  7. **Resiliency**: DRIPS, with brief wakes.
- "Windows prevents desktop applications from running during any part of
  modern standby after completing the DAM phase." So maintenance wakes during
  *Sleep* do not run the helper, and do not send a false "awake".

**[INFERENCE]** The DAM message separates sleep from display-off. On a
display-off timeout, the system waits in No-CS for the sleep timeout. DAM does
not run, so `PBT_APMSUSPEND` is not sent. After an explicit Sleep, No-CS ends at
once (unless a power request is held), so the signal comes a few seconds after
the screen goes off. The probe must confirm both halves.

### 1.3 Display-state GUIDs

`GUID_CONSOLE_DISPLAY_STATE`, `GUID_SESSION_DISPLAY_STATUS` and
`GUID_MONITOR_POWER_ON` report the display on, off or dimmed
([Power Setting GUIDs](https://learn.microsoft.com/en-us/windows/win32/power/power-setting-guids)).
They fire on any display-off, so #286 rules them out. No public power-setting
GUID marks the *Sleep* state. A "low power epoch" GUID is not in the public
`winnt.h` (checked in mingw-w64 14 headers and SDK 10.0.16299 `winnt.h`).

### 1.4 Firmware notifications and Kernel-Power events

- The `_DSM` Display Off/On (3/4), Lowest Power State Entry/Exit (5/6) and
  Sleep Entry/Exit (7/8) functions "are firmware notifications rather than
  OS-level notifications." Apps cannot see them.
  ([Modern Standby firmware notifications](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-firmware-notifications))
- Kernel-Power 506 ("entering Modern Standby") and 507 ("exiting") are in the
  System log. No Microsoft page documents them. Pointers only:
  [Geoff Chappell](https://www.geoffchappell.com/studies/windows/km/ntoskrnl/events/microsoft-windows-kernel-power.htm)
  and Q&A threads. **[INFERENCE]** A Modern Standby session runs "between the
  transition to screen off and subsequent transition to screen on"
  ([SleepStudy](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-sleepstudy)).
  So 506 marks screen off and cannot separate sleep from display-off. After
  DAM, the helper would be frozen when 507 is logged.

---

## 2. Real apps

SHAs are the default-branch HEAD on 2026-09-29, unless a merge commit is named.

| App | File | API | Modern Standby? |
| --- | --- | --- | --- |
| Chromium `base::PowerMonitor` (and so Electron, Edge, Chrome) | [`base/power_monitor/power_monitor_device_source_win.cc` L117–L120](https://github.com/chromium/chromium/blob/e6f7ed878cf4de2c2c7e42ef608185977641d958/base/power_monitor/power_monitor_device_source_win.cc#L117-L120) | `RegisterSuspendResumeNotification(message_hwnd_, DEVICE_NOTIFY_WINDOW_HANDLE)` on a hidden `WS_POPUP` window, then `PBT_APMSUSPEND` → suspend and `PBT_APMRESUMEAUTOMATIC` → resume | **Yes.** Added in [a46a3d0d4e](https://github.com/chromium/chromium/commit/a46a3d0d4e8683b59b55c3fc5250dad6d3cda01f) (2022, by a Microsoft engineer): "On machines with modern standby and Win8+, calling RegisterSuspendResumeNotification is required in order to get the PBT\_APMSUSPEND message. The notification is no longer automatically fired." |
| Electron `powerMonitor` | [`shell/browser/api/electron_api_power_monitor_win.cc` L44–L50 @ 5ed3460](https://github.com/electron/electron/blob/5ed34607511bead8e810f5c6ec8432db00546f06/shell/browser/api/electron_api_power_monitor_win.cc#L44-L50) | Same call, added by [PR #25076](https://github.com/electron/electron/pull/25076) (2020): "we must explicitly register for its notifications to get the suspend/resume events" | **Yes.** [PR #47162](https://github.com/electron/electron/pull/47162) (2025) moved suspend/resume to Chromium's `base::PowerMonitor`. [PR #54352](https://github.com/electron/electron/pull/54352) (2026-09-25) removed Electron's own registration, because Chromium already does it. |
| LGSTrayEx (Logitech battery tray, talks to HID devices) | [`LGSTrayUI/App.xaml.cs` L391–L490 @ aabd09a](https://github.com/strain08/LGSTrayEx/blob/aabd09ad6f7dde303cf1c7d851ef7b9616def5db/LGSTrayUI/App.xaml.cs#L391-L490) | `RegisterSuspendResumeNotification` on a hidden WPF window. Comment: "Required for Modern Standby (S0) compatibility" | **Yes** |
| OmenMon (HP fan/BIOS tool) | [`App/Gui/Gui.cs` L139–L162 @ d89340e](https://github.com/OmenMon/OmenMon/blob/d89340e6d4dc9802b609390729b0bfaec05563e2/App/Gui/Gui.cs#L139-L162) | `PowerRegisterSuspendResumeNotification(DEVICE_NOTIFY_CALLBACK)` | Yes (the API) |
| ModernStandbyFix | [`ModernStandbyFix/App.xaml.cs` L62–L103 @ e197943](https://github.com/martinchrzan/ModernStandbyFix/blob/e1979439587624984a018f2e47482c84b8d4f116/ModernStandbyFix/App.xaml.cs#L62-L103) | `PowerRegisterSuspendResumeNotification`. On `PBT_APMSUSPEND` it disables every network adapter. | **Yes.** This is its whole purpose, and it does real work inside the handler. |
| Deskflow | [`src/lib/platform/MSWindowsComputer.cpp` L953–L975 @ 1d530d7](https://github.com/deskflow/deskflow/blob/1d530d76422daab4af730959d2093247893bbbc5/src/lib/platform/MSWindowsComputer.cpp#L953-L975) | Plain `WM_POWERBROADCAST`, **no registration** | **No.** Its comment: "On windows 10 we don't receive WM\_POWERBROADCAST after sleep. We receive only WM\_TIMECHANGE hence this message is used to resume." |
| Input Leap | [`src/lib/platform/MSWindowsScreen.cpp` L1034–L1045 @ 34a34fb](https://github.com/input-leap/input-leap/blob/34a34fb20b93113a6b26052cb5a54f9be2327775/src/lib/platform/MSWindowsScreen.cpp#L1034-L1045) | Plain `WM_POWERBROADCAST`, no registration | **[INFERENCE]** No, for the same reason as Deskflow |
| .NET `SystemEvents.PowerModeChanged` | [`SystemEvents.cs` L852–L870 @ 3270359](https://github.com/dotnet/runtime/blob/3270359d4a3a9926cd59df43204bd529d08d1e7c/src/libraries/Microsoft.Win32.SystemEvents/src/Microsoft/Win32/SystemEvents.cs#L852-L870) | Maps `WM_POWERBROADCAST`. The file never calls `RegisterSuspendResumeNotification`. | **[INFERENCE]** No suspend on Modern Standby |
| PowerToys Mouse Without Borders | [`InitAndCleanup.cs` L139–L150 @ 31bf904](https://github.com/microsoft/PowerToys/blob/31bf904826709b81b4a9bd9756788a0596f2ff81/src/modules/MouseWithoutBorders/App/Core/InitAndCleanup.cs#L139-L150) | `SystemEvents.PowerModeChanged` | **[INFERENCE]** It inherits the .NET gap |
| Tailscale, WireGuard for Windows | whole repo grep @ f5f3260 / 6ece77b | No suspend/resume handling found | n/a |

---

## 3. Can the helper still send after the signal?

- DAM sends the message first, then suspends the process "after a few
  seconds delay". The timeout "is global, not per process"
  ([DAM](https://learn.microsoft.com/en-us/windows/win32/w8cookbook/desktop-activity-moderator)).
- On the classic path: "The system allows approximately two seconds for an
  application to handle this notification"
  ([PBT_APMSUSPEND](https://learn.microsoft.com/en-us/windows/win32/power/pbt-apmsuspend)).
- The DAM phase is "Typically, less than one second"
  ([Prepare software](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/prepare-software-for-modern-standby)).
- The helper's `HidTransport::send` is a synchronous overlapped `WriteFile`
  with a 250 ms bound (`helpers/windows/src/hid_transport.cpp`). The board is
  in D0 (§4). **[INFERENCE]** A send done inside the `WM_POWERBROADCAST`
  handler, before the handler returns, ends well inside the window. No source
  gives an exact number of milliseconds, so the probe confirms it (check 3).
- The helper already has a hidden top-level `WS_POPUP` window
  (`helpers/windows/src/main.cpp`, since #208). It can receive the registered
  message with no new window.

---

## 4. Does the board ever see a USB suspend?

- Modern Standby "remains in S0 while in standby"
  ([Modern Standby vs S3](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-vs-s3)).
- The USB idle callback "is invoked only while the system is in S0 and the
  device is in D0". Windows "always selectively suspends each USB device …
  before it ceases all USB traffic on the bus". The bus suspends only when
  every device is in D1–D3
  ([USB Selective Suspend](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-selective-suspend)).
  So in Modern Standby, a device suspends only when its own driver idles it.
- "By default, USB selective suspend is disabled by Windows" for HID. A HID
  device opts in through a Microsoft OS descriptor or a vendor INF
  ([Selective Suspend for HID Over USB](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/selective-suspend-for-hid-over-usb-devices)).
- "The USB host controller can power down only after all of the USB devices
  connected to it have entered a selective suspend state. … If either the mouse
  or the keyboard stays powered on, the USB host controller also stays powered
  on"
  ([Prepare hardware](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/prepare-hardware-for-modern-standby)).
  Microsoft's own lab shows a USB device "still in state D0 for 100% of the
  time" through a standby session
  ([WPT exercise 4](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/debugging-problems-with-standby-exercise-4)).
- **[INFERENCE]** The board's HID interfaces do not opt in to selective
  suspend. They stay in D0, so the host keeps the bus running, and the board
  never sees a suspend. This matches the 2026-09-29 hardware finding on #286.
- **[UNVERIFIED]** Whether the HID class driver idles keyboards or mice at
  screen off on some systems. The wake-sources table says an external USB
  keyboard "Multiple key presses might be needed to generate a resume event"
  ([Wake sources](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-wake-sources)),
  which hints that some do suspend. Check 5 of the probe covers this on
  Derek's PC.

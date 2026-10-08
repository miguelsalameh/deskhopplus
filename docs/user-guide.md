# DeskHopPlus user guide

How to pair, use, fix and remove DeskHopPlus. Nothing here needs the source code.

You need a DeskHop board pair running `deskhopplus.uf2`, and a helper on each computer. The
README's [Get started](../README.md#get-started) covers flashing and the first run of each helper.
This guide starts where that ends.

Three words used throughout:

- **Board A** and **board B** are the two halves of the DeskHop. Each plugs into one computer.
  The keyboard and mouse plug into one of them, usually board A.
- **The chord** is `Left Ctrl + Right Shift + C + O`. It enters and leaves config mode. It always
  works, whatever you change on the config page.
- **The pair chord** is `Left Ctrl + Right Shift + P`. It opens a pairing window on both boards.
  It is fixed too.

A Linux computer gets the keyboard and mouse. There is no helper for it, so no clipboard and no
cursor placement on that side.

## Pair a helper

A helper must be paired with its board before the clipboard or cursor placement work. Pairing is
one press of the **pair chord**, `Left Ctrl + Right Shift + P`, with the helper already running.

1. Start the helper on each computer you are pairing. Wait until its menu says
   **Not paired — press the Pair chord (Left Ctrl + Right Shift + P)**.
2. Press the pair chord once, on the keyboard. The on-board LED blinks. Both boards open a
   **60-second pairing window** at once, so the keyboard can be on either board.
3. Each waiting helper pairs by itself. It shows **Paired** once, and its menu changes to
   **Connected and paired**. Each helper's menu also says **Other computer connected** once the
   other computer's helper is connected too, so you learn it worked without walking over. It says
   **Other computer not connected** while that helper is not paired, not running, or its board
   is not reachable.

Nothing reboots and no drive appears. The pair chord is fixed, like the chord: a hotkey you
set on the config page cannot replace it.

- **The helper must be running before the window opens.** A helper started more than 60 seconds
  after the press has missed it. Press the pair chord again.

Each board holds exactly one paired helper. Pairing a second helper to the same board replaces the
first. The wipe chord (`Right Shift + F12 + D`) clears the pairing on both boards, along with every
other setting, and then opens a pairing window on both boards. A helper that is running re-pairs by
itself within the minute.

### Which helper is a board paired with?

Open the config page: press the chord, open the `DESKHOP` drive, open `config.htm` in Chrome, click
**Connect**. Under **Status**, **Paired helper** shows an id of sixteen hex characters, or
*none — press the Pair chord (Left Ctrl + Right Shift + P)*.

Each helper writes its own id, `helper key id: …`, as the first line every time it starts. Look
for the last one in the log. Where the log is:
[Where the logs are](#where-the-logs-are). Same id: this helper is the one. Different id: the
board is paired with something else, and a pair chord press moves it.

## What the menu bar or tray says

The macOS helper is a menu bar item. The Windows helper is an icon in the notification area. Both
show the same states; two rows are worded a little differently on Windows. Both are always there
while the helper runs.

The icon is two screens. It has three looks, and the look tells you the state at a glance; the
words are one hover or one click away, and they are what to act on:

| The icon | Meaning |
| --- | --- |
| Two solid screens joined by a bar | Connected and paired, including a live config-mode session. |
| Two outlined screens | Looking for the board, board not connected, or config mode. Nothing to do unless it stays that way. |
| A badge with **!** on the screens | Something to read: open the menu. Every state with a remedy, a reconnect storm, or files waiting for your answer. |

On macOS the menu bar item is the icon alone, with a few characters beside it when something is
happening: **⬇ files?** when files are offered, **⬇ 42%** while they arrive, **⬆** while this
computer is sending, and **⚠** when the menu holds a message for you. Hover for the words.

On Windows the icon itself turns into the percent while a file arrives; hover for the rest. The
helper asks Windows to keep the icon on the taskbar rather than behind the **^** overflow. If it
still lands in the overflow, turn it on once by hand: **Settings › Personalization › Taskbar ›
Other system tray icons**, and turn on **DeskHopPlus Helper**. The helper asks again on every
start, so the setting follows the exe if you move it.

The first row of the menu is greyed and shows the release, e.g. **DeskHopPlus Helper 1.2**. The
second row is the state.

| Menu says | What it means | What to do |
| --- | --- | --- |
| **Waiting for the device** (macOS) or **Looking for the device** (Windows) | The helper is looking for the board, or the board went away for a moment. | Wait a few seconds. If it stays, check the USB cable. |
| **Connected and paired** | Everything works. | Nothing. |
| **Not paired — press the Pair chord (Left Ctrl + Right Shift + P)** | The board has no pairing for this helper. | [Pair a helper](#pair-a-helper). |
| **Device in config mode** | You pressed the chord once. | Press it again, or wait five minutes. |
| **Connected and paired — config mode** | The helper has a live session while the config page is open. | Save and try the layout, then Exit when finished. |
| **Device not connected** | The helper cannot find the board. | Check the USB cable. If the board was just replugged, wait a few seconds. |
| **Reconnecting repeatedly — check the link, and that the helper is up to date** | The connection keeps dropping and coming back. | Check the cable. Make sure the helper and the firmware are from the same release. |
| **Helper version does not match the device — update the helper; file transfers are refused** | The board and the helper speak different versions. | Install the helper from the same release as the firmware. The cursor still works; the clipboard does not. |
| **Another program is writing to the device channel — find and stop it, and do not press the pair chord while it is running** | Something else on this computer is talking to the board. | Find and quit it. Do not press the pair chord until it is gone: the pair chord would pair whatever is attached. |
| **Device identity changed — if you re-flashed it, remove the pinned board key** | The board's own key is not the one this helper remembers. A firmware upgrade never changes it; a whole-flash erase or a swapped board does. | Only if you did that yourself: [Identity changed](#identity-changed-after-a-re-flash). Otherwise do not press the pair chord; pressing it would accept the new board. |

Below the state the menu may also show:

- a cursor placement problem, until the next placement succeeds;
- a file offer with **Accept and start the transfer** and **Decline**;
- a transfer in progress, **Receiving 2.1 MB of 8.0 MB — 26%**, with **Cancel this transfer**;
- **Cancel what is being sent** while this computer is sending;
- **Start at login** (macOS) or **Start at logon** (Windows);
- **Debug logging**, off by default: tick it to make the helper write its log;
- **Quit DeskHopPlus Helper**.

**Start at login / Start at logon** makes the helper start when you log in. On macOS the change
takes effect at the next login. On Windows a managed laptop may refuse every method the helper
tries; the helper log says so (tick **Debug logging** first), and the helper still works when you start it yourself.

**Quit** stops the helper until you start it again or log in again. On macOS, double-click the
helper file to start it again now. On Windows, run the exe again.

## Clipboard

Copy on one computer, cross to the other, paste. What crosses depends on what was copied and how
big it is.

| Copied | What happens |
| --- | --- |
| Text | Crosses at once. |
| Text with a picture of it (Office applications do this) | Both cross together while text and picture add up to 256 KB or under. Above that the text crosses alone. |
| Image up to 256 KB | Crosses at once. |
| Image over 256 KB | The other computer fetches it as soon as it hears about it, then puts it on its clipboard. Large images take a moment to arrive. |
| Files up to 1 MB in total | Cross at once. |
| Files over 1 MB in total | Nothing crosses until you accept on the paste side. See below. |

A new copy replaces whatever the last copy was still doing.

### Files: the accept step

Copying files sends only their names and sizes. When you cross to the other computer, its helper
shows the offer in the menu: how many files, how big, and about how long it will take. Choose
**Accept and start the transfer** or **Decline**. On Windows, clicking the notification balloon
also accepts.

The rate is about 33 KB per second. A 10 MB set takes about five minutes. A paste does not start
the transfer, because both operating systems would freeze the pasting application for the whole
transfer.

An offer you do not answer in two minutes is declined for you. Copy again to get it back.

When the transfer completes, the files are put on the clipboard as if you had copied them locally.
Paste them anywhere. They live in a temporary folder:

- macOS: `deskhopplus` inside your temporary directory. In Terminal: `open "$TMPDIR/deskhopplus"`.
- Windows: `%TEMP%\deskhopplus`.

The helper empties that folder every time it starts. Only the newest set survives a restart. Move
the files somewhere real if you want to keep them.

A file that grows between the copy and the accept, such as a log, arrives at the size it had when
you copied it. A file that shrinks fails the transfer.

### The size cap

The board holds the largest clipboard it will carry: **10 MB** by default, up to **64 MB**. It is
on the config page under **Clipboard → Clipboard size cap (MB)**. The same cap applies to both
directions and both helpers.

When something too big is copied, nothing happens on the copy side. When you cross to the paste
side, its helper tells you: *… is 23 MB, larger than the 10 MB clipboard limit, so it was not
brought over. Raise the limit on the board's config page.*

### Turning a direction off

On the config page under **Clipboard** are **Block clipboard A to B** and
**Block clipboard B to A**. Tick one to stop copies crossing that way. The board is the only place
this is set; the helpers follow it.

Nothing crosses without a paired helper at each end, so a computer with no helper shares nothing.

## Layout, remapping and hotkeys

All of this is on the config page. To open it: press the chord, open the `DESKHOP` drive, open
`config.htm` in Chrome or another Chromium browser (Firefox has no WebHID), click **Connect**.
Change what you want and click **Save**. Wait for **Connected and paired — config mode**
in the helper menu, then try multi-monitor navigation while the page stays connected. **Device in
config mode** means there is no usable helper channel; multi-monitor hops may then land on the wrong
monitor. Click **Exit** when finished, or the board leaves config mode after five minutes. Each
entry and exit reboots the board and starts a fresh helper session.

Without its helper, a computer with more than one screen still gets the cursor on the correct
screen. The board moves it to a screen edge and gives it a small push across, from the screen
where it left it. The board cannot see the computer's own trackpad or a second mouse. If one of
them moved the cursor in between, the push can land on the wrong screen. The helper places the
cursor on the exact pixel, so run it where you can.

Without its helper, Windows' second screens also run on the board's estimate of the cursor. The
**Pointer speed** group under a Windows output with two or more screens sets it up: move its
**Speed** slider to the number Windows shows for **Mouse pointer speed** (Settings → Bluetooth &
devices → Mouse; drag its slider to see the number), check the **Monitor size**, turn off
**Enhance pointer precision** (on that same Windows page: Additional mouse settings → Pointer
Options), and click **Save**. The board saves the pointer speed and monitor size and copies
Windows' own pointer maths on those screens; the page also works out Speed X and Speed Y for the
main screen. Until you move the slider and Save once, the board uses Speed X and Speed Y for its
estimate, and the cursor crosses early or late at most pointer speeds. See
[Crossings early or late?](#crossings-early-or-late-check-these-3-settings)

On a Mac, or on Windows with one screen, the **Speed** slider sets only how fast the cursor
moves. Speed Y follows the screen shape, so both directions feel the same. Untick **Same speed
both directions** to set Speed Y on its own.

**On a Mac, eject `DESKHOP` in Finder before you leave config mode** — before Exit, before the
chord, and before the five minutes run out. macOS can hang its disk mounter when a mounted drive
vanishes ([#178](https://github.com/myn/deskhopplus/issues/178)). The board withdraws the drive
two seconds before it reboots, but macOS does not look while the drive is mounted, so only an
eject makes it let go first. After an eject the board treats the drive as gone and reboots at once
on the next exit.

The page is one settings window. The sidebar on the left has six sections, shown one at a time:
**Desk**, **Keyboard & Mouse**, **Hotkeys**, **Clipboard**, **Status** and **Service**. The
toolbar at the top says in words whether the page is connected and holds **Connect**, **Read**,
**Save** and **Exit**. Save shows how many fields you have changed and not yet saved; a section
holding unsaved changes carries a dot in the sidebar. Read throws unsaved changes away.

**Desk** is the picture of both computers' monitors, with the selected computer's settings under
it: click a computer in the picture, **Output A** or **Output B**, to see its settings. Output A
is the computer on board A, Output B the computer on board B. Not sure which board is which?
Unplug and replug both boards: the keyboard and mouse always start on board A's computer.

**Service** holds the maintenance buttons: **Blink**, **Blink both**, **Bootloader** and
**Wipe Config**. Bootloader and Wipe Config need two clicks; the first arms the button and says so.

The idle and maximum screensaver times for each output are in seconds, from 0 to 4294.967295
in steps of 0.000001. A maximum time of 0 leaves the screensaver without a time limit.

When Save finds a field it cannot send — a screensaver time outside that range, or an unknown
hotkey or key name — nothing is sent. A yellow strip at the top of the window says how many fields
need a fix and names each with its section. Click a name to land on the field. Fix them and Save again.

### Where the computers sit

The picture in **Desk** shows both computers' monitors after **Connect** or **Read**. Each
computer is a block with a label bar, the selected one in blue; its boxes are numbered from **Main**,
the monitor its operating system calls main, outward along the line. The bands on the seam
are the segments: which monitor meets which, and in what order. Move things by dragging:

- Drag a computer's **label bar** to move that computer as a block: to another side of the other
  computer, or along the seam in half-monitor steps.
- Drag **Main** onto the last box of its own line to flip a straight line. Main lands on the far
  end, the other boxes stay put and are renumbered from Main, and Chain Direction flips with it.
  Main dropped on an empty cell is not moved, and Main of an L or a T does not flip; drag the label
  to move the whole computer.
- Drag **any other box** to a cell beside, above or below Main. It moves alone, to the end of the
  line out of Main that cell is on, and the boxes past where it was close up. So a computer's
  monitors can make an L or a T around Main: with `[3][2][Main]`, dragging 3 under Main gives
  `[2][Main]` with 3 below. A row becomes a column one box at a time.
- **+** under the picture adds a monitor at the end of monitor 2's line, up to seven. **−** removes
  the highest-numbered one, never Main.
- A one-monitor computer's only box moves the computer, the same as its label.
- Keyboard: Tab to a box and press an arrow key. That is the same as dropping it one cell over.

A drop fills in the fields for you, the same as picking them by hand: **Save** sends them to the
board, **Read** throws them away. A drop the board cannot run — a gap, an overlap, a corner-only
touch, computers meeting along more than one edge, the main monitor of a straight line away from
the seam, or more than four segments — is not made; one line under the picture says why, and the
picture stays as it was.

Set **Operating System** on each output. The lock-both-computers chord and the Cmd/Ctrl swap
default depend on it.

### Advanced: the fields behind the picture

One closed **Advanced** panel under the selected computer's settings holds the fields the picture
sets for that computer; click the other computer in the picture to see its fields. Changing one
redraws the picture. Values the picture cannot draw exactly show a note. Each output has:

- **Screen Count** — how many monitors that computer drives (1 to 7).
- **Speed X** and **Speed Y** — how far the cursor moves per mouse count where the board places
  it, 1 to 128 (defaults 16 and 28). The **Pointer speed** group sets them; a value set here
  shows there as a Windows pointer speed, or as **Custom** when it does not match.
- **Border Direction** — where the *other* computer is: Left, Right, Top or Bottom.
- **Chain Direction** — which way this computer's own monitors run from its main monitor: Left,
  Right, Top or Bottom. Screen 1 is the main monitor; screen 2 is the next one along that
  direction.
- **Monitor Turns** — which way monitors 2 to 5 branch off Main, for an L or a T: two bits per
  monitor, from monitor 2 in the lowest bits, each 0 along Chain Direction, 1 clockwise from it,
  2 opposite, 3 counter-clockwise. Monitors 6 and 7 run along Chain Direction. 0 is a straight
  line. The picture sets it; a monitor on a turned line crosses to the other computer from its
  own edge facing it. Both boards need firmware that knows this field: update them before you
  save an L or a T, because a board on older firmware ignores the field and runs a straight line,
  and Save does not resend a field the page read back unchanged.

Side by side, with the Windows machine on the right of the Mac: Mac's Border Direction is Right,
Windows's is Left. One above the other, Mac on top: Mac's Border Direction is Bottom, Windows's is
Top. Both computers with their monitors running rightward: Chain Direction Right on both.

When Border Direction and Chain Direction are at right angles (monitors in a row, the other
computer above or below), every monitor touches the seam and the cursor can cross from any of
them. When they are on the same axis, only the monitor nearest the other computer crosses, as on a
stock DeskHop.

### Seam ranges: which monitor meets which

With more than one monitor on each side, the board needs to know which monitor on this side meets
which monitor on the other side, and where. That is the **Seam ranges** table on each output: four
segments, each with a **Screen**, a **Start** and an **End**.

- **Screen** is the monitor number, 1 to 7. 0 means the segment is unused.
- **Start** and **End** are positions along the seam, from 0 to 65535, on that monitor. The whole
  edge of a monitor is 0 to 65535.
- **Segment 1 on A meets segment 1 on B**, segment 2 meets segment 2, and so on. The cursor
  leaves at some fraction along A's segment and arrives at the same fraction along B's.

An along-seam offset has no separate field. The Layout records it in these Segment **Start** and
**End** values; for example, 32768 is approximately halfway along a monitor edge.

Two monitors above two monitors, left above left and right above right, with the left monitor the
main one on both computers:

| | Segment 1 | Segment 2 |
| --- | --- | --- |
| Output A (top) | Screen 1, 0 to 65535 | Screen 2, 0 to 65535 |
| Output B (bottom) | Screen 1, 0 to 65535 | Screen 2, 0 to 65535 |

If one computer's main monitor is on the right instead, swap the screen numbers on that side, so
each segment still pairs the two monitors that physically touch.

Narrower segments handle monitors of different widths, or a monitor that only half overlaps the
one across the seam; the position is scaled. A monitor with no segment does not cross. Until both
sides of a segment are filled in, the board falls back to the stock DeskHop rule, so the cursor is
never trapped while you set this up.

If your monitors are the same size and aligned, `Right Shift + F12 + Y` still works as on a stock
DeskHop: park the mouse where the smaller monitor's edge is and press it.

### Status LED

The LED on each board is lit while that board's computer has the keyboard and mouse. **Keyboard &
Mouse** has a **Status LED** group that can turn it off on its own, for a desk in a bedroom:

- **Turn off**: **Never** (the default), **When idle**, **After a switch** or **Always**.
- **After**: 5 seconds, 10 seconds, 30 seconds, 1 minute, 5 minutes, 15 minutes or 1 hour. It is
  greyed out while Turn off is Never or Always. The default is 1 minute.

**When idle** turns the LED off once the computer has had no key press or mouse move for that
time. A switch to that computer counts as a move, so a hotkey switch onto a computer idle since
morning lights the LED, and any key or mouse move turns it back on. **After a switch** turns it
off that long after each switch, even while you type. **Always** keeps the LED off.

Config mode always blinks. The one-off blinks (a keyboard or mouse plugged in, a hotkey
acknowledged) still show, and the LED goes off again after.

Save sends one setting to both boards. The keyboard's Caps Lock light is not part of this.

### Sleep sync

**Keyboard & Mouse** has a **Power** group with **Sleep sync**, Off by default. With it On, when
the computer you use goes to sleep, the other computer goes to sleep too, and its monitors go dark.

- Only the computer that has the keyboard and mouse starts this. It must stay asleep for 5 seconds
  first, so a short USB power-saving blip does nothing.
- With the helper running on a Mac, the helper tells the board when the Mac goes to sleep. The 5
  seconds start then.
- Without the helper, a Mac turns its screen off first, then can stay in a half-awake state
  (DarkWake) for up to about 30 seconds before it really sleeps. The 5 seconds start only then. So
  the other computer can go to sleep 20 to 35 seconds after the Mac screen goes dark. A Mac on a
  Thunderbolt dock can do a DarkWake almost every minute. Without the helper, the other computer
  can then sleep late or not at all, and a key press during a DarkWake does not wake it.
- The other board then presses the System Sleep key on its computer. It does not press it if that
  computer is already asleep.
- A computer you are not using that goes to sleep from idle sleeps alone. Its idle timer only counts
  its own keyboard and mouse, and you are typing on the other one, so it must not put that one to
  sleep. Use **Sleep when idle** (below) to sleep the computer you use too.
- "Sleep" is system sleep. A screen that only goes dark or locks does not count. The board cannot
  tell a shutdown from sleep, so shutting down the computer you use can also sleep the other one.
- Nothing happens if the other board is unplugged. No helper is needed, except on a Windows PC
  with Modern Standby (below) and on a Mac on a Thunderbolt dock (above).

**Sleep when idle.** This row shows only when Sleep sync is On. Pick Never (the default),
Immediately, 1 minute, 15 minutes, 30 minutes, 1 hour or 2 hours. If the other computer is already
asleep, the computer you use sleeps after this long with no key or mouse input through the boards.
Immediately works differently: see below.

- The time counts from your last input, or from your last switch if that came later. Example: the
  PC sleeps at 2:00, your last input on the Mac was at 1:50, and the setting is 30 minutes. The Mac
  sleeps at 2:20.
- The computer's own keyboard or trackpad does not count as input. Nor does a video: a long video
  can be cut, so pick a time long enough.
- If the other computer wakes, nothing happens. If it goes back to sleep and the time is already
  up, the computer you use sleeps at once.
- A key press or mouse move then wakes both, as below.

**Immediately** does not wait for idle time. The computer you use sleeps as soon as the other
computer goes to sleep, even while you type. It takes about 6 seconds: the other board waits 5
seconds to be sure its computer sleeps, then tells this one.

- It sleeps once for each time the other computer goes to sleep. After you wake both, it stays
  awake, even if the other computer does not wake.
- A switch onto the computer you use, while the other one sleeps, does not count. Nor does turning
  Sleep sync On, or the other board coming back after a reboot or an update.
- If you pick Immediately while the other computer already sleeps, nothing happens until it next
  goes to sleep.
- The other computer can also go to sleep by itself: its own idle timer, or a Windows PC that goes
  back to sleep after a short wake. Each time, the computer you use sleeps too, even while you
  type.

**Waking.** With Sleep sync On, a key press or mouse move while the computer you use is asleep wakes
both computers.

- Only input to the computer that has the keyboard and mouse does this. Typing on the computer you
  use never wakes an idle computer you are not using.
- A computer that wakes by itself (lid, power button, a wake timer) wakes alone.
- If you press a key while the other computer is still going to sleep, its board remembers the wake
  for 30 seconds after it pressed System Sleep. As soon as that computer is asleep, the board wakes
  it again.
- A Mac half awake with its screens off (a "DarkWake") ignores a mouse move but wakes for a key. So
  when the helper on a Mac said the Mac sleeps, the first mouse move also taps Left Shift. A wake
  from the other computer gets the tap too.
- A computer can only be woken this way if it lets USB devices wake it. The same limit applies when a key press
  wakes the computer you use.

**Windows PCs with Modern Standby.** Many new Windows PCs sleep with Modern Standby, not the older
S3 sleep. To check, run `powercfg /a`. If it lists "Standby (S0 Low Power Idle)", the PC uses
Modern Standby. The board cannot see when such a PC goes to sleep, so:

- The other computer sleeping still sleeps this PC.
- This PC sleeping sleeps the other computer only if the helper runs on this PC. The helper tells
  the board when Windows goes to sleep, about 1 second after the screen goes dark. The 5-second
  wait follows. Without the helper, this PC sleeping does not sleep the other computer.
- A screen that goes dark on a timeout is not sleep, and the helper does not report it.
- Windows can wake for a moment by itself right after it goes to sleep, with the screen dark. The
  other computer stays asleep: only a wake by you counts.
- A key press while the other computer is asleep still wakes this PC. The board moves the pointer
  1 pixel out and 1 pixel back. Real input wakes Modern Standby, even after the PC has slept for
  minutes. The pointer ends where it started, so on a PC that is already awake you see nothing.

Save sends one setting to both boards.

### Cmd and Ctrl swap

Each output has **Swap Ctrl and Cmd**, under **Key mapping**. When on, Ctrl and Cmd (the Windows key) change places
on keys going to that computer, left and right both. `Ctrl+C` on the keyboard becomes `Cmd+C` on
the Mac. It is on by default for an output whose Operating System is MacOS and off for the others.

Hotkeys are matched on the keys you physically press, before the swap, so a chord is the same
fingering on both computers.

### Key overrides and passthrough

Each output has two text boxes under **Key mapping**.

**Key overrides** — one `from=to` per line. Keys going to that computer are changed on the way.
Modifiers and ordinary keys mix freely:

```
capslock=lctrl
rgui=ralt
```

Up to 32 lines per output.

**Passthrough keys** — a comma-separated list of keys that are never changed for that computer, by
overrides or by the swap:

```
capslock, f1
```

Up to 16 per output. Order of precedence: passthrough first, then overrides, then the swap.

### Hotkeys

**Hotkeys** has one box per action. Write the keys joined by `+`, up to six keys:

```
lctrl+capslock
```

The defaults:

| Action | Default | What it does |
| --- | --- | --- |
| output_toggle | `lctrl+capslock` | Switch to the other computer |
| mouse_zoom | `ralt+m` | Slow mouse on or off |
| switch_lock | `ralt+k` | Stop the mouse crossing on or off |
| screen_lock | `ralt+l` | Lock both computers (needs Operating System set on both outputs) |
| gaming_mode | `lctrl+rshift+g` | Gaming mode on or off: locked to this computer, relative mouse |
| screensaver_pong | `lctrl+rshift+s` | Screensaver on, pong |
| screensaver_jitter | `lctrl+rshift+j` | Screensaver on, jitter |
| screensaver_disable | `lctrl+rshift+x` | Screensaver off |
| wipe_config | `rshift+f12+d` | Erase every setting on both boards, including pairings |
| screen_seam | `rshift+f12+y` | Save the cursor height for the seam |
| config_mode | `lctrl+rshift+c+o` | Config mode on or off |
| firmware_upgrade_a | `lshift+rshift+a` | Put board A into its USB bootloader (`RPI-RP2` drive) |
| firmware_upgrade_b | `lshift+rshift+b` | Put board B into its USB bootloader |

`lctrl+rshift+c+o` always enters config mode, even if you change the config_mode box to something
else. That is the way back if a hotkey change locks you out.

The pair chord, `lctrl+rshift+p`, has no box. It always opens a pairing window, and it wins over
any hotkey you give the same keys.

### Key names

Lower case. Letters `a` to `z`, digits `0` to `9`, `f1` to `f12`.

Modifiers: `lctrl`, `lshift`, `lalt`, `lgui`, `rctrl`, `rshift`, `ralt`, `rgui`. `gui` is Cmd on
a Mac and the Windows key on a PC.

Others: `enter`, `esc`, `backspace`, `tab`, `space`, `capslock`, `insert`, `delete`, `home`,
`end`, `pageup`, `pagedown`, `up`, `down`, `left`, `right`, `printscreen`, `scrolllock`,
`numlock`, `minus`, `equals`, `lbracket`, `rbracket`, `backslash`, `semicolon`, `quote`,
`grave`, `comma`, `period`, `slash`.

Keypad: `kp0` to `kp9`, `kp_divide`, `kp_multiply`, `kp_minus`, `kp_plus`, `kp_enter`,
`kp_period`.

## Troubleshooting

### macOS will not open the helper

macOS refuses an unsigned download the first time. If it says the app **cannot be verified**: open
**System Settings → Privacy & Security**, scroll down, click **Open Anyway**, then double-click the
helper again. If it says the app **is damaged**, there is no Open Anyway: in Terminal, run

```sh
xattr -dr com.apple.quarantine ~/Applications/deskhopplus-helper.app
```

with the path you put it at. Then double-click it. That command works in both cases: it removes
the mark Safari puts on a download, which is what macOS checks.

The helper needs macOS 13 or later on Apple Silicon, or an Intel Mac with the T2 chip. It keeps its
key in the Secure Enclave, and an Intel Mac without T2 has none.

### Windows says "Windows protected your PC"

SmartScreen, because the exe is not signed. Click **More info**, then **Run anyway**. Once.

### Identity changed after a re-flash

The helper remembers the board's key and refuses a board with a different one. A firmware
upgrade, by either route in Get started, keeps the key. Only erasing the whole flash (a "nuke"
image) or swapping in another board changes it.

If you did that yourself, tell the helper: quit it, delete the remembered key, start it again.

macOS: click **Quit DeskHopPlus Helper** in the menu. In Terminal:

```sh
rm ~/Library/Application\ Support/deskhopplus/board_key
```

Then double-click the app, or log out and in.

Windows: click **Quit DeskHopPlus Helper** in the tray menu, delete
`%LOCALAPPDATA%\deskhopplus\board_key`, run the exe again.

Then [pair](#pair-a-helper) again.

If you did **not** re-flash or swap anything, leave the pair chord alone and find out what changed.

### Where the logs are

Tick **Debug logging** in the helper's menu or tray first. It is off by default, and off writes
no log at all. While it is on, a log over 5 MB is emptied when the helper starts.

- macOS: `/tmp/deskhop-helper.log` when started at login. When started by double-clicking, no
  file gets the log. To watch one, quit the helper and start it from Terminal instead:
  `~/Applications/deskhopplus-helper.app/Contents/MacOS/deskhopplus-helper`, with the path you put
  it at. The log is that window.
- Windows: `%LOCALAPPDATA%\deskhopplus\helper.log`.

On macOS each line starts with the wall clock and the time since the helper started. On Windows
each line starts with the time since Windows booted, in milliseconds.

### The DESKHOP drive does not appear on macOS

Pressed the chord, LED blinks, no drive. First: did you press twice? The chord is a toggle, and
a second press leaves config mode.

If you pressed once and still nothing, and Disk Utility or `diskutil list` hangs, macOS's disk
mounter is stuck. Nothing else will mount either until the Mac reboots. Reboot the Mac. This is
[#178](https://github.com/myn/deskhopplus/issues/178). To stop it happening again, eject
`DESKHOP` in Finder before every exit from config mode; the board cannot make macOS let go by
itself ([#229](https://github.com/myn/deskhopplus/issues/229)).

### Crossings early or late? Check these 3 settings

On a Windows computer with two or more screens and no helper running, a crossing that comes too
early or too late is almost always a setting:

1. **Enhance pointer precision** is off in Windows (Settings → Bluetooth & devices → Mouse →
   Additional mouse settings → Pointer Options). With it on, no speed matches.
2. The **Speed** slider in the config page's **Pointer speed** group shows the same number as
   Windows' **Mouse pointer speed** (Settings → Bluetooth & devices → Mouse).
3. The **Monitor size** on the config page is that computer's real size in pixels, and you clicked
   **Save** after setting all three. **Read** shows what the board holds.

The board copies Windows' maths: each mouse report moves the cursor by its counts × the pointer
speed's multiplier, in whole pixels, and keeps the part-pixel for the next report, so a slow hand
still creeps the cursor. On the main screen the board places the cursor itself, with Speed =
32768 ÷ pixels × the multiplier, rounded, once for the width (Speed X) and once for the height
(Speed Y). At pointer speed 10, 1920 × 1080 gives 17 and 30, and 2560 × 1440 gives 13 and 23.
The multipliers, with Enhance pointer precision off:

| Pointer speed | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| Multiplier | 1/32 | 1/16 | 1/8 | 1/4 | 3/8 | 1/2 | 5/8 | 3/4 | 7/8 | 1 |

| Pointer speed | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 |
|---|---|---|---|---|---|---|---|---|---|---|
| Multiplier | 1.25 | 1.5 | 1.75 | 2 | 2.25 | 2.5 | 2.75 | 3 | 3.25 | 3.5 |

The older Pointer Options slider has 11 ticks. They are pointer speeds 1, 2, 4, 6 … 20, so its
middle tick is 10.

A speed must be 1 to 128. A pointer speed that needs less than 1, or more than 128, cannot be
matched on the main screen, and the page says so; crossings still land on the edge, but the main
screen feels a little different from the others. **Enable Acceleration** changes only the main
Windows screen too. Screens of different sizes on one Windows computer cannot all be matched
([#313](https://github.com/myn/deskhopplus/issues/313)).

### The cursor jumps to the middle of the screen, or the clipboard fills with screenshots

Another keyboard-and-mouse sharing program is running. Deskflow did this on a Mac after a reboot,
where it starts itself: it warps the cursor to the centre of the main screen and syncs clipboards
on its own. Quit it. In Terminal, `pgrep -il deskflow` says whether it is running.

### On Windows the cursor lands in the bottom-right corner

An administrator window (a UAC prompt, an elevated app) had focus when you crossed. The helper may
not move the cursor while such a window is in front, so it stays where the board parked it. Click
a normal window; the next crossing is placed correctly.

### A managed Windows PC prompts, or refuses, when the DESKHOP drive appears

Corporate endpoint software such as Trellix treats the config-mode drive as removable storage. It
may ask you to justify it each time, and it may refuse writes, which also blocks the drag-and-drop
firmware upgrade on that PC. Nothing in DeskHopPlus can change that policy. Use the Mac, or any
unmanaged computer, for config mode and firmware upgrades. Pairing is unaffected: the pairing
window opens on the reboot back, whether or not the drive mounted. [#58](https://github.com/myn/deskhopplus/issues/58).

### Caps Lock does not blink

The keyboard LEDs do not show config mode or acknowledge a chord, although the original DeskHop
manual says they do. Watch the LED on the board instead; it blinks in config mode. Do not read a
still Caps Lock as "the chord did not work". [#101](https://github.com/myn/deskhopplus/issues/101).

### The keyboard is dead after the board reboots

Unplug the keyboard from the board and plug it back in. The board flashes its LED twice when it
sees a new keyboard.

### Nothing pastes, no message

Check the menu on the paste side. A file offer waits there. If the menu says **Connected and
paired** on both computers and the copy was under the size cap, check the direction is not blocked
on the config page.

## Uninstall

### macOS

1. Click the menu bar item. If **Start at login** is ticked, click it to untick it. (If the
   helper is not running, double-click it first.)
2. Click **Quit DeskHopPlus Helper**.
3. In Terminal:

```sh
rm -f ~/Library/LaunchAgents/com.deskhopplus.helper.plist ~/Library/LaunchAgents/com.deskhopplus.helper.plist.disabled
rm -rf ~/Library/Application\ Support/deskhopplus "$TMPDIR/deskhopplus" /tmp/deskhop-helper.log
```

4. Delete the helper file from wherever you put it.

That removes the helper's key handle, the remembered board key and any received files. There is
nothing else.

### Windows

1. Right-click the tray icon. If **Start at logon** is ticked, click it to untick it. (If the
   helper is not running, run the exe first.) This removes the logon entry it made.
2. Click **Quit DeskHopPlus Helper**.
3. Delete `%LOCALAPPDATA%\deskhopplus\`.
4. Delete the exe.

### The boards

The boards keep their pairings and settings until wiped. To unpair, quit both helpers, then press
`Right Shift + F12 + D` on the keyboard plugged into the board. That erases every setting on both
boards. The wipe also opens a pairing window, so a helper still running re-pairs within the
minute. To go back to a
stock DeskHop, flash DeskHop's own `.uf2` the same way you flashed this one.

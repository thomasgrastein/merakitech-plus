/* merakiplus.c — Meraki+ (identifier: merakiplus): a custom display UI for the round
 * 480x480 screen, v6.16: feature parity (settings + activity control), its own visual
 * design system, boot-slot integration, product name + two languages — and from v6.4 a SIM build.
 *
 * v6.22 = v6.21 + WATER DOSING GATE + MANUAL HOT WATER (docs/water-dosing-compat.md §10-§11). Hot water by weight (AUTO: the controller
 *  stops at word 17) exists only on controllers > 5.1.2. The controller's firmware string (words 80..87, copy 88..95 — already in the
 *  sub56 len56 poll) is parsed with the stock rule (mcu_has_hot_water_weight_ui 0x10885C: > 5.1.2); empty / not polled / garbage / copy
 *  mismatch / scale fault (word 35 & 0x1400) -> MANUAL ONLY: the WATER page hides HOT WATER + WATER MODE, frame_policy rejects word 16 = 1.
 *  NEW: manual hot water — start cmd6 sub19 len1 word 19 = 1, stop word 19 = 0 (stock legacy page 0x105118); also for WATER MODE = MANUAL
 *  on a dosing controller (v6.21: "use stock UI"). No time limit, no display-side weight stop (as stock). A scale fault stops a running AUTO
 *  dose once (word 16 = 0, stock 0x115BD8). DIAGNOSTICS › CONTROLLER shows the firmware string. config.json is never written by the gate.
 * v6.20 = v6.19 + STEAM DURING A BREW (docs/concurrent-brew-steam.md §9, hardware-verified §11): act_guard() lets a latched steam
 *  start through when a brew (pre-infusion or extraction) is the ONLY running activity (no steam bits, word 24 = 0, no grind / hot-water
 *  bit, no error, queue idle). HOME shows HOLD·STEAM next to STOP BREW during a brew; once steam latches it is STOP STEAM (the existing
 *  two-button layout and stop paths). Steam during grind / hot water and every other start stay blocked; no brew start during steam
 *  is offered. The frames are unchanged (the stock steam start cmd6 sub24 len8). No new power/thermal guard: the MCU time-slices the heaters.
 * v6.19 = v6.18.1 + (1) NO READY TONE AT A POWER-ON WITH HOT BOILERS (user: jingle + both ready tones back-to-back is redundant): at every
 *  power-on (cold boot of the display, OFF -> on, sleep -> wake) a boiler whose heat stage is already 100 over the first 1.5 s counts as
 *  announced — only the jingle plays; a tone needs a real cold -> hot transition during the session (re-arm after cooling unchanged).
 *  (2) nicer sounds: tools/gen_audio.py now synthesizes struck-instrument timbres (marimba / glockenspiel partials + a soft FM bell + a
 *  small room) instead of sine beeps; the three audio/<name>.wav files regenerated. Control logic, frames, the helper and the settings are unchanged.
 * v6.18.1 = v6.18 + a fix in the sound helper only (src/merakiplus_snd.c puts libpthread.so.0 into the global scope before
 *  libmi_common.so; docs/audio.md §3). In this file only MERAKIPLUS_VERSION changed.
 *
 * v6.18 = v6.17 + SOUND (UI_v6.md "CHANGES-v6.18", docs/audio.md). The stock UI plays a jingle at power-on (the audio track of
 *  video/power.mp4, page_logo 0x1001B8) and ONE "pre-heat complete" sound when BOTH boilers are hot (uartRxProcess 0x145BA0: words 62 AND 63
 *  > 99, once per power-on). Meraki+ plays its OWN short tones (audio/<name>.wav, made by tools/gen_audio.py): a start jingle once per
 *  power-on and a DIFFERENT ready tone per boiler — extract (word 62, the MCU's heat stage, reaches 100) and steam (word 63) — so you
 *  can hear which boiler is ready. Once per power-on, re-armed only after real cooling (stage <= 30 or 15 °C below the ready point);
 *  silent while off/asleep; dropped while an activity runs (like stock). SYSTEM › DISPLAY · SOUND: SOUND (off / on / stock sounds),
 *  READY TONE (off / brew / steam / both boilers) — own file /new_customer/merakiplus/sound — and VOLUME (config "volume", moved here
 *  from STOCK UI, capped at 70 like stock). Playback via the helper merakiplus_snd (glibc; the display's own libmi_ao.so, exactly the
 *  MI_AO calls of stock's player libhvplayer.so). DIAGNOSTICS › SOUND shows mode · volume and the last event. No UART frame changed.
 * v6.16 = v6.15 + TIME & SCHEDULING (UI_v6.md "CHANGES-v6.16", docs/time-scheduling.md). User bugs: (1) TIME FORMAT was inverted —
 *  stock update_time 0xD5944 XORs model[0x59] with 1 before the test (missed in v6.1): timemode 1 = 24 h, 0 = 12 h A.M./P.M.; (2) no clock —
 *  HOME's top line now shows the local time (SoC UTC + stock zone table 0x1A5890 + DST when sumtime is on), DIAGNOSE a TIME · SCHEDULE
 *  block; (3) no planned pre-heat — a schedule (on/off, hh:mm, once / every day / Mon-Fri / Sat-Sun) whose next occurrence goes out as
 *  the cmd8 alarm with the power-off frame exactly as stock (performSleepActions 0x148324 -> powerOffSend 0x14700C(day, hour, minute))
 *  and is pre-armed while on; the MCU's sleep task wakes on it (reset -> heating; the display cold-boots -> boot push). Plus optional
 *  AUTO OFF after 15/20/30/60 min without touch or activity (stock: fixed 30 min, SleepRefresh 0x149268). Schedule + auto off live in
 *  merakiplus's own file /new_customer/merakiplus/schedule; config.json gets only timemode/sumtime as before. The cmd8 date/time is now stock's
 *  local time (v6.14 sent the SoC's UTC).
 * v6.15 = v6.14 + the HOME graph RESETS WHEN AN ACTIVITY STARTS (UI_v6.md "CHANGES-v6.15" + §14.7). User bug: grind/brew/water start
 *  tares the weight to 0 (MCU), but the graph's 40 s window still held the pre-start history (e.g. a 200-300 g cup placed on the
 *  scale), so the autoscale was dominated by the cup step and the 0 -> 36 g curve was a flat line at the bottom. Now graph_push()
 *  clears the ring buffer (and restarts the chart filters) on the IDLE -> ACTIVE transition of the main phase (phase_main: grind,
 *  brew, water, or steam alone); the scale is recomputed from the fresh buffer, so the curve starts at 0 and fills the height.
 *  A concurrent secondary activity (steam/wand flush during a brew or grind) is NOT an idle -> active transition and keeps the
 *  curve. Within 1.5 s of the start the buffer is re-anchored once more if the weight source latches late (a physical grind
 *  start) or the tare lands late (weight drops > 2 g in one sample). One ring buffer holds pressure, weight and milk, so all
 *  series are action-relative (flow is word 52, a stat number without history). DISPLAY ONLY: regs, control, auto-stop and
 *  thresholds read the raw words; the DIAGNOSTICS RAW view still shows raw (unconditioned) values of the buffer.
 * v6.14 = v6.13 + the PHYSICAL POWER BUTTON (UI_v6.md "CHANGES-v6.14" + §19). User bug: the button did nothing in merakiplus —
 *  merakiplus only read the touch node (event0); the button is the k62091 node (event1, EV_KEY), which stock's app_powerKey_thread (0x140AB4)
 *  reads. The stock semantics are reversed (dealPowerState 0x14911C / powerOff 0x149074 / SleepRefresh 0x149268) and mirrored:
 *  HOLD the button (stock: two one-second ticks = 1-2 s; merakiplus 1.5 s) -> cmd8 time/alarm frame (alarm 0/0/0 = none) + 80 ms ->
 *  cmd6 sub0 len2 = [0, 0] (word 0 = power off, word 1 = awake) -> OFF view, backlight off, keep-awake paused.
 *  Blocked (like stock's isExtractionOrSteamSwitchOn 0x1481E0) while brew/steam/grind is running — stock NEVER stops an activity.
 *  The MCU (sleep task 0x2F52) deletes the status task itself (no UART replies), resets work_sta + outputs and wakes ONLY on a
 *  hold of the button (>= 0.8 s, its own GPIO PC.8 -> MCU reset 0x326) or an alarm. merakiplus: button again -> TURNING ON… -> when
 *  the controller replies with word 0 = 1 -> PowerOn (sub0=[1] + sub1=[0], as at boot) -> HOME. No SoC suspend
 *  (stock: echo mem > /sys/power/state + killall demo_v9) — net watchdog and telnet keep running; only the backlight goes off.
 * v6.13 = v6.12 + MANUAL STOP for WATER, BREW and GRIND (UI_v6.md "CHANGES-v6.13" + §18). User bug: hot water could not be
 *  stopped from the screen — only steam had a STOP button. The stop words are reversed from the MCU firmware and cross-checked against
 *  the stock UI's own stop calls: WATER cmd6 sub16 len1 word16=0 (+ sub19 len1 word19=0, if word 19 != 0), BREW cmd6 sub22 len1
 *  word22=0, GRIND cmd6 sub6 len1 word6=0 (addresses at act_push_stop()). HOME shows the STOP button (STOP WATER / STOP BREW /
 *  STOP GRIND) in the same place as STOP STEAM while the activity's bits are set; with latched steam at the same time = two buttons side by side.
 *  Press -> toast "STOPPING …" -> frame (CRC + ACK + one retransmit) -> "… STOPPED" once the MCU has cleared the bits; otherwise
 *  "STOP: NO REPLY" / "STOP NOT CONFIRMED" (5 s). Auto-stop is unchanged (merakiplus sends nothing when the MCU stops by itself).
 *  v6.12 = the v6.11 UI (source = merakiplus_v6_12.c, binary 725386cc…) + the boot agent's gesture fix (boot/GESTUS-FIX.md).
 *
 * v6.10 = v6.9 + PRESSURE ESTIMATE in bar (UI_v6.md "CHANGES-v6.10" + §16): "PRESSURE* UNCALIBRATED" is replaced by "≈BAR · EST.".
 *  One linear anchor: the pre-infusion plateau in the capture (BREW.md §6: word 51 = 358 raw at 6.06 s) = config
 *  preInfusionPressure (4 bar) -> k = preInfusionPressure / P_ANCHOR_RAW bar per raw unit (0.01117). The anchor is read from
 *  config (SET[S_PIPRESS]), so a changed config changes the scale. Above P_EST_MAX (20 bar) the linear value is not physical
 *  (deadhead/maintenance pulses up to 16670 raw = 186 bar): the BAR field shows "PULSE", DIAGNOSTICS "MAINTENANCE PULSE" — never
 *  a number. The basis is shown on DIAGNOSTICS (new block PRESSURE · ESTIMATE) and in a toast when tapping the chart/BAR field. The RAW view
 *  (DIAGNOSTICS → GRAPH) shows word 51 as raw numbers. The section "v6.10: pressure estimate" below has the constants and caveats.
 *  Control, writes, auto-stop and thresholds still read only raw values — v6.10 only changes what is displayed.
 *
 * v6.9 = v6.8 + DISPLAY CONDITIONING of the graph and the hero number (UI_v6.md "CHANGES-v6.9" + §14): the chart and
 *  hero/gauge/BAR show median-3 + EMA of the raw samples; the chart scale ignores short deadhead pulses (they are shown as
 *  small ticks along the top edge). The ring buffer and reg[] are still RAW — control, writes, auto-stop, thresholds and
 *  DIAGNOSTICS counters are unchanged. DIAGNOSTICS → GRAPH switches to the raw view (this session only). The section
 *  "v6.9: display conditioning" below has the constants and how to tune them.
 *
 * v6.8 = v6.7 without the REVERT button on HOME (UI_v6.md "CHANGES-v6.8"): it was a phase-2 safeguard.
 *  HOME = gauge + stats + activity icons + page dots; there is no touch zone where the pill used to be.
 *  The escape routes are unchanged: SYSTEM "SWITCH TO ORIGINAL NOW" (in run.sh mode = clean exit, run.sh SIGCONTs)
 *  + the BOOT UI row, the switcher gesture and run.sh's trap.
 *
 * v6.7 = v6.6 + two things (UI_v6.md "CHANGES-v6.7"):
 *  - NET WATCHDOG: wifi at boot + self-heal (the section "NET" below). On a slot B boot nothing started
 *    wifi (INSTALL-REPORT §3.1). merakiplus now does what stock's libmi_wlan does according to /config/wifi/wlan.json —
 *    init script, conf from config.json (byte-identical w. save_config), wpa_supplicant and udhcpc with
 *    stock's arguments — but ONLY when the stack is down. A live wpa_supplicant is never touched, and still
 *    no signal is sent to any process (0 kill calls). DIAGNOSTICS has a NET row; log in
 *    /tmp/merakiplus_net.log; `merakiplus --net-status` (read-only) and `--net-watch [s]`.
 *  - WORD 113 IN THE BOOT PUSH: the lo byte is sent as 0 (stock's captured boot push, GROUND-TRUTH.md);
 *    the SAVE path still packs breakTime.
 *
 * v6.6 = v6.5 + three things (UI_v6.md "CHANGES-v6.6"):
 *  - PHASE SEMANTICS per STEAMLIVE.md (proven live): two steam paths — LATCHED (word 24 = 1, UI start,
 *    auto-stop at word 48 >= word 25: STEAM·AUTO w. target and STOP button) and MOMENTARY wand/flush (bits 5+14
 *    without word 24 = 1: STEAM·WAND, no auto-stop expectation, no STOP button). Concurrent phases
 *    (brew bits + steam bits, 0x5327): HOME shows the main activity AND a steam marker, weight + milk temp.
 *    The state contract's phase (phase_raw: steam first) is unchanged; phase_main() is new.
 *  - 64-BIT CLOCK: all times are ms_t (int64_t). Up to and including v6.5 the clock was a 32-bit `long` on the device and
 *    wrapped after 24.86 days (poll/render could stall, keep-awake could run away). The main loop's
 *    timers live in `lt` and are driven by loop_poll()/loop_graph()/loop_render_due()/loop_rendered().
 *  - RE-PUSH AFTER MCU RESTART: if the controller's version request (0x89) comes again after the
 *    first handshake, or words 112/113 hold the MCU's start values (0 / 0), the boot push is sent
 *    again — same blocks, same frame policy, same activity guard. Counted on DIAGNOSTICS.
 *
 * v6.5 = v6.4 + two fixes in the DEVICE build (UI_v6.md "CHANGES-v6.5"):
 *  - BOOT PUSH on the device too: at boot cmd6 sub1 len30 + cmd6 sub112 len3 are sent from
 *    config.json — the same two blocks as the stock UI's fuart_thread — through the frame policy and
 *    the write queue (ACK wait + one retransmit). On a slot B boot the stock UI never starts, and
 *    the controller would otherwise sit on its own start values (brew 92.0 °C / group 50 °C).
 *    ACTIVITY GUARD: if an activity is already running the first time work_sta is known, the
 *    push is skipped (run.sh started in the middle of a brew: the block would reset the start words).
 *  - RX RESYNC WATCHDOG: a header candidate with an impossible length is discarded immediately, and an incomplete
 *    candidate that has waited RX_RESYNC_MS is discarded, so the parser finds the next AA 55. No bytes
 *    are consumed without a CRC verdict. New counter on DIAGNOSTICS (RX CRC / RESYNC) + boot push status.
 *
 * v6.4 = v6.3 + SIM HAL against the simulated test environment (sim/CONTRACT.md; UI_v6.md "CHANGES-v6.4"):
 *  - -DMERAKIPLUS_SIM (without MERAKIPLUS_PREVIEW) = REAL-TIME build without hardware: framebuffer -> memory
 *    + file, touch -> FIFO w. raw 16-byte evdev records, UART -> pty, all system paths under a
 *    sandbox root, state JSONL per frame and a ctrl socket w. test hooks. Same main loop,
 *    same protocol, save, activity and drawing code as the device.
 *  - WITHOUT -DMERAKIPLUS_SIM all of this is gone: the source after the preprocessor is the same as
 *    v6.3 except for the version string and one toast text (see below).
 *  - BOOT PUSH (sub1 len30 + sub112 len3, like the stock UI's fuart_thread) existed in v6.4 ONLY in
 *    the SIM and preview builds (contract requirement). From v6.5 the device build sends it too (see above).
 *  - Toast on SAVE without ACK: "SAVE FAILED · NO ACK · NOTHING SAVED" (before: "NO ACK · NOTHING SAVED").
 *
 * v6.3 = v6.2 + product name + internationalisation (see UI_v6.md "CHANGES-v6.3", REVERT.md):
 *  - PRODUCT NAME: the UI is called Meraki+ (splash, HOME title line w. "+" badge, DIAGNOSTICS/ABOUT,
 *    exit texts). No generation name in any UI string — the same software runs on several
 *    machine generations. Paths, binaries and logs use the identifier merakiplus (run.sh/boot).
 *  - I18N: ONE string table (STR_TABLE) w. English (default) + Danish for EVERY visible text;
 *    lookup per render (TR()), so a language switch takes effect live without a restart.
 *  - SYSTEM row UI LANGUAGE / UI-SPROG: saved in merakiplus's OWN file /new_customer/merakiplus/lang
 *    ("en"/"da", atomic; missing/unreadable/unknown -> English). The stock UI's config.language
 *    is not touched; its row (STOCK LANG. / STOCK-SPROG) is independent.
 *  - Numbers, decimal point and units (°C, g, s, bar) are the same in both languages; only words are translated.
 *  - Protocol, frame policy, keep-awake, EVIOCGRAB, circle clip, boot logic and run.sh: UNCHANGED.
 *
 * v6.2 = v6.1 + boot-slot integration (see UI_v6.md "CHANGES-v6.2", REVERT.md):
 *  - SYSTEM row BOOT UI: MERAKIPLUS/ORIGINAL for the NEXT reboot = the flag file /customer/new
 *    (touch/rm). Only when /new_customer is mounted and its demo_v9 is merakiplus; otherwise locked.
 *  - SYSTEM action SWITCH TO ORIGINAL NOW: runtime switch without reboot per boot/BOOT.md §9.2
 *    (close UART/fb/touch -> fork+setsid+execl /customer/demo_v9 -> wait 400 ms -> exit).
 *  - REVERT/exit: visible reminder when the stock UI continues on an old RAM mirror.
 *  - DIAGNOSTICS: boot flag, exe slot, switcher daemon, latest switch event (scroll).
 *  - Protocol, frame policy, keep-awake, EVIOCGRAB, circle clip and run.sh: UNCHANGED.
 *
 * v6.1 = v6 reconciled against PARITY.md / parity_spec.json (see UI_v6.md "CHANGES-v6.1"):
 *  - SET[] uses spec-proven limits/steps; estimated max values are marked SF_EST (visible hint).
 *  - brewMode/sleepMode/sleepModeSetting removed (no stock editor); breakTime locked
 *    (MCU 0x1E10 overwrites it); hot-water TEMPERATURE is shown as a sensor (word 40).
 *  - Central frame policy in wq_push_t(): trigger words 26/27/30 can NEVER be != 0,
 *    start words only w. explicit intent, and every word must lie within its spec interval.
 *
 * Phase 2 soft-replace (unchanged contract): run.sh SIGSTOPs demo_v9, merakiplus owns
 * /dev/fb0 + /dev/ttyS1 + /dev/input/event0, run.sh SIGCONTs demo_v9 on exit.
 *
 * v6 vs v4 (details in UI_v6.md):
 *  - KEEP-AWAKE FIX: word1 is written 0 (=awake). v4 wrote 1 (=sleep/drain) — bug.
 *  - SETTINGS: 24 rows on 3 pages (list -> stepper detail -> SAVE/CANCEL).
 *    Save = cmd6 write (where the word is known) + atomic config.json persist
 *    (tmp+rename, pristine+rolling backup, md5 log). The UI is the only range guard.
 *  - ACTIVITIES: GRIND/BREW/WATER/STEAM w. observed recipes (hold-to-start),
 *    STOP for steam (word24=0), auto-stop expectation in UI text.
 *  - PHASES: steam = work_sta bits 5+14 (0x4020); sleep = word1==1 / bit8 cleared.
 *  - GRAPHICS: anti-aliased stroke font (own, no dependencies), AA primitives,
 *    270-degree gauge w. LUT, retained compositor w. dirty rects, page slide,
 *    crossfade, touch ripple, toasts, boot splash. Global circle clip kept.
 *
 * Build (device):  zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 -o merakiplus_v6 merakiplus.c
 * Build (SIM):     zig cc -DMERAKIPLUS_SIM -O2 -o merakiplus_v6_sim merakiplus.c -lm          (host; the one run_e2e.py starts)
 *                  zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 -DMERAKIPLUS_SIM -o merakiplus_v6_sim_arm merakiplus.c
 *                  env: MERAKIPLUS_SIM_TTY, MERAKIPLUS_SIM_FB, MERAKIPLUS_SIM_STATE (required), MERAKIPLUS_SIM_ROOT (required),
 *                  MERAKIPLUS_SIM_TOUCH, MERAKIPLUS_SIM_CTRL, MERAKIPLUS_SIM_NOBOOTPUSH=1, MERAKIPLUS_SIM_NOPROVISION=1,
 *                  MERAKIPLUS_SIM_BOOTPUSH_GUARD=1 (the device's activity guard: skip instead of defer),
 *                  MERAKIPLUS_SIM_UPTIME_MS=<n> (v6.6: the clock starts at n ms — real-time wrap test; exists only in the SIM build)
 * Build (preview): zig cc -DMERAKIPLUS_PREVIEW -O2 -o /tmp/merakiplus_v6_preview merakiplus.c
 *                  /tmp/merakiplus_v6_preview [--lang en|da] <outdir>   (all pages/states -> .bin)
 *                  /tmp/merakiplus_v6_preview [--lang en|da] --selftest (frame vectors, config save, i18n)
 *                  /tmp/merakiplus_v6_preview --bench         (ms/frame)
 *                  /tmp/merakiplus_v6_preview --dump-table    (SET[] as TSV -> parity_check.py)
 *                  /tmp/merakiplus_v6_preview --dump-strings  (the string table as TSV: id, class, EN, DA)
 * Env (device):    MERAKIPLUS_CONFIG, MERAKIPLUS_BAKDIR, MERAKIPLUS_NOGRAB=1, MERAKIPLUS_NOBL=1, MERAKIPLUS_READONLY=1,
 *                  MERAKIPLUS_NOBOOTPUSH=1 (v6.5: no boot push at startup — v6.4 behaviour),
 *                  MERAKIPLUS_LANGFILE=<file> (the language file; default /new_customer/merakiplus/lang),
 *                  MERAKIPLUS_SWITCH_DRY=1 (SWITCH NOW only logs what it would do; merakiplus keeps running),
 *                  MERAKIPLUS_ROOT=<dir> (prefix for the boot paths /customer /new_customer /proc /tmp; test only)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <termios.h>
#include <math.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <dirent.h>
/* v6.6: the time base — monotonic milliseconds in 64 bits (rationale and now_ms(): see "TIME BASE" below) */
typedef int64_t ms_t;
/* v6.4: three build types.
 *   DEVICE_RT      (default)                        the device: /dev/fb0, /dev/ttyS1, /dev/input/event0
 *   MERAKIPLUS_PREVIEW  (-DMERAKIPLUS_PREVIEW)                offline previews + selftest (synthetic time)
 *   SIM_RT         (-DMERAKIPLUS_SIM without MERAKIPLUS_PREVIEW) real time against sim/mcu_sim.py (sim/CONTRACT.md)
 * Everything SIM-specific is in "#ifdef SIM_RT" blocks or in the macros below, which are EMPTY
 * (or equal to the v6.3 expression) in the device build. */
#if defined(MERAKIPLUS_SIM) && !defined(MERAKIPLUS_PREVIEW)
#define SIM_RT 1
#endif
#if !defined(MERAKIPLUS_PREVIEW) && !defined(SIM_RT)
#define DEVICE_RT 1
#endif
#ifdef DEVICE_RT
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#ifdef SIM_RT
#include <sys/socket.h>
#include <sys/un.h>
/* contract §4: raw 16-byte evdev record (= struct input_event on 32-bit ARM), little-endian */
struct input_event { int32_t sec, usec; uint16_t type, code; int32_t value; };
_Static_assert(sizeof(struct input_event)==16,"evdev record must be 16 bytes");
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_ABS 0x03
#define SYN_REPORT 0
#define BTN_TOUCH 0x14a
#define ABS_X 0x00
#define ABS_Y 0x01
#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36
#define ABS_MT_TRACKING_ID 0x39
#endif
/* ---- HAL macros: the device branch is verbatim v6.3 ---- */
#ifdef SIM_RT
static char sim_tty[300]="";
static int  sim_rc=0;
static void sim_perr(const char*s);
static void sim_toast(const char*m,uint32_t col);
static int  sim_ev_next(struct input_event*ev,ms_t now);
static void sim_env(void);
static void sim_start(void);
static void sim_tick(ms_t now);
static void sim_frame(ms_t now);
static void sim_exit(void);
static int  sim_bye_begin(int mode,ms_t now);
static int  sim_ka_hold=0;
static int  sim_net_on=1;
#define UART_DEV sim_tty
#define UART_ERR_RC 3
#define UART_PERR(s) sim_perr(s)
#define INP_READ(ev) sim_ev_next(&(ev),now)
#define INP_UNGRAB
#define SYS_SYNC
#define SIM_SIG(s) if((s)==SIGINT) sim_rc=130;
#define SIM_TOAST(m,col) sim_toast(m,col);
#define SIM_ENV sim_env();
#define SIM_START sim_start();
#define SIM_TICK(now) sim_tick(now);
#define SIM_FRAME(now) sim_frame(now);
#define SIM_EXIT sim_exit();
#define SIM_DRY_BYE(mode,now) if(sim_bye_begin(mode,now)) return;
#define SIM_KA_HOLD if(sim_ka_hold) return;
#define MAIN_RC sim_rc
#else
#define UART_DEV UART
#define UART_ERR_RC 1
#define UART_PERR(s) perror(s)
#define INP_READ(ev) read(ifd,&ev,sizeof(ev))==sizeof(ev)
#define INP_UNGRAB ioctl(ifd,EVIOCGRAB,0);
#define SYS_SYNC sync();
#define SIM_SIG(s)
#define SIM_TOAST(m,col)
#define SIM_ENV
#define SIM_START
#define SIM_TICK(now)
#define SIM_FRAME(now)
#define SIM_EXIT
#define SIM_DRY_BYE(mode,now)
#define SIM_KA_HOLD
#define MAIN_RC 0
#endif

#define W 480
#define H 480
#define CX 240
#define CY 240
#define RSAFE 234          /* safety radius (bezel) — all content clips to this */
#define REGN 129
#define UART "/dev/ttyS1"
#define FB   "/dev/fb0"
#define INP  "/dev/input/event0"
#define INP_KBD "/dev/input/event1"   /* v6.14: the power button (k62091, EV_KEY) — stock app_powerKey_thread 0x140AB4 opens exactly this one */
#define KBD_NAME "k62091"
#define BL_SYSFS "/sys/class/pwm/pwmchip0/pwm2/duty_cycle"
#define BL_EN_SYSFS "/sys/class/pwm/pwmchip0/pwm2/enable"   /* v6.14: stock uikit_os_back_light_off/on 0xC2278 = blpwm_set_enable(2,0/1) */
#define MERAKIPLUS_VERSION "V6.22"

/* ---- v6.10: pressure estimate (UI_v6.md §16) ----
 * Word 51 (SPI2 ADC channel 0xA, REGISTERMAP: pressure OR pump-drive candidate) -> bar with ONE linear anchor:
 *   P0 = P_ANCHOR_RAW = 358 raw: the pre-infusion plateau in the capture BREW.md §6 (UART capture 20:54:05-06.7, 6.06 s,
 *        median 356, span 332..366; STEAMLIVE.md confirms ~360 / 6.84 s), captured with config preInfusionPressure = 4.
 *   anchor pressure = config preInfusionPressure (SET[S_PIPRESS], default P_ANCHOR_BAR_DEF = 4; outside 1..15 -> default).
 *   k = anchor pressure / P0 bar per raw unit (4/358 = 0.011173); displayed bar = word51 x k (pre 358 -> 4.0; ext 1346 -> 15.0).
 * CAVEATS (also shown on DIAGNOSTICS and in UI_v6.md §16):
 *   (a) ext ~15 bar is plausible for a vibration pump WITHOUT an OPV, but it is pump-side pressure, not pressure at the puck.
 *   (b) deadhead/maintenance pulses (up to 16670 raw = 186 bar linear) are NOT physical under a linear scale: above
 *       P_EST_MAX (20 bar) no number is shown — the BAR field shows "PULSE", DIAGNOSTICS "MAINTENANCE PULSE" / "PULSE".
 *   (c) a two-point affine scale (rest 0 + pre 4 + ext 9) is inconsistent -> word 51 may be non-linear or a
 *       pump-drive level; hence an ESTIMATE, not a certified calibration. Raw is always available (DIAGNOSTICS → GRAPH).
 * Display only: control, writes, auto-stop and thresholds never use bar. Tuning: -DP_ANCHOR_RAW=… -DP_EST_MAX=… at build time. */
#ifndef P_ANCHOR_RAW
#define P_ANCHOR_RAW 358
#endif
#ifndef P_ANCHOR_BAR_DEF
#define P_ANCHOR_BAR_DEF 4
#endif
#ifndef P_EST_MAX
#define P_EST_MAX 20.f
#endif

/* ---- TIME BASE (v6.6): monotonic milliseconds in 64 bits ----
 * Up to and including v6.5 the clock was a `long`. On the device (ARMv7, ILP32) `long` is 32 bits, so the clock wrapped
 * 2^31 ms = 24.86 days after boot: timers compared absolutely (poll, render, keep-awake)
 * could stall for 49.7 days, and any time difference over 24.86 days became negative ("DONE" chip,
 * CRC warning and toast that came back). ms_t is 64 bits in all build types: the clock does not wrap
 * (292 million years), and ALL time values — points in time, deadlines and differences — are ms_t.
 * The sum is formed in 64 bits (time_t is 64 bits on musl and on the host) and only assigned afterwards.
 * (The typedef is at the top of the file, before the HAL macros.) */
_Static_assert(sizeof(ms_t)==8,"the time base must be 64 bits");
#ifdef SIM_RT
static int64_t sim_clk_off=0;                              /* MERAKIPLUS_SIM_UPTIME_MS: the clock starts at a chosen value (real-time wrap test) */
#define CLK_OFF sim_clk_off
#else
#define CLK_OFF 0
#endif
static ms_t ts_ms(const struct timespec*ts){ int64_t v=(int64_t)ts->tv_sec*1000+ts->tv_nsec/1000000+CLK_OFF; return (ms_t)v; }
static ms_t now_ms(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts_ms(&ts); }

/* ================= CRC-16/MODBUS ================= */
static uint16_t crc16(const uint8_t *d, int n){
    uint16_t c=0xFFFF;
    for(int i=0;i<n;i++){ c^=d[i]; for(int k=0;k<8;k++) c = (c&1)? (c>>1)^0xA001 : c>>1; }
    return c;
}

/* ================= register mirror ================= */
static uint16_t reg[REGN];
static int reg_seen[REGN];
static uint32_t rx_ok=0, rx_bad=0;      /* v6.6: uint32 — wraps in a well-defined way (2^32 frames = approx. 5 years at 26/s) */
static ms_t last_rx_ms=-100000, last_bad_ms=-100000;

/* ================= UART ================= */
static int ufd=-1;
static uint8_t rxbuf[8192]; static int rxlen=0;
static uint8_t txq[1024]; static int txq_len=0;
static int ro_mode=0;                 /* MERAKIPLUS_READONLY=1: no cmd6 writes at all */

#ifndef MERAKIPLUS_PREVIEW
static void uart_open(void){
    ufd=open(UART_DEV, O_RDWR|O_NOCTTY|O_NONBLOCK);
    if(ufd<0){ perror("open ttyS1"); exit(UART_ERR_RC); }
    struct termios t; tcgetattr(ufd,&t);
    cfmakeraw(&t);
    cfsetispeed(&t,B57600); cfsetospeed(&t,B57600);
    t.c_cflag &= ~(PARENB|CSTOPB|CRTSCTS);
    t.c_cflag |= CS8|CREAD|CLOCAL;
    t.c_cc[VMIN]=0; t.c_cc[VTIME]=0;
    tcsetattr(ufd,TCSANOW,&t);
    tcflush(ufd,TCIOFLUSH);
}
static void uart_flush(void){
    int off=0;
    while(off<txq_len){
        int n=write(ufd,txq+off,txq_len-off);
        if(n<=0){ if(errno==EAGAIN||errno==EWOULDBLOCK) break; UART_PERR("write uart"); break; }
        off+=n;
    }
    if(off>0){ memmove(txq,txq+off,txq_len-off); txq_len-=off; }
}
#endif
static void uart_send(const uint8_t*b,int n){
    if(txq_len+n<=(int)sizeof(txq)){ memcpy(txq+txq_len,b,n); txq_len+=n; }
}
static void frame_query(uint8_t sub,uint8_t len){
    uint8_t b[7]={0xAA,0x55,0x03,sub,len,0,0};
    uint16_t c=crc16(b,5); b[5]=c&0xFF; b[6]=c>>8;
    uart_send(b,7);
}
/* builds a cmd6 frame in out (>= 2*len+7 bytes); returns the length */
static int build_write(uint8_t*b,uint8_t sub,const uint16_t*w,int len){
    int n=0;
    b[n++]=0xAA;b[n++]=0x55;b[n++]=0x06;b[n++]=sub;b[n++]=(uint8_t)len;
    for(int i=0;i<len;i++){ b[n++]=w[i]&0xFF; b[n++]=w[i]>>8; }
    uint16_t c=crc16(b,n); b[n++]=c&0xFF; b[n++]=c>>8;
    return n;
}
static void frame_version(void){
    const char*v="1.1.1-69"; int L=strlen(v);
    uint8_t b[64]; int n=0;
    b[n++]=0xAA;b[n++]=0x55;b[n++]=0x09;b[n++]=(uint8_t)L;b[n++]=0x00;
    memcpy(b+n,v,L); n+=L;
    uint16_t c=crc16(b,n); b[n++]=c&0xFF; b[n++]=c>>8;
    uart_send(b,n);
}

/* ---- write queue: sequential cmd6 writes, each w. CRC + ACK wait + ONE retransmit ----
 * ACK = 0x86 w. the same sub/len and echo == payload. Timeout 150 ms -> retransmit,
 * then 300 ms -> FAIL (the rest of the queue is dropped; the UI shows the error). */
#define WQ_MAX 8
#define WQ_WMAX 30         /* the boot push block sub1 len30 (v6.5: all build types; the v6.4 device had 24) */
#define WQ_ACK1_MS 150
#define WQ_ACK2_MS 300
typedef struct { uint8_t sub,len; uint16_t w[WQ_WMAX]; } WFrame;
enum { WQ_IDLE=0, WQ_WAIT };
enum { WR_NONE=0, WR_OK, WR_FAIL };
static struct { WFrame q[WQ_MAX]; int n; int state; ms_t sent_ms; int tries; int result; long seq; int sim_ack; long n_ok,n_fail,n_retx,n_block; } wq;
static uint8_t last_tx[80]; static int last_tx_len=0;   /* latest cmd6 frame (selftest/diag) */

/* ---- FRAME POLICY (PARITY §1.5 + §6.1-6.4): last line of defence before the UART ----
 * The MCU has NO range checks (cmd6 = memcpy + ACK), and certain words are triggers that
 * the MCU consumes: 3/6/9 = start/tare, 26/27 = weight calibration (writes MCU flash),
 * 30 = MCU REBOOT. Therefore EVERY cmd6 frame — settings, activities, keep-awake — passes
 * this one gate, which rejects frames that
 *   (a) lie outside the write bands (words 0..31 / 112..114),
 *   (b) have a trigger word != 0 without the caller explicitly asking for it (intent), or
 *   (c) have a word outside its spec interval (word_ok(), derived from SET[]).
 * No intent can allow TRIG_NEVER — merakiplus never calibrates and never reboots. */
#define WBIT(n) (1u<<(n))
#define TRIG_NEVER (WBIT(26)|WBIT(27)|WBIT(30))
#define TRIG_START (WBIT(3)|WBIT(6)|WBIT(9)|WBIT(16)|WBIT(19)|WBIT(22)|WBIT(24))   /* v6.22: + 19 (manual hot water) */
#define I_NONE  0u
#define I_GRIND (WBIT(3)|WBIT(6))      /* grind-start + grind-tare   (sub3 len4)  */
#define I_BREW  (WBIT(9)|WBIT(22))     /* start trigger + brew active (sub5 len18) */
#define I_WATER (WBIT(9)|WBIT(16))     /* start trigger + auto flag  (sub9 len11) */
#define I_STEAM (WBIT(24))             /* steam switch               (sub24 len8) */
/* v6.22: MANUAL hot water = word 19 alone (sub19 len1), the stock legacy water page's frame: set_manual_hot_water_switch 0x105118
 * usart_send(6,19,1) (word 19 = 1 start / 0 stop). Word 16 = 1 (AUTO, weight-dosed) is additionally gated on the controller version in
 * frame_policy (wd_dosing(), docs/water-dosing-compat.md). */
#define I_WATERMAN (WBIT(19))         /* manual hot water           (sub19 len1) */
/* v6.14: word 0 = 0 (PowerOff) is not a start word but a "power off" word: it may ONLY be sent with this explicit intent
 * (pw_off_step, stock powerOff 0x149074: usart_send(6,0,2) with word 0 = 0). Without intent it is rejected (WJ_TRIG). */
#define I_POWEROFF (WBIT(0))
_Static_assert(((I_GRIND|I_BREW|I_WATER|I_WATERMAN|I_STEAM)&TRIG_NEVER)==0,"no intent may allow words 26/27/30");
_Static_assert(((I_GRIND|I_BREW|I_WATER|I_WATERMAN|I_STEAM)&~TRIG_START)==0,"intent may only name start words");
_Static_assert((I_POWEROFF&(TRIG_NEVER|TRIG_START))==0,"power-off intent is neither a start word nor a service word");
_Static_assert((TRIG_NEVER&TRIG_START)==0,"the trigger classes must not overlap");
enum { WJ_NONE=0, WJ_RO, WJ_FULL, WJ_BAND, WJ_TRIG, WJ_RANGE };
static int wq_rej=WJ_NONE;             /* reason for the latest rejection (selftest/diag) */
static int frame_policy(uint8_t sub,const uint16_t*w,int len,uint32_t intent);   /* defined after SET[] */

static int wq_busy(void){ return wq.n>0; }
static int wq_push_t(uint8_t sub,const uint16_t*w,int len,uint32_t intent){
    wq_rej=WJ_NONE;
    if(ro_mode){ wq_rej=WJ_RO; return 0; }
    if(wq.n>=WQ_MAX||len>WQ_WMAX||len<1){ wq_rej=WJ_FULL; return 0; }
    /* write-band guard (REGISTERMAP §3): words 0..31 or words 112..114 */
    int end=sub+len;
    if(!(end<33 || (sub>=112 && end>=113 && end<=115))){ wq_rej=WJ_BAND; wq.n_block++; return 0; }
    int pj=frame_policy(sub,w,len,intent);
    if(pj){ wq_rej=pj; wq.n_block++; return 0; }
    WFrame*f=&wq.q[wq.n++]; f->sub=sub; f->len=(uint8_t)len; memcpy(f->w,w,len*2);
    if(wq.n==1){ wq.state=WQ_IDLE; wq.result=WR_NONE; }
    return 1;
}
static int wq_push(uint8_t sub,const uint16_t*w,int len){ return wq_push_t(sub,w,len,I_NONE); }   /* without intent: no start words */
static void wq_tx(ms_t now){
    WFrame*f=&wq.q[0]; uint8_t b[2*WQ_WMAX+7]; int n=build_write(b,f->sub,f->w,f->len);
    uart_send(b,n); memcpy(last_tx,b,n); last_tx_len=n; wq.sent_ms=now;
}
static void wq_pop(int ok){
    if(ok){ wq.n_ok++; memmove(&wq.q[0],&wq.q[1],sizeof(WFrame)*(wq.n-1)); wq.n--; if(wq.n==0){ wq.result=WR_OK; wq.seq++; } }
    else { wq.n_fail++; wq.n=0; wq.result=WR_FAIL; wq.seq++; }
    wq.state=WQ_IDLE;
}
static void wq_step(ms_t now){
    if(wq.n==0) return;
    if(wq.state==WQ_IDLE){ wq.tries=1; wq.state=WQ_WAIT; wq_tx(now); return; }
    if(wq.sim_ack && now-wq.sent_ms>=40){   /* preview only: simulated ACK */
        WFrame*f=&wq.q[0]; for(int k=0;k<f->len&&f->sub+k<REGN;k++) reg[f->sub+k]=f->w[k];
        wq_pop(1); return;
    }
    if(wq.tries==1 && now-wq.sent_ms>=WQ_ACK1_MS){ wq.tries=2; wq.n_retx++; wq_tx(now); }
    else if(wq.tries>=2 && now-wq.sent_ms>=WQ_ACK2_MS){ wq_pop(0); }
}
static void wq_on_ack(uint8_t sub,uint8_t L,const uint8_t*payload){
    if(wq.n==0||wq.state!=WQ_WAIT) return;
    WFrame*f=&wq.q[0];
    if(f->sub!=sub||f->len!=L) return;
    for(int k=0;k<L;k++) if(f->w[k]!=(uint16_t)(payload[2*k]|(payload[2*k+1]<<8))) return;
    wq_pop(1);
}

/* ---- BOOT PUSH: state (v6.5: all build types; the logic is at boot_push_step) ---- */
enum { BP_OFF=0, BP_WAIT_A, BP_A, BP_WAIT_B, BP_B, BP_DONE };
enum { BPR_NONE=0, BPR_OK, BPR_NOACK, BPR_BLOCKED, BPR_NOCFG, BPR_SKIP };
static struct { int state, ra, rb, runs, guard; long seq; ms_t t0; int t0_set; } bp;   /* t0: when the push left WAITING (block queued / settled) */
static int bp_on=1;                    /* MERAKIPLUS_NOBOOTPUSH / MERAKIPLUS_SIM_NOBOOTPUSH turn the boot push off */
static int repush_count(void);         /* v6.6: number of re-pushes after an MCU restart (repush_step) */

/* ---- RX frame parse (v4 logic + ACK queue + timestamps) — v6.5: RESYNC WATCHDOG ----
 * A header candidate (AA 55 …) only becomes a frame when the WHOLE frame has been received and the CRC
 * matches; otherwise it advances one byte and searches for the next AA 55. No bytes are consumed
 * without a CRC verdict (v6.4 skipped cmd 3 and cmd 8/0x88 without CRC and could thus swallow the start of
 * a valid frame right after noise). The only thing that can keep the parser stuck is a candidate whose
 * length field promises more bytes than have arrived. That situation has two guards:
 *  (1) IMPOSSIBLE LENGTH: LEN > 129 words does not exist (the register file is 129 words) -> discarded immediately.
 *      (v6.4: the parser stayed stuck here until the 8 kB buffer overflowed — approx. 3.5 s without RX.)
 *  (2) WATCHDOG: an incomplete candidate that has waited RX_RESYNC_MS is discarded; the rest of
 *      the buffer is searched for the next AA 55, so valid frames that arrived BEHIND it are not lost.
 *      Bytes whose latest arrival is RX_RESYNC_MS old cannot be a frame in transit and
 *      are discarded the same way. main() calls rx_feed(…,0,now) on every pass, so the guard also
 *      works when nothing arrives.
 * RX_RESYNC_MS = 400 ms, chosen from the measurements (UI_v6.md CHANGES-v6.5 item 3):
 *  - the longest valid frame (2*129+7 = 265 bytes) takes 46 ms on the line at 57600 8N1, and
 *    the MCU sends a frame in one go — a valid gap IN THE MIDDLE of a frame has not been seen;
 *  - the MCU's longest measured silence is 279 ms (first ACK after a start write, BREW.md §3.2);
 *    400 ms is 1.4x that — a frame split by such a long gap is still received;
 *  - 400 ms is below the write queue's ACK budget (150 + 300 = 450 ms), so a stuck candidate in front of
 *    an ACK does not by itself turn into "SAVE FAILED", and far below the OFFLINE limit (1500 ms).
 * Gaps BETWEEN frames (poll cadence ~100 ms, measured up to 456 ms) do not affect the watchdog:
 * the buffer is empty in between, and it only measures a candidate waiting for the rest of itself.
 * rx_resync (DIAGNOSTICS "RX CRC / RESYNC") counts discarded candidates: impossible length, watchdog
 * (one per call) and buffer overflow. CRC errors on a complete frame are counted in rx_bad as before. */
#define RX_RESYNC_MS 400
static uint32_t rx_resync=0;
/* v6.6 (MCU restart -> re-push, see repush_step): the controller's version requests and fresh reads of words 112..113 */
static unsigned ver_n=0, rx_n112=0; static ms_t ver_ms=0;
static ms_t rx_pend_ms=0, rx_last_ms=0;     /* when the head of rxbuf started waiting / when the latest byte arrived */
static void pw_ack8(const uint8_t*p);       /* v6.14: the controller's acknowledgement (0x88) of the power-off sequence's time/alarm frame */
static void rx_feed(const uint8_t*d,int n,ms_t now){
    if(n<=0&&rxlen==0) return;
    if(rxlen+n>(int)sizeof(rxbuf)){ rxlen=0; rx_resync++; }
    int old_n=rxlen; ms_t old_last=rx_last_ms;
    if(n>0){ if(rxlen==0) rx_pend_ms=now; memcpy(rxbuf+rxlen,d,n); rxlen+=n; rx_last_ms=now; }
    /* the latest time bytes from position i can have started waiting: the head has its own clock,
     * older bytes behind it arrived at the latest at the previous arrival, and new bytes arrived now */
#define RX_REF(i) ((i)==0?rx_pend_ms:((i)<old_n?old_last:now))
    int i=0, fired=0;
    while(i+7<=rxlen){
        if(rxbuf[i]!=0xAA||rxbuf[i+1]!=0x55){ i++; continue; }
        uint8_t cmd=rxbuf[i+2]; int ver=(cmd==0x09||cmd==0x89), tot;
        if(ver) tot=rxbuf[i+3]+7;                           /* version frames: LEN is bytes (PROTOCOL §2.2) */
        else if(cmd==0x08||cmd==0x88) tot=15;               /* power/time: fixed 15 bytes (§2.3) */
        else if(cmd==0x03) tot=7;                           /* query: no payload */
        else if(rxbuf[i+4]>REGN){ rx_resync++; i++; continue; }   /* (1) impossible length */
        else tot=2*rxbuf[i+4]+7;
        if(i+tot>rxlen){                                    /* incomplete: wait — but at most RX_RESYNC_MS */
            if(now-RX_REF(i)<RX_RESYNC_MS) break;
            fired=1; i++; continue;                         /* (2) watchdog */
        }
        uint16_t rx=crc16(rxbuf+i,tot-2);
        uint16_t got=rxbuf[i+tot-2]|(rxbuf[i+tot-1]<<8);
        if(rx!=got){ rx_bad++; last_bad_ms=now; i++; continue; }
        if(ver){ rx_ok++; last_rx_ms=now; if(cmd==0x89){ frame_version(); ver_n++; ver_ms=now; } }
        else if(cmd==0x88) pw_ack8(rxbuf+i+3);                 /* v6.14: only counted for DIAGNOSTICS — stock does not wait for it either */
        else if(cmd!=0x08&&cmd!=0x88&&cmd!=0x03){
            uint8_t sub=rxbuf[i+3], L=rxbuf[i+4];
            rx_ok++; last_rx_ms=now;
            if(cmd==0x83||cmd==0x86){
                for(int k=0;k<L;k++){
                    int idx=sub+k; if(idx>=REGN) break;
                    reg[idx]=rxbuf[i+5+2*k]|(rxbuf[i+6+2*k]<<8); reg_seen[idx]=1;
                }
                if(cmd==0x83&&sub<=112&&sub+L>113) rx_n112++;
                if(cmd==0x86) wq_on_ack(sub,L,rxbuf+i+5);
            }
        }
        i+=tot;
    }
    /* fewer than 7 bytes left: keep only what can be the start of a header — and only while it is fresh */
    if(i+7>rxlen){
        while(i<rxlen&&!(rxbuf[i]==0xAA&&(i+1>=rxlen||rxbuf[i+1]==0x55))) i++;
        if(i<rxlen&&now-RX_REF(i)>=RX_RESYNC_MS){ fired=1; i=rxlen; }
    }
    if(fired) rx_resync++;
    if(i>0){ rx_pend_ms=RX_REF(i); memmove(rxbuf,rxbuf+i,rxlen-i); rxlen-=i; }
#undef RX_REF
}

/* ================= MD5 (for the config log; RFC 1321) ================= */
typedef struct { uint32_t s[4]; uint64_t len; uint8_t buf[64]; int nb; } MD5;
static const uint32_t MD5K[64]={
 0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
 0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
 0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
 0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
 0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
 0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
 0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
 0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
static const uint8_t MD5R[64]={7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
 4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
static void md5_block(MD5*m,const uint8_t*p){
    uint32_t a=m->s[0],b=m->s[1],c=m->s[2],d=m->s[3],x[16];
    for(int i=0;i<16;i++) x[i]=p[4*i]|(p[4*i+1]<<8)|(p[4*i+2]<<16)|((uint32_t)p[4*i+3]<<24);
    for(int i=0;i<64;i++){
        uint32_t f; int g;
        if(i<16){ f=(b&c)|(~b&d); g=i; }
        else if(i<32){ f=(d&b)|(~d&c); g=(5*i+1)&15; }
        else if(i<48){ f=b^c^d; g=(3*i+5)&15; }
        else { f=c^(b|~d); g=(7*i)&15; }
        uint32_t t=d; d=c; c=b;
        uint32_t r=a+f+MD5K[i]+x[g]; b=b+((r<<MD5R[i])|(r>>(32-MD5R[i]))); a=t;
    }
    m->s[0]+=a; m->s[1]+=b; m->s[2]+=c; m->s[3]+=d;
}
static void md5_hex(const uint8_t*data,size_t n,char out[33]){
    MD5 m; m.s[0]=0x67452301; m.s[1]=0xefcdab89; m.s[2]=0x98badcfe; m.s[3]=0x10325476;
    size_t i=0; for(;i+64<=n;i+=64) md5_block(&m,data+i);
    uint8_t t[128]; size_t r=n-i; memcpy(t,data+i,r); t[r++]=0x80;
    size_t pad=(r<=56)?56:120; memset(t+r,0,pad-r);
    uint64_t bits=(uint64_t)n*8; for(int k=0;k<8;k++) t[pad+k]=(uint8_t)(bits>>(8*k));
    md5_block(&m,t); if(pad==120) md5_block(&m,t+64);
    for(int k=0;k<4;k++) for(int j=0;j<4;j++) sprintf(out+8*k+2*j,"%02x",(m.s[k]>>(8*j))&0xFF);
    out[32]=0;
}

/* ================= sysroot + boot-id (v6.2) =================
 * All boot-related paths (/customer, /new_customer, /proc, /tmp) go through rp(), so
 * the flag/switch logic can be run against a mock root (MERAKIPLUS_ROOT / selftest). Empty = real root. */
static char sysroot[200]="";
static const char* rp(const char*p){
    static char b[4][400]; static int k;
    if(!sysroot[0]) return p;
    char*o=b[k++&3]; snprintf(o,400,"%s%s",sysroot,p); return o;
}
static int64_t mono_fake=-1;                               /* selftest only */
static int64_t mono_s(void){ return mono_fake>=0?mono_fake:now_ms()/1000; }   /* seconds since boot (64 bit, v6.6) */
static void boot_id_read(char*out,size_t n){
    out[0]=0; int fd=open(rp("/proc/sys/kernel/random/boot_id"),O_RDONLY); if(fd<0) return;
    int k=read(fd,out,n-1); close(fd); if(k<0)k=0; out[k]=0; out[strcspn(out," \r\n")]=0;
}

/* ================= config.json: read + atomic field persist =================
 * Pattern from WRITETEST.md §7: the byte-exact cJSON format is preserved (only the number token of
 * the one field is replaced), tmp file in the SAME directory + fsync + verify + rename().
 * demo_v9 (SIGSTOPped) holds a write fd on the old inode -> no race.
 * Before the first write: pristine backup (config.json.orig, never overwritten).
 * Before every write: rolling backup (config.json.bak). Every write is logged w. md5. */
#define CFG_MAX 16384
static char cfg_path[256]="/customer/config.json";
static char cfg_bakdir[256]="/new_customer/merakiplus/cfgbak";
static char cfgbuf[CFG_MAX]; static int cfglen=0; static int cfg_ok=0;
static char cfg_md5[33]="-"; static int cfg_writes=0; static char cfg_err[64]="";

static int cfg_read(void){
    cfg_ok=0; cfglen=0;
    int fd=open(cfg_path,O_RDONLY); if(fd<0){ snprintf(cfg_err,sizeof cfg_err,"open: %s",strerror(errno)); return 0; }
    int n=read(fd,cfgbuf,CFG_MAX-1); close(fd);
    if(n<=2){ snprintf(cfg_err,sizeof cfg_err,"empty file"); return 0; }
    if(n>=CFG_MAX-1){ snprintf(cfg_err,sizeof cfg_err,"too large"); return 0; }
    cfglen=n; cfgbuf[n]=0; cfg_ok=1; md5_hex((uint8_t*)cfgbuf,cfglen,cfg_md5);
    return 1;
}
/* find the value span for a top-level key (depth 1; ignores presetStore contents) */
static int cfg_find(const char*buf,int len,const char*key,int*vs,int*ve){
    int depth=0,i=0,kl=strlen(key),hits=0;
    while(i<len){
        char c=buf[i];
        if(c=='"'){
            int s=i+1; i++;
            while(i<len&&buf[i]!='"'){ if(buf[i]=='\\') i++; i++; }
            int e=i; i++;
            if(depth==1){
                int j=i; while(j<len&&(buf[j]==' '||buf[j]=='\t'||buf[j]=='\r'||buf[j]=='\n')) j++;
                if(j<len&&buf[j]==':'&&e-s==kl&&!memcmp(buf+s,key,kl)){
                    j++; while(j<len&&(buf[j]==' '||buf[j]=='\t')) j++;
                    int a=j; if(j<len&&buf[j]=='-') j++;
                    while(j<len&&buf[j]>='0'&&buf[j]<='9') j++;
                    if(j>a&&(buf[j]==','||buf[j]=='\n'||buf[j]=='\r'||buf[j]=='}'||buf[j]==' '||buf[j]=='\t')){ *vs=a; *ve=j; hits++; }
                }
            }
            continue;
        }
        if(c=='{'||c=='[') depth++;
        else if(c=='}'||c==']') depth--;
        i++;
    }
    return hits==1;
}
static int cfg_get_int(const char*key,int*out){
    int a,b; if(!cfg_ok||!cfg_find(cfgbuf,cfglen,key,&a,&b)) return 0;
    *out=atoi(cfgbuf+a); return 1;
}
static int write_all(const char*path,const char*data,int n,int excl){
    int fd=open(path,O_WRONLY|O_CREAT|(excl?O_EXCL:O_TRUNC),0644); if(fd<0) return 0;
    int off=0; while(off<n){ int k=write(fd,data+off,n-off); if(k<=0){ close(fd); return 0; } off+=k; }
    if(fsync(fd)!=0){ close(fd); return 0; }
    close(fd); return 1;
}
static void fsync_dir(const char*filepath){
    char d[256]; snprintf(d,sizeof d,"%s",filepath); char*s=strrchr(d,'/'); if(!s) return; if(s==d) s[1]=0; else *s=0;
    int fd=open(d,O_RDONLY); if(fd>=0){ fsync(fd); close(fd); }
}
/* returns 1 on success; on failure config.json is unchanged and cfg_err is set */
static int cfg_set_int(const char*key,int newv){
    char cur[CFG_MAX]; char md_before[33],md_after[33];
    /* 1) fresh read (never from cache) */
    int fd=open(cfg_path,O_RDONLY); if(fd<0){ snprintf(cfg_err,sizeof cfg_err,"open: %s",strerror(errno)); return 0; }
    int n=read(fd,cur,CFG_MAX-1); close(fd);
    if(n<=2||n>=CFG_MAX-1){ snprintf(cfg_err,sizeof cfg_err,"read error"); return 0; }
    cur[n]=0; md5_hex((uint8_t*)cur,n,md_before);
    int a,b; if(!cfg_find(cur,n,key,&a,&b)){ snprintf(cfg_err,sizeof cfg_err,"field missing: %s",key); return 0; }
    int oldv=atoi(cur+a);
    /* 2) backups */
    mkdir(cfg_bakdir,0755);
    char p[320];
    snprintf(p,sizeof p,"%s/config.json.orig",cfg_bakdir);
    if(access(p,F_OK)!=0){ if(!write_all(p,cur,n,1)){ snprintf(cfg_err,sizeof cfg_err,"orig backup failed"); return 0; } fsync_dir(p); }
    char pt[340]; snprintf(p,sizeof p,"%s/config.json.bak",cfg_bakdir); snprintf(pt,sizeof pt,"%s.tmp",p);
    if(!write_all(pt,cur,n,0)||rename(pt,p)!=0){ snprintf(cfg_err,sizeof cfg_err,"backup failed"); return 0; }
    /* 3) build the new content: only the number token is replaced */
    char nb[CFG_MAX]; char num[16]; int nl=snprintf(num,sizeof num,"%d",newv);
    int nn=a+nl+(n-b); if(nn>=CFG_MAX-1){ snprintf(cfg_err,sizeof cfg_err,"too large"); return 0; }
    memcpy(nb,cur,a); memcpy(nb+a,num,nl); memcpy(nb+a+nl,cur+b,n-b); nb[nn]=0;
    /* 4) tmp in the same directory -> fsync -> re-read+compare -> rename */
    char tmp[300]; snprintf(tmp,sizeof tmp,"%s",cfg_path);
    char*sl=strrchr(tmp,'/'); if(sl) snprintf(sl+1,sizeof tmp-(sl+1-tmp),".merakiplus_cfg.tmp"); else snprintf(tmp,sizeof tmp,".merakiplus_cfg.tmp");
    if(!write_all(tmp,nb,nn,0)){ snprintf(cfg_err,sizeof cfg_err,"tmp-write: %s",strerror(errno)); unlink(tmp); return 0; }
    char chk[CFG_MAX]; fd=open(tmp,O_RDONLY); int cn=fd>=0?read(fd,chk,CFG_MAX):-1; if(fd>=0) close(fd);
    if(cn!=nn||memcmp(chk,nb,nn)){ snprintf(cfg_err,sizeof cfg_err,"tmp verification failed"); unlink(tmp); return 0; }
    if(rename(tmp,cfg_path)!=0){ snprintf(cfg_err,sizeof cfg_err,"rename: %s",strerror(errno)); unlink(tmp); return 0; }
    fsync_dir(cfg_path);
    md5_hex((uint8_t*)nb,nn,md_after);
    /* 5) log + flag */
    snprintf(p,sizeof p,"%s/config_writes.log",cfg_bakdir);
    FILE*lf=fopen(p,"a");
    if(lf){
        time_t t=time(NULL); struct tm tmv; gmtime_r(&t,&tmv); char ts[32]; strftime(ts,sizeof ts,"%Y-%m-%dT%H:%M:%SZ",&tmv);
        fprintf(lf,"%s %s %d->%d md5_before=%s md5_after=%s size=%d->%d\n",ts,key,oldv,newv,md_before,md_after,n,nn);
        fflush(lf); fsync(fileno(lf)); fclose(lf);
    }
    snprintf(p,sizeof p,"%s/RESTART_NEEDED",cfg_bakdir);
    lf=fopen(p,"w"); if(lf){ fprintf(lf,"config.json changed by merakiplus. The controller already has the values (cmd6 on SAVE).\n"
                           "The stock UI (demo_v9) reads config ONLY at startup: restart it (kill $(pidof demo_v9);\n"
                           "the watchdog starts it again) BEFORE stock settings are used. At startup it boot-pushes\n"
                           "the config values to the controller (cmd6 sub1 len30 + sub112 len3).\n");
                /* stamp for restart_pending(): a stock UI started AFTER this has read config itself */
                char bid[64]; boot_id_read(bid,sizeof bid); fprintf(lf,"boot_id=%s mono_s=%lld\n",bid[0]?bid:"?",(long long)mono_s()); fclose(lf); }
    memcpy(cfgbuf,nb,nn+1); cfglen=nn; cfg_ok=1; strcpy(cfg_md5,md_after); cfg_writes++;
    return 1;
}

/* ====================================================================== */
/* ================= NET: wifi at startup + self-heal (v6.7) ============ */
/* ====================================================================== */
/* The stock UI brings wifi up ITSELF (libmi_wlan.so driven by /config/wifi/wlan.json) — demo.sh does
 * not. On a slot B boot the stock UI never starts, and the machine had no network (INSTALL-REPORT §3.1):
 * neither the driver (8188fu.ko), wpa_supplicant nor DHCP was started. The mechanism is reverse-proven on
 * the device (wlan.json, ssw01bInit.sh, libmi_wlan.so, demo_v9's _wifi_init/_wifi_open/_wifi_connect and
 * stock's own strace capture; see UI_v6.md "CHANGES-v6.7" + net_re/NET-REVERSE.md). Stock does:
 *   1. MI_WLAN_Init:    sh -c "source /config/wifi/ssw01bInit.sh dummy1 dummy2 "   (mkdir /tmp/wifi/run,
 *                       insmod /config/wifi/8188fu.ko, mdev -s, wait for wlan0 for up to 10 s)
 *   2. MI_WLAN_Open:    sh -c "ifconfig wlan0 up  ; wpa_supplicant -i wlan0 -Dnl80211 -c
 *                       /appconfigs/wpa_supplicant.conf  & sleep 1  ; "
 *   3. MI_WLAN_Connect: wpa_cli add_network / set_network ssid, psk, key_mgmt WPA-PSK / select_network /
 *                       enable_network / save_config  — save_config writes the network block into
 *                       /appconfigs/wpa_supplicant.conf (demo.sh copies .conf_bak over it on EVERY boot)
 *                       and:  udhcpc -i wlan0 -s /etc/init.d/udhcpc.script -a -q -b -t 20 -T 1 &
 *      (-q: udhcpc exits once it has a lease — which is why there is no udhcpc in normal operation)
 *   Environment for the children: LD_LIBRARY_PATH and PATH get ":/config/wifi" appended.
 * merakiplus does the same, but only when the stack is DOWN:
 *   - the init script is run ONLY when wlan0 does not exist (an insmod of a loaded driver fails);
 *   - the conf file is written directly from config.json (ssid/password) in EXACTLY the format save_config produces
 *     (byte-identical, md5-proven) — and only when the content differs; then wpa_supplicant is started with the
 *     same arguments, and 1 s later udhcpc with the same arguments — unless wlan0 still has its
 *     IP and default route (heal after a dead supplicant): then they are kept (same IP; stock never runs
 *     udhcpc again in operation either, -q);
 *   - a live wpa_supplicant is NEVER killed — merakiplus does not send a signal to any process (0 kill calls).
 *     If it is alive but wlan0 is administratively DOWN, "ifconfig wlan0 up" is done (like MI_WLAN_Open).
 *     If the IP is missing (or the default route, if there has been one) and no udhcpc is running, udhcpc is started
 *     (after NET_NOIP_MS / NET_NOGW_MS, with backoff). Without carrier it waits (the supplicant reconnects by itself).
 * Everything is non-blocking: the main loop calls net_step(); slow steps are child processes.
 * Log: /tmp/merakiplus_net.log (transitions only; the password is never written). MERAKIPLUS_NONET=1 turns the watchdog off. */
#define NET_IF          "wlan0"
#define NET_WIFI_DIR    "/config/wifi"
#define NET_WPA_BIN     NET_WIFI_DIR "/wpa_supplicant"
#define NET_INIT_SH     NET_WIFI_DIR "/ssw01bInit.sh"
#define NET_CONF        "/appconfigs/wpa_supplicant.conf"
#define NET_CONF_BAK    "/appconfigs/wpa_supplicant.conf_bak"
#define NET_RUN_DIR     "/tmp/wifi/run"
#define NET_CTRL        NET_RUN_DIR "/wpa_supplicant"
#define NET_DHCP_BIN    "/sbin/udhcpc"
#define NET_DHCP_SH     "/etc/init.d/udhcpc.script"
#define NET_LOG         "/tmp/merakiplus_net.log"
#define NET_LOG_MAX     131072
#define NET_CONF_HDR    "ctrl_interface=" NET_CTRL "\nupdate_config=1\n"
#define NET_PROBE_MS       1000     /* while something is in progress */
#define NET_PROBE_UP_MS    2000     /* healthy stack: one /proc lookup + carrier + SIOCGIFADDR */
#define NET_PROBE_IDLE_MS  5000     /* OFF / waiting for the next attempt */
#define NET_DEAD_MS        3000     /* wpa_supplicant gone this long -> bring-up (at startup: immediately) */
#define NET_WPA_SETTLE_MS  1000     /* stock: "& sleep 1" */
#define NET_NOIP_MS        8000     /* supplicant alive, no IP, no udhcpc -> udhcpc */
#define NET_NOGW_MS        3000     /* carrier + IP, but the default route is gone (e.g. after ifconfig down/up) -> udhcpc */
#define NET_IFDOWN_MS      3000     /* supplicant alive, wlan0 administratively down -> ifconfig wlan0 up */
#define NET_DHCP_MAX_MS    60000    /* backoff ceiling for repeated udhcpc starts within the same outage */
#define NET_INIT_TMO_MS    25000    /* the init script itself waits at most ~10 s for wlan0; after that it is logged (not killed) */
#define NET_BACKOFF0_MS    5000
#define NET_BACKOFF_MAX_MS 60000
#define NET_CFG_RECHECK_MS 30000
enum { NS_OFF=0, NS_DOWN, NS_STARTING, NS_CONNECTING, NS_UP };
enum { NB_NONE=0, NB_INIT, NB_WPA };
enum { NC_OK=0, NC_NOCFG, NC_WIFIOFF, NC_NOSSID, NC_BADPSK };
/* ip: a.b.c.d = a<<24|b<<16|c<<8|d; 0 = none. ifup = IFF_UP ("ifconfig wlan0 up"). gw = default route via wlan0 */
typedef struct { int iface, ifup, wpa_pid, dhcp_pid, carrier, gw; uint32_t ip; } NetProbe;
static struct {
    int on, state, step, cfg, cfg_logged, booted, was_up, acted, dead, job, orphan, init_hung, backoff, gw_seen, carrier_was, dhcp_wait, ifdown;
    NetProbe p;
    ms_t t_probe, t_dead, t_noip, t_step, t_retry, t_down, t_cfg, t_ifdown, heal_ms;
    int n_heal, n_boot, n_init, n_wpa, n_dhcp, n_conf, n_fail, n_ifup;
    char ssid[100], psk[80], conf_md5[33], evt[200]; int ssid_n;
} net;
static ms_t net_now=0; static int net_echo=0;
static const char*const NC_TXT[]={"ok","config.json cannot be read","wifionoff = 0","no (or too long) ssid in config.json","password: invalid length/characters (0 or 8..63 ASCII, like stock)"};

/* top-level STRING value (depth 1) w. JSON escapes -> out; returns the length, -1 if the key is missing / not a string */
static int cfg_find_str(const char*buf,int len,const char*key,char*out,int on){
    int depth=0,i=0,kl=(int)strlen(key);
    while(i<len){
        char c=buf[i];
        if(c=='"'){
            int s=i+1; i++;
            while(i<len&&buf[i]!='"'){ if(buf[i]=='\\') i++; i++; }
            int e=i; i++;
            if(depth==1&&e-s==kl&&!memcmp(buf+s,key,(size_t)kl)){
                int j=i; while(j<len&&(buf[j]==' '||buf[j]=='\t'||buf[j]=='\r'||buf[j]=='\n')) j++;
                if(j<len&&buf[j]==':'){
                    j++; while(j<len&&(buf[j]==' '||buf[j]=='\t')) j++;
                    if(j<len&&buf[j]=='"'){
                        int n=0; j++;
                        while(j<len&&buf[j]!='"'){
                            unsigned ch=(unsigned char)buf[j++];
                            if(ch=='\\'&&j<len){
                                char q=buf[j++];
                                if(q=='n') ch='\n'; else if(q=='t') ch='\t'; else if(q=='r') ch='\r'; else if(q=='b') ch='\b'; else if(q=='f') ch='\f';
                                else if(q=='u'){
                                    unsigned u=0; int k=0;
                                    for(;k<4&&j<len&&isxdigit((unsigned char)buf[j]);k++,j++){ char h=buf[j]; u=u*16+(unsigned)(h<='9'?h-'0':(h|32)-'a'+10); }
                                    if(k<4) return -1;
                                    if(u<0x80) ch=u;
                                    else if(u<0x800){ if(n+2>=on) return -1; out[n++]=(char)(0xC0|(u>>6)); ch=0x80|(u&0x3F); }
                                    else { if(n+3>=on) return -1; out[n++]=(char)(0xE0|(u>>12)); out[n++]=(char)(0x80|((u>>6)&0x3F)); ch=0x80|(u&0x3F); }
                                } else ch=(unsigned char)q;
                            }
                            if(n+1>=on) return -1;
                            out[n++]=(char)ch;
                        }
                        if(j>=len) return -1;
                        out[n]=0; return n;
                    }
                }
            }
            continue;
        }
        if(c=='{'||c=='[') depth++;
        else if(c=='}'||c==']') depth--;
        i++;
    }
    return -1;
}
/* wifionoff / ssid / password are read FRESH from config.json (own buffer: the settings model is not touched) */
static void net_cfg_load(void){
    static char b[CFG_MAX]; int a,e,on=1;
    net.ssid[0]=net.psk[0]=0; net.ssid_n=0;
    int fd=open(cfg_path,O_RDONLY); int n=fd>=0?(int)read(fd,b,CFG_MAX-1):-1; if(fd>=0) close(fd);
    if(n<=2){ net.cfg=NC_NOCFG; return; }
    b[n]=0;
    if(cfg_find(b,n,"wifionoff",&a,&e)) on=atoi(b+a);
    int sn=cfg_find_str(b,n,"ssid",net.ssid,(int)sizeof net.ssid), pn=cfg_find_str(b,n,"password",net.psk,(int)sizeof net.psk);
    if(pn<0){ pn=0; net.psk[0]=0; }
    if(sn<0){ sn=0; net.ssid[0]=0; }
    net.ssid_n=sn;
    if(!on){ net.cfg=NC_WIFIOFF; return; }
    if(sn<=0||sn>32){ net.cfg=NC_NOSSID; return; }
    if(pn!=0&&(pn<8||pn>63)){ net.cfg=NC_BADPSK; return; }
    for(int i=0;i<pn;i++) if((unsigned char)net.psk[i]<32||(unsigned char)net.psk[i]>=127){ net.cfg=NC_BADPSK; return; }
    net.cfg=NC_OK;
}
/* the conf file in save_config format. Global lines (ctrl_interface, update_config, ...) are taken from the vendor template
 * (.conf_bak — the one demo.sh copies on every boot), otherwise from the current conf, otherwise the default.
 * ssid: "text" — or hex without quotes if it has bytes < 32 or >= 127 (wpa_config_write_string). */
static int net_conf_build(char*out,int max){
    static const char*const src[2]={NET_CONF_BAK,NET_CONF};
    char hdr[1024]; int hn=0;
    for(int s=0;s<2&&!hn;s++){
        char raw[2048]; int fd=open(rp(src[s]),O_RDONLY); if(fd<0) continue;
        int n=(int)read(fd,raw,sizeof raw-1); close(fd); if(n<=0) continue;
        raw[n]=0;
        char*nb=strstr(raw,"network={"); if(nb) *nb=0;
        if(!strstr(raw,"ctrl_interface=")) continue;
        for(char*ln=raw;*ln;){
            char*nl=strchr(ln,'\n'); int L=nl?(int)(nl-ln):(int)strlen(ln), LL=L;
            while(LL>0&&(ln[LL-1]=='\r'||ln[LL-1]==' '||ln[LL-1]=='\t')) LL--;
            if(LL>0&&hn+LL+1<(int)sizeof hdr){ memcpy(hdr+hn,ln,(size_t)LL); hn+=LL; hdr[hn++]='\n'; }
            ln+=L+(nl?1:0);
        }
    }
    if(!hn){ hn=(int)strlen(NET_CONF_HDR); memcpy(hdr,NET_CONF_HDR,(size_t)hn); }
    hdr[hn]=0;
    int hex=0; for(int i=0;i<net.ssid_n;i++){ unsigned char c=(unsigned char)net.ssid[i]; if(c<32||c>=127) hex=1; }
    char ss[80];
    if(hex){ for(int i=0;i<net.ssid_n&&i<32;i++) snprintf(ss+2*i,3,"%02x",(unsigned char)net.ssid[i]); }
    else snprintf(ss,sizeof ss,"\"%s\"",net.ssid);
    int n;
    if(net.psk[0]) n=snprintf(out,(size_t)max,"%s\nnetwork={\n\tssid=%s\n\tpsk=\"%s\"\n\tkey_mgmt=WPA-PSK\n}\n",hdr,ss,net.psk);
    else           n=snprintf(out,(size_t)max,"%s\nnetwork={\n\tssid=%s\n\tkey_mgmt=NONE\n}\n",hdr,ss);
    return (n>0&&n<max)?n:-1;
}
/* 0 = the file already has exactly this content (not touched), 1 = written (tmp + fsync + rename), -1 = error */
static int net_conf_sync(void){
    char want[4096],have[4096],path[420],tmp[440];
    int wn=net_conf_build(want,sizeof want); if(wn<0) return -1;
    md5_hex((const uint8_t*)want,(size_t)wn,net.conf_md5);
    snprintf(path,sizeof path,"%s",rp(NET_CONF));
    int fd=open(path,O_RDONLY); int hn=fd>=0?(int)read(fd,have,sizeof have):-1; if(fd>=0) close(fd);
    if(hn==wn&&!memcmp(have,want,(size_t)wn)) return 0;
    snprintf(tmp,sizeof tmp,"%s.merakiplus.tmp",path);
    if(!write_all(tmp,want,wn,0)){ unlink(tmp); return -1; }
    if(rename(tmp,path)!=0){ unlink(tmp); return -1; }
    fsync_dir(path);
    return 1;
}
static const char* net_ipstr(uint32_t ip,char*b,size_t n){
    if(!ip) snprintf(b,n,"-"); else snprintf(b,n,"%u.%u.%u.%u",(unsigned)(ip>>24),(unsigned)((ip>>16)&255),(unsigned)((ip>>8)&255),(unsigned)(ip&255));
    return b;
}
static void net_log(const char*fmt,...){
    char m[200],path[420]; va_list ap; va_start(ap,fmt); vsnprintf(m,sizeof m,fmt,ap); va_end(ap);
    snprintf(net.evt,sizeof net.evt,"%s",m);
    snprintf(path,sizeof path,"%s",rp(NET_LOG)); struct stat st;
    if(stat(path,&st)==0&&st.st_size>NET_LOG_MAX){ char old[440]; snprintf(old,sizeof old,"%s.1",path); rename(path,old); }
    FILE*f=fopen(path,"a");
    if(f){
        time_t t=time(NULL); struct tm tmv; gmtime_r(&t,&tmv); char ts[32]; strftime(ts,sizeof ts,"%Y-%m-%dT%H:%M:%SZ",&tmv);
        fprintf(f,"%s up=%lld.%03d merakiplus[%d] NET %s\n",ts,(long long)(net_now/1000),(int)(net_now%1000),(int)getpid(),m); fclose(f);
    }
    if(net_echo){ printf("up=%lld.%03d NET %s\n",(long long)(net_now/1000),(int)(net_now%1000),m); fflush(stdout); }
}

/* ---- HAL: the device (real) / model (SIM + preview/selftest) ---- */
#ifdef DEVICE_RT
static int net_sock=-1;
static int net_slurp(const char*path,char*b,int n){
    int fd=open(path,O_RDONLY|O_CLOEXEC); if(fd<0) return -1;
    int k=(int)read(fd,b,(size_t)n-1); close(fd); if(k<0) k=0; b[k]=0; return k;
}
static int net_pid_is(int pid,const char*name){        /* argv[0]'s basename; zombies/kernel threads have an empty cmdline */
    char p[64],b[200]; snprintf(p,sizeof p,"/proc/%d/cmdline",pid);
    if(net_slurp(p,b,sizeof b)<=0) return 0;
    const char*a=strrchr(b,'/'); return !strcmp(a?a+1:b,name);
}
static void net_pid_scan(int*wpa,int*dhcp){            /* one pass over /proc; only the slots that are 0 are searched */
    DIR*d=opendir("/proc"); if(!d) return;
    struct dirent*de;
    while((de=readdir(d))&&((wpa&&!*wpa)||(dhcp&&!*dhcp))){
        if(de->d_name[0]<'1'||de->d_name[0]>'9') continue;
        char p[64],b[200]; snprintf(p,sizeof p,"/proc/%.20s/cmdline",de->d_name);
        if(net_slurp(p,b,sizeof b)<=0) continue;
        const char*a=strrchr(b,'/'); a=a?a+1:b;
        if(wpa&&!*wpa&&!strcmp(a,"wpa_supplicant")) *wpa=atoi(de->d_name);
        else if(dhcp&&!*dhcp&&!strcmp(a,"udhcpc")) *dhcp=atoi(de->d_name);
    }
    closedir(d);
}
static int net_ifreq(unsigned long req,struct ifreq*r){
    if(net_sock<0) net_sock=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);
    if(net_sock<0) return -1;
    return ioctl(net_sock,req,r);
}
static int net_has_gw(void){                           /* /proc/net/route: "wlan0  00000000  <gw>  <flags w. RTF_UP|RTF_GATEWAY>" */
    char b[2048]; if(net_slurp("/proc/net/route",b,sizeof b)<=0) return 0;
    for(char*l=b;l&&*l;){
        char ifn[20]; unsigned dst=1,gw=0,fl=0; char*nl=strchr(l,'\n');
        if(sscanf(l,"%19s %x %x %x",ifn,&dst,&gw,&fl)==4&&!strcmp(ifn,NET_IF)&&dst==0&&(fl&0x3)==0x3) return 1;
        l=nl?nl+1:NULL;
    }
    return 0;
}
static void nh_probe(NetProbe*p,ms_t now,int want_dhcp){
    (void)now; char b[16]; struct ifreq r;
    p->iface=access("/sys/class/net/" NET_IF,F_OK)==0;
    p->carrier=p->iface&&net_slurp("/sys/class/net/" NET_IF "/carrier",b,sizeof b)>0&&b[0]=='1';
    p->ip=0; p->ifup=0;
    memset(&r,0,sizeof r); snprintf(r.ifr_name,sizeof r.ifr_name,"%s",NET_IF);
    if(p->iface&&net_ifreq(SIOCGIFFLAGS,&r)==0) p->ifup=(r.ifr_flags&IFF_UP)!=0;
    memset(&r,0,sizeof r); snprintf(r.ifr_name,sizeof r.ifr_name,"%s",NET_IF);
    if(p->iface&&net_ifreq(SIOCGIFADDR,&r)==0) p->ip=ntohl(((struct sockaddr_in*)(void*)&r.ifr_addr)->sin_addr.s_addr);
    p->gw=p->ip?net_has_gw():0;
    want_dhcp=want_dhcp&&!(p->ip&&(p->gw||!net.gw_seen));    /* udhcpc is only searched for while the IP configuration is missing */
    if(p->wpa_pid>0&&!net_pid_is(p->wpa_pid,"wpa_supplicant")) p->wpa_pid=0;
    if(!want_dhcp) p->dhcp_pid=0; else if(p->dhcp_pid>0&&!net_pid_is(p->dhcp_pid,"udhcpc")) p->dhcp_pid=0;
    if(!p->wpa_pid||(want_dhcp&&!p->dhcp_pid)) net_pid_scan(&p->wpa_pid,want_dhcp?&p->dhcp_pid:NULL);
}
/* called in the CHILD after fork: the same environment addition as MI_WLAN_Init (wlan.json "env": "%s:%s" — appended
 * without a duplicate check; stock's wpa_supplicant therefore has ".../config/wifi:/config/wifi", measured in /proc/841/environ) */
static void net_child_env(void){
    const char*l=getenv("LD_LIBRARY_PATH"),*pa=getenv("PATH"); char b[1200];
    if(!l||!*l) setenv("LD_LIBRARY_PATH","/lib:" NET_WIFI_DIR,1);
    else { snprintf(b,sizeof b,"%s:%s",l,NET_WIFI_DIR); setenv("LD_LIBRARY_PATH",b,1); }
    if(!pa||!*pa) setenv("PATH","/bin:/sbin:/usr/bin:/usr/sbin:" NET_WIFI_DIR,1);
    else { snprintf(b,sizeof b,"%s:%s",pa,NET_WIFI_DIR); setenv("PATH",b,1); }
}
/* detach=1: double fork — the process is inherited by init (PPid 1, own session), like stock's "… &" from system().
 * The child inherits NONE of merakiplus's fds (fb, UART, touch grab) — stock's wpa_supplicant inherits all of them.
 * Returns: detach -> 1 on success; otherwise the child's pid; -1 on error. */
static int net_spawn(int detach,const char*out,const char*path,char*const argv[]){
    fflush(stdout); fflush(stderr);
    pid_t c=fork(); if(c<0) return -1;
    if(c==0){
        setsid();
        if(detach){ pid_t g=fork(); if(g!=0) _exit(g<0?1:0); }
        if(chdir("/")!=0){}
        net_child_env();
        int nul=open("/dev/null",O_RDWR), o=out?open(out,O_WRONLY|O_CREAT|O_APPEND,0644):-1;
        if(nul>=0){ dup2(nul,0); dup2(o>=0?o:nul,1); dup2(o>=0?o:nul,2); }
        for(int fd=3;fd<1024;fd++) close(fd);
        signal(SIGINT,SIG_DFL); signal(SIGTERM,SIG_DFL);
        execv(path,argv);
        _exit(127);
    }
    if(detach){ int st=0; while(waitpid(c,&st,0)<0&&errno==EINTR){} return (WIFEXITED(st)&&WEXITSTATUS(st)==0)?1:-1; }
    return (int)c;
}
static int nh_init_start(void){
    char*const a[]={(char*)"sh",(char*)"-c",(char*)"source " NET_INIT_SH " dummy1 dummy2 ",NULL};
    return net_spawn(0,NET_LOG,"/bin/sh",a);
}
static int nh_job_done(int pid,int*rc){
    int st=0; pid_t r=waitpid(pid,&st,WNOHANG); if(r==0) return 0;
    *rc=(r==pid&&WIFEXITED(st))?WEXITSTATUS(st):-1; return 1;
}
static void nh_prepare(void){          /* the part of the init script that wpa_supplicant needs when wlan0 already exists */
    mkdir("/tmp/wifi",0755); mkdir(NET_RUN_DIR,0777); chmod(NET_RUN_DIR,0777);
    unlink(NET_CTRL "/" NET_IF);      /* only called when no wpa_supplicant is alive: a leftover socket is dead */
}
static void nh_iface_up(void){         /* = "ifconfig wlan0 up" */
    struct ifreq r; memset(&r,0,sizeof r); snprintf(r.ifr_name,sizeof r.ifr_name,"%s",NET_IF);
    if(net_ifreq(SIOCGIFFLAGS,&r)==0&&!(r.ifr_flags&IFF_UP)){ r.ifr_flags|=IFF_UP; net_ifreq(SIOCSIFFLAGS,&r); }
}
static int nh_wpa_start(void){
    char*const a[]={(char*)"wpa_supplicant",(char*)"-i",(char*)NET_IF,(char*)"-Dnl80211",(char*)"-c",(char*)NET_CONF,NULL};
    return net_spawn(1,NULL,NET_WPA_BIN,a)>0;
}
static int nh_dhcp_start(void){
    char*const a[]={(char*)"udhcpc",(char*)"-i",(char*)NET_IF,(char*)"-s",(char*)NET_DHCP_SH,(char*)"-a",(char*)"-q",(char*)"-b",(char*)"-t",(char*)"20",(char*)"-T",(char*)"1",NULL};
    return net_spawn(1,NULL,NET_DHCP_BIN,a)>0;
}
#else
/* MODEL of the wifi stack (SIM build + preview/selftest): the same state machine, no processes, no network.
 * the init script provides wlan0 after NM_INIT_MS; a started supplicant gets carrier after NM_ASSOC_MS (if the AP is
 * there); udhcpc resets the IP at start (deconfig), gets a lease NM_LEASE_MS after carrier and exits (-q). */
#define NM_INIT_MS  600
#define NM_ASSOC_MS 1500
#define NM_LEASE_MS 1200
/* ifdown (ifconfig wlan0 down) removes the kernel's routes via wlan0, but not the IP — as on the device. init_hang: the script never finishes. */
static struct { int iface, ifup, wpa_pid, dhcp_pid, carrier, gw, ap, gw_offer, job, pid_next, init_fail, init_hang, wpa_fail, n_init, n_wpa, n_dhcp, n_prep, n_ifup; uint32_t ip, lease; ms_t now, t_init, assoc_at, lease_at; } nm;
static void nm_reset(int cold){
    memset(&nm,0,sizeof nm); nm.ap=1; nm.gw_offer=1; nm.pid_next=900; nm.lease=(192u<<24)|(168u<<16)|(1u<<8)|100u;
    if(!cold){ nm.iface=1; nm.ifup=1; nm.wpa_pid=841; nm.carrier=1; nm.ip=nm.lease; nm.gw=1; }
}
static void nm_step(ms_t now){
    nm.now=now;
    if(nm.wpa_pid&&nm.ap&&nm.iface&&nm.ifup){ if(!nm.carrier&&now>=nm.assoc_at) nm.carrier=1; } else nm.carrier=0;
    if(nm.dhcp_pid){
        if(!nm.carrier) nm.lease_at=0;
        else { if(!nm.lease_at) nm.lease_at=now+NM_LEASE_MS; if(now>=nm.lease_at){ nm.ip=nm.lease; nm.gw=nm.gw_offer; nm.dhcp_pid=0; nm.lease_at=0; } }
    }
}
static void nh_probe(NetProbe*p,ms_t now,int want_dhcp){ nm_step(now); p->iface=nm.iface; p->ifup=nm.iface&&nm.ifup; p->wpa_pid=nm.wpa_pid; p->dhcp_pid=want_dhcp?nm.dhcp_pid:0; p->carrier=nm.carrier; p->ip=nm.iface?nm.ip:0; p->gw=p->ip&&nm.gw; }
static int nh_init_start(void){ nm.n_init++; nm.job=nm.pid_next++; nm.t_init=nm.now; return nm.job; }
static int nh_job_done(int pid,int*rc){ (void)pid; if(nm.init_hang||nm.now-nm.t_init<NM_INIT_MS) return 0; nm.job=0; nm.iface=!nm.init_fail; nm.ifup=0; *rc=nm.init_fail?255:0; return 1; }
static void nh_prepare(void){ nm.n_prep++; }
static void nh_iface_up(void){ if(nm.iface&&!nm.ifup){ nm.ifup=1; nm.n_ifup++; nm.assoc_at=nm.now+NM_ASSOC_MS; } }
static int nh_wpa_start(void){ nm.n_wpa++; if(nm.wpa_fail||!nm.iface) return 1; nm.wpa_pid=nm.pid_next++; nm.ifup=1; nm.carrier=0; nm.assoc_at=nm.now+NM_ASSOC_MS; return 1; }
static int nh_dhcp_start(void){ nm.n_dhcp++; nm.dhcp_pid=nm.pid_next++; nm.ip=0; nm.gw=0; nm.lease_at=0; return 1; }
#endif

/* ---- the state machine (identical in all build types) ---- */
static void net_fail(ms_t now,const char*why){
    net.step=NB_NONE; net.job=0; net.n_fail++; net.state=NS_DOWN;
    net.backoff=net.backoff?(net.backoff*2>NET_BACKOFF_MAX_MS?NET_BACKOFF_MAX_MS:net.backoff*2):NET_BACKOFF0_MS;
    net.t_retry=now+net.backoff;
    net_log("bring-up FAILED: %s - retrying in %d s (failures in total: %d)",why,net.backoff/1000,net.n_fail);
}
static void net_off(ms_t now){
    if(net.state!=NS_OFF||net.cfg_logged!=net.cfg){ net.cfg_logged=net.cfg; net_log("OFF: %s - the network is not touched",NC_TXT[net.cfg]); }
    net.state=NS_OFF; net.step=NB_NONE; net.t_cfg=now+NET_CFG_RECHECK_MS;
}
/* the IP configuration is complete: an IP — and the default route, if there has ever been one (a network without a gateway is healthy too) */
static int net_ipok(const NetProbe*p){ return p->ip&&(p->gw||!net.gw_seen); }
static void net_dhcp(ms_t now,const char*why){
    if(nh_dhcp_start()){ net.n_dhcp++; net_log("%s: started udhcpc -i " NET_IF " -s " NET_DHCP_SH " -a -q -b -t 20 -T 1",why); }
    else net_log("%s: udhcpc could NOT be started",why);
    net.t_noip=now;
}
static void net_wpa_begin(ms_t now){
    char sv_[40]; int k=0; for(;k<net.ssid_n&&k<32;k++){ unsigned char c=(unsigned char)net.ssid[k]; sv_[k]=(c<32||c>=127)?'?':(char)c; } sv_[k]=0;
    nh_prepare();
    int c=net_conf_sync();
    if(c<0){ net_fail(now,"cannot write " NET_CONF); return; }
    if(c>0){ net.n_conf++; net_log(NET_CONF " written from config.json (ssid \"%s\", %s, md5 %.8s)",sv_,net.psk[0]?"WPA-PSK":"open network",net.conf_md5); }
    else net_log(NET_CONF " already has the correct content (ssid \"%s\", md5 %.8s) - not written",sv_,net.conf_md5);
    nh_iface_up();
    if(!nh_wpa_start()){ net_fail(now,"wpa_supplicant could not be started"); return; }
    net.n_wpa++; net.step=NB_WPA; net.t_step=now;
    net_log("started: wpa_supplicant -i " NET_IF " -Dnl80211 -c " NET_CONF " (waiting %d ms like stock)",NET_WPA_SETTLE_MS);
}
static void net_bringup(ms_t now){
    net_cfg_load();
    if(net.cfg!=NC_OK){ net_off(now); return; }
    net.acted=1; net.state=NS_STARTING;
    if(!net.p.iface){
        if(net.orphan){ net_fail(now,"an earlier init script is still running - not starting another"); return; }
        int j=nh_init_start(); if(j<=0){ net_fail(now,"the init script could not be started"); return; }
        net.job=j; net.init_hung=0; net.n_init++; net.step=NB_INIT; net.t_step=now;
        net_log(NET_IF " missing: running \"source " NET_INIT_SH " dummy1 dummy2\" (like MI_WLAN_Init)");
        return;
    }
    net_wpa_begin(now);
}
static void net_eval(ms_t now){
    NetProbe*p=&net.p; char ipb[20];
    int wpa=p->wpa_pid>0, first=!net.booted; net.booted=1;
    if(p->ip&&p->gw) net.gw_seen=1;
    if(net.step==NB_INIT){
        int rc=0;
        if(!nh_job_done(net.job,&rc)){
            if(now-net.t_step<NET_INIT_TMO_MS) return;
            if(!p->iface){                                    /* no kill calls: the script is allowed to finish, and no second one is started */
                if(!net.init_hung){ net.init_hung=1; net_log("the init script (pid %d) has been running for %d s without " NET_IF " - still waiting (it is not killed)",net.job,NET_INIT_TMO_MS/1000); }
                return;
            }
            net_log("the init script (pid %d) is still running after %d s, but " NET_IF " exists: continuing (the script is reaped when it ends)",net.job,(int)((now-net.t_step)/1000));
            net.orphan=net.job; net.job=0; net_wpa_begin(now); return;
        }
        net.job=0; nh_probe(p,now,0);
        if(!p->iface){ char w[72]; snprintf(w,sizeof w,"init script rc=%d, " NET_IF " still does not exist",rc); net_fail(now,w); return; }
        net_log("init script finished (rc=%d): " NET_IF " exists",rc);
        net_wpa_begin(now); return;
    }
    if(net.step==NB_WPA){
        if(now-net.t_step<NET_WPA_SETTLE_MS) return;
        if(!wpa){ net_fail(now,"wpa_supplicant not running after start"); return; }
        net.step=NB_NONE; net.state=NS_CONNECTING; net.t_noip=now; net.carrier_was=p->carrier; net.dhcp_wait=0;
        if(p->dhcp_pid) net_log("wpa_supplicant pid %d running; udhcpc pid %d already running",p->wpa_pid,p->dhcp_pid);
        else if(net_ipok(p)) net_log("wpa_supplicant pid %d running; " NET_IF " still has ip %s%s - kept (no udhcpc)",p->wpa_pid,net_ipstr(p->ip,ipb,sizeof ipb),p->gw?" and default route":"");
        else { char w[64]; snprintf(w,sizeof w,"wpa_supplicant pid %d running",p->wpa_pid); net_dhcp(now,w); }
        return;
    }
    if(wpa&&p->ifup&&p->carrier&&net_ipok(p)){              /* healthy: not touched */
        if(net.state!=NS_UP){
            int ms=(int)(now-net.t_down); net_ipstr(p->ip,ipb,sizeof ipb);
            if(first) net_log("UP at start: wpa_supplicant pid %d, ip %s%s - the stack is healthy and is not touched",p->wpa_pid,ipb,p->gw?", default route":"");
            else if(net.acted&&net.was_up){ net.n_heal++; net.heal_ms=ms; net_log("SELF-HEAL done in %d ms: wpa_supplicant pid %d, ip %s (heal no. %d)",ms,p->wpa_pid,ipb,net.n_heal); }
            else if(net.acted){ net.n_boot++; net.heal_ms=ms; net_log("UP %d ms after startup: wpa_supplicant pid %d, ip %s",ms,p->wpa_pid,ipb); }
            else net_log("UP again after %d ms without intervention: wpa_supplicant pid %d, ip %s",ms,p->wpa_pid,ipb);
        }
        net.state=NS_UP; net.was_up=1; net.acted=0; net.dead=0; net.backoff=0; net.t_retry=0; net.dhcp_wait=0; net.ifdown=0; net.carrier_was=1; return;
    }
    if(net.state==NS_UP||first) net.t_down=now;               /* the outage starts here */
    if(wpa){                                                  /* the supplicant is alive: it is NEVER killed */
        net.dead=0;
        if(net.state!=NS_CONNECTING){
            net.state=NS_CONNECTING; net.t_noip=now; net.carrier_was=p->carrier; net.dhcp_wait=0;
            net_log("wpa_supplicant pid %d alive, but %s - waiting (the supplicant is not touched)",p->wpa_pid,
                    !p->iface?NET_IF " missing":!p->ifup?NET_IF " is down":!p->carrier?"no carrier":!p->ip?"no IP":"no default route");
        }
        if(p->iface&&!p->ifup){                               /* "ifconfig wlan0 down": the supplicant just waits -> up again (MI_WLAN_Open) */
            if(!net.ifdown){ net.ifdown=1; net.t_ifdown=now; }
            if(now-net.t_ifdown<NET_IFDOWN_MS) return;
            net.acted=1; net.n_ifup++; net.t_ifdown=now; nh_iface_up();
            net_log(NET_IF " has been down for %d s with a live supplicant: ifconfig " NET_IF " up (like MI_WLAN_Open)",NET_IFDOWN_MS/1000);
            return;
        }
        net.ifdown=0;
        if(p->carrier&&!net.carrier_was) net.t_noip=now;      /* the time until udhcpc is measured from carrier */
        net.carrier_was=p->carrier;
        if(net_ipok(p)||p->dhcp_pid){ net.t_noip=now; return; }
        if(p->ip&&!p->carrier) return;                        /* IP but no route/carrier: a udhcpc now would only take the IP away */
        int wait=net.dhcp_wait?net.dhcp_wait:(p->ip?NET_NOGW_MS:NET_NOIP_MS);
        if(now-net.t_noip<wait) return;
        net_cfg_load(); if(net.cfg!=NC_OK){ net.t_noip=now; return; }
        net.acted=1; net.dhcp_wait=wait*2>NET_DHCP_MAX_MS?NET_DHCP_MAX_MS:wait*2;
        char w[80]; snprintf(w,sizeof w,p->ip?"no default route for %d s and no udhcpc":"no IP for %d s and no udhcpc",wait/1000);
        net_dhcp(now,w);
        return;
    }
    if(!net.dead){                                            /* the supplicant is gone */
        net.dead=1; net.t_dead=now; if(net.state!=NS_OFF) net.state=NS_DOWN;
        net_log("wpa_supplicant not running (" NET_IF " %s, carrier %d, ip %s)",p->iface?(p->ifup?"exists":"exists, down"):"missing",p->carrier,net_ipstr(p->ip,ipb,sizeof ipb));
    }
    if(net.state==NS_OFF&&now<net.t_cfg) return;
    if(!first&&now-net.t_dead<NET_DEAD_MS) return;
    if(now<net.t_retry) return;
    net_bringup(now);
}
static void net_step(ms_t now){
    if(!net.on||now<net.t_probe) return;
    net_now=now;
    if(net.orphan){ int rc=0; if(nh_job_done(net.orphan,&rc)){ net_log("the orphaned init script (pid %d) ended (rc=%d)",net.orphan,rc); net.orphan=0; } }
    nh_probe(&net.p,now,1);
    net_eval(now);
    int idle=net.step==NB_NONE&&(net.state==NS_OFF||(net.state==NS_DOWN&&now<net.t_retry));
    net.t_probe=now+(net.state==NS_UP?NET_PROBE_UP_MS:(idle?NET_PROBE_IDLE_MS:NET_PROBE_MS));
}
static void net_start(ms_t now,int enable){
    memset(&net,0,sizeof net); net_now=now; net.cfg_logged=-1; net.on=enable;
    if(!enable) return;
    net_cfg_load();
    net_log("net-vagt startet (" MERAKIPLUS_VERSION "): %s; ssid length %d, password length %d",NC_TXT[net.cfg],net.ssid_n,(int)strlen(net.psk));   /* "net-vagt startet" stays: tools/merakiplus.py greps the net log for it */
}


/* ====================================================================== */
/* ================= PRODUCT NAME + I18N (v6.3) ========================= */
/* ====================================================================== */
/* The product name lives in ONE place. UI strings use PRODUCT; the word mark is drawn as BRAND_WORD +
 * "+" badge (brand_at()). "merakiplus" is only the technical identifier: paths, binaries, logs. */
#define BRAND_WORD  "MERAKI"
#define BRAND_BADGE "+"
#define PRODUCT     BRAND_WORD BRAND_BADGE

/* control characters in UI strings (interpreted by glyph_next): code spans w. lowercase letters + icons */
#define LC "\x0e"
#define UC "\x0f"
#define IC_BEAN "\x01"
#define IC_CUP "\x02"
#define IC_DROP "\x03"
#define IC_STEAM "\x04"
#define IC_CHECK "\x05"
#define IC_CROSS "\x06"
#define IC_MOON "\x07"
#define IC_WARN "\x08"
#define IC_BOLT "\x09"
#define IC_CLOCK "\x0b"   /* v6.16: clock face (pre-heat); 0x0A is the line break */

/* boot paths (used in the strings below; the logic is in the BOOT-SLOT section) */
#define BOOT_FLAG  "/customer/new"
#define SLOT_A_EXE "/customer/demo_v9"
#define SLOT_B_EXE "/new_customer/demo_v9"
#define SLOT_B_MNT "/new_customer"
#define SW_LOG     "/tmp/switcher.log"            /* written by the switcher daemon (boot/switcher.c) */

/* THE STRING TABLE — the only place a visible text may live. X(id, class, EN, DA).
 *   class = where the text is shown; the selftest measures EVERY string in BOTH languages against the class's
 *   pixel budget (K0 = measured in context by the layout tests, KCON = console).
 *   Rules (enforced by --selftest): both languages filled in; the same printf conversions;
 *   the same numbers/punctuation for numbers and units; only characters the stroke font has; no
 *   generation name; the identifier "merakiplus" only inside code spans (LC … UC) or on the console. */
enum { LANG_EN=0, LANG_DA, NLANG };
enum { K0=0, KT, KSUB, KLAB, KCHIP, KBTN, KBTNS, KTITLE, KPSUB, KSPL, KDL, KDV, KDF, KNOTE, KF1, KF2, KXSUB, KST, KCON, NKLASS };
#define STR_TABLE(X) \
 X(TX_NONE, K0, "", "") \
 X(TX_U_G, K0, "G", "G") \
 X(TX_U_S, K0, "S", "S") \
 X(TX_U_BAR, K0, "BAR", "BAR") \
 X(TX_U_C, K0, "°C", "°C") \
 X(TX_U_F, K0, "°F", "°F") \
 X(TX_TAGLINE, KSPL, "ADVANCED BREWING INTERFACE", "AVANCERET BRYGGE-INTERFACE") \
 X(TX_SPL_CFG_OK, K0, "CONFIG LOADED", "CONFIG INDLÆST") \
 X(TX_SPL_CFG_MISSING, K0, "CONFIG MISSING", "CONFIG MANGLER") \
 X(TX_PG_SYSTEM, KTITLE, "SYSTEM", "SYSTEM") \
 X(TX_PG_DIAG, KTITLE, "DIAGNOSTICS", "DIAGNOSE") \
 X(TX_SEC_DISPLAY, K0, "DISPLAY · SOUND", "SKÆRM · LYD") \
 X(TX_SEC_CLOCK, K0, "CLOCK", "UR") \
 X(TX_SEC_SCHED, K0, "SCHEDULE", "TIDSPLAN") \
 X(TX_SEC_STOCK, K0, "STOCK UI", "STOCK-UI") \
 X(TX_LV_GRIND, K0, "ON THE SCALE %s G", "PÅ VÆGTEN %s G") \
 X(TX_LV_BREW, K0, "BOILER %s° · GROUP %s°", "KEDEL %s° · GRUPPE %s°") \
 X(TX_LV_WATER, K0, "WATER %s°", "VAND %s°") \
 X(TX_LV_STEAM, K0, "BOILER %s° · MILK %s°", "KEDEL %s° · MÆLK %s°") \
 X(TX_SL_GRINDW, K0, "GRIND DOSE", "KVÆRN-DOSIS") \
 X(TX_SN_GRINDW, KNOTE, "STOPS AUTOMATICALLY AT THIS WEIGHT", "STOPPER AUTOMATISK VED DENNE VÆGT") \
 X(TX_SL_GRINDMODE, K0, "GRIND MODE", "KVÆRN-TILSTAND") \
 X(TX_SN_GRINDMODE, KNOTE, PRODUCT " STARTS WEIGHT MODE ONLY", PRODUCT " STARTER KUN VÆGT-TILSTAND") \
 X(TX_SL_BREWW, K0, "BREW WEIGHT", "BRYG-VÆGT") \
 X(TX_SN_BREWW, KNOTE, "STOPS AUTOMATICALLY AT THIS WEIGHT", "STOPPER AUTOMATISK VED DENNE VÆGT") \
 X(TX_SL_BREWT, K0, "BREW TEMP", "BRYG-TEMP") \
 X(TX_SN_BREWT, KNOTE, "TARGET FOR THE BREW BOILER", "MÅL FOR BRYGKEDLEN") \
 X(TX_SL_PISW, K0, "PRE-INFUSION", "PRE-INFUSION") \
 X(TX_SN_PISW, KNOTE, "SOAKS THE PUCK BEFORE FULL PRESSURE", "VÆDER KAFFEN FØR FULDT TRYK") \
 X(TX_SL_PITIME, K0, "PRE-INF. TIME", "PRE-INF. TID") \
 X(TX_SN_PITIME, KNOTE, "SOAK TIME BEFORE FULL PRESSURE", "VÆDE-TID FØR FULDT TRYK") \
 X(TX_SL_PIPRESS, K0, "PRE-INF. PRESSURE", "PRE-INF. TRYK") \
 X(TX_SN_PIPRESS, KNOTE, "THE MACHINE ONLY KNOWS 3 AND 4", "MASKINEN KENDER KUN 3 OG 4") \
 X(TX_SL_BREAK, K0, "PAUSE TIME", "PAUSE-TID") \
 X(TX_SN_BREAK, KNOTE, "NO EFFECT · PRE-INF. TIME IS USED", "INGEN EFFEKT · PRE-INF. TID BRUGES") \
 X(TX_SL_HEADT, K0, "GROUP HEAD", "GRUPPEHOVED") \
 X(TX_SN_HEADT, KNOTE, "TARGET FOR THE GROUP HEAD", "MÅL FOR GRUPPEHOVEDET") \
 X(TX_SL_STEAMT, K0, "MILK TEMP", "MÆLKETEMP") \
 X(TX_SN_STEAMT, KNOTE, "STEAM STOPS AT THIS MILK TEMP", "DAMPEN STOPPER VED DENNE MÆLKETEMP") \
 X(TX_SL_STEAMG, K0, "STEAM LEVEL", "DAMP-NIVEAU") \
 X(TX_SN_STEAMG, KNOTE, "STEAM POWER 1-4", "DAMP-STYRKE 1-4") \
 X(TX_SL_HOTW, K0, "HOT WATER", "VARMT VAND") \
 X(TX_SN_HOTW, KNOTE, "STOPS AUTOMATICALLY AT THIS WEIGHT", "STOPPER AUTOMATISK VED DENNE VÆGT") \
 X(TX_SL_HOTMODE, K0, "WATER MODE", "VAND-TILSTAND") \
 X(TX_SN_HOTMODE, KNOTE, "AUTO STOPS AT THE WEIGHT · MANUAL AT STOP", "AUTO STOPPER VED VÆGTEN · MANUEL VED STOP") \
 X(TX_SL_HOTT, K0, "WATER SENSOR", "VAND-SENSOR") \
 X(TX_SN_HOTT, KNOTE, "MEASURED ONLY · NO TARGET EXISTS", "KUN MÅLT · INTET MÅL FINDES") \
 X(TX_SL_UILANG, K0, "UI LANGUAGE", "UI-SPROG") \
 X(TX_SN_UILANG, KNOTE, "LIVE PREVIEW · SAVE TO KEEP", "LIVE PREVIEW · GEM FOR AT BEHOLDE") \
 X(TX_SL_BL, K0, "BRIGHTNESS", "LYSSTYRKE") \
 X(TX_SN_BL, KNOTE, "LIVE PREVIEW", "LIVE PREVIEW") \
 X(TX_SL_VOL, K0, "VOLUME", "LYDSTYRKE") \
 X(TX_SN_VOL, KNOTE, "MERAKI+ + STOCK UI SOUNDS", "MERAKI+ + STOCK-UI LYDE") \
 X(TX_SH_VOL, K0, "STEPS = ESTIMATE", "TRIN = SKØN") \
 X(TX_SL_TUNIT, K0, "TEMP UNIT", "TEMP-ENHED") \
 X(TX_SN_TUNIT, KNOTE, "ALSO APPLIES IN " PRODUCT, "GÆLDER OGSÅ I " PRODUCT) \
 X(TX_SL_LANG, K0, "STOCK LANG.", "STOCK-SPROG") \
 X(TX_SN_LANG, KNOTE, "STOCK UI ONLY", "KUN STOCK-UI") \
 X(TX_SL_HOTSC, K0, "WATER SHORTCUT", "VAND-GENVEJ") \
 X(TX_SN_HOTSC, KNOTE, "STOCK UI HOME SHORTCUT", "STOCK-UI HJEM-GENVEJ") \
 X(TX_SL_DST, K0, "DAYLIGHT SAVING", "SOMMERTID") \
 X(TX_SL_CLEARTIP, K0, "CLEANING REMINDER", "RENSE-PÅMINDELSE") \
 X(TX_SN_CLEARTIP, KNOTE, "STOCK UI TIP", "STOCK-UI TIP") \
 X(TX_SL_TIMEMODE, K0, "TIME FORMAT", "TIDSFORMAT") \
 X(TX_SL_AUTOTIME, K0, "AUTO TIME", "AUTO-TID") \
 X(TX_SN_AUTOTIME, KNOTE, "STOCK UI NETWORK TIME", "STOCK-UI NETVÆRKSTID") \
 X(TX_SL_TZ, K0, "TIME ZONE", "TIDSZONE") \
 X(TX_SN_TZ, KNOTE, "SET IT IN THE STOCK UI", "SÆTTES I STOCK-UI'ET") \
 X(TX_SH_EST, K0, "MAX = ESTIMATE", "MAX = SKØN") \
 X(TX_E_OFF, K0, "OFF", "FRA") \
 X(TX_E_ON, K0, "ON", "TIL") \
 X(TX_E_WEIGHT, K0, "WEIGHT", "VÆGT") \
 X(TX_E_TIME, K0, "TIME", "TID") \
 X(TX_E_AUTO, K0, "AUTO", "AUTO") \
 X(TX_E_MANUAL, K0, "MANUAL", "MANUEL") \
 X(TX_E_GENTLE, K0, "GENTLE", "BLID") \
 X(TX_E_MILD, K0, "MILD", "MILD") \
 X(TX_E_MEDIUM, K0, "MEDIUM", "MEDIUM") \
 X(TX_E_STRONG, K0, "STRONG", "STÆRK") \
 X(TX_E_24H, K0, "24-HOUR", "24-TIMERS") \
 X(TX_E_12H, K0, "12-HOUR", "12-TIMERS") \
 X(TX_LG_EN, K0, "ENGLISH", "ENGELSK") \
 X(TX_LG_DE, K0, "GERMAN", "TYSK") \
 X(TX_LG_FR, K0, "FRENCH", "FRANSK") \
 X(TX_LG_IT, K0, "ITALIAN", "ITALIENSK") \
 X(TX_LG_ES, K0, "SPANISH", "SPANSK") \
 X(TX_LG_PL, K0, "POLISH", "POLSK") \
 X(TX_LG_AR, K0, "ARABIC", "ARABISK") \
 X(TX_LG_ZHS, K0, "CHINESE SIMPL.", "KINESISK FORENKL.") \
 X(TX_LG_ZHT, K0, "CHINESE TRAD.", "KINESISK TRAD.") \
 X(TX_LG_KO, K0, "KOREAN", "KOREANSK") \
 X(TX_LG_JA, K0, "JAPANESE", "JAPANSK") \
 X(TX_UL_EN, K0, "ENGLISH", "ENGLISH") \
 X(TX_UL_DA, K0, "DANSK", "DANSK") \
 X(TX_PH_OFFLINE, KCHIP, "OFFLINE", "OFFLINE") \
 X(TX_PH_SLEEP, KCHIP, "ASLEEP", "SOVER") \
 X(TX_PH_ERROR, KCHIP, "ERROR", "FEJL") \
 X(TX_PH_HEAT, KCHIP, "HEATING UP", "VARMER OP") \
 X(TX_PH_READY, KCHIP, "READY", "KLAR") \
 X(TX_PH_GRIND, KCHIP, "GRINDING", "KVÆRNER") \
 X(TX_PH_PRE, KCHIP, "PRE-INFUSION", "PRE-INFUSION") \
 X(TX_PH_EXT, KCHIP, "EXTRACTION", "EKSTRAKTION") \
 X(TX_PH_WATER, KCHIP, "HOT WATER", "VARMT VAND") \
 X(TX_PH_STEAM, KCHIP, "STEAM", "DAMP") \
 X(TX_PH_DONE, KCHIP, "DONE", "FÆRDIG") \
 X(TX_PH_STEAM_AUTO, KCHIP, "STEAM·AUTO", "DAMP·AUTO") \
 X(TX_PH_STEAM_WAND, KCHIP, "STEAM·WAND", "DAMP·WAND") \
 X(TX_ER_E3_BREW, KSUB, "BREW BOILER TOO HOT", "BRYGKEDLEN ER FOR VARM") \
 X(TX_ER_E3_STEAM, KSUB, "STEAM BOILER TOO HOT", "DAMPKEDLEN ER FOR VARM") \
 X(TX_ER_E1_GROUP, KSUB, "GROUP HEAD TOO HOT", "GRUPPEHOVEDET ER FOR VARMT") \
 X(TX_ER_E1_MAINS, KSUB, "PROBLEM WITH MAINS VOLTAGE", "PROBLEM MED NETSPÆNDINGEN") \
 X(TX_ER_E2_BREW, KSUB, "BREW BOILER SENSOR FAULT", "FEJL PÅ BRYGKEDEL-SENSOR") \
 X(TX_ER_E2_GROUP, KSUB, "GROUP HEAD SENSOR FAULT", "FEJL PÅ GRUPPEHOVED-SENSOR") \
 X(TX_ER_E2_STEAM, KSUB, "STEAM BOILER SENSOR FAULT", "FEJL PÅ DAMPKEDEL-SENSOR") \
 X(TX_ER_E2_MILK, KSUB, "MILK SENSOR FAULT", "FEJL PÅ MÆLKESENSOR") \
 X(TX_ER_UNKNOWN, KSUB, "UNKNOWN FAULT", "UKENDT FEJL") \
 X(TX_A_GRIND, K0, "GRIND", "KVÆRN") \
 X(TX_A_BREW, K0, "BREW", "BRYG") \
 X(TX_A_WATER, K0, "WATER", "VAND") \
 X(TX_A_STEAM, K0, "STEAM", "DAMP") \
 X(TX_G_RO, KT, "READ-ONLY · NO WRITES", "READ-ONLY · INGEN WRITES") \
 X(TX_G_NOLINK, KT, "NO CONNECTION", "INGEN FORBINDELSE") \
 X(TX_G_ERROR, KT, "ERROR ACTIVE · START BLOCKED", "FEJL AKTIV · START SPÆRRET") \
 X(TX_G_SLEEP, KT, "WAKING UP · ONE MOMENT", "VÅGNER · ET ØJEBLIK") \
 X(TX_G_BUSY, KT, "ACTIVITY IN PROGRESS", "AKTIVITET I GANG") \
 X(TX_G_WAIT, KT, "ONE MOMENT…", "ET ØJEBLIK…") \
 X(TX_G_NOCFG, KT, "CONFIG MISSING · START BLOCKED", "CONFIG MANGLER · START SPÆRRET") \
 X(TX_G_OOR, KT, "CONFIG OUT OF SPEC · FIX FIRST", "CONFIG UDEN FOR SPEC · RET FØRST") \
 X(TX_G_GRINDTIME, KT, "GRIND = TIME · USE STOCK UI", "KVÆRN = TID · BRUG STOCK-UI") \
 X(TX_T_POLICY, KT, "BLOCKED BY A SAFETY RULE", "SPÆRRET AF EN SIKKERHEDSREGEL") \
 X(TX_T_QFULL, KT, "BUSY · TRY AGAIN", "OPTAGET · PRØV IGEN") \
 X(TX_T_RANGE, KT, "OUT OF RANGE", "UDEN FOR GRÆNSER") \
 X(TX_T_CFG_UNAVAIL, KT, "CONFIG UNAVAILABLE · NOT SAVED", "CONFIG UTILGÆNGELIG · INTET GEMT") \
 X(TX_T_NOLINK_SAVE, KT, "NO CONNECTION · NOT SAVED", "INGEN FORBINDELSE · INTET GEMT") \
 X(TX_T_WAIT_ACT, KT, "WAIT UNTIL THE ACTIVITY ENDS", "VENT TIL AKTIVITET ER SLUT") \
 X(TX_T_WRITE_REJ, KT, "NOT ACCEPTED · NOT SAVED", "IKKE ACCEPTERET · INTET GEMT") \
 X(TX_T_NOACK_SAVE, KT, "SAVE FAILED · NO REPLY", "GEM FEJLET · INTET SVAR") \
 X(TX_T_SAVED_MCU, KT, "SAVED · ACTIVE NOW", "GEMT · AKTIV NU") \
 X(TX_T_SAVED_CFG, KT, "SAVED", "GEMT") \
 X(TX_T_CFG_ERR, KT, "CONFIG ERROR · NOT SAVED", "CONFIG-FEJL · INTET GEMT") \
 X(TX_T_LANG_SAVED, KT, "LANGUAGE SAVED · ENGLISH", "SPROG GEMT · DANSK") \
 X(TX_T_LANG_NOFILE, KT, "LANGUAGE ACTIVE · FILE NOT SAVED", "SPROG AKTIVT · FIL IKKE GEMT") \
 X(TX_T_DRYRUN, KT, "DRY RUN · SEE " LC "cfgbak/boot_ui.log", "DRY-RUN · SE " LC "cfgbak/boot_ui.log") \
 X(TX_T_SW_FAIL_NOBIN, KT, "SWITCH FAILED · NO STOCK BINARY", "SKIFT FEJLEDE · INGEN STOCK-BINÆR") \
 X(TX_T_SW_FAIL_FORK, KT, "SWITCH FAILED · FORK ERROR", "SKIFT FEJLEDE · FORK-FEJL") \
 X(TX_T_SW_FAIL_DIED, KT, "SWITCH FAILED · STOCK UI EXITED", "SKIFT FEJLEDE · STOCK-UI STOPPEDE") \
 X(TX_T_BOOT_SAVED_OURS, KT, "SAVED · NEXT RESTART: " PRODUCT, "GEMT · NÆSTE GENSTART: " PRODUCT) \
 X(TX_T_BOOT_SAVED_ORIG, KT, "SAVED · NEXT RESTART: ORIGINAL", "GEMT · NÆSTE GENSTART: ORIGINAL") \
 X(TX_T_BOOT_LOCKED, KT, "LOCKED · FLAG NOT CHANGED", "LÅST · FLAG IKKE ÆNDRET") \
 X(TX_T_BOOT_ERR, KT, "FLAG ERROR · NOTHING CHANGED", "FLAG-FEJL · INTET ÆNDRET") \
 X(TX_T_STOP_SENT, KT, "STOPPING STEAM…", "STOPPER DAMP…") \
 X(TX_T_STOP_GRIND, KT, "STOPPING GRIND…", "STOPPER KVÆRN…") \
 X(TX_T_STOP_BREW, KT, "STOPPING BREW…", "STOPPER BRYG…") \
 X(TX_T_STOP_WATER, KT, "STOPPING WATER…", "STOPPER VAND…") \
 X(TX_T_STOPPED_GRIND, KT, "GRIND STOPPED", "KVÆRN STOPPET") \
 X(TX_T_STOPPED_BREW, KT, "BREW STOPPED", "BRYG STOPPET") \
 X(TX_T_STOPPED_WATER, KT, "WATER STOPPED", "VAND STOPPET") \
 X(TX_T_STOPPED_STEAM, KT, "STEAM STOPPED", "DAMP STOPPET") \
 X(TX_T_STOP_NOACK, KT, "STOP: NO REPLY · TRY AGAIN", "STOP: INTET SVAR · PRØV IGEN") \
 X(TX_T_STOP_UNCONF, KT, "STOP NOT CONFIRMED · TRY AGAIN", "STOP IKKE BEKRÆFTET · PRØV IGEN") \
 X(TX_T_STOP_FAIL, KT, "STOP NOT SENT · TRY AGAIN", "STOP IKKE SENDT · PRØV IGEN") \
 X(TX_T_PW_BUSY, KT, "STOP THE ACTIVITY FIRST · THEN OFF", "STOP AKTIVITETEN FØRST · SÅ SLUK") \
 X(TX_T_PW_NOLINK, KT, "NO CONNECTION · CANNOT TURN OFF", "INGEN FORBINDELSE · SLUKKER IKKE") \
 X(TX_T_PW_NOACK, KT, "POWER OFF: NO REPLY · TRY AGAIN", "SLUK: INTET SVAR · PRØV IGEN") \
 X(TX_T_PW_ON, KT, "MACHINE IS ON", "MASKINEN ER TÆNDT") \
 X(TX_T_HOLD_HINT, KT, "KEEP HOLDING TO START", "BLIV VED AT HOLDE FOR START") \
 X(TX_T_PINFO, K0, "PRE-INF. %d BAR = RAW %d · LINEAR", "PRÆ-INF. %d BAR = RÅ %d · LINEÆR") \
 X(TX_T_NOACK, KT, "THE MACHINE DID NOT ANSWER", "MASKINEN SVAREDE IKKE") \
 X(TX_T_NOSTART, KT, "NO START · CHECK THE MACHINE", "INGEN START · TJEK MASKINEN") \
 X(TX_H_DOSE, KLAB, "DOSE", "DOSIS") \
 X(TX_H_CUP, KLAB, "IN THE CUP", "I KOPPEN") \
 X(TX_H_MILK_TGT, KLAB, "MILK · TARGET %s°", "MÆLK · MÅL %s°") \
 X(TX_H_MILK, KLAB, "MILK", "MÆLK") \
 X(TX_H_MILK_PROBE, KLAB, "MILK PROBE", "MÆLKEPROBE") \
 X(TX_H_BOILER, KLAB, "BREW BOILER", "BRYGKEDEL") \
 X(TX_H_CRC, K0, "CRC %u", "CRC %u") \
 X(TX_H_DONE_IN, KSUB, "DONE IN %s", "FÆRDIG PÅ %s") \
 X(TX_H_AUTOSTOP_G, KSUB, "STOPS AT %s G", "STOPPER VED %s G") \
 X(TX_H_AUTOSTOP_T, KSUB, "STOPS AT %s°", "STOPPER VED %s°") \
 X(TX_H_WAND_NOSTOP, KSUB, "RELEASE THE WAND TO STOP", "SLIP WAND FOR AT STOPPE") \
 X(TX_H_DONE_RISE, KSUB, "DONE IN %s · RISES ≈%s° MORE", "FÆRDIG PÅ %s · STIGER ≈%s° TIL") \
 X(TX_H_NORESP, KSUB, "NO CONNECTION TO THE MACHINE", "INGEN FORBINDELSE TIL MASKINEN") \
 X(TX_H_WAKING, KSUB, "WAKING UP…", "VÅGNER…") \
 X(TX_H_HEATING_TO, KSUB, "HEATING TO %s°", "PÅ VEJ MOD %s°") \
 X(TX_H_SET_TO, KSUB, "TARGET %s°", "MÅL %s°") \
 X(TX_H_STARTING, KSUB, "STARTING %s…", "STARTER %s…") \
 X(TX_H_HOLD_GRIND, KSUB, "HOLD · GRIND %s G", "HOLD · KVÆRN %s G") \
 X(TX_H_HOLD_BREW, KSUB, "HOLD · BREW %s G", "HOLD · BRYG %s G") \
 X(TX_H_HOLD_WATER, KSUB, "HOLD · HOT WATER %s G", "HOLD · VARMT VAND %s G") \
 X(TX_H_HOLD_STEAM, KSUB, "HOLD · STEAM TO %s°", "HOLD · DAMP TIL %s°") \
 X(TX_ST_TIME, K0, "TIME", "TID") \
 X(TX_ST_BAR, K0, "≈BAR", "≈BAR") \
 X(TX_ST_PRAW, K0, "RAW", "RÅ") \
 X(TX_ST_PULSE, K0, "PULSE", "PULS") \
 X(TX_ST_FLOW, K0, "FLOW", "FLOW") \
 X(TX_ST_CURRENT, K0, "CURRENT", "STRØM") \
 X(TX_ST_GRINDSTEP, K0, "STEP", "TRIN") \
 X(TX_ST_BOILER, K0, "BOILER", "KEDEL") \
 X(TX_ST_STEAMBOILER, K0, "STEAM", "DAMP") \
 X(TX_ST_LEVEL, K0, "LEVEL", "NIVEAU") \
 X(TX_ST_GROUP, K0, "GROUP", "GRUPPE") \
 X(TX_ST_WEIGHT, K0, "WEIGHT", "VÆGT") \
 X(TX_ST_MILK, K0, "MILK", "MÆLK") \
 X(TX_C_HINT, K0, "HOLD = START · TAP = SETTINGS", "HOLD = START · TRYK = INDSTIL") \
 X(TX_C_PREHEAT, K0, IC_CLOCK " PRE-HEAT · %s %s", IC_CLOCK " FORVARMNING · %s %s") \
 X(TX_C_ERRCODE, K0, "CODE %s · TAP FOR DETAILS", "KODE %s · TRYK FOR DETALJER") \
 X(TX_C_DIAGREF, K0, "TAP FOR DETAILS", "TRYK FOR DETALJER") \
 X(TX_C_PRESS, K0, "PRESSURE", "TRYK") \
 X(TX_C_WEIGHT, K0, "WEIGHT", "VÆGT") \
 X(TX_C_MILK, K0, "MILK TEMP", "MÆLKETEMP") \
 X(TX_C_TARGET, K0, "- - TARGET", "- - MÅL") \
 X(TX_C_RAW, K0, "RAW", "RÅ") \
 X(TX_B_STOP_STEAM, K0, "STOP STEAM", "STOP DAMP") \
 X(TX_B_STOP_GRIND, K0, "STOP GRIND", "STOP KVÆRN") \
 X(TX_B_STOP_BREW, K0, "STOP BREW", "STOP BRYG") \
 X(TX_B_STOP_WATER, K0, "STOP WATER", "STOP VAND") \
 X(TX_B_START, K0, "HOLD TO START", "HOLD FOR START") \
 X(TX_B_HOLD_STEAM, K0, "HOLD\xC2\xB7" "STEAM", "HOLD\xC2\xB7" "DAMP") \
 X(TX_B_BACK, KBTN, "BACK", "TILBAGE") \
 X(TX_B_CANCEL, KBTN, "CANCEL", "ANNULLER") \
 X(TX_B_SAVE, KBTNS, "SAVE", "GEM") \
 X(TX_B_SAVING, KBTNS, "SAVING", "GEMMER") \
 X(TX_B_SWITCH, KBTN, "SWITCH NOW", "SKIFT NU") \
 X(TX_B_EXIT, KBTNS, "EXIT", "AFSLUT") \
 X(TX_X_BOOTUI, K0, "BOOT UI", "BOOT-UI") \
 X(TX_X_SWITCH, K0, "SWITCH TO ORIGINAL NOW", "SKIFT TIL ORIGINAL NU") \
 X(TX_X_SWITCH_SUB, KXSUB, "IMMEDIATE · NO RESTART", "STRAKS · UDEN GENSTART") \
 X(TX_BN_ORIGINAL, K0, "ORIGINAL", "ORIGINAL") \
 X(TX_BN_OURS, K0, PRODUCT, PRODUCT) \
 X(TX_BN_SLOTB, K0, "SLOT B", "SLOT B") \
 X(TX_BL_NOMOUNT, KXSUB, "LOCKED · SLOT B NOT MOUNTED", "LÅST · SLOT B IKKE MOUNTET") \
 X(TX_BL_NOMOUNT_L, KNOTE, LC SLOT_B_MNT UC " IS NOT MOUNTED", LC SLOT_B_MNT UC " ER IKKE MOUNTET") \
 X(TX_BL_NOBIN, KXSUB, "LOCKED · SLOT B HAS NO " LC "demo_v9", "LÅST · SLOT B UDEN " LC "demo_v9") \
 X(TX_BL_NOBIN_L, KNOTE, LC SLOT_B_EXE UC " IS MISSING", LC SLOT_B_EXE UC " MANGLER") \
 X(TX_BL_VENDOR, KXSUB, "LOCKED · SLOT B IS NOT " PRODUCT, "LÅST · SLOT B ER IKKE " PRODUCT) \
 X(TX_BL_VENDOR_L, KNOTE, LC SLOT_B_EXE UC " IS NOT " PRODUCT, LC SLOT_B_EXE UC " ER IKKE " PRODUCT) \
 X(TX_BS_OURS, K0, "NOW: " PRODUCT " (B) AT BOOT", "NU: " PRODUCT " (B) VED BOOT") \
 X(TX_BS_ORIG, K0, "NOW: ORIGINAL (A) AT BOOT", "NU: ORIGINAL (A) VED BOOT") \
 X(TX_BS_SLOTB, K0, "NOW: SLOT B (NOT " PRODUCT ") AT BOOT", "NU: SLOT B (IKKE " PRODUCT ") VED BOOT") \
 X(TX_BH1, KXSUB, "APPLIES AT NEXT RESTART", "GÆLDER NÆSTE GENSTART") \
 X(TX_BH2, KF1, "OTA MAY OVERWRITE THIS", "OTA KAN OVERSKRIVE DETTE") \
 X(TX_BH3, KF2, "CHOICE — SEE BOOT" LC ".md" UC " §7", "VALG — SE BOOT" LC ".md" UC " §7") \
 X(TX_SW_LOCK, KXSUB, "LOCKED · STOCK BINARY MISSING", "LÅST · STOCK-BINÆR MANGLER") \
 X(TX_SW_LOCK_L, KNOTE, LC SLOT_A_EXE UC " IS MISSING", LC SLOT_A_EXE UC " MANGLER") \
 X(TX_D_MEASURED, K0, "MEASURED VALUE · NOT A SETTING", "MÅLT VÆRDI · INGEN INDSTILLING") \
 X(TX_D_NOEDIT, K0, "CANNOT BE CHANGED HERE", "KAN IKKE ÆNDRES HER") \
 X(TX_D_NOPTS, K0, "%d OPTIONS", "%d VALG") \
 X(TX_D_SAVING, KST, "SAVING…", "GEMMER…") \
 X(TX_D_OOR, KST, "OUT OF SPEC · PICK A NEW VALUE", "UDEN FOR SPEC · VÆLG NY VÆRDI") \
 X(TX_D_HOLDFAST, KST, "HOLD - OR + TO GO FAST", "HOLD - ELLER + FOR HURTIGT") \
 X(TX_D_CHANGED, K0, "CHANGED · WAS %s %s", "ÆNDRET · FØR %s %s") \
 X(TX_D_LOCKED, KST, "LOCKED", "LÅST") \
 X(TX_Y_CHANGED, K0, "CHANGED · WAS %s", "ÆNDRET · FØR %s") \
 X(TX_Y_FLAG_YES, KF1, "FLAG " LC BOOT_FLAG UC " EXISTS", "FLAG " LC BOOT_FLAG UC " FINDES") \
 X(TX_Y_FLAG_NO, KF1, "NO FLAG " LC BOOT_FLAG, "INTET FLAG " LC BOOT_FLAG) \
 X(TX_Y_FLAG_HANDSOFF, KF2, "FLAG IS NOT TOUCHED HERE", "FLAGET RØRES IKKE HERFRA") \
 X(TX_Y_NEXT, K0, "NEXT RESTART: %s", "NÆSTE GENSTART: %s") \
 X(TX_Y_NOSTOCK, KF1, "NO STOCK UI TO START", "INTET STOCK-UI AT STARTE") \
 X(TX_Y_STAYS, KF2, PRODUCT " KEEPS RUNNING", PRODUCT " BLIVER KØRENDE") \
 X(TX_Y_CLOSE_CONT, KST, PRODUCT " CLOSES · STOCK UI RESUMES", PRODUCT " LUKKER · STOCK-UI FORTSÆTTER") \
 X(TX_Y_CLOSE_START, KST, PRODUCT " CLOSES · STOCK UI STARTS", PRODUCT " LUKKER · STOCK-UI STARTER") \
 X(TX_Y_REMINDER_FOLLOWS, KNOTE, "CONFIG CHANGED · REMINDER FOLLOWS", "CONFIG ÆNDRET · PÅMINDELSE FØLGER") \
 X(TX_Y_PAUSED_PID, KNOTE, "STOCK UI IS PAUSED · PID %d", "STOCK-UI ER PAUSET · PID %d") \
 X(TX_Y_RUNSH, KF1, "RUN.SH MODE · CLEAN EXIT", "RUN.SH-TILSTAND · REN EXIT") \
 X(TX_Y_BACK_RUNSH, KF2, "BACK HERE: RUN.SH", "TILBAGE HERTIL: RUN.SH") \
 X(TX_Y_FRESH, KNOTE, "FRESH START · READS CURRENT CONFIG", "FRISK START · LÆSER AKTUEL CONFIG") \
 X(TX_Y_BACK_GESTURE1, KF1, "BACK HERE: 3 LONG PRESSES", "TILBAGE HERTIL: 3 LANGE TRYK") \
 X(TX_Y_BACK_GESTURE2, KF2, "TOP LEFT CORNER (1.5 S)", "ØVERST TIL VENSTRE (1.5 S)") \
 X(TX_Y_NOSWITCHER, KF1, "SWITCHER DAEMON NOT RUNNING", "SWITCHER-DAEMON KØRER IKKE") \
 X(TX_Y_BACK_RESTART, KF2, "BACK HERE: RESTART", "TILBAGE HERTIL: GENSTART") \
 X(TX_Y_BACK_TELNET, KF2, "BACK HERE: TELNET ONLY", "TILBAGE HERTIL: KUN TELNET") \
 X(TX_Y_EXIT_TITLE, K0, "CONFIG HAS CHANGED", "CONFIG ER ÆNDRET") \
 X(TX_Y_EXIT_SUB, K0, "STOCK UI HAS OLD VALUES", "STOCK-UI HAR GAMLE VÆRDIER") \
 X(TX_Y_EXIT_NOTE, KNOTE, "RUN VIA TELNET · WATCHDOG RESTARTS IT", "KØR VIA TELNET · WATCHDOG STARTER DEN IGEN") \
 X(TX_Y_EXIT_F1, KF1, "OTHERWISE STOCK UI SENDS", "ELLERS SENDER STOCK-UI") \
 X(TX_Y_EXIT_F2, KF2, "OLD VALUES WHEN BREWING", "GAMLE VÆRDIER VED BRYG") \
 X(TX_Y_EXIT_L1, K0, "STOCK UI NEEDS A RESTART OF", "STOCK-UI KRÆVER GENSTART AF") \
 X(TX_Y_EXIT_L2, K0, LC "demo_v9" UC " FOR NEW VALUES", LC "demo_v9" UC " FOR NYE VÆRDIER") \
 X(TX_Y_EXIT_CMD, K0, LC "kill $(pidof demo_v9)", LC "kill $(pidof demo_v9)") \
 X(TX_Y_BYE_T, K0, "SWITCHING TO ORIGINAL", "SKIFTER TIL ORIGINAL") \
 X(TX_Y_BYE_CONT, K0, "STOCK UI RESUMES", "STOCK-UI FORTSÆTTER") \
 X(TX_Y_BYE_START, K0, "STOCK UI STARTING · ONE MOMENT", "STOCK-UI STARTER · ET ØJEBLIK") \
 X(TX_Y_BYE_CLOSED, K0, PRODUCT " HAS CLOSED", PRODUCT " ER AFSLUTTET") \
 X(TX_PW_OFFING, K0, "TURNING OFF…", "SLUKKER…") \
 X(TX_PW_OFF, K0, "OFF", "SLUKKET") \
 X(TX_PW_WAKING, K0, "TURNING ON…", "TÆNDER…") \
 X(TX_PW_SUB_OFFING, K0, "THE MACHINE GOES TO SLEEP", "MASKINEN GÅR I DVALE") \
 X(TX_PW_SUB_OFF, K0, "HOLD THE POWER BUTTON TO TURN ON", "HOLD KNAPPEN INDE FOR AT TÆNDE") \
 X(TX_PW_SUB_WAKING, K0, "KEEP HOLDING THE BUTTON A MOMENT", "HOLD KNAPPEN INDE ET ØJEBLIK") \
 X(TX_PW_NOTE_OFF, K0, "THE SCREEN TURNS OFF · TOUCH TO SHOW", "SKÆRMEN SLUKKER · RØR FOR AT VISE") \
 X(TX_DL_LINK, K0, "LINK", "LINK") \
 X(TX_DL_PHASE, K0, "PHASE", "FASE") \
 X(TX_DL_RXOK, K0, "RX OK", "RX OK") \
 X(TX_DL_RXCRC, K0, "RX CRC / RESYNC", "RX CRC / RESYNC") \
 X(TX_DL_WORKSTA, K0, "WORK_STA", "WORK_STA") \
 X(TX_DL_E123, K0, "E1 / E2 / E3", "E1 / E2 / E3") \
 X(TX_DL_CMD6, K0, "CMD6 OK·RTX·ERR·BLK", "CMD6 OK·RTX·FEJL·BLOK") \
 X(TX_DL_AWAKE, K0, "AWAKE (WORD 1)", "VÅGEN (ORD 1)") \
 X(TX_DL_MAINS, K0, "MAINS", "NET") \
 X(TX_DL_CURRENT, K0, "CURRENT 1 / 2", "STRØM 1 / 2") \
 X(TX_DL_FRAMEMS, K0, "FRAME MS NOW/MAX", "FRAME MS NU/MAX") \
 X(TX_DL_MIRROR, K0, "MIRROR 112-114", "SPEJL 112-114") \
 X(TX_DL_CFGMD5, K0, "CONFIG MD5", "CONFIG MD5") \
 X(TX_DL_VERSION, K0, "VERSION", "VERSION") \
 X(TX_DL_BP_A, K0, "BOOT PUSH 1-30", "BOOT-PUSH 1-30") \
 X(TX_DL_BP_B, K0, "BOOT PUSH 112-114", "BOOT-PUSH 112-114") \
 X(TX_DL_BP_A_RE, K0, "RE-PUSH %d · 1-30", "RE-PUSH %d · 1-30") \
 X(TX_DL_BP_B_RE, K0, "112-114", "112-114") \
 X(TX_DL_FLAG, KDL, "FLAG " LC BOOT_FLAG, "FLAG " LC BOOT_FLAG) \
 X(TX_DL_NEXTBOOT, KDL, "NEXT BOOT", "NÆSTE BOOT") \
 X(TX_DL_STARTED, KDL, "STARTED FROM", "STARTET FRA") \
 X(TX_DL_SLOTB, KDL, "SLOT B", "SLOT B") \
 X(TX_DL_SWITCHER, KDL, "SWITCHER DAEMON", "SWITCHER-DAEMON") \
 X(TX_DL_STOCK, KDL, "STOCK PROCESS", "STOCK-PROCES") \
 X(TX_DV_OK, KDV, "OK", "OK") \
 X(TX_DV_DOWN, KDV, "DOWN", "NEDE") \
 X(TX_DV_YES, KDV, "YES", "JA") \
 X(TX_DV_NO, KDV, "NO", "NEJ") \
 X(TX_DV_MAINS, K0, "%u V · %u HZ", "%u V · %u HZ") \
 X(TX_DV_VERSION, K0, PRODUCT " " MERAKIPLUS_VERSION, PRODUCT " " MERAKIPLUS_VERSION) \
 X(TX_DV_VERSION_RO, K0, PRODUCT " " MERAKIPLUS_VERSION " RO", PRODUCT " " MERAKIPLUS_VERSION " RO") \
 X(TX_OTA_TRIAL, K0, "TRIAL", "PRØVE") \
 X(TX_OTA_FALLBACK, K0, "FALLBACK", "RESERVE") \
 X(TX_OTA_FAILED, K0, "%s FAILED", "%s FEJLET") \
 X(TX_OTA_VENDOR, K0, "VENDOR UPDATE · RUN INSTALL", "PRODUCENT-OPD. · KØR INSTALL") \
 X(TX_T_UPD_VENDOR, KT, "VENDOR UPDATE FOUND · RUN INSTALL", "PRODUCENT-OPD. · KØR INSTALL") \
 X(TX_T_UPD_ROLLED, KT, "%s FAILED · BACK ON %s", "%s FEJLEDE · TILBAGE PÅ %s") \
 X(TX_DV_MISSING, KDV, "MISSING", "MANGLER") \
 X(TX_DV_FLAG_YES, KDV, "EXISTS", "FINDES") \
 X(TX_DV_FLAG_NO, KDV, "NOT PRESENT", "FINDES IKKE") \
 X(TX_DV_SLOTA, KDV, "SLOT A", "SLOT A") \
 X(TX_DV_SLOTB, KDV, "SLOT B", "SLOT B") \
 X(TX_DV_RUNSH, KDV, "RUN.SH", "RUN.SH") \
 X(TX_DV_UNKNOWN, KDV, "UNKNOWN", "UKENDT") \
 X(TX_DV_B_OK, KDV, PRODUCT " FOUND", PRODUCT " FUNDET") \
 X(TX_DV_B_NOMOUNT, KDV, "NOT MOUNTED", "IKKE MOUNTET") \
 X(TX_DV_B_NOBIN, KDV, "NO DEMO_V9", "UDEN DEMO_V9") \
 X(TX_DV_B_VENDOR, KDV, "NOT " PRODUCT, "IKKE " PRODUCT) \
 X(TX_DV_RUNNING, KDV, "RUNNING", "KØRER") \
 X(TX_DV_NOTRUNNING, KDV, "NOT RUNNING", "KØRER IKKE") \
 X(TX_DV_NONE, KDV, "NONE", "INGEN") \
 X(TX_DV_ALSO, KDV, "ALSO RUNNING!", "KØRER SAMTIDIG!") \
 X(TX_DV_PAUSED, KDV, "PAUSED", "PAUSET") \
 X(TX_DV_BP_OFF, KDV, "OFF", "FRA") \
 X(TX_DV_BP_WAIT, KDV, "WAITING", "VENTER") \
 X(TX_DV_BP_SENT, KDV, "SENDING", "SENDER") \
 X(TX_DV_BP_NOACK, KDV, "NO ACK", "INGEN ACK") \
 X(TX_DV_BP_BLOCKED, KDV, "REJECTED", "AFVIST") \
 X(TX_DV_BP_NOCFG, KDV, "NO CONFIG", "INGEN CONFIG") \
 X(TX_DV_BP_SKIP, KDV, "SKIPPED", "SPRUNGET OVER") \
 X(TX_DG_CAP, KDF, "BOOT · SLOT · SWITCHER", "BOOT · SLOT · SWITCHER") \
 X(TX_DG_L_PID, KDF, "PID · " PRODUCT " / STOCK UI / SWITCHER", "PID · " PRODUCT " / STOCK-UI / SWITCHER") \
 X(TX_DG_L_EXE, KDF, "RUNNING FROM · " LC "/proc/self/exe", "KØRER FRA · " LC "/proc/self/exe") \
 X(TX_DG_L_EVT, KDF, "LAST SWITCH · " LC SW_LOG, "SENESTE SKIFT · " LC SW_LOG) \
 X(TX_DG_L_NET, KDF, "NETWORK · WPA PID / IP / STATE", "NETVÆRK · WPA-PID / IP / TILSTAND") \
 X(TX_NS_OFF, K0, "OFF", "FRA") \
 X(TX_NS_DOWN, K0, "DOWN", "NEDE") \
 X(TX_NS_STARTING, K0, "STARTING", "STARTER") \
 X(TX_NS_CONNECTING, K0, "CONNECTING", "FORBINDER") \
 X(TX_NS_UP, K0, "UP", "OPPE") \
 X(TX_DG_NOEXE, KDF, "NOT READABLE", "IKKE LÆSBAR") \
 X(TX_DG_NOEVT, KDF, "NO SWITCH EVENT IN THE LOG", "INGEN SKIFT-HÆNDELSE I LOGGEN") \
 X(TX_DG_NOLOG, KDF, "LOG NOT READABLE", "LOG IKKE LÆSBAR") \
 X(TX_DG_L_GRAPH, KDF, "GRAPH · TAP TO TOGGLE", "GRAF · TRYK FOR AT SKIFTE") \
 X(TX_DV_G_SMOOTH, KDF, "SMOOTH · MEDIAN 3 + EMA", "GLAT · MEDIAN 3 + EMA") \
 X(TX_DV_G_RAW, KDF, "RAW · UNFILTERED", "RÅ · UFILTRERET") \
 X(TX_DG_PCAP, KDF, "PRESSURE · ESTIMATE", "TRYK · ESTIMAT") \
 X(TX_DG_L_PANCH, KDF, "ANCHOR · PRE-INF. IN RECORDING", "ANKER · PRÆ-INF. I OPTAGELSEN") \
 X(TX_DV_PANCH, K0, "%d BAR = RAW %d · " LC "k" UC " %.5f", "%d BAR = RÅ %d · " LC "k" UC " %.5f") \
 X(TX_DG_L_PNOW, KDF, "NOW · WORD 51 · PUMP SIDE, LINEAR", "NU · ORD 51 · PUMPE-SIDE, LINEÆR") \
 X(TX_DV_PNOW, K0, "RAW %d = ≈%.1f BAR", "RÅ %d = ≈%.1f BAR") \
 X(TX_DV_PPULSE, K0, "RAW %d · PULSE, NO PRESSURE", "RÅ %d · PULS, IKKE TRYK") \
 X(TX_DV_PMAINT, K0, "RAW %d · MAINTENANCE PULSE", "RÅ %d · VEDLIGEHOLD-PULS") \
 X(TX_DG_ABOUT, KDF, "ABOUT", "OM") \
 X(TX_DG_RAWCAP, KDF, "RAW COUNTERS · SERVICE", "RÅ TÆLLERE · SERVICE") \
 X(TX_DG_L_LANG, KDF, "UI LANGUAGE · " LC "merakiplus/lang", "UI-SPROG · " LC "merakiplus/lang") \
 X(TX_DG_LANG_DEFAULT, K0, "%s · DEFAULT (NO FILE)", "%s · STANDARD (INGEN FIL)") \
 X(TX_DG_LANG_FILE, K0, "%s · FROM FILE", "%s · FRA FIL") \
 X(TX_DG_LANG_UNKNOWN, K0, "%s · UNKNOWN FILE VALUE", "%s · UKENDT VÆRDI I FIL") \
 X(TX_DG_LANG_SESSION, K0, "%s · NOT SAVED", "%s · IKKE GEMT") \
 X(TX_DG_PWCAP, KDF, "POWER BUTTON", "TÆND/SLUK-KNAP") \
 X(TX_DG_L_PWNODE, KDF, "BUTTON NODE · EVDEV · NO GRAB", "KNAP-NODE · EVDEV · INTET GRAB") \
 X(TX_DL_PW_STATE, KDL, "STATE", "TILSTAND") \
 X(TX_DL_PW_W0, KDL, "POWER · WORD 0", "STRØM · ORD 0") \
 X(TX_DL_PW_COUNT, KDL, "PRESS · OFF · ON", "TRYK · SLUK · TÆND") \
 X(TX_DL_PW_LAST, KDL, "LAST KEY", "SIDSTE KNAP") \
 X(TX_DV_PW_ON, KDV, "ON", "TÆNDT") \
 X(TX_DV_PW_OFFING, KDV, "TURNING OFF", "SLUKKER") \
 X(TX_DV_PW_OFF, KDV, "OFF", "SLUKKET") \
 X(TX_DV_PW_WAKING, KDV, "TURNING ON", "TÆNDER") \
 X(TX_DV_PW_NOREPLY, KDV, "NO REPLY", "INTET SVAR") \
 X(TX_DV_PW_KEY, K0, "%d = %d · %ld S", "%d = %d · %ld S") \
 X(TX_DV_PW_NOKEY, KDV, "NO PRESS YET", "INGEN TRYK") \
 X(TX_DV_PW_NONODE, KDF, "NO BUTTON NODE FOUND", "INGEN KNAP-NODE FUNDET") \
 X(TX_SL_PREHEAT, K0, "PRE-HEAT", "FORVARMNING") \
 X(TX_SL_PHAT, K0, "PRE-HEAT AT", "FORVARM KL.") \
 X(TX_SL_PHREP, K0, "REPEAT", "GENTAG") \
 X(TX_SL_AUTOOFF, K0, "AUTO OFF", "AUTO-SLUK") \
 X(TX_SN_PREHEAT, KNOTE, "THE MACHINE WAKES + HEATS ITSELF", "MASKINEN VÅGNER + VARMER SELV") \
 X(TX_SN_PHAT, KNOTE, "LOCAL TIME", "LOKAL TID") \
 X(TX_SN_PHREP, KNOTE, "ONCE = SWITCHES ITSELF OFF AFTER USE", "EN GANG = SLÅR SIG SELV FRA BAGEFTER") \
 X(TX_SN_AUTOOFF, KNOTE, "NO TOUCH OR ACTIVITY · THEN OFF", "INGEN BERØRING ELLER AKTIVITET · SÅ SLUK") \
 X(TX_SN_TIMEFMT, KNOTE, "HOME CLOCK + STOCK UI CLOCK", "UR PÅ HJEM + STOCK-UI'ETS UR") \
 X(TX_E_ONCE, K0, "ONCE", "EN GANG") \
 X(TX_E_DAILY, K0, "EVERY DAY", "HVER DAG") \
 X(TX_E_WORKDAYS, K0, "MON-FRI", "MAN-FRE") \
 X(TX_E_WEEKEND, K0, "SAT-SUN", "LØR-SØN") \
 X(TX_E_MIN15, K0, "15 MIN", "15 MIN") \
 X(TX_E_MIN20, K0, "20 MIN", "20 MIN") \
 X(TX_E_MIN30, K0, "30 MIN", "30 MIN") \
 X(TX_E_MIN60, K0, "60 MIN", "60 MIN") \
 X(TX_T_OWN_SAVED, KT, "SCHEDULE SAVED", "TIDSPLAN GEMT") \
 X(TX_T_OWN_NOFILE, KT, "ACTIVE · FILE NOT SAVED", "AKTIV · FIL IKKE GEMT") \
 X(TX_SL_SOUND, K0, "SOUND", "LYD") \
 X(TX_SL_SNDRDY, K0, "READY TONE", "KLAR-TONE") \
 X(TX_SN_SOUND, KNOTE, "START JINGLE + BOILER READY TONES", "START-JINGLE + KLAR-TONER") \
 X(TX_SN_SNDRDY, KNOTE, "ITS OWN TONE PER BOILER · WHEN HOT", "SIN EGEN TONE PR. KEDEL · NÅR VARM") \
 X(TX_E_SND_STOCK, K0, "STOCK SOUNDS", "STOCK-LYDE") \
 X(TX_E_RDY_BREW, K0, "BREW BOILER", "BRYGKEDEL") \
 X(TX_E_RDY_STEAM, K0, "STEAM BOILER", "DAMPKEDEL") \
 X(TX_E_RDY_BOTH, K0, "BOTH BOILERS", "BEGGE KEDLER") \
 X(TX_T_SND_SAVED, KT, "SOUND SAVED", "LYD GEMT") \
 X(TX_DG_SNDCAP, KDF, "SOUND", "LYD") \
 X(TX_DL_SND, KDL, "SOUND · VOLUME", "LYD · LYDSTYRKE") \
 X(TX_DL_SND_LAST, KDL, "LAST SOUND", "SIDSTE LYD") \
 /* v6.22: water dosing gate — one contiguous block (TX_ER_E2_SCALE .. TX_DV_WD_FAULT; the v6.17 string-count rule leaves it out) */ \
 X(TX_ER_E2_SCALE, KSUB, "SCALE FAULT", "VÆGTFEJL") \
 X(TX_H_HOLD_WATER_MAN, KSUB, "HOLD · HOT WATER · MANUAL", "HOLD · VARMT VAND · MANUEL") \
 X(TX_H_WATER_MAN, KSUB, "MANUAL · RUNS UNTIL STOP", "MANUEL · KØRER TIL STOP") \
 X(TX_A_WATER_MAN, K0, "MAN.", "MAN.") \
 X(TX_W_MAN_T, KST, "MANUAL HOT WATER", "MANUELT VARMT VAND") \
 X(TX_W_MAN_RUN, KST, "RUNS UNTIL YOU TAP STOP", "LØBER TIL DU TRYKKER STOP") \
 X(TX_W_WHY_OLD, KST, "CONTROLLER < 5.1.3: NO WEIGHT STOP", "STYRING < 5.1.3: INGEN VÆGT-STOP") \
 X(TX_W_WHY_UNK, KST, "CONTROLLER VERSION UNKNOWN", "STYRINGENS VERSION UKENDT") \
 X(TX_W_WHY_FAULT, KST, "SCALE FAULT: NO WEIGHT STOP", "VÆGTFEJL: INGEN VÆGT-STOP") \
 X(TX_T_WATER_SCALE, KT, "SCALE FAULT · WATER STOPPED", "VÆGTFEJL · VAND STOPPET") \
 X(TX_DG_MCUCAP, KDF, "CONTROLLER", "STYRING") \
 X(TX_DL_MCU_VER, KDL, "FIRMWARE", "FIRMWARE") \
 X(TX_DL_MCU_WD, KDL, "HOT WATER", "VARMT VAND") \
 X(TX_DV_MCU_NONE, KDL, "NOT REPORTED", "IKKE MELDT") \
 X(TX_DV_MCU_COPY, KDL, "COPY DIFFERS", "KOPI AFVIGER") \
 X(TX_DV_WD_AUTO, KDL, "AUTO + MANUAL", "AUTO + MANUEL") \
 X(TX_DV_WD_MAN, KDL, "MANUAL ONLY", "KUN MANUEL") \
 X(TX_DV_WD_FAULT, KDL, "MANUAL (SCALE)", "MANUEL (VÆGT)") \
 X(TX_DV_SND_ON, KDV, "ON · %d", "TIL · %d") \
 X(TX_DV_SND_STOCK, KDV, "STOCK · %d", "STOCK · %d") \
 X(TX_DV_SND_OFF, KDV, "OFF", "FRA") \
 X(TX_DV_SND_MUTE, KDV, "VOLUME 0", "LYDSTYRKE 0") \
 X(TX_DV_SND_NOHELPER, KDV, "NO PLAYER", "INGEN AFSPILLER") \
 X(TX_DV_SE_JINGLE, KDV, "JINGLE", "JINGLE") \
 X(TX_DV_SE_BREW, KDV, "BREW", "BRYG") \
 X(TX_DV_SE_STEAM, KDV, "STEAM", "DAMP") \
 X(TX_DV_SE_PREVIEW, KDV, "TEST", "TEST") \
 X(TX_DV_SO_PLAYED, KDV, "PLAYED", "SPILLET") \
 X(TX_DV_SO_MUTED, KDV, "MUTED", "LYDLØS") \
 X(TX_DV_SO_SKIP, KDV, "SKIPPED", "UDELADT") \
 X(TX_DV_SO_FAIL, KDV, "FAILED", "FEJL") \
 X(TX_DV_SO_DROP, KDV, "BUSY", "OPTAGET") \
 X(TX_T_AUTOOFF_SOON, KT, "AUTO OFF IN 30 S · TOUCH TO STAY ON", "AUTO-SLUK OM 30 S · RØR SKÆRMEN") \
 X(TX_T_ONCE_USED, KT, "ONE-TIME PRE-HEAT USED · NOW OFF", "FORVARMNING BRUGT · NU FRA") \
 X(TX_T_NOCLOCK, KT, "CLOCK NOT SET · NO PRE-HEAT ALARM", "UR IKKE SAT · INGEN FORVARM-ALARM") \
 X(TX_AM, K0, "A.M.", "A.M.") \
 X(TX_PM, K0, "P.M.", "P.M.") \
 X(TX_WD_SUN, K0, "SUN", "SØN") \
 X(TX_WD_MON, K0, "MON", "MAN") \
 X(TX_WD_TUE, K0, "TUE", "TIR") \
 X(TX_WD_WED, K0, "WED", "ONS") \
 X(TX_WD_THU, K0, "THU", "TOR") \
 X(TX_WD_FRI, K0, "FRI", "FRE") \
 X(TX_WD_SAT, K0, "SAT", "LØR") \
 X(TX_DG_TCAP, KDF, "TIME · SCHEDULE", "TID · PLAN") \
 X(TX_DL_T_CLOCK, KDL, "LOCAL TIME", "LOKAL TID") \
 X(TX_DL_T_ZONE, KDL, "ZONE · DST", "ZONE · SOMMERTID") \
 X(TX_DL_T_AUTOOFF, KDL, "AUTO OFF · IDLE", "AUTO-SLUK · HVILE") \
 X(TX_DL_T_ALARM, KDL, "ALARM SENT · D H:M", "ALARM SENDT · D T:M") \
 X(TX_DG_L_NEXT, KDF, "NEXT PRE-HEAT · CONTROLLER ALARM", "NÆSTE FORVARMNING · ALARM") \
 X(TX_DV_T_NOTSET, KDV, "NOT SET", "IKKE SAT") \
 X(TX_DV_T_NONE, KDV, "NONE", "INGEN") \
 X(TX_DV_T_NOSCHED, KDF, "NO PRE-HEAT PLANNED", "INGEN FORVARMNING PLANLAGT") \
 X(TX_DV_T_DST, K0, "%s · DST", "%s · SOMMERTID") \
 X(TX_DV_T_STD, K0, "%s · STANDARD", "%s · NORMALTID") \
 X(TX_DV_T_IDLE, K0, "%s · %d:%02d", "%s · %d:%02d") \
 X(TX_O_SWITCHED, KCON, "== merakiplus: runtime switch B->A (user button): stock UI pid %d started from " SLOT_A_EXE "; merakiplus exits\n", "== merakiplus: runtime-skift B->A (bruger-knap): stock-UI pid %d startet fra " SLOT_A_EXE "; merakiplus afslutter\n") \
 X(TX_O_RESTART, KCON, "== merakiplus: STOCK UI NEEDS A RESTART OF demo_v9 FOR NEW VALUES:   kill $(pidof demo_v9)\n==        (config.json changed by merakiplus; the watchdog restarts it, the boot push syncs the controller)\n", "== merakiplus: STOCK-UI KRAEVER GENSTART AF demo_v9 FOR NYE VAERDIER:   kill $(pidof demo_v9)\n==        (config.json aendret af merakiplus; watchdog genstarter, boot-push synker controlleren)\n") \
 X(TX_O_CFGCHANGED, KCON, "== merakiplus: config.json changed %d time(s) - no paused stock UI is waiting; the next stock start reads it itself\n", "== merakiplus: config.json aendret %d gang(e) - intet pauset stock-UI venter; naeste stock-start laeser den selv\n") \
 X(TX_O_BLOCKED, KCON, "== merakiplus: %ld cmd6 frame(s) rejected by the frame policy (see UI_v6.md)\n", "== merakiplus: %ld cmd6-frame(s) afvist af frame-politikken (se UI_v6.md)\n") \
 /* end */

enum {
#define X(id,k,en,da) id,
 STR_TABLE(X)
#undef X
 NSTR };
static const char*const STR[NLANG][NSTR]={
 [LANG_EN]={
#define X(id,k,en,da) en,
 STR_TABLE(X)
#undef X
 },
 [LANG_DA]={
#define X(id,k,en,da) da,
 STR_TABLE(X)
#undef X
 }};
#if defined(MERAKIPLUS_PREVIEW)||defined(SIM_RT)
static const char*const STR_ID[NSTR]={
#define X(id,k,en,da) #id,
 STR_TABLE(X)
#undef X
};
#endif
#ifdef MERAKIPLUS_PREVIEW
static const unsigned char STR_K[NSTR]={
#define X(id,k,en,da) k,
 STR_TABLE(X)
#undef X
};
#endif
static int ui_lang=LANG_EN;
/* lookup per render: never cached across a language switch. Unknown id/language -> English. */
static const char* TR(int id){
    if(id<0||id>=NSTR) id=TX_NONE;
    const char*s=STR[(ui_lang>=0&&ui_lang<NLANG)?ui_lang:LANG_EN][id];
    return s?s:STR[LANG_EN][id];
}

/* ---- the language file: merakiplus's OWN setting (not the stock UI's config.language) ----
 * Content: exactly "en" or "da" (plain text; on read, whitespace/newlines and
 * upper case are tolerated, so `echo da > lang` also works). If the file is missing, cannot be read,
 * or contains anything else: ENGLISH. Written atomically: tmp in the same directory + fsync +
 * re-read/compare + rename() + fsync of the directory. */
static char lang_path[256]="/new_customer/merakiplus/lang";
static const char*const LANG_CODE[NLANG]={"en","da"};
enum { LS_DEFAULT=0, LS_FILE, LS_UNKNOWN, LS_SESSION };   /* where the active language comes from (DIAGNOSTICS) */
static int lang_src=LS_DEFAULT;
static int static_dirty;                                   /* (defined in the compositor section) */
static int lang_parse(const char*b,int n){
    while(n>0&&isspace((unsigned char)b[n-1])) n--;
    while(n>0&&isspace((unsigned char)*b)){ b++; n--; }
    if(n!=2) return -1;
    for(int l=0;l<NLANG;l++) if(tolower((unsigned char)b[0])==LANG_CODE[l][0]&&tolower((unsigned char)b[1])==LANG_CODE[l][1]) return l;
    return -1;
}
static void lang_set(int l){ if(l<0||l>=NLANG) l=LANG_EN; if(l!=ui_lang){ ui_lang=l; static_dirty=1; } }
static int lang_load(void){
    char b[16]; int l=-1, fd=open(lang_path,O_RDONLY);
    if(fd<0) lang_src=LS_DEFAULT;
    else {
        int n=(int)read(fd,b,sizeof b); close(fd);
        if(n<0) lang_src=LS_DEFAULT;                       /* unreadable (e.g. a directory) */
        else { l=(n>0&&n<(int)sizeof b)?lang_parse(b,n):-1; lang_src=l<0?LS_UNKNOWN:LS_FILE; }
    }
    lang_set(l<0?LANG_EN:l); return ui_lang;
}
/* returns 1 = the file now contains the language; 0 = nothing changed on disk (the old file is intact) */
static int lang_save(int l){
    if(l<0||l>=NLANG) return 0;
    char tmp[280],chk[8]; const char*d=LANG_CODE[l]; int n=(int)strlen(d);
    snprintf(tmp,sizeof tmp,"%s.tmp",lang_path);
    if(!write_all(tmp,d,n,0)){ unlink(tmp); return 0; }
    int fd=open(tmp,O_RDONLY); int cn=fd>=0?(int)read(fd,chk,sizeof chk):-1; if(fd>=0) close(fd);
    if(cn!=n||memcmp(chk,d,n)){ unlink(tmp); return 0; }
    if(rename(tmp,lang_path)!=0){ unlink(tmp); return 0; }
    fsync_dir(lang_path); lang_src=LS_FILE; return 1;
}

/* ====================================================================== */
/* ================= BOOT SLOT + RUNTIME SWITCH (v6.2) ================== */
/* ====================================================================== */
/* Vendor boot (artifacts/demo.sh): the flag file /customer/new selects slot B (/new_customer)
 * over slot A (/customer). The watchdog /customer/test.sh only checks whether there is a
 * process whose ps line contains "demo_v9". In the boot integration merakiplus lives as
 * /new_customer/demo_v9 (slot B); the stock UI is /customer/demo_v9 (slot A).
 *
 *   BOOT UI (persistent)  = create/remove /customer/new. Applies to the NEXT reboot. Only allowed
 *                           when /new_customer is mounted AND its demo_v9 is merakiplus — otherwise
 *                           the flag belongs to the vendor's OTA mechanism, and we do not touch it.
 *   SWITCH TO ORIGINAL NOW = runtime, without reboot (BOOT.md §9.2): merakiplus closes UART/fb/touch,
 *                           starts /customer/demo_v9 directly and exits 400 ms later.
 * merakiplus NEVER sends a signal to any process — neither kill, SIGSTOP nor SIGCONT. */
/* (BOOT_FLAG, SLOT_*_EXE, SLOT_B_MNT and SW_LOG are defined at the string table above) */
/* what(1) marker: this is how the BOOT UI guard recognises a Meraki+ binary in slot B (the "@(#)merakiplus "
 * prefix is the needle). v6.1 had no marker and is recognised by a string from its exit message. */
#define MERAKIPLUS_MARK_HEAD "@(#)merakiplus "
static const char MERAKIPLUS_MARK[]=MERAKIPLUS_MARK_HEAD MERAKIPLUS_VERSION " meraki-plus display-ui";
#define MERAKIPLUS_MARK_N ((int)sizeof MERAKIPLUS_MARK_HEAD-1)
static const char MERAKIPLUS_MARK_V61[]="== merakiplus: config.json aendret";

enum { SLOT_OTHER=0, SLOT_A, SLOT_B };
enum { BL_OK=0, BL_NOMOUNT, BL_NOBIN, BL_VENDOR };
#define STOCK_MAX 4
static struct {
    int flag;                       /* /customer/new exists */
    int b_mnt, b_bin, b_ours, lock; /* slot B: mounted / demo_v9 executable / is merakiplus / BL_* */
    int a_bin;                      /* /customer/demo_v9 exists and is executable */
    int self_pid, self_slot; char self_exe[128];
    int stock[STOCK_MAX], n_stock, stock_run;   /* /customer/demo_v9 processes; stock_run = number NOT stopped */
    int sw_pid;                     /* pidof switcher (0 = not running) */
    int sw_log; char sw_evt[100];   /* 0 = log not readable, 1 = no switch line, 2 = event */
    ms_t t;
    dev_t m_dev; ino_t m_ino; off_t m_size; time_t m_mtime; int m_ours, m_valid;   /* marker cache */
} bs={.t=-100000};

static int slot_b_mounted(void){
    FILE*f=fopen(rp("/proc/mounts"),"r"); if(!f) return 0;
    char ln[512],dev[160],mnt[160]; int hit=0;
    while(fgets(ln,sizeof ln,f)) if(sscanf(ln,"%159s %159s",dev,mnt)==2&&!strcmp(mnt,SLOT_B_MNT)) hit=1;
    fclose(f); return hit;
}
/* does the file contain the merakiplus marker? (cached on dev/ino/size/mtime — the binary is only read when it has changed) */
static int bin_is_merakiplus(const char*path){
    struct stat st; if(stat(path,&st)!=0||!S_ISREG(st.st_mode)){ bs.m_valid=0; return 0; }
    if(bs.m_valid&&st.st_dev==bs.m_dev&&st.st_ino==bs.m_ino&&st.st_size==bs.m_size&&st.st_mtime==bs.m_mtime) return bs.m_ours;
    int ours=0, fd=open(path,O_RDONLY);
    if(fd>=0){
        static char buf[65536+64]; int keep=0,n; const int ov=(int)sizeof MERAKIPLUS_MARK_V61;
        while((n=read(fd,buf+keep,65536))>0){
            int tot=keep+n;
            if(memmem(buf,tot,MERAKIPLUS_MARK,MERAKIPLUS_MARK_N)||memmem(buf,tot,MERAKIPLUS_MARK_V61,sizeof MERAKIPLUS_MARK_V61-1)){ ours=1; break; }
            keep=tot<ov?tot:ov; memmove(buf,buf+tot-keep,keep);
        }
        close(fd);
    }
    bs.m_dev=st.st_dev; bs.m_ino=st.st_ino; bs.m_size=st.st_size; bs.m_mtime=st.st_mtime; bs.m_ours=ours; bs.m_valid=1;
    return ours;
}
/* /proc/<pid>/stat: state (field 3) and start time in seconds since boot (field 22 / 100) */
static int64_t proc_stat(int pid,char*state){   /* v6.6: 64 bit — the start time is in ticks (100/s) and passes 2^31 after 248 days of uptime */
    char p[420],b[640]; snprintf(p,sizeof p,"%s/proc/%d/stat",sysroot,pid);
    int fd=open(p,O_RDONLY); if(fd<0) return -1; int n=read(fd,b,sizeof b-1); close(fd); if(n<=0) return -1; b[n]=0;
    char*q=strrchr(b,')'); if(!q) return -1; q++;
    int64_t start=-1; int idx=0;
    for(char*t=strtok(q," \n");t;t=strtok(NULL," \n"),idx++){
        if(idx==0&&state) *state=t[0];
        if(idx==19){ start=atoll(t)/100; break; }
    }
    return start;
}
static void proc_scan(void){
    bs.n_stock=0; bs.stock_run=0; bs.sw_pid=0;
    DIR*d=opendir(rp("/proc")); if(!d) return; struct dirent*de;
    while((de=readdir(d))){
        if(de->d_name[0]<'1'||de->d_name[0]>'9') continue;
        int pid=atoi(de->d_name); if(pid<=0||pid==bs.self_pid) continue;
        char p[420],l[200]; snprintf(p,sizeof p,"%s/proc/%d/exe",sysroot,pid);
        ssize_t r=readlink(p,l,sizeof l-1);
        if(r>0){ l[r]=0;
            size_t al=strlen(SLOT_A_EXE);                 /* also ".../demo_v9 (deleted)" = the binary replaced under a running process */
            if(!strncmp(l,SLOT_A_EXE,al)&&(l[al]==0||l[al]==' ')&&bs.n_stock<STOCK_MAX){ char stt='?'; proc_stat(pid,&stt); bs.stock[bs.n_stock++]=pid; if(stt!='T'&&stt!='t') bs.stock_run++; } }
        snprintf(p,sizeof p,"%s/proc/%d/comm",sysroot,pid);
        int fd=open(p,O_RDONLY);
        if(fd>=0){ int n=read(fd,l,sizeof l-1); close(fd); if(n>0){ l[n]=0; l[strcspn(l,"\n")]=0; if(!strcmp(l,"switcher")&&!bs.sw_pid) bs.sw_pid=pid; } }
    }
    closedir(d);
}
static void utf8_trim(char*s){
    int L=(int)strlen(s), i=L; while(i>0&&((unsigned char)s[i-1]&0xC0)==0x80) i--;
    if(i>0){ unsigned char c=(unsigned char)s[i-1]; int need=c>=0xF0?4:(c>=0xE0?3:(c>=0xC0?2:1)); if(L-(i-1)<need) s[i-1]=0; }
}
/* a switch event in switcher.log: "switch A→B …" / "switch FAILED …" / "GESTURE TRIGGERED …";
 * "skift …" / "GESTUS …" are the same lines from switcher builds before the English log text */
static int swlog_is_event(const char*p){
    const char*t=p; if(p[0]=='['){ const char*q=strstr(p,"] "); if(q) t=q+2; }
    return !strncmp(t,"switch ",7)||strstr(t,"GESTURE TRIGGERED")||strstr(t,"skift")||strstr(t,"GESTUS");
}
/* latest switch event in switcher.log: the last event line -> "HH:MM:SS text" */
static void swlog_scan(void){
    bs.sw_log=0; bs.sw_evt[0]=0;
    int fd=open(rp(SW_LOG),O_RDONLY); if(fd<0) return;
    static char buf[4097]; off_t sz=lseek(fd,0,SEEK_END); off_t st=sz>4096?sz-4096:0; lseek(fd,st,SEEK_SET);
    int n=read(fd,buf,4096); close(fd); if(n<0) return; buf[n]=0; bs.sw_log=1;
    char*last=NULL;
    for(char*p=buf;p&&*p;){ char*e=strchr(p,'\n'); if(e)*e=0; if((st==0||p!=buf)&&swlog_is_event(p)) last=p; p=e?e+1:NULL; }
    if(!last) return;
    /* "[YYYY-MM-DD HH:MM:SS UTC] text" */
    if(last[0]=='['&&strlen(last)>26&&last[11]==' '&&last[24]==']') snprintf(bs.sw_evt,sizeof bs.sw_evt-4,"%.8s %s",last+12,last+26);
    else snprintf(bs.sw_evt,sizeof bs.sw_evt-4,"%s",last);
    utf8_trim(bs.sw_evt);                                  /* snprintf may have cut in the middle of a character */
    bs.sw_log=2;
}
/* ================= v6.21: OTA phase 1b (docs/ota-plan.md §5) =================
 * The launcher (src/merakiplus_boot.c, installed as /new_customer/demo_v9) exec()s this UI from a release directory
 * /new_customer/merakiplus/rel/<name>/ (same PID) and passes MERAKIPLUS_REL (that directory), MERAKIPLUS_RELNAME,
 * MERAKIPLUS_TRIAL=1 (on trial), MERAKIPLUS_FALLBACK=1 (an older level runs this boot) and MERAKIPLUS_NOTE=<release> (a release
 * that failed right before this start). This block: where the UI's own files are, the healthy marker the launcher and the
 * installer read, the exec-reload into a newly installed release (ota_reload_step), and the vendor-update notice. */
#define OTA_DIR       "/new_customer/merakiplus"
#define OTA_STATE     OTA_DIR "/ota/state"
#define OTA_HEALTHY   OTA_DIR "/ota/healthy"         /* on trial only (flash): the launcher commits the release */
#define OTA_HMARK     "/tmp/merakiplus_healthy"      /* every start (tmpfs): "the start with this boot_id + PID was fine" */
#define OTA_RELOAD    "/tmp/merakiplus_reload"       /* written by the installer: exec the launcher when idle */
#define OTA_REL_PFX   OTA_DIR "/rel/"
#define OTA_LMARK     "@(#)merakiplus-launcher "
#define OTA_GUARD_KEY OTA_DIR "/boot/switcher"       /* the demo.sh guard line (tools/merakiplus.py GUARD_KEY) */
#define HEALTHY_RUN_MS  60000                        /* healthy = running >= 60 s ... */
#define HEALTHY_LINK_MS 30000                        /* ... with the controller answering for the last >= 30 s without a gap */
static struct {
    char dir[320], name[64], note[64], cur[64], last[96];
    int trial, fallback, vendor, st_ok, marked, toasted;
    ms_t t0, link0, t_reload;
} ota={.link0=-1};
/* /new_customer/merakiplus/rel/<name>/demo_v9 (also "... (deleted)" after a release directory was replaced) */
static int ota_rel_exe(const char*e){
    size_t pl=strlen(OTA_REL_PFX); if(strncmp(e,OTA_REL_PFX,pl)||e[pl]=='.'||e[pl]=='/'||!e[pl]) return 0;
    const char*s=strchr(e+pl,'/'); return s&&(!strcmp(s,"/demo_v9")||!strcmp(s,"/demo_v9 (deleted)"));
}
/* "6.22-42cd7b9d" -> "V6.22" */
static void ota_ver(const char*rel,char*o,size_t n){ size_t l=strcspn(rel,"-"); if(l>12) l=12; snprintf(o,n,"V%.*s",(int)l,rel); }
static int ota_file_has(const char*path,const char*needle){
    static char b[65536]; int fd=open(path,O_RDONLY); if(fd<0) return 0; int n=(int)read(fd,b,sizeof b-1); close(fd); if(n<0) n=0; b[n]=0;
    return memmem(b,(size_t)n,needle,strlen(needle))!=NULL;
}
static void ota_read_state(void){
    char b[2048]; ota.st_ok=0; ota.cur[0]=ota.last[0]=0;
    int fd=open(rp(OTA_STATE),O_RDONLY); if(fd<0) return; int n=(int)read(fd,b,sizeof b-1); close(fd); if(n<=0) return; b[n]=0;
    if(!strstr(b,"format=1\n")) return;
    for(char*l=b;l&&*l;){ char*e=strchr(l,'\n'); if(e)*e=0;
        if(!strncmp(l,"current=",8)) snprintf(ota.cur,sizeof ota.cur,"%s",l+8);
        else if(!strncmp(l,"last=",5)) snprintf(ota.last,sizeof ota.last,"%s",l+5);
        l=e?e+1:NULL; }
    ota.st_ok=1;
}
/* docs/ota-plan.md §7 layer 3: a vendor update is waiting for the next boot, or replaced demo.sh (our guard line is gone) */
static void ota_vendor_scan(void){
    ota.vendor=access(rp("/customer/temp.tar.gz"),F_OK)==0||access(rp("/customer/demo_temp"),F_OK)==0||
               (access(rp(OTA_GUARD_KEY),F_OK)==0&&!ota_file_has(rp("/customer/demo.sh"),OTA_GUARD_KEY));
}
static void ota_init(ms_t now){
    const char*e; memset(&ota,0,sizeof ota); ota.t0=now; ota.link0=-1; ota.t_reload=now;
    if((e=getenv("MERAKIPLUS_RELNAME"))) snprintf(ota.name,sizeof ota.name,"%s",e);
    if((e=getenv("MERAKIPLUS_NOTE"))) snprintf(ota.note,sizeof ota.note,"%s",e);
    ota.trial=(e=getenv("MERAKIPLUS_TRIAL"))&&!strcmp(e,"1");
    ota.fallback=(e=getenv("MERAKIPLUS_FALLBACK"))&&!strcmp(e,"1");
    if((e=getenv("MERAKIPLUS_REL"))&&*e) snprintf(ota.dir,sizeof ota.dir,"%s",e);
    else {                                          /* started without the launcher: our own directory, if it holds the sound helper */
        char x[300]; ssize_t r=readlink(rp("/proc/self/exe"),x,sizeof x-1);
        if(r>0){ x[r]=0; char*s=strrchr(x,'/'); if(s){ *s=0; char h[340]; snprintf(h,sizeof h,"%s/merakiplus_snd",x); if(access(h,X_OK)==0) snprintf(ota.dir,sizeof ota.dir,"%s",x); } }
    }
    if(!ota.dir[0]) snprintf(ota.dir,sizeof ota.dir,"%s",OTA_DIR);   /* the v6.18..v6.20 layout (sound files in the install dir) */
    ota_read_state(); ota_vendor_scan();
}
/* the marker: boot_id + PID + release. clean=1: a deliberate exit (SWITCH TO ORIGINAL, reload, SIGTERM) — not a failed start */
static int ota_mark_write(int clean){
    char b[240],bid[64],t[420]; boot_id_read(bid,sizeof bid);
    int n=snprintf(b,sizeof b,"boot=%s\npid=%d\nrel=%s\nup=%lld\nstate=%s\n",bid[0]?bid:"?",(int)getpid(),ota.name,(long long)mono_s(),clean?"exit":"healthy");
    const char*p=rp(OTA_HMARK); snprintf(t,sizeof t,"%s.tmp",p);
    int ok=write_all(t,b,n,0)&&rename(t,p)==0;
    if(ota.trial&&ota.name[0]&&!clean){             /* flash: tmp + fsync (write_all) + rename + fsync(dir) */
        p=rp(OTA_HEALTHY); snprintf(t,sizeof t,"%s.tmp",p);
        if(write_all(t,b,n,0)&&rename(t,p)==0) fsync_dir(p); else ok=0;
    }
    return ok;
}
static void boot_refresh(void){
    struct stat st;
    if(!bs.self_pid) bs.self_pid=(int)getpid();
    bs.flag=stat(rp(BOOT_FLAG),&st)==0;
    bs.b_mnt=slot_b_mounted();
    bs.b_bin=bs.b_mnt&&access(rp(SLOT_B_EXE),X_OK)==0;
    bs.b_ours=bs.b_bin&&bin_is_merakiplus(rp(SLOT_B_EXE));
    bs.lock=!bs.b_mnt?BL_NOMOUNT:(!bs.b_bin?BL_NOBIN:(!bs.b_ours?BL_VENDOR:BL_OK));
    bs.a_bin=access(rp(SLOT_A_EXE),X_OK)==0;
    ssize_t r=readlink(rp("/proc/self/exe"),bs.self_exe,sizeof bs.self_exe-1); bs.self_exe[r>0?r:0]=0;
    bs.self_slot=(!strcmp(bs.self_exe,SLOT_B_EXE)||ota_rel_exe(bs.self_exe))?SLOT_B:(!strcmp(bs.self_exe,SLOT_A_EXE)?SLOT_A:SLOT_OTHER);   /* v6.21: a release started by the launcher is slot B */
    proc_scan(); swlog_scan();
}
static void boot_log(const char*fmt,...){
    char p[340]; mkdir(cfg_bakdir,0755); snprintf(p,sizeof p,"%s/boot_ui.log",cfg_bakdir);
    FILE*f=fopen(p,"a"); if(!f) return;
    time_t t=time(NULL); struct tm tmv; gmtime_r(&t,&tmv); char ts[32]; strftime(ts,sizeof ts,"%Y-%m-%dT%H:%M:%SZ",&tmv);
    fprintf(f,"%s ",ts); va_list ap; va_start(ap,fmt); vfprintf(f,fmt,ap); va_end(ap); fputc('\n',f);
    fflush(f); fsync(fileno(f)); fclose(f);
}
/* BOOT UI: set (want_b=1, touch) or remove (want_b=0, rm) the flag. Idempotent. The guard
 * applies to BOTH directions. Returns 1 = the flag is as requested; 0 = nothing changed (boot_err set). */
static char boot_err[72]="";
static int boot_flag_set(int want_b){
    boot_refresh(); boot_err[0]=0;
    if(bs.lock!=BL_OK){ snprintf(boot_err,sizeof boot_err,"locked (%s)",bs.lock==BL_NOMOUNT?"/new_customer not mounted":(bs.lock==BL_NOBIN?"demo_v9 missing in slot B":"slot B is not merakiplus")); return 0; }
    int before=bs.flag; const char*p=rp(BOOT_FLAG); struct stat st;
    if(want_b){ int fd=open(p,O_WRONLY|O_CREAT,0644); if(fd<0){ snprintf(boot_err,sizeof boot_err,"touch: %s",strerror(errno)); return 0; } fsync(fd); close(fd); }
    else if(unlink(p)!=0&&errno!=ENOENT){ snprintf(boot_err,sizeof boot_err,"rm: %s",strerror(errno)); return 0; }
    fsync_dir(p); SYS_SYNC                                 /* BOOT.md §9.1: open(O_CREAT)/unlink + sync() (SIM: no global sync) */
    bs.flag=stat(p,&st)==0;
    if(bs.flag!=(want_b!=0)){ snprintf(boot_err,sizeof boot_err,"verification failed"); return 0; }
    if(before!=bs.flag) boot_log("BOOT-UI: flag %s %s (%d->%d): %s at next reboot",BOOT_FLAG,bs.flag?"created":"removed",before,bs.flag,bs.flag?"MERAKIPLUS (slot B)":"ORIGINAL (slot A)");
    return 1;
}
/* is a stock UI with an old RAM mirror waiting? True when RESTART_NEEDED exists AND a
 * /customer/demo_v9 is running that was started BEFORE the latest SAVE (same boot). A freshly started
 * stock UI has read config.json itself — then there is nothing to remind about. */
static int restart_pending(void){
    char p[340],b[1024]; snprintf(p,sizeof p,"%s/RESTART_NEEDED",cfg_bakdir);
    int fd=open(p,O_RDONLY); if(fd<0) return 0; int n=read(fd,b,sizeof b-1); close(fd); if(n<0)n=0; b[n]=0;
    if(bs.n_stock==0) return 0;
    char*q=strstr(b,"boot_id="),*m=strstr(b,"mono_s="); char was[64]="",cur[64];
    if(!q||!m||sscanf(q+8,"%63s",was)!=1||!strcmp(was,"?")) return 1;       /* v6.1 flag without a stamp: assume it applies */
    boot_id_read(cur,sizeof cur); if(!cur[0]) return 1;
    if(strcmp(cur,was)) return 0;                                           /* the machine has rebooted since the SAVE */
    int64_t t=atoll(m+7);
    for(int i=0;i<bs.n_stock;i++){ int64_t s=proc_stat(bs.stock[i],NULL); if(s<0||s<=t) return 1; }
    return 0;
}

/* ---- runtime switch: LD_LIBRARY_PATH for slot A ----
 * BOOT.md §9.2 specifies "/customer/libs:/config/wifi". demo.sh's A branch however sets
 * "$LD_LIBRARY_PATH:/customer/libs:/config/wifi" — i.e. the boot environment's prefix is kept. We do
 * the same: inherit our own path, remove the slot components (B's and A's libs + /config/wifi)
 * and append A's. Without a prefix (e.g. when the switcher has started us) the result is EXACTLY
 * "/customer/libs:/config/wifi"; with a prefix it is what a real A boot would give the stock UI. */
static void ld_path_for_a(const char*inh,char*out,size_t n){
    size_t o=0; out[0]=0;
    static const char*const drop[]={"/new_customer/libs","/customer/libs","/config/wifi"};
    for(const char*p=inh;p&&*p;){
        const char*e=strchr(p,':'); size_t l=e?(size_t)(e-p):strlen(p); int skip=(l==0);
        for(unsigned k=0;k<3&&!skip;k++) if(l==strlen(drop[k])&&!memcmp(p,drop[k],l)) skip=1;
        for(size_t i=0;i<l&&!skip;i++){ unsigned char c=(unsigned char)p[i]; if(c=='\''||c<0x21||c==0x7F) skip=1; }   /* must be able to sit inside '...' */
        if(!skip&&o+l+1+32<n){ memcpy(out+o,p,l); o+=l; out[o++]=':'; out[o]=0; }
        p=e?e+1:NULL;
    }
    snprintf(out+o,n-o,"/customer/libs:/config/wifi");
}
/* ---- runtime switch B->A: BOOT.md §9.2 steps 2 + 3 ----
 * Called AFTER merakiplus has closed UART/fb/touch (step 1, see do_switch). The child: own session
 * (setsid), cwd /customer, slot A's LD_LIBRARY_PATH, stdio -> /dev/null and ALL other fds
 * closed (an inherited touch fd would keep EVIOCGRAB alive inside the stock UI), then
 * execl("/customer/demo_v9","./demo_v9") — argv0 contains "demo_v9", so the watchdog grep
 * matches. The parent waits 400 ms while A initialises fb/UART; if the child has already died,
 * an error is reported, and merakiplus takes the screen back. Returns: stock pid (>0) or 0 w. err set. */
#define SW_WAIT_MS 400
enum { SWM_LAUNCH=0, SWM_CONT };
static int sw_dry=0;                                       /* MERAKIPLUS_SWITCH_DRY=1: log only */
static char sw_ldp[512]="";                                /* LD_LIBRARY_PATH given to stock (log/selftest) */
static int switch_mode(void){ return bs.n_stock>0?SWM_CONT:SWM_LAUNCH; }   /* SWM_CONT = run.sh mode: stock exists (paused) */
static int switch_locked(void){ return bs.n_stock==0&&!bs.a_bin; }         /* no stock UI to switch to */
/* what demo.sh starts at the next boot, given the flag + slot B's state. The flag alone is not
 * enough: without a mount slot A starts, and with a vendor binary in slot B it is NOT merakiplus. */
enum { BN_ORIGINAL=0, BN_OURS, BN_SLOTB };
_Static_assert(TX_BN_OURS==TX_BN_ORIGINAL+BN_OURS&&TX_BN_SLOTB==TX_BN_ORIGINAL+BN_SLOTB,"TX_BN_* must follow BN_*");
static int boot_next_kind(void){
    if(!bs.flag||bs.lock==BL_NOMOUNT) return BN_ORIGINAL;
    return bs.lock==BL_OK?BN_OURS:BN_SLOTB;
}
static const char* boot_next_name(void){ return TR(TX_BN_ORIGINAL+boot_next_kind()); }
enum { SWF_NONE=0, SWF_NOBIN, SWF_FORK, SWF_DIED };
static int sw_fail=SWF_NONE;                               /* why the latest launch failed (-> localised toast; err goes to the log) */
static int switch_launch(char*err,size_t en){
    char exe[420],dir[420]; snprintf(exe,sizeof exe,"%s",rp(SLOT_A_EXE)); snprintf(dir,sizeof dir,"%s",rp("/customer"));
    ld_path_for_a(getenv("LD_LIBRARY_PATH"),sw_ldp,sizeof sw_ldp); sw_fail=SWF_NONE;
    if(access(exe,X_OK)!=0){ sw_fail=SWF_NOBIN; snprintf(err,en,"%s missing",SLOT_A_EXE); return 0; }
    fflush(stdout); fflush(stderr);
    pid_t c=fork(); if(c<0){ sw_fail=SWF_FORK; snprintf(err,en,"fork: %s",strerror(errno)); return 0; }
    if(c==0){
        setsid();
        if(chdir(dir)!=0) _exit(126);
        setenv("LD_LIBRARY_PATH",sw_ldp,1);
        int nul=open("/dev/null",O_RDWR); if(nul>=0){ dup2(nul,0); dup2(nul,1); dup2(nul,2); }
        for(int fd=3;fd<1024;fd++) close(fd);
        execl(exe,"./demo_v9",(char*)NULL);
        _exit(127);
    }
    for(ms_t t0=now_ms();now_ms()-t0<SW_WAIT_MS;){          /* clock-driven: 400 ms regardless of timer resolution */
        struct timespec sl={0,10*1000*1000}; nanosleep(&sl,NULL);
        int st; if(waitpid(c,&st,WNOHANG)==c){ sw_fail=SWF_DIED; snprintf(err,en,"stock UI exited immediately (rc %d)",WIFEXITED(st)?WEXITSTATUS(st):-1); return 0; }
    }
    return (int)c;
}

/* ================= settings model =================
 * ONE table = the single source of truth for label/field/word/limits. The controller has NO
 * range checks (PARITY §0.10) -> min/max/step are enforced here and in frame_policy().
 * All limits are reconciled against PARITY.md §2 / parity_spec.json (raw config units);
 * parity_check.py compares the table with the spec automatically (--dump-table).
 *   ptier = the spec's tier (1 core / 2 comfort / 3 system); tier = merakiplus page.
 *   SF_EST: min+step are proven, but max is an ESTIMATE (the stock picker's option count is
 *           not mapped, PARITY §6.6). The UI shows a visible hint, and a value that
 *           the stock UI itself has saved (<= hard) widens the interval.
 *   SF_LOCK: shown, but has no editor (reason in note).
 *   SF_SENSOR: read-only measured value from reg[def] — no config key, no write.
 *   SF_LOCAL (v6.3): merakiplus's OWN setting (UI language) — no config key, no MCU word,
 *           not a spec parameter (left out of --dump-table); saved in its own file.
 *   SF_OWN (v6.16): merakiplus's OWN time settings (pre-heat schedule + auto off) — like SF_LOCAL, saved in
 *           /new_customer/merakiplus/schedule (sched_save); SF_HHMM: the value is minutes after midnight, shown as a clock time.
 * label/unit/note/hint and enum names are string ids (TX_*), never text — see STR_TABLE. */
enum { K_NUM=0, K_ENUM };
enum { M_NONE=0, M_W4, M_W23, M_W11, M_W112, M_W113, M_W114, M_W25, M_W14, M_W17, M_W2 };
#define SF_TEMP10 1   /* value in 0.1 °C */
#define SF_TEMP1  2   /* value in whole °C */
#define SF_LOCK   4
#define SF_BL     8   /* live preview: backlight */
#define SF_EST    16
#define SF_SENSOR 32
#define SF_LOCAL  64
#define SF_OWN    128 /* v6.16: own file (schedule) */
#define SF_HHMM   256 /* v6.16: minutes after midnight, shown as hh:mm (12/24 h per timemode) */
#define SF_SND    512 /* v6.18: own file (sound) */
typedef struct {
    const char *key; short label, unit;
    int tier, ptier, min, max, step, dec, kind;
    const short *opts;
    int mcu, flags, def, hard;
    short note, hint;             /* hint 0 (TX_NONE) = no hint */
    int val, loaded, lo, hi;      /* runtime: value, loaded?, effective interval */
} Setting;
static const short OPT_OFFON[]={TX_E_OFF,TX_E_ON};
static const short OPT_GRINDM[]={TX_E_WEIGHT,TX_E_TIME};                /* 1=weight auto, 2=time [PARITY #12] */
static const short OPT_HOTM[]={TX_E_AUTO,TX_E_MANUAL};                  /* 1=auto, 2=manual     [PARITY #13] */
static const short OPT_GEAR[]={TX_E_GENTLE,TX_E_MILD,TX_E_MEDIUM,TX_E_STRONG};   /* Gentle/Mild/Medium/Strong = 1..4 [0xdd028] */
/* v6.16: FIXED (inverted since v6.1). Stock update_time 0xD5944: r3 = model[0x59] ^ 1 (0xD5970); r3 == 0 (timemode 1) -> "%02d:%02d"
 * (0x165D94), else (timemode 0) -> "%02d:%02d A.M./P.M." (0x165D84/0x165D74). model_data_init 0xCEF94: model[0x59] = timemode != 0;
 * the stock switch "24-Hour Format" (0x171F60) is ON when model[0x59] = 1 (onClick 0xFCD60 toggles it, setTimeMode 0xFCDEC). */
static const short OPT_TIME[]={TX_E_12H,TX_E_24H};                      /* 0 = 12-hour (A.M./P.M.), 1 = 24-hour */
static const short OPT_REPEAT[]={TX_E_ONCE,TX_E_DAILY,TX_E_WORKDAYS,TX_E_WEEKEND};   /* v6.16: stock repeat Never / Everyday / Workday / Weekend */
static const short OPT_AUTOOFF[]={TX_E_OFF,TX_E_MIN15,TX_E_MIN20,TX_E_MIN30,TX_E_MIN60};   /* v6.16: stock "%dmin" table 0x1A02D0 = 15/20/30/60 */
static const short OPT_TUNIT[]={TX_U_F,TX_U_C};                         /* 0=°F, 1=°C           [PARITY #17] */
static const short OPT_LANG[]={TX_LG_EN,TX_LG_DE,TX_LG_FR,TX_LG_IT,TX_LG_ES,TX_LG_PL,TX_LG_AR,TX_LG_ZHS,TX_LG_ZHT,TX_LG_KO,TX_LG_JA};   /* stock: index 0..10 [strings @0x166d84] */
static const short OPT_UILANG[]={TX_UL_EN,TX_UL_DA};
static const short OPT_SOUND[]={TX_E_OFF,TX_E_ON,TX_E_SND_STOCK};                  /* v6.18: 0 off, 1 Meraki+ tones, 2 the stock UI's own mp3 files (if present) */
static const short OPT_SNDRDY[]={TX_E_OFF,TX_E_RDY_BREW,TX_E_RDY_STEAM,TX_E_RDY_BOTH};   /* v6.18: bit 0 = brew (extract) boiler, bit 1 = steam boiler */                    /* v6.3: = LANG_EN, LANG_DA */
_Static_assert(TX_UL_DA==TX_UL_EN+LANG_DA,"TX_UL_* must follow LANG_*");
enum { S_GRINDW=0, S_GRINDMODE, S_BREWW, S_BREWT, S_PISW, S_PITIME, S_PIPRESS, S_BREAK, S_HEADT,
       S_STEAMT, S_STEAMG, S_HOTW, S_HOTMODE, S_HOTT,
       S_UILANG, S_BL, S_VOL, S_TUNIT, S_LANG, S_HOTSC, S_DST, S_CLEARTIP, S_TIMEMODE, S_AUTOTIME, S_TZ,
       S_PREHEAT, S_PHAT, S_PHREP, S_AUTOOFF, S_SOUND, S_SNDRDY, NSET };   /* v6.16: + the time rows (merakiplus's own file); v6.18: + sound (own file) */
static Setting SET[NSET]={
 /*              key                   label            unit    page,spec-tier  min   max step dec kind   opts        mcu    flags                    def hard  note, hint */
 /* v6.17: page = the list page the row belongs to (0 GRIND, 1 BREW, 2 WATER, 3 STEAM, 4 SYSTEM); the row ORDER on a page and the
  * SYSTEM sections are in FEAT_ROWS / SEC_ROWS (the row model). Before v6.17: 0 BREW, 1 STEAM · WATER, 2 SYSTEM (the spec's tiers). */
 /* ---- GRIND + BREW (spec tier 1) ---- */
 [S_GRINDW]   ={"autoGrindWeight",    TX_SL_GRINDW,    TX_U_G,   0,1,   10, 309,  1, 1,K_NUM, 0,         M_W4,  0,                      180, 309,TX_SN_GRINDW,0},
 [S_GRINDMODE]={"grindMode",          TX_SL_GRINDMODE, TX_NONE,  0,1,    1,   2,  1, 0,K_ENUM,OPT_GRINDM,M_W2,  0,                        1,   2,TX_SN_GRINDMODE,0},
 [S_BREWW]    ={"autobrewWeight",     TX_SL_BREWW,     TX_U_G,   1,1,  100, 990, 10,-1,K_NUM, 0,         M_W23, 0,                      360, 990,TX_SN_BREWW,0},
 [S_BREWT]    ={"brewTemper",         TX_SL_BREWT,     TX_U_C,   1,1,  850, 960, 10, 1,K_NUM, 0,         M_W11, SF_TEMP10,              870, 960,TX_SN_BREWT,0},
 [S_PISW]     ={"preInfusionSwitch",  TX_SL_PISW,      TX_NONE,  1,1,    0,   1,  1, 0,K_ENUM,OPT_OFFON, M_W112,0,                        0,   1,TX_SN_PISW,0},
 [S_PITIME]   ={"preInfusionTime",    TX_SL_PITIME,    TX_U_S,   1,1,    5,  15,  1, 0,K_NUM, 0,         M_W113,SF_EST,                  15,  60,TX_SN_PITIME,TX_SH_EST},
 [S_PIPRESS]  ={"preInfusionPressure",TX_SL_PIPRESS,   TX_U_BAR, 1,1,    3,   4,  1, 0,K_NUM, 0,         M_W112,0,                        4,   4,TX_SN_PIPRESS,0},
 [S_BREAK]    ={"breakTime",          TX_SL_BREAK,     TX_U_S,   4,1,    0, 255,  1, 0,K_NUM, 0,         M_W113,SF_LOCK,                  5, 255,TX_SN_BREAK,0},
 [S_HEADT]    ={"headTemper",         TX_SL_HEADT,     TX_U_C,   1,1,   30,  80, 10, 0,K_NUM, 0,         M_W114,SF_TEMP1|SF_EST,         50, 100,TX_SN_HEADT,TX_SH_EST},
 /* ---- STEAM + WATER (spec tier 1); the WATER SENSOR row is not listed: the WATER page shows it as its live line ---- */
 [S_STEAMT]   ={"AtuoSteamTarge",     TX_SL_STEAMT,    TX_U_C,   3,1,  450, 700, 10, 1,K_NUM, 0,         M_W25, SF_TEMP10,              550, 700,TX_SN_STEAMT,0},
 [S_STEAMG]   ={"steamGears",         TX_SL_STEAMG,    TX_NONE,  3,1,    1,   4,  1, 0,K_ENUM,OPT_GEAR,  M_W14, 0,                        4,   4,TX_SN_STEAMG,0},
 [S_HOTW]     ={"autoHotWaterTarget", TX_SL_HOTW,      TX_U_G,   2,1,  300,5000,100,-1,K_NUM, 0,         M_W17, 0,                     3600,5000,TX_SN_HOTW,0},
 [S_HOTMODE]  ={"hotWaterMode",       TX_SL_HOTMODE,   TX_NONE,  2,1,    1,   2,  1, 0,K_ENUM,OPT_HOTM,  M_NONE,0,                        1,   2,TX_SN_HOTMODE,0},
 [S_HOTT]     ={0,                    TX_SL_HOTT,      TX_U_C,   2,1,  200,1200,  1, 1,K_NUM, 0,         M_NONE,SF_SENSOR|SF_LOCK|SF_TEMP10,40,  0,TX_SN_HOTT,0},
 /* ---- SYSTEM: first merakiplus's own UI language (v6.3; not a spec parameter), then
  *      spec tier 2 and tier 3 — all config only, no MCU write ---- */
 [S_UILANG]   ={0,                    TX_SL_UILANG,    TX_NONE,  4,0,    0,   1,  1, 0,K_ENUM,OPT_UILANG,M_NONE,SF_LOCAL,                 0,   1,TX_SN_UILANG,0},
 [S_BL]       ={"backlight",          TX_SL_BL,        TX_NONE,  4,2,    1,  10,  1, 0,K_NUM, 0,         M_NONE,SF_BL,                    8,  10,TX_SN_BL,0},
 [S_VOL]      ={"volume",             TX_SL_VOL,       TX_NONE,  4,2,    0, 100,  1, 0,K_NUM, 0,         M_NONE,0,                       40, 100,TX_SN_VOL,TX_SH_VOL},
 [S_TUNIT]    ={"Temperature",        TX_SL_TUNIT,     TX_NONE,  4,2,    0,   1,  1, 0,K_ENUM,OPT_TUNIT, M_NONE,0,                        0,   1,TX_SN_TUNIT,0},
 [S_LANG]     ={"language",           TX_SL_LANG,      TX_NONE,  4,2,    0,  10,  1, 0,K_ENUM,OPT_LANG,  M_NONE,0,                        0,  10,TX_SN_LANG,0},
 [S_HOTSC]    ={"hotWaterShortcut",   TX_SL_HOTSC,     TX_NONE,  4,2,    0,   1,  1, 0,K_ENUM,OPT_OFFON, M_NONE,0,                        0,   1,TX_SN_HOTSC,0},
 [S_DST]      ={"sumtime",            TX_SL_DST,       TX_NONE,  4,2,    0,   1,  1, 0,K_ENUM,OPT_OFFON, M_NONE,0,                        0,   1,TX_SN_TIMEFMT,0},
 [S_CLEARTIP] ={"clearTip",           TX_SL_CLEARTIP,  TX_NONE,  4,2,    0,   1,  1, 0,K_ENUM,OPT_OFFON, M_NONE,0,                        0,   1,TX_SN_CLEARTIP,0},
 [S_TIMEMODE] ={"timemode",           TX_SL_TIMEMODE,  TX_NONE,  4,3,    0,   1,  1, 0,K_ENUM,OPT_TIME,  M_NONE,0,                        1,   1,TX_SN_TIMEFMT,0},
 [S_AUTOTIME] ={"Automatically",      TX_SL_AUTOTIME,  TX_NONE,  4,3,    0,   1,  1, 0,K_ENUM,OPT_OFFON, M_NONE,0,                        1,   1,TX_SN_AUTOTIME,0},
 [S_TZ]       ={"timeZone",           TX_SL_TZ,        TX_NONE,  4,3,    0,   0,  1, 0,K_NUM, 0,         M_NONE,SF_LOCK,                 44,   0,TX_SN_TZ,0},
 /* ---- v6.16: pre-heat schedule + auto off — merakiplus's OWN file (/new_customer/merakiplus/schedule), not config.json, no MCU word ---- */
 [S_PREHEAT]  ={0,                    TX_SL_PREHEAT,   TX_NONE,  4,0,    0,   1,  1, 0,K_ENUM,OPT_OFFON, M_NONE,SF_OWN,                   0,   1,TX_SN_PREHEAT,0},
 [S_PHAT]     ={0,                    TX_SL_PHAT,      TX_NONE,  4,0,    0,1435,  5, 0,K_NUM, 0,         M_NONE,SF_OWN|SF_HHMM,         390,1435,TX_SN_PHAT,0},
 [S_PHREP]    ={0,                    TX_SL_PHREP,     TX_NONE,  4,0,    0,   3,  1, 0,K_ENUM,OPT_REPEAT,M_NONE,SF_OWN,                   1,   3,TX_SN_PHREP,0},
 [S_AUTOOFF]  ={0,                    TX_SL_AUTOOFF,   TX_NONE,  4,0,    0,   4,  1, 0,K_ENUM,OPT_AUTOOFF,M_NONE,SF_OWN,                  0,   4,TX_SN_AUTOOFF,0},
 /* ---- v6.18: sound — merakiplus's OWN file (/new_customer/merakiplus/sound), not config.json, no MCU word. The volume is the stock
  *      config "volume" (S_VOL, shared with the stock UI, which caps it at 70 like merakiplus) ---- */
 [S_SOUND]    ={0,                    TX_SL_SOUND,     TX_NONE,  4,0,    0,   2,  1, 0,K_ENUM,OPT_SOUND, M_NONE,SF_SND,                   1,   2,TX_SN_SOUND,0},
 [S_SNDRDY]   ={0,                    TX_SL_SNDRDY,    TX_NONE,  4,0,    0,   3,  1, 0,K_ENUM,OPT_SNDRDY,M_NONE,SF_SND,                   3,   3,TX_SN_SNDRDY,0},
};
static const short TIER_NAME[5]={TX_A_GRIND,TX_A_BREW,TX_A_WATER,TX_A_STEAM,TX_PG_SYSTEM};   /* v6.17: the list pages' titles (page = SET[].tier) */
static int sv(int i){ return SET[i].val; }
/* v6.10: pressure estimate (the constants and caveats are at P_ANCHOR_RAW at the top). The anchor pressure is config preInfusionPressure,
 * so a changed config changes the scale; outside 1..15 bar (or no config: default 4) P_ANCHOR_BAR_DEF is used. */
static int p_anchor_bar(void){ int v=sv(S_PIPRESS); return (v>=1&&v<=15)?v:P_ANCHOR_BAR_DEF; }
static float p_k(void){ return (float)p_anchor_bar()/(float)P_ANCHOR_RAW; }
/* raw word 51 -> bar estimate; -1 = above P_EST_MAX: a deadhead/maintenance pulse, not a physical pressure — never shown as a number */
static float p_bar(float raw){ float b=raw*p_k(); return b>P_EST_MAX?-1.f:(b<0.f?0.f:b); }
static int clampi(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }
static int set_valid(const Setting*s,int v){ return v>=s->lo && v<=s->hi; }
/* loaded value outside the spec interval (only possible if config was changed behind the stock UI's back) */
static int set_oor(const Setting*s){ return s->loaded && !(s->flags&SF_SENSOR) && !set_valid(s,s->val); }

/* ================= v6.16: TIME & SCHEDULING (docs/time-scheduling.md + UI_v6.md "CHANGES-v6.16") =================
 * Reversed from the stock UI demo_v9 (symbols kept) and the MCU firmware:
 *  - TIME SOURCE: the SoC system clock, kept in UTC. Stock sets it only via an HTTP query (time_sync_thread 0x13FDEC, gated by
 *    config "Automatically") -> biz_set_system_time 0x1359B0 = `date -s <UTC>` + `hwclock -w`; no NTP, nothing from the MCU.
 *    merakiplus does NOT sync the clock (no network calls): it reads the same SoC clock. Before 2025 = "not set" -> "--:--", no alarm.
 *  - LOCAL TIME: _refresh_systime 0x135548 = UTC + the time zone table entry 0x1A5890[timeZone].offset (136 entries x 12 bytes:
 *    offset s, "UTC±hh:mm", name; copied below verbatim, incl. two stock quirks: #30 Newfoundland -2:30 under the label -03:30
 *    and #135 Kiritimati +14:00 under the label +13:00) + 3600 if sumtime and is_dst 0x1354D4. Stock's DST window is date-only,
 *    comes from the HTTP answer (model+0x15B8..0x15CC, RAM only) and is never computed offline; merakiplus computes the window from the
 *    region's rule (the rules stock's calc_dst_dates_for_timezone 0x13BB14 uses: EU last Sun Mar -> last Sun Oct at 01:00 UTC;
 *    US/CA 2nd Sun Mar -> 1st Sun Nov; Levant last Fri Mar -> last Fri Oct; AU 1st Sun Oct -> 1st Sun Apr; NZ last Sun Sep ->
 *    1st Sun Apr; others none) — config "sumtime" stays the user's on/off switch (deliberate, documented deviation).
 *  - DISPLAY: update_time 0xD5944 — timemode 1 = "%02d:%02d", timemode 0 = "%02d:%02d A.M./P.M." with 0 -> 12 A.M., 12 -> 12 P.M.,
 *    13..23 -> 1..11 P.M. (0xD5990..0xD5A6C).
 *  - SCHEDULED PRE-HEAT: stock keeps up to 30 entries of 20 bytes at model+0x74 (saved in the binary /customer/config.bin:
 *    data_save 0xD091C copies model+0x70, 0xF78 bytes): [0] exists, [1] enabled, [2] hour, [3] minute, [4..10] Sun..Sat repeat
 *    flags (judgeWeek 0x14955C / judgeWeekEqual 0x1495E0; all 0 = one-shot). Nothing is sent while the machine is on: only
 *    powerOff 0x149074 -> performSleepActions 0x148324 computes the NEXT enabled occurrence (minute resolution, strictly after
 *    now: today if the weekday matches and hh:mm > now, else the next matching weekday, one-shot -> today or tomorrow) and sends it
 *    as the cmd8 alarm via powerOffSend 0x14700C(mday of the target day, hour, minute) — day wrapped with biz_days_in_month
 *    0x135AE8; no entry -> 0/0/0. A one-shot entry is remembered (model+0x1758..0x176C) and switched off at the next start once
 *    its time has passed (deal_preheat_data 0xCF49C).
 *  - AUTO OFF: SleepRefresh 0x149268 counts a 1800 s idle counter down once per second (reset by every touch, touch_feedback_cb ->
 *    sleepTimeUpfdate 0x14818C, and every second while isExtractionOrSteamSwitchOn); at 0 updateSleepState 0x14827C calls powerOff
 *    (the same alarm frame as above). config sleepMode/sleepModeSetting are loaded (model+0x54/0x55) but never read in this
 *    firmware; the setting's own table (0x1A02D0) is 15/20/30/60 min.
 *  - MCU (MEM03): cmd8 handler 0x9C88 copies the 10 bytes to 0x20000E18, echoes 0x88 and sets its RTC from them (0x9DCE -> 0x7494);
 *    the sleep task 0x2F52 compares RTC day/hour/minute with the alarm once per second and, if day+hour+minute != 0 and they match,
 *    resets the CPU (0x326) = wake: word 0 starts as 1 (heating), and on this hardware the display gets power again = cold boot.
 * merakiplus: the clock on HOME + DIAGNOSE (SoC UTC + zone + DST, 12/24 h per timemode), a pre-heat schedule (one entry: on/off, time,
 * once / every day / Mon-Fri / Sat-Sun) and auto off (off / 15 / 20 / 30 / 60 min) in merakiplus's own file; the alarm goes out with
 * the power-off frame exactly as stock (pw_off_begin), and — a documented addition — is PRE-ARMED with the same frame while the
 * machine is on, so a power-off merakiplus does not see (the button's kernel cut, §19) still wakes it. Never while off/turning off. */
enum { DR_NONE=0, DR_US, DR_EU, DR_LEV, DR_AU, DR_NZ };
static void idle_kick(ms_t now);     /* auto off: every touch/button/SAVE restarts the idle timer (sched_step) */
#define TZ_N 136
static const struct { int off; const char*utc,*name; unsigned char rule; } TZT[TZ_N]={   /* stock 0x1A5890 (offset, label, name) + DST rule */
 {-43200,"UTC-12:00","DATELINE STANDARD TIME",DR_US},  /* 0 */
 {-39600,"UTC-11:00","UTC-11",DR_US},  /* 1 */
 {-36000,"UTC-10:00","DATELINE STANDARD TIME",DR_US},  /* 2 */
 {-36000,"UTC-11:00","UTC-10:00",DR_US},  /* 3 */
 {-34200," UTC-09:30","MARQUESAS ISLANDS",DR_US},  /* 4 */
 {-32400,"UTC-09:00","ALASKA",DR_US},  /* 5 */
 {-32400,"UTC-09:00","COORDINATED UNIVERSAL TIME-09",DR_US},  /* 6 */
 {-28800,"UTC-08:00","BAJA CALIFORNIA",DR_US},  /* 7 */
 {-28800,"UTC-08:00","COORDINATED UNIVERSAL TIME-08",DR_US},  /* 8 */
 {-28800,"UTC-08:00","PACIFIC TIME (US + CANADA)",DR_US},  /* 9 */
 {-25200,"UTC-07:00","ARIZONA",DR_US},  /* 10 */
 {-25200,"UTC-07:00","CHIHUAHUA, LA PAZ, MAZATLAN",DR_US},  /* 11 */
 {-21600,"UTC-06:00","CENTRAL AMERICA",DR_US},  /* 12 */
 {-21600,"UTC-06:00","CENTRAL TIME (US + CANADA)",DR_US},  /* 13 */
 {-21600,"UTC-06:00","EASTER ISLAND",DR_US},  /* 14 */
 {-21600,"UTC-06:00","GUADALAJARA, MEXICO CITY, MONTERREY",DR_US},  /* 15 */
 {-21600,"UTC-06:00","SASKATCHEWAN",DR_US},  /* 16 */
 {-18000,"UTC-05:00","BOGOTA, LIMA, QUITO, RIO BRANCO",DR_US},  /* 17 */
 {-18000,"UTC-05:00","CHETUMAL",DR_US},  /* 18 */
 {-18000,"UTC-05:00","EASTERN TIME (US + CANADA)",DR_US},  /* 19 */
 {-18000,"UTC-05:00","HAITI",DR_US},  /* 20 */
 {-18000,"UTC-05:00","HAVANA",DR_US},  /* 21 */
 {-18000,"UTC-05:00","INDIANA (EAST)",DR_US},  /* 22 */
 {-18000,"UTC-05:00","TURKS AND CAICOS",DR_US},  /* 23 */
 {-14400,"UTC-04:00","ASUNCION",DR_US},  /* 24 */
 {-14400,"UTC-04:00","ATLANTIC TIME (CANADA)",DR_US},  /* 25 */
 {-14400,"UTC-04:00","CARACAS",DR_US},  /* 26 */
 {-14400,"UTC-04:00","CUIABA",DR_US},  /* 27 */
 {-14400,"UTC-04:00","GEORGETOWN, LA PAZ, MANAUS, SAN JUAN",DR_US},  /* 28 */
 {-14400,"UTC-04:00","SANTIAGO",DR_NONE},  /* 29 */
 { -9000,"UTC-03:30","NEWFOUNDLAND",DR_US},  /* 30 */
 {-10800,"UTC-03:00","ARAGUAINA",DR_NONE},  /* 31 */
 {-10800,"UTC-03:00","BRASILIA",DR_NONE},  /* 32 */
 {-10800,"UTC-03:00","CAYENNE, FORTALEZA",DR_NONE},  /* 33 */
 {-10800,"UTC-03:00","CITY OF BUENOS AIRES",DR_NONE},  /* 34 */
 {-10800,"UTC-03:00","GREENLAND",DR_US},  /* 35 */
 {-10800,"UTC-03:00","MONTEVIDEO",DR_NONE},  /* 36 */
 {-10800,"UTC-03:00","PUNTA ARENAS",DR_NONE},  /* 37 */
 {-10800,"UTC-03:00","SAINT PIERRE AND MIQUELON",DR_US},  /* 38 */
 {-10800,"UTC-03:00","SALVADOR",DR_NONE},  /* 39 */
 { -7200,"UTC-02:00","COORDINATED UNIVERSAL TIME-02",DR_NONE},  /* 40 */
 { -3600,"UTC-01:00","AZORES",DR_EU},  /* 41 */
 { -3600,"UTC-01:00","CABO VERDE IS.",DR_EU},  /* 42 */
 {     0,"UTC","COORDINATED UNIVERSAL TIME",DR_NONE},  /* 43 */
 {     0,"UTC+00:00","DUBLIN, EDINBURGH, LISBON, LONDON",DR_EU},  /* 44 */
 {     0,"UTC+00:00","MONROVIA, REYKJAVIK",DR_EU},  /* 45 */
 {     0,"UTC+00:00","SAO TOME",DR_EU},  /* 46 */
 {  3600,"UTC+01:00","CASABLANCA",DR_EU},  /* 47 */
 {  3600,"UTC+01:00","AMSTERDAM, BERLIN, BERN, ROME, STOCKHOLM, VIENNA",DR_EU},  /* 48 */
 {  3600,"UTC+01:00","BELGRADE, BRATISLAVA, BUDAPEST, LJUBLJANA, PRAGUE",DR_EU},  /* 49 */
 {  3600,"UTC+01:00","BRUSSELS, COPENHAGEN, MADRID, PARIS",DR_EU},  /* 50 */
 {  3600,"UTC+01:00","SARAJEVO, SKOPJE, WARSAW, ZAGREB",DR_EU},  /* 51 */
 {  3600,"UTC+01:00","WEST CENTRAL AFRICA",DR_EU},  /* 52 */
 {  7200,"UTC+02:00","AMMAN",DR_LEV},  /* 53 */
 {  7200,"UTC+02:00","ATHENS, BUCHAREST",DR_EU},  /* 54 */
 {  7200,"UTC+02:00","BEIRUT",DR_LEV},  /* 55 */
 {  7200,"UTC+02:00","CAIRO",DR_EU},  /* 56 */
 {  7200,"UTC+02:00","CHISINAU",DR_EU},  /* 57 */
 {  7200,"UTC+02:00","DAMASCUS",DR_LEV},  /* 58 */
 {  7200,"UTC+02:00","GAZA, HEBRON",DR_LEV},  /* 59 */
 {  7200,"UTC+02:00","HARARE, PRETORIA",DR_EU},  /* 60 */
 {  7200,"UTC+02:00","HELSINKI, KYIV, RIGA, SOFIA, TALLINN, VILNIUS",DR_EU},  /* 61 */
 {  7200,"UTC+02:00","JERUSALEM",DR_LEV},  /* 62 */
 {  7200,"UTC+02:00","KALININGRAD",DR_EU},  /* 63 */
 {  7200,"UTC+02:00","KHARTOUM",DR_EU},  /* 64 */
 {  7200,"UTC+02:00","TRIPOLI",DR_EU},  /* 65 */
 {  7200,"UTC+02:00","WINDHOEK",DR_EU},  /* 66 */
 { 10800,"UTC+03:00","BAGHDAD",DR_EU},  /* 67 */
 { 10800,"UTC+03:00","ISTANBUL",DR_EU},  /* 68 */
 { 10800,"UTC+03:00","KUWAIT, RIYADH",DR_EU},  /* 69 */
 { 10800,"UTC+03:00","MINSK",DR_EU},  /* 70 */
 { 10800,"UTC+03:00","MOSCOW, ST. PETERSBURG",DR_EU},  /* 71 */
 { 10800,"UTC+03:00","NAIROBI",DR_NONE},  /* 72 */
 { 10800,"UTC+03:00","TEHRAN",DR_NONE},  /* 73 */
 { 14400,"UTC+04:00","ABU DHABI, MUSCAT",DR_NONE},  /* 74 */
 { 14400,"UTC+04:00","ASTRAKHAN, ULYANOVSK",DR_NONE},  /* 75 */
 { 14400,"UTC+04:00","BAKU",DR_NONE},  /* 76 */
 { 14400,"UTC+04:00","IZHEVSK, SAMARA",DR_NONE},  /* 77 */
 { 14400,"UTC+04:00","PORT LOUIS",DR_NONE},  /* 78 */
 { 14400,"UTC+04:00","SARATOV",DR_NONE},  /* 79 */
 { 14400,"UTC+04:00","TBILISI",DR_NONE},  /* 80 */
 { 14400,"UTC+04:00","VOLGOGRAD",DR_NONE},  /* 81 */
 { 14400,"UTC+04:00","YEREVAN",DR_NONE},  /* 82 */
 { 16200,"UTC+04:30","KABUL",DR_NONE},  /* 83 */
 { 18000,"UTC+05:00","ASHGABAT, TASHKENT",DR_NONE},  /* 84 */
 { 18000,"UTC+05:00","EKATERINBURG",DR_NONE},  /* 85 */
 { 18000,"UTC+05:00","ISLAMABAD, KARACHI",DR_NONE},  /* 86 */
 { 18000,"UTC+05:00","QYZYLORDA",DR_NONE},  /* 87 */
 { 19800,"UTC+05:30","CHENNAI, KOLKATA, MUMBAI, NEW DELHI",DR_NONE},  /* 88 */
 { 19800,"UTC+05:30","SRI JAYAWARDENEPURA",DR_NONE},  /* 89 */
 { 20700,"UTC+05:45","KATHMANDU",DR_NONE},  /* 90 */
 { 21600,"UTC+06:00","ASTANA",DR_NONE},  /* 91 */
 { 21600,"UTC+06:00","DHAKA",DR_NONE},  /* 92 */
 { 21600,"UTC+06:00","OMSK",DR_NONE},  /* 93 */
 { 23400,"UTC+06:30","YANGON (RANGOON)",DR_NONE},  /* 94 */
 { 25200,"UTC+07:00","BANGKOK, HANOI, JAKARTA",DR_NONE},  /* 95 */
 { 25200,"UTC+07:00","BARNAUL, GORNO-ALTAYSK",DR_NONE},  /* 96 */
 { 25200,"UTC+07:00","HOVD",DR_NONE},  /* 97 */
 { 25200,"UTC+07:00","KRASNOYARSK",DR_NONE},  /* 98 */
 { 25200,"UTC+07:00","NOVOSIBIRSK",DR_NONE},  /* 99 */
 { 25200,"UTC+07:00","TOMSK",DR_NONE},  /* 100 */
 { 28800,"UTC+08:00","BEIJING, CHONGQING, HONG KONG, URUMQI",DR_NONE},  /* 101 */
 { 28800,"UTC+08:00","IRKUTSK",DR_NONE},  /* 102 */
 { 28800,"UTC+08:00","KUALA LUMPUR, SINGAPORE",DR_NONE},  /* 103 */
 { 28800,"UTC+08:00","PERTH",DR_NONE},  /* 104 */
 { 28800,"UTC+08:00","TAIPEI",DR_NONE},  /* 105 */
 { 28800,"UTC+08:00","ULAANBAATAR",DR_NONE},  /* 106 */
 { 31500,"UTC+08:45","EUCLA",DR_NONE},  /* 107 */
 { 32400,"UTC+09:00","CHITA",DR_NONE},  /* 108 */
 { 32400,"UTC+09:00","OSAKA, SAPPORO, TOKYO",DR_NONE},  /* 109 */
 { 32400,"UTC+09:00","PYONGYANG",DR_NONE},  /* 110 */
 { 32400,"UTC+09:00","SEOUL",DR_NONE},  /* 111 */
 { 32400,"UTC+09:00","YAKUTSK",DR_NONE},  /* 112 */
 { 34200,"UTC+09:30","ADELAIDE",DR_AU},  /* 113 */
 { 34200,"UTC+09:30","DARWIN",DR_AU},  /* 114 */
 { 36000,"UTC+10:00","BRISBANE",DR_AU},  /* 115 */
 { 36000,"UTC+10:00","CANBERRA, MELBOURNE, SYDNEY",DR_AU},  /* 116 */
 { 36000,"UTC+10:00","GUAM, PORT MORESBY",DR_AU},  /* 117 */
 { 36000,"UTC+10:00","HOBART",DR_AU},  /* 118 */
 { 36000,"UTC+10:00","VLADIVOSTOK",DR_AU},  /* 119 */
 { 37800,"UTC+10:30","LORD HOWE ISLAND",DR_AU},  /* 120 */
 { 39600,"UTC+11:00","BOUGAINVILLE ISLAND",DR_NONE},  /* 121 */
 { 39600,"UTC+11:00","CHOKURDAKH",DR_NONE},  /* 122 */
 { 39600,"UTC+11:00","MAGADAN",DR_NONE},  /* 123 */
 { 39600,"UTC+11:00","NORFOLK ISLAND",DR_NONE},  /* 124 */
 { 39600,"UTC+11:00","SAKHALIN",DR_NONE},  /* 125 */
 { 39600,"UTC+11:00","SOLOMON IS., NEW CALEDONIA",DR_NONE},  /* 126 */
 { 43200,"UTC+12:00","ANADYR, PETROPAVLOVSK-KAMCHATSKY",DR_NZ},  /* 127 */
 { 43200,"UTC+12:00","AUCKLAND, WELLINGTON",DR_NZ},  /* 128 */
 { 43200,"UTC+12:00","COORDINATED UNIVERSAL TIME+12",DR_NZ},  /* 129 */
 { 43200,"UTC+12:00","FIJI",DR_NZ},  /* 130 */
 { 45900,"UTC+12:45","CHATHAM ISLANDS",DR_NZ},  /* 131 */
 { 46800,"UTC+13:00","COORDINATED UNIVERSAL TIME+13",DR_NONE},  /* 132 */
 { 46800,"UTC+13:00","NUKU'ALOFA",DR_NONE},  /* 133 */
 { 46800,"UTC+13:00","SAMOA",DR_NONE},  /* 134 */
 { 50400,"UTC+13:00","KIRITIMATI ISLAND",DR_NONE},  /* 135 */
};
_Static_assert(sizeof TZT/sizeof TZT[0]==TZ_N,"TZT must have the stock table's 136 entries");
#define CLK_MIN_VALID 1735689600LL          /* 2025-01-01 00:00 UTC: before this the SoC clock has never been set */
static int64_t clk_fake=-1;                 /* PREVIEW/selftest: a fixed UTC time */
static int64_t clk_off=0;                   /* SIM: offset to time(NULL) (MERAKIPLUS_SIM_CLOCK / ctrl "clock") */
static int64_t clk_utc(void){ return clk_fake>=0?clk_fake:(int64_t)time(NULL)+clk_off; }
static int clk_valid(int64_t u){ return u>=CLK_MIN_VALID; }
static int tz_idx(void){ int i=SET[S_TZ].val; return (i>=0&&i<TZ_N)?i:43; }      /* outside the table: #43 = UTC */
/* civil date <-> days since 1970-01-01 (proleptic Gregorian) */
static int64_t days_civil(int y,int m,int d){ y-=m<=2; int64_t era=(y>=0?y:y-399)/400; int yoe=(int)(y-era*400); int doy=(153*(m+(m>2?-3:9))+2)/5+d-1; int doe=yoe*365+yoe/4-yoe/100+doy; return era*146097+doe-719468; }
static int dow_civil(int y,int m,int d){ int64_t z=days_civil(y,m,d); return (int)((z%7+11)%7); }   /* 0 = Sunday (1970-01-01 = Thursday) */
static int dim_civil(int y,int m){ static const int D[12]={31,28,31,30,31,30,31,31,30,31,30,31}; return m==2&&((y%4==0&&y%100)||y%400==0)?29:D[m-1]; }
/* the n-th (n >= 1) or the last (n = -1) weekday `dow` of a month -> day of month */
static int nth_dow(int y,int m,int dow,int n){
    if(n>0){ int d1=dow_civil(y,m,1); return 1+(dow-d1+7)%7+(n-1)*7; }
    int dl=dim_civil(y,m), w=dow_civil(y,m,dl); return dl-(w-dow+7)%7;
}
static int64_t utc_of(int y,int m,int d,int h){ return days_civil(y,m,d)*86400+h*3600LL; }
/* is DST in effect at UTC time u in zone i (standard offset `off`)? Transitions: EU 01:00 UTC; the others 02:00 local standard time */
static int dst_on(int i,int64_t u){
    int off=TZT[i].off; time_t t=(time_t)(u+off); struct tm lt; gmtime_r(&t,&lt); int y=lt.tm_year+1900; int64_t a,b;
    switch(TZT[i].rule){
    case DR_EU:  a=utc_of(y,3,nth_dow(y,3,0,-1),1); b=utc_of(y,10,nth_dow(y,10,0,-1),1); return u>=a&&u<b;
    case DR_US:  a=utc_of(y,3,nth_dow(y,3,0,2),2)-off; b=utc_of(y,11,nth_dow(y,11,0,1),1)-off; return u>=a&&u<b;
    case DR_LEV: a=utc_of(y,3,nth_dow(y,3,5,-1),2)-off; b=utc_of(y,10,nth_dow(y,10,5,-1),1)-off; return u>=a&&u<b;
    case DR_AU:  a=utc_of(y,10,nth_dow(y,10,0,1),2)-off; b=utc_of(y,4,nth_dow(y,4,0,1),2)-off; return u>=a||u<b;   /* southern hemisphere */
    case DR_NZ:  a=utc_of(y,9,nth_dow(y,9,0,-1),2)-off; b=utc_of(y,4,nth_dow(y,4,0,1),2)-off; return u>=a||u<b;
    default: return 0;
    }
}
/* local time like stock's _refresh_systime: UTC + zone offset + (sumtime && DST ? 1 h : 0). Returns 1 if DST was applied. */
static int clk_local_at(int64_t u,struct tm*tm){
    int i=tz_idx(), d=SET[S_DST].val&&dst_on(i,u); time_t t=(time_t)(u+TZT[i].off+(d?3600:0)); gmtime_r(&t,tm); return d;
}
static int clk_now(struct tm*tm){ int64_t u=clk_utc(); clk_local_at(u,tm); return clk_valid(u); }
/* "HH:MM" (timemode 1) or "HH:MM A.M./P.M." (timemode 0) exactly as stock's update_time; ampm=0 drops the A.M./P.M. suffix */
static void fmt_hm(char*b,size_t n,int h,int m,int ampm){
    if(SET[S_TIMEMODE].val){ snprintf(b,n,"%02d:%02d",h,m); return; }
    int pm=h>=12, hh=h>12?h-12:(h==0?12:h);
    if(ampm) snprintf(b,n,"%02d:%02d %s",hh,m,TR(pm?TX_PM:TX_AM)); else snprintf(b,n,"%02d:%02d",hh,m);
}
static const short WD_TX[7]={TX_WD_SUN,TX_WD_MON,TX_WD_TUE,TX_WD_WED,TX_WD_THU,TX_WD_FRI,TX_WD_SAT};

/* ---- the schedule file: merakiplus's OWN settings (not config.json, not the stock UI's /customer/config.bin) ----
 * Text, one "key=value" per line: preheat=0|1, at=HH:MM, repeat=once|daily|mon-fri|sat-sun, autooff=0|15|20|30|60,
 * once_at=<UTC seconds of the one-shot occurrence, 0 = none>. Unknown lines are ignored, a bad value -> its default.
 * Written atomically like the language file (tmp + fsync + re-read/compare + rename + fsync of the directory). */
static char sched_path[256]="/new_customer/merakiplus/schedule";
static const char*const REP_KEY[4]={"once","daily","mon-fri","sat-sun"};
static const unsigned char REP_MASK[4]={0x00,0x7F,0x3E,0x41};   /* bit k = weekday k (0 = Sunday), stock's Sun..Sat flags; 0 = one-shot */
static const int AO_MIN[5]={0,15,20,30,60};
enum { SCS_DEFAULT=0, SCS_FILE, SCS_SESSION };
static struct { int on,hm,rep,ao,src; int64_t once_at; } sch={0,390,1,0,SCS_DEFAULT,0};
static int sched_val(int i){ return i==S_PREHEAT?sch.on:(i==S_PHAT?sch.hm:(i==S_PHREP?sch.rep:sch.ao)); }
static void sched_load(void){
    sch.on=0; sch.hm=390; sch.rep=1; sch.ao=0; sch.once_at=0; sch.src=SCS_DEFAULT;
    char b[512]; int fd=open(sched_path,O_RDONLY); if(fd<0) return;
    int n=(int)read(fd,b,sizeof b-1); close(fd); if(n<=0) return; b[n]=0; sch.src=SCS_FILE;
    for(char*l=strtok(b,"\r\n");l;l=strtok(NULL,"\r\n")){
        char*e=strchr(l,'='); if(!e) continue; *e++=0;
        if(!strcmp(l,"preheat")) sch.on=atoi(e)==1;
        else if(!strcmp(l,"at")){ int h,m; if(sscanf(e,"%d:%d",&h,&m)==2&&h>=0&&h<24&&m>=0&&m<60&&m%5==0) sch.hm=h*60+m; }
        else if(!strcmp(l,"repeat")){ for(int k=0;k<4;k++) if(!strcmp(e,REP_KEY[k])) sch.rep=k; }
        else if(!strcmp(l,"autooff")){ int v=atoi(e); for(int k=0;k<5;k++) if(v==AO_MIN[k]) sch.ao=k; }
        else if(!strcmp(l,"once_at")) sch.once_at=strtoll(e,NULL,10);
    }
    if(sch.once_at<0) sch.once_at=0;
}
static int sched_save(void){
    char d[192],chk[192],tmp[280]; int n=snprintf(d,sizeof d,"preheat=%d\nat=%02d:%02d\nrepeat=%s\nautooff=%d\nonce_at=%lld\n",
        sch.on,sch.hm/60,sch.hm%60,REP_KEY[sch.rep],AO_MIN[sch.ao],(long long)sch.once_at);
    snprintf(tmp,sizeof tmp,"%s.tmp",sched_path);
    if(!write_all(tmp,d,n,0)){ unlink(tmp); sch.src=SCS_SESSION; return 0; }
    int fd=open(tmp,O_RDONLY); int cn=fd>=0?(int)read(fd,chk,sizeof chk):-1; if(fd>=0) close(fd);
    if(cn!=n||memcmp(chk,d,n)||rename(tmp,sched_path)!=0){ unlink(tmp); sch.src=SCS_SESSION; return 0; }
    fsync_dir(sched_path); sch.src=SCS_FILE; return 1;
}
/* ---- v6.18: the sound file — merakiplus's OWN setting (docs/audio.md) ----
 * Text, one "key=value" per line: sound=off|on|stock, ready=off|brew|steam|both. Missing file / unknown value -> the default
 * (sound=on, ready=both). Written atomically like the schedule (tmp + fsync + re-read/compare + rename + fsync of the directory). */
static char snd_path[256]="/new_customer/merakiplus/sound";
static const char*const SND_MODE_KEY[3]={"off","on","stock"};
static const char*const SND_RDY_KEY[4]={"off","brew","steam","both"};
static struct { int mode,ready,src; } sndc={1,3,SCS_DEFAULT};
static void snd_cfg_load(void){
    sndc.mode=1; sndc.ready=3; sndc.src=SCS_DEFAULT;
    char b[256]; int fd=open(snd_path,O_RDONLY); if(fd<0) return;
    int n=(int)read(fd,b,sizeof b-1); close(fd); if(n<=0) return; b[n]=0; sndc.src=SCS_FILE;
    for(char*l=strtok(b,"\r\n");l;l=strtok(NULL,"\r\n")){
        char*e=strchr(l,'='); if(!e) continue; *e++=0;
        if(!strcmp(l,"sound")){ for(int k=0;k<3;k++) if(!strcmp(e,SND_MODE_KEY[k])) sndc.mode=k; }
        else if(!strcmp(l,"ready")){ for(int k=0;k<4;k++) if(!strcmp(e,SND_RDY_KEY[k])) sndc.ready=k; }
    }
}
static int snd_cfg_save(void){
    char d[96],chk[96],tmp[280]; int n=snprintf(d,sizeof d,"sound=%s\nready=%s\n",SND_MODE_KEY[sndc.mode],SND_RDY_KEY[sndc.ready]);
    snprintf(tmp,sizeof tmp,"%s.tmp",snd_path);
    if(!write_all(tmp,d,n,0)){ unlink(tmp); sndc.src=SCS_SESSION; return 0; }
    int fd=open(tmp,O_RDONLY); int cn=fd>=0?(int)read(fd,chk,sizeof chk):-1; if(fd>=0) close(fd);
    if(cn!=n||memcmp(chk,d,n)||rename(tmp,snd_path)!=0){ unlink(tmp); sndc.src=SCS_SESSION; return 0; }
    fsync_dir(snd_path); sndc.src=SCS_FILE; return 1;
}
/* stock performSleepActions 0x148324 for ONE entry (hm = minutes after midnight, mask = REP_MASK): the next occurrence strictly after
 * `now` (minute resolution). Returns the days ahead (0..7); *ad = the day of month (wrapped like stock w. the month's length). */
static int sched_next(const struct tm*now,int hm,int mask,int*ad){
    int nowm=now->tm_hour*60+now->tm_min, wd=now->tm_wday, k;
    if(!mask) k=hm>nowm?0:1;                                          /* one-shot: today if later, else tomorrow */
    else if((mask>>wd&1)&&hm>nowm) k=0;                               /* judgeWeekEqual: today, later than now */
    else { for(k=1;k<=7;k++) if(mask>>((wd+k)%7)&1) break; }          /* getLastWeek 0x1496C0: the next flagged weekday (7 = same day next week) */
    int y=now->tm_year+1900, m=now->tm_mon+1, d=now->tm_mday+k, dm=dim_civil(y,m);
    *ad=d>dm?d-dm:d; return k;
}
/* the alarm to send now: 1 + (d,h,m), or 0 = none (0/0/0). A one-shot uses the occurrence it was saved for (once_at). */
static int sched_alarm(int64_t u,int*ad,int*ah,int*am,int64_t*at){
    *ad=*ah=*am=0; if(at) *at=0;
    if(!sch.on||!clk_valid(u)) return 0;
    struct tm lt; clk_local_at(u,&lt);
    if(!REP_MASK[sch.rep]){
        if(sch.once_at<=u) return 0;
        struct tm ot; clk_local_at(sch.once_at,&ot); *ad=ot.tm_mday; *ah=ot.tm_hour; *am=ot.tm_min; if(at) *at=sch.once_at; return 1;
    }
    int k=sched_next(&lt,sch.hm,REP_MASK[sch.rep],ad); *ah=sch.hm/60; *am=sch.hm%60;
    if(at) *at=u-(lt.tm_hour*3600+lt.tm_min*60+lt.tm_sec)+k*86400LL+sch.hm*60LL;   /* approx. UTC of the occurrence (for display) */
    return 1;
}
/* a one-shot: the UTC time of the next occurrence of hh:mm after u (stored when the schedule is saved) */
static int64_t sched_once_target(int64_t u){
    if(!clk_valid(u)) return 0;
    struct tm lt; int dst=clk_local_at(u,&lt); int ad, k=sched_next(&lt,sch.hm,0,&ad);
    int64_t mid=u-(lt.tm_hour*3600+lt.tm_min*60+lt.tm_sec);
    int64_t t=mid+k*86400LL+sch.hm*60LL; struct tm ct; int dst2=clk_local_at(t,&ct); return t+(dst-dst2)*3600LL;   /* across a DST switch */
}
/* load all settings from config.json. The interval is NEVER widened for proven limits:
 * a value outside spec is shown as it is, marked, and blocks activity start
 * until the user has chosen a valid value. */
static void settings_load(void){
    cfg_read(); sched_load(); snd_cfg_load();
    for(int i=0;i<NSET;i++){
        Setting*s=&SET[i]; int v;
        s->lo=s->min; s->hi=s->max;
        if(s->flags&SF_SENSOR){ s->val=0; s->loaded=0; continue; }
        if(s->flags&SF_LOCAL){ s->val=ui_lang; s->loaded=1; continue; }      /* v6.3: the UI language comes from the lang file, not config.json */
        if(s->flags&SF_OWN){ s->val=sched_val(i); s->loaded=1; continue; }   /* v6.16: the schedule file (sched_load) */
        if(s->flags&SF_SND){ s->val=i==S_SOUND?sndc.mode:sndc.ready; s->loaded=1; continue; }   /* v6.18: the sound file (snd_cfg_load) */
        if(cfg_get_int(s->key,&v)){ s->val=v; s->loaded=1; }
        else { s->val=s->def; s->loaded=0; }
        if(s->flags&SF_LOCK){ s->lo=s->hi=s->val; continue; }
        if((s->flags&SF_EST)&&s->loaded&&s->val>s->hi&&s->val<=s->hard) s->hi=s->val;
    }
}
/* word 112/113/114 packing [PARITY §3.7; demo_v9 model_data_init 0xcf284..0xcf43c, onClick 0xeb1ac/0x103858]:
 *   word112 = (preInfusionSwitch ? 0x0100 : 0) | (preInfusionPressure & 0xFF)
 *   word113 = (preInfusionTime << 8) | (breakTime & 0xFF)          word114 = headTemper (whole °C; MCU x10)
 * The MCU recipe 0x1E10 reads word112 lo (only 3/4 are handled), copies word113 hi -> lo
 * (breakTime is overwritten) and uses word13 + word113_lo as the pre-infusion duration. */
static uint16_t pack112(int sw,int press){ return (uint16_t)((sw?0x0100:0)|(press&0xFF)); }
static uint16_t pack113(int t,int brk){ return (uint16_t)(((t&0xFF)<<8)|(brk&0xFF)); }

/* ================= v6.22: WATER DOSING GATE (docs/water-dosing-compat.md §5 + decisions §10) =================
 * "Dosing" = hot water in AUTO, stopped BY THE CONTROLLER when the brew scale passes word 17 (controller fw 0x1C64..0x1C84). Only
 * controllers > 5.1.2 have it (release note 5.1.4 "Added hot water weighing"). The controller reports its firmware string read-only in
 * words 80..87 (ASCII, lo byte first: "V5.1.4") and a copy in words 88..95 — both are in the sub56 len56 poll merakiplus already sends.
 * The rule is the stock UI's, copied exactly (maintainer decision 1, no owner override):
 *   stock mcu_has_hot_water_weight_ui 0x10885C = compare_versions(word80 string, "5.1.2") > 0   (literal 0x178FB0)
 *   stock hotWaterScaleFault 0x112BD8 = word 35 & (0x1000 | 0x0400) -> MANUAL; a running AUTO dose is stopped (hotWaterUpdate 0x115B80:
 *   mirror word 16 != 0 -> word 16 = 0, usart_send(6,16,1))
 * Stricter than stock on purpose (fail-safe: a wrong answer only costs convenience): the string must be <= 6 letters + N.N.N (digits only,
 * <= 4 per part) and nothing else, and the copy (88..95) must match byte for byte. Empty, not polled yet, garbage, copy mismatch, scale
 * fault -> MANUAL ONLY: the WATER page hides HOT WATER + WATER MODE, a start is the manual frame, frame_policy rejects word 16 = 1.
 * config.json is NEVER written by the gate (hotWaterMode = 1 on an old controller is simply not used — like stock). */
enum { MV_NONE=0, MV_OK, MV_BAD, MV_COPY };
static int ver_parse(const char*s,int v[3]){
    int k=0; while(k<6&&((s[k]>='A'&&s[k]<='Z')||(s[k]>='a'&&s[k]<='z'))) k++;
    for(int p=0;p<3;p++){
        int n=0,d=0; while(s[k]>='0'&&s[k]<='9'){ if(++d>4) return 0; n=n*10+(s[k]-'0'); k++; }
        if(!d) return 0; v[p]=n;
        if(p<2){ if(s[k]!='.') return 0; k++; }
    }
    return s[k]==0;
}
/* the firmware string as reported (o: printable, upper case, '?' for anything else; "" when empty) + its state */
static int mcu_ver_read(char*o,size_t n,int v[3]){
    char a[17],b[17]; o[0]=0;
    for(int w=80;w<96;w++) if(!reg_seen[w]) return MV_NONE;
    for(int k=0;k<8;k++){ a[2*k]=(char)(reg[80+k]&0xFF); a[2*k+1]=(char)(reg[80+k]>>8); b[2*k]=(char)(reg[88+k]&0xFF); b[2*k+1]=(char)(reg[88+k]>>8); }
    a[16]=b[16]=0;
    size_t L=strlen(a), j=0;
    for(size_t i=0;i<L&&j+1<n;i++){ unsigned char c=(unsigned char)a[i]; if(c>='a'&&c<='z') c=(unsigned char)(c-32);
        o[j++]=((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_')?(char)c:'?'; }
    o[j]=0;
    if(!L) return MV_NONE;
    if(memcmp(a,b,16)) return MV_COPY;
    return ver_parse(a,v)?MV_OK:MV_BAD;
}
static int wd_scale_fault(void){ return reg_seen[35]&&(reg[35]&0x1400)!=0; }
static int wd_ver_dosing(void){     /* the version alone says "> 5.1.2" */
    char b[20]; int v[3]; if(mcu_ver_read(b,sizeof b,v)!=MV_OK) return 0;
    return v[0]!=5?v[0]>5:(v[1]!=1?v[1]>1:v[2]>2);
}
static int wd_dosing(void){ return wd_ver_dosing()&&!wd_scale_fault(); }
/* the next WATER start is MANUAL: no dosing, or WATER MODE = MANUAL (config hotWaterMode 2) */
static int water_manual(void){ return !wd_dosing()||sv(S_HOTMODE)!=1; }
/* why there is no dosing (the manual-only WATER page's last line): 0 controller <= 5.1.2, 1 version not (validly) reported, 2 scale fault */
enum { WDW_OLD=0, WDW_UNK, WDW_FAULT };
static int wd_why(void){ char b[20]; int v[3]; if(mcu_ver_read(b,sizeof b,v)!=MV_OK) return WDW_UNK; return wd_ver_dosing()?WDW_FAULT:WDW_OLD; }
static struct { int fault, due, man; long n_fault, n_stop; } wd;   /* fault: last seen; due: an AUTO stop still has to go out; man: the running/last water was MANUAL */

/* ---- frame policy (declared at the write queue): per-word spec interval + trigger words ---- */
static int word_in(int si,int v){ return set_valid(&SET[si],v); }
static int word_ok(int idx,uint16_t v){
    switch(idx){
    case 0:   return v<=1;                               /* power: 1 = PowerOn; 0 = PowerOff only w. I_POWEROFF (frame_policy) — v6.14 */
    case 1:   return v==0;                               /* 0=awake; 1 (sleep/drain) is never sent */
    case 2:   return word_in(S_GRINDMODE,v);
    case 3: case 6: case 9: case 16: case 19: case 22: case 24: return v<=1;   /* start words (intent checked in frame_policy); v6.22: + 19 */
    case 4:   return word_in(S_GRINDW,v);
    case 5: case 13: case 15: return v==60;              /* hardcoded recipe words [fuart_thread 0x146810] */
    case 11:  return word_in(S_BREWT,v);
    case 14:  return word_in(S_STEAMG,v);
    case 17:  return word_in(S_HOTW,v);
    case 23:  return word_in(S_BREWW,v);
    case 25:  return word_in(S_STEAMT,v);
    case 112: return (v>>8)<=1 && word_in(S_PIPRESS,v&0xFF);
    case 113: return word_in(S_PITIME,v>>8);             /* lo byte = breakTime: arbitrary byte, no effect */
    case 114: return word_in(S_HEADT,v);
    default:  return v==0;                               /* 7,8,10,12,18,20,21,26..31: always 0 */
    }
}
static int frame_policy(uint8_t sub,const uint16_t*w,int len,uint32_t intent){
    if(intent&(TRIG_NEVER|~(TRIG_START|I_POWEROFF))) return WJ_TRIG;
    for(int k=0;k<len;k++){
        int idx=sub+k; uint16_t v=w[k];
        if(idx==0&&v==0&&!(intent&I_POWEROFF)) return WJ_TRIG;                    /* v6.14: PowerOff only w. explicit intent */
        if(idx<32&&v!=0){
            if(TRIG_NEVER&WBIT(idx)) return WJ_TRIG;                              /* calibration/reboot: never */
            if((TRIG_START&WBIT(idx))&&!(intent&WBIT(idx))) return WJ_TRIG;       /* start word without intent */
            if(idx==16&&!wd_dosing()) return WJ_TRIG;                             /* v6.22: AUTO hot water only on a dosing controller */
        }
        if(!word_ok(idx,v)) return WJ_RANGE;
    }
    return 0;
}

/* queues cmd6 write(s) for setting i with new value v. Returns: 1=queued, 0=no MCU part, -1=error.
 * Only single-word/pre-infusion blocks from PARITY §3.7-3.8 — never a trigger word in the frame
 * (grindMode is sent as sub2 len1; stock's sub2 len2 would also send start word 3). */
static int setting_mcu_push(int i,int v){
    uint16_t w[2];
    switch(SET[i].mcu){
    case M_NONE: return 0;
    case M_W4:   w[0]=(uint16_t)v; return wq_push(4,w,1)?1:-1;
    case M_W23:  w[0]=(uint16_t)v; return wq_push(23,w,1)?1:-1;
    case M_W11:  w[0]=(uint16_t)v; return wq_push(11,w,1)?1:-1;
    case M_W25:  w[0]=(uint16_t)v; return wq_push(25,w,1)?1:-1;
    case M_W14:  w[0]=(uint16_t)v; return wq_push(14,w,1)?1:-1;
    case M_W17:  w[0]=(uint16_t)v; return wq_push(17,w,1)?1:-1;
    case M_W2:   w[0]=(uint16_t)v; return wq_push(2,w,1)?1:-1;
    case M_W114: w[0]=(uint16_t)v; return wq_push(114,w,1)?1:-1;
    case M_W112: /* stock: usart_send(6,112,2) — word112+113 together [0xeb570/0x103bc8] */
        w[0]=pack112(i==S_PISW?v:sv(S_PISW), i==S_PIPRESS?v:sv(S_PIPRESS));
        w[1]=pack113(sv(S_PITIME),sv(S_BREAK));
        return wq_push(112,w,2)?1:-1;
    case M_W113: /* stock: usart_send(6,113,1) [0xeb700/0xeb880] */
        w[0]=pack113(i==S_PITIME?v:sv(S_PITIME), i==S_BREAK?v:sv(S_BREAK));
        return wq_push(113,w,1)?1:-1;
    }
    return -1;
}

/* ================= activity recipes (PARITY §3.1-3.4; observed frames, byte-for-byte) =================
 * GRIND  [L BREW.md W1]     cmd6 sub3 len4  : 1, dose, 60, 1
 * BREW   [L BREW.md W3]     cmd6 sub5 len18 : 60,0,0,0,1,0,temp,0,60,gear,60,0,water,0,0,0,0,1
 * WATER  [L HOTWATER.md W1] cmd6 sub9 len11 : 1,0,temp,0,60,gear,60,1,water,0,0   (AUTO; only on a dosing controller, v6.22)
 * WATER  MANUAL (v6.22)     cmd6 sub19 len1 : 1   (stock legacy page set_manual_hot_water_switch 0x105118 usart_send(6,19,1))
 * STEAM  [S SESSION.md §10] cmd6 sub24 len8 : 1,target,0,0,0,0,0,0   (stop: word24=0)
 * Only the config-derived fields are substituted; all other words are as in the atlas. Each
 * start frame states explicitly which start words it may set (I_*); words 26/27/30 in the
 * steam block are constant 0 and cannot become anything else (frame_policy). */
enum { ACT_NONE=-1, ACT_GRIND=0, ACT_BREW, ACT_WATER, ACT_STEAM };
static int act_push(int kind){
    uint16_t w[18];
    switch(kind){
    case ACT_GRIND: { uint16_t g[4]={1,(uint16_t)sv(S_GRINDW),60,1}; return wq_push_t(3,g,4,I_GRIND); }
    case ACT_BREW: {
        /* sync only if the MCU mirror differs from config (stock pushes its mirror at boot).
         * word113 is compared on the hi byte only: the MCU overwrites the lo byte itself (0x1E1E). */
        if(reg_seen[23] && reg[23]!=(uint16_t)sv(S_BREWW)){ w[0]=(uint16_t)sv(S_BREWW); if(!wq_push(23,w,1)) return 0; }
        if(reg_seen[112]&&reg_seen[113]&&reg_seen[114]){
            uint16_t p[3]={pack112(sv(S_PISW),sv(S_PIPRESS)),pack113(sv(S_PITIME),sv(S_BREAK)),(uint16_t)sv(S_HEADT)};
            if(p[0]!=reg[112]||(p[1]>>8)!=(reg[113]>>8)||p[2]!=reg[114]){ if(!wq_push(112,p,3)){ wq.n=0; return 0; } }
        }
        uint16_t b[18]={60,0,0,0,1,0,(uint16_t)sv(S_BREWT),0,60,(uint16_t)sv(S_STEAMG),60,0,(uint16_t)sv(S_HOTW),0,0,0,0,1};
        if(!wq_push_t(5,b,18,I_BREW)){ wq.n=0; return 0; }      /* never half sequences */
        return 1;
    }
    case ACT_WATER: {
        if(water_manual()){ uint16_t m[1]={1}; return wq_push_t(19,m,1,I_WATERMAN); }   /* v6.22: manual — runs until word 19 = 0 (no timeout, like stock) */
        uint16_t h[11]={1,0,(uint16_t)sv(S_BREWT),0,60,(uint16_t)sv(S_STEAMG),60,1,(uint16_t)sv(S_HOTW),0,0}; return wq_push_t(9,h,11,I_WATER); }
    case ACT_STEAM: { uint16_t s[8]={1,(uint16_t)sv(S_STEAMT),0,0,0,0,0,0}; return wq_push_t(24,s,8,I_STEAM); }
    }
    return 0;
}
/* STOP must never be rejectable by the policy: the target word is clamped into the spec interval */
static int act_push_steam_stop(void){
    uint16_t s[8]={0,(uint16_t)clampi(sv(S_STEAMT),SET[S_STEAMT].lo,SET[S_STEAMT].hi),0,0,0,0,0,0};
    return wq_push(24,s,8);
}
/* ================= v6.13: manual STOP for grind / brew / water (UI_v6.md §18 + CHANGES-v6.13) =================
 * User-reported bug in v6.12: hot water could not be stopped from the screen — only steam had a STOP button. The stop words are reversed from
 * the MCU firmware (MEM03_V514_260923.bin) and cross-checked against the stock UI's own stop calls (demo_v9, symbols preserved):
 *   GRIND cmd6 sub6  len1: word 6  = 0   aa 55 06 06 01 00 00 c4 47
 *         MCU grind-ctl 0x19C6: the grinder runs while word 6 != 0 (0x1AA2: work_sta |= 0x40, else &= ~0x40). The MCU's own
 *         stops — weight target 0x1A68, timeout 0x1A72, error 0x1A3E/0x1A8C — write exactly word 6 = 0. Stock: onClick 0x10928C
 *         usart_send(6,6,1), after mirror word 6 has been flipped to 0 (the start branch 0x109278 sends sub3 len4).
 *   BREW  cmd6 sub22 len1: word 22 = 0   aa 55 06 16 01 00 00 c0 87
 *         MCU brew-ctl 0x1E9C: on every CHANGE of word 22, running = (word 22 != 0) (0x1F94..0x1FC6). The MCU's weight stop
 *         itself writes word 22 = 0 (0x1F92) — same path, incl. the valve pulse on bit 2 afterwards (0x20B2..0x20EC). Stock:
 *         onClick 0x109694 flips mirror word 22 to 0 and sends the WHOLE block usart_send(6,5,18); merakiplus writes only the word
 *         that changes (words 5..21 in stock's block are the MCU's own polled values — a no-op for the MCU; cmd6 = memcpy).
 *   WATER cmd6 sub16 len1: word 16 = 0   aa 55 06 10 01 00 00 c0 0f   (+ sub19 len1 word 19 = 0 — only if word 19 != 0)
 *         MCU hotwater-ctl 0x1B8C: active = (word 19 | word 16) != 0, recomputed every cycle (0x1C44..0x1C4E). The MCU's weight stop
 *         itself writes word 16 = 0 (0x1C80). Stock: hotWaterStartStop 0x11468C usart_send(6,16,1) (auto mode, = merakiplus's
 *         start) and 0x114720 usart_send(6,19,1) (manual mode; since v6.22 merakiplus starts it too — and on a controller that
 *         is not known to be > 5.1.2 the stop is word 19 = 0 ONLY, the legacy page's frame).
 *   STEAM cmd6 sub24 len8: word 24 = 0   (unchanged from v6)
 * A stop frame contains only zeros: frame_policy does not reject it (start words <= 1, others == 0), and it can never start
 * anything. Same write queue as everything else: CRC + ACK wait + one retransmit. AUTO-STOP is unchanged: merakiplus sends nothing when the MCU
 * stops by itself. Confirmation (stop_step): the activity's work_sta bits are cleared — grind bit 6; brew bits 1+2 (bits 2 + 12 are cleared
 * in the first cycle after stop, 0x20C4); water bits 1+3 (bit 3 is always cleared, 0x1C9A; bits 1+9 are shared with boiler refill). */
static int act_push_stop(int kind){
    uint16_t z=0; int n0=wq.n;
    switch(kind){
    case ACT_GRIND: return wq_push(6,&z,1);
    case ACT_BREW:  return wq_push(22,&z,1);
    case ACT_WATER:
        /* v6.22: a controller that is not known to be > 5.1.2 gets only the legacy page's stop (word 19 = 0) — the stock UI never sends it
         * word 16 (what word 16 does on <= 5.1.2 firmware is unknown, docs/water-dosing-compat.md §1.4) */
        if(!wd_ver_dosing()) return wq_push(19,&z,1);
        if(!wq_push(16,&z,1)) return 0;
        if(reg_seen[19]&&reg[19]!=0&&!wq_push(19,&z,1)){ wq.n=n0; return 0; }   /* never a half water stop */
        return 1;
    case ACT_STEAM: return act_push_steam_stop();
    }
    return 0;
}
#define STOP_CONFIRM_MS 5000   /* how long to wait for the bits to clear (measured: ACK ~20-280 ms + MCU cycle 10 ms + poll <= 0.5 s) */
static struct { int kind, active; ms_t t_req; long seq, n_req, n_ok, n_fail; } stp={ACT_NONE,0,0,0,0,0,0};

/* ================= phases (work_sta word 33; SESSION.md §6) ================= */
enum { PH_OFFLINE=0, PH_SLEEP, PH_ERROR, PH_HEAT, PH_READY, PH_GRIND, PH_PRE, PH_EXT, PH_WATER, PH_STEAM, PH_DONE, NPHASE };
_Static_assert(TX_PH_DONE-TX_PH_OFFLINE==NPHASE-1,"TX_PH_* must follow PH_*");
static const char* ph_name(int p){ return TR(TX_PH_OFFLINE+((p>=0&&p<NPHASE)?p:PH_OFFLINE)); }
static int link_up(ms_t now){ return now-last_rx_ms<1500; }
static int phase_raw(ms_t now){
    if(!link_up(now)) return PH_OFFLINE;
    uint16_t ws=reg[33];
    if((ws&0x4020)==0x4020) return PH_STEAM;                 /* bits 5+14 */
    if(ws&0x0040) return PH_GRIND;                           /* bit 6 */
    if((ws&0x0006)==0x0006) return (ws&0x0200)?PH_EXT:PH_PRE;/* bits 1+2 (+9 = extraction) */
    if((ws&0x000A)==0x000A) return PH_WATER;                 /* bits 1+3 */
    if(reg[34]||reg[35]||reg[36]) return PH_ERROR;
    if((reg_seen[1]&&reg[1]==1) || (reg_seen[33]&&!(ws&0x0100))) return PH_SLEEP;
    if(reg_seen[40]&&reg_seen[11]&&(int)reg[40] < (int)reg[11]-20) return PH_HEAT;
    return PH_READY;
}
static int phase_active(int p){ return p>=PH_GRIND&&p<=PH_STEAM; }
/* ---- v6.6: phase semantics per STEAMLIVE.md (proven live) ----
 * (1) TWO STEAM PATHS, both = work_sta bits 5+14 (0x4020):
 *     AUTO (latched)  word 24 != 0 — UI start (cmd6 sub24 len8, word24=1). The MCU stops BY ITSELF when the milk
 *                     (word 48) reaches the target (word 25), clears bits 5+14 and resets word 24. No stop write.
 *                     The probe keeps rising approx. 4 °C for ~11 s after stop (thermal inertia) — info only.
 *     WAND (momentary) word 24 == 0 — the physical wand switch (word 44 bit0, word 46 = 1 follows approx. 0.4 s
 *                     later). NO auto-stop, not even above the target: stops when the wand is released.
 *                     Sends no UART frames; a STOP write (word24=0) would do nothing.
 *     The class is decided by word 24 while bits 5+14 are set, and is STICKY for AUTO: after a manual
 *     STOP write, word 24 = 0 (ACK echo) for a moment before the MCU clears the bits — it is still AUTO.
 *     If word 24 has not been read yet: UNKNOWN (neutral text; the STOP button is shown — it is always safe).
 * (2) CONCURRENT PHASES: brew bits (1+2+9+12) and steam bits (5+14) are disjoint and can be set together
 *     (0x5327 = flush during brew). HOME then shows BOTH: the main activity (grind/brew/water: gauge,
 *     weight, time) + a steam marker, and the milk temperature is in the stat row.
 * phase_raw() is UNCHANGED (the state contract: steam first). phase_main() is what HOME shows as the
 * main activity: grind/brew/water take precedence over steam. The guards still use phase_raw(): any
 * activity — including a wand flush — blocks start, SAVE, SWITCH NOW and boot push (v6.20: except a latched steam start on top
 * of a brew, steam_brew_ok()). */
enum { SK_NONE=0, SK_UNKNOWN, SK_AUTO, SK_WAND };
static struct { int kind, last_kind, wand; ms_t t0; } stm;   /* kind: ongoing steam; last_kind: the most recently finished; wand: the switch was seen */
static int steam_bits(void){ return (reg[33]&0x4020)==0x4020; }
static int wand_sw(void){ return (reg_seen[44]&&(reg[44]&1))||(reg_seen[46]&&reg[46]!=0); }
static void steam_track(ms_t now){
    if(!link_up(now)||!steam_bits()){ if(stm.kind!=SK_NONE){ stm.last_kind=stm.kind; stm.kind=SK_NONE; } return; }
    if(stm.kind==SK_NONE){ stm.kind=SK_UNKNOWN; stm.t0=now; stm.wand=0; }
    if(wand_sw()) stm.wand=1;
    if(reg_seen[24]&&reg[24]!=0) stm.kind=SK_AUTO;                         /* latched — sticky until the bits clear */
    else if(reg_seen[24]&&stm.kind!=SK_AUTO) stm.kind=SK_WAND;             /* word 24 = 0: the wand path */
}
static int steam_stop_visible(void){ return stm.kind==SK_AUTO||stm.kind==SK_UNKNOWN; }   /* WAND: STOP would be a no-op */
static int steam_target(void);                                                           /* word 25 (the MCU's target), else config */
static int phase_main(ms_t now){
    if(!link_up(now)) return PH_OFFLINE;
    uint16_t ws=reg[33];
    if(ws&0x0040) return PH_GRIND;
    if((ws&0x0006)==0x0006) return (ws&0x0200)?PH_EXT:PH_PRE;
    if((ws&0x000A)==0x000A) return PH_WATER;
    return phase_raw(now);
}
static const char* steam_label(int k){ return TR(k==SK_AUTO?TX_PH_STEAM_AUTO:(k==SK_WAND?TX_PH_STEAM_WAND:TX_PH_STEAM)); }
/* v6.13: which STOP buttons HOME shows. The main activity (phase_main: grind / brew / water) ALWAYS has a STOP button as long
 * as its bits are set; latched steam has its own as before. Concurrent main activity + latched steam = two buttons side by side (left =
 * the main activity, right = steam). Wand/flush still has none (a STOP write would do nothing). Returns: number of buttons (0..2). */
static int stop_main_kind(ms_t now){
    switch(phase_main(now)){ case PH_GRIND: return ACT_GRIND; case PH_PRE: case PH_EXT: return ACT_BREW; case PH_WATER: return ACT_WATER; default: return ACT_NONE; }
}
static int stop_btns(ms_t now,int k[2]){
    int n=0, m=stop_main_kind(now); k[0]=k[1]=ACT_NONE;
    if(m!=ACT_NONE) k[n++]=m;
    if(steam_stop_visible()) k[n++]=ACT_STEAM;
    return n;
}
/* v6.20: HOLD · STEAM — during a brew with no steam at all, the right-hand slot next to STOP BREW starts a latched steam session
 * (hold HOLD_MS, same act_request() as everywhere). Once steam latches the slot is STOP STEAM (stop_btns). Shown even when the guard
 * says no (dimmed; a tap says why), so the layout does not jump. */
static int steam_hold_shown(ms_t now){ return link_up(now)&&stop_main_kind(now)==ACT_BREW&&!steam_bits(); }
/* are the activity's work_sta bits still set? (same masks as phase_raw/phase_main) */
static int stop_running(int kind){
    uint16_t ws=reg[33];
    switch(kind){ case ACT_GRIND: return (ws&0x0040)!=0; case ACT_BREW: return (ws&0x0006)==0x0006; case ACT_WATER: return (ws&0x000A)==0x000A; case ACT_STEAM: return steam_bits(); }
    return 0;
}
static const char* err_text(void){
    uint16_t e1=reg[34],e2=reg[35],e3=reg[36];
    if(e3&0x10) return TR(TX_ER_E3_BREW);
    if(e3&0x20) return TR(TX_ER_E3_STEAM);
    if(e1&0x20) return TR(TX_ER_E1_GROUP);
    if(e1&0x01) return TR(TX_ER_E1_MAINS);
    if(e2&0x01) return TR(TX_ER_E2_BREW);
    if(e2&0x02) return TR(TX_ER_E2_GROUP);
    if(e2&0x08) return TR(TX_ER_E2_STEAM);
    if(e2&0x10) return TR(TX_ER_E2_MILK);
    if(e2&0x1400) return TR(TX_ER_E2_SCALE);                 /* v6.22: stock hotWaterScaleFault 0x112BD8 */
    return TR(TX_ER_UNKNOWN);
}
/* v6.11: the error code (E1/E2/E3) is no longer in the HOME subline (that is plain language); it is shown dimmed in the chart area
 * with a pointer to DIAGNOSE (the E1 / E2 / E3 cell). Same priority as err_text(). */
static const char* err_code(void){
    uint16_t e1=reg[34],e2=reg[35],e3=reg[36];
    if(e3&0x30) return "E3";
    if(e1&0x21) return "E1";
    if(e2&0x141B) return "E2";                               /* v6.22: + the scale-fault bits 0x1400 */
    return "?";
}

/* ================= v6.9: display conditioning (ONLY what is drawn) =================
 * User complaint: large swings make the graph look unstable — on the device (ADC noise ±9..±40, maintenance/deadhead
 * pulses on word 51 up to 15-16k for 0.1..1 s, BREW.md §6 / STEAMLIVE.md §5.3) and in the SIM. The fix lives in the UI,
 * so it applies in both places (sim/mcu_sim.py's 'calm' is a separate demo aid, default OFF, and is not used here).
 *  RAW  : reg[] and the ring buffer g_p/g_w/g_m are unchanged. ALL control (writes, auto-stop expectations, guards, thresholds,
 *         phase logic, DIAGNOSE counters) still reads the raw values — v6.9 changes none of them.
 *  CHART: per sample (10 Hz) median of the last 3 (removes single-sample spikes) -> EMA, alpha CD_A_CHART = 0.18
 *         (time constant -1/ln(1-0.18) = 5.0 samples = 0.50 s). Stored next to the raw values in g_pd/g_wd/g_md.
 *         The filters follow median-3 directly for 2 samples when the activity changes (start/stop is not smeared; see SWITCH).
 *  SCALE: y top = max(target/floor x 1.08, P x CD_HEAD (1.15)), P = the CD_PCT percentile (p90) of the window's ACTIVE raw samples
 *         (those above the noise floor: pressure >= 40, weight/milk > 0). Short pulses (deadhead after brew, maintenance at rest) are few
 *         samples and therefore do not flatten the curve; when a raw sample lies above the top, a small tick is drawn at
 *         the top edge (the information is not hidden). Only ACTIVE samples, because a p90 over the WHOLE 40 s window is 0 in the
 *         first approx. 4 s of an extraction (the window is mostly idle) and would squash the curve up into the ceiling — the selftest
 *         measures both. cd.scale=CS_HOLD (highest level held for CD_HOLD samples) and CS_MAX (v6.8) can be selected in the SIM.
 *  IDLE : the pressure curve is drawn only for samples taken during brew/pre-inf./water (g_a). Outside an activity, word 51 is
 *         maintenance/pump pulses (small heating cycles 400-900 for 0.5-1.5 s, deadhead 3000-16670 for 0.1-0.25 s) — they are shown
 *         as small ticks on the baseline. The chart is "active" at rest as long as the 40 s window contains a brew/water (v6.8: one raw
 *         sample >= 40 — so every maintenance pulse turned on a spike chart at rest). The weight line is drawn all the way.
 *  GATE : the pressure curve also has a pulse gate: if median-3 jumps to > 2 x the shown value + 400, the shown value is held
 *         for up to CD_GATE = 3 samples (0.3 s); if the jump lasts longer, it is real and is followed. Median-3 alone only removes
 *         1-sample pulses — a 2-sample deadhead pulse (15500) would otherwise show up as a 1.5 s long hump at the ceiling.
 *  SWITCH: when the activity (or the weight source) changes, the chart filters follow median-3 directly for 2 samples (CD_FAST) instead
 *         of EMA — the step is not smeared, and a pulse sample right at the switch does not get through.
 *  HERO : the hero number (+ gauge and the BAR field, which show the same quantity) = median-3 + EMA alpha CD_A_HERO = 0.25 (0.35 s),
 *         updated at a 10 Hz rate regardless of frame rate. Snaps to the raw value on phase change (incl. DONE), change of
 *         source (weight/milk/boiler), change of setpoint/target, a median jump > CD_HERO_JUMP and after > 0.5 s without update.
 *  WEIGHT: (v6.9, user complaint about periodic weight spikes at rest) the source is LATCHED (wsl, see "stable weight source") instead
 *         of following the phase sample by sample, and the shown weight (hero, chart, stat field) has a DEADBAND: |weight| < 0.25 g
 *         (|word| < CD_WDB = 2.5 in 0.1 g) is shown as 0. Words 38/39 are SIGNED (tare 0xFFFD = -3): all filtering is done on int16
 *         values (s16) — a word interpreted as unsigned would smear a tare of -0.3 g out towards 6553.3 g.
 *  TUNING: -DCD_A_CHART=… -DCD_A_HERO=… -DCD_MED=1|3 -DCD_PCT=… -DCD_HOLD=… -DCD_HEAD=… -DCD_WDB=… at build; in the SIM live via
 *         ctrl {"cmd":"cond", ...}; DIAGNOSE → GRAPH switches to the raw (unconditioned) series + values — this session only. */
#ifndef CD_A_CHART
#define CD_A_CHART 0.18f
#endif
#ifndef CD_A_HERO
#define CD_A_HERO 0.25f
#endif
#ifndef CD_MED
#define CD_MED 3
#endif
#ifndef CD_HOLD
#define CD_HOLD 6
#endif
#ifndef CD_PCT
#define CD_PCT 90
#endif
#ifndef CD_HEAD
#define CD_HEAD 1.15f
#endif
#ifndef CD_WDB
#define CD_WDB 2.5f          /* weight deadband: |weight| < 0.25 g is shown as 0 (user's request) — display only */
#endif
#ifndef CD_GATE
#define CD_GATE 3            /* pulse gate (chart pressure only): median > 2 x shown + CD_GATE_ABS is held for up to 3 samples (0.3 s) */
#endif
#define CD_GATE_ABS 400.f
#define CD_W_JUMP 20.f       /* weight series: median jump > 2.0 g in one sample = cup off/on -> snap (noise ±0.2 g; a brew rises approx. 0.2 g/sample) */
#define CD_HERO_JUMP 150     /* hero: median jump > 15.0 units (g / °) in 0.1 s = a real step (cup off/on) -> snap, no gliding */
enum { CS_HOLD=0, CS_PCT, CS_MAX };
static struct { float a_chart,a_hero,head; int med,hold,pct,scale,raw,gate; } cd={CD_A_CHART,CD_A_HERO,CD_HEAD,CD_MED,CD_HOLD,CD_PCT,CS_PCT,0,CD_GATE};
#if defined(MERAKIPLUS_SIM)||defined(MERAKIPLUS_PREVIEW)
static const char*const CS_KEY[]={"hold","pct","max"};     /* ctrl "cond" + state JSON (SIM/preview only) */
#endif
/* the weight deadband (display only; the RAW view from DIAGNOSE has no deadband) */
static float wdb(float v){ return (cd.raw||fabsf(v)>=CD_WDB)?v:0.f; }
static int wdbi(int v){ return (int)wdb((float)v); }
typedef struct { int h[3],m,fast,held; float y; } Cond;  /* the 3 latest raw samples, latest median, median-follow counter, gate counter, the EMA value */
static int med3(int a,int b,int c){ int lo=a<b?a:b, hi=a<b?b:a; return c<lo?lo:(c>hi?hi:c); }
static void cond_snap(Cond*c,int x){ c->h[0]=c->h[1]=c->h[2]=c->m=x; c->y=(float)x; c->fast=0; c->held=0; }
/* activity switch in the chart: the next CD_FAST samples follow median-3 DIRECTLY (no EMA smearing of the step), but a
 * single pulse sample right at the switch does not get through (a plain snap to the raw value would take the pulse along and let it
 * decay slowly — that is what v6.9-wip looked like when a brew ended in a deadhead pulse) */
#define CD_FAST 2
static void cond_rearm(Cond*c){ c->fast=CD_FAST; c->held=0; }
static float cond_stepg(Cond*c,int x,float a,int gate){
    c->h[0]=c->h[1]; c->h[1]=c->h[2]; c->h[2]=x;
    c->m=cd.med>=3?med3(c->h[0],c->h[1],c->h[2]):x;
    if(c->fast>0){ c->fast--; c->y=(float)c->m; return c->y; }
    if((gate&1)&&cd.gate>0&&(float)c->m>c->y*2.f+CD_GATE_ABS){ if(c->held<cd.gate){ c->held++; return c->y; } }  /* pulse gate: hold */
    else c->held=0;
    if((gate&2)&&fabsf((float)c->m-c->y)>CD_W_JUMP){ c->y=(float)c->m; return c->y; }      /* weight: real step (cup off/on) -> snap */
    c->y+=a*((float)c->m-c->y);
    if(fabsf((float)c->m-c->y)<0.5f) c->y=(float)c->m;      /* the tail lands exactly (otherwise an EMA creeps forever) */
    return c->y;
}
static float cond_step(Cond*c,int x,float a){ return cond_stepg(c,x,a,0); }

/* ================= history (60 s @ 10 Hz) ================= */
#define GN 640
static ms_t g_t[GN]; static int g_p[GN], g_w[GN], g_m[GN];   /* RAW — the only thing control/tests/DIAGNOSE read */
static unsigned char g_a[GN];                                  /* v6.9: sample taken during brew/pre-inf./water (that is where the pressure curve belongs) */
static float g_pd[GN], g_wd[GN], g_md[GN];                    /* v6.9: conditioned (for drawing only) */
static int g_n=0,g_head=0;
static Cond cf_p,cf_w,cf_m; static int cf_ph=-1, g_lsrc=39;
/* v6.15: the graph starts over when an activity starts (display only). g_prim = the main phase (phase_main) of the latest ONLINE
 * sample was active; OFFLINE samples do not count as idle, so a link gap during a brew does not wipe the brew on reconnect.
 * gr.t0 = start of the current activity's curve; resets = idle -> active clears; reanchors = re-clears within G_REANCHOR_MS. */
#define G_REANCHOR_MS 1500
static int g_prim=0;
static struct { int resets,reanchors; ms_t t0; } gr={0,0,-100000};
static void graph_clear(void){ g_n=0; g_head=0; cf_ph=-1; }   /* empty ring + the next sample snaps all three chart filters */
#ifdef MERAKIPLUS_PREVIEW
static int gr_off=0;                                           /* preview/selftest only: 1 = v6.14 behaviour (no reset) for the before/after proof */
#define GR_ON (!gr_off)
#else
#define GR_ON 1
#endif
/* src = the weight word (38/39), chosen by the latched source (wsrc_tick). The weight is read SIGNED (int16). The filters snap to
 * the raw value when the ACTIVITY (grind/pre-inf./extraction/water/steam vs. idle) or the source changes — not on switches between
 * the idle phases (READY/HEATING/SLEEPING …), so threshold flicker at rest does not reset the smoothing. */
static void graph_push(ms_t t,int p,int src){
    int w=(int)(int16_t)reg[src], m=reg[48], pm=phase_main(t), key=(phase_active(pm)?pm:0)<<8|src;
    if(GR_ON&&phase_active(pm)){
        if(!g_prim){ graph_clear(); gr.t0=t; gr.resets++; }                  /* v6.15: idle -> active: the curve starts from the tare */
        else if(g_n>0&&t>=gr.t0&&t-gr.t0<G_REANCHOR_MS&&(src!=g_lsrc||(float)w<(float)g_w[(g_head+GN-1)%GN]-CD_W_JUMP)){
            graph_clear(); gr.reanchors++; }                                     /* late source latch / late tare right after the start */
    }
    if(pm!=PH_OFFLINE) g_prim=phase_active(pm);
    if(cf_ph<0){ cond_snap(&cf_p,p); cond_snap(&cf_w,w); cond_snap(&cf_m,m); cf_p.fast=cf_w.fast=cf_m.fast=CD_FAST+1; }   /* first sample (possibly a pulse: gone again after 2 samples, no tail) */
    else if(key!=cf_ph){ cond_rearm(&cf_p); cond_rearm(&cf_m);                                         /* activity switch: no smearing */
        if(src!=g_lsrc) cond_snap(&cf_w,w); else cond_rearm(&cf_w); }                                 /* source switch: a DIFFERENT weight word — no history to carry over */
    cf_ph=key; g_lsrc=src; g_a[g_head]=(unsigned char)(pm==PH_PRE||pm==PH_EXT||pm==PH_WATER);
    g_t[g_head]=t; g_p[g_head]=p; g_w[g_head]=w; g_m[g_head]=m;
    g_pd[g_head]=cond_stepg(&cf_p,p,cd.a_chart,1); g_wd[g_head]=cond_stepg(&cf_w,w,cd.a_chart,2); g_md[g_head]=cond_step(&cf_m,m,cd.a_chart);
    g_head=(g_head+1)%GN; if(g_n<GN)g_n++;
}
static struct { Cond f; int key,set,ok,n_snap; ms_t t; } hf;   /* the hero filter (hero_v() near the HOME model) */
#if defined(MERAKIPLUS_SIM)||defined(MERAKIPLUS_PREVIEW)
static void cond_reset(void){ cf_ph=-1; }                     /* the next sample snaps all three chart filters (ctrl "cond" / sim_reset) */
#endif
/* latest conditioned pressure (gauge + BAR field during brew); raw when the view is set to RAW or the history is empty/stale */
static float p_disp(ms_t now){
    if(cd.raw||g_n==0) return reg[51];
    int i=(g_head+GN-1)%GN; if(now-g_t[i]>400||now<g_t[i]) return reg[51];
    return g_pd[i];
}
/* latest conditioned weight from source word src (the stat field at rest), w. deadband; raw (s16) when the view is RAW, the history
 * is empty/stale or the latest sample came from the other weight word */
static int w_disp(int src,ms_t now){
    int raw=(int)(int16_t)reg[src];
    if(cd.raw||g_n==0||g_lsrc!=src) return wdbi(raw);
    int i=(g_head+GN-1)%GN; if(now-g_t[i]>400||now<g_t[i]) return wdbi(raw);
    return (int)lroundf(wdb(g_wd[i]));
}

/* backlight live preview (device only; mapping duty=100-7v verified in WRITETEST.md) */
static int bl_enabled=1;
#ifdef SIM_RT
static int sim_bl=-1;                                      /* latest brightness preview (for state JSON only; sysfs is NEVER written in SIM) */
#endif
static void bl_set(int v){
#ifdef DEVICE_RT
    if(!bl_enabled) return;
    int duty=100-7*v; if(duty<1)duty=1; if(duty>99)duty=99;
    int fd=open(BL_SYSFS,O_WRONLY); if(fd<0) return;
    char b[8]; int n=snprintf(b,sizeof b,"%d",duty); if(write(fd,b,n)<0){} close(fd);
#elif defined(SIM_RT)
    sim_bl=v;
#else
    (void)v;
#endif
}
/* v6.14: backlight fully OFF / ON (the power button). Stock powerOff 0x1490C0: uikit_os_back_light_off (= pwm2/enable 0,
 * 0xC2278) and then blpwm_set_dutycycle(2,0). merakiplus writes duty 99 (= dimmest, mapping duty = 100 - 7v) BEFORE enable 0: with
 * "polarity inversed", stock's duty 0 is the BRIGHTEST light if enable 0 should fail to take effect. ON: duty from config's
 * brightness and then enable 1 (stock: app restart -> uikit_os_back_light_set + back_light_on 0xC2298). bl_is_off is the ground truth
 * for the exit paths: a merakiplus process that exits with the light off turns it back on (otherwise the stock UI would be dark after run.sh). */
static int bl_is_off=0;
static void bl_power(int on){
    bl_is_off=!on;
#ifdef DEVICE_RT
    if(!bl_enabled) return;
    int fd;
    if(on){ bl_set(SET[S_BL].val); fd=open(BL_EN_SYSFS,O_WRONLY); if(fd>=0){ if(write(fd,"1",1)<0){} close(fd); } }
    else { fd=open(BL_SYSFS,O_WRONLY); if(fd>=0){ if(write(fd,"99",2)<0){} close(fd); }
           fd=open(BL_EN_SYSFS,O_WRONLY); if(fd>=0){ if(write(fd,"0",1)<0){} close(fd); } }
#endif
}

/* ====================================================================== */
/* ================= GRAPHICS: canvas, circle clip, AA primitives ====== */
/* ====================================================================== */
typedef struct { short x0,y0,x1,y1; } Rect;      /* [x0,x1) x [y0,y1) */
#define RL_MAX 16
typedef struct { int n; Rect r[RL_MAX]; } RL;
static const Rect FULL={0,0,W,H};

static uint32_t *canvas,*bgpure,*bgbase,*slay,*snapA,*snapB,*tbuf,*cv;
static Rect clipr={0,0,W,H};
static int cx0[H], cx1[H];   /* chord mask: safe x interval per row (GLOBAL CIRCLE CLIP) */

static void clip_init(void){
    for(int y=0;y<H;y++){
        int dy=y-CY;
        if(dy<=-RSAFE||dy>=RSAFE){ cx0[y]=1; cx1[y]=0; continue; }
        int h=(int)sqrt((double)(RSAFE*RSAFE-dy*dy));
        cx0[y]=CX-h; cx1[y]=CX+h;
    }
}
static inline uint32_t rgb(int r,int g,int b){ return (uint32_t)b | ((uint32_t)g<<8) | ((uint32_t)r<<16) | 0xFF000000u; }
static inline uint32_t mix(uint32_t d,uint32_t s,int a){
    a+=a>>7;
    uint32_t rb=(((d&0xFF00FF)*(256-a)+(s&0xFF00FF)*a)>>8)&0xFF00FF;
    uint32_t g =(((d&0x00FF00)*(256-a)+(s&0x00FF00)*a)>>8)&0x00FF00;
    return 0xFF000000u|rb|g;
}
static inline uint32_t lerpc(uint32_t a,uint32_t b,float t){ if(t<=0) return a; if(t>=1) return b; return mix(a,b,(int)(t*255)); }
static inline float clampf(float v,float lo,float hi){ return v<lo?lo:(v>hi?hi:v); }
static void set_clip(Rect r){ clipr=r; if(clipr.x0<0)clipr.x0=0; if(clipr.y0<0)clipr.y0=0; if(clipr.x1>W)clipr.x1=W; if(clipr.y1>H)clipr.y1=H; }
static Rect R(int x,int y,int w,int h){ Rect r={(short)x,(short)y,(short)(x+w),(short)(y+h)}; return r; }

/* pset: ALL primitives end here or in hspan -> nothing outside the circle, ever */
static inline void pset(int x,int y,uint32_t c,int a){
    if(a<=0||y<clipr.y0||y>=clipr.y1||x<clipr.x0||x>=clipr.x1) return;
    if(x<cx0[y]||x>cx1[y]) return;
    uint32_t*p=&cv[y*W+x]; *p = a>=255? c : mix(*p,c,a);
}
static void hspan(int y,int x0,int x1,uint32_t c,int a){
    if(a<=0||y<clipr.y0||y>=clipr.y1) return;
    if(x0<clipr.x0)x0=clipr.x0; if(x1>clipr.x1)x1=clipr.x1;
    if(x0<cx0[y])x0=cx0[y]; if(x1>cx1[y]+1)x1=cx1[y]+1;
    uint32_t*p=cv+y*W;
    if(a>=255) for(int x=x0;x<x1;x++) p[x]=c;
    else for(int x=x0;x<x1;x++) p[x]=mix(p[x],c,a);
}
static void fill_rect(int x,int y,int w,int h,uint32_t c,int a){ for(int j=0;j<h;j++) hspan(y+j,x,x+w,c,a); }
/* rounded rectangle w. AA corners and vertical gradient ct->cb */
static void rrect_g(int x,int y,int w,int h,float r,uint32_t ct,uint32_t cb,int a){
    if(w<=0||h<=0) return;
    if(r>h/2.f) r=h/2.f; if(r>w/2.f) r=w/2.f; int ri=(int)ceilf(r);
    for(int j=0;j<h;j++){
        int yy=y+j; if(yy<clipr.y0||yy>=clipr.y1) continue;
        uint32_t c=(ct==cb||h<2)? ct : lerpc(ct,cb,(float)j/(h-1));
        float dy=0; if(j<ri) dy=r-0.5f-j; else if(j>=h-ri) dy=j+0.5f-(h-r);
        if(dy<=0){ hspan(yy,x,x+w,c,a); continue; }
        for(int i=0;i<ri;i++){
            float dx=r-0.5f-i; float cov=r+0.5f-sqrtf(dx*dx+dy*dy);
            if(cov<=0) continue; if(cov>1)cov=1; int aa=(int)(a*cov);
            pset(x+i,yy,c,aa); pset(x+w-1-i,yy,c,aa);
        }
        hspan(yy,x+ri,x+w-ri,c,a);
    }
}
static void rrect(int x,int y,int w,int h,float r,uint32_t c,int a){ rrect_g(x,y,w,h,r,c,c,a); }
static void disc(float cx,float cy,float r,uint32_t c,int a){
    int y0=(int)floorf(cy-r-1), y1=(int)ceilf(cy+r+1);
    for(int y=y0;y<y1;y++){
        if(y<clipr.y0||y>=clipr.y1) continue;
        float dy=y+0.5f-cy; float hh=(r+1)*(r+1)-dy*dy; if(hh<=0) continue;
        float hx=sqrtf(hh); int xa=(int)floorf(cx-hx), xb=(int)ceilf(cx+hx);
        float in=(r-1)*(r-1)-dy*dy; int ia=xb, ib=xb;
        if(in>0){ float ix=sqrtf(in); ia=(int)ceilf(cx-ix); ib=(int)floorf(cx+ix); }
        for(int x=xa;x<xb;x++){
            if(x>=ia&&x<ib){ hspan(y,ia,ib,c,a); x=ib-1; continue; }
            float dx=x+0.5f-cx; float cov=r+0.5f-sqrtf(dx*dx+dy*dy);
            if(cov<=0) continue; if(cov>1)cov=1; pset(x,y,c,(int)(a*cov));
        }
    }
}
/* annulus r0..r1 (AA) */
static void ring(float cx,float cy,float r0,float r1,uint32_t c,int a){
    int y0=(int)floorf(cy-r1-1), y1=(int)ceilf(cy+r1+1);
    for(int y=y0;y<y1;y++){
        if(y<clipr.y0||y>=clipr.y1) continue;
        float dy=y+0.5f-cy; float hh=(r1+1)*(r1+1)-dy*dy; if(hh<=0) continue;
        float hx=sqrtf(hh); int xa=(int)floorf(cx-hx), xb=(int)ceilf(cx+hx);
        float in=(r0-1)*(r0-1)-dy*dy; int ia=xb, ib=xb;
        if(r0>1&&in>0){ float ix=sqrtf(in); ia=(int)ceilf(cx-ix); ib=(int)floorf(cx+ix); }
        for(int x=xa;x<xb;x++){
            if(x>=ia&&x<ib){ x=ib-1; continue; }
            float dx=x+0.5f-cx; float d=sqrtf(dx*dx+dy*dy);
            float cov=fminf(d-r0+0.5f,r1-d+0.5f);
            if(cov<=0) continue; if(cov>1)cov=1; pset(x,y,c,(int)(a*cov));
        }
    }
}
static inline float seg_dist(float px,float py,float x0,float y0,float x1,float y1){
    float vx=x1-x0, vy=y1-y0, wx=px-x0, wy=py-y0;
    float l2=vx*vx+vy*vy; float t=l2>0? (wx*vx+wy*vy)/l2 : 0; if(t<0)t=0; if(t>1)t=1;
    float dx=wx-t*vx, dy=wy-t*vy; return sqrtf(dx*dx+dy*dy);
}
/* thick AA line w. round caps */
static void capsule(float x0,float y0,float x1,float y1,float hw,uint32_t c,int a){
    int xa=(int)floorf(fminf(x0,x1)-hw-1), xb=(int)ceilf(fmaxf(x0,x1)+hw+1);
    int ya=(int)floorf(fminf(y0,y1)-hw-1), yb=(int)ceilf(fmaxf(y0,y1)+hw+1);
    if(ya<clipr.y0)ya=clipr.y0; if(yb>clipr.y1)yb=clipr.y1; if(xa<clipr.x0)xa=clipr.x0; if(xb>clipr.x1)xb=clipr.x1;
    for(int y=ya;y<yb;y++) for(int x=xa;x<xb;x++){
        float cov=hw+0.5f-seg_dist(x+0.5f,y+0.5f,x0,y0,x1,y1);
        if(cov<=0) continue; if(cov>1)cov=1; pset(x,y,c,(int)(a*cov));
    }
}
/* generic AA arc (degrees, screen coords: 0=right, 90=down), round caps; for small arcs */
static void arc_aa(float cx,float cy,float r,float hw,float a0,float a1,uint32_t c,int a){
    if(a1<=a0) return;
    float sx=cx+r*cosf(a0*(float)M_PI/180), sy=cy+r*sinf(a0*(float)M_PI/180);
    float ex=cx+r*cosf(a1*(float)M_PI/180), ey=cy+r*sinf(a1*(float)M_PI/180);
    float sweep=a1-a0; float ro=r+hw+1;
    int y0=(int)floorf(cy-ro), y1=(int)ceilf(cy+ro);
    for(int y=y0;y<y1;y++){
        if(y<clipr.y0||y>=clipr.y1) continue;
        float dy=y+0.5f-cy; float hh=ro*ro-dy*dy; if(hh<=0) continue;
        float hx=sqrtf(hh); int xa=(int)floorf(cx-hx), xb=(int)ceilf(cx+hx);
        for(int x=xa;x<xb;x++){
            float dx=x+0.5f-cx; float rho=sqrtf(dx*dx+dy*dy);
            if(rho<r-hw-1) continue;
            float d;
            if(sweep>=360) d=fabsf(rho-r);
            else {
                float ang=atan2f(dy,dx)*180/(float)M_PI; float rel=fmodf(ang-a0+720,360);
                if(rel<=sweep) d=fabsf(rho-r);
                else { float d1=hypotf(x+0.5f-sx,y+0.5f-sy), d2=hypotf(x+0.5f-ex,y+0.5f-ey); d=fminf(d1,d2); }
            }
            float cov=hw+0.5f-d; if(cov<=0) continue; if(cov>1)cov=1; pset(x,y,c,(int)(a*cov));
        }
    }
}

/* ================= background: radial vignette w. ordered dither (no banding) ================= */
static void bg_build(void){
    static const uint8_t B4[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    for(int y=0;y<H;y++) for(int x=0;x<W;x++){
        uint32_t*p=&bgpure[y*W+x];
        if(x<cx0[y]||x>cx1[y]){ *p=0xFF000000u; continue; }
        float dx=x+0.5f-CX, dy=y+0.5f-CY; float d=sqrtf(dx*dx+dy*dy)/RSAFE;
        float t=powf(d,1.7f);
        /* slight light offset upwards (soft top highlight) */
        float lift=clampf(0.5f-dy/480.f,0,1)*0.35f;
        float r=(23-17*t)+3*lift*(1-t), g=(25-18*t)+3*lift*(1-t), b=(31-21*t)+4*lift*(1-t);
        float th=(B4[(y&3)*4+(x&3)]+0.5f)/16.f-0.5f;
        int ri=(int)floorf(r+th+0.5f), gi=(int)floorf(g+th+0.5f), bi=(int)floorf(b+th+0.5f);
        *p=rgb(ri<0?0:ri,gi<0?0:gi,bi<0?0:bi);
    }
}

/* ================= STROKE FONT (custom; AA via distance field, glyph cache) =================
 * Glyphs are stroke drawings on a 10-unit capital grid: M x,y | L x,y | E cx,cy,rx,ry,a0,a1.
 * Rasterized per (style, glyph) the first time and cached as 8-bit alpha. Digits are tabular
 * (all digits adv=6). Only capitals + digits + ÆØÅ + symbols/icons — the typography is
 * deliberately all-caps w. letter spacing. */
#define NG 160
#define G_LC 128            /* a..z in code spans (paths/commands): G_LC + (c - 'a') */
enum { G_AE=96, G_OE, G_AA, G_DEG, G_MDOT, G_BEAN, G_CUP, G_DROP, G_STEAM, G_CHECK, G_CROSS, G_MOON, G_WARN, G_APPROX, G_BOLT, G_ELL, G_SECT, G_CLOCK };
typedef struct { float adv; const char*path; } GDef;
static const GDef GD[NG]={
 [' ']={2.8f,""},
 ['0']={6,"M0,3 L0,7 E3,7,3,3,0,180 M6,7 L6,3 E3,3,3,3,180,360"},
 ['1']={6,"M1.6,2.3 L3.9,0 L3.9,10"},
 ['2']={6,"E3,3,3,3,180,405 M5.12,5.12 L0,10 L6,10"},
 ['3']={6,"E3,2.55,2.7,2.55,205,450 E3,7.45,3,2.55,270,515"},
 ['4']={6,"M4.6,10 L4.6,0 L0,7 L6,7"},
 ['5']={6,"M5.4,0 L1,0 L0.6,4.4 E2.9,6.75,3.1,3.25,228,505"},
 ['6']={6,"E3,6.75,3,3.25,0,360 M0,6.75 L0,4.4 E4,4.4,4,4.4,180,282"},
 ['7']={6,"M0,0 L6,0 L2.4,10"},
 ['8']={6,"E3,2.45,2.6,2.45,0,360 E3,7.4,3,2.6,0,360"},
 ['9']={6,"E3,3.25,3,3.25,0,360 M6,3.25 L6,5.6 E2,5.6,4,4.4,0,102"},
 ['A']={6.4f,"M0,10 L3.2,0 L6.4,10 M1.2,6.6 L5.2,6.6"},
 ['B']={6,"M0,0 L0,10 M0,0 L3.2,0 E3.2,2.4,2.4,2.4,270,450 M3.4,4.8 L0,4.8 E3.2,7.4,2.8,2.6,270,450 M3.2,10 L0,10"},
 ['C']={6.2f,"E3.4,5,3.4,5,52,308"},
 ['D']={6,"M0,0 L0,10 M0,0 L2.4,0 E2.4,5,3.6,5,270,450 M2.4,10 L0,10"},
 ['E']={5.4f,"M5.4,0 L0,0 L0,10 L5.4,10 M0,5 L4.6,5"},
 ['F']={5.4f,"M5.4,0 L0,0 L0,10 M0,5 L4.4,5"},
 ['G']={6.8f,"E3.4,5,3.4,5,0,308 M6.8,5 L3.9,5"},
 ['H']={6.2f,"M0,0 L0,10 M6.2,0 L6.2,10 M0,5 L6.2,5"},
 ['I']={0,"M0,0 L0,10"},
 ['J']={4,"M4,0 L4,7.2 E1.9,7.2,2.1,2.8,0,165"},
 ['K']={6,"M0,0 L0,10 M5.8,0 L0,6.4 M2.2,4.3 L6,10"},
 ['L']={5,"M0,0 L0,10 L5,10"},
 ['M']={8,"M0,10 L0,0 L4,7.4 L8,0 L8,10"},
 ['N']={6.2f,"M0,10 L0,0 L6.2,10 L6.2,0"},
 ['O']={7,"E3.5,5,3.5,5,0,360"},
 ['P']={6,"M0,10 L0,0 L3.2,0 E3.2,2.8,2.8,2.8,270,450 M3.2,5.6 L0,5.6"},
 ['Q']={7,"E3.5,5,3.5,5,0,360 M4.4,7.4 L7,10.6"},
 ['R']={6,"M0,10 L0,0 L3.2,0 E3.2,2.8,2.8,2.8,270,450 M3.2,5.6 L0,5.6 M3,5.6 L6,10"},
 ['S']={6,"E3,2.55,2.8,2.55,90,325 E3,7.45,3,2.55,270,505"},
 ['T']={6.4f,"M0,0 L6.4,0 M3.2,0 L3.2,10"},
 ['U']={6.2f,"M0,0 L0,6.8 E3.1,6.8,3.1,3.2,0,180 M6.2,6.8 L6.2,0"},
 ['V']={6.4f,"M0,0 L3.2,10 L6.4,0"},
 ['W']={9.2f,"M0,0 L2.3,10 L4.6,2.4 L6.9,10 L9.2,0"},
 ['X']={6.2f,"M0,0 L6.2,10 M6.2,0 L0,10"},
 ['Y']={6.2f,"M0,0 L3.1,5.2 L6.2,0 M3.1,5.2 L3.1,10"},
 ['Z']={6,"M0,0 L6,0 L0,10 L6,10"},
 [G_AE]={8.6f,"M0,10 L4.2,0 L8.6,0 M4.5,0 L4.5,10 L8.6,10 M4.5,5 L7.8,5 M1.7,6.6 L4.5,6.6"},
 [G_OE]={7,"E3.5,5,3.5,5,0,360 M0.6,9.9 L6.4,0.1"},
 [G_AA]={6.4f,"M0,10 L3.2,2.8 L6.4,10 M1.3,7.3 L5.1,7.3 E3.2,0.6,1.1,1.1,0,360"},
 ['.']={0,"M0,9.9 L0,10"},
 [',']={0.3f,"M0.3,9.8 L-0.4,11.6"},
 [':']={0,"M0,3.2 L0,3.3 M0,9.9 L0,10"},
 ['-']={4,"M0.3,5.4 L3.7,5.4"},
 ['+']={5,"M0,5.4 L5,5.4 M2.5,2.9 L2.5,7.9"},
 ['/']={3.8f,"M0,10.4 L3.8,-0.4"},
 ['%']={7.2f,"M0.6,10 L6.6,0 E1.5,2.1,1.5,2.1,0,360 E5.7,7.9,1.5,2.1,0,360"},
 ['*']={4,"M2,0.2 L2,4.2 M0.2,1.2 L3.8,3.2 M3.8,1.2 L0.2,3.2"},
 ['(']={1.7f,"E3.4,5,3.2,6.4,125,235"},
 [')']={1.7f,"E-1.7,5,3.2,6.4,-55,55"},
 ['<']={3.6f,"M3.6,1.4 L0,5 L3.6,8.6"},
 ['>']={3.6f,"M0,1.4 L3.6,5 L0,8.6"},
 ['!']={0,"M0,0 L0,6.6 M0,9.9 L0,10"},
 ['?']={5,"E2.5,2.6,2.5,2.6,180,450 M2.5,5.2 L2.5,6.6 M2.5,9.9 L2.5,10"},
 ['=']={5,"M0,3.6 L5,3.6 M0,7 L5,7"},
 ['_']={5,"M0,10 L5,10"},
 [G_DEG]={2.8f,"E1.4,1.5,1.4,1.5,0,360"},
 [G_MDOT]={0,"M0,5 L0,5.1"},
 [G_APPROX]={5,"E1.25,4,1.25,1,180,360 E3.75,4,1.25,1,0,180 E1.25,7,1.25,1,180,360 E3.75,7,1.25,1,0,180"},
 /* icons (10x10 box) */
 [G_BEAN]={10,"E5,5,3.7,4.9,0,360 E3.7,2.7,1.5,2.4,-72,72 E6.3,7.3,1.5,2.4,108,252"},
 [G_CUP]={10.2f,"M1,3 L1,5.6 E4.6,5.6,3.6,3.4,0,180 M8.2,5.6 L8.2,3 M0.2,3 L9,3 E8.6,4.9,1.5,1.5,280,440 M2.2,10 L7,10"},
 [G_DROP]={10,"M5,0.4 L2.25,4.58 M5,0.4 L7.75,4.58 E5,6.4,3.3,3.3,326.6,573.4"},
 [G_STEAM]={10,"E2,2.75,1.1,2.25,90,270 E2,7.25,1.1,2.25,270,450 E5,2.75,1.1,2.25,90,270 E5,7.25,1.1,2.25,270,450 E8,2.75,1.1,2.25,90,270 E8,7.25,1.1,2.25,270,450"},
 [G_CHECK]={10,"M0.5,5.4 L3.6,8.6 L9.5,1.6"},
 [G_CROSS]={10,"M1.2,1.2 L8.8,8.8 M8.8,1.2 L1.2,8.8"},
 [G_MOON]={8,"E5,5,4.5,4.5,60,300 E10,5,4.77,4.77,125.2,234.8"},
 [G_WARN]={10,"M5,0.8 L9.6,9.2 L0.4,9.2 L5,0.8 M5,3.8 L5,6.2 M5,7.9 L5,8"},
 [G_BOLT]={6,"M4,0 L0.4,5.6 L3.2,5.6 L2,10 L5.6,4.2 L2.9,4.2 L4,0"},
 [G_ELL]={5.6f,"M0,9.9 L0,10 M2.8,9.9 L2.8,10 M5.6,9.9 L5.6,10"},
 ['$']={6,"E3,2.55,2.8,2.55,90,325 E3,7.45,3,2.55,270,505 M3,-1.3 L3,11.3"},
 [G_CLOCK]={10,"E5,5,4.8,4.8,0,360 M5,1.9 L5,5 L7.3,6.4"},   /* v6.16 */
 [G_SECT]={5.6f,"E2.8,1.8,2.4,1.8,95,325 E2.8,5,2.7,1.6,0,360 E2.8,8.2,2.4,1.8,275,505"},
 ['\'']={0,"M0,0 L0,2.6"},
 /* lowercase letters (x-height 3.6..10, descender to 12.8) — ONLY in code spans (LC ... UC):
  * the shell is case-sensitive, so paths and commands are shown as they must be typed. */
 [G_LC+0] ={5.2f,"E2.6,6.8,2.6,3.2,0,360 M5.2,3.6 L5.2,10"},
 [G_LC+1] ={5.2f,"M0,0 L0,10 E2.6,6.8,2.6,3.2,0,360"},
 [G_LC+2] ={4.7f,"E2.6,6.8,2.6,3.2,52,308"},
 [G_LC+3] ={5.2f,"E2.6,6.8,2.6,3.2,0,360 M5.2,0 L5.2,10"},
 [G_LC+4] ={5.2f,"E2.6,6.8,2.6,3.2,48,360 M5.2,6.8 L0,6.8"},
 [G_LC+5] ={3.6f,"M1.3,10 L1.3,2.2 E3.3,2.2,2,2.2,180,285 M0,3.8 L3.3,3.8"},
 [G_LC+6] ={5.2f,"E2.6,6.8,2.6,3.2,0,360 M5.2,3.6 L5.2,10.4 E2.6,10.4,2.6,2.4,0,150"},
 [G_LC+7] ={5,"M0,0 L0,10 E2.5,6.1,2.5,2.5,180,360 M5,6.1 L5,10"},
 [G_LC+8] ={0,"M0,3.6 L0,10 M0,0.7 L0,0.8"},
 [G_LC+9] ={1.6f,"M1.6,3.6 L1.6,10.8 E-0.2,10.8,1.8,2,0,120 M1.6,0.7 L1.6,0.8"},
 [G_LC+10]={4.6f,"M0,0 L0,10 M4.3,3.6 L0,7.7 M1.6,6.2 L4.6,10"},
 [G_LC+11]={0,"M0,0 L0,10"},
 [G_LC+12]={7.6f,"M0,3.6 L0,10 E1.9,5.6,1.9,2,180,360 M3.8,5.6 L3.8,10 E5.7,5.6,1.9,2,180,360 M7.6,5.6 L7.6,10"},
 [G_LC+13]={5,"M0,3.6 L0,10 E2.5,6.1,2.5,2.5,180,360 M5,6.1 L5,10"},
 [G_LC+14]={5.2f,"E2.6,6.8,2.6,3.2,0,360"},
 [G_LC+15]={5.2f,"M0,3.6 L0,12.8 E2.6,6.8,2.6,3.2,0,360"},
 [G_LC+16]={5.2f,"E2.6,6.8,2.6,3.2,0,360 M5.2,3.6 L5.2,12.8"},
 [G_LC+17]={3.4f,"M0,3.6 L0,10 E2.4,6,2.4,2.4,180,300"},
 [G_LC+18]={4.5f,"E2.25,5.23,2.1,1.63,90,325 E2.25,8.37,2.25,1.63,270,505"},
 [G_LC+19]={3.5f,"M1.3,1.2 L1.3,8 E3.3,8,2,2,90,180 M0,3.8 L3.4,3.8"},
 [G_LC+20]={5,"M0,3.6 L0,7.5 E2.5,7.5,2.5,2.5,0,180 M5,3.6 L5,10"},
 [G_LC+21]={5,"M0,3.6 L2.5,10 L5,3.6"},
 [G_LC+22]={7.6f,"M0,3.6 L1.9,10 L3.8,5 L5.7,10 L7.6,3.6"},
 [G_LC+23]={4.8f,"M0,3.6 L4.8,10 M4.8,3.6 L0,10"},
 [G_LC+24]={5,"M0,3.6 L2.5,10 M5,3.6 L1.4,12.8"},
 [G_LC+25]={4.6f,"M0,3.6 L4.6,3.6 L0,10 L4.6,10"},
};
typedef struct { float cap, stroke, gap, track; } FStyle;
enum { F_XS=0, F_S, F_M, F_MB, F_L, F_XL, F_XXL, F_IC, F_ICL, F_H, NFONT };
static const FStyle FS[NFONT]={
 [F_XS] ={11, 1.55f,0.20f,1.3f},
 [F_S]  ={14, 1.95f,0.19f,1.0f},
 [F_M]  ={18, 2.4f, 0.18f,0.6f},
 [F_MB] ={18, 3.0f, 0.18f,0.6f},
 [F_L]  ={26, 3.4f, 0.16f,0.5f},
 [F_XL] ={44, 5.2f, 0.13f,0},
 [F_XXL]={66, 7.2f, 0.11f,0},
 [F_IC] ={20, 2.1f, 0.10f,0},
 [F_ICL]={30, 2.8f, 0.10f,0},
 [F_H]  ={34, 4.3f, 0.14f,0.2f},     /* v6.2: word hero between −/+ (MERAKIPLUS / ORIGINAL) */
};
#define GSEG_MAX 128
static float *gseg[NG]; static int gnseg[NG]; static char gparsed[NG];
typedef struct { uint8_t*a; short w,h,ox,oy; float adv; char st; } GBmp;   /* st: 0=not built, 1=ok, 2=empty */
static GBmp gcache[NFONT][NG];

static void glyph_parse(int g){
    gparsed[g]=1; const char*p=GD[g].path; if(!p||!*p){ gnseg[g]=0; return; }
    float buf[4*GSEG_MAX]; int n=0; float px=0,py=0;
    while(*p){
        while(*p==' ')p++;
        char cmd=*p; if(!cmd) break; p++;
        float v[6]; int nv=(cmd=='E')?6:2;
        for(int k=0;k<nv;k++){ char*e; v[k]=strtof(p,&e); p=e; if(*p==',')p++; }
        if(cmd=='M'){ px=v[0]; py=v[1]; }
        else if(cmd=='L'){ if(n<GSEG_MAX){ buf[4*n]=px; buf[4*n+1]=py; buf[4*n+2]=v[0]; buf[4*n+3]=v[1]; n++; } px=v[0]; py=v[1]; }
        else if(cmd=='E'){
            int ns=(int)ceilf(fabsf(v[5]-v[4])/12.f); if(ns<3)ns=3;
            float ax=v[0]+v[2]*cosf(v[4]*(float)M_PI/180), ay=v[1]+v[3]*sinf(v[4]*(float)M_PI/180);
            for(int k=1;k<=ns;k++){
                float ang=(v[4]+(v[5]-v[4])*k/ns)*(float)M_PI/180;
                float bx=v[0]+v[2]*cosf(ang), by=v[1]+v[3]*sinf(ang);
                if(n<GSEG_MAX){ buf[4*n]=ax; buf[4*n+1]=ay; buf[4*n+2]=bx; buf[4*n+3]=by; n++; }
                ax=bx; ay=by;
            }
        }
    }
    gnseg[g]=n; gseg[g]=malloc(sizeof(float)*4*n); memcpy(gseg[g],buf,sizeof(float)*4*n);
}
static GBmp* glyph_get(int f,int g){
    GBmp*b=&gcache[f][g]; if(b->st) return b;
    if(!gparsed[g]) glyph_parse(g);
    const FStyle*fs=&FS[f]; float hw=fs->stroke/2, s=(fs->cap-fs->stroke)/10.f;
    b->adv=GD[g].adv*s+fs->stroke+fs->cap*fs->gap+fs->track;
    int n=gnseg[g]; if(n==0){ b->st=2; if(g==' ') b->adv=GD[g].adv*s+fs->track; return b; }
    float sg[4*GSEG_MAX]; float mnx=1e9f,mny=1e9f,mxx=-1e9f,mxy=-1e9f;
    for(int k=0;k<4*n;k++){ sg[k]=hw+gseg[g][k]*s; if(k&1){ if(sg[k]<mny)mny=sg[k]; if(sg[k]>mxy)mxy=sg[k]; } else { if(sg[k]<mnx)mnx=sg[k]; if(sg[k]>mxx)mxx=sg[k]; } }
    int x0=(int)floorf(mnx-hw-1), x1=(int)ceilf(mxx+hw+1), y0=(int)floorf(mny-hw-1), y1=(int)ceilf(mxy+hw+1);
    b->w=(short)(x1-x0); b->h=(short)(y1-y0); b->ox=(short)x0; b->oy=(short)y0;
    b->a=calloc(1,(size_t)b->w*b->h);
    float rj=hw+1.f;
    for(int k=0;k<n;k++){
        float ax=sg[4*k],ay=sg[4*k+1],bx=sg[4*k+2],by=sg[4*k+3];
        int ia=(int)floorf(fminf(ax,bx)-rj)-x0, ib=(int)ceilf(fmaxf(ax,bx)+rj)-x0;
        int ja=(int)floorf(fminf(ay,by)-rj)-y0, jb=(int)ceilf(fmaxf(ay,by)+rj)-y0;
        if(ia<0)ia=0; if(ja<0)ja=0; if(ib>b->w)ib=b->w; if(jb>b->h)jb=b->h;
        for(int j=ja;j<jb;j++) for(int i=ia;i<ib;i++){
            float cov=hw+0.5f-seg_dist(x0+i+0.5f,y0+j+0.5f,ax,ay,bx,by);
            if(cov<=0) continue; if(cov>1)cov=1;
            int v=(int)(cov*255); uint8_t*q=&b->a[j*b->w+i]; if(v>*q)*q=(uint8_t)v;
        }
    }
    b->st=1; return b;
}
/* UTF-8 -> glyph id (uppercasing; ÆØÅ, °, ·, ≈; \x01.. = icons).
 * LC/UC (\x0e/\x0f) open/close a code span in which a..z are NOT uppercased; they take up
 * no space themselves (return -1). The state is reset by text_wf()/text_at() for each string. */
static int g_lc=0;
static int glyph_next(const unsigned char**pp){
    const unsigned char*p=*pp; int c=*p++;
    if(c<0x80){
        *pp=p;
        if(c==0x0E){ g_lc=1; return -1; }
        if(c==0x0F){ g_lc=0; return -1; }
        if(c>='a'&&c<='z'){ if(g_lc) return G_LC+(c-'a'); c-=32; }
        if(c>=1&&c<=9){ static const int ic[10]={0,G_BEAN,G_CUP,G_DROP,G_STEAM,G_CHECK,G_CROSS,G_MOON,G_WARN,G_BOLT}; return ic[c]; }
        if(c==0x0B) return G_CLOCK;                       /* v6.16 */
        if(c<32||c>95) return '?';
        return c;
    }
    int g='?';
    if(c==0xC3){ int d=*p++; if(d==0x86||d==0xA6) g=G_AE; else if(d==0x98||d==0xB8) g=G_OE; else if(d==0x85||d==0xA5) g=G_AA; else if(d==0x97) g='X'; }
    else if(c==0xC2){ int d=*p++; if(d==0xB0) g=G_DEG; else if(d==0xB7) g=G_MDOT; else if(d==0xA7) g=G_SECT; }
    else if(c==0xE2){ int d=*p++; int e=*p++; if(d==0x89&&e==0x88) g=G_APPROX; else if(d==0x80&&(e==0x93||e==0x94)) g='-'; else if(d==0x80&&e==0xA6) g=G_ELL; else if(d==0x86&&e==0x92) g='>'; }
    else { while((*p&0xC0)==0x80) p++; }
    *pp=p; return g;
}
static float text_wf(const char*s,int f){
    const unsigned char*p=(const unsigned char*)s; float w=0,last=0; g_lc=0;
    while(*p){ int g=glyph_next(&p); if(g<0) continue; GBmp*b=glyph_get(f,g); w+=b->adv; last=FS[f].cap*FS[f].gap+FS[f].track; }
    return w>0? w-last : 0;
}
static int text_w(const char*s,int f){ return (int)ceilf(text_wf(s,f)); }
static void glyph_blit(const GBmp*b,int gx,int gy,uint32_t c,int a){
    for(int j=0;j<b->h;j++){
        int y=gy+j; if(y<clipr.y0||y>=clipr.y1) continue;
        int xa=gx, xb=gx+b->w;
        if(xa<clipr.x0)xa=clipr.x0; if(xb>clipr.x1)xb=clipr.x1;
        if(xa<cx0[y])xa=cx0[y]; if(xb>cx1[y]+1)xb=cx1[y]+1;
        const uint8_t*src=b->a+j*b->w-gx; uint32_t*dst=cv+y*W;
        for(int x=xa;x<xb;x++){ int v=src[x]; if(!v) continue; v=(v*a+127)/255; dst[x]= v>=255? c : mix(dst[x],c,v); }
    }
}
/* text: (x,y) = left edge / cap top */
static float text_at(float x,int y,const char*s,int f,uint32_t c,int a){
    const unsigned char*p=(const unsigned char*)s; g_lc=0;
    while(*p){ int g=glyph_next(&p); if(g<0) continue; GBmp*b=glyph_get(f,g); if(b->st==1) glyph_blit(b,(int)lroundf(x)+b->ox,y+b->oy,c,a); x+=b->adv; }
    return x;
}
static void text_c(int cx,int y,const char*s,int f,uint32_t c,int a){ text_at(cx-text_wf(s,f)/2,y,s,f,c,a); }
static void text_r(int xr,int y,const char*s,int f,uint32_t c,int a){ text_at(xr-text_wf(s,f),y,s,f,c,a); }
/* truncate s (buffer w. >= 4 bytes of headroom) with "…" until it is <= maxw px wide; cuts at UTF-8 boundaries */
static void fit_text(char*s,int f,int maxw){
    if(text_w(s,f)<=maxw) return;
    size_t n=strlen(s);
    while(n>0){ do{ n--; }while(n>0&&((unsigned char)s[n]&0xC0)==0x80); memcpy(s+n,"\xE2\x80\xA6",4); if(text_w(s,f)<=maxw) return; }
}
static void font_prewarm(void){
    static const char*const dig="0123456789.-";
    for(const char*p=dig;*p;p++){ glyph_get(F_XXL,*p); glyph_get(F_XL,*p); glyph_get(F_L,*p); glyph_get(F_M,*p); glyph_get(F_MB,*p); }
}

/* ================= dirty rect lists ================= */
static int r_empty(Rect r){ return r.x1<=r.x0||r.y1<=r.y0; }
static int r_touch(Rect a,Rect b){ return a.x0<=b.x1&&b.x0<=a.x1&&a.y0<=b.y1&&b.y0<=a.y1; }
static int r_isect(Rect a,Rect b){ return a.x0<b.x1&&b.x0<a.x1&&a.y0<b.y1&&b.y0<a.y1; }
static Rect r_union(Rect a,Rect b){ Rect r={a.x0<b.x0?a.x0:b.x0,a.y0<b.y0?a.y0:b.y0,a.x1>b.x1?a.x1:b.x1,a.y1>b.y1?a.y1:b.y1}; return r; }
static void rl_add(RL*l,Rect r){
    if(r.x0<0)r.x0=0; if(r.y0<0)r.y0=0; if(r.x1>W)r.x1=W; if(r.y1>H)r.y1=H;
    if(r_empty(r)) return;
    for(;;){
        int merged=0;
        for(int i=0;i<l->n;i++) if(r_touch(l->r[i],r)){ r=r_union(l->r[i],r); l->r[i]=l->r[--l->n]; merged=1; break; }
        if(!merged) break;
    }
    if(l->n<RL_MAX) l->r[l->n++]=r;
    else l->r[0]=r_union(l->r[0],r);
}
static int rl_hits(const RL*l,Rect r){ for(int i=0;i<l->n;i++) if(r_isect(l->r[i],r)) return 1; return 0; }
static void blit_rect(uint32_t*dst,const uint32_t*src,Rect r){
    if(r.x0<0)r.x0=0; if(r.y0<0)r.y0=0; if(r.x1>W)r.x1=W; if(r.y1>H)r.y1=H;
    if(r_empty(r)) return;
    for(int y=r.y0;y<r.y1;y++) memcpy(dst+y*W+r.x0,src+y*W+r.x0,(size_t)(r.x1-r.x0)*4);
}

/* ================= GAUGE ARC w. LUT (track + glow + ticks) =================
 * The geometry is precomputed per pixel once (angle fraction t, coverage, glow).
 * Per frame, ONLY pixels in the changed angle interval are recomputed, directly in the
 * semi-static layer 'slay' — the changed bbox is reported as damage. */
typedef struct { uint32_t off; float t; short x,y; uint8_t cov,glow,kind; } AEnt;
typedef struct {
    float rm,hw,gw,a0,sweep; int nt; AEnt*e; int n;
    float f_d,f_t,f_drawn;                 /* shown / target / last drawn fill (0..1) */
    uint32_t c_lo,c_hi,d_lo,d_hi;          /* fill colour (target) / last drawn */
    float mark,mark_drawn;                 /* target marker (0..1; <0 = none) */
    int vis,vis_drawn; uint32_t track; Rect mrect;
} Arc;
static Arc garc, parc;                      /* outer gauge + thin progress ring */
#define COL_TICK_MIN rgb(50,55,66)
#define COL_TICK_MAJ rgb(98,106,122)
static void arc_build(Arc*a,float rm,float hw,float gw,float a0,float sweep,int nticks,float tr0,float tr1){
    a->rm=rm; a->hw=hw; a->gw=gw; a->a0=a0; a->sweep=sweep; a->nt=nticks;
    float ro=fmaxf(rm+hw+gw,tr1)+2, ri=rm-hw-gw-2;
    float sx=CX+rm*cosf(a0*(float)M_PI/180), sy=CY+rm*sinf(a0*(float)M_PI/180);
    float ex=CX+rm*cosf((a0+sweep)*(float)M_PI/180), ey=CY+rm*sinf((a0+sweep)*(float)M_PI/180);
    int cap=0; a->n=0; a->e=NULL;
    for(int pass=0;pass<2;pass++){
        int n=0;
        for(int y=0;y<H;y++) for(int x=cx0[y];x<=cx1[y];x++){
            float dx=x+0.5f-CX, dy=y+0.5f-CY; float rho=sqrtf(dx*dx+dy*dy);
            if(rho>ro||rho<ri) continue;
            float ang=atan2f(dy,dx)*180/(float)M_PI; float rel=fmodf(ang-a0+720,360);
            float t,d; int in=rel<=sweep;
            if(in){ t=rel/sweep; d=fabsf(rho-rm); }
            else if(rel>sweep+(360-sweep)/2){ t=-1; d=hypotf(x+0.5f-sx,y+0.5f-sy); }
            else { t=2; d=hypotf(x+0.5f-ex,y+0.5f-ey); }
            float cov=clampf(hw+0.5f-d,0,1), gl=0;
            if(gw>0&&d>hw){ float g=1-(d-hw)/gw; if(g>0) gl=g*g; }
            if(cov>0||gl>0.01f){
                if(pass){ AEnt*e=&a->e[n]; e->off=(uint32_t)(y*W+x); e->t=t; e->x=(short)x; e->y=(short)y; e->cov=(uint8_t)(cov*255); e->glow=(uint8_t)(gl*255); e->kind=0; }
                n++; continue;
            }
            if(nticks>0&&in&&rho>=tr0-1&&rho<=tr1+1){
                float step=sweep/nticks; int k=(int)lroundf(rel/step); int major=(k%5)==0;
                float r0=major?tr0:tr0+2, r1=major?tr1:tr1-2;
                float dpx=fabsf(rel-k*step)*(float)M_PI/180*rho;
                float c=clampf((major?1.0f:0.75f)+0.5f-dpx,0,1)*clampf(fminf(rho-r0+0.5f,r1-rho+0.5f),0,1);
                if(c>0.02f){
                    if(pass){ AEnt*e=&a->e[n]; e->off=(uint32_t)(y*W+x); e->t=k*step/sweep; e->x=(short)x; e->y=(short)y; e->cov=(uint8_t)(c*255); e->glow=0; e->kind=major?2:1; }
                    n++;
                }
            }
        }
        if(!pass){ cap=n; a->e=malloc(sizeof(AEnt)*(cap>0?cap:1)); } else a->n=n;
    }
    a->f_d=a->f_t=0; a->f_drawn=-1; a->mark=a->mark_drawn=-1; a->vis=0; a->vis_drawn=-1; a->track=rgb(34,38,47);
    a->c_lo=a->c_hi=a->d_lo=a->d_hi=rgb(255,255,255);
}
static void arc_pt(const Arc*a,float f,float r,float*x,float*y){
    float ang=(a->a0+a->sweep*f)*(float)M_PI/180; *x=CX+r*cosf(ang); *y=CY+r*sinf(ang);
}
/* draw the changed interval into slay; return the damage bbox in *out (empty if nothing) */
static int arc_paint(Arc*a,int force,Rect*out){
    int all=force||a->vis!=a->vis_drawn||a->c_lo!=a->d_lo||a->c_hi!=a->d_hi||a->mark!=a->mark_drawn||a->f_drawn<0;
    if(!all&&a->f_d==a->f_drawn) return 0;
    float capf=(a->hw+a->gw+2)/(a->rm*a->sweep*(float)M_PI/180);
    float lo=fminf(a->f_d,a->f_drawn)-capf, hi=fmaxf(a->f_d,a->f_drawn)+capf;
    float ex,ey; arc_pt(a,a->f_d,a->rm,&ex,&ey);
    int lit=a->vis&&a->f_d>0.002f;
    Rect bb={W,H,0,0};
    if(all&&!r_empty(a->mrect)){ blit_rect(slay,bgbase,a->mrect); bb=a->mrect; a->mrect=(Rect){0,0,0,0}; }
    for(int i=0;i<a->n;i++){
        const AEnt*e=&a->e[i]; float t=e->t;
        if(!all){ if(t<0){ if(lo>0) continue; } else if(t>1.5f){ if(hi<1) continue; } else if(t<lo||t>hi) continue; }
        uint32_t p=bgbase[e->off];
        if(a->vis){
            if(e->kind==0){
                if(e->cov) p=mix(p,a->track,e->cov);
                if(lit){
                    int lc=0,lg=0; float tt=t;
                    if(t<0){ lc=e->cov; lg=e->glow; tt=0; }
                    else if(t<=a->f_d){ lc=e->cov; lg=e->glow; }
                    else if(t<=a->f_d+capf||t>1.5f){
                        float d=hypotf(e->x+0.5f-ex,e->y+0.5f-ey);
                        float c=clampf(a->hw+0.5f-d,0,1); lc=(int)(c*255); if(lc>e->cov&&t<=1.5f&&e->cov) lc=e->cov;
                        if(a->gw>0&&d>a->hw){ float g=1-(d-a->hw)/a->gw; if(g>0) lg=(int)(g*g*255); }
                        tt=a->f_d;
                    }
                    if(lc||lg){
                        float u=tt/a->f_d; uint32_t col=lerpc(a->c_lo,a->c_hi,0.18f+0.82f*u*u);
                        if(lg) p=mix(p,col,lg*70/255);
                        if(lc) p=mix(p,col,lc);
                    }
                }
            } else {
                int on=lit&&t<=a->f_d+0.001f;
                p=mix(p,on?a->c_hi:(e->kind==2?COL_TICK_MAJ:COL_TICK_MIN),on?e->cov:e->cov);
            }
        }
        slay[e->off]=p;
        if(e->x<bb.x0)bb.x0=e->x; if(e->x+1>bb.x1)bb.x1=e->x+1; if(e->y<bb.y0)bb.y0=e->y; if(e->y+1>bb.y1)bb.y1=e->y+1;
    }
    /* head highlight + target marker are drawn on top (in slay) */
    uint32_t*save=cv; Rect sc=clipr; cv=slay; set_clip(FULL);
    if(lit&&a->hw>=4){ disc(ex,ey,a->hw-2.2f,rgb(255,255,255),215); }
    if(a->vis&&a->mark>=0){
        float x0,y0,x1,y1; arc_pt(a,a->mark,a->rm-a->hw-5,&x0,&y0); arc_pt(a,a->mark,a->rm+a->hw+5,&x1,&y1);
        capsule(x0,y0,x1,y1,1.6f,rgb(250,251,253),255);
        Rect mr={(short)(fminf(x0,x1)-4),(short)(fminf(y0,y1)-4),(short)(fmaxf(x0,x1)+5),(short)(fmaxf(y0,y1)+5)}; bb=r_empty(bb)?mr:r_union(bb,mr); a->mrect=mr;
    }
    cv=save; clipr=sc;
    a->f_drawn=a->f_d; a->d_lo=a->c_lo; a->d_hi=a->c_hi; a->mark_drawn=a->mark; a->vis_drawn=a->vis;
    if(r_empty(bb)) return 0;
    *out=bb; return 1;
}
/* eased fill: exponential approach (tau ~110 ms) */
static int arc_ease(Arc*a,float dt_ms){
    float d=a->f_t-a->f_d; if(d==0) return 0;
    if(fabsf(d)<0.0012f){ a->f_d=a->f_t; return 1; }
    a->f_d+=d*(1-expf(-dt_ms/110.f)); return 1;
}

/* ================= compositor: slay layer + widgets + overlays + present ================= */
#define NWID 96
static RL dmg, pres, pres_prev;
static uint32_t wsig[NWID];
static int force_all=1, static_dirty=1;      /* static_dirty is forward-declared at lang_set() */
static uint32_t hs(uint32_t h,const char*s){ while(*s){ h^=(unsigned char)*s++; h*=16777619u; } return h; }
static uint32_t hi(uint32_t h,int v){ for(int k=0;k<4;k++){ h^=(v>>(8*k))&0xFF; h*=16777619u; } return h; }
#define H0 2166136261u
/* widget: drawn only if the signature has changed or its rect is hit by damage */
static int W_begin(int id,Rect r,uint32_t sig){
    if(sig==0) sig=1;
    if(!force_all&&wsig[id]==sig&&!rl_hits(&dmg,r)) return 0;
    wsig[id]=sig; blit_rect(canvas,slay,r); rl_add(&pres,r); cv=canvas; set_clip(r); return 1;
}
static void W_end(void){ set_clip(FULL); }

/* ---- framebuffer (device) ---- */
static int ffd=-1; static uint32_t *fbmem=NULL; static long fblen=0; static int line_px=W; static int curpage=0;
#ifdef SIM_RT
/* SIM: fb = one 480x480 BGRA buffer in memory (same pixel format as the device; no page flip).
 * fb_present() copies ONLY the rects the compositor reports as changed — exactly like the device — so a
 * bug in the dirty-rect logic would also be visible in the SIM images. sim_frame() writes the buffer to a file. */
static long sim_fb_seq=0;
static void fb_open(void){
    (void)line_px; (void)curpage;
    fblen=(long)W*H*4;
    fbmem=mmap(NULL,fblen,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(fbmem==MAP_FAILED){ perror("mmap sim-fb"); exit(4); }
    for(long i=0;i<(long)W*H;i++) fbmem[i]=0xFF000000u;
}
#endif
#ifdef DEVICE_RT
static struct fb_var_screeninfo vi;
static void fb_open(void){
    ffd=open(FB,O_RDWR);
    if(ffd<0){ perror("open fb0"); exit(1); }
    struct fb_fix_screeninfo fi;
    ioctl(ffd,FBIOGET_VSCREENINFO,&vi);
    ioctl(ffd,FBIOGET_FSCREENINFO,&fi);
    line_px = fi.line_length/4;
    fblen = (long)vi.yres_virtual*fi.line_length;
    fbmem=mmap(NULL,fblen,PROT_READ|PROT_WRITE,MAP_SHARED,ffd,0);
    if(fbmem==MAP_FAILED){ perror("mmap fb"); exit(1); }
    curpage = (vi.yoffset/H)&1;
    /* clear BOTH fb pages completely (also outside the circle) to pure black */
    int pages=(int)(vi.yres_virtual/H); if(pages>2)pages=2;
    for(int pg=0;pg<pages;pg++){ uint32_t*p=fbmem+(long)pg*H*line_px; for(long i=0;i<(long)H*line_px;i++) p[i]=0xFF000000u; }
}
#endif
/* present: the back page lacks changes from the previous AND this frame -> copy the union, flip */
static void fb_present(const uint32_t*src,const RL*now){
#ifdef DEVICE_RT
    if(now->n==0&&pres_prev.n==0) return;
    uint32_t*dst=fbmem+(long)curpage*H*line_px;
    for(int l=0;l<2;l++){
        const RL*rl=l?&pres_prev:now;
        for(int i=0;i<rl->n;i++){ Rect r=rl->r[i]; for(int y=r.y0;y<r.y1;y++) memcpy(dst+(long)y*line_px+r.x0,src+y*W+r.x0,(size_t)(r.x1-r.x0)*4); }
    }
    struct fb_var_screeninfo v=vi; v.yoffset=curpage*H; v.xoffset=0;
    ioctl(ffd,FBIOPAN_DISPLAY,&v);
    curpage^=1;
#elif defined(SIM_RT)
    if(fbmem&&now->n){
        for(int i=0;i<now->n;i++){ Rect r=now->r[i]; for(int y=r.y0;y<r.y1;y++) memcpy(fbmem+(long)y*W+r.x0,src+y*W+r.x0,(size_t)(r.x1-r.x0)*4); }
        sim_fb_seq++;
    }
#else
    (void)src;
#endif
    pres_prev=*now;
}

/* ---- transitions: slide (page change) + crossfade (detail/splash) ---- */
enum { TR_NONE=0, TR_SLIDE, TR_FADE };
static struct { int type; ms_t t0; int dur; int dir; } tr;
static float ease_out(float t){ float u=1-t; return 1-u*u*u; }
static float ease_io(float t){ return t<0.5f? 4*t*t*t : 1-powf(-2*t+2,3)/2; }
static int trans_frame(ms_t now){
    float t=(float)(now-tr.t0)/tr.dur;
    if(t>=1){ tr.type=TR_NONE; return 0; }
    if(tr.type==TR_SLIDE){
        int off=(int)(ease_io(t)*W);
        for(int y=0;y<H;y++){
            uint32_t*d=tbuf+y*W; const uint32_t*a=snapA+y*W,*b=snapB+y*W;
            for(int x=cx0[y];x<=cx1[y];x++){
                int sxp=tr.dir>0? x+off : x-off;
                d[x]= (sxp>=0&&sxp<W)? a[sxp] : (tr.dir>0? b[sxp-W] : b[sxp+W]);
            }
        }
    } else {
        int al=(int)(ease_out(t)*255);
        for(int y=0;y<H;y++){
            uint32_t*d=tbuf+y*W; const uint32_t*a=snapA+y*W,*b=snapB+y*W;
            for(int x=cx0[y];x<=cx1[y];x++) d[x]=mix(a[x],b[x],al);
        }
    }
    RL full; full.n=1; full.r[0]=FULL; fb_present(tbuf,&full);
    return 1;
}
static void gfx_init(void){
    size_t n=(size_t)W*H*4;
    canvas=calloc(1,n); bgpure=calloc(1,n); bgbase=calloc(1,n); slay=calloc(1,n); snapA=calloc(1,n); snapB=calloc(1,n); tbuf=calloc(1,n);
    for(size_t i=0;i<(size_t)W*H;i++){ canvas[i]=0xFF000000u; tbuf[i]=0xFF000000u; }
    cv=canvas; clip_init(); bg_build();
    arc_build(&garc,214,7,5,135,270,50,225.5f,232.5f);
    arc_build(&parc,198,1.5f,0,135,270,0,0,0);
    font_prewarm();
}

/* ====================================================================== */
/* ================= UI: design tokens, state, pages ==================== */
/* ====================================================================== */
#define C_TXT    rgb(242,244,247)
/* v6.11: contrast raised (WCAG 2.x, measured in the selftest): C_TXT2 >= 7:1 on background/surfaces, C_TXT3 (small captions in F_XS) >= 4.5:1
 * on background, C_SURF and C_SURF2 (v6.10: 104,111,126 = 3.4:1 on background and 2.9:1 on list rows). What used to be at 3.4:1 but is a
 * DISABLED state (icons, stepper glyph, locked values, offline hero) is drawn in C_DIS — disabled UI is exempt in WCAG and is
 * meant to look switched off. */
#define C_TXT2   rgb(176,183,196)
#define C_TXT3   rgb(140,147,162)
#define C_DIS    rgb(98,105,120)
#define C_SURF   rgb(25,28,35)
#define C_SURF2  rgb(37,41,51)
#define C_SURF3  rgb(52,57,70)
#define C_LINE   rgb(56,61,73)
#define C_ACC    rgb(244,192,122)     /* crema — the only UI accent */
#define C_ACCD   rgb(46,34,18)
#define C_OK     rgb(98,222,168)
#define C_ERR    rgb(255,96,96)
#define C_WARN   rgb(255,190,84)
#define C_STOP_T  rgb(214,58,62)      /* v6.17: the STOP button — bright red, white text >= 4.5:1 on it (v6.13: 120,40,44 / 92,28,34 w. pink text; 1.8:1 against the background) */
#define C_STOP_B  rgb(182,42,50)
#define C_STOP_FG rgb(255,255,255)
/* semantic phase colours (hi = light end of gradient, lo = dark end) */
static uint32_t ph_hi(int p){
    switch(p){
    case PH_OFFLINE: return rgb(124,130,144); case PH_SLEEP: return rgb(136,144,222);
    case PH_ERROR:   return rgb(255,96,96);   case PH_HEAT:  return rgb(255,186,84);
    case PH_READY:   return rgb(98,222,168);  case PH_GRIND: return rgb(174,228,112);
    case PH_PRE:     return rgb(92,180,255);  case PH_EXT:   return rgb(255,142,68);
    case PH_WATER:   return rgb(70,216,230);  case PH_STEAM: return rgb(198,152,255);
    default:         return rgb(98,222,168);
    }
}
static uint32_t ph_lo(int p){
    switch(p){
    case PH_OFFLINE: return rgb(66,70,80);    case PH_SLEEP: return rgb(58,62,120);
    case PH_ERROR:   return rgb(130,36,40);   case PH_HEAT:  return rgb(150,78,26);
    case PH_READY:   return rgb(24,104,92);   case PH_GRIND: return rgb(62,116,46);
    case PH_PRE:     return rgb(30,74,168);   case PH_EXT:   return rgb(166,54,28);
    case PH_WATER:   return rgb(20,96,140);   case PH_STEAM: return rgb(88,56,170);
    default:         return rgb(24,104,92);
    }
}
static const int ACT_PH[4]={PH_GRIND,PH_EXT,PH_WATER,PH_STEAM};
static const char*const ACT_ICON[4]={IC_BEAN,IC_CUP,IC_DROP,IC_STEAM};
static const short ACT_NAME[4]={TX_A_GRIND,TX_A_BREW,TX_A_WATER,TX_A_STEAM};
/* v6.13: STOP button, "STOPPING …" toast and "… STOPPED" toast per activity (index = ACT_*) */
static const short STOP_BTN[4]={TX_B_STOP_GRIND,TX_B_STOP_BREW,TX_B_STOP_WATER,TX_B_STOP_STEAM};
static const short STOP_SENT[4]={TX_T_STOP_GRIND,TX_T_STOP_BREW,TX_T_STOP_WATER,TX_T_STOP_SENT};
static const short STOP_DONE[4]={TX_T_STOPPED_GRIND,TX_T_STOPPED_BREW,TX_T_STOPPED_WATER,TX_T_STOPPED_STEAM};

enum { V_SPLASH=0, V_MAIN, V_DETAIL, V_SYS, V_BYE, V_OFF };   /* v6.14: V_OFF = the power button's TURNING OFF / OFF / TURNING ON */   /* V_SYS: boot/switch/exit view (xsel); V_BYE: last frame before runtime switch */
/* v6.17 INFORMATION ARCHITECTURE (docs/ux-audit-v617.md). HOME is the root and the live view of whatever runs. Everything else is a
 * SHEET that is opened from the thing it belongs to and closed with the chevron at its top (or a swipe right / a pull down on the header):
 *   HOME icon (tap)   -> that feature's page: GRIND / BREW / WATER / STEAM = live line + ITS settings as rows + HOLD TO START
 *   HOME icon (hold)  -> start, as before (the icon now carries its target underneath: "18.5 G", "36 G", "360 G", "65°")
 *   HOME gear         -> SYSTEM: four collapsible sections (DISPLAY, CLOCK, SCHEDULE, STOCK UI) + the DIAGNOSTICS entry
 *   SYSTEM row        -> DIAGNOSTICS (advanced; also opened by tapping HOME's error / offline hint)
 * Before v6.17 the pages were a carousel (HOME | BREW | STEAM · WATER | SYSTEM | DIAGNOSTICS: swipe, invisible edge zones, page dots) and
 * the settings lived in three lists that followed the spec's tiers, not the activity they belong to. */
enum { PG_HOME=0, PG_GRIND, PG_BREW, PG_WATER, PG_STEAM, PG_SYS, PG_DIAG, NPAGE };
#define PG_T0 PG_GRIND     /* first list page (page = PG_T0 + SET[].tier) */
enum { T_NONE=0, T_ACT0, T_ACT1, T_ACT2, T_ACT3, T_STOP, T_MINUS, T_PLUS, T_SAVE, T_CANCEL, T_START, T_DIAG, T_GRAPH, T_PINFO, T_GEAR, T_BACK, T_STOPS, T_STEAMH, T_ROW0=32 };   /* v6.11: T_GEAR (HOME -> SYSTEM), T_BACK (close chevron); v6.17: T_START (a feature page's HOLD TO START), T_DIAG (HOME's error/offline hint -> DIAGNOSTICS); v6.20: T_STEAMH (HOME's HOLD · STEAM during a brew) */
static int pinfo_hit(int x,int y);   /* v6.10: tap on the chart / BAR field -> toast w. the basis for ≈BAR (defined at draw_chart) */
static int diag_hint_shown(void);    /* v6.17: HOME's error/offline hint is on screen (the chart field is empty) -> it is the tap target for DIAGNOSTICS */
#define SPLASH_MS 1500
#define HOLD_MS 650

static int view=V_SPLASH, page=PG_HOME, sel=-1, edit_val=0, edit_orig=0;
static float scroll[NPAGE];
static ms_t ui_start=-1, ui_last=0; static int ui_anim=1; static volatile int run=1;
static int ui_kick=0; static float ft_last=0, ft_max=0;   /* frame time (ms) measured on device, shown on DIAGNOSE */
static struct { int down,x0,y0,x,y,moved,target,scrolling,fired,rep_n,axis; ms_t t0,rep_next; float scroll0; } tch;
static struct { char msg[96]; ms_t t0; int dur; uint32_t col; } toast;
static struct { float x,y; ms_t t0; int on; } rip;
static Rect ov_prev[2];
static struct { int raw,ph,last_ph,pending,gkind; ms_t t_act0,t_done,done_ms,t_req; long seq; uint32_t c_from_hi,c_from_lo; ms_t t_col; } act={PH_OFFLINE,PH_OFFLINE,PH_READY,ACT_NONE,-1,0,-100000,0,0,0,0,0,0};
/* ---- v6.9: STABLE WEIGHT SOURCE for chart, hero and stat field (word 38 = grind weight, word 39 = brew weight; both signed 0.1 g) ----
 * The user saw periodic weight spikes, even when the machine was not touched. Up to and including v6.9-wip the graph chose the weight word per
 * sample by phase (phase_main()==GRIND ? 38 : 39), and the hero by the shown phase — so any phase flicker (a single
 * wrong work_sta sample, a link gap, the maintenance pulses 0x0400/0x0800/0x2000/0x0C00/0x2800 at rest) could put one sample
 * of the OTHER weight word into the curve (e.g. 18.7 g grind weight in the middle of 36.7 g brew weight). Now the source is a LATCH:
 *  - default = the brew weight (word 39);
 *  - the grind weight (word 38) only while a grind IS running (work_sta bit 6) and while its DONE screen is shown (8 s) — the
 *    latter only if the source was already latched to 38 (a real grind, not one wrong sample);
 *  - a switch requires the wanted source to have held for >= WSRC_DEB_MS (300 ms) — one deviating sample never switches;
 *  - exception: if the UI itself started the activity (act_request, < 4 s ago) and the activity's bits are set, it switches
 *    immediately — the activity is real (we asked for it, and the MCU has set its bits).
 * DISPLAY ONLY: control, frame policy, auto-stop expectations, guards and DIAGNOSE read words 38/39 directly and raw. */
#define WSRC_DEB_MS 300
static struct { int src,cand,n_sw,req; ms_t t_cand,t_req; } wsl={39,39,0,ACT_NONE,0,-100000};
static int wsrc_want(ms_t now){
    int pm=phase_main(now);
    if(pm==PH_GRIND) return 38;                                              /* bit 6: the grinder is running */
    if(pm==PH_PRE||pm==PH_EXT||pm==PH_WATER) return 39;                      /* brew/water bits */
    if(wsl.src==38&&(act.ph==PH_DONE||act.ph==PH_GRIND)&&act.last_ph==PH_GRIND) return 38;   /* the grind result is on screen (DONE) —
                                                                             only after a REAL grind (the source was already latched);
                                                                             otherwise one bit-6 sample gives 8 s of "DONE" w. grind weight */
    return 39;
}
static int wsrc_tick(ms_t now){
    int w=wsrc_want(now);
    if(w==wsl.src){ wsl.cand=w; return w; }
    if(w!=wsl.cand){ wsl.cand=w; wsl.t_cand=now; }
    int pm=phase_main(now);
    int req=wsl.req!=ACT_NONE&&now>=wsl.t_req&&now-wsl.t_req<4000&&phase_active(pm)&&((wsl.req==ACT_GRIND)==(pm==PH_GRIND))&&((w==38)==(pm==PH_GRIND));
    if(req||now-wsl.t_cand>=WSRC_DEB_MS||now<wsl.t_cand){ wsl.src=w; wsl.n_sw++; }
    return wsl.src;
}
static struct { int active,stage,idx,val,rollback; long seq; } sav;
/* v6.2: system actions. X_BOOTUI + X_SWITCH are rows at the bottom of SYSTEM; X_EXIT is the exit reminder */
enum { X_BOOTUI=0, X_SWITCH, X_EXIT };
static int xsel=-1, x_edit=0, x_orig=0, x_from_switch=0, x_pending=0, bye_mode=0;
static int sw_sim=0;       /* preview only: SWITCH NOW starts no process */
/* device hooks for BOOT.md §9.2 step 1: release UART/touch/fb BEFORE stock is started; reacquire them on failure */
static void (*dev_release)(void)=NULL;
static void (*dev_reacquire)(void)=NULL;
static int sw_child=0, sw_seq=0, sw_seq_frame=0, sw_seq_release=0, sw_seq_launch=0;

#define TOAST_TW 312       /* text width that fits in the toast pill (356 - 44); longer text is truncated w. … */
static void toast_show(const char*m,uint32_t col,ms_t now){
    snprintf(toast.msg,sizeof toast.msg-4,"%s",m); utf8_trim(toast.msg); fit_text(toast.msg,F_XS,TOAST_TW);
    toast.t0=now; toast.dur=2400; toast.col=col;
    SIM_TOAST(m,col)
}
/* ---- v6.17: THE ROW MODEL. Every list page is a list of rows of ONE pitch; what a row is, is decided here and nowhere else
 * (hit-test, drawing, the SIM layout and the selftest all walk rows_get()).
 *   feature pages : that feature's settings, most used first (FEAT_ROWS)
 *   SYSTEM        : section headers (RK_SEC; tap = open/close, one open at a time) + the open section's rows (SEC_ROWS: settings and
 *                   the two system actions BOOT UI / SWITCH TO ORIGINAL NOW) + the DIAGNOSTICS entry (RK_DIAG)
 * Every setting of SET[] is in exactly one of the two tables — except the WATER SENSOR (read-only word 40), which the WATER page shows
 * as its live line instead of as a row that looks like a setting (the selftest checks both). */
enum { SEC_DISPLAY=0, SEC_CLOCK, SEC_SCHED, SEC_STOCK, NSEC };
enum { RK_SET=0, RK_SEC, RK_X, RK_DIAG };
typedef struct { signed char kind, a; } Row;      /* a: SET[] index / section / X_BOOTUI|X_SWITCH / 0 */
#define ROW_MAX 20
#define RX(x) (-2-(x))                            /* a system action inside SEC_ROWS */
static const signed char FEAT_ROWS[4][8]={
 {S_GRINDW,S_GRINDMODE,-1},
 {S_BREWW,S_BREWT,S_PISW,S_PITIME,S_PIPRESS,S_HEADT,-1},
 {S_HOTW,S_HOTMODE,-1},
 {S_STEAMT,S_STEAMG,-1} };
static const signed char SEC_ROWS[NSEC][10]={
 [SEC_DISPLAY]={S_UILANG,S_BL,S_TUNIT,S_SOUND,S_SNDRDY,S_VOL,-1},          /* v6.18: + sound; VOLUME moved here (it now sets Meraki+'s sounds too) */
 [SEC_CLOCK]  ={S_TIMEMODE,S_DST,S_TZ,-1},
 [SEC_SCHED]  ={S_PREHEAT,S_PHAT,S_PHREP,S_AUTOOFF,-1},
 [SEC_STOCK]  ={RX(X_BOOTUI),RX(X_SWITCH),S_LANG,S_HOTSC,S_CLEARTIP,S_AUTOTIME,S_BREAK,-1} };   /* what only matters to / leads to the stock UI */
static const short SEC_NAME[NSEC]={TX_SEC_DISPLAY,TX_SEC_CLOCK,TX_SEC_SCHED,TX_SEC_STOCK};
static int sec_open=-1;                           /* SYSTEM: the open section (-1 = all closed: five rows, no scrolling) */
static int pg_feat(int pg){ return pg>=PG_GRIND&&pg<=PG_STEAM; }
static int pg_list(int pg){ return pg>=PG_GRIND&&pg<=PG_SYS; }
static int tier_of_page(int pg){ return pg-PG_T0; }
static int rows_get(int pg,Row*r){
    int n=0;
    if(pg_feat(pg)){ for(const signed char*p=FEAT_ROWS[pg-PG_GRIND];*p!=-1;p++){
        if(pg==PG_WATER&&!wd_dosing()) break;          /* v6.22: no dosing -> the WATER page is manual-only (stock legacy page): no rows */
        r[n].kind=RK_SET; r[n++].a=*p; } }
    else if(pg==PG_SYS){
        for(int sc=0;sc<NSEC;sc++){
            r[n].kind=RK_SEC; r[n++].a=(signed char)sc;
            if(sc==sec_open) for(const signed char*p=SEC_ROWS[sc];*p!=-1;p++){ if(*p<-1){ r[n].kind=RK_X; r[n++].a=(signed char)(-2-*p); } else { r[n].kind=RK_SET; r[n++].a=*p; } }
        }
        r[n].kind=RK_DIAG; r[n++].a=0;
    }
    return n;
}
#if defined(MERAKIPLUS_PREVIEW)||defined(SIM_RT)      /* test hooks only: the product code walks rows_get() */
static int sec_of_set(int i){ for(int sc=0;sc<NSEC;sc++) for(const signed char*p=SEC_ROWS[sc];*p!=-1;p++) if(*p==i) return sc; return -1; }
#endif
#ifdef MERAKIPLUS_PREVIEW
static int row_find(int pg,int kind,int a){ Row r[ROW_MAX]; int n=rows_get(pg,r); for(int k=0;k<n;k++) if(r[k].kind==kind&&r[k].a==a) return k; return -1; }
#endif
#define ROW_H 50
#define ROW_P 58
#define LIST_Y0 96
#define LIST_VY0 86
#define LIST_VY1 394
#define FEAT_VY1 342       /* v6.17: a feature page's list ends above its HOLD TO START pill */
static int list_vy1(int pg){ return pg_feat(pg)?FEAT_VY1:LIST_VY1; }
static int page_rows(int pg){ Row r[ROW_MAX]; return rows_get(pg,r); }
static int row_y0(int pg,int k){ (void)pg; return LIST_Y0+k*ROW_P; }   /* content coordinate (before scroll) */
/* where a page's close chevron leads: DIAGNOSTICS back to where it was opened from (SYSTEM, or HOME via the error hint), the rest to HOME */
static int diag_from=PG_SYS;
static int page_parent(int pg){ return pg==PG_DIAG?diag_from:PG_HOME; }
#define DG_Y0 94
#define DG_P 42
/* v6.11: DIAGNOSE is grouped. At the top STATUS (4 rows: link, phase, error codes, awake, net, version, boot push) — what you look
 * for first; then BOOT · SLOT · SWITCHER, NET, GRAPH and PRESSURE · ESTIMATE as before; RAW COUNTERS (rx, crc, work_sta, cmd6, current,
 * frame time, mirror, config md5) got their own section below the pressure estimate; ABOUT at the bottom. The cells' content is unchanged
 * (diag_top_text, index 0..15) — only the placement (DG_SLOT) is new. */
#define DG_BOOT (DG_Y0+4*DG_P+32)          /* first boot row on DIAGNOSE (v6.11: 4 status rows at the top; v6.5-v6.10: 8) */
#define DG_GRAPH (DG_BOOT+7*DG_P)          /* v6.9: the GRAPH row (smooth / raw) — the only tap target on DIAGNOSE */
#define DG_PRESS (DG_BOOT+8*DG_P+6)        /* v6.10: heading of the PRESSURE · ESTIMATE block (anchor, current value, 2 note lines) — where ABOUT was in v6.9 */
#define DG_RAW (DG_PRESS+28+2*DG_P+20)      /* v6.11: the RAW COUNTERS heading (where ABOUT was in v6.10); 4 rows below it. v6.17: the two note lines under the pressure estimate are gone (-32) */
#define DG_PWR (DG_RAW+28+4*DG_P+10)        /* v6.14: POWER BUTTON (state, word 0, press, last button, node) — where ABOUT was in v6.13 */
#define DG_TIME (DG_PWR+28+3*DG_P+10)       /* v6.16: TIME · SCHEDULE (clock, zone/DST, auto off, last alarm sent, next pre-heat) — where ABOUT was in v6.15 */
#define DG_SND (DG_TIME+28+3*DG_P+10)        /* v6.18: SOUND (mode + volume, last event + outcome) — where ABOUT was in v6.17 */
#define DG_MCU (DG_SND+28+DG_P+10)           /* v6.22: CONTROLLER (firmware string from words 80..87 + what hot water can do) — where ABOUT was in v6.21 */
#define DG_ABOUT (DG_MCU+28+DG_P+10)         /* v6.22: + CONTROLLER. v6.18: + SOUND. v6.14: + POWER BUTTON. v6.11: + RAW COUNTERS. v6.3: heading of the ABOUT block (UI language, wordmark, tagline); v6.7: + the NET line; v6.9: + the GRAPH row; v6.10: + PRESSURE · ESTIMATE */
#define DG_BOTTOM (DG_ABOUT+76+24+11+22)
static float scroll_max(int pg){
    if(pg==PG_DIAG) return DG_BOTTOM-(LIST_VY1-14);
    if(!pg_list(pg)) return 0;
    int n=page_rows(pg); float m=n*ROW_P-8-(list_vy1(pg)-LIST_VY0-20); return m>0?m:0;
}

/* ---- formatting ---- */
static int use_c(void){ return sv(S_TUNIT)!=0; }
static void fmt_fix(char*b,size_t n,int v,int dec){
    if(dec==1) snprintf(b,n,"%s%d.%d",v<0?"-":"",abs(v)/10,abs(v)%10);
    else if(dec==-1) snprintf(b,n,"%d",(v+(v<0?-5:5))/10);
    else snprintf(b,n,"%d",v);
}
static void fmt_t10(char*b,size_t n,int v10,int dec){   /* temperature in 0.1 °C -> text in the selected unit */
    if(!use_c()) v10=v10*9/5+320;
    fmt_fix(b,n,v10,dec?1:-1);
}
static const char* t_unit(void){ return TR(use_c()?TX_U_C:TX_U_F); }
static void set_fmt(int i,int v,char*b,size_t n){
    const Setting*s=&SET[i];
    if((s->flags&SF_SENSOR)&&!s->loaded){ snprintf(b,n,"--"); return; }
    if(s->kind==K_ENUM){ if(v>=s->min&&v<=s->max) snprintf(b,n,"%s",TR(s->opts[v-s->min])); else snprintf(b,n,"(%d)",v); return; }
    if(s->flags&SF_HHMM){ fmt_hm(b,n,v/60,v%60,1); return; }                               /* v6.16 */
    if(i==S_TZ&&v>=0&&v<TZ_N){ snprintf(b,n,"%s",TZT[v].utc); return; }                 /* v6.16: the stock label "UTC+01:00" */
    if(s->flags&SF_TEMP10){ fmt_t10(b,n,v,1); return; }
    if(s->flags&SF_TEMP1){ fmt_t10(b,n,v*10,0); return; }
    fmt_fix(b,n,v,s->dec);
}
static const char* set_unit(int i){ const Setting*s=&SET[i]; if(s->flags&(SF_TEMP10|SF_TEMP1)) return t_unit(); return TR(s->unit); }
static int s16(int r){ return (int)(int16_t)reg[r]; }

/* ---- activity guards (the UI is the only guard) ---- */
/* v6.20: STEAM DURING A BREW (docs/concurrent-brew-steam.md §9, hardware-verified §11). The MCU has no brew<->steam lock: brew and steam
 * drive disjoint outputs, its heater task never runs both heaters (the steam heater stays off during a brew until the steam boiler is 10 °C
 * below target, then 100 %), and each activity stops on its own. So a latched steam start is allowed when a BREW (pre-infusion or
 * extraction) is the ONLY thing running: no steam bits (no wand flush), word 24 = 0, no grind bit 6, no hot-water bit 3. The rest of
 * act_guard() still applies (error, queue/pending/SAVE, config). Everything else stays blocked: steam during grind (the MCU stops it anyway)
 * or hot water (the steam heater gets no slots), and grind / brew / water during anything. A brew start during latched steam is not offered
 * (the MCU would accept it; Meraki+'s scope is steam-during-brew). */
static int steam_brew_ok(ms_t now){
    int p=phase_raw(now);
    return (p==PH_PRE||p==PH_EXT)&&!(reg[33]&0x4068)&&reg[24]==0;
}
static const char* act_guard(int kind,ms_t now){
    if(ro_mode) return TR(TX_G_RO);
    int p=phase_raw(now);
    if(p==PH_OFFLINE) return TR(TX_G_NOLINK);
    if(p==PH_ERROR)   return TR(TX_G_ERROR);
    if(p==PH_SLEEP)   return TR(TX_G_SLEEP);
    if(phase_active(p)&&!(kind==ACT_STEAM&&steam_brew_ok(now))) return TR(TX_G_BUSY);
    if(reg[34]||reg[35]||reg[36]) return TR(TX_G_ERROR);           /* v6.20: phase_raw puts a running brew before the error words */
    if(wq_busy()||act.pending!=ACT_NONE||sav.active) return TR(TX_G_WAIT);
    /* all config values that go into a cmd6 frame must be loaded and within spec */
    for(int i=0;i<NSET;i++){
        const Setting*s=&SET[i]; if(s->mcu==M_NONE) continue;
        if(!s->loaded) return TR(TX_G_NOCFG);
        if(set_oor(s)) return TR(TX_G_OOR);
    }
    if(kind==ACT_GRIND&&sv(S_GRINDMODE)!=1) return TR(TX_G_GRINDTIME);
    return NULL;
}
static void act_request(int kind,ms_t now){
    const char*g=act_guard(kind,now);
    if(g){ toast_show(g,C_WARN,now); return; }
    long seq=wq.seq;
    if(!act_push(kind)){ toast_show(TR(wq_rej==WJ_TRIG||wq_rej==WJ_RANGE?TX_T_POLICY:TX_T_QFULL),C_ERR,now); return; }
    act.pending=kind; act.t_req=now; act.seq=seq;
    wsl.req=kind; wsl.t_req=now;                                    /* v6.9: UI-started activity -> the weight source may switch immediately */
}

/* ---- save flow: MCU write (ACK) -> config.json (atomic) ; all-or-nothing ---- */
static void detail_close(ms_t now);
static void boot_push_resave(void);
static void snd_preview(ms_t now);                                   /* v6.18 (the sound engine is after sched_step) */
static void save_begin(ms_t now){
    if(sav.active||sel<0) return;
    Setting*s=&SET[sel];
    if(s->flags&SF_LOCK) return;
    if(edit_val==s->val){ detail_close(now); return; }
    if(!set_valid(s,edit_val)){ toast_show(TR(TX_T_RANGE),C_ERR,now); return; }
    if(s->flags&SF_LOCAL){   /* v6.3: UI language — own file; no MCU write, no config.json, no RESTART_NEEDED */
        int ok=lang_save(edit_val);
        s->val=edit_val; lang_set(edit_val); if(!ok) lang_src=LS_SESSION;      /* the language applies now in any case */
        toast_show(TR(ok?TX_T_LANG_SAVED:TX_T_LANG_NOFILE),ok?C_OK:C_WARN,now); /* confirmation in the NEW language */
        detail_close(now); return;
    }
    if(s->flags&SF_OWN){     /* v6.16: pre-heat schedule / auto off — merakiplus's own file; no MCU write, no config.json */
        s->val=edit_val;
        if(sel==S_PREHEAT) sch.on=edit_val; else if(sel==S_PHAT) sch.hm=edit_val; else if(sel==S_PHREP) sch.rep=edit_val; else sch.ao=edit_val;
        sch.once_at=(sch.on&&!REP_MASK[sch.rep])?sched_once_target(clk_utc()):0;   /* a one-shot is for the next hh:mm after now */
        int ok=sched_save(); idle_kick(now);
        if(sch.on&&!clk_valid(clk_utc())) toast_show(TR(TX_T_NOCLOCK),C_WARN,now);
        else toast_show(TR(ok?TX_T_OWN_SAVED:TX_T_OWN_NOFILE),ok?C_OK:C_WARN,now);
        detail_close(now); return;
    }
    if(s->flags&SF_SND){     /* v6.18: sound on/off + ready tones — merakiplus's own file; no MCU write, no config.json */
        s->val=edit_val; if(sel==S_SOUND) sndc.mode=edit_val; else sndc.ready=edit_val;
        int ok=snd_cfg_save();
        toast_show(TR(ok?TX_T_SND_SAVED:TX_T_OWN_NOFILE),ok?C_OK:C_WARN,now);
        detail_close(now); snd_preview(now); return;
    }
    if(!cfg_ok||!s->loaded){ toast_show(TR(TX_T_CFG_UNAVAIL),C_ERR,now); return; }
    sav.idx=sel; sav.val=edit_val; sav.rollback=0;
    if(s->mcu!=M_NONE){
        if(ro_mode){ toast_show(TR(TX_G_RO),C_WARN,now); return; }
        if(!link_up(now)){ toast_show(TR(TX_T_NOLINK_SAVE),C_ERR,now); return; }
        if(phase_active(phase_raw(now))){ toast_show(TR(TX_T_WAIT_ACT),C_WARN,now); return; }
        if(wq_busy()){ toast_show(TR(TX_G_WAIT),C_WARN,now); return; }
        sav.seq=wq.seq;
        if(setting_mcu_push(sel,edit_val)<0){ toast_show(TR(TX_T_WRITE_REJ),C_ERR,now); return; }
        sav.active=1; sav.stage=1;
    } else { sav.active=1; sav.stage=2; }
}
static void save_step(ms_t now){
    if(!sav.active) return;
    Setting*s=&SET[sav.idx];
    if(sav.stage==1){
        if(wq.seq==sav.seq) return;
        if(wq.result!=WR_OK){ sav.active=0; toast_show(TR(TX_T_NOACK_SAVE),C_ERR,now); return; }
        sav.stage=2;
    }
    if(sav.stage==2){
        if(cfg_set_int(s->key,sav.val)){
            s->val=sav.val; sav.active=0;
            boot_push_resave();
            toast_show(TR(s->mcu!=M_NONE?TX_T_SAVED_MCU:TX_T_SAVED_CFG),C_OK,now);
            if(view==V_DETAIL) detail_close(now);
            if(sav.idx==S_VOL) snd_preview(now);                       /* v6.18: hear the new volume */
        } else {
            /* config failed: roll the MCU back to the old value so both sides are unchanged */
            if(s->mcu!=M_NONE){ sav.seq=wq.seq; if(setting_mcu_push(sav.idx,s->val)>0) sav.stage=3; else sav.active=0; }
            else sav.active=0;
            toast_show(TR(TX_T_CFG_ERR),C_ERR,now);
            if(s->flags&SF_BL) bl_set(s->val);
            edit_val=s->val;
        }
    } else if(sav.stage==3){ if(wq.seq!=sav.seq) sav.active=0; }
}

/* ---- page navigation / transitions ---- */
static void compose(ms_t now);
static void arc_targets(ms_t now);
static void go_page(int np,int dir,ms_t now){
    if(np==page||tr.type) return;
    memcpy(snapA,canvas,(size_t)W*H*4);
    page=np; static_dirty=1; if(np==PG_HOME) act.gkind=-1; arc_targets(now); compose(now);
    memcpy(snapB,canvas,(size_t)W*H*4);
    tr.type=TR_SLIDE; tr.t0=now; tr.dur=260; tr.dir=dir;
}
static void fade_to(ms_t now,int dur){ tr.type=TR_FADE; tr.t0=now; tr.dur=dur; tr.dir=0; }
/* v6.17: sheets. nav_open() slides a page in from the right, nav_back() slides back to its parent. SYSTEM always opens with its sections
 * closed and at the top; DIAGNOSTICS remembers where it was opened from. */
static void nav_open(int np,ms_t now){
    if(np==page||tr.type) return;
    if(np==PG_DIAG) diag_from=(page==PG_SYS)?PG_SYS:PG_HOME;
    if(np==PG_SYS&&page==PG_HOME){ sec_open=-1; scroll[PG_SYS]=0; }
    if(pg_feat(np)) scroll[np]=0;
    go_page(np,1,now);
}
static void nav_back(ms_t now){ if(page!=PG_HOME&&!tr.type) go_page(page_parent(page),-1,now); }
static void sec_toggle(int sc){
    sec_open=(sec_open==sc)?-1:sc;
    scroll[PG_SYS]=clampf(sec_open<0?0:(float)(sc*ROW_P),0,scroll_max(PG_SYS));   /* the opened header goes to the top of the field */
}
static void detail_open(int i,ms_t now){
    memcpy(snapA,canvas,(size_t)W*H*4);
    view=V_DETAIL; sel=i; edit_val=edit_orig=SET[i].val; static_dirty=1; garc.f_d=0; arc_targets(now); compose(now);
    memcpy(snapB,canvas,(size_t)W*H*4); fade_to(now,200);
}
static void detail_close(ms_t now){
    if(sel>=0&&(SET[sel].flags&SF_BL)) bl_set(SET[sel].val);
    if(sel>=0&&(SET[sel].flags&SF_LOCAL)) lang_set(SET[sel].val);          /* CANCEL: back to the saved language */
    memcpy(snapA,canvas,(size_t)W*H*4);
    view=V_MAIN; sel=-1; static_dirty=1; act.gkind=-1; arc_targets(now); compose(now);
    memcpy(snapB,canvas,(size_t)W*H*4); fade_to(now,200);
}
/* ---- v6.2: system actions (BOOT UI, SWITCH TO ORIGINAL NOW, exit reminder) ---- */
static int sys_locked(void){ return xsel==X_BOOTUI? bs.lock!=BL_OK : (xsel==X_SWITCH? switch_locked() : 0); }
static void boot_poll(ms_t now){ boot_refresh(); bs.t=now; x_pending=restart_pending(); }
static void sys_open(int x,ms_t now){
    boot_poll(now);
    memcpy(snapA,canvas,(size_t)W*H*4);
    view=V_SYS; xsel=x; x_edit=x_orig=bs.flag; static_dirty=1; garc.f_d=0; arc_targets(now); compose(now);
    memcpy(snapB,canvas,(size_t)W*H*4); fade_to(now,200);
}
static void sys_close(ms_t now){
    memcpy(snapA,canvas,(size_t)W*H*4);
    view=V_MAIN; xsel=-1; x_from_switch=0; static_dirty=1; act.gkind=-1; arc_targets(now); compose(now);
    memcpy(snapB,canvas,(size_t)W*H*4); fade_to(now,200);
}
static const char* switch_guard(ms_t now){
    if(phase_active(phase_raw(now))) return TR(TX_T_WAIT_ACT);
    if(wq_busy()||sav.active||act.pending!=ACT_NONE) return TR(TX_G_WAIT);
    return NULL;
}
static void bye(int mode){ bye_mode=mode; view=V_BYE; xsel=-1; tr.type=TR_NONE; rip.on=0; toast.dur=0; static_dirty=1; run=0; }   /* the last frame freezes: no ripple/toast */
/* SWITCH TO ORIGINAL NOW — BOOT.md §9.2, in the order the specification requires:
 *   (0) last frame on screen   (1) stop own activity: close UART, touch grab and fb
 *   (2) fork + setsid + chdir /customer + LD_LIBRARY_PATH + execl ./demo_v9   (3) wait 400 ms, exit
 *   (4) the flag is not touched; no signals   (5) exit releases the rest   (6) log w. time + reason.
 * If the stock UI cannot be started, merakiplus takes UART/fb/touch back and keeps running.
 * run.sh mode (stock exists, paused): nothing is started — merakiplus exits, run.sh SIGCONTs. */
static const uint32_t* ui_frame(ms_t now);
static void do_switch(ms_t now){
    char err[72]="", rn[340]; int mode=switch_mode();
    if(sw_sim){ bye(mode); return; }
    if(mode==SWM_CONT){
        boot_log("RUNTIME SWITCH to original: reason=user-button, run.sh mode (stock UI pid %d is paused): merakiplus exits, nothing is started",bs.stock[0]);
        bye(mode); return;
    }
    if(sw_dry){
        ld_path_for_a(getenv("LD_LIBRARY_PATH"),sw_ldp,sizeof sw_ldp);
        boot_log("RUNTIME SWITCH DRY-RUN: would close UART/touch/fb, start %s (cwd /customer, LD_LIBRARY_PATH=%s), wait %d ms and exit",SLOT_A_EXE,sw_ldp,SW_WAIT_MS);
        SIM_DRY_BYE(mode,now)
        toast_show(TR(TX_T_DRYRUN),C_WARN,now); return;
    }
    int v0=view, x0=xsel;
    bye(mode); force_all=1; ui_frame(now); sw_seq_frame=++sw_seq;        /* (0) */
    if(dev_release) dev_release();         sw_seq_release=++sw_seq;        /* (1) */
    int hp=switch_launch(err,sizeof err);  sw_seq_launch=++sw_seq;         /* (2)+(3) */
    if(hp<=0){
        if(dev_reacquire) dev_reacquire();
        run=1; view=v0; xsel=x0; static_dirty=1; force_all=1;
        toast_show(TR(sw_fail==SWF_NOBIN?TX_T_SW_FAIL_NOBIN:(sw_fail==SWF_FORK?TX_T_SW_FAIL_FORK:TX_T_SW_FAIL_DIED)),C_ERR,now);   /* the detail (err) is in the log */
        boot_log("RUNTIME SWITCH FAILED: %s — merakiplus continues",err);
        return;
    }
    sw_child=hp;
    snprintf(rn,sizeof rn,"%s/RESTART_NEEDED",cfg_bakdir); int had=unlink(rn)==0;    /* stock has started fresh and reads config itself */
    boot_log("RUNTIME SWITCH B->A: reason=user-button, stock UI pid %d (%s, argv0 ./demo_v9), LD_LIBRARY_PATH=%s%s",hp,SLOT_A_EXE,sw_ldp,had?", RESTART_NEEDED removed":"");
}
static void sys_confirm(ms_t now){
    if(xsel==X_BOOTUI){
        if(x_edit==x_orig){ sys_close(now); return; }
        int want=x_edit;
        if(boot_flag_set(want)){ toast_show(TR(want?TX_T_BOOT_SAVED_OURS:TX_T_BOOT_SAVED_ORIG),C_OK,now); sys_close(now); }
        else { toast_show(TR(bs.lock!=BL_OK?TX_T_BOOT_LOCKED:TX_T_BOOT_ERR),C_ERR,now); x_edit=x_orig=bs.flag; static_dirty=1; fprintf(stderr,"merakiplus: BOOT-UI: %s\n",boot_err); }
    } else if(xsel==X_SWITCH){
        const char*g=switch_guard(now); if(g){ toast_show(g,C_WARN,now); return; }
        boot_poll(now);
        if(switch_locked()){ static_dirty=1; return; }
        if(bs.n_stock>0&&x_pending){ x_from_switch=1; sys_open(X_EXIT,now); return; }   /* paused stock UI w. stale mirror: say so first */
        do_switch(now);
    } else if(xsel==X_EXIT){
        if(x_from_switch) do_switch(now); else run=0;
    }
}
/* v6.8: the REVERT button on HOME has been removed. The same way out exists on SYSTEM: "SWITCH TO ORIGINAL NOW" is, in run.sh
 * mode, a plain exit (run.sh SIGCONTs; the reminder first if config has been saved), and in slot B boot it starts the
 * stock UI. In addition there is the switcher gesture and run.sh's trap. */
static void edit_step(int dir,int mult){
    Setting*s=&SET[sel]; if(s->flags&SF_LOCK) return;
    int v=edit_val+dir*s->step*mult;
    if(mult>1) v=(v/(s->step*mult))*(s->step*mult);   /* snap to coarse grid during acceleration */
    v=clampi(v,s->lo,s->hi);
    v=s->lo+((v-s->lo)/s->step)*s->step;              /* always on the stock picker's step grid */
    edit_val=v;
    if(s->flags&SF_BL) bl_set(edit_val);
    if(s->flags&SF_LOCAL) lang_set(edit_val);          /* v6.3: live preview — the whole UI switches language immediately; SAVE makes it permanent */
}

/* ---- hit-test ---- */
static int in_r(int x,int y,int rx,int ry,int rw,int rh){ return x>=rx&&x<rx+rw&&y>=ry&&y<ry+rh; }
static int in_c(int x,int y,int cx,int cy,int r){ int dx=x-cx,dy=y-cy; return dx*dx+dy*dy<=r*r; }
static const int ACT_X[4]={150,210,270,330};
#define ACT_Y 372
#define HINT_W 304         /* v6.11: the chart area's hint lines (the chart itself is 244 wide; the hint text may use 304) */
#define ACT_LAB_Y 405      /* v6.17: each icon's target ("18.5 G", "36 G", "360 G", "65°") right under it — what a hold will do */
#define GEAR_Y 448
#define GEAR_HW 32
#define BACK_X0 (CX-44)    /* v6.11: close chevron in detail/system: 88 x 46 px tap area above the title */
#define BACK_Y0 34
#define BACK_W 88
#define BACK_H 46
/* v6.17: a page's header = chevron (y 22..30) + title (y 44) + one line under it (y 70). The whole header is the close target. */
#define PBACK_X0 (CX-80)
#define PBACK_Y0 14
#define PBACK_W 160
#define PBACK_H 66
#define PG_CHEV_Y 22
#define PG_TITLE_Y 44
#define PG_LIVE_Y 70
/* v6.17: HOLD TO START on a feature page — same hold (HOLD_MS) and same guard as the HOME icon */
#define START_W 232
#define START_H 48
#define START_Y 350
#define START_HIT_W 248
#define START_HIT_Y 344
#define START_HIT_H 60
/* v6.13: the STOP buttons' tap areas (hit) and drawn pills. ONE button: same tap area as v6.6's STOP STEAM (180 x 56), the pill
 * 160 x 44 in F_S (v6.6-v6.12: 150 — "STOP WATER" / "STOP KVÆRN" are 123 / 122 px and need 14 px of space on each side).
 * TWO buttons (main activity + latched steam): two areas of 148 x 56 each side by side, no gap between; the pills 136 x 44 in F_XS. */
/* v6.17: STOP is the most important control while something runs, so it is the largest and brightest thing below the numbers: ONE button
 * 216 x 54 in F_M (tap area 232 x 60), TWO buttons 152 x 54 in F_S (tap areas 160 x 60, no gap between) — bright red with white text
 * (v6.13-v6.16: 160 x 44 / 136 x 44 in a dark red that read as "disabled"). */
#define STOP_PILL1_W 216
#define STOP_PILL2_W 152
#define STOP_PILL_H 54
#define STOP_PILL_Y 346
#define STOP_Y 342
#define STOP_H 60
static Rect stop_hit(int n,int i){ if(n<=1) return R(CX-116,STOP_Y,232,STOP_H); return i==0?R(80,STOP_Y,160,STOP_H):R(240,STOP_Y,160,STOP_H); }
static Rect stop_pill(int n,int i){ if(n<=1) return R(CX-STOP_PILL1_W/2,STOP_PILL_Y,STOP_PILL1_W,STOP_PILL_H); return i==0?R(84,STOP_PILL_Y,STOP_PILL2_W,STOP_PILL_H):R(244,STOP_PILL_Y,STOP_PILL2_W,STOP_PILL_H); }
static int stop_font(int n){ return n>1?F_S:F_M; }
static int diag_hint(int ph){ return ph==PH_ERROR||ph==PH_OFFLINE; }      /* HOME's hint line points to DIAGNOSTICS -> it is a tap target */
static int hit_test(int x,int y,ms_t now){
    if(view==V_SPLASH||view==V_BYE||view==V_OFF||tr.type) return T_NONE;
    if((view==V_SYS||view==V_DETAIL)&&in_r(x,y,BACK_X0,BACK_Y0,BACK_W,BACK_H)) return T_BACK;   /* v6.11: close chevron at the top */
    if(view==V_SYS){
        if(sys_locked()){ if(in_r(x,y,170,284,140,64)) return T_CANCEL; return T_NONE; }
        if(xsel==X_BOOTUI){ if(in_c(x,y,84,184,42)) return T_MINUS; if(in_c(x,y,396,184,42)) return T_PLUS; }
        if(in_r(x,y,100,284,136,64)) return T_CANCEL;
        if(in_r(x,y,244,284,136,64)) return T_SAVE;
        return T_NONE;
    }
    if(view==V_DETAIL){
        if(SET[sel].flags&SF_LOCK){ if(in_r(x,y,170,284,140,64)) return T_CANCEL; return T_NONE; }   /* locked/sensor: BACK only */
        if(in_c(x,y,84,184,42)) return T_MINUS;
        if(in_c(x,y,396,184,42)) return T_PLUS;
        if(in_r(x,y,100,284,136,64)) return T_CANCEL;
        if(in_r(x,y,244,284,136,64)) return T_SAVE;
        return T_NONE;
    }
    if(page==PG_HOME){
        int p=phase_raw(now), sk[2], sn=stop_btns(now,sk);
        if(sn==1&&steam_hold_shown(now)){ Rect r=stop_hit(2,0); if(in_r(x,y,r.x0,r.y0,r.x1-r.x0,r.y1-r.y0)) return T_STOP; r=stop_hit(2,1); if(in_r(x,y,r.x0,r.y0,r.x1-r.x0,r.y1-r.y0)) return T_STEAMH; }   /* v6.20 */
        else if(sn){ for(int i=0;i<sn;i++){ Rect r=stop_hit(sn,i); if(in_r(x,y,r.x0,r.y0,r.x1-r.x0,r.y1-r.y0)) return i?T_STOPS:T_STOP; } }   /* v6.13: STOP for grind/brew/water + latched steam */
        else if(!phase_active(p)) for(int i=0;i<4;i++) if(in_c(x,y,ACT_X[i],ACT_Y,30)) return T_ACT0+i;
        if(pinfo_hit(x,y)) return T_PINFO;
        if(diag_hint_shown()&&in_r(x,y,CX-HINT_W/2,236,HINT_W,58)) return T_DIAG;   /* v6.17: "TAP FOR DETAILS" (error / offline) */
        if(in_r(x,y,CX-GEAR_HW,GEAR_Y-22,2*GEAR_HW,46)) return T_GEAR;          /* v6.11: the gear; v6.17: -> SYSTEM */
        return T_NONE;
    }
    if(in_r(x,y,PBACK_X0,PBACK_Y0,PBACK_W,PBACK_H)) return T_BACK;              /* v6.17: every page but HOME closes with its header chevron */
    if(pg_list(page)){
        if(y>=LIST_VY0&&y<list_vy1(page)&&x>=64&&x<416){
            int n=page_rows(page);
            for(int k=0;k<n;k++){ float ry=row_y0(page,k)-scroll[page]; if(y>=ry-4&&y<ry+ROW_H+4) return T_ROW0+k; }
        }
        if(pg_feat(page)&&in_r(x,y,CX-START_HIT_W/2,START_HIT_Y,START_HIT_W,START_HIT_H)) return T_START;
        return T_NONE;
    }
    if(page==PG_DIAG&&y>=LIST_VY0&&y<LIST_VY1){                      /* v6.9: the GRAPH pill (smooth / raw) */
        float py=DG_GRAPH+12-scroll[PG_DIAG]; if(x>=CX-150&&x<CX+150&&y>=py-8&&y<py+36) return T_GRAPH;   /* v6.11: 44 px high (v6.10: 40) */
    }
    return T_NONE;
}
/* v6.13: STOP pressed (or the ctrl hook). Queues stop frame(s) and shows "STOPPING …"; stop_step() confirms when the bits are
 * cleared ("… STOPPED"), or reports an error (no ACK after retransmit / not cleared within STOP_CONFIRM_MS). */
static void stop_request(int kind,ms_t now){
    if(kind<ACT_GRIND||kind>ACT_STEAM) return;
    if(ro_mode){ toast_show(TR(TX_G_RO),C_WARN,now); return; }
    if(stp.active&&stp.kind==kind&&wq_busy()){ toast_show(TR(STOP_SENT[kind]),C_TXT,now); return; }   /* the stop frame is already in the queue */
    if(!act_push_stop(kind)){ stp.n_fail++; toast_show(TR(TX_T_STOP_FAIL),C_ERR,now); return; }
    stp.n_req++; stp.kind=kind; stp.t_req=now; stp.seq=wq.seq;
    stp.active=link_up(now)&&stop_running(kind);            /* only a running activity can be confirmed (the ctrl hook can send without one) */
    toast_show(TR(STOP_SENT[kind]),C_TXT,now);
}
static void stop_step(ms_t now){
    if(!stp.active) return;
    if(!link_up(now)){ stp.active=0; return; }                                                       /* the OFFLINE view takes over */
    if(!stop_running(stp.kind)){ stp.active=0; stp.n_ok++; toast_show(TR(STOP_DONE[stp.kind]),C_OK,now); return; }
    if(wq.seq!=stp.seq&&wq.result==WR_FAIL&&!wq_busy()){ stp.active=0; stp.n_fail++; toast_show(TR(TX_T_STOP_NOACK),C_ERR,now); return; }
    if(now-stp.t_req>=STOP_CONFIRM_MS){ stp.active=0; stp.n_fail++; toast_show(TR(TX_T_STOP_UNCONF),C_WARN,now); }
}
static void activate(int t,ms_t now){
    if(t==T_STOP||t==T_STOPS){ int sk[2]; stop_btns(now,sk); int k=sk[t==T_STOPS]; if(k!=ACT_NONE) stop_request(k,now); return; }   /* v6.13 */
    if(t>=T_ACT0&&t<=T_ACT3){ nav_open(PG_GRIND+(t-T_ACT0),now); return; }          /* v6.17: a TAP opens the feature's page (v6.6-v6.16: only a toast "HOLD THE BUTTON TO START"); the HOLD still starts */
    if(t==T_START||t==T_STEAMH){ const char*g=act_guard(t==T_STEAMH?ACT_STEAM:page-PG_GRIND,now); toast_show(g?g:TR(TX_T_HOLD_HINT),g?C_WARN:C_TXT,now); return; }   /* a tap on HOLD TO START (v6.20: or HOLD · STEAM): say what is missing */
    if(t==T_GEAR){ nav_open(PG_SYS,now); return; }                                   /* v6.11; v6.17: -> SYSTEM */
    if(t==T_DIAG){ nav_open(PG_DIAG,now); return; }                                  /* v6.17 */
    if(t==T_BACK&&view==V_MAIN){ nav_back(now); return; }                            /* v6.17: a page's header chevron */
    if(t==T_BACK) t=T_CANCEL;                                                        /* v6.11: chevron = CANCEL/BACK */
    if(t==T_GRAPH){ cd.raw=!cd.raw; hf.ok=0; return; }          /* v6.9: display only — no config, no frames, forgotten on restart */
    if(t==T_PINFO){ char b[96]; snprintf(b,sizeof b,TR(TX_T_PINFO),p_anchor_bar(),P_ANCHOR_RAW); toast_show(b,C_TXT,now); return; }   /* v6.10: the basis for ≈BAR */
    if(t==T_CANCEL){ if(view==V_SYS){ sys_close(now); return; } if(!sav.active) detail_close(now); return; }
    if(t==T_SAVE){ if(view==V_SYS){ sys_confirm(now); return; } save_begin(now); return; }
    if(t>=T_ROW0){
        Row r[ROW_MAX]; int n=rows_get(page,r), k=t-T_ROW0; if(k>=n) return;
        if(r[k].kind==RK_SET) detail_open(r[k].a,now);
        else if(r[k].kind==RK_SEC) sec_toggle(r[k].a);
        else if(r[k].kind==RK_X) sys_open(r[k].a,now);                 /* system action (SYSTEM > STOCK UI) */
        else nav_open(PG_DIAG,now);
        return;
    }
}
enum { TE_DOWN, TE_MOVE, TE_UP };
/* v6.11: SWIPE GUARD. Page switch only for a gesture that STARTED horizontally (axis lock at the first 14 px), is >= 60 px long and
 * at most ~30° from horizontal (|dx| >= 1.7·|dy|). v6.10 allowed |dy| <= 50 regardless of direction, so a diagonal drag (e.g. dx 70, dy 45) on a
 * list that started slightly horizontal switched page. A gesture that starts vertically can never switch page — not even
 * on pages without scrolling (HOME).
 * v6.17: there is no carousel any more. The only horizontal gesture is "swipe RIGHT = back" (same guard), on every page and editor
 * except HOME — a shortcut for the visible chevron. A swipe LEFT does nothing, and nothing on HOME reacts to a swipe. */
static int swipe_h(int axis,int dx,int dy){ return axis==1&&abs(dx)>=60&&10*abs(dx)>=17*abs(dy); }
/* v6.11: swipe DOWN (>= 70 px, vertically locked, |dy| >= 1.7·|dx|) closes the detail/system view like CANCEL — not if
 * the gesture started on −/+ (there the press-down has already changed the value) */
static int swipe_down(int axis,int target,int dx,int dy){ return axis==2&&dy>=70&&10*dy>=17*abs(dx)&&target!=T_MINUS&&target!=T_PLUS; }
/* ================= v6.14: the physical POWER BUTTON (UI_v6.md §19 + CHANGES-v6.14) =================
 * The bug: the button did nothing in merakiplus — only the touch node (event0) was read. The button is the k62091 node (/dev/input/event1,
 * "Handlers=kbd event1", EV=3 = SYN+KEY, one key). The stock semantics, reversed from demo_v9 (symbols preserved) + the MCU firmware:
 *  - app_powerKey_thread 0x140AB4: open("/dev/input/event1", O_RDWR) — NO EVIOCGRAB; reads struct input_event; for EVERY
 *    EV_KEY (the code is not checked) value is stored in app+0x15A8 (1 = down, 0 = released).
 *  - SleepRefresh 0x149268 (lv_timer, 100 ms): only when the second changes, and only if isExtractionOrSteamSwitchOn 0x1481E0 is
 *    false (work_sta bit 12 / bit 14, word 22, word 24, word 6 = brew/steam/grind), e60 is counted up while the button is down; then
 *    dealPowerState 0x14911C(value) every cycle: e60 > 1 (= down across TWO second ticks, i.e. 1-2 s) and mirror word 0 != 0 ->
 *    powerOff 0x149074. Release before that = short press (only wakes stock's screensaver). If an activity is running, NOTHING happens —
 *    stock never stops an activity on power-off; the button only works once brew/steam/grind has ended (hot water does not block).
 *  - powerOff: mirror word 0 = 0; performSleepActions 0x148324 -> powerOffSend 0x14700C(0,0,0) = cmd8 (15 bytes: year u16, month,
 *    day, hour, minute, second, alarm day/hour/minute = 0/0/0 = no alarm; CRC) + usleep(80 ms); usart_send(6,0,2) = cmd6
 *    sub0 len2 [word 0 = 0, word 1 = the mirror (0 = awake)]; backlight off (pwm2 enable 0 + duty 0); the power-off page.
 *    1.5 s later: fb is closed, "echo mem > /sys/power/state" (SoC suspend); after resume "killall demo_v9" -> watchdog restarts
 *    stock -> app_init 0xD9170 PowerOn (word 0 = 1).
 *  - MCU (MEM03_V514_260923.bin): the status task sees word 0 = 0 (0x29D4, every 500 ms) and starts "sleep" (0x2F52), which DELETES
 *    boiler_heat, head_heat, status (= RX parser + replies: the controller is silent while it sleeps) and run, sets work_sta = 0 and
 *    resets the outputs (0x2F0C). It wakes ONLY on a hold of the button (own GPIO PC.8; 8 x 100 ms low after a release) or
 *    the alarm -> 0x326 "reset muc" (CPU reset; word 0 starts as 1, the 0x89 round comes -> merakiplus's re-push sends config again).
 * merakiplus mirrors this: HOLD 1.5 s -> the same two frames (cmd8 w. alarm 0/0/0, 80 ms, cmd6 sub0 len2 = [0,0] w. I_POWEROFF) ->
 * TURNING OFF… -> ACK -> OFF; the backlight is turned off 1.5 s after the press; keep-awake is paused while off.
 * Blocked as in stock (brew/steam/grind running: toast, nothing is sent) — and without link / with word 0 = 0 / read-only.
 * Power on: the button again -> light + TURNING ON… (the MCU wakes by itself from the same hold); if the controller answers with word 0 = 1 -> PowerOn
 * (sub0=[1] + sub1=[0], exactly as at startup) -> HOME. Otherwise back to OFF after 10 s. If the controller answers by itself
 * with word 0 = 1 while merakiplus is in OFF (alarm, or the button was held while merakiplus did not see it), it powers on the same way.
 * A tap on the dark screen shows the OFF page for 8 s — no frames (the MCU cannot receive anything while it sleeps).
 * Deliberate deviation: no SoC suspend and no "killall" — only the backlight; the net guard and telnet keep running. */
enum { PW_ON=0, PW_OFFING, PW_OFF, PW_WAKING };
#define PW_HOLD_MS  1500    /* stock: 2 second ticks (1.0-2.0 s); the middle */
#define PW_GAP_MS     80    /* stock performSleepActions: cmd8 ... usleep(80000) ... cmd6 sub0 len2 */
#define PW_BL_MS    1500    /* stock: 1.5 s after powerOff -> fb/light/suspend (lv_tick_elaps > 0x5DC); merakiplus: the light is turned off */
#define PW_PEEK_MS  8000    /* tap on the dark screen: the OFF page is shown this long */
#define PW_WAKE_MS 10000    /* TURNING ON…: how long to wait for word 0 = 1 from the controller */
#define PW_SEEN_MS   600    /* word 0 = 1 seen (w. link) this long -> the controller is awake */
static struct { int state, key_down, key_fired, blk_shown, kbd_ok, key_code, key_val, f8_ok, push_set;
                ms_t t_press, t_down, t_req, t_state, t_peek, t_on1, t_key; long push_seq;
                long n_ev, n_press, n_short, n_off, n_on, n_blk, n_fail, n_ack8, n_auto, n_wto;
                uint8_t f8[15]; char node[64]; } pw;
static void proto_keepawake(ms_t now,int first);
/* stock isExtractionOrSteamSwitchOn 0x1481E0: GetMaichinState(0x1000) | GetMaichinState(0x4000) | mirror word 22 | 24 | 6.
 * merakiplus's own start in flight (act.pending) counts too, for the same three activities. */
static int pw_busy(void){
    uint16_t ws=reg_seen[33]?reg[33]:0;
    return (ws&0x1000)||(ws&0x4000)||(reg_seen[22]&&reg[22])||(reg_seen[24]&&reg[24])||(reg_seen[6]&&reg[6])
         ||act.pending==ACT_GRIND||act.pending==ACT_BREW||act.pending==ACT_STEAM;
}
/* cmd8 like stock's powerOffSend 0x14700C: AA 55 08 | year lo, year hi | month 1-12 | day | hour | minute | second | alarm day, hour,
 * minute | CRC lo, hi (CRC_GetModbus16 over 13 bytes). MCU 0x9C88 copies the 10 bytes to 0x20000E18 and replies 0x88 w. echo;
 * the sleep task only uses the alarm (day + hour + minute = 0 -> none) — date/time there is overwritten from the MCU's own RTC. */
static int pw_cmd8(uint8_t b[15],const struct tm*tm,int ad,int ah,int am){
    int y=tm->tm_year+1900;
    b[0]=0xAA; b[1]=0x55; b[2]=0x08; b[3]=(uint8_t)(y&0xFF); b[4]=(uint8_t)(y>>8); b[5]=(uint8_t)(tm->tm_mon+1); b[6]=(uint8_t)tm->tm_mday;
    b[7]=(uint8_t)tm->tm_hour; b[8]=(uint8_t)tm->tm_min; b[9]=(uint8_t)tm->tm_sec; b[10]=(uint8_t)ad; b[11]=(uint8_t)ah; b[12]=(uint8_t)am;
    uint16_t c=crc16(b,13); b[13]=c&0xFF; b[14]=c>>8; return 15;
}
/* v6.16: the cmd8 date/time is stock's model time (_refresh_systime: SoC UTC + zone + DST) — v6.14 used localtime() of the SoC,
 * i.e. UTC on the device (no TZ). The MCU sets its RTC from these bytes, so the alarm below is compared against the same local time.
 * PREVIEW/selftest: clk_fake (default 2026-11-18 15:35:28 UTC = 16:35:28 in zone #48 without DST). */
static void pw_now_tm(struct tm*tm){ clk_now(tm); }
/* v6.16: pre-arm state (the cmd8 alarm while on) — see sched_step() */
static struct { int sent,ad,ah,am,f8_ok,src; long n_sent,n_ack,mcu_rs; ms_t t_sent; uint8_t f8[15]; } arm;
static void pw_ack8(const uint8_t*p){ if(pw.f8_ok&&!memcmp(p,pw.f8+3,10)) pw.n_ack8++; if(arm.f8_ok&&!memcmp(p,arm.f8+3,10)) arm.n_ack++; }
/* for the OFF view: an open detail is reverted (light/language preview back), no transition, no ripple, no held start */
static void pw_view(int v){
    if(view==V_DETAIL&&sel>=0){ if(SET[sel].flags&SF_BL) bl_set(SET[sel].val); if(SET[sel].flags&SF_LOCAL) lang_set(SET[sel].val); }
    view=v; sel=-1; xsel=-1; tr.type=TR_NONE; rip.on=0; memset(&tch,0,sizeof tch); stp.active=0;
    if(v==V_MAIN){ page=PG_HOME; act.gkind=-1; scroll[PG_HOME]=0; }
    static_dirty=1; force_all=1; ui_kick=1;
}
static void pw_off_begin(ms_t now){
    /* v6.16: the alarm = the next pre-heat occurrence, exactly as stock's performSleepActions -> powerOffSend(day, hour, minute);
     * no schedule (or no valid clock) -> 0/0/0 as in v6.14 */
    struct tm tm; int ad,ah,am; pw_now_tm(&tm); sched_alarm(clk_utc(),&ad,&ah,&am,NULL);
    pw_cmd8(pw.f8,&tm,ad,ah,am); pw.f8_ok=1; uart_send(pw.f8,15); arm.sent=0;   /* the off frame supersedes any pre-arm */
    pw.state=PW_OFFING; pw.t_req=pw.t_state=now; pw.push_set=0; pw.t_peek=0; pw.t_on1=0; pw.n_off++;
    toast.dur=0; pw_view(V_OFF);
}
static void pw_fail(ms_t now){   /* PowerOff did not get through: back to ON (the light has not been turned off yet) */
    pw.state=PW_ON; pw.t_state=now; pw.n_fail++; if(bl_is_off) bl_power(1); pw_view(V_MAIN); toast_show(TR(TX_T_PW_NOACK),C_ERR,now);
}
static void pw_on(ms_t now){
    pw.state=PW_ON; pw.t_state=now; pw.t_on1=0; pw.t_peek=0; pw.n_on++;
    arm.sent=0;                          /* v6.16: the MCU woke = CPU reset (0x326): its RAM alarm is gone -> sched_step re-arms */
    if(bl_is_off) bl_power(1);
    proto_keepawake(now,1);              /* PowerOn (sub0=[1]) + awake (sub1=[0]) — the same two frames as at startup (stock: app_init PowerOn) */
    pw_view(V_MAIN); toast_show(TR(TX_T_PW_ON),C_OK,now);
}
static void pw_wake(ms_t now){ pw.state=PW_WAKING; pw.t_state=now; pw.t_on1=0; pw.t_peek=0; if(bl_is_off) bl_power(1); static_dirty=1; ui_kick=1; }
/* button event from the k62091 node (all EV_KEY codes, like stock); value 2 (autorepeat) does not exist on the node and is ignored */
static void pw_key(int code,int val,ms_t now){
    pw.n_ev++; pw.key_code=code; pw.key_val=val; pw.t_key=now; ui_kick=1;
    idle_kick(now);                      /* v6.16 */
    if(val==1&&!pw.key_down){
        pw.key_down=1; pw.t_press=pw.t_down=now; pw.key_fired=0; pw.blk_shown=0; pw.n_press++;
        if(pw.state==PW_OFF){ pw.key_fired=1; pw_wake(now); }
    } else if(val==0&&pw.key_down){
        pw.key_down=0; if(pw.state==PW_ON&&!pw.key_fired) pw.n_short++;    /* short press: stock only wakes its screensaver — merakiplus has none */
        pw.key_fired=0;
    }
}
/* tap on the screen while off: swallowed (no navigation); the OFF page is shown for 8 s */
static int pw_touch(int ev,ms_t now){
    if(pw.state==PW_ON) return 0;
    if(ev==TE_DOWN&&pw.state==PW_OFF){ pw.t_peek=now+PW_PEEK_MS; if(bl_is_off) bl_power(1); }
    return 1;
}
/* the controller answers with word 0 = 1 (and has done so for PW_SEEN_MS): it is awake */
static int pw_mcu_on(ms_t now){
    if(link_up(now)&&reg_seen[0]&&reg[0]==1){ if(!pw.t_on1) pw.t_on1=now; return now-pw.t_on1>=PW_SEEN_MS; }
    pw.t_on1=0; return 0;
}
static void pw_step(ms_t now){
    if(pw.state==PW_ON&&pw.key_down&&!pw.key_fired){
        if(pw_busy()){ pw.t_down=now;                 /* stock: e60 does not count while the activity runs -> a new full hold after it */
            if(now-pw.t_press>=PW_HOLD_MS&&!pw.blk_shown){ pw.blk_shown=1; pw.n_blk++; toast_show(TR(TX_T_PW_BUSY),C_WARN,now); ui_kick=1; } }
        else if(now-pw.t_down<PW_HOLD_MS||view==V_SPLASH||view==V_BYE||sav.active){ /* still held / wait: startup, switch or SAVE in progress */ }
        else { pw.key_fired=1; ui_kick=1;
            if(ro_mode) toast_show(TR(TX_G_RO),C_WARN,now);
            else if(!link_up(now)||!reg_seen[0]||reg[0]==0){ pw.n_fail++; toast_show(TR(TX_T_PW_NOLINK),C_WARN,now); }   /* stock: mirror word 0 == 0 -> nothing */
            else pw_off_begin(now); }
    }
    switch(pw.state){
    case PW_OFFING:
        if(!pw.push_set&&now-pw.t_req>=PW_GAP_MS){
            uint16_t w[2]={0,0}; pw.push_set=1; pw.push_seq=wq.seq;
            if(!wq_push_t(0,w,2,I_POWEROFF)) pw_fail(now);
        } else if(pw.push_set&&wq.seq!=pw.push_seq&&!wq_busy()){
            if(wq.result==WR_OK){ pw.state=PW_OFF; pw.t_state=now; pw.t_on1=0; static_dirty=1; ui_kick=1; } else pw_fail(now);
        }
        break;
    case PW_OFF: {
        int lit=now-pw.t_req<PW_BL_MS||(pw.t_peek&&now<pw.t_peek);
        if(!lit){ pw.t_peek=0; if(!bl_is_off) bl_power(0); }
        if(pw_mcu_on(now)){ pw.n_auto++; pw_on(now); }
        break; }
    case PW_WAKING:
        if(pw_mcu_on(now)) pw_on(now);
        else if(now-pw.t_state>=PW_WAKE_MS){ pw.n_wto++; pw.state=PW_OFF; pw.t_state=now; pw.t_peek=0; static_dirty=1; ui_kick=1; }
        break;
    }
}
/* ================= v6.16: pre-arm + one-shot use + auto off (the stock semantics are at "TIME & SCHEDULING") =================
 * PRE-ARM: while ON (link, word 0 = 1, boot push done, write queue idle, not read-only) the alarm stock would send at power-off
 * (sched_alarm) is sent ahead with the same 15-byte cmd8 whenever it differs from what this MCU session last got: after a start,
 * after an MCU restart/wake (its RAM alarm is gone), after a schedule change, and once a day when the occurrence moves on.
 * Never while turning off/off (the power-off frame carries the alarm itself). No schedule and nothing armed -> nothing is sent
 * (v6.15 traffic unchanged); disarming sends alarm 0/0/0 once. At most one cmd8 per ARM_GAP_MS.
 * ONE-SHOT: once its occurrence (once_at) is a minute in the past it is switched off and saved (stock: deal_preheat_data at start).
 * AUTO OFF: no touch, no button and no activity (any active phase, not only brew/steam/grind) for N minutes -> the same power-off as
 * the button (pw_off_begin: cmd8 w. the alarm, 80 ms, cmd6 sub0 len2 [0,0]); a toast warns 30 s before. Only when the power-off
 * itself would be allowed (link, word 0 = 1, not read-only, no SAVE in flight). Stock: fixed 30 min (SleepRefresh 0x149268). */
#define ARM_GAP_MS   5000
#define AO_WARN_MS  30000
static ms_t idle_t0=0; static int ao_warned=0; static long ao_n=0;
static void idle_kick(ms_t now){ idle_t0=now; if(ao_warned){ ao_warned=0; toast.dur=0; } }   /* a touch also dismisses the 30 s warning */
static int sched_ok_to_send(ms_t now){
    return pw.state==PW_ON&&!ro_mode&&link_up(now)&&reg_seen[0]&&reg[0]==1&&(bp.state==BP_DONE||bp.state==BP_OFF)&&!wq_busy()&&view!=V_SPLASH&&view!=V_BYE;
}
static void sched_step(ms_t now){
    int64_t u=clk_utc();
    if(!idle_t0) idle_kick(now);                                                   /* first call: the idle timer starts now */
    if(sch.on&&!REP_MASK[sch.rep]&&sch.once_at>0&&clk_valid(u)&&u>=sch.once_at+60){   /* one-shot used */
        sch.on=0; sch.once_at=0; SET[S_PREHEAT].val=0; sched_save(); toast_show(TR(TX_T_ONCE_USED),C_OK,now); ui_kick=1;
    }
    if(arm.mcu_rs!=repush_count()){ arm.mcu_rs=repush_count(); arm.sent=0; }        /* MCU restart: the alarm in its RAM is gone */
    int ad,ah,am; int want=sched_alarm(u,&ad,&ah,&am,NULL);
    int need=arm.sent?(ad!=arm.ad||ah!=arm.ah||am!=arm.am):want;
    if(need&&sched_ok_to_send(now)&&(!arm.n_sent||now-arm.t_sent>=ARM_GAP_MS)){
        struct tm tm; pw_now_tm(&tm); pw_cmd8(arm.f8,&tm,ad,ah,am); arm.f8_ok=1; uart_send(arm.f8,15);
        arm.sent=want; arm.ad=ad; arm.ah=ah; arm.am=am; arm.t_sent=now; arm.n_sent++;
    }
    /* auto off */
    if(pw.state!=PW_ON||pw_busy()||phase_active(phase_raw(now))||act.pending!=ACT_NONE||pw.key_down){ idle_kick(now); return; }
    int mins=AO_MIN[SET[S_AUTOOFF].val>=0&&SET[S_AUTOOFF].val<5?SET[S_AUTOOFF].val:0];
    if(!mins||view==V_SPLASH||view==V_BYE||sav.active||ro_mode||!link_up(now)||!reg_seen[0]||reg[0]!=1){ if(!mins) ao_warned=0; return; }
    ms_t left=(ms_t)mins*60000-(now-idle_t0);
    if(left<=AO_WARN_MS&&!ao_warned){ ao_warned=1; toast_show(TR(TX_T_AUTOOFF_SOON),C_WARN,now); toast.dur=AO_WARN_MS; ui_kick=1; }
    if(left<=0){ ao_n++; idle_kick(now); pw_off_begin(now); }
}
/* ================= v6.18: SOUND — start jingle + one ready tone PER BOILER (docs/audio.md, UI_v6.md "CHANGES-v6.18") =================
 * STOCK (demo_v9, reversed): one sound thread (playpthread_init 0x137478 -> deal_play 0x13725C) waits on a semaphore that
 * set_volumn_type 0x1374B8 posts with a slot 0..10 (0x1A588C); the slot picks a file under /customer/audio/ (slide, keyboard,
 * LackWater, toasterror, fault, power, preheatCompele, nfc, brewcOrMilk x2, milkTempHight) and plays it with
 * mmplayer_play_audio_once 0x136CE8 -> mm_player_set_opts / mm_player_open / mm_player_set_volumn (libhvplayer.so, ffmpeg) ->
 * MI_AO (libmi_ao.so). Volume = config "volume" capped at 70 (deal_play 0x1372C4); libhvplayer maps it to
 * dB = min(vol*90/100 - 60, 15), vol 0 = mute (mm_player_set_volumn 0x563C). Nothing plays while the power-on logo runs
 * (model+0x15B0) or while a brew/steam animation video runs (model+0x15AD) — such a request is DROPPED.
 *  - START JINGLE: the audio track of video/power.mp4, played by the logo page (page_logo onEnter 0x1001B8) at every power-on.
 *  - "PRE-HEAT COMPLETE" (slot 6, preheatCompele.mp3): uartRxProcess 0x145BA0 — ONCE per power-on (latch model+0x15AA, cleared only by
 *    the logo page) when BOTH mirror words 62 AND 63 are > 99. The UI mirrors the MCU's registers at model+0xFE8+2*word, so
 *    model+0x1064/0x1066 = words 62/63 (also HOME's two arcs, home_date_update 0x10DFF0/0x10E0DC).
 * MCU (MEM03 boiler_heat 0x3502) writes words 62/63 as a heat STAGE 0/30/60/100, not a power:
 *    extract (62): e_boiler < 40.0 °C -> 0, < 60.0 -> 30, < brew set temp (word 11) -> 60, else 100 (+ an MCU-internal state test);
 *                  100 while work_sta bit 12. steam (63): s_boiler < 50.0 -> 0, < 80.0 -> 30, < steam target - 10.0 °C -> 60, else 100
 *                  (target by steam gear word 14: 1 = 120.0, 2 = 125.0, 3 = 130.0, else 135.0 °C); 100 while work_sta bit 5.
 * MERAKI+: the same signal, but PER BOILER — a ready tone when the EXTRACT boiler's stage first reaches 100 and a different one when
 * the STEAM boiler's does (fallback if a word was never received: word 40 >= word 11 / word 47 >= steam target - 10 °C).
 *  - ONCE per heat-up; RE-ARM only after that boiler has really cooled: stage <= 30, or 15.0 °C below its ready point (so the
 *    dips of a brew or a steam session never re-trigger it).
 *  - START JINGLE once per power-on: OFF/sleep -> on, and the first "on" after a cold boot of the display (SoC uptime < 120 s).
 *  - v6.19: at EVERY "on" (power-on with the jingle, and a Meraki+ start on a machine that was already on — switch from the stock
 *    UI, watchdog restart — without it) a boiler that is already hot (stage 100, decided over the first SND_SETTLE_MS so stale
 *    first samples do not count) counts as announced: a ready tone only for a real cold -> hot transition during the session
 *    (stock plays preheatCompele right after its logo even when both are hot; the user found jingle + 2 tones redundant).
 *  - SILENT while off / sleeping / TURNING ON… (nothing is queued), and a ready tone that comes due while an activity runs is
 *    dropped like stock drops it during its animations (logged "suppressed"). OFFLINE (no link) holds everything (no decision).
 *  - SOUND off (own file) / READY TONE per boiler / config volume 0 = mute: the event is logged, nothing is played.
 * Playback (device): the helper bin merakiplus_snd (glibc, dlopens the display's own libmi_sys.so + libmi_ao.so) is started per
 * sound with the file and the volume — WAV = our own PCM, sent to MI_AO exactly like libhvplayer's mm_audio_init/mm_audio_play
 * (48 kHz, 16 bit, 6 x 2048 frames, SetVolume in dB), MP3 (stock sounds) via libhvplayer's mm_player like stock. One sound at a
 * time, the rest queued (max SND_QMAX); the helper ends itself after 8 s (alarm) — merakiplus sends no signal; if one still runs
 * after SND_TIMEOUT_MS, sounds are skipped ("noplayer") until it has exited. No UART frame, display + audio only. */
#define SND_BOOT_WINDOW_S 120     /* SoC uptime at Meraki+'s start below this = a cold boot of the display (mains on) */
#define SND_REARM_DROP    150     /* 15.0 °C below the ready point re-arms a boiler's tone */
#define SND_VOL_CAP        70     /* stock deal_play: volume > 70 -> 70 */
#define SND_QMAX            4
#define SND_TIMEOUT_MS  10000     /* the helper ends itself after 8 s (alarm); merakiplus never sends it a signal */
#define SND_LOGN            8
#define SND_SETTLE_MS    1500     /* a start on a running machine: "already hot" is decided over the first 1.5 s of link */
enum { SE_JINGLE=0, SE_READY_E, SE_READY_S, SE_PREVIEW, NSE };
static const char*const SE_KEY[NSE]={"start_jingle","boiler_ready_extract","boiler_ready_steam","preview"};
enum { SO_PLAYED=0, SO_OFF, SO_MUTED, SO_SUPPRESSED, SO_NOPLAYER, SO_DROPPED, NSO };
static const char*const SO_KEY[NSO]={"played","off","muted","suppressed","noplayer","dropped"};
/* v6.21: the helper and the WAVs live in the UI's own release directory (ota.dir; legacy: /new_customer/merakiplus) */
static const char*const SND_WAV[NSE]={"start.wav","ready_extract.wav","ready_steam.wav","ready_extract.wav"};
static const char* snd_path_of(int ev){ static char b[NSE][360]; snprintf(b[ev],sizeof b[ev],"%s/audio/%s",ota.dir[0]?ota.dir:OTA_DIR,SND_WAV[ev]); return b[ev]; }
static const char* snd_helper(void){ static char b[360]; snprintf(b,sizeof b,"%s/merakiplus_snd",ota.dir[0]?ota.dir:OTA_DIR); return b; }
#define SND_HELPER snd_helper()
#define SND_OURS_(ev) snd_path_of(ev)
static const char*const SND_STOCK[NSE]={"/customer/audio/power.mp3","/customer/audio/preheatCompele.mp3","/customer/audio/preheatCompele.mp3","/customer/audio/keyboard.mp3"};
static struct {
    int init, st, seen_off, cold_boot, settle, armed[2], ready[2];
    int n[NSE], n_played[NSE], n_out[NSO];
    ms_t t_on;
    struct { ms_t t; signed char ev, out; } log[SND_LOGN]; int log_n;
    int q[SND_QMAX], qn; int pid; ms_t t_start; int cur, last_rc, n_spawn, stuck, n_stuck;
    char last_file[360];
} snd;
static int snd_steam_thr(void){   /* MCU boiler_heat 0x36F6..0x3714 (target by gear) + 0x37F4 (ready = target - 10.0 °C) */
    int g=reg[14], t=g==1?1200:(g==2?1250:(g==3?1300:1350)); return t-100;
}
static int snd_ready(int b){
    int w=b?63:62;
    if(reg_seen[w]) return reg[w]>=100;                               /* the MCU's own heat stage (stock: > 99) */
    if(!b) return reg_seen[40]&&reg_seen[11]&&(int)reg[40]>=(int)reg[11];
    return reg_seen[47]&&(int)reg[47]>=snd_steam_thr();
}
static int snd_cooled(int b){
    int w=b?63:62;
    if(reg_seen[w]&&reg[w]<=30) return 1;                            /* stage 0/30: extract < 60 °C, steam < 80 °C */
    if(!b) return reg_seen[40]&&reg_seen[11]&&(int)reg[40]<(int)reg[11]-SND_REARM_DROP;
    return reg_seen[47]&&(int)reg[47]<snd_steam_thr()-SND_REARM_DROP;
}
/* 0 = no decision (no link while the UI thinks the machine is on), 1 = off / sleeping / turning on, 2 = on */
static int snd_machine(ms_t now){
    if(pw.state!=PW_ON||view==V_OFF) return 1;
    if(!link_up(now)) return 0;
    if(phase_raw(now)==PH_SLEEP||(reg_seen[0]&&reg[0]!=1)) return 1;
    return 2;
}
static int snd_vol(void){ int v=SET[S_VOL].loaded?SET[S_VOL].val:SET[S_VOL].def; return v<0?0:(v>SND_VOL_CAP?SND_VOL_CAP:v); }
static double snd_uptime_ovr=-1;                                  /* preview/selftest: a fixed SoC uptime (s) */
static int snd_cold_boot(void){
    char b[64]; int fd=snd_uptime_ovr>=0?-1:open(rp("/proc/uptime"),O_RDONLY); double up=snd_uptime_ovr;
    if(fd>=0){ int n=(int)read(fd,b,sizeof b-1); close(fd); if(n>0){ b[n]=0; up=atof(b); } }
    if(up<0) up=(double)now_ms()/1000.0;                             /* CLOCK_MONOTONIC = time since boot */
    return up<SND_BOOT_WINDOW_S;
}
static const char* snd_file(int ev){
    if(SET[S_SOUND].val==2&&access(rp(SND_STOCK[ev]),R_OK)==0) return SND_STOCK[ev];   /* the stock UI's own files (never shipped) */
    return SND_OURS_(ev);
}
#ifdef DEVICE_RT
static int snd_spawn(int ev){
    char vb[8]; snprintf(vb,sizeof vb,"%d",snd_vol()); const char*f=snd_file(ev);
    if(access(SND_HELPER,X_OK)!=0||access(f,R_OK)!=0) return -1;
    fflush(stdout); fflush(stderr);
    pid_t c=fork(); if(c<0) return -1;
    if(c==0){
        setsid(); if(chdir("/")!=0){}
        int nul=open("/dev/null",O_RDWR), o=open("/tmp/merakiplus_snd.log",O_WRONLY|O_CREAT|O_APPEND,0644);
        if(nul>=0){ dup2(nul,0); dup2(o>=0?o:nul,1); dup2(o>=0?o:nul,2); }
        for(int fd=3;fd<1024;fd++) close(fd);
        signal(SIGINT,SIG_DFL); signal(SIGTERM,SIG_DFL);
        setenv("LD_LIBRARY_PATH","/lib:/customer/libs",1);           /* libmi_*.so (/lib) + libhvplayer & ffmpeg (stock's libs dir) */
        execl(SND_HELPER,"merakiplus_snd",f,vb,(char*)NULL);
        _exit(127);
    }
    snprintf(snd.last_file,sizeof snd.last_file,"%s",f);
    return (int)c;
}
#endif
/* the player: one at a time; device = helper process (reaped with waitpid, never signalled), SIM/preview = the event log only */
static void snd_service(ms_t now){
#ifdef DEVICE_RT
    if(snd.pid>0){
        int st=0; pid_t r=waitpid(snd.pid,&st,WNOHANG);
        if(r==snd.pid||r<0){ snd.last_rc=(r==snd.pid&&WIFEXITED(st))?WEXITSTATUS(st):-1; snd.pid=0; snd.stuck=0; }
        else { if(now-snd.t_start>SND_TIMEOUT_MS&&!snd.stuck){ snd.stuck=1; snd.n_stuck++; snd.qn=0; } return; }   /* a hung helper: no new sound until it is gone */
    }
    while(snd.qn>0&&snd.pid==0){
        int ev=snd.q[0]; memmove(snd.q,snd.q+1,(size_t)(--snd.qn)*sizeof snd.q[0]);
        int c=snd_spawn(ev); if(c>0){ snd.pid=c; snd.t_start=now; snd.cur=ev; snd.n_spawn++; } else snd.last_rc=127;
    }
#else
    (void)now; snd.qn=0;                                              /* SIM / preview: "played" = queued for the speaker */
#endif
}
static void snd_log(int ev,int out,ms_t now){
    snd.n[ev]++; snd.n_out[out]++; if(out==SO_PLAYED) snd.n_played[ev]++;
    memmove(snd.log+1,snd.log,sizeof snd.log[0]*(SND_LOGN-1)); snd.log[0].t=now; snd.log[0].ev=(signed char)ev; snd.log[0].out=(signed char)out;
    if(snd.log_n<SND_LOGN) snd.log_n++;
#ifdef DEVICE_RT
    { FILE*f=fopen("/tmp/merakiplus_snd.log","a");                   /* for the on-device check: one line per event */
      if(f){ fprintf(f,"%lld %s %s vol=%d mode=%d ready=%d\n",(long long)now,SE_KEY[ev],SO_KEY[out],snd_vol(),SET[S_SOUND].val,SET[S_SNDRDY].val); fclose(f); } }
#endif
}
static void snd_emit(int ev,ms_t now){
    int out=SO_PLAYED, m=SET[S_SOUND].val, r=SET[S_SNDRDY].val;
    if(m<=0) out=SO_OFF;
    else if((ev==SE_READY_E&&!(r&1))||(ev==SE_READY_S&&!(r&2))) out=SO_OFF;
    else if(snd_vol()<=0) out=SO_MUTED;                               /* stock: volume 0 = mute */
#ifdef DEVICE_RT
    else if(access(SND_HELPER,X_OK)!=0||snd.stuck) out=SO_NOPLAYER;
#endif
    else if(snd.qn>=SND_QMAX) out=SO_DROPPED;
    if(out==SO_PLAYED){
#ifndef DEVICE_RT
        snprintf(snd.last_file,sizeof snd.last_file,"%s",snd_file(ev));
#endif
        snd.q[snd.qn++]=ev; snd_service(now); }
    snd_log(ev,out,now);
}
static void snd_flush(void){ snd.qn=0; }                              /* off/sleep: nothing still waiting may play later */
static int snd_activity(ms_t now){ return phase_active(phase_raw(now))||steam_bits()||act.pending!=ACT_NONE; }
static void snd_tick(ms_t now){
    if(!snd.init){ memset(&snd,0,sizeof snd); snd.init=1; snd.cold_boot=snd_cold_boot(); snd.armed[0]=snd.armed[1]=1; }
    snd_service(now);
    int st=snd_machine(now);
    if(st==0) return;                                                 /* OFFLINE: hold (a link glitch is not a power cycle) */
    if(st==1){
        if(snd.st!=1){ snd.seen_off=1; snd.armed[0]=snd.armed[1]=1; snd_flush(); }
        snd.st=1; return;
    }
    if(snd.st!=2){                                                    /* -> on */
        int power_on=snd.seen_off||snd.cold_boot;
        snd.st=2; snd.t_on=now; snd.cold_boot=0;
        if(power_on) snd_emit(SE_JINGLE,now);
        snd.settle=1;          /* v6.19: power-on too — a boiler already hot now is announced (only the jingle plays); before: both armed */
    }
    if(snd.settle){            /* "already hot?" = the LAST sample of the window: the first ones after a (re)start / wake may be stale */
        for(int b=0;b<2;b++){ snd.ready[b]=snd_ready(b); snd.armed[b]=!snd.ready[b]; }
        if(now-snd.t_on>=SND_SETTLE_MS) snd.settle=0;
        return;
    }
    int busy=snd_activity(now);
    for(int b=0;b<2;b++){
        int r=snd_ready(b);
        if(!snd.armed[b]&&!r&&snd_cooled(b)) snd.armed[b]=1;
        if(snd.armed[b]&&r){ snd.armed[b]=0; if(busy) snd_log(b?SE_READY_S:SE_READY_E,SO_SUPPRESSED,now); else snd_emit(b?SE_READY_S:SE_READY_E,now); }
        snd.ready[b]=r;
    }
}
static void snd_preview(ms_t now){ if(snd_machine(now)==2) snd_emit(SE_PREVIEW,now); }
/* DIAGNOSTICS: "ON · 31" / "STOCK · 31" / "OFF" / "VOLUME 0" / "NO PLAYER" + the last event "BREW · PLAYED"; returns the mode cell's colour */
static uint32_t snd_diag_text(char*m,size_t mn,char*l,size_t ln){
    static const short EV[NSE]={TX_DV_SE_JINGLE,TX_DV_SE_BREW,TX_DV_SE_STEAM,TX_DV_SE_PREVIEW};
    static const short OUT[NSO]={TX_DV_SO_PLAYED,TX_DV_SND_OFF,TX_DV_SO_MUTED,TX_DV_SO_SKIP,TX_DV_SO_FAIL,TX_DV_SO_DROP};
    uint32_t c=C_TXT; int v=snd_vol(), md=SET[S_SOUND].val;
    if(md<=0){ snprintf(m,mn,"%s",TR(TX_DV_SND_OFF)); c=C_TXT2; }
    else if(v<=0){ snprintf(m,mn,"%s",TR(TX_DV_SND_MUTE)); c=C_WARN; }
#ifdef DEVICE_RT
    else if(access(SND_HELPER,X_OK)!=0){ snprintf(m,mn,"%s",TR(TX_DV_SND_NOHELPER)); c=C_WARN; }
#endif
    else snprintf(m,mn,TR(md==2?TX_DV_SND_STOCK:TX_DV_SND_ON),v);
    if(snd.log_n) snprintf(l,ln,"%s · %s",TR(EV[(int)snd.log[0].ev]),TR(OUT[(int)snd.log[0].out]));
    else snprintf(l,ln,"%s",TR(TX_DV_T_NONE));
    return c;
}
static void ui_touch(int ev,int x,int y,ms_t now){
    ui_kick=1;
    idle_kick(now);                                                  /* v6.16: every touch restarts the auto-off timer (stock touch_feedback_cb) */
    if(pw_touch(ev,now)) return;                                     /* v6.14: off — the screen shows only the OFF page */
    if(ev==TE_DOWN){
        memset(&tch,0,sizeof tch); tch.down=1; tch.x0=tch.x=x; tch.y0=tch.y=y; tch.t0=now;
        tch.target=hit_test(x,y,now); tch.scroll0=scroll[page];
        if(tch.target){ rip.x=x; rip.y=y; rip.t0=now; rip.on=1; }
        if((tch.target==T_MINUS||tch.target==T_PLUS)&&!sav.active){
            if(view==V_SYS) x_edit=(tch.target==T_PLUS);               /* BOOT UI: − = ORIGINAL, + = MERAKIPLUS */
            else { edit_step(tch.target==T_PLUS?1:-1,1); tch.rep_next=now+420; }
        }
    } else if(ev==TE_MOVE&&tch.down){
        tch.x=x; tch.y=y; int dx=x-tch.x0, dy=y-tch.y0;
        if(!tch.moved&&(abs(dx)>14||abs(dy)>14)){
            tch.moved=1; tch.axis=abs(dy)>abs(dx)?2:1;                       /* v6.11: axis lock at first movement (1 = horizontal, 2 = vertical) */
            if(view==V_MAIN&&page!=PG_HOME&&tch.axis==2&&tch.y0>=LIST_VY0) tch.scrolling=1;   /* lists + DIAGNOSE (v6.17: a drag that starts on the header is the close gesture) */
        }
        if(tch.scrolling) scroll[page]=clampf(tch.scroll0-dy,0,scroll_max(page));
    } else if(ev==TE_UP&&tch.down){
        tch.down=0; int dx=tch.x-tch.x0, dy=tch.y-tch.y0;
        if(tch.scrolling) return;
        int stepper=tch.target==T_MINUS||tch.target==T_PLUS;
        if(!tr.type&&dx>0&&swipe_h(tch.axis,dx,dy)&&!stepper){                 /* v6.17: swipe right = back (the chevron's shortcut) */
            if(view==V_MAIN&&page!=PG_HOME){ nav_back(now); return; }
            if(view==V_DETAIL||view==V_SYS){ activate(T_CANCEL,now); return; }
        }
        if((view==V_DETAIL||view==V_SYS)&&!tr.type&&swipe_down(tch.axis,tch.target,dx,dy)){ activate(T_CANCEL,now); return; }   /* v6.11: swipe down = close */
        if(view==V_MAIN&&page!=PG_HOME&&!tr.type&&tch.y0<LIST_VY0&&swipe_down(tch.axis,tch.target,dx,dy)){ nav_back(now); return; }   /* v6.17: pulling the header down closes a page (inside the list a vertical drag scrolls) */
        if(!tch.moved&&tch.target&&!tch.fired&&tch.target!=T_MINUS&&tch.target!=T_PLUS&&hit_test(tch.x,tch.y,now)==tch.target) activate(tch.target,now);
    }
}
static int pressed(int t){ return tch.down&&!tch.moved&&tch.target==t; }

/* ================= per-frame logic ================= */
static int gauge_kind(int ph){ switch(ph){ case PH_GRIND: case PH_WATER: return 1; case PH_PRE: case PH_EXT: return 2; case PH_STEAM: return 3; default: return 0; } }
typedef struct { int ph,eff; uint32_t hi,lo; char hero[16]; const char*unit; char label[64],sub[80]; const char*sl[3]; char svv[3][16]; float gf,gmark,pf; int pvis;
                 int sk,conc; const char*chip,*badge;
                 char hint1[72]; const char*hint2; uint32_t hc1; char hint2b[72]; char alab[4][16]; } HomeM;   /* v6.17: alab = each icon's target, drawn under it */   /* v6.11: empty-state hint in the chart area (only when the chart is inactive) */   /* v6.6: steam class, concurrent steam, chip text, steam marker (NULL = none) */
/* the three stat labels per HOME state (the selftest measures each trio against the columns, in both languages) */
enum { SS_IDLE=0, SS_BREW, SS_GRIND, SS_WATER, SS_STEAM, SS_BREW_M, SS_GRIND_M, SS_WATER_M, NSS };   /* _M: concurrent steam — the 3rd field is the milk temperature */
_Static_assert(SS_GRIND_M-SS_GRIND==SS_BREW_M-SS_BREW&&SS_WATER_M-SS_WATER==SS_BREW_M-SS_BREW,"SS_*_M must be at the same offset from SS_*");
static const short STAT_SET[NSS][3]={
 [SS_IDLE] ={TX_ST_GROUP,TX_ST_STEAMBOILER,TX_ST_WEIGHT},
 [SS_BREW] ={TX_ST_TIME,TX_ST_BAR,TX_ST_FLOW},
 [SS_GRIND]={TX_ST_TIME,TX_ST_CURRENT,TX_ST_GRINDSTEP},
 [SS_WATER]={TX_ST_TIME,TX_ST_FLOW,TX_ST_BOILER},
 [SS_STEAM]={TX_ST_TIME,TX_ST_BOILER,TX_ST_LEVEL},          /* v6.11: BOILER (the steam context) — "STEAM" next to "LEVEL" reads as "STEAM LEVEL" */
 [SS_BREW_M] ={TX_ST_TIME,TX_ST_BAR,TX_ST_MILK},
 [SS_GRIND_M]={TX_ST_TIME,TX_ST_CURRENT,TX_ST_MILK},
 [SS_WATER_M]={TX_ST_TIME,TX_ST_FLOW,TX_ST_MILK},
};
#define STAT_PITCH 94
#define STAT_GAP 16        /* v6.11: minimum space between two stat labels (v6.10: 6 px — "GROUP STEAM BOILER WEIGHT" read as one line) */
#define SUB_X0 72          /* the HOME subline's area (v6.3: 336 px — also room for °F values and the English texts) */
#define SUB_W 336
static HomeM hm;
static int steam_target(void){ return reg_seen[25]?reg[25]:sv(S_STEAMT); }
#define MILK_RISE10 39     /* the milk probe keeps rising approx. 3.9 °C after auto-stop (STEAMLIVE §5.2: 650 -> 689 in ~11 s) */
/* v6.9: the hero filter (median-3 + EMA at a 10 Hz rate). key = phase<<8 | source word, set = setpoint/target: if either changes,
 * the number snaps to the raw value. Returns the shown value in raw units (rounded) — RAW when DIAGNOSE → GRAPH is RAW. */
static int hero_v(int key,int raw,int set,ms_t now){
    if(cd.raw){ hf.ok=0; return raw; }
    if(!hf.ok||key!=hf.key||set!=hf.set||now-hf.t>500||now<hf.t){ cond_snap(&hf.f,raw); hf.key=key; hf.set=set; hf.t=now; hf.ok=1; hf.n_snap++; return raw; }
    while(now-hf.t>=100){
        hf.t+=100; cond_step(&hf.f,raw,cd.a_hero);
        if(abs(hf.f.m-(int)lroundf(hf.f.y))>CD_HERO_JUMP){ cond_snap(&hf.f,hf.f.m); hf.n_snap++; }   /* real step (the median has moved far): no gliding */
    }
    return (int)lroundf(hf.f.y);
}
/* v6.16: HOME texts — the top line (local clock as stock's update_time; wordmark while the clock is not set) and the planned pre-heat */
#define HOME_BRAND_Y 54
static int home_clock_text(char*b,size_t n){
    struct tm lt; if(!clk_now(&lt)) return 0;
    fmt_hm(b,n,lt.tm_hour,lt.tm_min,1); return 1;
}
static int home_preheat_text(char*b,size_t n){
    int ad,ah,am; int64_t at; if(!sched_alarm(clk_utc(),&ad,&ah,&am,&at)) return 0;
    struct tm lt; clk_local_at(at,&lt); char t[24]; fmt_hm(t,sizeof t,ah,am,1);
    snprintf(b,n,TR(TX_C_PREHEAT),TR(WD_TX[lt.tm_wday]),t); return 1;
}
#define WMAN_GAUGE 3000.f    /* v6.22: MANUAL hot water — the gauge is full at 300 g (0.1 g); there is no target */
static void home_model(ms_t now){
    HomeM*m=&hm; memset(m,0,sizeof *m);
    int ph=act.ph, e=(ph==PH_DONE)?act.last_ph:ph; m->ph=ph; m->eff=e;
    m->hi=ph_hi(ph); m->lo=ph_lo(ph); m->gmark=-1; m->pvis=0;
    ms_t el=phase_active(ph)? now-(ph==PH_STEAM?stm.t0:act.t_act0) : act.done_ms; char tb[16]; snprintf(tb,sizeof tb,"%ld.%ld %s",(long)(el/1000),(long)((el%1000)/100),TR(TX_U_S));
    char a[16],b[16]; int ss=SS_IDLE;
    if(e==PH_GRIND||e==PH_WATER||e==PH_PRE||e==PH_EXT){
        int wm=e==PH_WATER&&wd.man;                                  /* v6.22: MANUAL hot water has no target — it runs until STOP */
        int tgt=(e==PH_GRIND)?sv(S_GRINDW):(e==PH_WATER?(wm?0:sv(S_HOTW)):sv(S_BREWW));
        int src=wsl.src, w=wdbi(hero_v(ph<<8|src,s16(src),tgt,now)); if(w<0&&w>-30)w=0; if(w<-999)w=-999; if(w>9999)w=9999;   /* v6.9: latched source, conditioned, deadband */
        fmt_fix(m->hero,sizeof m->hero,w,1); m->unit=TR(TX_U_G);
        fmt_fix(a,sizeof a,tgt,1);
        snprintf(m->label,sizeof m->label,"%s",TR(e==PH_GRIND?TX_H_DOSE:TX_H_CUP));   /* v6.17: the target is said ONCE — in the sub-line ("STOPS AT 36.0 G"); v6.16 had it in the label too */
        if(ph==PH_DONE) snprintf(m->sub,sizeof m->sub,TR(TX_H_DONE_IN),tb);
        else if(wm) snprintf(m->sub,sizeof m->sub,"%s",TR(TX_H_WATER_MAN));
        else snprintf(m->sub,sizeof m->sub,TR(TX_H_AUTOSTOP_G),a);
        float fr=tgt>0? (float)w/tgt : 0;
        if(e==PH_PRE||e==PH_EXT){
            float pd=p_disp(now);                                   /* v6.9: gauge + BAR = conditioned pressure (like the chart) */
            m->gf=clampf(pd/1600.f,0,1); m->pf=clampf(fr,0,1); m->pvis=1; ss=SS_BREW;
            snprintf(m->svv[0],16,"%s",tb);
            if(cd.raw) snprintf(m->svv[1],16,"%d",(int)lroundf(pd));   /* v6.10: the RAW view = word 51 as a raw number (label PRESSURE RAW) */
            else { float pb=p_bar(pd); if(pb<0) snprintf(m->svv[1],16,"%s",TR(TX_ST_PULSE)); else snprintf(m->svv[1],16,"%.1f",pb); }   /* v6.10: ≈bar, pulse > 20 bar = no number */
            snprintf(m->svv[2],16,"%u",reg[52]);
        } else {
            m->gf=clampf(fr/1.15f,0,1); m->gmark=1/1.15f;
            if(wm){ m->gf=clampf((float)w/WMAN_GAUGE,0,1); m->gmark=-1; }   /* v6.22: no target mark; the ring fills over WMAN_GAUGE */
            snprintf(m->svv[0],16,"%s",tb);
            if(e==PH_GRIND){ ss=SS_GRIND; snprintf(m->svv[1],16,"%u",reg[58]); snprintf(m->svv[2],16,"%u",reg[37]); }
            else { ss=SS_WATER; snprintf(m->svv[1],16,"%u",reg[52]); fmt_t10(b,sizeof b,reg[40],1); snprintf(m->svv[2],16,"%s°",b); }
        }
        /* v6.6: steam CONCURRENT with the main activity (e.g. flush during brew, work_sta 5327): weight and gauge
         * stay, the steam marker is shown, and the 3rd stat field becomes the milk temperature */
        if(stm.kind!=SK_NONE&&phase_active(ph)){ m->conc=1; m->sk=stm.kind; ss+=SS_BREW_M-SS_BREW; fmt_t10(b,sizeof b,reg[48],1); snprintf(m->svv[2],16,"%s°",b); }
    } else if(e==PH_STEAM){
        int sk=(ph==PH_DONE)?stm.last_kind:stm.kind, tgt=steam_target(); m->sk=sk;
        int mk=hero_v(ph<<8|48,reg[48],tgt*4+sk,now);              /* v6.9: conditioned milk temperature */
        fmt_t10(m->hero,sizeof m->hero,mk,1); m->unit=t_unit(); ss=SS_STEAM;
        fmt_t10(a,sizeof a,tgt,1);
        m->gf=clampf((mk-200)/600.f,0,1);
        if(sk==SK_WAND){            /* momentary wand/flush: no target, no auto-stop expectation — not even above the target */
            snprintf(m->label,sizeof m->label,"%s",TR(TX_H_MILK_PROBE));
            if(ph==PH_DONE) snprintf(m->sub,sizeof m->sub,TR(TX_H_DONE_IN),tb);
            else snprintf(m->sub,sizeof m->sub,"%s",TR(TX_H_WAND_NOSTOP));
            m->gmark=-1;
        } else {                    /* latched (AUTO) — or word 24 not read yet: the target is shown, but without a promise of auto-stop */
            if(sk==SK_AUTO&&ph!=PH_DONE) snprintf(m->label,sizeof m->label,"%s",TR(TX_H_MILK));   /* v6.17: the sub-line carries the target ("STOPS AT 65.0°") */
            else snprintf(m->label,sizeof m->label,TR(TX_H_MILK_TGT),a);
            if(ph==PH_DONE){
                if(sk==SK_AUTO){ fmt_fix(b,sizeof b,use_c()?MILK_RISE10:MILK_RISE10*9/5,-1); snprintf(m->sub,sizeof m->sub,TR(TX_H_DONE_RISE),tb,b); }
                else snprintf(m->sub,sizeof m->sub,TR(TX_H_DONE_IN),tb);
            } else if(sk==SK_AUTO) snprintf(m->sub,sizeof m->sub,TR(TX_H_AUTOSTOP_T),a);
            m->gmark=clampf((tgt-200)/600.f,0,1);
        }
        snprintf(m->svv[0],16,"%s",tb);
        fmt_t10(b,sizeof b,reg[47],0); snprintf(m->svv[1],16,"%s°",b);
        snprintf(m->svv[2],16,"%d",sv(S_STEAMG));
    } else {
        int set=reg_seen[11]?reg[11]:sv(S_BREWT);
        if(ph==PH_OFFLINE){ snprintf(m->hero,sizeof m->hero,"--.-"); m->gf=0; }
        else { int bt=hero_v(ph<<8|40,reg[40],set,now);           /* v6.9: conditioned boiler temperature */
               fmt_t10(m->hero,sizeof m->hero,bt,1); m->gf=clampf((bt-200)/1000.f,0,1); m->gmark=clampf((set-200)/1000.f,0,1); }
        m->unit=t_unit(); fmt_t10(a,sizeof a,set,1);
        snprintf(m->label,sizeof m->label,"%s",TR(TX_H_BOILER));
        switch(ph){
        case PH_OFFLINE: snprintf(m->sub,sizeof m->sub,"%s",TR(TX_H_NORESP)); break;
        case PH_SLEEP:   snprintf(m->sub,sizeof m->sub,"%s",TR(TX_H_WAKING)); break;
        case PH_ERROR:   snprintf(m->sub,sizeof m->sub,"%s",err_text()); break;
        case PH_HEAT:    snprintf(m->sub,sizeof m->sub,TR(TX_H_HEATING_TO),a); break;
        default:         snprintf(m->sub,sizeof m->sub,TR(TX_H_SET_TO),a); break;
        }
        fmt_t10(b,sizeof b,reg[42],0); snprintf(m->svv[0],16,"%s°",b);
        fmt_t10(b,sizeof b,reg[47],0); snprintf(m->svv[1],16,"%s°",b);
        int w=s16(39); if(w<-50||ph==PH_OFFLINE) snprintf(m->svv[2],16,"--"); else { fmt_fix(b,sizeof b,w_disp(39,now),1); snprintf(m->svv[2],16,"%s %s",b,TR(TX_U_G)); }   /* v6.9: conditioned + deadband (the "--" threshold on raw) */
        if(ph==PH_OFFLINE){ snprintf(m->svv[0],16,"--"); snprintf(m->svv[1],16,"--"); }
    }
    for(int i=0;i<3;i++) m->sl[i]=TR(STAT_SET[ss][i]);
    /* v6.11: hint in the empty chart area — what the user can DO now (replaces v6.10's "NO ACTIVITY", which repeated the phase chip).
     * If a start is possible: "HOLD AN ICON TO START"; otherwise the reason (the guard's text) — and on error/offline a pointer to DIAGNOSE,
     * where the raw code (E1/E2/E3) and the link counters are. Line 2 points to the settings (swipe + the gear). */
    { const char*g=act_guard(ACT_BREW,now); m->hint2=NULL; m->hc1=C_TXT3;      /* v6.17: ONE hint line ("HOLD = START · TAP = SETTINGS"); v6.11-v6.16: two */
      if(home_preheat_text(m->hint2b,sizeof m->hint2b)) m->hint2=m->hint2b;    /* v6.16: a planned pre-heat is the second line */
      if(!g) snprintf(m->hint1,sizeof m->hint1,"%s",TR(TX_C_HINT));
      else if(ph==PH_ERROR) snprintf(m->hint1,sizeof m->hint1,TR(TX_C_ERRCODE),err_code());
      else if(ph==PH_OFFLINE) snprintf(m->hint1,sizeof m->hint1,"%s",TR(TX_C_DIAGREF));
      else if(ph==PH_SLEEP) m->hint1[0]=0;                                   /* the subline already says "WAKING UP…" */
      else { snprintf(m->hint1,sizeof m->hint1,"%s",g); m->hc1=C_TXT2; if(g==TR(TX_G_NOCFG)||g==TR(TX_G_OOR)||g==TR(TX_G_RO)) m->hc1=C_WARN; }
      if(g&&diag_hint(ph)) m->hc1=C_TXT2; }
    /* v6.17: what each icon will do, under the icon (grind dose, brew weight, water amount, milk temperature) */
    { char u[16];
      fmt_fix(u,sizeof u,sv(S_GRINDW),1); snprintf(m->alab[ACT_GRIND],16,"%s %s",u,TR(TX_U_G));
      fmt_fix(u,sizeof u,sv(S_BREWW),-1); snprintf(m->alab[ACT_BREW],16,"%s %s",u,TR(TX_U_G));
      fmt_fix(u,sizeof u,sv(S_HOTW),-1);  snprintf(m->alab[ACT_WATER],16,"%s %s",u,TR(TX_U_G));
      int wml=water_manual(); if(wml) snprintf(m->alab[ACT_WATER],16,"%s",TR(TX_A_WATER_MAN));   /* v6.22 */
      fmt_t10(u,sizeof u,sv(S_STEAMT),0); snprintf(m->alab[ACT_STEAM],16,"%s\xC2\xB0",u);
      for(int k=0;k<4;k++){ static const int si[4]={S_GRINDW,S_BREWW,S_HOTW,S_STEAMT}; if(!SET[si[k]].loaded&&!(k==ACT_WATER&&wml)) snprintf(m->alab[k],16,"--"); } }
    if(cd.raw&&(ss==SS_BREW||ss==SS_BREW_M)) m->sl[1]=TR(TX_ST_PRAW);   /* v6.10: the RAW view shows raw numbers — so ≈BAR is not shown above them */
    m->chip=(ph==PH_STEAM)?steam_label(stm.kind):ph_name(ph);
    m->badge=m->conc?steam_label(m->sk):NULL;
    /* pending / hold override the subline. v6.6: steam is "STARTING …" too — display steam starts by
     * itself approx. 0.2 s after the start write, without the wand switch being activated (STEAMLIVE §3 + §6.3) */
    if(act.pending!=ACT_NONE&&!phase_active(ph)) snprintf(m->sub,sizeof m->sub,TR(TX_H_STARTING),TR(ACT_NAME[act.pending]));
    else if(tch.down&&!tch.moved&&tch.target>=T_ACT0&&tch.target<=T_ACT3&&!act_guard(tch.target-T_ACT0,now)){
        int k=tch.target-T_ACT0;
        if(k==ACT_GRIND){ fmt_fix(a,sizeof a,sv(S_GRINDW),1); snprintf(m->sub,sizeof m->sub,TR(TX_H_HOLD_GRIND),a); }
        else if(k==ACT_BREW){ fmt_fix(a,sizeof a,sv(S_BREWW),1); snprintf(m->sub,sizeof m->sub,TR(TX_H_HOLD_BREW),a); }
        else if(k==ACT_WATER&&water_manual()) snprintf(m->sub,sizeof m->sub,"%s",TR(TX_H_HOLD_WATER_MAN));   /* v6.22 */
        else if(k==ACT_WATER){ fmt_fix(a,sizeof a,sv(S_HOTW),1); snprintf(m->sub,sizeof m->sub,TR(TX_H_HOLD_WATER),a); }
        else { fmt_t10(a,sizeof a,sv(S_STEAMT),1); snprintf(m->sub,sizeof m->sub,TR(TX_H_HOLD_STEAM),a); }
    }
}
/* gauge targets for the current view (called per frame AND before the snapshot on transitions) */
static void arc_targets(ms_t now){
    if(view==V_SPLASH){
        float t=clampf((now-ui_start)/(float)(SPLASH_MS-250),0,1);
        garc.vis=1; garc.c_hi=C_ACC; garc.c_lo=rgb(120,80,36); garc.mark=-1; garc.f_t=garc.f_d=ease_io(t); parc.vis=0; ui_anim=1;
    } else if(view==V_DETAIL){
        const Setting*s=&SET[sel]; float span=(float)(s->hi-s->lo);
        garc.vis=1; garc.c_hi=C_ACC; garc.c_lo=rgb(120,80,36); parc.vis=0;
        garc.f_t=span>0? clampf((edit_val-s->lo)/span,0,1) : 0;
        garc.mark=(span>0&&!(s->flags&SF_SENSOR))? clampf((edit_orig-s->lo)/span,0,1) : -1;
    } else if(view==V_SYS){
        parc.vis=0; garc.vis=1; garc.mark=-1;
        if(xsel==X_EXIT){ garc.c_hi=C_WARN; garc.c_lo=rgb(128,80,24); garc.f_t=1; }            /* full warning ring */
        else {
            garc.c_hi=C_ACC; garc.c_lo=rgb(120,80,36); garc.f_t=0;
            if(xsel==X_BOOTUI&&!sys_locked()){ garc.f_t=x_edit?1:0; garc.mark=x_orig?1:0; }
        }
    } else if(view==V_BYE||view==V_OFF){ garc.vis=0; parc.vis=0;
    } else if(page==PG_HOME){
        home_model(now);
        int gk=gauge_kind(hm.eff); if(gk!=act.gkind){ act.gkind=gk; garc.f_d=0; }   /* sweep from zero on a new quantity */
        float ct=clampf((now-act.t_col)/300.f,0,1);
        garc.vis=1; garc.f_t=hm.gf; garc.mark=hm.gmark;
        if(ct<1){ int q=(int)(ct*8); garc.c_hi=lerpc(act.c_from_hi,hm.hi,q/8.f); garc.c_lo=lerpc(act.c_from_lo,hm.lo,q/8.f); ui_anim=1; }
        else { garc.c_hi=hm.hi; garc.c_lo=hm.lo; }
        parc.vis=hm.pvis; parc.c_hi=C_TXT; parc.c_lo=rgb(160,167,180);   /* v6.11: v6.10's C_TXT2 — the progress ring is unchanged (C_TXT2 was raised for text) */ parc.f_t=hm.pf; parc.mark=-1;
    } else { garc.vis=0; parc.vis=0; }
}
/* v6.22: the scale-fault fallback (stock hotWaterUpdate 0x115B40..0x115BD8): on the rising edge of word 35 & 0x1400 a pending water start
 * is dropped (hotWaterCancelStartWait) and, on a > 5.1.2 controller whose word 16 is still set, word 16 = 0 is sent ONCE (usart_send(6,16,1)).
 * The controller's own weight branch stops on the same bits; a MANUAL run is not stopped (stock does not either). No config write. */
static void wd_step(ms_t now){
    int f=link_up(now)&&wd_scale_fault();
    if(f&&!wd.fault){
        wd.n_fault++;
        if(act.pending==ACT_WATER) act.pending=ACT_NONE;
        wd.due=wd_ver_dosing()&&reg_seen[16]&&reg[16]!=0;
    }
    wd.fault=f;
    if(!f||(reg_seen[16]&&reg[16]==0)) wd.due=0;
    if(wd.due&&!ro_mode){ uint16_t z=0; if(wq_push(16,&z,1)){ wd.due=0; wd.n_stop++; toast_show(TR(TX_T_WATER_SCALE),C_WARN,now); } }
}
static void ui_update(ms_t now){
    if(ui_start<0){ ui_start=now; ui_last=now; }
    float dt=(float)(now-ui_last); if(dt>200)dt=200; ui_last=now; ui_anim=0;
    /* phase tracking (v6.6: the steam class first; the main phase is phase_main — grind/brew/water take precedence over steam) */
    steam_track(now);
    int p=phase_main(now);
    if(p!=act.raw){
        if(phase_active(p)&&!phase_active(act.raw)){ act.t_act0=now; act.pending=ACT_NONE; wd.man=0; }
        if(!phase_active(p)&&phase_active(act.raw)){ act.t_done=now; act.done_ms=now-(act.raw==PH_STEAM?stm.t0:act.t_act0); }
        act.raw=p;
    }
    if(phase_active(p)) act.last_ph=p;
    if(p==PH_WATER&&reg_seen[19]&&reg[19]!=0) wd.man=1;            /* v6.22: this water run is MANUAL (word 19) — sticky until the next activity */
    wd_step(now);
    int ph=p; if(p>=PH_HEAT&&!phase_active(p)&&now-act.t_done<8000) ph=PH_DONE;
    if(ph!=act.ph){ act.c_from_hi=garc.c_hi; act.c_from_lo=garc.c_lo; act.t_col=now; act.ph=ph; }
    /* pending activity: ACK result / start timeout */
    if(act.pending==ACT_STEAM&&steam_bits()) act.pending=ACT_NONE;  /* v6.20: steam on top of a brew does not change phase_main */
    if(act.pending!=ACT_NONE){
        if(wq.seq!=act.seq&&wq.result==WR_FAIL&&!wq_busy()){ act.pending=ACT_NONE; toast_show(TR(TX_T_NOACK),C_ERR,now); }
        else if(now-act.t_req>(act.pending==ACT_STEAM?15000:3500)){ act.pending=ACT_NONE; toast_show(TR(TX_T_NOSTART),C_WARN,now); }
    }
    stop_step(now);                                                  /* v6.13: manual STOP — confirmation / failure */
    save_step(now);
    /* v6.17: a feature page is the setup for something that is NOT running. As soon as an activity runs (started here, on the machine or
     * by the wand), the UI is on HOME — the one live view, where STOP is. SYSTEM / DIAGNOSTICS and open editors stay (deliberate). */
    if(view==V_MAIN&&pg_feat(page)&&phase_active(p)&&!tr.type) go_page(PG_HOME,-1,now);
    /* boot state is live (stat + /proc), but only while it is actually shown */
    if((view==V_SYS||(view==V_MAIN&&(page==PG_SYS||page==PG_DIAG)))&&(now-bs.t>=1000||now<bs.t)){
        int l0=(view==V_SYS)?sys_locked():0, m0=switch_mode(), n0=boot_next_kind();
        boot_poll(now);
        if(view==V_SYS){
            if(xsel==X_BOOTUI){ if(x_edit==x_orig) x_edit=bs.flag; x_orig=bs.flag; }
            if(sys_locked()!=l0||switch_mode()!=m0||boot_next_kind()!=n0) static_dirty=1;     /* static text depends on the state */
        }
    }
    /* sensor rows (read-only) follow the controller register */
    for(int i=0;i<NSET;i++){
        Setting*s=&SET[i]; if(!(s->flags&SF_SENSOR)) continue;
        s->loaded=link_up(now)&&reg_seen[s->def]; s->val=s->loaded?reg[s->def]:0;
        if(view==V_DETAIL&&sel==i) edit_val=edit_orig=s->val;
    }
    /* splash -> home */
    if(view==V_SPLASH&&now-ui_start>=SPLASH_MS&&!tr.type){
        memcpy(snapA,canvas,(size_t)W*H*4); view=V_MAIN; static_dirty=1; garc.f_d=0; act.gkind=-1; arc_targets(now); garc.f_d=0; compose(now);
        memcpy(snapB,canvas,(size_t)W*H*4); fade_to(now,320);
    }
    /* hold-to-start + stepper repeat w. acceleration */
    if(tch.down&&!tch.moved){
        if(tch.target>=T_ACT0&&tch.target<=T_ACT3){
            ui_anim=1;
            if(!tch.fired&&now-tch.t0>=HOLD_MS){ tch.fired=1; act_request(tch.target-T_ACT0,now); }
        }
        if(tch.target==T_STEAMH&&view==V_MAIN&&page==PG_HOME){      /* v6.20: HOLD · STEAM during a brew */
            ui_anim=1;
            if(!tch.fired&&now-tch.t0>=HOLD_MS){ tch.fired=1; act_request(ACT_STEAM,now); }
        }
        if(tch.target==T_START&&view==V_MAIN&&pg_feat(page)){      /* v6.17: HOLD TO START on the feature page — the same request as the icon's hold */
            ui_anim=1;
            if(!tch.fired&&now-tch.t0>=HOLD_MS){ int k=page-PG_GRIND; tch.fired=1; act_request(k,now); if(act.pending==k) go_page(PG_HOME,-1,now); }
        }
        if((tch.target==T_MINUS||tch.target==T_PLUS)&&view==V_DETAIL&&!sav.active){
            while(now>=tch.rep_next){
                tch.rep_n++; int mult=tch.rep_n>28?5:1;
                edit_step(tch.target==T_PLUS?1:-1,mult);
                tch.rep_next+= tch.rep_n<5?170:(tch.rep_n<14?90:45);
            }
            ui_anim=1;
        }
    }
    arc_targets(now);
    if(garc.vis&&view!=V_SPLASH) ui_anim|=arc_ease(&garc,dt);
    if(parc.vis) ui_anim|=arc_ease(&parc,dt);
    if(rip.on){ if(now-rip.t0>360) rip.on=0; ui_anim=1; }
    if(toast.dur&&now-toast.t0<toast.dur+50) ui_anim=1;
    if(sav.active||tr.type||tch.scrolling) ui_anim=1;
}

/* ================= drawing: shared parts ================= */
/* The wordmark: BRAND_WORD + "+" badge. Large fonts (cap height >= 24 px): crema disc with
 * a dark plus, raised like a superscript. Small fonts: the plus glyph in the accent colour.
 * brand_at() returns x after the mark (incl. normal letter spacing), so text can continue. */
static int brand_disc(int f){ return FS[f].cap>=24; }
static float brand_wf(int f){
    if(!brand_disc(f)) return text_wf(PRODUCT,f);
    return text_wf(BRAND_WORD,f)+FS[f].cap*0.14f+FS[f].cap*0.60f;
}
static float brand_at(float x,int y,int f,uint32_t cw,int a){
    float cap=FS[f].cap, trail=cap*FS[f].gap+FS[f].track;
    x=text_at(x,y,BRAND_WORD,f,cw,a);
    if(!brand_disc(f)) return text_at(x,y,BRAND_BADGE,f,C_ACC,a);
    float r=cap*0.30f, bx=x-trail+cap*0.14f+r, by=y+r+cap*0.02f, l=r*0.50f;
    disc(bx,by,r,C_ACC,a);
    capsule(bx-l,by,bx+l,by,cap*0.052f,C_ACCD,a); capsule(bx,by-l,bx,by+l,cap*0.052f,C_ACCD,a);
    return bx+r+trail;
}
static void brand_c(int cx,int y,int f,uint32_t cw,int a){ brand_at(cx-brand_wf(f)/2,y,f,cw,a); }
static void draw_pill_btn(Rect r,const char*txt,int font,uint32_t bg_t,uint32_t bg_b,uint32_t fg,int press){
    int w=r.x1-r.x0,h=r.y1-r.y0;
    rrect_g(r.x0,r.y0+2,w,h,h/2.f,rgb(0,0,0),rgb(0,0,0),70);              /* soft shadow */
    rrect_g(r.x0,r.y0,w,h,h/2.f,bg_t,bg_b,255);
    if(press) rrect(r.x0,r.y0,w,h,h/2.f,rgb(255,255,255),36);
    text_c((r.x0+r.x1)/2,r.y0+(h-(int)FS[font].cap)/2,txt,font,fg,255);
}
static Rect toast_rect(ms_t now){ Rect z={0,0,0,0}; if(!toast.dur||now-toast.t0>=toast.dur) return z; return R(56,90,368,50); }
static Rect rip_rect(void){ Rect z={0,0,0,0}; if(!rip.on) return z; return R((int)rip.x-52,(int)rip.y-52,104,104); }
static void overlays_damage(ms_t now){
    Rect c0=toast_rect(now), c1=rip_rect();
    if(!r_empty(ov_prev[0])) rl_add(&dmg,ov_prev[0]); if(!r_empty(c0)) rl_add(&dmg,c0); ov_prev[0]=c0;
    if(!r_empty(ov_prev[1])) rl_add(&dmg,ov_prev[1]); if(!r_empty(c1)) rl_add(&dmg,c1); ov_prev[1]=c1;
}
static void overlays_draw(ms_t now){
    cv=canvas;
    Rect c1=rip_rect();
    if(!r_empty(c1)){
        set_clip(c1); float t=(now-rip.t0)/360.f; float e=ease_out(clampf(t,0,1));
        ring(rip.x,rip.y,6+e*30,10+e*38,rgb(255,255,255),(int)(54*(1-t)));
        rl_add(&pres,c1);
    }
    Rect c0=toast_rect(now);
    if(!r_empty(c0)){
        set_clip(c0); float t=(float)(now-toast.t0); float a=clampf(t/160.f,0,1)*clampf((toast.dur-t)/260.f,0,1);
        int w=text_w(toast.msg,F_XS)+44; if(w>356)w=356; int x=CX-w/2, y=98-(int)((1-ease_out(clampf(t/200.f,0,1)))*8);
        rrect_g(x,y+3,w,34,17,rgb(0,0,0),rgb(0,0,0),(int)(90*a));
        rrect_g(x,y,w,34,17,rgb(58,63,76),rgb(42,46,57),(int)(250*a));
        disc(x+17,y+17,4,toast.col,(int)(255*a));
        text_at(x+29,y+12,toast.msg,F_XS,C_TXT,(int)(255*a));
        rl_add(&pres,c0);
    }
    set_clip(FULL);
}

/* ================= page: HOME ================= */
enum { W_CHIP=1, W_LABEL, W_HERO, W_SUB, W_CHART, W_STATS, W_ACT, W_FOOT, W_LIST, W_SCROLL, W_DHERO, W_DCHG, W_MINUS, W_PLUS, W_CANCEL, W_SAVE, W_DIAG, W_SPL, W_XNOTE, W_XFOOT, W_BYE, W_GEAR, W_BACKCH, W_OFF, W_CLOCK, W_LIVE, W_START };   /* v6.16: W_CLOCK = HOME's top line (clock / wordmark); v6.17: W_LIVE / W_START (a page's live line, HOLD TO START) */
/* v6.9: latest chart scale (state JSON "chart" + selftest) */
static struct { int mode,active,full,top,raw_max,disp_max,ticks,bticks,n,clip,na,wmax,wtop; } cst;   /* v6.15: + wmax/wtop (weight line, mode 0) */
static int cmp_int(const void*a,const void*b){ int x=*(const int*)a,y=*(const int*)b; return (x>y)-(x<y); }
/* v6.10: the chart, when it shows pressure (latest drawing: active, mode 0), and the BAR field during brew are tap targets for the basis toast */
static int pinfo_hit(int x,int y){
    if(cst.active&&cst.mode==0&&in_r(x,y,118,240,244,54)) return 1;
    return (hm.eff==PH_PRE||hm.eff==PH_EXT)&&in_r(x,y,CX-STAT_PITCH/2,298,STAT_PITCH,40);
}
static int diag_hint_shown(void){ return diag_hint(act.ph)&&!cst.active&&hm.hint1[0]; }
static void draw_chart(Rect r,ms_t now,const HomeM*m){
    int x0=r.x0+4, w=r.x1-r.x0-8, yb=r.y1-5, h=r.y1-r.y0-20, raw=cd.raw;
    static int c1[260], c2[260], cr[260], ci[260], vs[GN], rs[GN];
    /* series choice per phase: brew/water = pressure (area) + weight (line); grind = weight; steam = milk temp */
    int mode=(m->eff==PH_GRIND)?1:(m->eff==PH_STEAM?2:0);
    for(int i=0;i<w;i++){ c1[i]=-1; c2[i]=-100000; cr[i]=-1; ci[i]=-1; }
    ms_t t0=now-40000; int m1=0,m2=0,mr=0,nv=0,nact=0;
    for(int i=0;i<g_n;i++){
        int idx=(g_head+GN-g_n+i)%GN; ms_t t=g_t[idx]; if(t<t0||t>now) continue;
        int c=(int)((t-t0)*(w-1)/40000); if(c<0||c>=w) continue;
        int r1= mode==1? (g_w[idx]<0?0:g_w[idx]) : (mode==2? (g_m[idx]>200?g_m[idx]-200:0) : g_p[idx]);          /* raw */
        /* v6.9: the pressure curve (mode 0) is drawn only for samples during brew/pre-inf./water (g_a). Outside the activity, word 51 is
         * maintenance/pump pulses (BREW.md §5-7, STEAMLIVE.md §5.3) — they are shown as small ticks on the baseline. */
        int ia= mode!=0||g_a[idx];
        int v1= raw? r1 : (!ia? 0 : (int)lroundf(mode==1? fmaxf(wdb(g_wd[idx]),0) : (mode==2? fmaxf(g_md[idx]-200,0) : g_pd[idx])));   /* what is drawn (weight: deadband) */
        if(ia){ if(r1>cr[c]) cr[c]=r1; } else if(r1>ci[c]) ci[c]=r1;
        if(r1>mr) mr=r1; if(mode==0&&g_a[idx]) nact++;
        if(v1>c1[c]) c1[c]=v1; if(v1>m1) m1=v1;
        vs[nv]=v1; rs[nv]=raw||ia?r1:0; nv++;
        if(mode==0){ int w2=raw?g_w[idx]:(int)lroundf(wdb(g_wd[idx])); if(w2>c2[c]) c2[c]=w2; if(w2>m2) m2=w2; }
    }
    int active=phase_active(m->ph)||m->ph==PH_DONE||(mode==0&&(raw?mr>=40:nact>0));   /* v6.9: the idle chart is "active" when the window contains a brew/water */
    cst.mode=mode; cst.active=active; cst.raw_max=mr; cst.disp_max=m1; cst.n=nv; cst.ticks=0; cst.bticks=0; cst.clip=0; cst.na=0;
    cst.wmax=mode==0?m2:0; cst.wtop=mode==0?(int)((m2<100?100:m2)*1.08f):0;
    if(!active){
        for(int x=x0+2;x<x0+w;x+=8) hspan(yb,x,x+3,C_SURF3,255);
        if(!raw) for(int i=0;i<w;i++) if(mode==0&&ci[i]>=40){ cst.bticks++; for(int yy=yb-4;yy<yb;yy++) pset(x0+i,yy,C_DIS,200); }   /* pulses at rest: small ticks on the baseline */
        if(m->hint1[0]&&m->hint2){ text_c(CX,r.y0+8,m->hint1,F_XS,m->hc1,255); text_c(CX,r.y0+26,m->hint2,F_XS,C_TXT3,255); }   /* v6.11: hint instead of "NO ACTIVITY" */
        else if(m->hint1[0]||m->hint2) text_c(CX,r.y0+17,m->hint1[0]?m->hint1:m->hint2,F_XS,m->hint1[0]?m->hc1:C_TXT3,255);   /* v6.17: one line, centred */
        cst.full=cst.top=0;
        return;
    }
    hspan(yb,x0,x0+w,C_LINE,255);
    int stgt=steam_target(), tline=(mode==1)||(mode==2&&m->sk!=SK_WAND);   /* wand/flush has no target: no target line */
    int floor1= mode==0? 400 : (mode==1? sv(S_GRINDW) : stgt-200);
    int full1=floor1; float top;
    if(raw||cd.scale==CS_MAX){ if(m1>full1) full1=m1; if(full1<1) full1=1; top=full1*1.08f; }   /* v6.8 */
    else {
        int lv=0;
        if(cd.scale==CS_PCT){ int na=0; for(int i=0;i<nv;i++) if(rs[i]>=(mode==0?40:1)) vs[na++]=rs[i];   /* p90 of the ACTIVE raw samples */
                              if(na>0){ qsort(vs,(size_t)na,sizeof vs[0],cmp_int); lv=vs[(na-1)*cd.pct/100]; } cst.na=na; }
        else { int k=cd.hold<1?1:cd.hold; for(int i=k-1;i<nv;i++){ int mn=rs[i]; for(int j=1;j<k;j++) if(rs[i-j]<mn) mn=rs[i-j]; if(mn>lv) lv=mn; } }   /* highest level HELD for k samples */
        if(full1<1) full1=1;
        top=fmaxf(full1*1.08f,lv*cd.head); if(lv>full1) full1=lv;
    }
    cst.full=full1; cst.top=(int)top;
    float s1=h/top, s2=h/((m2<100?100:m2)*1.08f);
    uint32_t pc=m->hi;
    if(tline){  /* target line (auto-stop level) */
        int ty=yb-(int)((mode==1?sv(S_GRINDW):stgt-200)*s1);
        for(int x=x0;x<x0+w;x+=7) hspan(ty,x,x+3,C_TXT2,200);
    }
    int last=-1; float ly=0, ytop=(float)(yb-h);
    for(int i=0;i<w;i++){
        if(c1[i]<0) continue;
        float y=yb-c1[i]*s1; if(y<ytop){ y=ytop; cst.clip++; }   /* v6.9: above the top -> clipped at the ceiling (the v6.8 scale never gets up here) */
        for(int yy=(int)y;yy<yb;yy++) pset(x0+i,yy,pc,22+(yy-(int)y<40?40-(yy-(int)y):0));
        if(last>=0&&i-last<=3) capsule(x0+last+0.5f,ly,x0+i+0.5f,y,0.9f,pc,255);
        last=i; ly=y;
    }
    if(!raw) for(int i=0;i<w;i++){
        if(cr[i]>top){ cst.ticks++; for(int yy=yb-h;yy<yb-h+3;yy++) pset(x0+i,yy,pc,170); }               /* raw sample above the top: small tick just below the ceiling (not in the headings) */
        if(ci[i]>=40){ cst.bticks++; for(int yy=yb-4;yy<yb;yy++) pset(x0+i,yy,C_DIS,200); } }           /* pulses outside the activity: the baseline */
    last=-1;
    if(mode==0) for(int i=0;i<w;i++){
        if(c2[i]<=-100000) continue; int v=c2[i]<0?0:c2[i]; float y=yb-v*s2;
        if(last>=0&&i-last<=3) capsule(x0+last+0.5f,ly,x0+i+0.5f,y,0.7f,C_TXT,170);
        last=i; ly=y;
    }
    { const char*cap=TR(mode==0?TX_C_PRESS:(mode==1?TX_C_WEIGHT:TX_C_MILK)); text_at(r.x0+4,r.y0+1,cap,F_XS,pc,235); }
    if(raw) text_r(r.x1-4,r.y0+1,TR(TX_C_RAW),F_XS,C_WARN,235);                  /* v6.9: the raw view is marked (instead of the right-hand heading) */
    else if(mode==0||tline) text_r(r.x1-4,r.y0+1,TR(mode==0?TX_C_WEIGHT:TX_C_TARGET),F_XS,C_TXT2,235);
}
/* v6.11: the sub-line at idle (TARGET 87.0°, NO CONNECTION …, errors in plain language) is the screen's most important message -> F_S when it
 * fits in the field; during activities (AUTO-STOP AT …) and for too-long text F_XS as before */
/* v6.17: the activity sub-lines are short now ("STOPS AT 36.0 G"), so they get F_S too — readable from where you stand while brewing */
static int sub_font(const HomeM*m){ return text_wf(m->sub,F_S)<=SUB_W-8?F_S:F_XS; }
/* v6.11: gear (settings) drawn with primitives — no new glyph in the font */
static void draw_gear(float cx,float cy,float r,uint32_t c,uint32_t hole){
    for(int k=0;k<8;k++){ float a=k*(float)M_PI/4.f, ca=cosf(a), sa=sinf(a); capsule(cx+ca*r*0.55f,cy+sa*r*0.55f,cx+ca*r*0.92f,cy+sa*r*0.92f,r*0.16f,c,255); }
    disc(cx,cy,r*0.70f,c,255); disc(cx,cy,r*0.30f,hole,255);
}
static void draw_home(ms_t now){
    const HomeM*m=&hm; uint32_t h; char b[64];
    { char c[24]; int ok=home_clock_text(c,sizeof c);                  /* v6.16: the clock (stock update_time format) instead of the wordmark */
      if(W_begin(W_CLOCK,R(CX-70,HOME_BRAND_Y-6,140,24),hs(hi(H0,ok),ok?c:""))){
          if(ok) text_c(CX,HOME_BRAND_Y-2,c,F_S,C_TXT2,255); else brand_c(CX,HOME_BRAND_Y,F_XS,C_TXT3,255);
          W_end(); } }
    /* phase chip (+ CRC warning) */
    int crc=now-last_bad_ms<10000&&rx_bad>0;
    h=hi(hs(H0,m->chip),crc?(int)rx_bad:0);
    if(W_begin(W_CHIP,R(110,74,260,32),h)){
        const char*nm=m->chip; int tw=text_w(nm,F_S); int w=tw+40; int x=CX-w/2; if(crc) x-=40;
        rrect(x,76,w,28,14,mix(C_SURF,m->hi,38),255);
        disc(x+15,90,4.2f,m->hi,255); text_at(x+26,83,nm,F_S,m->hi,255);
        if(crc){ snprintf(b,sizeof b,TR(TX_H_CRC),rx_bad); int cw2=text_w(b,F_XS)+20; rrect(x+w+8,78,cw2,24,12,mix(C_SURF,C_WARN,44),255); text_at(x+w+18,85,b,F_XS,C_WARN,255); }
        W_end();
    }
    /* v6.6: during concurrent steam the steam badge (STEAM·WAND / STEAM·AUTO) sits on the label line — the main activity's
     * target stays in the sub-line ("AUTO-STOP AT … G"), so both activities are named on screen */
    if(W_begin(W_LABEL,R(110,110,260,18),hi(hs(hs(H0,m->label),m->badge?m->badge:""),m->conc))){
        if(m->badge){
            uint32_t sc=ph_hi(PH_STEAM); int bw=text_w(m->badge,F_XS)+30, bx=CX-bw/2;
            rrect(bx,110,bw,18,9,mix(C_SURF,sc,44),255); disc(bx+11,119,3.2f,sc,255); text_at(bx+20,114,m->badge,F_XS,sc,255);
        } else text_c(CX,113,m->label,F_XS,C_TXT3,255);
        W_end();
    }
    h=hi(hs(hs(H0,m->hero),m->unit),m->ph==PH_OFFLINE||m->ph==PH_SLEEP);
    if(W_begin(W_HERO,R(92,130,296,80),h)){
        float nw=text_wf(m->hero,F_XXL), uw=text_wf(m->unit,F_M); float x=CX-(nw+10+uw)/2;
        uint32_t c=(m->ph==PH_OFFLINE||m->ph==PH_SLEEP)?C_DIS:C_TXT;
        text_at(x,136,m->hero,F_XXL,c,255); text_at(x+nw+10,136+66-18,m->unit,F_M,C_TXT2,255);
        W_end();
    }
    if(W_begin(W_SUB,R(SUB_X0,212,SUB_W,24),hi(hs(H0,m->sub),m->ph))){
        int sf=sub_font(m); text_c(CX,sf==F_S?215:217,m->sub,sf,(m->ph==PH_ERROR)?C_ERR:(phase_active(m->ph)||m->ph==PH_DONE?m->hi:C_TXT2),255); W_end();
    }
    if(W_begin(W_CHART,R(CX-HINT_W/2,240,HINT_W,54),hs(hs(hi(hi(hi(hi(hi(H0,g_head),g_n),(int)m->hi),m->sk*8+(m->eff==PH_STEAM)),cd.raw*16+cd.scale),m->hint1),m->hint2?m->hint2:""))){ draw_chart(R(118,240,244,54),now,m); W_end(); }
    h=H0; for(int i=0;i<3;i++){ h=hs(h,m->sl[i]); h=hs(h,m->svv[i]); }
    if(W_begin(W_STATS,R(96,298,288,40),h)){
        for(int i=0;i<3;i++){ int x=CX+(i-1)*STAT_PITCH; text_c(x,300,m->sl[i],F_XS,C_TXT3,255); text_c(x,317,m->svv[i],F_M,C_TXT,255); }
        W_end();
    }
    /* action row */
    int running=phase_active(m->ph); int akind=m->ph==PH_GRIND?0:(m->ph==PH_WATER?2:(m->ph==PH_STEAM?3:1));
    int st_on=stm.kind!=SK_NONE, sk[2], sn=stop_btns(now,sk);   /* v6.6: steam STOP only for latched steam; v6.13: + STOP for grind/brew/water */
    float hp=0; int hk=-1;
    if(tch.down&&!tch.moved&&tch.target>=T_ACT0&&tch.target<=T_ACT3){ hk=tch.target-T_ACT0; hp=clampf((now-tch.t0)/(float)HOLD_MS,0,1); }
    int sh=sn==1&&steam_hold_shown(now), she=sh&&!act_guard(ACT_STEAM,now), shp=pressed(T_STEAMH); float shh=(shp&&she)?clampf((now-tch.t0)/(float)HOLD_MS,0,1):0;   /* v6.20 */
    h=hi(hi(hi(hi(H0,m->ph),hk),(int)(hp*40)),act.pending); h=hi(h,pressed(T_STOP)); h=hi(h,pressed(T_STOPS)); h=hi(hi(hi(h,sh*4+she*2+shp),(int)(shh*48)),ui_lang); h=hi(h,sn*25+(sk[0]+1)*5+sk[1]+1); h=hi(h,stm.kind); if(running) h=hi(h,(int)((now/66)&0x7FFFFFFF)); h=hi(h,sv(S_GRINDMODE)*4+sv(S_HOTMODE)); for(int i=0;i<NSET;i++) h=hi(h,set_oor(&SET[i]));
    for(int i=0;i<4;i++) h=hs(h,m->alab[i]);
    if(W_begin(W_ACT,R(72,338,336,82),h)){                        /* v6.17: 336 x 82 (STOP 216 / 2 x 152, + the icons' targets); v6.13: 296 x 64 */
        if(sh){                                                       /* v6.20: STOP BREW (left) + HOLD · STEAM (right; the start button's look, in steam's colour) */
            draw_pill_btn(stop_pill(2,0),TR(STOP_BTN[sk[0]]),stop_font(2),C_STOP_T,C_STOP_B,C_STOP_FG,pressed(T_STOP));
            Rect r=stop_pill(2,1); int w=r.x1-r.x0, hh=r.y1-r.y0; float rr=hh/2.f; uint32_t ac=ph_hi(PH_STEAM);
            rrect(r.x0,r.y0+2,w,hh,rr,rgb(0,0,0),70);
            rrect(r.x0,r.y0,w,hh,rr,she?mix(C_SURF3,ac,150):C_SURF3,255);
            rrect_g(r.x0+2,r.y0+2,w-4,hh-4,rr-2,shp?C_SURF3:C_SURF2,shp?C_SURF2:C_SURF,255);
            if(shh>0){ set_clip(R(r.x0,r.y0,(int)(w*shh),hh)); rrect(r.x0+2,r.y0+2,w-4,hh-4,rr-2,mix(C_SURF2,ac,120),255); set_clip(R(72,338,336,82)); }   /* the hold fills the pill left -> right */
            text_c((r.x0+r.x1)/2,r.y0+(hh-(int)FS[F_S].cap)/2,TR(TX_B_HOLD_STEAM),F_S,she?C_TXT:C_DIS,255);
        } else if(sn){
            for(int i=0;i<sn;i++) draw_pill_btn(stop_pill(sn,i),TR(STOP_BTN[sk[i]]),stop_font(sn),C_STOP_T,C_STOP_B,C_STOP_FG,pressed(i?T_STOPS:T_STOP));
        } else {
            for(int i=0;i<4;i++){
                int en=!running&&!act_guard(i,now); uint32_t ac=ph_hi(ACT_PH[i]); int pr=(hk==i);
                if(running&&(i==akind||(i==ACT_STEAM&&st_on))){   /* active activity (v6.6: also steam on top of another): filled button + pulsing ring */
                    uint32_t on_c=(i==ACT_STEAM)?ph_hi(PH_STEAM):m->hi;
                    float pu=0.5f+0.5f*sinf((float)(now&0xFFFFFF)/260.f);   /* v6.6: the phase is taken from the lower 24 bits — a float can't hold a clock of several days in ms */
                    ring(ACT_X[i],ACT_Y,27.5f+pu*2,29.5f+pu*2,on_c,(int)(150-90*pu));
                    disc(ACT_X[i],ACT_Y,25,on_c,255);
                    float iw2=text_wf(ACT_ICON[i],F_IC); text_at(ACT_X[i]-iw2/2,ACT_Y-10,ACT_ICON[i],F_IC,rgb(16,18,22),255);
                    continue;
                }
                disc(ACT_X[i],ACT_Y+2,25,rgb(0,0,0),70);
                disc(ACT_X[i],ACT_Y,25,pr?C_SURF3:C_SURF2,255);
                ring(ACT_X[i],ACT_Y,24,25.2f,en?mix(C_SURF3,ac,90):C_SURF3,255);
                float iw=text_wf(ACT_ICON[i],F_IC);
                text_at(ACT_X[i]-iw/2,ACT_Y-10,ACT_ICON[i],F_IC,en?ac:C_DIS,255);
                if(pr&&en&&hp>0) arc_aa(ACT_X[i],ACT_Y,28.5f,1.8f,-90,-90+360*hp,ac,255);
                if(!running) text_c(ACT_X[i],ACT_LAB_Y,m->alab[i],F_XS,en?C_TXT2:C_DIS,255);   /* v6.17: the icon's target */
            }
        }
        W_end();
    }
    /* v6.11: visible path to the settings; v6.17: the gear opens SYSTEM (each feature's settings are behind its own icon), no page dots */
    if(W_begin(W_GEAR,R(CX-22,GEAR_Y-20,44,40),hi(H0,pressed(T_GEAR)))){
        int pr=pressed(T_GEAR); uint32_t bg=pr?C_SURF3:C_SURF2;
        disc(CX,GEAR_Y+1,15,rgb(0,0,0),60); disc(CX,GEAR_Y,15,bg,255); ring(CX,GEAR_Y,14,15.2f,C_LINE,255);
        draw_gear(CX,GEAR_Y,9,pr?C_TXT:C_TXT2,bg); W_end();
    }
}

/* ================= page: SETTINGS LIST ================= */
/* HOME: discreet title line at the top inside the gauge ring — the wordmark w/ "+" badge */
static void static_home(void){ }   /* v6.16: the wordmark is drawn by W_CLOCK (draw_home) — it gives way to the clock */
/* v6.17: a page's title. Feature pages: the feature's icon in its colour + its name (the same icon as on HOME, so the page is
 * recognisably "the thing behind that button"); SYSTEM / DIAGNOSTICS: the name. No descriptive sub-line any more. */
static const char* page_title(int pg){ return pg==PG_DIAG?TR(TX_PG_DIAG):TR(TIER_NAME[clampi(tier_of_page(pg),0,4)]); }
static float page_title_w(int pg){ return text_wf(page_title(pg),F_M)+(pg_feat(pg)?text_wf(ACT_ICON[pg-PG_GRIND],F_IC)+8:0); }
static void static_page_title(void){
    const char*t=page_title(page); float x=CX-page_title_w(page)/2;
    if(pg_feat(page)){ int k=page-PG_GRIND; float iw=text_wf(ACT_ICON[k],F_IC); text_at(x,PG_TITLE_Y-1,ACT_ICON[k],F_IC,ph_hi(ACT_PH[k]),255); x+=iw+8; }
    text_at(x,PG_TITLE_Y,t,F_M,C_TXT,255);
}
static void static_list(void){ static_page_title(); }
/* soft edges: fade to background at top/bottom of a scroll field */
static void edge_fade(Rect vr,int top,int bottom){
    for(int j=0;j<14;j++){
        int a=255-j*255/14;
        for(int e=0;e<2;e++){
            if(e?!bottom:!top) continue;
            int y=e?vr.y1-1-j:vr.y0+j;
            int xa=vr.x0<cx0[y]?cx0[y]:vr.x0, xb=vr.x1>cx1[y]+1?cx1[y]+1:vr.x1;
            for(int x=xa;x<xb;x++) canvas[y*W+x]=mix(canvas[y*W+x],slay[y*W+x],a);
        }
    }
}
/* scroll indicator: short arc at the right edge */
static void draw_scroll_ind(float content_h){
    float mx=scroll_max(page); int sc=(int)lroundf(scroll[page]);
    if(W_begin(W_SCROLL,R(444,140,30,200),hi(hi(hi(H0,sc),(int)mx),page))){
        if(mx>0){
            arc_aa(CX,CY,228,1.4f,-22,22,C_SURF3,255);
            float vis=(list_vy1(page)-LIST_VY0)/content_h, span=44*vis, a0=-22+(44-span)*(scroll[page]/mx);
            arc_aa(CX,CY,228,1.6f,a0,a0+span,C_TXT2,255);
        }
        W_end();
    }
}
/* v6.2: texts for the system-action rows at the bottom of SYSTEM (shared w/ the selftest's layout check) */
/* short = the list row's sub-line (<= 300 px); long = the detail view's note w/ the actual path */
static const char* boot_lock_text(int l,int lng){
    if(l==BL_NOMOUNT) return TR(lng?TX_BL_NOMOUNT_L:TX_BL_NOMOUNT);
    if(l==BL_NOBIN)   return TR(lng?TX_BL_NOBIN_L:TX_BL_NOBIN);
    return TR(lng?TX_BL_VENDOR_L:TX_BL_VENDOR);
}
/* BOOT.md §9.1: "exists -> our UI (B) at boot; does not exist -> original (A) at boot" — but never
 * "our UI" if slot B is not mounted (then A starts) or does not contain merakiplus. From v6.3
 * the product name is shown instead of "our UI". */
static const char* boot_state_text(void){
    int k=boot_next_kind();
    return TR(k==BN_OURS?TX_BS_OURS:(k==BN_ORIGINAL?TX_BS_ORIG:TX_BS_SLOTB));
}
static void xrow_text(int x,const char**label,const char**sub,const char**val,int*lock){
    if(x==X_BOOTUI){
        *label=TR(TX_X_BOOTUI); *lock=bs.lock!=BL_OK; *val=boot_next_name();
        *sub=*lock?boot_lock_text(bs.lock,0):TR(TX_BH1);
    } else {
        *label=TR(TX_X_SWITCH); *lock=switch_locked(); *val=NULL;
        *sub=TR(*lock?TX_SW_LOCK:TX_X_SWITCH_SUB);
    }
}
/* list row: font for an enum value — the largest that fits to the right of the label */
static int row_val_font(const char*label,const char*val,int xr,int*room){
    *room=xr-(94+text_w(label,F_S)+12);
    if(text_w(val,F_M)<=*room) return F_M;
    return text_w(val,F_S)<=*room?F_S:F_XS;
}
/* v6.17: the close chevron of a PAGE (feature / SYSTEM / DIAGNOSTICS): the same glyph as the editors' (v6.11), at the very top;
 * the tap area is the whole header (PBACK_*). */
static void draw_pback(void){
    int pr=pressed(T_BACK);
    if(W_begin(W_BACKCH,R(CX-26,PG_CHEV_Y-8,52,24),hi(hi(H0,pr),page))){
        if(pr) rrect(CX-24,PG_CHEV_Y-7,48,22,11,C_SURF3,255);
        uint32_t c=pr?C_TXT:C_TXT2;
        capsule(CX-9,PG_CHEV_Y,CX,PG_CHEV_Y+8,1.5f,c,255); capsule(CX,PG_CHEV_Y+8,CX+9,PG_CHEV_Y,1.5f,c,255);
        W_end();
    }
}
/* v6.17: the line under a page's title (shared w/ the selftest). Feature pages: WHY start is not possible (the guard's text, amber) —
 * otherwise the live values that matter before starting: what is on the scale, how hot the boiler / group / milk is.
 * (TX_G_WAIT is transient — a write is on its way — and is not shown: the line would flicker after every SAVE.) SYSTEM: product + version. */
/* SYSTEM's line under the title: the version + the OTA state that matters (vendor update > fallback > failed trial > trial) */
static uint32_t ota_line(char*o,size_t n,int ro){
    const char*base=TR(ro?TX_DV_VERSION_RO:TX_DV_VERSION); char v[24],x[64];
    if(ota.vendor){ snprintf(o,n,"%s",TR(TX_OTA_VENDOR)); return C_WARN; }
    if(ota.fallback){ snprintf(o,n,"%s · %s",base,TR(TX_OTA_FALLBACK)); return C_WARN; }
    if(ota.st_ok&&!strncmp(ota.last,"rolled_back:",12)&&(!ota.name[0]||!strcmp(ota.cur,ota.name))){
        char r[64]; snprintf(r,sizeof r,"%s",ota.last+12); r[strcspn(r,":")]=0; ota_ver(r,v,sizeof v);
        snprintf(x,sizeof x,TR(TX_OTA_FAILED),v); snprintf(o,n,"%s · %s",base,x); return C_WARN; }
    if(ota.trial&&!ota.marked){ snprintf(o,n,"%s · %s",base,TR(TX_OTA_TRIAL)); return C_TXT3; }
    snprintf(o,n,"%s",base); return C_TXT3;
}
#define LIVE_W 296
static uint32_t page_live_text(int pg,char*o,size_t n,ms_t now){
    char a[16],b[16]; o[0]=0;
    if(pg==PG_SYS) return ota_line(o,n,ro_mode);                                   /* v6.21: + TRIAL / FALLBACK / FAILED / vendor update */
    if(!pg_feat(pg)) return C_TXT3;
    int k=pg-PG_GRIND, up=link_up(now); const char*g=act_guard(k,now);
    if(g&&g!=TR(TX_G_WAIT)){ snprintf(o,n,"%s",g); return C_WARN; }
#define LV_T(dst,r_,dec_) do{ if(up&&reg_seen[r_]) fmt_t10(dst,sizeof dst,reg[r_],dec_); else snprintf(dst,sizeof dst,"--"); }while(0)
    if(k==ACT_GRIND){ if(up&&reg_seen[38]){ int w=wdbi(s16(38)); if(w<-999)w=-999; if(w>9999)w=9999; fmt_fix(a,sizeof a,w,1); } else snprintf(a,sizeof a,"--"); snprintf(o,n,TR(TX_LV_GRIND),a); }
    else if(k==ACT_BREW){ LV_T(a,40,1); LV_T(b,42,0); snprintf(o,n,TR(TX_LV_BREW),a,b); }
    else if(k==ACT_WATER){ LV_T(a,40,1); snprintf(o,n,TR(TX_LV_WATER),a); }       /* word 40 = the stock UI's "water sensor" (SET[S_HOTT]) */
    else { LV_T(a,47,0); LV_T(b,48,1); snprintf(o,n,TR(TX_LV_STEAM),a,b); }
#undef LV_T
    return C_TXT2;
}
/* v6.17: a section header's summary on SYSTEM (shared w/ the selftest): what you would otherwise have to open the section to see */
static void sec_summary(int sc,char*o,size_t n){
    o[0]=0;
    if(sc==SEC_DISPLAY) snprintf(o,n,"%s",TR(TX_UL_EN+ui_lang));
    else if(sc==SEC_CLOCK){ if(!home_clock_text(o,n)) snprintf(o,n,"%s",TR(TX_DV_T_NOTSET)); }
    else if(sc==SEC_SCHED){ if(sch.on) fmt_hm(o,n,sch.hm/60,sch.hm%60,1); else snprintf(o,n,"%s",TR(TX_E_OFF)); }
}
/* the DIAGNOSTICS entry's status: OK / the error code / DOWN */
static uint32_t diag_row_text(char*o,size_t n,ms_t now){
    if(!link_up(now)){ snprintf(o,n,"%s",TR(TX_DV_DOWN)); return C_ERR; }
    if(reg[34]|reg[35]|reg[36]){ snprintf(o,n,"%s",err_code()); return C_ERR; }
    snprintf(o,n,"%s",TR(TX_DV_OK)); return C_OK;
}
static void draw_start(ms_t now){
    int k=page-PG_GRIND, en=!act_guard(k,now), pr=pressed(T_START); uint32_t ac=ph_hi(ACT_PH[k]);
    float hp=(pr&&en)?clampf((now-tch.t0)/(float)HOLD_MS,0,1):0;
    Rect wr=R(CX-START_HIT_W/2,START_HIT_Y,START_HIT_W,START_HIT_H);
    if(W_begin(W_START,wr,hi(hi(hi(hi(hi(H0,k),en),pr),(int)(hp*48)),ui_lang))){
        int x=CX-START_W/2, y=START_Y; float rr=START_H/2.f; const char*t=TR(TX_B_START);
        rrect(x,y+2,START_W,START_H,rr,rgb(0,0,0),70);
        rrect(x,y,START_W,START_H,rr,en?mix(C_SURF3,ac,150):C_SURF3,255);                 /* outline in the feature's colour */
        rrect_g(x+2,y+2,START_W-4,START_H-4,rr-2,pr?C_SURF3:C_SURF2,pr?C_SURF2:C_SURF,255);
        if(hp>0){ set_clip(R(x,y,(int)(START_W*hp),START_H)); rrect(x+2,y+2,START_W-4,START_H-4,rr-2,mix(C_SURF2,ac,120),255); set_clip(wr); }   /* the hold fills the pill left -> right */
        float iw=text_wf(ACT_ICON[k],F_IC), tw=text_wf(t,F_S), tx=CX-(iw+10+tw)/2;
        text_at(tx,y+START_H/2-10,ACT_ICON[k],F_IC,en?ac:C_DIS,255);
        text_at(tx+iw+10,y+START_H/2-7,t,F_S,en?C_TXT:C_DIS,255);
        W_end();
    }
}
#define WMAN_Y0 170          /* v6.22: the manual-only WATER page's three lines (title F_S, two F_XS lines) */
static void draw_list(ms_t now){
    Row rw[ROW_MAX]; int n=rows_get(page,rw), vy1=list_vy1(page); int sc=(int)lroundf(scroll[page]); int prow=-1;
    draw_pback();
    { char lv[96]; uint32_t lc=page_live_text(page,lv,sizeof lv-4,now); fit_text(lv,F_XS,LIVE_W);
      if(W_begin(W_LIVE,R(CX-LIVE_W/2-2,PG_LIVE_Y-3,LIVE_W+4,17),hi(hi(hs(H0,lv),(int)lc),page))){ text_c(CX,PG_LIVE_Y,lv,F_XS,lc,255); W_end(); } }
    if(tch.down&&!tch.moved&&tch.target>=T_ROW0) prow=tch.target-T_ROW0;
    char dgs[16]; uint32_t dgc=diag_row_text(dgs,sizeof dgs,now);
    uint32_t h=hi(hi(hi(hi(H0,sc),prow),page),sec_open*8+ui_lang);
    for(int k=0;k<n;k++){ h=hi(h,rw[k].kind*64+rw[k].a); if(rw[k].kind==RK_SET){ const Setting*q=&SET[rw[k].a]; h=hi(hi(h,q->val),q->loaded); } }
    h=hi(h,sv(S_TUNIT));
    int wman=page==PG_WATER&&n==0; if(wman) h=hi(h,100+wd_why());      /* v6.22: the manual-only WATER page */
    if(page==PG_SYS){ char sm[40]; h=hi(hi(hi(h,bs.flag*8+bs.lock),switch_locked()),switch_mode()); for(int q=0;q<NSEC;q++){ sec_summary(q,sm,sizeof sm); h=hs(h,sm); } h=hi(hs(h,dgs),(int)dgc); }
    Rect vr=R(60,LIST_VY0,360,vy1-LIST_VY0);
    if(W_begin(W_LIST,vr,h)){
        char b[40]; int c0=-1,c1=-1;
        for(int k=0;k<n;k++){
            int y=row_y0(page,k)-sc;
            if(page==PG_SYS&&rw[k].kind!=RK_SEC&&rw[k].kind!=RK_DIAG){ if(c0<0) c0=y; c1=y+ROW_H; }   /* the open section's rows */
            if(y+ROW_H<LIST_VY0||y>vy1) continue;
            int pr=(k==prow);
            if(rw[k].kind==RK_SET){
                int i=rw[k].a; const Setting*s=&SET[i]; int lock=(s->flags&SF_LOCK)!=0;
                int oor=set_oor(s); const char*lab=TR(s->label);
                rrect_g(72,y,336,ROW_H,16,pr?C_SURF3:C_SURF2,pr?C_SURF2:C_SURF,255);
                if(oor) rrect(72,y,336,ROW_H,16,C_ERR,34);                                        /* out of spec: red tint */
                text_at(94,y+18,lab,F_S,lock?C_TXT2:C_TXT,255);
                set_fmt(i,s->val,b,sizeof b-4); const char*u=set_unit(i);
                int xr=lock?388:372;
                if(*u&&s->kind==K_NUM){ int uw=text_w(u,F_XS); text_r(xr,y+21,u,F_XS,C_TXT3,255); xr-=uw+6; }
                int vf=F_M, room=0;
                if(s->kind==K_ENUM||(s->flags&SF_HHMM)||i==S_TZ){ vf=row_val_font(lab,b,xr,&room); if(vf==F_XS) fit_text(b,F_XS,room); }   /* v6.16: + clock times, zone label */   /* long enum names: M -> S -> XS -> … */
                text_r(xr,y+(vf==F_M?16:(vf==F_S?18:20)),b,vf,oor?C_ERR:(lock?C_TXT3:C_ACC),255); xr-=text_w(b,vf);
                if(s->hint&&!lock&&!oor) text_r(xr-7,y+21,"≈",F_XS,C_WARN,255);                   /* estimate marker */
                if(!lock) text_at(380,y+19,">",F_XS,C_TXT3,255);
            } else if(rw[k].kind==RK_SEC){   /* v6.17: section header — name, summary, chevron (right = closed, down = open) */
                int open=(rw[k].a==sec_open); char sm[40]; sec_summary(rw[k].a,sm,sizeof sm-4);
                rrect_g(72,y,336,ROW_H,16,pr?C_SURF3:(open?C_SURF3:C_SURF2),pr?C_SURF2:(open?C_SURF2:C_SURF),255);
                text_at(94,y+18,TR(SEC_NAME[rw[k].a]),F_S,open?C_ACC:C_TXT,255);
                if(sm[0]&&!open){ fit_text(sm,F_XS,372-(94+text_w(TR(SEC_NAME[rw[k].a]),F_S)+14)); text_r(366,y+20,sm,F_XS,C_TXT3,255); }
                if(open){ capsule(378,y+22,384,y+28,1.3f,C_ACC,255); capsule(384,y+28,390,y+22,1.3f,C_ACC,255); }
                else text_at(380,y+19,">",F_XS,C_TXT3,255);
            } else if(rw[k].kind==RK_X){     /* v6.2: system actions — two-line rows (label + explanation) */
                const char*label,*sub,*val; int lock; xrow_text(rw[k].a,&label,&sub,&val,&lock);
                rrect_g(72,y,336,ROW_H,16,pr?C_SURF3:C_SURF2,pr?C_SURF2:C_SURF,255);
                text_at(94,y+10,label,F_S,lock?C_TXT2:C_TXT,255);
                text_at(94,y+31,sub,F_XS,lock?C_WARN:C_TXT3,255);
                if(val) text_r(lock?388:372,y+8,val,F_M,lock?C_TXT3:C_ACC,255);
                if(!lock) text_at(380,y+(val?12:19),">",F_XS,C_TXT3,255);
            } else {                          /* v6.17: the DIAGNOSTICS entry (advanced) w. the machine's status */
                rrect_g(72,y,336,ROW_H,16,pr?C_SURF3:C_SURF2,pr?C_SURF2:C_SURF,255);
                text_at(94,y+18,TR(TX_PG_DIAG),F_S,C_TXT,255);
                text_r(366,y+18,dgs,F_S,dgc,255);
                text_at(380,y+19,">",F_XS,C_TXT3,255);
            }
        }
        if(c0>=0){ int ya=c0<LIST_VY0?LIST_VY0:c0, yb=c1>vy1?vy1:c1; if(yb>ya) fill_rect(64,ya,2,yb-ya,C_ACC,150); }   /* the open section's rows are tied together by a thin accent rail */
        if(wman){   /* v6.22: no dosing — what HOLD TO START does here, and why there is no weight target (docs/water-dosing-compat.md §5.2) */
            static const short WHY[3]={TX_W_WHY_OLD,TX_W_WHY_UNK,TX_W_WHY_FAULT}; int why=wd_why();
            text_c(CX,WMAN_Y0,TR(TX_W_MAN_T),F_S,C_TXT,255);
            text_c(CX,WMAN_Y0+34,TR(TX_W_MAN_RUN),F_XS,C_TXT2,255);
            text_c(CX,WMAN_Y0+60,TR(WHY[why]),F_XS,why==WDW_FAULT?C_WARN:C_TXT3,255);
        }
        edge_fade(vr,1,1);
        W_end();
    }
    draw_scroll_ind(n*ROW_P);
    if(pg_feat(page)) draw_start(now);
}

/* ================= page: STEPPER DETAIL ================= */
/* texts for the detail view (shared w/ the selftest's layout check) */
/* v6.17: the two footer lines ("CONTROLLER NOW + CONFIG" / "STOCK UI: AFTER RESTART", file paths, "SHOWN FROM CONFIG.JSON" …) are gone:
 * they described the implementation on every single editor. What matters is said once — by the SAVED toast, by the row's note, and by
 * the STOCK UI section the stock-only rows now live in. */
static void detail_text(int i,char*l,size_t n){
    const Setting*s=&SET[i]; char a[24],b[24]; int lock=(s->flags&SF_LOCK)!=0, sens=(s->flags&SF_SENSOR)!=0;
    if(sens) snprintf(l,n,"%s",TR(TX_D_MEASURED));
    else if(lock) snprintf(l,n,"%s",TR(TX_D_NOEDIT));
    else if(s->kind==K_ENUM){ if(s->max-s->min==1) snprintf(l,n,"%s / %s",TR(s->opts[0]),TR(s->opts[1])); else snprintf(l,n,TR(TX_D_NOPTS),s->max-s->min+1); }
    else { const char*u=set_unit(i); set_fmt(i,s->lo,a,sizeof a); set_fmt(i,s->hi,b,sizeof b); snprintf(l,n,*u?"%s - %s %s":"%s - %s",a,b,u); }
}
/* v6.17: is the stepper worth holding? (hold = repeat w. acceleration; shown as a micro-hint while the value is unchanged) */
static int detail_holdable(const Setting*s){ return s->kind==K_NUM&&!(s->flags&SF_LOCK)&&s->step>0&&(s->hi-s->lo)/s->step>=20; }
static void static_detail(void){
    const Setting*s=&SET[sel]; char l[72]; int lock=(s->flags&SF_LOCK)!=0, sens=(s->flags&SF_SENSOR)!=0;
    detail_text(sel,l,sizeof l);
    text_c(CX,82,TR(s->label),F_S,C_TXT2,255);
    /* range line (+ visible hint when max/mapping is an estimate, PARITY §6.6) */
    if(s->hint&&!lock){
        const char*hint=TR(s->hint);
        float w1=text_wf(l,F_XS), w2=text_wf(" · ",F_XS), w3=text_wf(hint,F_XS); float x=CX-(w1+w2+w3)/2;
        x=text_at(x,106,l,F_XS,C_TXT3,255); x=text_at(x,106," · ",F_XS,C_TXT3,255); text_at(x,106,hint,F_XS,C_WARN,255);
    } else text_c(CX,106,l,F_XS,C_TXT3,255);
    text_c(CX,254,TR(s->note),F_XS,(lock&&!sens&&s->mcu!=M_NONE)?C_WARN:C_TXT3,255);
}
/* v6.11: close chevron at the top of the detail/system view (tap = CANCEL/BACK; swiping down does the same). The down arrow is
 * the "pull down to close" handle; the tap field is 88 x 46 px (BACK_*). */
/* v6.17: the editor's status line (shared w/ the selftest): SAVING… / out of spec / CHANGED · WAS … / the zone's place names / — when the
 * value is untouched — the one thing about −/+ you cannot see: holding repeats and accelerates. Otherwise empty ("SAVED VALUE" is gone). */
static uint32_t detail_status(char*l,size_t n){
    const Setting*s=&SET[sel]; char o[40]; int lock=(s->flags&SF_LOCK)!=0; l[0]=0;
    if(sav.active){ snprintf(l,n,"%s",TR(TX_D_SAVING)); return C_ACC; }
    if(!lock&&!set_valid(s,edit_val)){ snprintf(l,n,"%s",TR(TX_D_OOR)); return C_ERR; }
    if(edit_val!=edit_orig){ set_fmt(sel,edit_orig,o,sizeof o); snprintf(l,n,TR(TX_D_CHANGED),o,s->kind==K_NUM?set_unit(sel):""); return C_ACC; }
    if(sel==S_TZ&&edit_val>=0&&edit_val<TZ_N){ snprintf(l,n-4,"%s",TZT[edit_val].name); fit_text(l,F_XS,276); return C_TXT2; }   /* v6.16: the zone's place names */
    if(detail_holdable(s)) snprintf(l,n,"%s",TR(TX_D_HOLDFAST));
    return C_TXT3;
}
static void draw_backch(void){
    int pr=pressed(T_BACK);
    if(W_begin(W_BACKCH,R(CX-26,42,52,30),hi(H0,pr))){
        if(pr) rrect(CX-24,44,48,26,13,C_SURF3,255);
        uint32_t c=pr?C_TXT:C_TXT2;
        capsule(CX-9,53,CX,61,1.5f,c,255); capsule(CX,61,CX+9,53,1.5f,c,255);
        W_end();
    }
}
static void draw_detail(ms_t now){
    (void)now; draw_backch(); const Setting*s=&SET[sel]; char b[40],l[112];
    int lock=(s->flags&SF_LOCK)!=0; set_fmt(sel,edit_val,b,sizeof b); const char*u=set_unit(sel);
    int eoor=!lock&&!set_valid(s,edit_val);
    if(W_begin(W_DHERO,R(122,128,236,90),hi(hs(hs(H0,b),u),eoor))){
        float uw=(*u&&s->kind==K_NUM)?text_wf(u,F_M)+10:0;
        int f=(s->kind==K_NUM)?F_XXL:F_XL;                       /* largest font that fits in the field */
        while(f>F_M&&text_wf(b,f)+uw>228) f=(f==F_XXL)?F_XL:(f==F_XL?F_L:F_M);
        float nw=text_wf(b,f); float x=CX-(nw+uw)/2; int y=173-(int)FS[f].cap/2;
        text_at(x,y,b,f,eoor?C_ERR:(lock?C_TXT2:C_TXT),255);
        if(uw>0) text_at(x+nw+10,y+(int)FS[f].cap-18,u,F_M,C_TXT2,255);
        W_end();
    }
    int chg=edit_val!=edit_orig;
    { uint32_t stc=detail_status(l,sizeof l);
      if(W_begin(W_DCHG,R(100,224,280,22),hi(hs(H0,l),(int)stc))){ if(l[0]) text_c(CX,228,l,F_XS,stc,255); W_end(); } }
    for(int k=0;k<2;k++){
        int cx=k?396:84, t=k?T_PLUS:T_MINUS; int en=!lock&&!sav.active&&(eoor||(k?edit_val<s->hi:edit_val>s->lo)); int pr=pressed(t);
        if(W_begin(k?W_PLUS:W_MINUS,R(cx-38,146,76,78),hi(hi(hi(H0,en),pr),lock))){
            if(!lock){
                disc(cx,186,30,rgb(0,0,0),70);
                disc(cx,184,30,pr&&en?C_SURF3:C_SURF2,255);
                ring(cx,184,29,30.2f,en?C_LINE:C_SURF2,255);
                const char*g=k?"+":"-"; float gw=text_wf(g,F_XL);
                text_at(cx-gw/2,184-24,g,F_XL,en?C_TXT:C_DIS,255);
            }
            W_end();
        }
    }
    if(W_begin(W_CANCEL,lock?R(170,286,140,60):R(100,286,136,60),hi(hi(H0,pressed(T_CANCEL)),sav.active))){
        draw_pill_btn(lock?R(178,292,124,48):R(108,292,124,48),TR(lock?TX_B_BACK:TX_B_CANCEL),F_XS,C_SURF2,C_SURF,sav.active?C_DIS:C_TXT,pressed(T_CANCEL)); W_end();
    }
    if(!lock&&W_begin(W_SAVE,R(244,286,136,60),hi(hi(hi(hi(H0,pressed(T_SAVE)),chg),sav.active),lock))){
        {
            if(chg&&!sav.active) draw_pill_btn(R(248,292,124,48),TR(TX_B_SAVE),F_S,rgb(250,204,140),rgb(232,174,100),C_ACCD,pressed(T_SAVE));
            else draw_pill_btn(R(248,292,124,48),TR(sav.active?TX_B_SAVING:TX_B_SAVE),F_S,C_SURF2,C_SURF,C_DIS,0);
        }
        W_end();
    }
}

/* ================= v6.2: SYSTEM ACTION (BOOT UI / SWITCH / EXIT REMINDER) =================
 * Same geometry as the stepper detail: title 82, sub-line 106, hero 128..218, status 228,
 * note 254, buttons 286..346, two footer lines 398/416. All text comes from sys_text(), which
 * the selftest also measures against the circle's chord. */
typedef struct { const char*title,*sub,*hero,*status,*note,*l1,*l2,*ok,*cancel; uint32_t c_status,c_note,c_l1; } SysT;
static void sys_text(SysT*t){
    static char st[96],nt[96],sb[96]; int lock=sys_locked();
    memset(t,0,sizeof *t); t->c_status=C_TXT3; t->c_note=C_TXT2; t->c_l1=C_TXT3; t->cancel=TR(lock?TX_B_BACK:TX_B_CANCEL);
    if(xsel==X_BOOTUI){
        int chg=x_edit!=x_orig;
        t->title=TR(TX_X_BOOTUI); t->sub=boot_state_text(); t->hero=lock?boot_next_name():TR(x_edit?TX_BN_OURS:TX_BN_ORIGINAL); t->ok=TR(TX_B_SAVE);
        if(lock) t->status=TR(TX_D_LOCKED);
        else if(chg){ snprintf(st,sizeof st,TR(TX_Y_CHANGED),TR(x_orig?TX_BN_OURS:TX_BN_ORIGINAL)); t->status=st; t->c_status=C_ACC; }
        /* v6.17: unchanged = no status line ("SAVED CHOICE" was noise) */
        t->note=lock?boot_lock_text(bs.lock,1):TR(TX_BH1); if(lock) t->c_note=C_WARN;
        if(lock){ t->l1=TR(bs.flag?TX_Y_FLAG_YES:TX_Y_FLAG_NO); t->l2=TR(TX_Y_FLAG_HANDSOFF); }
        else { t->l1=TR(TX_BH2); t->l2=TR(TX_BH3); }               /* the §9.1 help text */
    } else if(xsel==X_SWITCH){
        int cont=switch_mode()==SWM_CONT;
        snprintf(sb,sizeof sb,TR(TX_Y_NEXT),boot_next_name());      /* the runtime switch does not touch the boot choice */
        t->title=TR(TX_X_SWITCH); t->sub=sb; t->hero=TR(TX_BN_ORIGINAL); t->ok=TR(TX_B_SWITCH);
        if(lock){ t->status=TR(TX_D_LOCKED); t->note=TR(TX_SW_LOCK_L); t->c_note=C_WARN; t->l1=TR(TX_Y_NOSTOCK); t->l2=TR(TX_Y_STAYS); return; }
        t->status=TR(cont?TX_Y_CLOSE_CONT:TX_Y_CLOSE_START); t->c_status=C_TXT2;
        if(cont){
            if(x_pending){ t->note=TR(TX_Y_REMINDER_FOLLOWS); t->c_note=C_WARN; }
            else { snprintf(nt,sizeof nt,TR(TX_Y_PAUSED_PID),bs.stock[0]); t->note=nt; }
            t->l1=TR(TX_Y_RUNSH); t->l2=TR(TX_Y_BACK_RUNSH);
        } else {
            t->note=TR(TX_Y_FRESH);
            if(bs.sw_pid){ t->l1=TR(TX_Y_BACK_GESTURE1); t->l2=TR(TX_Y_BACK_GESTURE2); }
            else { t->l1=TR(TX_Y_NOSWITCHER); t->c_l1=C_WARN; t->l2=TR(bs.flag?TX_Y_BACK_RESTART:TX_Y_BACK_TELNET); }
        }
    } else {   /* X_EXIT */
        t->title=TR(TX_Y_EXIT_TITLE); t->sub=TR(TX_Y_EXIT_SUB); t->ok=TR(TX_B_EXIT); t->cancel=TR(TX_B_BACK);
        t->note=TR(TX_Y_EXIT_NOTE); t->c_note=C_TXT3;
        t->l1=TR(TX_Y_EXIT_F1); t->l2=TR(TX_Y_EXIT_F2); t->c_l1=C_TXT3;
    }
}
/* the exit reminder's body text (the v6.1 integration note): TX_Y_EXIT_L1 / _L2 + the command TX_Y_EXIT_CMD */
static int sys_hero_font(const char*hero){ if(xsel==X_BOOTUI) return F_H; int f=F_XL; while(f>F_M&&text_wf(hero,f)>296) f=(f==F_XL)?F_L:F_M; return f; }   /* BOOT UI: both choices in the SAME font */
static void static_sys(void){
    SysT t; sys_text(&t);
    if(xsel==X_EXIT){
        float iw=text_wf(IC_WARN,F_IC), tw=text_wf(t.title,F_S); float x=CX-(iw+9+tw)/2;
        text_at(x,78,IC_WARN,F_IC,C_WARN,255); text_at(x+iw+9,82,t.title,F_S,C_WARN,255);
    } else text_c(CX,82,t.title,F_S,C_TXT2,255);
    text_c(CX,106,t.sub,F_XS,C_TXT3,255);
}
static void draw_sys(ms_t now){
    (void)now; SysT t; sys_text(&t); int lock=sys_locked(); draw_backch();
    if(xsel==X_EXIT){
        if(W_begin(W_DHERO,R(60,130,360,102),hi(H0,xsel))){
            text_c(CX,138,TR(TX_Y_EXIT_L1),F_S,C_TXT,255);
            text_c(CX,162,TR(TX_Y_EXIT_L2),F_S,C_TXT,255);
            int w=text_w(TR(TX_Y_EXIT_CMD),F_M)+40;
            rrect_g(CX-w/2,188,w,38,12,C_SURF3,C_SURF2,255);
            text_c(CX,188+9,TR(TX_Y_EXIT_CMD),F_M,C_ACC,255);
            W_end();
        }
    } else {
        int f=sys_hero_font(t.hero);
        /* BOOT UI: the hero field must not overlap −/+ (x 46..122 / 358..434) — otherwise a hero redraw erases the button edges */
        if(W_begin(W_DHERO,xsel==X_BOOTUI?R(122,128,236,90):R(92,128,296,90),hi(hi(hs(H0,t.hero),lock),xsel))){
            text_c(CX,173-(int)FS[f].cap/2,t.hero,f,lock?C_TXT2:C_TXT,255);
            W_end();
        }
        for(int k=0;k<2&&xsel==X_BOOTUI;k++){
            int cx=k?396:84, tt=k?T_PLUS:T_MINUS; int en=!lock&&(k?!x_edit:x_edit); int pr=pressed(tt);
            if(W_begin(k?W_PLUS:W_MINUS,R(cx-38,146,76,78),hi(hi(hi(H0,en),pr),lock))){
                if(!lock){
                    disc(cx,186,30,rgb(0,0,0),70);
                    disc(cx,184,30,pr&&en?C_SURF3:C_SURF2,255);
                    ring(cx,184,29,30.2f,en?C_LINE:C_SURF2,255);
                    const char*g=k?"+":"-"; float gw=text_wf(g,F_XL);
                    text_at(cx-gw/2,184-24,g,F_XL,en?C_TXT:C_DIS,255);
                }
                W_end();
            }
        }
    }
    if(W_begin(W_DCHG,R(60,224,360,22),hi(hs(H0,t.status?t.status:""),(int)t.c_status))){ if(t.status) text_c(CX,228,t.status,F_XS,t.c_status,255); W_end(); }
    if(W_begin(W_XNOTE,R(40,xsel==X_EXIT?238:250,400,22),hi(hs(H0,t.note),(int)t.c_note))){ text_c(CX,xsel==X_EXIT?242:254,t.note,F_XS,t.c_note,255); W_end(); }
    if(W_begin(W_CANCEL,lock?R(170,286,140,60):R(100,286,136,60),hi(hi(H0,pressed(T_CANCEL)),lock))){
        draw_pill_btn(lock?R(178,292,124,48):R(108,292,124,48),t.cancel,F_XS,C_SURF2,C_SURF,C_TXT,pressed(T_CANCEL)); W_end();
    }
    int act_ok=(xsel!=X_BOOTUI)||(x_edit!=x_orig);
    if(!lock&&W_begin(W_SAVE,R(244,286,136,60),hi(hi(hs(H0,t.ok),pressed(T_SAVE)),act_ok))){
        if(act_ok) draw_pill_btn(R(248,292,124,48),t.ok,xsel==X_SWITCH?F_XS:F_S,rgb(250,204,140),rgb(232,174,100),C_ACCD,pressed(T_SAVE));
        else draw_pill_btn(R(248,292,124,48),t.ok,F_S,C_SURF2,C_SURF,C_DIS,0);
        W_end();
    }
    if(W_begin(W_XFOOT,R(70,392,340,40),hi(hs(hs(H0,t.l1),t.l2),(int)t.c_l1))){
        text_c(CX,398,t.l1,F_XS,t.c_l1,255); text_c(CX,416,t.l2,F_XS,C_TXT3,255); W_end();
    }
}
/* last frame before the runtime switch: stays on screen until the stock UI draws */
static const char* bye_sub(void){ return TR(bye_mode==SWM_CONT?TX_Y_BYE_CONT:TX_Y_BYE_START); }
static void draw_bye(void){
    if(W_begin(W_BYE,R(60,150,360,180),hi(hi(H0,bye_mode),ui_lang))){
        float iw=text_wf(IC_CUP,F_ICL); text_at(CX-iw/2,152,IC_CUP,F_ICL,C_ACC,255);
        text_c(CX,206,TR(TX_Y_BYE_T),F_M,C_TXT,255);
        text_c(CX,240,bye_sub(),F_XS,C_TXT2,255);
        text_c(CX,262,TR(TX_Y_BYE_CLOSED),F_XS,C_TXT3,255);
        W_end();
    }
}

/* ================= v6.14: TURNING OFF… / OFF / TURNING ON… (the power button) =================
 * Black page, power symbol (ring w/ opening at the top + bar), title and one or two lines. The backlight goes off 1.5 s after the off press
 * (like stock), so the OFF page is only seen for a moment — or for 8 s after a tap on the screen. */
#define OFF_IC_Y 178
static void draw_off(ms_t now){
    (void)now; int st=pw.state;
    if(W_begin(W_OFF,R(60,120,360,210),hi(hi(H0,st),ui_lang))){
        uint32_t ic=st==PW_OFF?C_DIS:C_ACC;
        arc_aa(CX,OFF_IC_Y,30,3.0f,-50,230,ic,255);                                   /* the ring: open from -130° to -50° (at the top) */
        capsule(CX,OFF_IC_Y-40,CX,OFF_IC_Y-6,3.0f,ic,255);                            /* the bar */
        text_c(CX,232,TR(st==PW_OFFING?TX_PW_OFFING:(st==PW_WAKING?TX_PW_WAKING:TX_PW_OFF)),F_M,st==PW_OFF?C_TXT2:C_TXT,255);
        text_c(CX,268,TR(st==PW_OFFING?TX_PW_SUB_OFFING:(st==PW_WAKING?TX_PW_SUB_WAKING:TX_PW_SUB_OFF)),F_XS,C_TXT3,255);
        if(st==PW_OFF) text_c(CX,290,TR(TX_PW_NOTE_OFF),F_XS,C_DIS,255);
        W_end();
    }
}

/* ================= page: DIAGNOSE ================= */
static void static_diag(void){ static_page_title(); }   /* v6.17: title only (the sub-line listed the sections) */
#define DG_N 16
#define DG_NB 6
#define DG_FULL_W 336
#define DG_COL_L 154
#define DG_COL_R 326
/* v6.11: cell i -> slot (0..7 = the STATUS block at the top, 8..15 = RAW COUNTERS); even = left column */
static const signed char DG_SLOT[DG_N]={0,1,8,9,10,2,11,3,4,12,13,14,15,5,6,7};
static int dg_cell_y(int i){ int k=DG_SLOT[i]; return k<8? DG_Y0+(k/2)*DG_P : DG_RAW+28+((k-8)/2)*DG_P; }
static const short DG_LAB[DG_N+DG_NB]={TX_DL_LINK,TX_DL_PHASE,TX_DL_RXOK,TX_DL_RXCRC,TX_DL_WORKSTA,TX_DL_E123,TX_DL_CMD6,TX_DL_AWAKE,TX_DL_MAINS,TX_DL_CURRENT,TX_DL_FRAMEMS,TX_DL_MIRROR,TX_DL_CFGMD5,TX_DL_VERSION,TX_DL_BP_A,TX_DL_BP_B,
                                       TX_DL_FLAG,TX_DL_NEXTBOOT,TX_DL_STARTED,TX_DL_SLOTB,TX_DL_SWITCHER,TX_DL_STOCK};
/* the boot block's texts (shared w/ the selftest). v[0..5] = cells, exe/evt = full-width lines */
static void diag_boot_text(char v[DG_NB][28],uint32_t col[DG_NB],char*exe,size_t en,char*evt,size_t vn,char*pids,size_t pn){
    for(int i=0;i<DG_NB;i++) col[i]=C_TXT;
    snprintf(v[0],28,"%s",TR(bs.flag?TX_DV_FLAG_YES:TX_DV_FLAG_NO));
    snprintf(v[1],28,"%s",boot_next_name()); if(bs.flag&&bs.lock!=BL_OK) col[1]=C_WARN;       /* flag set, but slot B is not merakiplus */
    snprintf(v[2],28,"%s",TR(bs.self_slot==SLOT_B?TX_DV_SLOTB:(bs.self_slot==SLOT_A?TX_DV_SLOTA:(bs.self_exe[0]?TX_DV_RUNSH:TX_DV_UNKNOWN))));
    snprintf(v[3],28,"%s",TR(bs.lock==BL_OK?TX_DV_B_OK:(bs.lock==BL_NOMOUNT?TX_DV_B_NOMOUNT:(bs.lock==BL_NOBIN?TX_DV_B_NOBIN:TX_DV_B_VENDOR)))); col[3]=bs.lock==BL_OK?C_OK:C_WARN;
    snprintf(v[4],28,"%s",TR(bs.sw_pid?TX_DV_RUNNING:TX_DV_NOTRUNNING)); col[4]=bs.sw_pid?C_OK:C_WARN;
    snprintf(v[5],28,"%s",TR(!bs.n_stock?TX_DV_NONE:(bs.stock_run?TX_DV_ALSO:TX_DV_PAUSED))); if(bs.stock_run) col[5]=C_ERR;
    { char a[16]="-",b[16]="-"; if(bs.n_stock) snprintf(a,sizeof a,"%d",bs.stock[0]); if(bs.sw_pid) snprintf(b,sizeof b,"%d",bs.sw_pid);
      snprintf(pids,pn,"%d / %s%s / %s",bs.self_pid,a,bs.n_stock>1?" +":"",b); }
    if(bs.self_exe[0]) snprintf(exe,en-4,LC "%s",bs.self_exe); else snprintf(exe,en-4,"%s",TR(TX_DG_NOEXE));
    utf8_trim(exe); fit_text(exe,F_XS,DG_FULL_W);
    /* the event text comes verbatim from the switcher daemon's log (boot/switcher.c) and is not translated */
    snprintf(evt,vn-4,"%s",bs.sw_log==2?bs.sw_evt:TR(bs.sw_log==1?TX_DG_NOEVT:TX_DG_NOLOG));
    utf8_trim(evt); fit_text(evt,F_XS,DG_FULL_W);
}
/* v6.5: boot-push status per block (0 = sub1 len30, 1 = sub112 len3) */
static const char* bp_text(int blk,uint32_t*col){
    int r=blk?bp.rb:bp.ra, sent=blk?bp.state==BP_B:bp.state==BP_A; *col=C_TXT;
    if(r==BPR_OK){ *col=C_OK; return TR(TX_DV_OK); }
    if(r==BPR_NOACK){ *col=C_WARN; return TR(TX_DV_BP_NOACK); }
    if(r==BPR_BLOCKED){ *col=C_WARN; return TR(TX_DV_BP_BLOCKED); }
    if(r==BPR_NOCFG){ *col=C_WARN; return TR(TX_DV_BP_NOCFG); }
    if(r==BPR_SKIP) return TR(TX_DV_BP_SKIP);
    if(bp.state==BP_OFF) return TR(TX_DV_BP_OFF);
    return TR(sent?TX_DV_BP_SENT:TX_DV_BP_WAIT);
}
/* the top 16 cells (shared w/ the selftest) */
static void diag_top_text(char v[DG_N][28],uint32_t col[DG_N],ms_t now){
    int up=link_up(now);
    snprintf(v[0],28,"%s",TR(up?TX_DV_OK:TX_DV_DOWN)); snprintf(v[1],28,"%s",act.ph==PH_STEAM?steam_label(stm.kind):ph_name(act.ph));
    snprintf(v[2],28,"%u",rx_ok); snprintf(v[3],28,"%u / %u",rx_bad,rx_resync);
    snprintf(v[4],28,"%04X",reg[33]); snprintf(v[5],28,"%X / %X / %X",reg[34],reg[35],reg[36]);
    snprintf(v[6],28,"%ld / %ld / %ld / %ld",wq.n_ok,wq.n_retx,wq.n_fail,wq.n_block); snprintf(v[7],28,"%s",!reg_seen[1]?"?":TR(reg[1]==0?TX_DV_YES:TX_DV_NO));
    snprintf(v[8],28,TR(TX_DV_MAINS),reg[49],reg[61]); snprintf(v[9],28,"%u / %u",reg[50],reg[58]);
    snprintf(v[10],28,"%.1f / %.1f",ft_last,ft_max);
    if(reg_seen[112]) snprintf(v[11],28,"%04X %04X %u",reg[112],reg[113],reg[114]); else snprintf(v[11],28,"-");
    if(cfg_ok) snprintf(v[12],28,"%.8s%s",cfg_md5,cfg_writes?" *":""); else snprintf(v[12],28,"%s",TR(TX_DV_MISSING));
    snprintf(v[13],28,"%s",TR(ro_mode?TX_DV_VERSION_RO:TX_DV_VERSION));
    uint32_t bc[2]; snprintf(v[14],28,"%s",bp_text(0,&bc[0])); snprintf(v[15],28,"%s",bp_text(1,&bc[1]));
    for(int i=0;i<DG_N;i++){ uint32_t c=C_TXT; if(i==0) c=up?C_OK:C_ERR; if(i==3&&(rx_bad||rx_resync)) c=C_WARN; if(i>=14) c=bc[i-14]; if(i==5&&(reg[34]|reg[35]|reg[36])) c=C_ERR; if(i==6&&(wq.n_fail||wq.n_block)) c=C_WARN; if(i==1) c=ph_hi(act.ph); col[i]=c; }
}
/* v6.6: cell label. After a re-push the boot-push row is labelled "RE-PUSH 2 · 1-30" | "112-114" (2 = the boot push has been resent
 * from scratch twice after an MCU restart), the first in the accent colour; the cells show the outcome of the LATEST push.
 * Without a re-push — and for all other cells — the label is the table's text. */
static uint32_t diag_label(int i,char*o,size_t n){
    int rc=repush_count(); if(rc>999) rc=999;
    if(i==14&&rc>0){ snprintf(o,n,TR(TX_DL_BP_A_RE),rc); return C_ACC; }
    if(i==15&&rc>0){ snprintf(o,n,"%s",TR(TX_DL_BP_B_RE)); return C_TXT3; }
    snprintf(o,n,"%s",TR(DG_LAB[i])); return C_TXT3;
}
/* the ABOUT block at the bottom: which UI language is active, and where it comes from */
static void diag_lang_text(char*o,size_t n){
    static const short f[]={[LS_DEFAULT]=TX_DG_LANG_DEFAULT,[LS_FILE]=TX_DG_LANG_FILE,[LS_UNKNOWN]=TX_DG_LANG_UNKNOWN,[LS_SESSION]=TX_DG_LANG_SESSION};
    snprintf(o,n,TR(f[lang_src]),TR(TX_UL_EN+ui_lang));
}
/* v6.7: the NET line (shared w/ the selftest): wpa_supplicant pid / IP / net watchdog state. F_S; if the line gets too wide
 * (5-digit pid + 15-char IP + CONNECTING), it is drawn in F_XS so it is never clipped by the circle. */
static int diag_net_font(const char*t){ return text_wf(t,F_S)<=DG_FULL_W?F_S:F_XS; }
static void diag_net_text(char*o,size_t n,uint32_t*col){
    char ipb[20],pid[16]="-"; int s=net.on?net.state:NS_OFF;
    if(net.p.wpa_pid>0) snprintf(pid,sizeof pid,"%d",net.p.wpa_pid);
    snprintf(o,n,"%s / %s / %s",pid,net_ipstr(net.p.ip,ipb,sizeof ipb),TR(TX_NS_OFF+s));
    *col=s==NS_UP?C_OK:(s==NS_OFF?C_TXT2:C_WARN);
}
/* v6.10: the PRESSURE · ESTIMATE block (shared w/ the selftest): the anchor line, the now line (raw word 51 -> ≈bar; above P_EST_MAX a pulse —
 * at idle "MAINTENANCE PULSE", during an activity "PULSE, NO PRESSURE", never a bar value) and note line 2 (the limit) */
static void diag_press_text(char*anc,size_t an,char*nw,size_t nn,uint32_t*col){
    snprintf(anc,an,TR(TX_DV_PANCH),p_anchor_bar(),P_ANCHOR_RAW,(double)p_k());
    int r=reg[51]; float b=p_bar((float)r);
    if(b>=0.f){ snprintf(nw,nn,TR(TX_DV_PNOW),r,(double)b); *col=C_TXT; }
    else { snprintf(nw,nn,TR(!phase_active(act.ph)&&act.ph!=PH_DONE?TX_DV_PMAINT:TX_DV_PPULSE),r); *col=C_WARN; }
}
static void dg_caption(int yc,const char*cap,Rect vr){
    int tw=text_w(cap,F_XS);
    if(yc+11>=vr.y0&&yc<=vr.y1){ text_c(CX,yc,cap,F_XS,C_ACC,255); hspan(yc+5,84,CX-tw/2-12,C_LINE,255); hspan(yc+5,CX+tw/2+12,396,C_LINE,255); }
}
/* v6.22: the CONTROLLER block's texts (shared w/ the selftest): the firmware string as reported (words 80..87, read-only) and the hot water
 * capability the gate derives from it. Owners quote this line in bug reports ("SYSTEM › DIAGNOSTICS › CONTROLLER"). */
static void diag_mcu_text(char ver[28],uint32_t*vc,char wdt[28],uint32_t*wc){
    char b[20]; int v[3], st=mcu_ver_read(b,sizeof b,v);
    if(st==MV_NONE){ snprintf(ver,28,"%s",TR(TX_DV_MCU_NONE)); *vc=C_TXT2; }
    else if(st==MV_COPY){ snprintf(ver,28,"%s",TR(TX_DV_MCU_COPY)); *vc=C_WARN; }
    else { snprintf(ver,28,"%s",b); *vc=st==MV_OK?C_TXT:C_WARN; }
    if(wd_dosing()){ snprintf(wdt,28,"%s",TR(TX_DV_WD_AUTO)); *wc=C_OK; }
    else if(wd_ver_dosing()){ snprintf(wdt,28,"%s",TR(TX_DV_WD_FAULT)); *wc=C_WARN; }
    else { snprintf(wdt,28,"%s",TR(TX_DV_WD_MAN)); *wc=C_TXT2; }
}
/* v6.14: the POWER BUTTON block's texts (shared w/ the selftest) */
static void diag_pw_text(char st[28],uint32_t*stc,char w0[28],char cnt[28],char key[40],char*node,size_t nn,ms_t now){
    static const short S[4]={TX_DV_PW_ON,TX_DV_PW_OFFING,TX_DV_PW_OFF,TX_DV_PW_WAKING};
    snprintf(st,28,"%s",TR(S[pw.state])); *stc=pw.state==PW_ON?C_OK:(pw.state==PW_OFF?C_TXT2:C_WARN);
    if(link_up(now)&&reg_seen[0]) snprintf(w0,28,"%u",(unsigned)reg[0]); else snprintf(w0,28,"%s",TR(TX_DV_PW_NOREPLY));
    snprintf(cnt,28,"%ld · %ld · %ld",pw.n_press,pw.n_off,pw.n_on);
    if(pw.n_ev) snprintf(key,40,TR(TX_DV_PW_KEY),pw.key_code,pw.key_val,(long)((now-pw.t_key)/1000)); else snprintf(key,40,"%s",TR(TX_DV_PW_NOKEY));
    if(pw.kbd_ok) snprintf(node,nn,LC "%s",pw.node); else snprintf(node,nn,"%s",TR(TX_DV_PW_NONODE));   /* the path as a code span (lower case) */
}
/* v6.16: the TIME · SCHEDULE block's texts (shared w/ the selftest) */
static void diag_time_text(char clk[28],uint32_t*cc,char zone[40],char ao[28],char al[40],char nx[72],ms_t now){
    struct tm lt; int64_t u=clk_utc(); int ok=clk_now(&lt), d=ok&&SET[S_DST].val&&dst_on(tz_idx(),u);
    if(ok){ fmt_hm(clk,28,lt.tm_hour,lt.tm_min,1); *cc=C_TXT; } else { snprintf(clk,28,"%s",TR(TX_DV_T_NOTSET)); *cc=C_WARN; }
    snprintf(zone,40,TR(d?TX_DV_T_DST:TX_DV_T_STD),TZT[tz_idx()].utc);
    int a=SET[S_AUTOOFF].val; if(a>0&&a<5){ ms_t id=now-idle_t0; if(id<0) id=0; snprintf(ao,28,TR(TX_DV_T_IDLE),TR(OPT_AUTOOFF[a]),(int)(id/60000),(int)(id/1000%60)); }
    else snprintf(ao,28,"%s",TR(TX_E_OFF));
    if(arm.n_sent) snprintf(al,40,"%d %02d:%02d · %ld/%ld",arm.ad,arm.ah,arm.am,arm.n_sent,arm.n_ack); else snprintf(al,40,"%s",TR(TX_DV_T_NONE));
    if(!home_preheat_text(nx,72)) snprintf(nx,72,"%s",TR(sch.on&&!ok?TX_T_NOCLOCK:TX_DV_T_NOSCHED));
}
static void draw_diag(ms_t now){
    char v[DG_N+DG_NB][28]; uint32_t col[DG_N+DG_NB]; char exe[160],evt[128],pids[48],lng[96],nets[64],panc[64],pnow[64]; uint32_t netc,pcol;
    diag_top_text(v,col,now); diag_net_text(nets,sizeof nets,&netc); diag_press_text(panc,sizeof panc,pnow,sizeof pnow,&pcol);
    diag_boot_text(v+DG_N,col+DG_N,exe,sizeof exe,evt,sizeof evt,pids,sizeof pids);
    diag_lang_text(lng,sizeof lng); draw_pback();
    char pst[28],pw0[28],pcnt[28],pkey[40],pnode[80]; uint32_t pstc; diag_pw_text(pst,&pstc,pw0,pcnt,pkey,pnode,sizeof pnode,now);   /* v6.14 */
    char tck[28],tzn[40],tao[28],tal[40],tnx[72]; uint32_t tcc; diag_time_text(tck,&tcc,tzn,tao,tal,tnx,now);   /* v6.16 */
    char mver[28],mwd[28]; uint32_t mvc,mwc; diag_mcu_text(mver,&mvc,mwd,&mwc);                                   /* v6.22 */
    int sc=(int)lroundf(scroll[PG_DIAG]); float mx=scroll_max(PG_DIAG);
    uint32_t h=hi(hi(hi(hi(hi(H0,sc),ui_lang),repush_count()),cd.raw),pressed(T_GRAPH)); for(int i=0;i<DG_N+DG_NB;i++) h=hi(hs(h,v[i]),(int)col[i]); h=hs(hs(hs(hs(h,exe),evt),pids),lng); h=hi(hs(h,nets),(int)netc); h=hi(hs(hs(h,panc),pnow),(int)pcol);
    h=hi(hs(hs(hs(hs(hs(h,pst),pw0),pcnt),pkey),pnode),(int)pstc);
    h=hi(hs(hs(hs(hs(hs(h,tck),tzn),tao),tal),tnx),(int)tcc);
    h=hi(hi(hs(hs(h,mver),mwd),(int)mvc),(int)mwc);
    Rect vr=R(40,LIST_VY0,400,LIST_VY1-LIST_VY0);
    if(W_begin(W_DIAG,vr,h)){
        for(int i=0;i<DG_N+DG_NB;i++){
            int cx=((i<DG_N?DG_SLOT[i]:i)&1)?DG_COL_R:DG_COL_L, y=(i<DG_N? dg_cell_y(i) : DG_BOOT+((i-DG_N)/2)*DG_P)-sc;
            if(y+30<vr.y0||y>vr.y1) continue;
            { char lb[48]; uint32_t lc=diag_label(i,lb,sizeof lb); text_c(cx,y,lb,F_XS,lc,255); }
            text_c(cx,y+16,v[i],F_S,col[i],255);
        }
        dg_caption(DG_BOOT-28-sc,TR(TX_DG_CAP),vr);                               /* section heading */
        for(int k=0;k<3;k++){   /* full-width lines: PIDs, exe path, latest switch */
            int y=DG_BOOT+(3+k)*DG_P-sc; if(y+30<vr.y0||y>vr.y1) continue;
            text_c(CX,y,TR(k==0?TX_DG_L_PID:(k==1?TX_DG_L_EXE:TX_DG_L_EVT)),F_XS,C_TXT3,255);
            if(k==0) text_c(CX,y+16,pids,F_S,C_TXT,255);
            else text_c(CX,y+16,k==2?evt:exe,F_XS,(k==2&&bs.sw_log!=2)?C_TXT2:C_TXT,255);
        }
        { int y=DG_BOOT+6*DG_P-sc;                                                  /* v6.7: NET — wpa pid / IP / state */
          if(y+30>=vr.y0&&y<=vr.y1){ text_c(CX,y,TR(TX_DG_L_NET),F_XS,C_TXT3,255); text_c(CX,y+16,nets,diag_net_font(nets),netc,255); } }
        { int y=DG_GRAPH-sc;                                                        /* v6.9: GRAPH — smooth / raw (tap) */
          if(y+40>=vr.y0&&y<=vr.y1){ text_c(CX,y,TR(TX_DG_L_GRAPH),F_XS,C_TXT3,255);
              uint32_t gc=cd.raw?C_WARN:C_TXT; rrect(CX-150,y+12,300,26,13,cd.raw?mix(C_SURF,C_WARN,40):C_SURF2,255);
              if(pressed(T_GRAPH)) rrect(CX-150,y+12,300,26,13,rgb(255,255,255),36);
              text_c(CX,y+18,TR(cd.raw?TX_DV_G_RAW:TX_DV_G_SMOOTH),F_XS,gc,255); } }
        /* v6.10: PRESSURE · ESTIMATE — anchor (pre-infusion in the capture = config pressure), current value (raw -> ≈bar / pulse), caveat */
        dg_caption(DG_PRESS-sc,TR(TX_DG_PCAP),vr);
        { int y=DG_PRESS+28-sc;
          if(y+30>=vr.y0&&y<=vr.y1){ text_c(CX,y,TR(TX_DG_L_PANCH),F_XS,C_TXT3,255); text_c(CX,y+16,panc,F_S,C_TXT,255); }
          y+=DG_P; if(y+30>=vr.y0&&y<=vr.y1){ text_c(CX,y,TR(TX_DG_L_PNOW),F_XS,C_TXT3,255); text_c(CX,y+16,pnow,F_S,pcol,255); } }   /* v6.17: the two caveat lines moved to docs/calibration.md */
        dg_caption(DG_RAW-sc,TR(TX_DG_RAWCAP),vr);                               /* v6.11: RAW COUNTERS (the cells are drawn in the loop above) */
        /* v6.14: POWER BUTTON — state, the controller's word 0, press/off/on, latest key event, node */
        dg_caption(DG_PWR-sc,TR(TX_DG_PWCAP),vr);
        { static const short L[4]={TX_DL_PW_STATE,TX_DL_PW_W0,TX_DL_PW_COUNT,TX_DL_PW_LAST}; const char*vv[4]={pst,pw0,pcnt,pkey};
          for(int k=0;k<4;k++){ int cx=(k&1)?DG_COL_R:DG_COL_L, y=DG_PWR+28+(k/2)*DG_P-sc; if(y+30<vr.y0||y>vr.y1) continue;
              text_c(cx,y,TR(L[k]),F_XS,C_TXT3,255); text_c(cx,y+16,vv[k],k==3?F_XS:F_S,k==0?pstc:C_TXT,255); }
          int y=DG_PWR+28+2*DG_P-sc;
          if(y+30>=vr.y0&&y<=vr.y1){ text_c(CX,y,TR(TX_DG_L_PWNODE),F_XS,C_TXT3,255); text_c(CX,y+16,pnode,F_XS,pw.kbd_ok?C_TXT:C_WARN,255); } }
        /* v6.16: TIME · SCHEDULE — local clock, zone + DST, auto off + idle time, last cmd8 alarm sent (day h:m · sent/acked), next pre-heat */
        dg_caption(DG_TIME-sc,TR(TX_DG_TCAP),vr);
        { static const short L[4]={TX_DL_T_CLOCK,TX_DL_T_ZONE,TX_DL_T_AUTOOFF,TX_DL_T_ALARM}; const char*vv[4]={tck,tzn,tao,tal};
          for(int k=0;k<4;k++){ int cx=(k&1)?DG_COL_R:DG_COL_L, y=DG_TIME+28+(k/2)*DG_P-sc; if(y+30<vr.y0||y>vr.y1) continue;
              text_c(cx,y,TR(L[k]),F_XS,C_TXT3,255); text_c(cx,y+16,vv[k],k==0?F_S:F_XS,k==0?tcc:C_TXT,255); }
          int y=DG_TIME+28+2*DG_P-sc;
          if(y+30>=vr.y0&&y<=vr.y1){ text_c(CX,y,TR(TX_DG_L_NEXT),F_XS,C_TXT3,255); text_c(CX,y+16,tnx,F_XS,sch.on?C_TXT:C_TXT2,255); } }
        /* v6.18: SOUND — mode · volume (amber: muted / no player), the last event and what became of it */
        dg_caption(DG_SND-sc,TR(TX_DG_SNDCAP),vr);
        { char sm[40], sl[64]; uint32_t smc=snd_diag_text(sm,sizeof sm,sl,sizeof sl); const short L[2]={TX_DL_SND,TX_DL_SND_LAST}; const char*vv[2]={sm,sl};
          for(int k=0;k<2;k++){ int cx=k?DG_COL_R:DG_COL_L, y=DG_SND+28-sc; if(y+30<vr.y0||y>vr.y1) continue;
              text_c(cx,y,TR(L[k]),F_XS,C_TXT3,255); text_c(cx,y+16,vv[k],F_XS,k?C_TXT:smc,255); } }
        /* v6.22: CONTROLLER — firmware string (words 80..87) · hot water capability (AUTO + MANUAL / MANUAL ONLY / MANUAL · SCALE FAULT) */
        dg_caption(DG_MCU-sc,TR(TX_DG_MCUCAP),vr);
        { const short L[2]={TX_DL_MCU_VER,TX_DL_MCU_WD}; const char*vv[2]={mver,mwd}; const uint32_t cc[2]={mvc,mwc};
          for(int k=0;k<2;k++){ int cx=k?DG_COL_R:DG_COL_L, y=DG_MCU+28-sc; if(y+30<vr.y0||y>vr.y1) continue;
              text_c(cx,y,TR(L[k]),F_XS,C_TXT3,255); text_c(cx,y+16,vv[k],F_XS,cc[k],255); } }
        /* v6.3: ABOUT — UI language (+ source), wordmark w/ version, tagline */
        dg_caption(DG_ABOUT-sc,TR(TX_DG_ABOUT),vr);
        { int y=DG_ABOUT+28-sc;
          if(y+30>=vr.y0&&y<=vr.y1){ text_c(CX,y,TR(TX_DG_L_LANG),F_XS,C_TXT3,255); text_c(CX,y+16,lng,F_S,(lang_src==LS_UNKNOWN||lang_src==LS_SESSION)?C_WARN:C_TXT,255); }   /* amber: the file has an unknown value / could not be written */
          y=DG_ABOUT+76-sc;
          if(y+36>=vr.y0&&y<=vr.y1){
              float w=text_wf(PRODUCT " " MERAKIPLUS_VERSION,F_S), x=brand_at(CX-w/2,y,F_S,C_TXT,255);
              text_at(x,y," " MERAKIPLUS_VERSION,F_S,C_TXT2,255);
              text_c(CX,y+24,TR(TX_TAGLINE),F_XS,C_TXT3,255);
          } }
        edge_fade(vr,sc>0,sc<(int)mx);
        W_end();
    }
    draw_scroll_ind(DG_BOTTOM-DG_Y0+16);
}

/* ================= boot splash ================= */
#define SPLASH_W 300
static void splash_status(char*b,size_t n){ snprintf(b,n,MERAKIPLUS_VERSION " · %s",TR(cfg_ok?TX_SPL_CFG_OK:TX_SPL_CFG_MISSING)); }
static void draw_splash(ms_t now){
    float t=(now-ui_start)/(float)SPLASH_MS; int q=(int)(t*30);
    if(W_begin(W_SPL,R(CX-SPLASH_W/2,140,SPLASH_W,200),hi(hi(H0,q),ui_lang))){
        int a=(int)(255*clampf(t*3,0,1));
        float y=4*(1-ease_out(clampf(t*2.2f,0,1)));
        float iw=text_wf(IC_BEAN,F_ICL); text_at(CX-iw/2,152+(int)y,IC_BEAN,F_ICL,C_ACC,a);
        brand_c(CX,204+(int)y,F_XL,C_TXT,a);                       /* wordmark: MERAKI + "+" badge */
        int a2=(int)(255*clampf(t*3-0.6f,0,1)); char b[96];
        text_c(CX,270,TR(TX_TAGLINE),F_XS,C_TXT2,a2);
        splash_status(b,sizeof b); text_c(CX,296,b,F_XS,cfg_ok?C_TXT3:C_WARN,a2);
        W_end();
    }
}

/* ================= composition ================= */
static void static_build(void){
    memcpy(bgbase,bgpure,(size_t)W*H*4);
    cv=bgbase; set_clip(FULL);
    if(view==V_OFF) memset(bgbase,0,(size_t)W*H*4);                   /* v6.14: the OFF page is black (the backlight is off anyway) */
    else if(view==V_DETAIL) static_detail();
    else if(view==V_SYS) static_sys();
    else if(view==V_MAIN){
        if(pg_list(page)) static_list();
        else if(page==PG_DIAG) static_diag();
        else static_home();
    }
    memcpy(slay,bgbase,(size_t)W*H*4);
    cv=canvas;
}
static void compose(ms_t now){
    dmg.n=0; pres.n=0;
    if(static_dirty){ static_build(); static_dirty=0; force_all=1; }
    Rect bb;
    if(arc_paint(&garc,force_all,&bb)) rl_add(&dmg,bb);
    if(arc_paint(&parc,force_all,&bb)) rl_add(&dmg,bb);
    overlays_damage(now);
    if(force_all){ dmg.n=0; rl_add(&dmg,FULL); }
    for(int i=0;i<dmg.n;i++){ blit_rect(canvas,slay,dmg.r[i]); rl_add(&pres,dmg.r[i]); }
    cv=canvas; set_clip(FULL);
    if(view==V_SPLASH) draw_splash(now);
    else if(view==V_DETAIL) draw_detail(now);
    else if(view==V_SYS) draw_sys(now);
    else if(view==V_BYE) draw_bye();
    else if(view==V_OFF) draw_off(now);                               /* v6.14 */
    else if(page==PG_HOME) draw_home(now);
    else if(page==PG_DIAG) draw_diag(now);
    else draw_list(now);
    overlays_draw(now);
    force_all=0;
}
static int tr_was=0;
/* one UI frame: update, compose, present. Returns the buffer that was shown. */
static const uint32_t* ui_frame(ms_t now){
    ui_update(now);
    if(tr.type){ tr_was=1; if(trans_frame(now)) return tbuf; }
    if(tr_was){ tr_was=0; force_all=1; }
    compose(now);
    fb_present(canvas,&pres);
    return canvas;
}

/* protocol maintenance: keep-awake = word1 written as 0 (FIX: v4 wrote 1 = sleep/drain) */
static ms_t ka_next=0;
static void proto_keepawake(ms_t now,int first){
    if(ro_mode) return;
    if(pw.state!=PW_ON) return;            /* v6.14: switched off with the button — keep-awake does not fight it (the MCU is asleep and does not reply) */
    SIM_KA_HOLD
    if(first){ uint16_t on=1, awake=0; wq_push(0,&on,1); wq_push(1,&awake,1); ka_next=now+30000; return; }
    int asleep=link_up(now)&&reg_seen[1]&&reg[1]!=0;
    if((now>=ka_next||(asleep&&now>=ka_next-27000))&&!wq_busy()&&act.pending==ACT_NONE&&!sav.active){
        uint16_t awake=0; wq_push(1,&awake,1); ka_next=now+30000;
    }
}

/* ---- BOOT-PUSH (PARITY §1.4) — v6.4: SIM/preview; v6.5: ALSO THE DEVICE ----
 * At startup the stock UI's fuart_thread sends its config mirror to the controller:
 *     cmd6 sub1 len30 (words 1..30)   +   cmd6 sub112 len3 (words 112..114)
 * merakiplus now does the same in all build types. Without it, after a slot B boot (where the stock UI
 * never starts) the controller sits on its own power-on defaults (REGISTERMAP §2: word 11 = 920,
 * word 114 = 50, word 14 = 4, word 25 = 550 …) instead of the config's. In run.sh mode the
 * paused stock UI has already sent the blocks; merakiplus then sends the same values again (no effect).
 * The word layout is stock's (model_data_init 0xcef40 + fuart_thread 0x146810): words 2/4/11/14/17/23/25
 * from config, words 5/13/15 = 60, all other words in 1..30 = 0, words 112/113 packed, word 114 = headTemper.
 * The blocks go through the SAME gate as everything else (wq_push without intent -> frame policy): start words
 * and words 26/27/30 are 0, and a config value out of spec gets the WHOLE block rejected and counted (BLOK) —
 * it is never sent. Each block is sent once (w/ the queue's ACK wait + single retransmit); if the
 * ACK does not arrive, it continues with the next block. The result is shown on DIAGNOSE (and in the SIM's state JSON).
 * After a completed SAVE, a push that was rejected, not ACKed or lacked config is repeated.
 *
 * ACTIVITY GUARD (guard, device): the block writes the start words 3/6/9/16/22/24 as 0 and could
 * thereby abort a brew that is already running — e.g. when run.sh is started in the middle of an activity,
 * or merakiplus is restarted during one. Hence it is decided ONCE, the first time the link is up and
 * work_sta (word 33) has been read: if grind/brew/water/steam is running (phase_active), the push is SKIPPED
 * (BPR_SKIP) — it is not deferred and not repeated by a later SAVE. The controller then has the
 * values that activity was started with; every SAVE and every start block still writes its words.
 * Without the guard (the SIM contract, unchanged from v6.4) the push is instead DEFERRED until the activity has ended.
 * An activity that starts AFTER the decision defers the remaining blocks (both modes). */
/* v6.7 — WORD 113 IN THE BOOT PUSH: the lo byte (breakTime) is sent as 0, not as config's breakTime. Stock's own
 * boot push was captured on the device (boot/ondevice/GROUND-TRUTH.md): sub112 len3 = 04 01 | 00 06 | 46 00, i.e.
 * word 113 = 0x0600 with breakTime = 5 in config — fuart_thread clears the lo byte at boot. PARITY §1.3's
 * packing (time<<8 | breakTime) applies to the SAVE path (setting_mcu_push / act_push), which is unchanged. No effect
 * on brewing: the controller (0x1E10) overwrites the lo byte itself on the next brew. The hi byte (pre-inf. time >= 5)
 * keeps the word != 0, so the re-push signature (word 112 = 0 and word 113 = 0) is still unambiguous. */
static void boot_push_words(uint16_t a[30],uint16_t p[3]){   /* a[k] = word k+1 */
    memset(a,0,30*sizeof a[0]);
    a[1-1]=0; a[2-1]=(uint16_t)sv(S_GRINDMODE); a[4-1]=(uint16_t)sv(S_GRINDW); a[5-1]=60;
    a[11-1]=(uint16_t)sv(S_BREWT); a[13-1]=60; a[14-1]=(uint16_t)sv(S_STEAMG); a[15-1]=60;
    a[17-1]=(uint16_t)sv(S_HOTW); a[23-1]=(uint16_t)sv(S_BREWW); a[25-1]=(uint16_t)sv(S_STEAMT);
    p[0]=pack112(sv(S_PISW),sv(S_PIPRESS)); p[1]=pack113(sv(S_PITIME),0); p[2]=(uint16_t)sv(S_HEADT);   /* v6.7: word 113 lo = 0 — see below */
}
static int boot_push_cfg_ok(void){ for(int i=0;i<NSET;i++) if(SET[i].mcu!=M_NONE&&!SET[i].loaded) return 0; return 1; }
static void boot_push_start(int guard){ bp.state=BP_WAIT_A; bp.ra=bp.rb=BPR_NONE; bp.guard=guard; bp.t0_set=0; }
#define BP_LEFT(now) do{ if(!bp.t0_set){ bp.t0=(now); bp.t0_set=1; } }while(0)   /* first block queued / push decided */
static void boot_push_step(ms_t now){
    if(bp.state==BP_OFF||bp.state==BP_DONE||ro_mode) return;
    if(bp.state==BP_A||bp.state==BP_B){
        if(wq.seq==bp.seq) return;                         /* waiting for ACK / timeout */
        int r=(wq.result==WR_OK)?BPR_OK:BPR_NOACK;
        if(bp.state==BP_A){ bp.ra=r; bp.state=BP_WAIT_B; } else { bp.rb=r; bp.state=BP_DONE; bp.runs++; }
        return;
    }
    if(bp.guard){                                          /* activity guard: decided once, when work_sta is known */
        if(!link_up(now)||!reg_seen[33]) return;
        bp.guard=0;
        if(phase_active(phase_raw(now))){ bp.ra=bp.rb=BPR_SKIP; bp.state=BP_DONE; bp.runs++; BP_LEFT(now); return; }
    }
    if(!link_up(now)||wq_busy()||sav.active||act.pending!=ACT_NONE||phase_active(phase_raw(now))) return;
    if(!boot_push_cfg_ok()){ bp.ra=bp.rb=BPR_NOCFG; bp.state=BP_DONE; bp.runs++; BP_LEFT(now); return; }   /* no config = nothing to push */
    uint16_t a[30],p[3]; boot_push_words(a,p); long seq=wq.seq;
    BP_LEFT(now);
    if(bp.state==BP_WAIT_A){ if(wq_push(1,a,30)){ bp.seq=seq; bp.state=BP_A; } else { bp.ra=BPR_BLOCKED; bp.state=BP_WAIT_B; } }
    else { if(wq_push(112,p,3)){ bp.seq=seq; bp.state=BP_B; } else { bp.rb=BPR_BLOCKED; bp.state=BP_DONE; bp.runs++; } }
}
/* a SAVE has gone through: a push that was rejected / not ACKed / lacked config is retried
 * (config may now be in spec). A push that the guard skipped is not repeated. */
static int bp_failed(int r){ return r==BPR_NOACK||r==BPR_BLOCKED||r==BPR_NOCFG; }
static void boot_push_resave(void){ if(bp_on&&bp.state==BP_DONE&&(bp_failed(bp.ra)||bp_failed(bp.rb))) boot_push_start(0); }

/* ---- MCU RESTART -> RE-PUSH (v6.6; the open item in CHANGES-v6.5 item 8) ----
 * The controller's register file lives in RAM and gets fixed power-on defaults at every MCU startup (REGISTERMAP §2:
 * word 4 = 180, 11 = 920, 14 = 4, 17 = 360, 23 = 360, 25 = 550, 112 = 0, 113 = 0, 114 = 50). If the MCU
 * restarts while merakiplus is running, the boot push is gone. The stock UI has the same gap; merakiplus closes it:
 *  (a) THE VERSION QUERY COMES BACK. The MCU sends cmd 0x89 every 20 ms from its startup until it
 *      has received a reply (PROTOCOL §2.2) — and only then (0 of them in 12 min of live traffic, STEAMLIVE §2). A 0x89
 *      arriving more than RP_STRAY_MS after the boot push left WAITING is therefore an MCU startup
 *      AFTER our push. (The first handshake precedes the push; a straggler from it falls within
 *      RP_STRAY_MS.) Once the burst has settled (RP_SETTLE_MS without a new 0x89) and any push in progress
 *      has finished, the boot push is restarted from scratch.
 *  (b) THE DEFAULTS SIGNATURE: word 112 = 0 AND word 113 = 0. These are the MCU's power-on defaults, and they can never
 *      be in-spec config values (word 112 lo = pre-inf. pressure 3..4; word 113 hi = pre-inf. time >= 5)
 *      — so the signature unambiguously says that words 112..114 have not been written since the MCU's startup. It must
 *      hold for RP_SIG_READS fresh reads in a row (poll every 1 s) while the machine is idle and config is
 *      in spec. Catches a restart where the 0x89 burst was lost — and a boot push the guard skipped
 *      while the controller was actually on its defaults (slot B boot with the wand open).
 * The re-push IS the boot push: same blocks (sub1 len30 + sub112 len3), same frame policy, same
 * write queue (ACK wait + one retransmit) and the SAME ACTIVITY GUARD — decided on a work_sta read AFTER
 * the restart (reg_seen[33] is cleared). If an activity is running at that moment it is skipped; if
 * the signature is still there afterwards, (b) tries again when the machine is idle.
 * Rate limit: at least RP_MIN_MS between two re-pushes, and at most RP_BUDGET signature-triggered ones per
 * RP_WINDOW_MS (a controller that does not accept them does not cause an endless stream of writes).
 * Counted on DIAGNOSE (the row is then labelled "RE-PUSH 2 · 1-30" | "112-114") and in the SIM's state JSON. */
#define RP_STRAY_MS  200
#define RP_SETTLE_MS 300
#define RP_MIN_MS    5000
#define RP_SIG_READS 3
#define RP_BUDGET    3
#define RP_WINDOW_MS 600000
enum { RPC_NONE=0, RPC_VER, RPC_DEF };
static struct { unsigned vn, sig_n0; int armed, sig_on, started, last, n_ver, n_def, def_k; ms_t t_start, def_t[RP_BUDGET]; } rps;
static int bp_guard_cfg=1;             /* the guard for a re-push: device 1; SIM follows MERAKIPLUS_SIM_BOOTPUSH_GUARD */
static int repush_count(void){ return rps.n_ver+rps.n_def; }
static int repush_cfg_sane(void){ for(int i=0;i<NSET;i++){ const Setting*q=&SET[i]; if(q->mcu!=M_NONE&&(!q->loaded||set_oor(q))) return 0; } return 1; }
static int mcu_defaults_sig(void){ return reg_seen[112]&&reg_seen[113]&&reg[112]==0&&reg[113]==0; }
static void repush_begin(int cause,ms_t now){
    rps.armed=0; rps.sig_on=0; rps.last=cause; rps.started=1; rps.t_start=now;
    if(cause==RPC_VER) rps.n_ver++; else { rps.n_def++; rps.def_t[rps.def_k++%RP_BUDGET]=now; }
    reg_seen[33]=0;                    /* the guard is decided on a work_sta read AFTER the restart */
    boot_push_start(bp_guard_cfg);
}
static void repush_step(ms_t now){
    if(!bp_on||ro_mode||bp.state==BP_OFF) return;
    if(ver_n!=rps.vn){ rps.vn=ver_n; if(bp.t0_set&&ver_ms-bp.t0>=RP_STRAY_MS) rps.armed=1; }       /* (a) */
    if(bp.state!=BP_DONE){ rps.sig_on=0; return; }                                               /* one push at a time */
    if(rps.started&&now-rps.t_start<RP_MIN_MS){ rps.sig_on=0; return; }
    if(rps.armed){ if(now-ver_ms>=RP_SETTLE_MS) repush_begin(RPC_VER,now); return; }
    int sig=link_up(now)&&mcu_defaults_sig()&&repush_cfg_sane()&&!phase_active(phase_raw(now))&&!wq_busy()&&!sav.active&&act.pending==ACT_NONE;   /* (b) */
    if(!sig){ rps.sig_on=0; return; }
    if(!rps.sig_on){ rps.sig_on=1; rps.sig_n0=rx_n112; return; }
    if(rx_n112-rps.sig_n0<RP_SIG_READS) return;                                                  /* 3 fresh reads, all while the machine is idle */
    if(rps.def_k>=RP_BUDGET&&now-rps.def_t[rps.def_k%RP_BUDGET]<RP_WINDOW_MS) return;              /* the budget is used up */
    repush_begin(RPC_DEF,now);
}

/* ---- the main loop's timers (v6.6) ----
 * Gathered in one place and in 64 bits, so the selftest runs EXACTLY the code main() runs (the wraparound test
 * drives loop_poll / loop_graph / loop_render_due / loop_rendered across 2^31 and 2^32 ms).
 * The deadlines are absolute times in ms_t; with the 32-bit clock up to and including v6.5 a deadline in
 * the clock's last milliseconds could be skipped and only become true again 49.7 days later. */
static struct { ms_t poll, slow, ext, render, graph, ft_reset; unsigned alt; } lt;
/* poll: sub0/sub33 @50 ms alternating + sub56 @200 ms (~10 Hz, like v4) + words 112..114 @1 s */
static void loop_poll(ms_t now){
    if(now>=lt.poll){ lt.poll=now+50; if(lt.alt++&1) frame_query(33,7); else frame_query(0,56); }
    if(now>=lt.slow){ lt.slow=now+200; frame_query(56,56); }
    if(now>=lt.ext){ lt.ext=now+1000; frame_query(112,3); }
}
static void loop_graph(ms_t now){
    if(reg_seen[51]&&now-lt.graph>=100){ lt.graph=now; graph_push(now,reg[51],wsrc_tick(now)); }   /* v6.9: latched weight source */
}
static int loop_render_due(ms_t now){ if(ui_kick){ ui_kick=0; lt.render=now; } return now>=lt.render; }
static void loop_rendered(ms_t now,float ft){
    ft_last=ft; if(now-lt.ft_reset>5000){ lt.ft_reset=now; ft_max=0; } if(ft_last>ft_max) ft_max=ft_last;
    lt.render=now+((ui_anim||tch.down||tr.type)?33:100);
}

/* ====================================================================== */
/* ================= main: device / SIM / preview ======================= */
/* ====================================================================== */
/* ---- v6.21 OTA (docs/ota-plan.md §5.4/5.5): the healthy marker, the one-time notices, the exec-reload ---- */
static void ota_step(ms_t now){
    if(!ota.marked){                                /* healthy: >= 60 s running AND the controller answering for >= 30 s in a row */
        if(link_up(now)){ if(ota.link0<0) ota.link0=now; } else ota.link0=-1;
        if(now-ota.t0>=HEALTHY_RUN_MS&&ota.link0>=0&&now-ota.link0>=HEALTHY_LINK_MS){ ota.marked=1; ota_mark_write(0); static_dirty=1; }
    }
    if(!ota.toasted&&view==V_MAIN&&now-ota.t0>=4000){  /* once per start, after the splash */
        ota.toasted=1;
        if(ota.vendor) toast_show(TR(TX_T_UPD_VENDOR),C_WARN,now);
        else if(ota.note[0]){ char a[24],b[24],m[96]; ota_ver(ota.note,a,sizeof a); snprintf(b,sizeof b,"%s",MERAKIPLUS_VERSION);
            snprintf(m,sizeof m,TR(TX_T_UPD_ROLLED),a,b); toast_show(m,C_WARN,now); }
    }
}
/* idle = nothing running or about to be sent, no dialog, HOME (or the OFF screen), no sound playing */
static int ota_idle(ms_t now){
    return !switch_guard(now)&&((view==V_MAIN&&page==PG_HOME&&sel<0)||view==V_OFF)&&!tr.type&&!tch.down&&snd.pid<=0;
}
#ifdef DEVICE_RT
/* exec-reload (§5.5): the installer put a new release in place and wrote /tmp/merakiplus_reload. When idle, this process
 * becomes the launcher again — execv() keeps the PID, so the vendor watchdog never sees a gap, and no signal is sent.
 * Every descriptor is close-on-exec, so the UART, the frame buffer and the touch grab are released by the exec itself. */
static void ota_reload_step(ms_t now){
    if(now-ota.t_reload<2000) return; ota.t_reload=now;
    if(access(OTA_RELOAD,F_OK)!=0) return;
    boot_refresh();
    if(bs.self_slot!=SLOT_B||bs.n_stock>0||!ota_file_has(SLOT_B_EXE,OTA_LMARK)){
        unlink(OTA_RELOAD); boot_log("RELOAD ignored: %s",bs.n_stock>0?"a stock UI is paused (run.sh mode)":(bs.self_slot!=SLOT_B?"not started from slot B":"no launcher in slot B")); return; }
    if(!ota_idle(now)) return;                      /* busy: ask again in 2 s (never during an activity) */
    unlink(OTA_RELOAD);
    boot_log("RELOAD: exec %s (installer request, machine idle) — same PID %d",SLOT_B_EXE,(int)getpid());
    ota_mark_write(1);
    if(bl_is_off) bl_power(1);
    fflush(stdout); fflush(stderr);
    for(int fd=3;fd<1024;fd++){ int fl=fcntl(fd,F_GETFD); if(fl>=0) fcntl(fd,F_SETFD,fl|FD_CLOEXEC); }
    setenv("MERAKIPLUS_LAUNCH","reload",1);
    if(chdir(SLOT_B_MNT)!=0){}
    char*const av[]={"./demo_v9",NULL};
    execv(SLOT_B_EXE,av);
    boot_log("RELOAD FAILED: %s — this build keeps running",strerror(errno));
    unsetenv("MERAKIPLUS_LAUNCH");
}
#endif
static void onsig(int s){ (void)s; SIM_SIG(s) run=0; }

#ifndef MERAKIPLUS_PREVIEW
/* ---- touch: raw events -> DOWN/MOVE/UP per SYN_REPORT ---- */
static int ifd=-1;
static int absx_min=0,absx_max=4095,absy_min=0,absy_max=4095;
static int in_x=-1,in_y=-1,in_down=0,in_pdown=0,in_pup=0,in_moved=0;
#ifdef DEVICE_RT
static void inp_open(void){
    ifd=open(INP,O_RDONLY|O_NONBLOCK);
    if(ifd<0){ ifd=-1; return; }
    struct input_absinfo ai;
    if(ioctl(ifd,EVIOCGABS(ABS_X),&ai)==0){ absx_min=ai.minimum; absx_max=ai.maximum; }
    if(ioctl(ifd,EVIOCGABS(ABS_Y),&ai)==0){ absy_min=ai.minimum; absy_max=ai.maximum; }
    if(ioctl(ifd,EVIOCGABS(ABS_MT_POSITION_X),&ai)==0){ absx_min=ai.minimum; absx_max=ai.maximum; }
    if(ioctl(ifd,EVIOCGABS(ABS_MT_POSITION_Y),&ai)==0){ absy_min=ai.minimum; absy_max=ai.maximum; }
    /* EVIOCGRAB: exclusive input while merakiplus runs, so the SIGSTOPped demo_v9 does not
     * get our touches in its evdev queue and replay them after SIGCONT (phantom touches in
     * the stock UI). Released automatically on close/exit. MERAKIPLUS_NOGRAB=1 disables it
     * (needed for tinj injection from another process). */
    if(!getenv("MERAKIPLUS_NOGRAB")){ if(ioctl(ifd,EVIOCGRAB,1)!=0) perror("EVIOCGRAB"); }
}
/* v6.14: THE POWER BUTTON — the k62091 node, read-only and WITHOUT EVIOCGRAB (stock does not grab it either; app_powerKey_thread
 * 0x140AB4 opens /dev/input/event1). Selection: MERAKIPLUS_KBD=<path> -> else the event node (not the touch node) whose name contains
 * "k62091" -> else the first one with EV_KEY and without EV_ABS -> else /dev/input/event1, if it can be opened. */
static int kfd=-1;
static int kbd_try(const char*path,int need_name,int*fdo,char*name,size_t nn){
    int fd=open(path,O_RDONLY|O_NONBLOCK); if(fd<0) return 0;
    name[0]=0; if(ioctl(fd,EVIOCGNAME((int)nn-1),name)<0) name[0]=0; name[nn-1]=0;
    unsigned long evb=0; if(ioctl(fd,EVIOCGBIT(0,sizeof evb),&evb)<0) evb=0;
    int ok= need_name==1 ? strstr(name,KBD_NAME)!=NULL : (need_name==2 ? ((evb&(1UL<<EV_KEY))&&!(evb&(1UL<<EV_ABS))) : 1);
    if(!ok){ close(fd); return 0; }
    *fdo=fd; return 1;
}
static void kbd_open(void){
    char path[48], name[40]; const char*e=getenv("MERAKIPLUS_KBD"); pw.kbd_ok=0; kfd=-1;
    if(e){ snprintf(path,sizeof path,"%s",e); if(!kbd_try(path,0,&kfd,name,sizeof name)) kfd=-1; }
    for(int pass=1;kfd<0&&!e&&pass<=2;pass++)
        for(int i=0;i<10&&kfd<0;i++){ snprintf(path,sizeof path,"/dev/input/event%d",i); if(!strcmp(path,INP)) continue; kbd_try(path,pass,&kfd,name,sizeof name); }
    if(kfd<0&&!e){ snprintf(path,sizeof path,"%s",INP_KBD); kbd_try(path,0,&kfd,name,sizeof name); }
    if(kfd<0){ pw.node[0]=0; return; }
    pw.kbd_ok=1; snprintf(pw.node,sizeof pw.node,"%s · %s",path,name[0]?name:"?");
}
static void kbd_poll(ms_t now){
    if(kfd<0) return;
    struct input_event ev;
    while(read(kfd,&ev,sizeof ev)==(ssize_t)sizeof ev) if(ev.type==EV_KEY) pw_key(ev.code,ev.value,now);
}
#endif
static int scalex(int v){ return (int)((long)(v-absx_min)*W/(absx_max-absx_min+1)); }
static int scaley(int v){ return (int)((long)(v-absy_min)*H/(absy_max-absy_min+1)); }
#ifdef SIM_RT
/* ====================================================================== */
/* ================= SIM-HAL (v6.4 · sim/CONTRACT.md) =================== */
/* ====================================================================== */
/* Everything from here to #endif exists ONLY in the SIM build. It replaces the device's three file nodes
 * (fb0, ttyS1, event0) and adds a test surface on top — the UI, protocol, save and drawing code
 * is the same as on the device:
 *   MERAKIPLUS_SIM_TTY    pty slave instead of /dev/ttyS1              (uart_open: UART_DEV)
 *   MERAKIPLUS_SIM_FB     fb buffer -> file (tmp+rename, <= 10 Hz + on change)
 *   MERAKIPLUS_SIM_TOUCH  FIFO w/ raw 16-byte evdev records; played back according to the records' timestamps
 *   MERAKIPLUS_SIM_STATE  state JSONL (one line per rendered frame whose content changed; at least ~10 Hz)
 *   MERAKIPLUS_SIM_CTRL   unix socket w/ JSONL hooks (ping, fb_dump, exit, action, save, page, touch,
 *                    boot_flag, runtime_switch, raw_write, config_reload, state_now, layout)
 *   MERAKIPLUS_SIM_ROOT   sandbox root for /customer /new_customer /proc /tmp (required)
 * The hooks call the SAME functions a tap would (act_request, save_begin, sys_confirm …);
 * they bypass neither guards nor frame policy. The only exception is raw_write (stock emulation),
 * see sim_raw_push(). Nothing is written outside the sandbox besides the paths the runner itself designates. */
static char sim_fb_path[300]="", sim_state_path[300]="", sim_touch_path[300]="", sim_ctrl_path[300]="";
static FILE*sim_sf=NULL;
static ms_t sim_t0=0;
static int sim_tfd_w=-1, sim_lfd=-1, sim_bp_guard=0;

/* the runner does not read stderr continuously (pipe): never a stream of error lines */
static void sim_perr(const char*s){ static int n; if(n<3){ n++; perror(s); } }
static void sim_die(int rc,const char*fmt,...){
    va_list ap; va_start(ap,fmt); fputs("merakiplus-sim: ",stderr); vfprintf(stderr,fmt,ap); va_end(ap); fputc('\n',stderr); exit(rc);
}

/* ---- sandbox ---- */
static void sim_mkparents(const char*path){
    char b[460]; snprintf(b,sizeof b,"%s",path);
    for(char*q=b+1;*q;q++) if(*q=='/'){ *q=0; mkdir(b,0755); *q='/'; }
}
static void sim_put(const char*rel,const char*data,int mode){   /* creates ONLY if the path is missing */
    char p[460]; snprintf(p,sizeof p,"%s%s",sysroot,rel); struct stat st; if(lstat(p,&st)==0) return;
    sim_mkparents(p); int fd=open(p,O_WRONLY|O_CREAT|O_EXCL,mode); if(fd<0) return;
    int n=(int)strlen(data); if(n&&write(fd,data,n)!=n){} close(fd); chmod(p,mode);
}
/* The runner's sandbox (contract §8) has /customer, /new_customer/merakiplus/boot and three /proc files.
 * The BOOT UI guard and SWITCH NOW read more than that: /proc/mounts (is slot B mounted?), slot B's
 * demo_v9 (is it merakiplus?), slot A's demo_v9 (is there a stock UI?) and /proc/self/exe. Without them
 * both would be LOCKED. Here whatever is missing is filled in — a slot B boot, like the preview's
 * mock_boot_b(). Nothing existing is overwritten; MERAKIPLUS_SIM_NOPROVISION=1 skips it. */
static void sim_provision(void){
    char mk[200], p[460]; struct stat st;
    sim_put("/proc/mounts","rootfs / rootfs rw 0 0\nmock-a /customer mockfs rw 0 0\nmock-b " SLOT_B_MNT " mockfs rw 0 0\n",0644);
    sim_put("/proc/sys/kernel/random/boot_id","6f757275-6973-4000-8000-73696d626f6f\n",0644);
    snprintf(mk,sizeof mk,"#!/bin/sh\n# SIM mock of slot B's demo_v9: %s\nexit 0\n",MERAKIPLUS_MARK); sim_put(SLOT_B_EXE,mk,0755);
    sim_put(SLOT_A_EXE,"#!/bin/sh\n# SIM mock of the stock UI (slot A)\nexec sleep 3\n",0755);
    snprintf(p,sizeof p,"%s/proc/self/exe",sysroot);
    if(lstat(p,&st)!=0){ sim_mkparents(p); if(symlink(SLOT_B_EXE,p)!=0){} }
}
static void sim_env_path(const char*name,char*dst,size_t n,int required){
    const char*e=getenv(name);
    if(!e||!*e){ if(required) sim_die(2,"%s missing (required in the SIM build, see sim/CONTRACT.md §2)",name); return; }
    if(strlen(e)>=n) sim_die(2,"%s is too long (%d chars, max %d)",name,(int)strlen(e),(int)n-1);
    snprintf(dst,n,"%s",e);
}
/* v6.16: "YYYY-MM-DDTHH:MM:SSZ" (UTC) -> seconds; -1 = malformed */
static int64_t sim_parse_utc(const char*e){
    int Y,M,D,h,m,sec; if(sscanf(e,"%d-%d-%dT%d:%d:%d",&Y,&M,&D,&h,&m,&sec)!=6||M<1||M>12||D<1||D>31||h<0||h>23||m<0||m>59||sec<0||sec>59) return -1;
    return days_civil(Y,M,D)*86400+h*3600LL+m*60LL+sec;
}
static void sim_env(void){
    const char*e; struct stat st;
    sim_t0=now_ms();
    signal(SIGPIPE,SIG_IGN);
    if((e=getenv("MERAKIPLUS_SIM_ROOT"))&&*e){ if(strlen(e)>=sizeof sysroot) sim_die(2,"MERAKIPLUS_SIM_ROOT is too long"); snprintf(sysroot,sizeof sysroot,"%s",e); }
    else if((e=getenv("MERAKIPLUS_ROOT"))&&strlen(e)>=sizeof sysroot) sim_die(2,"MERAKIPLUS_ROOT is too long");
    size_t L=strlen(sysroot); while(L>1&&sysroot[L-1]=='/') sysroot[--L]=0;
    if(!sysroot[0]||!strcmp(sysroot,"/")) sim_die(2,"MERAKIPLUS_SIM_ROOT missing: the SIM build must never touch the host's /customer, /new_customer, /proc or /tmp");
    if(stat(sysroot,&st)!=0||!S_ISDIR(st.st_mode)) sim_die(2,"sandbox root does not exist: %s",sysroot);
    /* paths without their own env follow the root (on the device the language file does NOT follow MERAKIPLUS_ROOT — unchanged) */
    if((e=getenv("MERAKIPLUS_CONFIG"))){ if(strlen(e)>=sizeof cfg_path) sim_die(2,"MERAKIPLUS_CONFIG is too long"); } else snprintf(cfg_path,sizeof cfg_path,"%s/customer/config.json",sysroot);
    if((e=getenv("MERAKIPLUS_BAKDIR"))){ if(strlen(e)>=sizeof cfg_bakdir) sim_die(2,"MERAKIPLUS_BAKDIR is too long"); } else snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s/new_customer/merakiplus/cfgbak",sysroot);
    if((e=getenv("MERAKIPLUS_LANGFILE"))){ if(strlen(e)>=sizeof lang_path) sim_die(2,"MERAKIPLUS_LANGFILE is too long"); } else snprintf(lang_path,sizeof lang_path,"%s/new_customer/merakiplus/lang",sysroot);
    if((e=getenv("MERAKIPLUS_SCHEDFILE"))){ if(strlen(e)>=sizeof sched_path) sim_die(2,"MERAKIPLUS_SCHEDFILE is too long"); snprintf(sched_path,sizeof sched_path,"%s",e); }
    else snprintf(sched_path,sizeof sched_path,"%s/new_customer/merakiplus/schedule",sysroot);   /* v6.16 */
    if((e=getenv("MERAKIPLUS_SOUNDFILE"))){ if(strlen(e)>=sizeof snd_path) sim_die(2,"MERAKIPLUS_SOUNDFILE is too long"); snprintf(snd_path,sizeof snd_path,"%s",e); }
    else snprintf(snd_path,sizeof snd_path,"%s/new_customer/merakiplus/sound",sysroot);         /* v6.18 */
    if((e=getenv("MERAKIPLUS_SIM_CLOCK"))&&*e){ int64_t u=sim_parse_utc(e); if(u<0) sim_die(2,"MERAKIPLUS_SIM_CLOCK: expected YYYY-MM-DDTHH:MM:SSZ"); clk_off=u-(int64_t)time(NULL); }   /* v6.16: the wall clock (UTC) */
    sim_env_path("MERAKIPLUS_SIM_TTY",sim_tty,sizeof sim_tty,1);
    sim_env_path("MERAKIPLUS_SIM_FB",sim_fb_path,sizeof sim_fb_path-8,1);
    sim_env_path("MERAKIPLUS_SIM_STATE",sim_state_path,sizeof sim_state_path,1);
    sim_env_path("MERAKIPLUS_SIM_TOUCH",sim_touch_path,sizeof sim_touch_path,0);
    sim_env_path("MERAKIPLUS_SIM_CTRL",sim_ctrl_path,sizeof sim_ctrl_path,0);
    bl_enabled=0;                                          /* the backlight sysfs is never written in SIM */
    if(getenv("MERAKIPLUS_SIM_NOBOOTPUSH")) bp_on=0;
    if(getenv("MERAKIPLUS_SIM_BOOTPUSH_GUARD")) sim_bp_guard=1;   /* v6.5: the device's activity guard (skip instead of defer) */
    bp_guard_cfg=sim_bp_guard;                             /* v6.6: a re-push uses the same guard choice as the boot push */
    if((e=getenv("MERAKIPLUS_SIM_UPTIME_MS"))&&*e){ sim_clk_off=0; sim_clk_off=strtoll(e,NULL,0)-(int64_t)now_ms(); sim_t0=now_ms(); }   /* v6.6: the clock starts at a chosen value (wraparound test) */
    if(!getenv("MERAKIPLUS_SIM_NOPROVISION")) sim_provision();
    /* v6.7: the wifi MODEL. Default = healthy stack (like runtime B: stock's wpa_supplicant is alive); "cold" = slot B cold start
     * (no wlan0, no supplicant, no IP); "off" = the watchdog disabled. The log lives in the sandbox's /tmp. */
    { e=getenv("MERAKIPLUS_SIM_NET"); nm_reset(e&&!strcmp(e,"cold")); sim_net_on=!(e&&!strcmp(e,"off")); mkdir(rp("/tmp"),0755); mkdir(rp("/appconfigs"),0755); }
}

/* ---- touch: FIFO w/ raw evdev records -> timed queue -> inp_poll() (same decoding as the device) ----
 * The runner writes a whole gesture at once; the duration lies in the records' timestamps
 * (contract §4). The queue therefore delivers each record once its timestamp offset from the gesture's
 * first record has elapsed on the wall clock — so hold-to-start (650 ms) is measured as on the device. */
#define SIM_EVQ 4096
static struct { struct input_event ev; ms_t due; } sim_evq[SIM_EVQ];
static int sim_evh=0, sim_evn=0;
static uint8_t sim_tb[16]; static int sim_tbn=0;
static ms_t sim_ev_bw=0, sim_ev_be=0;                      /* anchor: wall time / record time of the gesture's first record */
static ms_t sim_ev_last(void){ return sim_evn? sim_evq[(sim_evh+sim_evn-1)%SIM_EVQ].due : 0; }
static void sim_ev_push(uint16_t type,uint16_t code,int32_t value,ms_t due){
    if(sim_evn>=SIM_EVQ) return;
    struct input_event ev={0,0,type,code,value}; int k=(sim_evh+sim_evn)%SIM_EVQ; sim_evq[k].ev=ev; sim_evq[k].due=due; sim_evn++;
}
static void sim_touch_read(ms_t now){
    if(ifd<0||!sim_touch_path[0]) return;
    uint8_t b[2048]; int n;
    while((n=(int)read(ifd,b,sizeof b))>0){
        for(int i=0;i<n;i++){
            sim_tb[sim_tbn++]=b[i]; if(sim_tbn<16) continue;
            sim_tbn=0; struct input_event ev; memcpy(&ev,sim_tb,16);
            ms_t ems=(ms_t)ev.sec*1000+ev.usec/1000, due=sim_ev_bw+(ems-sim_ev_be);
            if(sim_evn==0||ems<sim_ev_be||due<now-50||due>now+120000){ sim_ev_bw=now; sim_ev_be=ems; due=now; }   /* new gesture: new anchor */
            if(due<sim_ev_last()) due=sim_ev_last();
            sim_ev_push(ev.type,ev.code,ev.value,due);
        }
    }
}
static int sim_ev_next(struct input_event*ev,ms_t now){
    sim_touch_read(now);
    if(!sim_evn||sim_evq[sim_evh].due>now) return 0;
    *ev=sim_evq[sim_evh].ev; sim_evh=(sim_evh+1)%SIM_EVQ; sim_evn--; return 1;
}
/* ctrl "touch": same record sequence as tinj_hold, put in the same queue (down now, up after ms) */
static void sim_touch_inject(int x,int y,int ms,ms_t now){
    int rx=clampi((x*4096+479)/480,0,4095), ry=clampi((y*4096+479)/480,0,4095);   /* rounded up: scalex() returns exactly x */
    ms_t due=now>sim_ev_last()?now:sim_ev_last()+1; if(ms<1) ms=1;
    sim_ev_push(EV_ABS,ABS_MT_TRACKING_ID,1,due); sim_ev_push(EV_ABS,ABS_MT_POSITION_X,rx,due); sim_ev_push(EV_ABS,ABS_MT_POSITION_Y,ry,due);
    sim_ev_push(EV_ABS,ABS_X,rx,due); sim_ev_push(EV_ABS,ABS_Y,ry,due); sim_ev_push(EV_KEY,BTN_TOUCH,1,due); sim_ev_push(EV_SYN,SYN_REPORT,0,due);
    sim_ev_push(EV_ABS,ABS_MT_TRACKING_ID,-1,due+ms); sim_ev_push(EV_KEY,BTN_TOUCH,0,due+ms); sim_ev_push(EV_SYN,SYN_REPORT,0,due+ms);
}
/* no EVIOCGRAB and no EVIOCGABS in SIM: the raw range is 0..4095 (screen = raw*480/4096) */
static void inp_open(void){
    struct stat st;
    if(!sim_touch_path[0]){ ifd=open("/dev/null",O_RDONLY); return; }      /* no FIFO: only ctrl "touch" */
    if(stat(sim_touch_path,&st)!=0&&mkfifo(sim_touch_path,0600)!=0) sim_die(4,"mkfifo %s: %s",sim_touch_path,strerror(errno));
    ifd=open(sim_touch_path,O_RDONLY|O_NONBLOCK);
    if(ifd<0) sim_die(4,"open touch %s: %s",sim_touch_path,strerror(errno));
    sim_tfd_w=open(sim_touch_path,O_WRONLY|O_NONBLOCK);    /* own write end: no EOF when the runner closes between injections */
}
static void dev_release_impl(void){                        /* BOOT.md §9.2 step 1 in SIM: UART + touch are released (fb is memory) */
    uart_flush();
    if(ufd>=0){ tcdrain(ufd); close(ufd); ufd=-1; }
    if(ifd>=0){ close(ifd); ifd=-1; }
    if(sim_tfd_w>=0){ close(sim_tfd_w); sim_tfd_w=-1; }
}
static void dev_reacquire_impl(void){
    uart_open(); inp_open();
    RL full; full.n=1; full.r[0]=FULL; pres_prev=full;
}

/* ---- JSON out ---- */
typedef struct { char*p,*e; } JB;
static void jb(JB*j,const char*fmt,...){
    if(j->p>=j->e) return;
    va_list ap; va_start(ap,fmt); int n=vsnprintf(j->p,(size_t)(j->e-j->p),fmt,ap); va_end(ap);
    if(n>0) j->p+=(n<j->e-j->p)?n:(j->e-j->p);
}
/* JSON string of a UI text: font switches (LC/UC) are removed, icon codes are written as \u00XX */
static void jb_s(JB*j,const char*s){
    jb(j,"\"");
    for(;s&&*s;s++){ unsigned char c=(unsigned char)*s;
        if(c==0x0e||c==0x0f) continue;
        if(c=='"'||c=='\\') jb(j,"\\%c",c); else if(c<0x20) jb(j,"\\u%04x",c); else if(j->p<j->e) *j->p++=(char)c; }
    jb(j,"\"");
}

/* ---- state that exists only for the state JSON ---- */
static const char*const PH_KEY[NPHASE]={"OFFLINE","SLEEP","ERROR","HEAT","READY","GRIND","PRE","EXT","WATER","STEAM","DONE"};
static const char*const ACT_KEY[4]={"grind","brew","water","steam"};
static const char*const STOP_KEY[4]={"grind_stop","brew_stop","water_stop","steam_stop"};   /* v6.13: ctrl action + layout names */
/* parity_spec id -> SET[] row (sim_e2e.py checks the table against parity_spec.json + --dump-table) */
static const struct { const char*id; int idx; } SIM_PARAM[]={
 {"grind_weight",S_GRINDW},{"grind_mode",S_GRINDMODE},{"brew_weight",S_BREWW},{"brew_temp",S_BREWT},
 {"preinfusion_switch",S_PISW},{"preinfusion_time",S_PITIME},{"preinfusion_pressure",S_PIPRESS},{"break_time",S_BREAK},
 {"head_temp",S_HEADT},{"steam_milk_target",S_STEAMT},{"steam_gear",S_STEAMG},{"hotwater_weight",S_HOTW},
 {"hotwater_mode",S_HOTMODE},{"hotwater_temp",S_HOTT},{"ui_language",S_UILANG},{"backlight",S_BL},{"volume",S_VOL},
 {"temp_unit",S_TUNIT},{"language",S_LANG},{"hotwater_shortcut",S_HOTSC},{"sumtime",S_DST},{"cleartip",S_CLEARTIP},
 {"timemode",S_TIMEMODE},{"automatically",S_AUTOTIME},{"timezone",S_TZ},
 {"preheat",S_PREHEAT},{"preheat_at",S_PHAT},{"preheat_repeat",S_PHREP},{"auto_off",S_AUTOOFF},   /* v6.16: merakiplus's own (schedule file) */
 {"sound",S_SOUND},{"ready_tone",S_SNDRDY}};                                                         /* v6.18: merakiplus's own (sound file) */
#define SIM_NPARAM ((int)(sizeof SIM_PARAM/sizeof SIM_PARAM[0]))
static int sim_param_idx(const char*id){
    for(int k=0;k<SIM_NPARAM;k++) if(!strcmp(SIM_PARAM[k].id,id)) return SIM_PARAM[k].idx;
    for(int i=0;i<NSET;i++) if(SET[i].key&&!strcmp(SET[i].key,id)) return i;               /* the config key works too */
    return -1;
}
static const char* sim_param_id(int idx){ for(int k=0;k<SIM_NPARAM;k++) if(SIM_PARAM[k].idx==idx) return SIM_PARAM[k].id; return "?"; }

/* toast: which string id was shown? (TR() returns the table's own pointer) */
static struct { int id; uint32_t col; } sim_tst={-1,0};
static void sim_toast(const char*m,uint32_t col){
    sim_tst.id=-1; sim_tst.col=col;
    for(int l=0;l<NLANG&&sim_tst.id<0;l++){ int lg=(ui_lang+l)%NLANG; for(int i=1;i<NSTR;i++) if(STR[lg][i]==m){ sim_tst.id=i; break; } }
}
/* latest SAVE (hook or tap): pending -> ok/fail; rejected/unchanged is set by the hook */
enum { SSV_NONE=0, SSV_PENDING, SSV_OK, SSV_FAIL, SSV_REJECTED, SSV_UNCHANGED };
static const char*const SSV_KEY[]={"none","pending","ok","fail","rejected","unchanged"};
static struct { int state, idx, val; char param[40]; } ssave;
static void sim_save_track(void){
    if(sav.active&&!(ssave.state==SSV_PENDING&&ssave.idx==sav.idx&&ssave.val==sav.val)){
        ssave.state=SSV_PENDING; ssave.idx=sav.idx; ssave.val=sav.val; snprintf(ssave.param,sizeof ssave.param,"%s",sim_param_id(sav.idx));
    }
    if(ssave.state==SSV_PENDING&&!sav.active){
        ssave.state=(SET[ssave.idx].val==ssave.val)?SSV_OK:SSV_FAIL;   /* since v6.5 the boot-push retry after a SAVE lives in save_step() (all build types) */
    }
}

/* ---- fb -> file (tmp + rename: the file is always one whole frame) ---- */
static long sim_fb_dumped=-1; static ms_t sim_fb_last=-100000;
static int sim_fb_write(const char*path){
    char tmp[460]; snprintf(tmp,sizeof tmp,"%s.tmp",path);
    int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd<0) return 0;
    const char*d=(const char*)fbmem; long off=0, tot=(long)W*H*4;
    while(off<tot){ long k=write(fd,d+off,(size_t)(tot-off)); if(k<=0){ close(fd); unlink(tmp); return 0; } off+=k; }
    close(fd);
    if(rename(tmp,path)!=0){ unlink(tmp); return 0; }
    return 1;
}

/* ---- state JSONL (contract §3 + addendum, see sim/CONTRACT.md "ADDENDUM v6.4") ---- */
static long sim_frames=0; static ms_t sim_state_last=-100000; static double sim_ft_sum=0; static float sim_ft_win=0, sim_ft_peak=0;
static uint32_t sim_sig=0;
static int sim_bye_on=0; static ms_t sim_bye_until=0;
static const char* sim_page_key(void){
    if(view==V_BYE) return "bye"; if(view==V_DETAIL) return "detail"; if(view==V_SYS) return "sys"; if(view==V_OFF) return "off";
    if(view==V_SPLASH||page==PG_HOME) return "home";
    return page==PG_DIAG?"diag":"settings";
}
static const char* sim_bpr(int r){ static const char*const k[]={"none","ok","noack","blocked","nocfg","skipped"}; return k[r]; }
static void sim_state_emit(ms_t now,int force){   /* force=0: only if the line's content changed, or 90 ms have passed */
    if(!sim_sf) return;
    static char buf[12288]; JB j={buf,buf+sizeof buf-4};
    static const char*const VW[]={"splash","main","detail","sys","bye","off"};
    static const char*const TAB[]={"home","grind","brew","water","steam","system","diag"};   /* v6.17: one page per feature + SYSTEM (v6.4-v6.16: home/brew/steam/system/diag) */
    static const short SHOWN[]={0,1,2,3,4,6,9,11,14,16,17,19,22,23,24,25,33,34,35,36,37,38,39,40,41,42,44,46,47,48,50,51,52,58,62,63,112,113,114};   /* v6.6: + 44, 46 (the wand switch); v6.18: + 62, 63 (heat stages) */
    int pr=phase_raw(now); struct stat st; char rn[340], stamp[160]="", a[40], b[40];
    static uint32_t last_h; jb(&j,"\"client\":\"merakiplus-sim\",\"version\":\"" MERAKIPLUS_VERSION "\"");
    jb(&j,",\"view\":\"%s\",\"view_raw\":\"%s\",\"page\":\"%s\",\"tab\":\"%s\"",view==V_BYE?"bye":"main",VW[view],sim_page_key(),TAB[page]);
    jb(&j,",\"phase\":\"%s\",\"phase_ui\":\"%s\",\"work_sta\":%u,\"link_up\":%s",PH_KEY[pr],PH_KEY[act.ph],reg[33],link_up(now)?"true":"false");
    /* v6.6: phase = the contract's phase_raw() (steam first, unchanged); phase_main = HOME's main activity; steam = the steam path */
    { static const char*const SKK[]={"none","unknown","auto","wand"}; int on=link_up(now)&&steam_bits(), pm=phase_main(now);
      jb(&j,",\"phase_main\":\"%s\",\"phases\":[",PH_KEY[pm]);
      if(phase_active(pm)&&pm!=PH_STEAM) jb(&j,"\"%s\"%s",PH_KEY[pm],on?",":""); if(on) jb(&j,"\"STEAM\""); if(!phase_active(pm)) jb(&j,"%s\"%s\"",on?",":"",PH_KEY[pm]);
      jb(&j,"],\"steam\":{\"on\":%s,\"kind\":\"%s\",\"last_kind\":\"%s\",\"concurrent\":%s,\"wand_sw\":%s,\"wand_seen\":%s,\"ord24\":%u,\"target\":%d,\"milk\":%u,\"stop_btn\":%s}",
         on?"true":"false",SKK[stm.kind],SKK[stm.last_kind],(on&&phase_active(pm)&&pm!=PH_STEAM)?"true":"false",wand_sw()?"true":"false",stm.wand?"true":"false",reg[24],steam_target(),reg[48],steam_stop_visible()?"true":"false"); }
    { int sk[2], sn=link_up(now)?stop_btns(now,sk):0;               /* v6.13: the STOP buttons (left -> right) + latest stop request */
      jb(&j,",\"stop\":{\"btns\":["); for(int i=0;i<sn;i++) jb(&j,"%s\"%s\"",i?",":"",ACT_KEY[sk[i]]);
      jb(&j,"],\"steam_hold\":%s",(sn==1&&steam_hold_shown(now))?(act_guard(ACT_STEAM,now)?"\"dim\"":"\"on\""):"null");   /* v6.20 */
      jb(&j,",\"pending\":"); if(stp.active) jb(&j,"\"%s\"",ACT_KEY[stp.kind]); else jb(&j,"null");
      jb(&j,",\"last\":"); if(stp.n_req) jb(&j,"\"%s\"",ACT_KEY[stp.kind]); else jb(&j,"null");
      jb(&j,",\"req\":%ld,\"ok\":%ld,\"fail\":%ld}",stp.n_req,stp.n_ok,stp.n_fail); }
    /* v6.22 (addendum): the water dosing gate — controller firmware string (words 80..87), its state, dosing, scale fault, the start kind */
    { static const char*const MVK[]={"none","ok","bad","copy"}; char vb[20]; int vv[3], vs=mcu_ver_read(vb,sizeof vb,vv); Row wr[ROW_MAX];
      jb(&j,",\"water\":{\"mcu_ver\":"); jb_s(&j,vb);
      jb(&j,",\"ver_state\":\"%s\",\"ver_dosing\":%s,\"dosing\":%s,\"scale_fault\":%s,\"start\":\"%s\",\"run_manual\":%s,\"rows\":%d,\"fault_n\":%ld,\"fault_stops\":%ld}",
         MVK[vs],wd_ver_dosing()?"true":"false",wd_dosing()?"true":"false",wd_scale_fault()?"true":"false",water_manual()?"manual":"auto",wd.man?"true":"false",rows_get(PG_WATER,wr),wd.n_fault,wd.n_stop);
      { char dv[28],dw[28]; uint32_t c1,c2; diag_mcu_text(dv,&c1,dw,&c2); j.p--; jb(&j,",\"diag_ver\":"); jb_s(&j,dv);   /* DIAGNOSTICS › CONTROLLER as drawn */
        jb(&j,",\"diag_wd\":\"%s\"}",wd_dosing()?"TX_DV_WD_AUTO":(wd_ver_dosing()?"TX_DV_WD_FAULT":"TX_DV_WD_MAN")); } }
    jb(&j,",\"regs\":{"); for(unsigned k=0;k<sizeof SHOWN/sizeof SHOWN[0];k++) jb(&j,"%s\"%d\":%u",k?",":"",SHOWN[k],reg[SHOWN[k]]);
    jb(&j,"},\"regs_s\":{\"38\":%d,\"39\":%d}",s16(38),s16(39));
    /* v6.18 (addendum): the sound engine — events by name with their outcome; log = newest first "event:outcome" */
    { static const char*const MK[]={"off","on","stock"};
      jb(&j,",\"audio\":{\"mode\":\"%s\",\"ready_mask\":%d,\"vol\":%d,\"machine\":%d,\"armed\":[%d,%d],\"ready\":[%d,%d],\"cold_boot\":%s",
         MK[clampi(SET[S_SOUND].val,0,2)],SET[S_SNDRDY].val,snd_vol(),snd.st,snd.armed[0],snd.armed[1],snd.ready[0],snd.ready[1],snd.cold_boot?"true":"false");
      for(int e=0;e<NSE;e++) jb(&j,",\"n_%s\":%d,\"played_%s\":%d",SE_KEY[e],snd.n[e],SE_KEY[e],snd.n_played[e]);
      jb(&j,",\"n_suppressed\":%d,\"n_muted\":%d,\"n_off\":%d,\"log\":\"",snd.n_out[SO_SUPPRESSED],snd.n_out[SO_MUTED],snd.n_out[SO_OFF]);
      for(int k=snd.log_n-1;k>=0;k--) jb(&j,"%s%s:%s",k==snd.log_n-1?"":",",SE_KEY[(int)snd.log[k].ev],SO_KEY[(int)snd.log[k].out]);
      jb(&j,"\",\"file\":"); jb_s(&j,snd.last_file); jb(&j,",\"last\":"); if(snd.log_n) jb(&j,"\"%s\"",SE_KEY[(int)snd.log[0].ev]); else jb(&j,"null"); jb(&j,"}"); }
    /* v6.9 (addendum): display conditioning + latest chart scale (display only — regs above are raw) */
    jb(&j,",\"cond\":{\"raw\":%s,\"a_chart\":%.3f,\"a_hero\":%.3f,\"med\":%d,\"scale\":\"%s\",\"pct\":%d,\"hold\":%d,\"head\":%.2f,\"hero_snaps\":%d,\"wdb\":%.1f,\"gate\":%d}",
       cd.raw?"true":"false",cd.a_chart,cd.a_hero,cd.med,CS_KEY[cd.scale],cd.pct,cd.hold,cd.head,hf.n_snap,(double)CD_WDB,cd.gate);
    /* v6.9: the weight source (latch) and the displayed weight (0.1 g, signed, conditioned + deadband) — regs/regs_s are raw */
    jb(&j,",\"weight\":{\"src\":%d,\"switches\":%d,\"shown\":%d}",wsl.src,wsl.n_sw,w_disp(wsl.src,now));
    { float pb=p_bar((float)reg[51]);                       /* v6.10: the pressure estimate (display only; regs are raw) — bar = null above P_EST_MAX (pulse) */
      jb(&j,",\"press\":{\"raw\":%u,\"k\":%.6f,\"anchor_bar\":%d,\"p0\":%d,\"max_bar\":%.1f",reg[51],(double)p_k(),p_anchor_bar(),P_ANCHOR_RAW,(double)P_EST_MAX);
      if(pb<0.f) jb(&j,",\"bar\":null,\"pulse\":true}"); else jb(&j,",\"bar\":%.2f,\"pulse\":false}",(double)pb); }
    jb(&j,",\"chart\":{\"mode\":%d,\"active\":%s,\"full\":%d,\"top\":%d,\"raw_max\":%d,\"disp_max\":%d,\"ticks\":%d,\"base_ticks\":%d,\"clipped\":%d,\"n\":%d,\"n_active\":%d",
       cst.mode,cst.active?"true":"false",cst.full,cst.top,cst.raw_max,cst.disp_max,cst.ticks,cst.bticks,cst.clip,cst.n,cst.na);
    /* v6.15: graph reset on activity start — buf = samples in the ring, w_max/w_top = the weight line's max + axis top (mode 0),
     * resets/reanchors = counters, act_ms = ms since the current curve started (-1 = none since start) */
    jb(&j,",\"buf\":%d,\"w_max\":%d,\"w_top\":%d,\"resets\":%d,\"reanchors\":%d,\"act_ms\":%lld}",
       g_n,cst.wmax,cst.wtop,gr.resets,gr.reanchors,(long long)(gr.resets?now-gr.t0:-1));
    /* what the screen shows */
    jb(&j,",\"shown\":{");
    if(view==V_MAIN&&page==PG_HOME){
        jb(&j,"\"chip\":"); jb_s(&j,hm.chip?hm.chip:ph_name(act.ph)); jb(&j,",\"badge\":"); if(hm.badge) jb_s(&j,hm.badge); else jb(&j,"null"); jb(&j,",\"hero\":"); jb_s(&j,hm.hero); jb(&j,",\"unit\":"); jb_s(&j,hm.unit?hm.unit:"");
        jb(&j,",\"label\":"); jb_s(&j,hm.label); jb(&j,",\"sub\":"); jb_s(&j,hm.sub); jb(&j,",\"stats\":[");
        for(int k=0;k<3;k++){ jb(&j,"%s[",k?",":""); jb_s(&j,hm.sl[k]?hm.sl[k]:""); jb(&j,","); jb_s(&j,hm.svv[k]); jb(&j,"]"); }
        jb(&j,"]");
    } else if(view==V_DETAIL&&sel>=0){
        set_fmt(sel,edit_val,a,sizeof a); set_fmt(sel,edit_orig,b,sizeof b);
        jb(&j,"\"param\":\"%s\",\"label\":",sim_param_id(sel)); jb_s(&j,TR(SET[sel].label)); jb(&j,",\"value\":"); jb_s(&j,a);
        jb(&j,",\"unit\":"); jb_s(&j,set_unit(sel)); jb(&j,",\"saved\":"); jb_s(&j,b);
        jb(&j,",\"raw\":%d,\"min\":%d,\"max\":%d,\"step\":%d,\"locked\":%s,\"saving\":%s",edit_val,SET[sel].lo,SET[sel].hi,SET[sel].step,(SET[sel].flags&SF_LOCK)?"true":"false",sav.active?"true":"false");
    } else if(view==V_SYS){
        jb(&j,"\"x\":\"%s\",\"edit\":%d,\"locked\":%s",xsel==X_BOOTUI?"bootui":(xsel==X_SWITCH?"switch":"exit"),x_edit,sys_locked()?"true":"false");
    } else if(view==V_MAIN&&pg_list(page)){
        static const char*const SECK[]={"display","clock","schedule","stock"}; char lv[96]; page_live_text(page,lv,sizeof lv,now);
        jb(&j,"\"title\":"); jb_s(&j,page_title(page)); jb(&j,",\"rows\":%d,\"scroll\":%d,\"live\":",page_rows(page),(int)scroll[page]); jb_s(&j,lv);
        jb(&j,",\"section\":"); if(page==PG_SYS&&sec_open>=0) jb(&j,"\"%s\"",SECK[sec_open]); else jb(&j,"null");
    } else if(view==V_BYE){ jb(&j,"\"sub\":"); jb_s(&j,bye_sub()); }
    jb(&j,"}");
    /* toast: text = the table's DANISH text (the scenarios' assert language), shown = the text on screen */
    if(toast.dur&&now-toast.t0<toast.dur){
        uint32_t c=sim_tst.col; const char*kind=c==C_OK?"ok":(c==C_ERR?"err":(c==C_WARN?"warn":"info"));
        jb(&j,",\"toast\":{\"text\":"); jb_s(&j,sim_tst.id>=0?STR[LANG_DA][sim_tst.id]:toast.msg);
        jb(&j,",\"shown\":"); jb_s(&j,toast.msg); jb(&j,",\"id\":\"%s\",\"kind\":\"%s\"}",sim_tst.id>=0?STR_ID[sim_tst.id]:"",kind);
    } else jb(&j,",\"toast\":null");
    /* language: lang = the stock UI's config.language (contract); ui_lang = Meraki+'s own language */
    jb(&j,",\"lang\":%d,\"ui_lang\":\"%s\",\"lang_src\":\"%s\"",sv(S_LANG),LANG_CODE[ui_lang],lang_src==LS_FILE?"file":(lang_src==LS_UNKNOWN?"unknown":(lang_src==LS_SESSION?"session":"default")));
    /* boot: the flag is read fresh; the rest is the UI's own view (bs) — the state export does not change it */
    int flag=stat(rp(BOOT_FLAG),&st)==0, nb=(!flag||bs.lock==BL_NOMOUNT)?BN_ORIGINAL:(bs.lock==BL_OK?BN_OURS:BN_SLOTB);
    jb(&j,",\"boot_flag\":%s,\"next_boot\":\"%s\",\"boot_lock\":\"%s\",\"exe_slot\":\"%s\",\"self_exe\":",flag?"true":"false",
       nb==BN_OURS?"ours":(nb==BN_SLOTB?"slot_b":"original"),bs.lock==BL_OK?"ok":(bs.lock==BL_NOMOUNT?"nomount":(bs.lock==BL_NOBIN?"nobin":"vendor")),
       bs.self_slot==SLOT_B?"B":(bs.self_slot==SLOT_A?"A":"other")); jb_s(&j,bs.self_exe);
    jb(&j,",\"switch\":{\"mode\":\"%s\",\"locked\":%s,\"dry\":%s,\"bye\":%s,\"stock_n\":%d,\"switcher_pid\":%d,\"child\":%d,\"last_evt\":",switch_mode()==SWM_CONT?"cont":"launch",
       switch_locked()?"true":"false",sw_dry?"true":"false",view==V_BYE?"true":"false",bs.n_stock,bs.sw_pid,sw_child); jb_s(&j,bs.sw_evt); jb(&j,"}");
    snprintf(rn,sizeof rn,"%s/RESTART_NEEDED",cfg_bakdir);
    { int fd=open(rn,O_RDONLY), have=fd>=0; if(have){ static char rb[1024]; int n=(int)read(fd,rb,sizeof rb-1); close(fd); if(n<0)n=0; rb[n]=0;
        char*q=strstr(rb,"boot_id="); if(q){ snprintf(stamp,sizeof stamp,"%s",q); stamp[strcspn(stamp,"\r\n")]=0; } }
      jb(&j,",\"restart_needed\":{\"file\":%s,\"pending\":%s,\"stamp\":",have?"true":"false",restart_pending()?"true":"false"); jb_s(&j,stamp); jb(&j,"}"); }
    /* link + write queue */
    jb(&j,",\"crc_bad\":%u,\"cmd6\":{\"ok\":%ld,\"retx\":%ld,\"fail\":%ld,\"block\":%ld},\"blok\":%ld,\"wq_n\":%d",rx_bad,wq.n_ok,wq.n_retx,wq.n_fail,wq.n_block,wq.n_block,wq.n);
    jb(&j,",\"boot_pushed\":%s,\"boot_push\":{\"enabled\":%s,\"sub1_len30\":\"%s\",\"sub112_len3\":\"%s\",\"runs\":%d}",(bp.state==BP_DONE&&bp.ra!=BPR_NOCFG&&bp.ra!=BPR_SKIP)?"true":"false",bp_on?"true":"false",sim_bpr(bp.ra),sim_bpr(bp.rb),bp.runs);
    jb(&j,",\"rx_resync\":%u",rx_resync);
    jb(&j,",\"repush\":{\"count\":%d,\"ver\":%d,\"defaults\":%d,\"last\":\"%s\",\"armed\":%s,\"ver_reqs\":%u,\"defaults_sig\":%s}",repush_count(),rps.n_ver,rps.n_def,
       rps.last==RPC_VER?"ver":(rps.last==RPC_DEF?"defaults":"none"),rps.armed?"true":"false",ver_n,mcu_defaults_sig()?"true":"false");
    jb(&j,",\"ka_hold\":%s,\"backlight_preview\":%d",sim_ka_hold?"true":"false",sim_bl);
    { struct tm lt; int64_t u=clk_utc(); int ok=clk_now(&lt), d=ok&&SET[S_DST].val&&dst_on(tz_idx(),u); char c[24]="", st[72]="";   /* v6.16: clock + schedule */
      if(ok) fmt_hm(c,sizeof c,lt.tm_hour,lt.tm_min,1);
      jb(&j,",\"clock\":{\"utc\":%lld,\"valid\":%s,\"local\":\"%04d-%02d-%02dT%02d:%02d:%02d\",\"wday\":%d,\"timemode\":%d,\"tz\":%d,\"dst\":%s,\"shown\":",
         (long long)u,ok?"true":"false",lt.tm_year+1900,lt.tm_mon+1,lt.tm_mday,lt.tm_hour,lt.tm_min,lt.tm_sec,lt.tm_wday,SET[S_TIMEMODE].val,tz_idx(),d?"true":"false"); jb_s(&j,c); jb(&j,"}");
      int ad,ah,am; int64_t at; int w=sched_alarm(u,&ad,&ah,&am,&at); int hp=home_preheat_text(st,sizeof st);
      jb(&j,",\"sched\":{\"preheat\":%d,\"at\":\"%02d:%02d\",\"repeat\":\"%s\",\"auto_off\":%d,\"once_at\":%lld,\"file\":\"%s\",\"alarm\":[%d,%d,%d],\"next_utc\":%lld",
         sch.on,sch.hm/60,sch.hm%60,REP_KEY[sch.rep],AO_MIN[sch.ao],(long long)sch.once_at,sch.src==SCS_FILE?"file":(sch.src==SCS_SESSION?"session":"default"),ad,ah,am,(long long)(w?at:0));
      jb(&j,",\"armed\":"); if(arm.n_sent) jb(&j,"[%d,%d,%d]",arm.ad,arm.ah,arm.am); else jb(&j,"null");
      jb(&j,",\"arm_sent\":%ld,\"arm_ack\":%ld,\"idle_s\":%d,\"auto_offs\":%ld,\"home\":",arm.n_sent,arm.n_ack,(int)((now-idle_t0)/1000),ao_n); if(hp) jb_s(&j,st); else jb(&j,"null"); jb(&j,"}"); }
    { static const char*const PWS[]={"on","turning_off","off","turning_on"};   /* v6.14: the power button */
      jb(&j,",\"power\":{\"state\":\"%s\",\"backlight_off\":%s,\"presses\":%ld,\"short\":%ld,\"offs\":%ld,\"ons\":%ld,\"blocked\":%ld,\"fails\":%ld,\"ack8\":%ld,\"auto_on\":%ld,\"wake_timeouts\":%ld}",
         PWS[pw.state],bl_is_off?"true":"false",pw.n_press,pw.n_short,pw.n_off,pw.n_on,pw.n_blk,pw.n_fail,pw.n_ack8,pw.n_auto,pw.n_wto); }
    { static const char*const NSK[]={"off","down","starting","connecting","up"}; char ipb[20];   /* v6.7: the net watchdog */
      jb(&j,",\"net\":{\"enabled\":%s,\"state\":\"%s\",\"iface\":%s,\"ifup\":%s,\"wpa_pid\":%d,\"dhcp_pid\":%d,\"carrier\":%s,\"ip\":\"%s\",\"gw\":%s,\"heals\":%d,\"boots\":%d,\"heal_ms\":%d,\"init_runs\":%d,\"wpa_starts\":%d,\"dhcp_starts\":%d,\"conf_writes\":%d,\"fails\":%d}",
         net.on?"true":"false",NSK[net.on?net.state:NS_OFF],net.p.iface?"true":"false",net.p.ifup?"true":"false",net.p.wpa_pid,net.p.dhcp_pid,net.p.carrier?"true":"false",net_ipstr(net.p.ip,ipb,sizeof ipb),net.p.gw?"true":"false",
         net.n_heal,net.n_boot,(int)net.heal_ms,net.n_init,net.n_wpa,net.n_dhcp,net.n_conf,net.n_fail); }
    /* config */
    jb(&j,",\"config_missing\":%s,\"config_writes\":%d,\"config_md5\":\"%s\",\"config_oor\":[",cfg_ok?"false":"true",cfg_writes,cfg_md5);
    for(int i=0,n=0;i<NSET;i++) if(set_oor(&SET[i])) jb(&j,"%s{\"param\":\"%s\",\"field\":\"%s\",\"value\":%d,\"min\":%d,\"max\":%d}",n++?",":"",sim_param_id(i),SET[i].key?SET[i].key:"",SET[i].val,SET[i].lo,SET[i].hi);
    jb(&j,"]");
    if(ssave.state==SSV_NONE) jb(&j,",\"save\":null");
    else jb(&j,",\"save\":{\"state\":\"%s\",\"param\":\"%s\",\"value\":%d}",SSV_KEY[ssave.state],ssave.param,ssave.val);
    if(act.pending==ACT_NONE) jb(&j,",\"activity_pending\":null"); else jb(&j,",\"activity_pending\":\"%s\"",ACT_KEY[act.pending]);
    /* counters outside the content comparison: RX counter + frame time (last, max since previous line, mean, max over the run; HOST) */
    char*tail=j.p; uint32_t h=H0; for(char*q=buf;q<tail;q++){ h^=(unsigned char)*q; h*=16777619u; }
    jb(&j,",\"crc_ok\":%u,\"frame_ms\":%.3f,\"frame_ms_max\":%.3f,\"frame_ms_avg\":%.3f,\"frame_ms_peak\":%.3f,\"frames\":%ld,\"fb_seq\":%ld}",
       rx_ok,ft_last,sim_ft_win>ft_last?sim_ft_win:ft_last,sim_frames?sim_ft_sum/sim_frames:0.0,sim_ft_peak,sim_frames,sim_fb_seq);
    if(j.p>=j.e){ static int warned; if(!warned){ warned=1; fputs("merakiplus-sim: state line too long\n",stderr); } return; }
    if(!force&&h==last_h&&now-sim_state_last<90) return;
    last_h=h; *j.p++='\n'; fprintf(sim_sf,"{\"t_ms\":%lld,",(long long)(now-sim_t0)); fwrite(buf,1,(size_t)(j.p-buf),sim_sf); fflush(sim_sf); sim_state_last=now; sim_ft_win=0;
}
/* after each rendered frame: frame statistics, state line (if the content changed, otherwise ~10 Hz), fb file */
static void sim_frame(ms_t now){
    sim_frames++; sim_ft_sum+=ft_last; if(ft_last>sim_ft_win) sim_ft_win=ft_last; if(ft_last>sim_ft_peak) sim_ft_peak=ft_last;
    uint32_t g=H0; int ton=toast.dur&&now-toast.t0<toast.dur;
    g=hi(g,view); g=hi(g,page); g=hi(g,sel); g=hi(g,xsel); g=hi(g,phase_raw(now)); g=hi(g,act.ph); g=hi(g,ton?(int)(toast.t0&0x7FFFFFFF):0);
    g=hi(g,ssave.state); g=hi(g,ssave.val); g=hi(g,act.pending); g=hi(g,(int)(wq.n_ok+wq.n_fail*7+wq.n_block*31+wq.n_retx*131));
    g=hi(g,ui_lang); g=hi(g,sv(S_LANG)); g=hi(g,cfg_writes); g=hi(g,bp.state); g=hi(g,stm.kind*4+stm.wand); g=hi(g,repush_count()); g=hi(g,edit_val); g=hi(g,x_edit); g=hi(g,access(rp(BOOT_FLAG),F_OK)==0);
    int chg=(g!=sim_sig); sim_sig=g; sim_state_emit(now,chg);   /* every frame if anything in the line changed; otherwise at least every 90 ms */
    if(sim_fb_seq!=sim_fb_dumped&&(chg||now-sim_fb_last>=100)){ if(sim_fb_write(sim_fb_path)){ sim_fb_last=now; sim_fb_dumped=sim_fb_seq; } }
}

/* ---- JSON in: one flat object per line ---- */
typedef struct { const char*k; int kl; const char*v; int vl; char t; } JKV;   /* t: 's' string (without quotes), 'a' array, 'o' object, 'n' number/true/false/null */
static const char* js_ws(const char*s){ while(*s==' '||*s=='\t'||*s=='\r'||*s=='\n') s++; return s; }
static const char* js_strend(const char*s){ while(*s&&*s!='"'){ if(*s=='\\'&&s[1]) s++; s++; } return s; }   /* s points past the opening quote */
static int js_parse(const char*s,JKV*kv,int max){
    int n=0; s=js_ws(s); if(*s!='{') return -1; s++;
    for(;;){
        s=js_ws(s); if(*s=='}') return n; if(*s!='"') return -1;
        const char*k=s+1,*ke=js_strend(k); if(!*ke) return -1;
        s=js_ws(ke+1); if(*s!=':') return -1; s=js_ws(s+1);
        JKV e={k,(int)(ke-k),s,0,'n'};
        if(*s=='"'){ const char*ve=js_strend(s+1); if(!*ve) return -1; e.v=s+1; e.vl=(int)(ve-s-1); e.t='s'; s=ve+1; }
        else if(*s=='['||*s=='{'){ int d=0; const char*q=s; e.t=(*s=='[')?'a':'o';
            for(;*q;q++){ if(*q=='"'){ q=js_strend(q+1); if(!*q) return -1; } else if(*q=='['||*q=='{') d++; else if(*q==']'||*q=='}'){ if(--d==0){ q++; break; } } }
            if(d) return -1; e.vl=(int)(q-s); s=q; }
        else { const char*q=s; while(*q&&*q!=','&&*q!='}'&&*q!=' '&&*q!='\r'&&*q!='\n') q++; e.vl=(int)(q-s); s=q; }
        if(n<max) kv[n++]=e;
        s=js_ws(s); if(*s==','){ s++; continue; } if(*s=='}') return n; return -1;
    }
}
static const JKV* js_get(const JKV*kv,int n,const char*key){ int L=(int)strlen(key); for(int i=0;i<n;i++) if(kv[i].kl==L&&!memcmp(kv[i].k,key,L)) return &kv[i]; return NULL; }
static int js_str(const JKV*kv,int n,const char*key,char*out,size_t on){
    const JKV*e=js_get(kv,n,key); out[0]=0; if(!e||e->t!='s') return 0;
    size_t o=0; for(int i=0;i<e->vl&&o+1<on;i++){ char c=e->v[i];
        if(c=='\\'&&i+1<e->vl){ c=e->v[++i]; if(c=='n') c='\n'; else if(c=='t') c='\t'; else if(c=='u'&&i+4<e->vl){ char h[5]; memcpy(h,e->v+i+1,4); h[4]=0; c=(char)strtol(h,NULL,16); i+=4; } }
        out[o++]=c; }
    out[o]=0; return 1;
}
static int js_int(const JKV*kv,int n,const char*key,int def){
    const JKV*e=js_get(kv,n,key); if(!e) return def;
    if(e->t=='n'){ if(e->vl>=4&&!memcmp(e->v,"true",4)) return 1; if(e->vl>=5&&!memcmp(e->v,"false",5)) return 0; if(e->vl>=4&&!memcmp(e->v,"null",4)) return def; }
    if(e->t=='n'||e->t=='s') return (int)strtod(e->v,NULL);
    return def;
}
static double js_num(const JKV*kv,int n,const char*key,double def){ const JKV*e=js_get(kv,n,key); return (e&&(e->t=='n'||e->t=='s')&&e->v[0]!='n')?strtod(e->v,NULL):def; }
static int js_words(const JKV*kv,int n,const char*key,uint16_t*w,int max){
    const JKV*e=js_get(kv,n,key); if(!e||e->t!='a') return -1;
    int c=0; const char*s=e->v+1,*end=e->v+e->vl-1;
    while(s<end){ s=js_ws(s); if(s>=end) break; char*q; long v=strtol(s,&q,0); if(q==s) return -1; if(c>=max||v<0||v>65535) return -1; w[c++]=(uint16_t)v; s=js_ws(q); if(*s==',') s++; }
    return c;
}

/* ---- test hooks ---- */
static int sim_def_kind=0, sim_def_a=0, sim_def_b=0; static ms_t sim_def_until=0; static char sim_def_param[40]="";
/* end splash/transition and close an open detail/system view — like a completed tap on BACK, without animation */
static void sim_go_main(void){
    if(sim_bye_on){ sim_bye_on=0; sim_bye_until=0; if(view==V_BYE) view=V_MAIN; }
    if(view==V_DETAIL&&sel>=0){ if(SET[sel].flags&SF_BL) bl_set(SET[sel].val); if(SET[sel].flags&SF_LOCAL) lang_set(SET[sel].val); }
    if(view!=V_MAIN&&view!=V_BYE){ view=V_MAIN; act.gkind=-1; }
    sel=-1; xsel=-1; x_from_switch=0; tr.type=TR_NONE; static_dirty=1; force_all=1; ui_kick=1;
}
static void sim_set_page(int np){ if(page!=np){ page=np; static_dirty=1; if(np==PG_HOME) act.gkind=-1; } }
/* v6.17: the page a row lives on — and, on SYSTEM, its section opened (as the user would have had to) */
static void sim_set_row(int idx){ sim_set_page(PG_T0+SET[idx].tier); if(SET[idx].tier==4&&sec_of_set(idx)>=0){ sec_open=sec_of_set(idx); scroll[PG_SYS]=clampf((float)(sec_open*ROW_P),0,scroll_max(PG_SYS)); } }
static void sim_set_stock(void){ sim_set_page(PG_SYS); sec_open=SEC_STOCK; scroll[PG_SYS]=clampf((float)(SEC_STOCK*ROW_P),0,scroll_max(PG_SYS)); }
/* SAVE as via stepper + SAVE: open the row, set the value the stepper would show, tap SAVE (save_begin) */
static int sim_save_now(int idx,int val,const char*param,ms_t now){
    ssave.idx=idx; ssave.val=val; snprintf(ssave.param,sizeof ssave.param,"%s",param);
    if(idx<0||(SET[idx].flags&SF_LOCK)){                    /* no editor: unknown/not exposed, locked or sensor */
        ssave.state=SSV_REJECTED; ssave.idx=0; toast_show(TR(TX_D_LOCKED),C_WARN,now); ui_kick=1; return 0;
    }
    Setting*s=&SET[idx]; int was=s->val;
    sim_go_main(); sim_set_row(idx);
    detail_open(idx,now);
    if(!set_valid(s,val)||(s->step>1&&(val-s->lo)%s->step)){   /* outside the range or between the stepper's steps */
        ssave.state=SSV_REJECTED; toast_show(TR(TX_T_RANGE),C_ERR,now); detail_close(now); return 0;
    }
    edit_val=val; if(s->flags&SF_BL) bl_set(val); if(s->flags&SF_LOCAL) lang_set(val);
    save_begin(now);
    if(sav.active){ ssave.state=SSV_PENDING; return 1; }
    if(was==val){ ssave.state=SSV_UNCHANGED; return 1; }    /* SAVE without a change just closes the editor */
    if(s->flags&(SF_LOCAL|SF_OWN|SF_SND)){ ssave.state=SSV_OK; return 1; }
    ssave.state=SSV_REJECTED; if(view==V_DETAIL) detail_close(now);   /* a guard rejected it (toast shown): the user cancels */
    return 0;
}
/* activity as via the button (the hold time is skipped; the guards are the same) */
static int sim_action_now(int kind,ms_t now){
    sim_go_main(); sim_set_page(PG_HOME);
    act_request(kind,now); return act.pending==kind;
}
/* raw_write: emulates a STOCK UI write (contract §5), e.g. MachSleep word1=1, which merakiplus itself never
 * sends. Same band and trigger guard as the frame policy (words 26/27/30 and start words cannot
 * be set), and all other words must lie within their spec range — the only relaxation is word1 ∈ {0,1}.
 * word1=1 also pauses merakiplus's keep-awake (otherwise the UI wakes the controller again within
 * 3 s — that is the v6 behaviour); a raw_write w/ word1=0 lifts the pause. */
static int sim_raw_push(int sub,const uint16_t*w,int len){
    if(ro_mode||wq.n>=WQ_MAX||len<1||len>WQ_WMAX||sub<0||sub>255) return 0;
    int end=sub+len;
    if(!(end<33||(sub>=112&&end>=113&&end<=115))){ wq.n_block++; return 0; }
    for(int k=0;k<len;k++){
        int idx=sub+k; uint16_t v=w[k];
        if(idx<32&&v!=0&&((TRIG_NEVER|TRIG_START)&WBIT(idx))){ wq.n_block++; return 0; }
        if(idx==1){ if(v>1){ wq.n_block++; return 0; } continue; }
        if(!word_ok(idx,v)){ wq.n_block++; return 0; }
    }
    WFrame*f=&wq.q[wq.n++]; f->sub=(uint8_t)sub; f->len=(uint8_t)len; memcpy(f->w,w,(size_t)len*2);
    if(wq.n==1){ wq.state=WQ_IDLE; wq.result=WR_NONE; }
    if(sub<=1&&end>1) sim_ka_hold=(w[1-sub]==1);
    return 1;
}
/* SWITCH NOW in dry-run: SIM shows the goodbye screen for 1 s and returns (device: only log + toast) */
static int sim_bye_begin(int mode,ms_t now){
    bye_mode=mode; view=V_BYE; xsel=-1; x_from_switch=0; tr.type=TR_NONE; rip.on=0; toast.dur=0; static_dirty=1; force_all=1;
    sim_bye_on=1; sim_bye_until=now+1000; ui_kick=1; return 1;
}
static void sim_hooks_step(ms_t now){
    if(sim_bye_on&&now>=sim_bye_until){
        sim_bye_on=0;
        if(view==V_BYE&&run){ view=V_MAIN; static_dirty=1; force_all=1; act.gkind=-1; toast_show(TR(TX_T_DRYRUN),C_WARN,now); ui_kick=1; }
    }
    if(sim_def_kind){                                       /* deferred hook: wait (at most 600 ms) until the write queue is empty, like a tap a moment later */
        int busy=wq_busy()||sav.active;
        if(!busy||now>=sim_def_until){ int k=sim_def_kind; sim_def_kind=0; if(k==1) sim_save_now(sim_def_a,sim_def_b,sim_def_param,now); else sim_action_now(sim_def_a,now); }
    }
}
static void sim_dispatch(const char*line,char*out,size_t on,ms_t now){
    JKV kv[24]; int n=js_parse(line,kv,24); char cmd[32]="", name[40]="", path[420]="";
    JB j={out,out+on-48};
    if(n<0){ jb(&j,"{\"ok\":false,\"error\":\"invalid JSON\""); goto done; }
    js_str(kv,n,"cmd",cmd,sizeof cmd); js_str(kv,n,"name",name,sizeof name);
    if(!strcmp(cmd,"ping")){ jb(&j,"{\"ok\":true,\"t\":%.3f,\"page\":\"%s\",\"phase\":\"%s\",\"client\":\"merakiplus-sim\",\"version\":\"" MERAKIPLUS_VERSION "\"",(now-sim_t0)/1000.0,sim_page_key(),PH_KEY[phase_raw(now)]); }
    else if(!strcmp(cmd,"state_now")){ sim_state_emit(now,1); jb(&j,"{\"ok\":true"); }
    else if(!strcmp(cmd,"fb_dump")){
        if(!js_str(kv,n,"path",path,sizeof path-8)) snprintf(path,sizeof path,"%s",sim_fb_path);
        jb(&j,"{\"ok\":%s,\"bytes\":%d",sim_fb_write(path)?"true":"false",W*H*4);
    }
    else if(!strcmp(cmd,"exit")){ sim_rc=js_int(kv,n,"code",0); run=0; jb(&j,"{\"ok\":true"); }
    else if(!strcmp(cmd,"key")){   /* v6.14: the power button (SIM has no k62091 node): {"cmd":"key","value":1|0[,"code":116]} */
        int v=js_int(kv,n,"value",-1), c=js_int(kv,n,"code",116);
        if(v!=0&&v!=1) jb(&j,"{\"ok\":false,\"error\":\"value must be 0 or 1\"");
        else { pw_key(c,v,now); jb(&j,"{\"ok\":true,\"power\":\"%s\"",pw.state==PW_ON?"on":(pw.state==PW_OFF?"off":(pw.state==PW_OFFING?"turning_off":"turning_on"))); } }
    else if(!strcmp(cmd,"action")){
        int kind=-1; for(int k=0;k<4;k++) if(!strcmp(name,ACT_KEY[k])) kind=k;
        int stk=-1; for(int k=0;k<4;k++) if(!strcmp(name,STOP_KEY[k])) stk=k;     /* v6.13: grind_stop / brew_stop / water_stop (+ steam_stop as before) */
        if(stk>=0){ int n0=wq.n; stop_request(stk,now); ui_kick=1; jb(&j,"{\"ok\":%s",wq.n>n0?"true":"false"); }
        else if(kind<0) jb(&j,"{\"ok\":false,\"error\":\"unknown activity\"");
        else if((wq_busy()||sav.active)&&act.pending==ACT_NONE){ sim_def_kind=2; sim_def_a=kind; sim_def_until=now+600; jb(&j,"{\"ok\":true,\"deferred\":true"); }
        else { int ok=sim_action_now(kind,now); jb(&j,"{\"ok\":%s",ok?"true":"false"); if(!ok&&sim_tst.id>=0) jb(&j,",\"guard\":\"%s\"",STR_ID[sim_tst.id]); }
    }
    else if(!strcmp(cmd,"save")){
        char param[40]=""; js_str(kv,n,"param",param,sizeof param); int idx=sim_param_idx(param), val=js_int(kv,n,"value",0);
        if(idx>=0&&!(SET[idx].flags&SF_LOCK)&&(sav.active||(SET[idx].mcu!=M_NONE&&wq_busy()))){
            sim_def_kind=1; sim_def_a=idx; sim_def_b=val; snprintf(sim_def_param,sizeof sim_def_param,"%s",param); sim_def_until=now+600; jb(&j,"{\"ok\":true,\"deferred\":true");
        } else { int ok=sim_save_now(idx,val,param,now); jb(&j,"{\"ok\":%s,\"state\":\"%s\",\"row\":",ok?"true":"false",SSV_KEY[ssave.state]); jb_s(&j,idx>=0&&SET[idx].key?SET[idx].key:(idx>=0?"(ui)":"")); }
    }
    else if(!strcmp(cmd,"page")){
        char param[40]=""; js_str(kv,n,"param",param,sizeof param); int ok=1;
        sim_go_main();
        /* v6.17: grind | brew | water | steam | system by name ("settings" + tab 0..4 = the same five pages; tab 1 = BREW is the default);
         * "section" (display | clock | schedule | stock) opens that SYSTEM section */
        static const char*const PGN[]={"grind","brew","water","steam","system"}; int np=-1; char sn[16]="";
        for(int k=0;k<5;k++) if(!strcmp(name,PGN[k])) np=PG_GRIND+k;
        if(!strcmp(name,"settings")) np=PG_GRIND+clampi(js_int(kv,n,"tab",1),0,4);
        if(!strcmp(name,"home")) { if(page!=PG_HOME) go_page(PG_HOME,-1,now); }
        else if(np>=0){
            if(np==PG_SYS){ static const char*const SECK[]={"display","clock","schedule","stock"}; sec_open=-1; scroll[PG_SYS]=0;
                if(js_str(kv,n,"section",sn,sizeof sn)) for(int k=0;k<NSEC;k++) if(!strcmp(sn,SECK[k])){ sec_open=k; scroll[PG_SYS]=clampf((float)(k*ROW_P),0,scroll_max(PG_SYS)); } }
            if(page!=np) go_page(np,1,now); }
        else if(!strcmp(name,"diag")){ if(page!=PG_DIAG){ diag_from=PG_SYS; go_page(PG_DIAG,1,now); } }
        else if(!strcmp(name,"detail")){ int idx=param[0]?sim_param_idx(param):S_GRINDW; if(idx<0) ok=0; else { sim_set_row(idx); detail_open(idx,now); } }
        else if(!strcmp(name,"sys")){ sim_set_stock(); sys_open(js_int(kv,n,"x",X_BOOTUI)==X_SWITCH?X_SWITCH:X_BOOTUI,now); }
        else ok=0;
        jb(&j,"{\"ok\":%s,\"page\":\"%s\"",ok?"true":"false",sim_page_key());
    }
    else if(!strcmp(cmd,"touch")){ sim_touch_inject(js_int(kv,n,"x",CX),js_int(kv,n,"y",CY),js_int(kv,n,"ms",80),now); jb(&j,"{\"ok\":true"); }
    else if(!strcmp(cmd,"boot_flag")){                      /* SYSTEM -> BOOT UI -> choose -> SAVE (sys_confirm -> boot_flag_set w/ guard) */
        int want=js_int(kv,n,"value",0)!=0; struct stat st;
        sim_go_main(); sim_set_stock(); sys_open(X_BOOTUI,now); x_edit=want; sys_confirm(now);
        if(view==V_SYS) sys_close(now);                     /* locked / error: toast is shown, the user taps BACK */
        int have=stat(rp(BOOT_FLAG),&st)==0;
        jb(&j,"{\"ok\":%s,\"flag\":%s,\"lock\":%d",have==want?"true":"false",have?"true":"false",bs.lock);
    }
    else if(!strcmp(cmd,"runtime_switch")){                 /* SYSTEM -> SWITCH TO ORIGINAL NOW -> SWITCH NOW (sys_confirm -> do_switch) */
        sim_go_main(); sim_set_stock(); sys_open(X_SWITCH,now); sys_confirm(now);
        int went=sim_bye_on||!run;
        if(view==V_SYS&&xsel==X_SWITCH) sys_close(now);     /* blocked (activity/locked): toast shown, the user goes back */
        jb(&j,"{\"ok\":%s,\"dry\":%s",went?"true":"false",sw_dry?"true":"false"); if(!went&&sim_tst.id>=0) jb(&j,",\"guard\":\"%s\"",STR_ID[sim_tst.id]);
    }
    else if(!strcmp(cmd,"raw_write")){
        uint16_t w[WQ_WMAX]; int len=js_words(kv,n,"words",w,WQ_WMAX), ok=len>0&&sim_raw_push(js_int(kv,n,"sub",-1),w,len);
        jb(&j,"{\"ok\":%s,\"ka_hold\":%s",ok?"true":"false",sim_ka_hold?"true":"false");
    }
    else if(!strcmp(cmd,"config_reload")){                  /* as at startup: read language file + config, and (SIM) boot-push again */
        sim_go_main(); lang_load(); settings_load(); if(bp_on) boot_push_start(sim_bp_guard);
        int oor=0; for(int i=0;i<NSET;i++) oor+=set_oor(&SET[i]);
        jb(&j,"{\"ok\":%s,\"oor\":%d",cfg_ok?"true":"false",oor);
    }
    else if(!strcmp(cmd,"clock")){                          /* v6.16: the wall clock — {"utc":"YYYY-MM-DDTHH:MM:SSZ"} and/or {"advance_s":N} */
        char u[40]=""; js_str(kv,n,"utc",u,sizeof u); int adv=js_int(kv,n,"advance_s",0), ok=1;
        if(u[0]){ int64_t t=sim_parse_utc(u); if(t<0) ok=0; else clk_off=t-(int64_t)time(NULL); }
        if(ok) clk_off+=adv; ui_kick=1; static_dirty=1;
        jb(&j,"{\"ok\":%s,\"utc\":%lld",ok?"true":"false",(long long)clk_utc());
    }
    else if(!strcmp(cmd,"cond")){                           /* v6.9: display conditioning — all fields optional; without fields = read */
        char sc[8]=""; js_str(kv,n,"scale",sc,sizeof sc);
        cd.a_chart=clampf((float)js_num(kv,n,"a_chart",cd.a_chart),0.01f,1); cd.a_hero=clampf((float)js_num(kv,n,"a_hero",cd.a_hero),0.01f,1);
        cd.med=js_int(kv,n,"med",cd.med)>=3?3:1; cd.hold=clampi(js_int(kv,n,"hold",cd.hold),1,50); cd.pct=clampi(js_int(kv,n,"pct",cd.pct),50,100);
        cd.head=clampf((float)js_num(kv,n,"head",cd.head),1,3); cd.raw=js_int(kv,n,"raw",cd.raw)!=0; cd.gate=clampi(js_int(kv,n,"gate",cd.gate),0,10);
        for(int k=0;k<3;k++) if(!strcmp(sc,CS_KEY[k])) cd.scale=k;
        if(js_get(kv,n,"reset")||js_get(kv,n,"a_chart")||js_get(kv,n,"med")) cond_reset();
        hf.ok=0; ui_kick=1;
        jb(&j,"{\"ok\":true,\"raw\":%s,\"a_chart\":%.3f,\"a_hero\":%.3f,\"med\":%d,\"scale\":\"%s\",\"hold\":%d,\"pct\":%d,\"head\":%.2f,\"gate\":%d",
           cd.raw?"true":"false",cd.a_chart,cd.a_hero,cd.med,CS_KEY[cd.scale],cd.hold,cd.pct,cd.head,cd.gate);
    }
    else if(!strcmp(cmd,"net")){                            /* v6.7: the wifi model — kill | ip_drop | if_down | cold | ap_down | ap_up | status */
        int ok=1; char ipb[20];
        if(!strcmp(name,"kill")){ nm.wpa_pid=0; nm.dhcp_pid=0; nm.carrier=0; }                       /* kill wpa_supplicant (+ udhcpc): the IP and the route stay on wlan0 */
        else if(!strcmp(name,"ip_drop")){ nm.ip=0; nm.gw=0; }                                       /* ifconfig wlan0 0.0.0.0 */
        else if(!strcmp(name,"if_down")){ nm.ifup=0; nm.carrier=0; nm.gw=0; }                       /* ifconfig wlan0 down: the IP stays, the routes disappear */
        else if(!strcmp(name,"cold")){ nm.iface=0; nm.ifup=0; nm.wpa_pid=0; nm.dhcp_pid=0; nm.carrier=0; nm.ip=0; nm.gw=0; }
        else if(!strcmp(name,"ap_down")){ nm.ap=0; nm.carrier=0; }
        else if(!strcmp(name,"ap_up")){ nm.ap=1; nm.assoc_at=now+NM_ASSOC_MS; }
        else if(strcmp(name,"status")) ok=0;
        if(ok&&strcmp(name,"status")) net.t_probe=0;        /* evaluate in the next loop iteration */
        jb(&j,"{\"ok\":%s,\"state\":%d,\"wpa_pid\":%d,\"ip\":\"%s\",\"heals\":%d",ok?"true":"false",net.state,nm.wpa_pid,net_ipstr(nm.ip,ipb,sizeof ipb),net.n_heal);
    }
    else if(!strcmp(cmd,"layout")){                         /* addendum: the REAL tap targets in the current view (hit_test's geometry) */
        jb(&j,"{\"ok\":true,\"page\":\"%s\",\"targets\":[",sim_page_key()); int c=0;
#define LT_C(nm,x,y,r) jb(&j,"%s{\"name\":\"%s\",\"x\":%d,\"y\":%d,\"r\":%d}",c++?",":"",nm,x,y,r)
#define LT_R(nm,x,y,w,h) jb(&j,"%s{\"name\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}",c++?",":"",nm,(x)+(w)/2,(y)+(h)/2,w,h)
        if(view==V_DETAIL||view==V_SYS){
            int locked=(view==V_SYS)?sys_locked():((SET[sel].flags&SF_LOCK)!=0);
            if(locked) LT_R("back",170,284,140,64);
            else { if(view==V_DETAIL||xsel==X_BOOTUI){ LT_C("minus",84,184,42); LT_C("plus",396,184,42); } LT_R("cancel",100,284,136,64); LT_R("save",244,284,136,64); }
        } else if(view==V_MAIN&&page==PG_HOME){
            int sk[2], sn=stop_btns(now,sk);                                   /* v6.13: STOP for grind/brew/water + latched steam */
            if(sn==1&&steam_hold_shown(now)){ Rect r=stop_hit(2,0); LT_R(STOP_KEY[sk[0]],r.x0,r.y0,r.x1-r.x0,r.y1-r.y0); r=stop_hit(2,1); LT_R("steam_hold",r.x0,r.y0,r.x1-r.x0,r.y1-r.y0); }   /* v6.20 */
            else if(sn){ for(int i=0;i<sn;i++){ Rect r=stop_hit(sn,i); LT_R(STOP_KEY[sk[i]],r.x0,r.y0,r.x1-r.x0,r.y1-r.y0); } }
            else if(!phase_active(phase_raw(now))) for(int k=0;k<4;k++) LT_C(ACT_KEY[k],ACT_X[k],ACT_Y,30);   /* tap = the feature's page, hold = start */
            if(diag_hint_shown()) LT_R("diag",CX-HINT_W/2,236,HINT_W,58);
            LT_R("gear",CX-GEAR_HW,GEAR_Y-22,2*GEAR_HW,46);
        } else if(view==V_MAIN){                                               /* v6.17: every page but HOME: "back" (the header) */
            LT_R("back",PBACK_X0,PBACK_Y0,PBACK_W,PBACK_H);
            if(pg_list(page)){
                static const char*const SECK[]={"sec_display","sec_clock","sec_schedule","sec_stock"};
                Row rw[ROW_MAX]; int nr=rows_get(page,rw), vy1=list_vy1(page);
                for(int k=0;k<nr;k++){ int y=(int)(row_y0(page,k)-scroll[page]); if(y<LIST_VY0-4||y+ROW_H>vy1+4) continue;
                    LT_R(rw[k].kind==RK_SET?sim_param_id(rw[k].a):(rw[k].kind==RK_SEC?SECK[rw[k].a]:(rw[k].kind==RK_DIAG?"diag":(rw[k].a==X_BOOTUI?"boot_ui":"switch_now"))),64,y,352,ROW_H); }
                if(pg_feat(page)) LT_R("start",CX-START_HIT_W/2,START_HIT_Y,START_HIT_W,START_HIT_H);
            } else if(page==PG_DIAG){ int py=(int)(DG_GRAPH+12-scroll[PG_DIAG]); if(py-8>=LIST_VY0&&py+36<=LIST_VY1) LT_R("graph",CX-150,py-8,300,44); }
        }
#undef LT_C
#undef LT_R
        jb(&j,"],\"swipe\":\"right >= 60 px = back (every page and editor but home); no carousel since v6.17\"");
    }
    else jb(&j,"{\"ok\":false,\"error\":\"unknown cmd\"");
done:
    { const JKV*id=n>0?js_get(kv,n,"id"):NULL; if(id&&id->t=='n'&&id->vl<24) jb(&j,",\"id\":%.*s}\n",id->vl,id->v); else jb(&j,",\"id\":null}\n"); }
    j.e=out+on-1; if(j.p>j.e) j.p=j.e; *j.p=0;
}
/* ---- ctrl socket: JSONL, one request per line, reply w/ the same id ---- */
#define SIM_NCLI 8
static struct { int fd, n; char buf[4096]; } sim_cli[SIM_NCLI];
static void sim_ctrl_open(void){
    struct sockaddr_un a; memset(&a,0,sizeof a); a.sun_family=AF_UNIX;
    for(int i=0;i<SIM_NCLI;i++) sim_cli[i].fd=-1;
    if(!sim_ctrl_path[0]) return;
    if(strlen(sim_ctrl_path)>=sizeof a.sun_path) sim_die(2,"MERAKIPLUS_SIM_CTRL is too long for a unix socket (%d chars, max %d)",(int)strlen(sim_ctrl_path),(int)sizeof a.sun_path-1);
    unlink(sim_ctrl_path); snprintf(a.sun_path,sizeof a.sun_path,"%s",sim_ctrl_path);
    sim_lfd=socket(AF_UNIX,SOCK_STREAM,0);
    if(sim_lfd<0||bind(sim_lfd,(struct sockaddr*)&a,sizeof a)!=0||listen(sim_lfd,8)!=0) sim_die(2,"ctrl-socket %s: %s",sim_ctrl_path,strerror(errno));
    fcntl(sim_lfd,F_SETFL,fcntl(sim_lfd,F_GETFL,0)|O_NONBLOCK);
}
static void sim_ctrl_io(ms_t now){
    if(sim_lfd<0) return;
    for(;;){
        int c=accept(sim_lfd,NULL,NULL); if(c<0) break;
        int k=0; while(k<SIM_NCLI&&sim_cli[k].fd>=0) k++;
        if(k==SIM_NCLI){ close(c); break; }
        fcntl(c,F_SETFL,fcntl(c,F_GETFL,0)|O_NONBLOCK); sim_cli[k].fd=c; sim_cli[k].n=0;
    }
    for(int k=0;k<SIM_NCLI;k++){
        if(sim_cli[k].fd<0) continue;
        int r=(int)read(sim_cli[k].fd,sim_cli[k].buf+sim_cli[k].n,sizeof sim_cli[k].buf-1-(size_t)sim_cli[k].n);
        if(r<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)) continue;
        if(r<=0){ close(sim_cli[k].fd); sim_cli[k].fd=-1; continue; }
        sim_cli[k].n+=r; sim_cli[k].buf[sim_cli[k].n]=0;
        char*s=sim_cli[k].buf,*nl;
        while((nl=strchr(s,'\n'))){
            *nl=0; const char*q=js_ws(s);
            if(*q){ static char resp[4096]; sim_dispatch(q,resp,sizeof resp,now);
                    int L=(int)strlen(resp), off=0; while(off<L){ int w=(int)write(sim_cli[k].fd,resp+off,(size_t)(L-off)); if(w<=0) break; off+=w; } }
            s=nl+1;
        }
        int rest=(int)(sim_cli[k].buf+sim_cli[k].n-s); memmove(sim_cli[k].buf,s,(size_t)rest); sim_cli[k].n=rest;
        if(sim_cli[k].n>=(int)sizeof sim_cli[k].buf-1) sim_cli[k].n=0;      /* line without an end: discard it */
    }
}

/* ---- lifecycle (called from main via the SIM_* macros) ---- */
static void sim_start(void){
    sim_sf=fopen(sim_state_path,"a"); if(!sim_sf) sim_die(2,"cannot open MERAKIPLUS_SIM_STATE %s: %s",sim_state_path,strerror(errno));
    if(!sim_fb_write(sim_fb_path)) sim_die(4,"cannot write MERAKIPLUS_SIM_FB %s: %s",sim_fb_path,strerror(errno));
    sim_ctrl_open();                                       /* listens BEFORE the first state line (the runner pings as soon as the line exists) */
    if(bp_on) boot_push_start(sim_bp_guard);
}
static void sim_tick(ms_t now){
    sim_save_track();                                      /* also catches a SAVE started by a tap in this iteration */
    sim_ctrl_io(now);
    sim_hooks_step(now);
    sim_save_track();
    boot_push_step(now);
    repush_step(now);
}
static void sim_exit(void){
    ms_t now=now_ms();
    sim_save_track(); sim_state_emit(now,1);
    if(fbmem) sim_fb_write(sim_fb_path);
    for(int k=0;k<SIM_NCLI;k++) if(sim_cli[k].fd>=0){ close(sim_cli[k].fd); sim_cli[k].fd=-1; }
    if(sim_lfd>=0){ close(sim_lfd); sim_lfd=-1; unlink(sim_ctrl_path); }
    if(sim_sf){ fclose(sim_sf); sim_sf=NULL; }
    if(sim_tfd_w>=0){ close(sim_tfd_w); sim_tfd_w=-1; }
}
#endif /* SIM_RT */
static void inp_poll(ms_t now){
    if(ifd<0) return;
    struct input_event ev;
    while(INP_READ(ev)){
        if(ev.type==EV_ABS){
            if(ev.code==ABS_X||ev.code==ABS_MT_POSITION_X){ in_x=scalex(ev.value); in_moved=1; }
            else if(ev.code==ABS_Y||ev.code==ABS_MT_POSITION_Y){ in_y=scaley(ev.value); in_moved=1; }
            else if(ev.code==ABS_MT_TRACKING_ID){ if(ev.value>=0) in_pdown=1; else in_pup=1; }
        } else if(ev.type==EV_KEY&&ev.code==BTN_TOUCH){ if(ev.value) in_pdown=1; else in_pup=1; }
        else if(ev.type==EV_SYN&&ev.code==SYN_REPORT){
            if(in_pdown&&!in_down){ in_down=1; ui_touch(TE_DOWN,in_x,in_y,now); }
            else if(in_down&&in_moved) ui_touch(TE_MOVE,in_x,in_y,now);
            if(in_pup&&in_down){ in_down=0; ui_touch(TE_UP,in_x,in_y,now); }
            in_pdown=in_pup=in_moved=0;
        }
    }
}

#ifdef DEVICE_RT
/* BOOT.md §9.2 step 1: stop own activity — UART, touch grab and fb are released BEFORE stock A is started */
static void dev_release_impl(void){
    uart_flush();
    if(ufd>=0){ tcdrain(ufd); close(ufd); ufd=-1; }
    if(ifd>=0){ ioctl(ifd,EVIOCGRAB,0); close(ifd); ifd=-1; }
    if(kfd>=0){ close(kfd); kfd=-1; }                                       /* v6.14 */
    if(bl_is_off) bl_power(1);                                              /* v6.14: the stock UI must not start in the dark */
    if(fbmem){ munmap(fbmem,fblen); fbmem=NULL; }
    if(ffd>=0){ close(ffd); ffd=-1; }
}
static void dev_reacquire_impl(void){      /* the stock UI did not come up: merakiplus takes the screen back */
    uart_open(); fb_open(); inp_open(); kbd_open();
    RL full; full.n=1; full.r[0]=FULL; pres_prev=full;
}
/* v6.7: the net watchdog alone, without screen/UART/touch — for status and for the self-heal proof on the device:
 *   merakiplus --net-status            (read-only: probe + comparison of the conf file with what config.json yields)
 *   merakiplus --net-watch [seconds]   (same net_step() as the UI's main loop; also logs to stdout) */
static int net_cli(int argc,char**argv){
    ms_t now=now_ms(); const char*e;
    if((e=getenv("MERAKIPLUS_CONFIG"))) snprintf(cfg_path,sizeof cfg_path,"%s",e);
    if(!strcmp(argv[1],"--net-status")){
        NetProbe p; char want[4096],have[4096],mw[33]="-",mh[33]="-",ipb[20]; int wn=-1;
        memset(&p,0,sizeof p); memset(&net,0,sizeof net); net_now=now; nh_probe(&p,now,1); net_cfg_load();
        if(net.cfg==NC_OK&&(wn=net_conf_build(want,sizeof want))>0) md5_hex((const uint8_t*)want,(size_t)wn,mw);
        int fd=open(NET_CONF,O_RDONLY); int hn=fd>=0?(int)read(fd,have,sizeof have):-1; if(fd>=0) close(fd);
        if(hn>0) md5_hex((const uint8_t*)have,(size_t)hn,mh);
        printf("merakiplus " MERAKIPLUS_VERSION " net-status (read-only)\n");
        printf("probe : " NET_IF " %s, wpa_supplicant pid %d, udhcpc pid %d, carrier %d, ip %s, default route %d\n",p.iface?(p.ifup?"present (UP)":"present (DOWN)"):"missing",p.wpa_pid,p.dhcp_pid,p.carrier,net_ipstr(p.ip,ipb,sizeof ipb),p.gw);
        printf("config: %s -> %s; ssid length %d, password length %d\n",cfg_path,NC_TXT[net.cfg],net.ssid_n,(int)strlen(net.psk));
        printf("conf  : " NET_CONF " on disk %d bytes md5 %s\n",hn,mh);
        printf("conf  : built from config.json %d bytes md5 %s -> %s\n",wn,mw,(wn>0&&hn==wn&&!memcmp(have,want,(size_t)wn))?"IDENTICAL (would not be written)":"DIFFERS (would be written on bring-up)");
        printf("dom   : %s\n",(p.wpa_pid>0&&p.ifup&&p.carrier&&p.ip)?"healthy stack - nothing to do":(p.wpa_pid>0?"wpa_supplicant alive - not touched (udhcpc if needed)":"wpa_supplicant not running - bring-up"));
        return 0;
    }
    if(!strcmp(argv[1],"--net-watch")){
        int secs=argc>2?atoi(argv[2]):0; net_echo=1;
        net_start(now,!getenv("MERAKIPLUS_NONET"));
        ms_t end=secs>0?now+(ms_t)secs*1000:0;
        while(run&&(!end||now_ms()<end)){ net_step(now_ms()); struct timespec sl={0,20*1000*1000}; nanosleep(&sl,NULL); }
        net_now=now_ms(); net_log("net watchdog (--net-watch) done: heal %d, wpa starts %d, udhcpc starts %d, conf writes %d, errors %d",net.n_heal,net.n_wpa,net.n_dhcp,net.n_conf,net.n_fail);
        return 0;
    }
    fprintf(stderr,"usage: merakiplus [--net-status | --net-watch [seconds]]\n"); return 2;
}
#define NET_CLI(argc,argv) if(argc>1) return net_cli(argc,argv);
#define NET_ENABLED (!getenv("MERAKIPLUS_NONET")&&!sysroot[0])      /* under a test root (MERAKIPLUS_ROOT) the real network is not touched */
#endif
#ifdef SIM_RT
#define NET_CLI(argc,argv) (void)argc; (void)argv;
#define NET_ENABLED sim_net_on
#endif
int main(int argc,char**argv){
    signal(SIGINT,onsig); signal(SIGTERM,onsig);
    NET_CLI(argc,argv)
    dev_release=dev_release_impl; dev_reacquire=dev_reacquire_impl;
    const char*e;
    if((e=getenv("MERAKIPLUS_CONFIG"))) snprintf(cfg_path,sizeof cfg_path,"%s",e);
    if((e=getenv("MERAKIPLUS_BAKDIR"))) snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s",e);
    if(getenv("MERAKIPLUS_NOBL")) bl_enabled=0;
    if(getenv("MERAKIPLUS_READONLY")) ro_mode=1;
    if((e=getenv("MERAKIPLUS_ROOT"))) snprintf(sysroot,sizeof sysroot,"%s",e);
    if((e=getenv("MERAKIPLUS_LANGFILE"))) snprintf(lang_path,sizeof lang_path,"%s",e);
    if((e=getenv("MERAKIPLUS_SCHEDFILE"))) snprintf(sched_path,sizeof sched_path,"%s",e);   /* v6.16 */
    if((e=getenv("MERAKIPLUS_SOUNDFILE"))) snprintf(snd_path,sizeof snd_path,"%s",e);       /* v6.18 */
    if(getenv("MERAKIPLUS_SWITCH_DRY")) sw_dry=1;
    if(getenv("MERAKIPLUS_NOBOOTPUSH")) bp_on=0;
    SIM_ENV
    bs.self_pid=(int)getpid();
    lang_load();                           /* v6.3: UI language BEFORE the first frame (splash); missing/unreadable/unknown -> English */
    ota_init(now_ms());                    /* v6.21: release directory, launcher environment, OTA state, vendor-update traces */
    gfx_init(); settings_load();
    boot_refresh();                        /* warm up (binary marker scan of slot B) during splash, not on the first tap */
    uart_open(); fb_open(); inp_open();
#ifdef DEVICE_RT
    kbd_open();                            /* v6.14: the power button (k62091) */
#endif
    SIM_START
    ms_t t0=now_ms();
    net_start(t0,NET_ENABLED);             /* v6.7: wifi at startup + self-heal (first evaluation in the first loop iteration) */
    proto_keepawake(t0,1);                 /* power=1 + word1=0 (awake) */
#ifdef DEVICE_RT
    if(bp_on&&!ro_mode) boot_push_start(1);   /* v6.5: boot push like stock (sub1 len30 + sub112 len3) w/ activity guard */
#endif
    RL full; full.n=1; full.r[0]=FULL; pres_prev=full;
    while(run){
        ms_t now=now_ms();
        uint8_t b[512]; int n;
        while((n=read(ufd,b,sizeof b))>0) rx_feed(b,n,now);
        rx_feed(b,0,now);                  /* v6.5: the resync watchdog must also run when nothing arrives */
        wq_step(now);
        loop_poll(now);                    /* v6.6: the timers live in lt (64 bit) — see loop_poll() */
        proto_keepawake(now,0);
        uart_flush();
        loop_graph(now);
        inp_poll(now);
#ifdef DEVICE_RT
        kbd_poll(now);                     /* v6.14: the power button */
#endif
        pw_step(now);                      /* v6.14: hold -> off frames / OFF / TURNING ON… */
        sched_step(now);                   /* v6.16: pre-arm the pre-heat alarm, one-shot use, auto off */
        snd_tick(now);                     /* v6.18: start jingle + per-boiler ready tones (display/audio only, no frames) */
        ota_step(now);                     /* v6.21: healthy marker + one-time notices */
#ifdef DEVICE_RT
        ota_reload_step(now);              /* v6.21: exec-reload into a newly installed release, when idle */
#endif
        net_step(now);                     /* v6.7: non-blocking; does nothing between two probes */
#ifdef DEVICE_RT
        boot_push_step(now);
        repush_step(now);                  /* v6.6: MCU restart (0x89 again / defaults signature) -> boot push from scratch */
#endif
        SIM_TICK(now)
        if(!run) break;                    /* REVERT / runtime switch: after step 1 fb/UART are closed — do not touch them any more */
        if(loop_render_due(now)){
            struct timespec fa,fb; clock_gettime(CLOCK_MONOTONIC,&fa);
            ui_frame(now);
            clock_gettime(CLOCK_MONOTONIC,&fb);
            loop_rendered(now,(fb.tv_sec-fa.tv_sec)*1e3f+(fb.tv_nsec-fa.tv_nsec)/1e6f);
            SIM_FRAME(now)
        }
        struct timespec sl={0, 2*1000*1000}; nanosleep(&sl,NULL);
    }
    /* run.sh mode: last frame (fb is still open; on a real B->A switch it has already been shown and fb closed) */
    if(view==V_BYE&&fbmem){ tr.type=TR_NONE; tr_was=0; static_dirty=1; force_all=1; ui_frame(now_ms()); }
    if(sw_child) printf(TR(TX_O_SWITCHED),sw_child);
    if(sel>=0&&(SET[sel].flags&SF_BL)) bl_set(SET[sel].val);   /* undo an unfinished brightness preview */
    /* same reminder as the exit screen — and only when it is true (a stock UI w/ a stale RAM mirror is waiting) */
    boot_refresh();
    if(restart_pending()) fputs(TR(TX_O_RESTART),stdout);
    else if(cfg_writes) printf(TR(TX_O_CFGCHANGED),cfg_writes);
    if(wq.n_block) printf(TR(TX_O_BLOCKED),wq.n_block);
    fflush(stdout); fflush(stderr);
    SIM_EXIT
    if(ufd>=0) close(ufd);
    if(ifd>=0){ INP_UNGRAB close(ifd); }
#ifdef DEVICE_RT
    if(kfd>=0) close(kfd);
#endif
    if(bl_is_off) bl_power(1);             /* v6.14: never leave the screen dark (run.sh -> the stock UI continues) */
#ifdef DEVICE_RT
    if(!ota.marked) ota_mark_write(1);     /* v6.21: an orderly exit is not a failed start (the launcher's crash count) */
#endif
    if(fbmem) munmap(fbmem,fblen);
    if(ffd>=0) close(ffd);
    return MAIN_RC;
}
#else
/* ================= PREVIEW: synthetic scenarios -> raw fb files (no device) ================= */
static ms_t sim_now=200000, sim_t0=0; static int sim_link=1;
static int pv_lang=LANG_EN;                /* --lang en|da: the language previews/selftest run in */
static int pv_sched=0;                     /* v6.16: run sched_step() in sim_run() */
static void (*sim_tick)(ms_t)=NULL;
static const uint32_t* sim_last=NULL;
/* v6.22: the controller's firmware string in words 80..87 (a) and its copy in 88..95 (b; NULL = same as a), lo byte first — as the
 * 2026-11-16 capture shows "V5.1.4". The preview/selftest base state is the reference machine (V5.1.4 = dosing). */
static void pv_mcu_ver(const char*a,const char*b){
    char x[16],y[16]; memset(x,0,16); memset(y,0,16); if(!b) b=a; memcpy(x,a,strlen(a)<16?strlen(a):16); memcpy(y,b,strlen(b)<16?strlen(b):16);
    for(int k=0;k<8;k++){ reg[80+k]=(uint16_t)((unsigned char)x[2*k]|((unsigned char)x[2*k+1]<<8)); reg[88+k]=(uint16_t)((unsigned char)y[2*k]|((unsigned char)y[2*k+1]<<8)); reg_seen[80+k]=reg_seen[88+k]=1; }
}
static void sim_base(void){
    memset(reg,0,sizeof reg); for(int i=0;i<REGN;i++) reg_seen[i]=1;
    reg[0]=1; reg[1]=0; reg[33]=0x0101; reg[2]=1; reg[4]=185; reg[11]=870; reg[14]=3; reg[17]=400; reg[23]=360; reg[25]=650;
    reg[40]=999; reg[41]=999; reg[42]=702; reg[47]=1297; reg[48]=226; reg[49]=236; reg[61]=50; reg[50]=5; reg[58]=5;
    reg[37]=16; reg[71]=41; reg[72]=39; reg[112]=0x0104; reg[113]=0x0605; reg[114]=70;
    reg[62]=100; reg[63]=100;                                         /* v6.18: both boilers at their heat stage 100 (hot) */
    pv_mcu_ver("V5.1.4",NULL);                                        /* v6.22: the reference controller (dosing) */
    rx_ok=8794; rx_bad=0; last_bad_ms=-100000;
}
static void sim_run(ms_t ms){
    static ms_t lg=0; ms_t end=sim_now+ms;
    while(sim_now<end){
        sim_now+=33;
        if(sim_tick) sim_tick(sim_now-sim_t0);
        if(sim_link){ last_rx_ms=sim_now; rx_ok++; }
        if(sim_now-lg>=100){ lg=sim_now; graph_push(sim_now,reg[51],wsrc_tick(sim_now)); }
        wq_step(sim_now);
        pw_step(sim_now);                  /* v6.14 */
        if(pv_sched) sched_step(sim_now);  /* v6.16 (only where a scenario/selftest switches it on) */
        snd_tick(sim_now);                 /* v6.18: the sound engine (the preview build only logs) */
        sim_last=ui_frame(sim_now);
    }
}
#define PV_CLK (days_civil(2026,11,18)*86400+15*3600+35*60+28)   /* v6.16: preview clock = 2026-11-18 15:35:28 UTC (16:35:28 in zone #48) */
static void sim_reset(void){
    clk_fake=PV_CLK; idle_t0=0; ao_warned=0; memset(&arm,0,sizeof arm);
    view=V_MAIN; page=PG_HOME; sel=-1; sec_open=-1; diag_from=PG_SYS; memset(scroll,0,sizeof scroll); memset(&tch,0,sizeof tch);
    toast.dur=0; rip.on=0; tr.type=0; memset(&sav,0,sizeof sav); memset(ov_prev,0,sizeof ov_prev);
    xsel=-1; x_from_switch=0; x_pending=0; bye_mode=0; run=1; bs.t=-100000; lang_set(pv_lang);
    act.raw=PH_OFFLINE; act.ph=PH_OFFLINE; act.last_ph=PH_READY; act.pending=ACT_NONE; act.gkind=-1; act.t_done=-100000; act.t_col=-100000;
    g_n=0; g_head=0; static_dirty=1; force_all=1; garc.f_d=0; parc.f_d=0; g_prim=0; gr.resets=gr.reanchors=0; gr.t0=-100000;
    cond_reset(); memset(&hf,0,sizeof hf); cd.raw=0;               /* v6.9: the filters restart (the display state from DIAGNOSE is reset) */
    wsl.src=wsl.cand=39; wsl.n_sw=0; wsl.req=ACT_NONE; wsl.t_req=-100000; g_lsrc=39;
    sim_now+=100000; ui_start=sim_now-20000; ui_last=sim_now; sim_tick=NULL; sim_link=1; sim_t0=sim_now;
    memset(&wq,0,sizeof wq); wq.sim_ack=1; sim_base(); settings_load();
    memset(&bp,0,sizeof bp); bp.state=BP_DONE; bp.ra=bp.rb=BPR_OK; bp.runs=1; rx_resync=0;   /* v6.5: normal operation — the boot push has completed */
    memset(&wd,0,sizeof wd);                                                                   /* v6.22 */
    memset(&stm,0,sizeof stm); memset(&stp,0,sizeof stp); stp.kind=ACT_NONE; memset(&rps,0,sizeof rps); memset(&lt,0,sizeof lt); ver_n=0; rx_n112=0;      /* v6.6 */
    memset(&pw,0,sizeof pw); pw.kbd_ok=1; snprintf(pw.node,sizeof pw.node,"%s · %s",INP_KBD,KBD_NAME); bl_is_off=0; ka_next=0;   /* v6.14: the button node as on the device */
    nm_reset(0); memset(&net,0,sizeof net); net.on=1; net.cfg_logged=-1; nh_probe(&net.p,sim_now,1); net.state=NS_UP; net.booted=net.was_up=net.gw_seen=1;   /* v6.7: healthy stack (DIAGNOSE: 841 / 192.168.1.100 / UP) */
    memset(&snd,0,sizeof snd); snd_uptime_ovr=86400;               /* v6.18: the sound engine restarts; the display has been up for a day (no cold boot) */
}
static void tick_brew(ms_t t){
    if(t<300){ reg[33]=0x0101; reg[51]=2; reg[52]=0; reg[39]=0; }
    else if(t<6950){ reg[33]=0x1107; reg[51]=350+(t/97)%17; reg[52]=360; reg[50]=1533; reg[39]=(uint16_t)(int16_t)-3; }
    else if(t<28900){ reg[33]=0x1307; reg[51]=(t<7300)?1481:1379-(int)((t-7300)*54/21600)+(t/131)%9; reg[52]=(t<8000)?640:150; reg[50]=1046;
        reg[39]= t<9200? 0 : (uint16_t)((t-9200)*344/(28900-9200)); }
    else { reg[33]=0x0101; reg[51]=t<29600?(int)(1300-(t-28900)*1.8):12; reg[52]=0; reg[50]=5; reg[39]=(uint16_t)(344+ (t<30500?(t-28900)*22/1600:22)); }
}
static void tick_grind(ms_t t){
    if(t<400){ reg[33]=0x0101; reg[38]=0; }
    else if(t<9900){ reg[33]=0x0141; reg[38]=(uint16_t)((t-400)*184/9500); reg[58]=640+(t/113)%40; }
    else { reg[33]=0x0101; reg[38]=186; reg[58]=5; }
}
/* v6.15: a 250 g cup on the brew scale at rest for 20 s, then a brew; the MCU tares word 39 at the start (cb_late ms after the bits,
 * 0 = the same sample, as mcu_sim). cb_flush0..1 = a wand flush (steam bits 0x4020) during the brew. Pressure 350..366 / 1340..1348. */
static int cb_late=0, cb_flush0=0, cb_flush1=0;
static void tick_cupbrew(ms_t t){
    if(t<20000){ reg[33]=0x0101; reg[51]=2; reg[52]=0; reg[39]=2500; }
    else if(t<24000){ reg[33]=0x1107; reg[51]=350+(t/97)%17; reg[52]=360; reg[50]=1533; reg[39]=t<20000+cb_late?2500:(uint16_t)(int16_t)-3; }
    else if(t<44000){ reg[33]=0x1307; reg[51]=1340+(t/131)%9; reg[52]=150; reg[50]=1046; reg[39]=t<25000?0:(uint16_t)((t-25000)*360/19000); }
    else { reg[33]=0x0101; reg[51]=12; reg[52]=0; reg[50]=5; reg[39]=362; }
    if(cb_flush1>cb_flush0&&t>=cb_flush0&&t<cb_flush1){ reg[33]|=0x4020; reg[24]=0; reg[44]=1; } else reg[44]=0;
}
static void tick_water(ms_t t){
    if(t<300){ reg[33]=0x0101; reg[39]=0; }
    else if(t<9300){ reg[33]=0x030B; reg[39]=(uint16_t)((t-300)*400/9000); reg[51]=1290+(t/89)%21; reg[52]=520; }
    else { reg[33]=0x0101; reg[39]=404; reg[51]=3; reg[52]=0; }
}
/* v6.6: display steam as in STEAMLIVE §5 — word 24 = 1 from the start write, bits 5+14, the wand switch (word 44 bit0 / word 46)
 * is NOT activated; at the target the MCU clears the bits and then word 24, and the probe keeps rising ~3.9 °C over ~11 s */
static void tick_steam(ms_t t){
    reg[44]=0x000C; reg[46]=0;
    if(t<300){ reg[33]=0x0101; reg[24]=0; }
    else { int m=226+(int)((t-300)*14/1000); reg[24]=1; reg[33]=0x4121; reg[48]=(uint16_t)(m>650?650:m); reg[47]=1262-(t/400)%9;
           if(m>=650){ ms_t ts=300+(650-226)*1000/14, d=t-ts; reg[33]=0x0101; reg[24]=0; reg[48]=(uint16_t)(650+(d>=11000?39:(int)(d*39/11000))); } }
}
/* momentary wand/flush (STEAMLIVE §6.2): bits 5+14 WITHOUT word 24 = 1; word 44 bit0 + word 46 follow approx. 0.4 s later; the probe
 * is heated past the target (650) to 695, and the steam continues until the wand is released */
static void tick_wand(ms_t t){
    reg[24]=0;
    if(t<300){ reg[33]=0x0101; reg[44]=0x000C; reg[46]=0; }
    else { int on=t>=700, m=514+(int)((t-300)*60/1000); reg[33]=0x4121; reg[44]=on?0x000D:0x000C; reg[46]=(uint16_t)on; reg[48]=(uint16_t)(m>695?695:m); reg[47]=(uint16_t)(1295-(t-300)*116/10000); }
}
/* flush DURING extraction (STEAMLIVE §7): 0x1307 | 0x4020 = 0x5327 for 3.1 s; the brew continues undisturbed */
static void tick_brew_wand(ms_t t){
    tick_brew(t); reg[24]=0; reg[44]=0x000C; reg[46]=0; reg[48]=514;
    if(t>=20600&&t<23700){ int on=t>=21000, m=514+(int)((t-20600)*60/1000); reg[33]=0x5327; reg[44]=on?0x000D:0x000C; reg[46]=(uint16_t)on; reg[48]=(uint16_t)(m>695?695:m); }
    else if(t>=23700){ int m=667-(int)((t-23700)/50); reg[48]=(uint16_t)(m<420?420:m); }
}
/* v6.13: brew (extraction) + LATCHED steam concurrently (word 24 = 1, bits 5+14 on top of the brew bits) — two STOP buttons */
static void tick_brew_latched(ms_t t){
    tick_brew(t); reg[24]=1; reg[44]=0x000C; reg[46]=0; reg[48]=(uint16_t)(300+t/60>640?640:300+t/60); reg[47]=1262;
    if(t>=300&&t<28900) reg[33]|=0x4020;
}
static void dump(const char*dir,const char*name){
    char path[400]; snprintf(path,sizeof path,"%s/%s.bin",dir,name);
    FILE*f=fopen(path,"wb"); if(!f){ perror(path); exit(1); }
    fwrite(sim_last,1,(size_t)W*H*4,f); fclose(f); printf("preview %s\n",path);
}
static void tap(int x,int y){ ui_touch(TE_DOWN,x,y,sim_now); sim_run(70); ui_touch(TE_UP,x,y,sim_now); }
static int copy_file(const char*src,const char*dst){
    char b[CFG_MAX]; int fd=open(src,O_RDONLY); if(fd<0) return 0; int n=read(fd,b,sizeof b); close(fd); if(n<=0) return 0;
    return write_all(dst,b,n,0);
}
static double msnow(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1e3+ts.tv_nsec/1e6; }

/* ---- v6.2: mock root for the boot/switch logic (preview/selftest only; never touches real paths) ---- */
enum { MB_NONE=0, MB_OURS, MB_VENDOR, MB_V61, MB_OURS_EDGE, MB_OURS_NOEXEC };
static void mk_p(const char*root,const char*rel){
    char p[520]; snprintf(p,sizeof p,"%s%s",root,rel);
    for(char*q=p+1;*q;q++) if(*q=='/'){ *q=0; mkdir(p,0755); *q='/'; }
    mkdir(p,0755);
}
static int mock_put(const char*root,const char*rel,const char*data,int n,int mode){
    char p[520]; snprintf(p,sizeof p,"%s%s",root,rel);
    int fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,mode); if(fd<0) return 0;
    if(n>0&&write(fd,data,n)!=n){ close(fd); return 0; }
    close(fd); chmod(p,mode); return 1;
}
static int mock_exists(const char*root,const char*rel){ char p[520]; struct stat st; snprintf(p,sizeof p,"%s%s",root,rel); return lstat(p,&st)==0; }
static void mock_rm(const char*root,const char*rel){ char p[520]; snprintf(p,sizeof p,"%s%s",root,rel); unlink(p); }
static void mock_reset(const char*root){
    if(!strstr(root,"_preview_root")&&!strstr(root,"merakiplus_v6_selftest")){ fprintf(stderr,"mock root: unexpected path %s\n",root); exit(2); }
    char c[640]; snprintf(c,sizeof c,"rm -rf '%s'",root); if(system(c)!=0){}
    mk_p(root,"/customer/libs"); mk_p(root,"/new_customer/libs"); mk_p(root,"/proc/self"); mk_p(root,"/proc/sys/kernel/random"); mk_p(root,"/tmp");
    static const char stock[]="\x7f" "ELF mock: vendor stock demo_v9 (slot A)\n";
    mock_put(root,SLOT_A_EXE,stock,sizeof stock-1,0755);
    static const char bid[]="0f6c1b1e-52a7-4f0b-9a3d-7e1c5a9b2d44\n";
    mock_put(root,"/proc/sys/kernel/random/boot_id",bid,sizeof bid-1,0644);
    bs.m_valid=0;
}
static void mock_slot_b(const char*root,int mounted,int kind){
    char m[400]; int n=snprintf(m,sizeof m,"rootfs / rootfs rw 0 0\nubi0:rootfs / ubifs ro,relatime 0 0\nproc /proc proc rw,relatime 0 0\nubi0:customer /customer ubifs rw,relatime 0 0\n%s",
                                mounted?"ubi0:new_customer /new_customer ubifs rw,relatime 0 0\n":"");
    mock_put(root,"/proc/mounts",m,n,0644);
    mock_rm(root,SLOT_B_EXE); bs.m_valid=0;
    if(kind==MB_NONE) return;
    static char big[66000]; int bn;
    if(kind==MB_VENDOR) bn=snprintf(big,sizeof big,"\x7f" "ELF mock: vendor demo_v9 in slot B (OTA) - no merakiplus marker\n");
    else if(kind==MB_V61) bn=snprintf(big,sizeof big,"\x7f" "ELF mock: merakiplus V6.1 ... %s %%d gang(e) ...\n",MERAKIPLUS_MARK_V61);
    else if(kind==MB_OURS_EDGE){ memset(big,'.',sizeof big); bn=65530+snprintf(big+65530,sizeof big-65530,"%s\n",MERAKIPLUS_MARK); }   /* the marker straddles the 64 KiB read limit */
    else bn=snprintf(big,sizeof big,"\x7f" "ELF mock: merakiplus in slot B ... %s ...\n",MERAKIPLUS_MARK);
    mock_put(root,SLOT_B_EXE,big,bn,kind==MB_OURS_NOEXEC?0644:0755);
}
static void mock_flag(const char*root,int on){ if(on) mock_put(root,BOOT_FLAG,"",0,0644); else mock_rm(root,BOOT_FLAG); }
static void mock_self(const char*root,const char*exe){ char p[520]; snprintf(p,sizeof p,"%s/proc/self/exe",root); unlink(p); if(symlink(exe,p)!=0){} }
static void mock_proc_rm(const char*root,int pid){
    char r[64]; static const char*const f[]={"exe","comm","cmdline","stat"};
    for(int i=0;i<4;i++){ snprintf(r,sizeof r,"/proc/%d/%s",pid,f[i]); mock_rm(root,r); }
    char p[520]; snprintf(p,sizeof p,"%s/proc/%d",root,pid); rmdir(p);
}
/* state: 'T' = stopped (SIGSTOP), 'S'/'R' = running, 'Z' = zombie (empty cmdline) */
static void mock_proc(const char*root,int pid,const char*exe,const char*comm,char state,int64_t start_s){
    char r[64],b[256],p[520]; mock_proc_rm(root,pid);
    snprintf(r,sizeof r,"/proc/%d",pid); mk_p(root,r);
    snprintf(p,sizeof p,"%s/proc/%d/exe",root,pid); if(state!='Z'&&symlink(exe,p)!=0){}
    snprintf(r,sizeof r,"/proc/%d/comm",pid); int n=snprintf(b,sizeof b,"%s\n",comm); mock_put(root,r,b,n,0644);
    snprintf(r,sizeof r,"/proc/%d/cmdline",pid); n=(state=='Z')?0:snprintf(b,sizeof b,"./%s",comm); mock_put(root,r,b,n,0644);
    snprintf(r,sizeof r,"/proc/%d/stat",pid);
    n=snprintf(b,sizeof b,"%d (%s) %c 1 %d %d 0 -1 4194560 0 0 0 0 0 0 0 0 20 0 1 0 %lld 1404928 91 4294967295\n",pid,comm,state,pid,pid,(long long)(start_s*100));
    mock_put(root,r,b,n,0644);
}
static void mock_swlog(const char*root,const char*text){ if(text) mock_put(root,SW_LOG,text,(int)strlen(text),0644); else mock_rm(root,SW_LOG); }
#define MOCK_SELF_PID 412
#define MOCK_STOCK_PID 187
#define MOCK_SW_PID 301
static const char MOCK_SWLOG[]=
 "[2026-11-17 12:40:41 UTC] switcher started (pid 301): gesture = 3\xC3\x97long-press \xE2\x89\xA5" "1500 ms in corner x<90,y<90 within 6 s\n"
 "[2026-11-17 12:40:41 UTC] input: /dev/input/event0 (name 'TECHWIN_TS', raw x 0..4095 y 0..4095) \xE2\x80\x94 READ-ONLY, no grab\n"
 "[2026-11-17 12:41:02 UTC] gesture: valid corner long-press counted (1/3)\n"
 "[2026-11-17 12:41:04 UTC] gesture: valid corner long-press counted (2/3)\n"
 "[2026-11-17 12:41:06 UTC] GESTURE TRIGGERED + mode A \xE2\x86\x92 runtime switch to merakiplus\n"
 "[2026-11-17 12:41:06 UTC] switch A\xE2\x86\x92" "B: SIGSTOP pid 187 (watchdog still sees the PID)\n"
 "[2026-11-17 12:41:06 UTC] switch A\xE2\x86\x92" "B: launched B pid 412 (/new_customer/demo_v9)\n"
 "[2026-11-17 12:41:07 UTC] switch A\xE2\x86\x92" "B: B verified (exe link) \xE2\x80\x94 SIGKILL pid 187\n"
 "[2026-11-17 12:41:07 UTC] switch A\xE2\x86\x92" "B DONE: merakiplus running, stock A exited, no reboot\n";
/* default state for previews: booted in slot B (flag set), merakiplus = /new_customer/demo_v9, switcher running */
static void mock_boot_b(const char*root){
    mock_reset(root); mock_slot_b(root,1,MB_OURS); mock_flag(root,1); mock_self(root,SLOT_B_EXE);
    mock_proc(root,MOCK_SW_PID,"/new_customer/merakiplus/boot/switcher","switcher",'S',12);
    mock_proc(root,MOCK_SELF_PID,SLOT_B_EXE,"demo_v9",'R',95);
    mock_swlog(root,MOCK_SWLOG);
    bs.self_pid=MOCK_SELF_PID; bs.t=-100000;
}
/* run.sh mode: A boot (no flag), merakiplus run from /new_customer/merakiplus, stock UI SIGSTOPped, no switcher */
static void mock_runsh(const char*root,int64_t stock_start_s){
    mock_reset(root); mock_slot_b(root,1,MB_OURS); mock_flag(root,0); mock_self(root,"/new_customer/merakiplus/merakiplus");
    mock_proc(root,MOCK_STOCK_PID,SLOT_A_EXE,"demo_v9",'T',stock_start_s);
    mock_proc(root,MOCK_SELF_PID,"/new_customer/merakiplus/merakiplus","merakiplus",'R',5200);
    mock_swlog(root,NULL);
    bs.self_pid=MOCK_SELF_PID; bs.t=-100000;
}
static int file_has(const char*path,const char*needle){
    static char b[65536]; int fd=open(path,O_RDONLY); if(fd<0) return 0; int n=read(fd,b,sizeof b-1); close(fd); if(n<0)n=0; b[n]=0; return strstr(b,needle)!=NULL;
}
static int file_lines(const char*path){ FILE*f=fopen(path,"r"); if(!f) return -1; int n=0,c; while((c=fgetc(f))!=EOF) if(c=='\n') n++; fclose(f); return n; }
static void file_print(const char*path,const char*pre){ FILE*f=fopen(path,"r"); if(!f) return; char ln[600]; while(fgets(ln,sizeof ln,f)) printf("%s%s",pre,ln); fclose(f); }
static int dir_count(const char*path){ DIR*d=opendir(path); if(!d) return -1; int n=0; struct dirent*de; while((de=readdir(d))) if(strcmp(de->d_name,".")&&strcmp(de->d_name,"..")) n++; closedir(d); return n; }

static int copy_big(const char*src,const char*dst,int mode){
    int a=open(src,O_RDONLY); if(a<0) return 0; int b=open(dst,O_WRONLY|O_CREAT|O_TRUNC,mode); if(b<0){ close(a); return 0; }
    static char buf[65536]; int n,ok=1; while((n=read(a,buf,sizeof buf))>0) if(write(b,buf,n)!=n){ ok=0; break; }
    close(a); close(b); chmod(dst,mode); return ok&&n==0;
}
/* mock "stock" for the launch selftest: a copy of this binary, placed as <root>/customer/demo_v9 and started
 * by switch_launch(). It reports what a real demo_v9 would inherit, and stays alive a little longer. */
static int mock_stock_main(const char*argv0){
    const char*out=getenv("MERAKIPLUS_MOCK_STOCK"),*rel=getenv("MERAKIPLUS_MOCK_RELEASED"),*ld=getenv("LD_LIBRARY_PATH");
    char cwd[600]=""; if(!getcwd(cwd,sizeof cwd)) cwd[0]=0;
    int nfd=0; for(int fd=3;fd<256;fd++) if(fcntl(fd,F_GETFD)!=-1) nfd++;
    struct stat a,b; int nul=stat("/dev/null",&a)==0; for(int fd=0;fd<3&&nul;fd++) if(fstat(fd,&b)!=0||b.st_rdev!=a.st_rdev) nul=0;
    int released=rel&&access(rel,F_OK)==0;
    FILE*f=fopen(out,"w");
    if(f){ fprintf(f,"pid=%d sid=%d argv0=%s cwd=%s extra_fds=%d stdio_null=%d released_before_start=%d\nLD=%s\n",(int)getpid(),(int)getsid(0),argv0,cwd,nfd,nul,released,ld?ld:""); fclose(f); }
    if(getenv("MERAKIPLUS_MOCK_STOCK_FAIL")) return 3;
    usleep(900000); return 0;
}
static const char*self_bin=NULL;
static int st_rel=0, st_reacq=0, st_relfd=-1; static const char*st_relfile=NULL;
static void st_release_hook(void){ st_rel++; if(st_relfd>=0){ close(st_relfd); st_relfd=-1; } if(st_relfile){ int fd=open(st_relfile,O_WRONLY|O_CREAT,0644); if(fd>=0) close(fd); } }
static void st_reacq_hook(void){ st_reacq++; }
static double msnow(void);
static int st_fail=0, st_n=0;
static void st_check(const char*name,int ok){ printf("  [%s] %s\n",ok?"PASS":"FAIL",name); st_n++; if(!ok) st_fail++; }
static int hexeq(const uint8_t*b,int n,const char*hex){
    int i=0; while(*hex){ while(*hex==' ')hex++; if(!*hex) break; unsigned v; if(sscanf(hex,"%2x",&v)!=1) return 0; hex+=2; if(i>=n||b[i]!=v) return 0; i++; }
    return i==n;
}
/* independent audit of a queued frame (does NOT reuse frame_policy): no trigger word != 0 beyond 'allowed' */
static int frame_trig_audit(const WFrame*f,uint32_t allowed){
    for(int k=0;k<f->len;k++){
        int idx=f->sub+k; if(idx>=32||f->w[k]==0) continue;
        if(idx==26||idx==27||idx==30) return 0;
        if((idx==3||idx==6||idx==9||idx==16||idx==19||idx==22||idx==24)&&!(allowed&WBIT(idx))) return 0;   /* v6.22: + 19 (manual water) */
    }
    return 1;
}
static int frame_covers(const WFrame*f,int idx){ return idx>=f->sub&&idx<f->sub+f->len; }
static int q_hex(int k,const char*hex){ uint8_t b[80]; int n=build_write(b,wq.q[k].sub,wq.q[k].w,wq.q[k].len); return hexeq(b,n,hex); }
static int rej(uint8_t sub,const uint16_t*w,int len,uint32_t intent,int why){ int n0=wq.n; int r=wq_push_t(sub,w,len,intent); int ok=(r==0&&wq_rej==why&&wq.n==n0); wq.n=n0; return ok; }
#define W1(v) (uint16_t[1]){(uint16_t)(v)}
/* like chord_half, but inside the gauge ring's inner edge (r 204) — for views where the ring is drawn */
static float ring_half(int y,int h){ int d0=abs(y-CY), d1=abs(y+h-CY); int d=d0>d1?d0:d1; return d>=204?0:sqrtf((float)(204*204-d*d)); }
static float chord_half(int y,int h){ int d0=abs(y-CY), d1=abs(y+h-CY); int d=d0>d1?d0:d1; return d>=RSAFE?0:sqrtf((float)(RSAFE*RSAFE-d*d)); }
static void sim_ready(ms_t now){   /* minimal "READY" state without simulator */
    memset(reg,0,sizeof reg); for(int i=0;i<REGN;i++) reg_seen[i]=1;
    reg[0]=1; reg[33]=0x0101; reg[11]=870; reg[40]=900; reg[23]=360; reg[112]=0x0104; reg[113]=0x0605; reg[114]=70; last_rx_ms=now;
    pv_mcu_ver("V5.1.4",NULL);                                        /* v6.22 */
}
/* ---- v6.3: i18n helpers ---- */
#define LAY(w,lim,what,st) do{ if((w)>(lim)){ ok=0; printf("     [%s] %s: '%s' is %.0f px, room %.0f px\n",LANG_CODE[ui_lang],what,st,(double)(w),(double)(lim)); } }while(0)
static const char* stl(const char*name){ static char b[4][420]; static int k; char*o=b[k++&3]; snprintf(o,420,"[%s] %s",LANG_CODE[ui_lang],name); return o; }
/* pixel budget per string class (font, max width) — same numbers as the drawing code uses */
static const struct { int font,maxw; const char*what; } KB[NKLASS]={
 [KT]={F_XS,TOAST_TW,"toast"},[KSUB]={F_XS,SUB_W-8,"HOME subline"},[KLAB]={F_XS,254,"HOME label (field 260)"},[KCHIP]={F_S,150,"phase chip"},
 [KBTN]={F_XS,104,"button"},[KBTNS]={F_S,104,"button"},[KTITLE]={F_M,224,"page title"},[KPSUB]={F_XS,298,"page subtitle"},[KSPL]={F_XS,SPLASH_W-8,"tagline"},
 [KDL]={F_XS,170,"diagnose label"},[KDV]={F_S,168,"diagnose value"},[KDF]={F_XS,DG_FULL_W,"full-width line"},[KNOTE]={F_XS,392,"note (field 400)"},
 [KF1]={F_XS,264,"footer 1 (clear of the gauge ends)"},[KF2]={F_XS,269,"footer 2"},[KXSUB]={F_XS,300,"row subline"},[KST]={F_XS,348,"status (field 360)"},
};
static const char*const KNAME[NKLASS]={"-","toast","home-sub","home-label","phase-chip","button","button","page-title","page-sub","tagline","diag-label","diag-value","full-line","note","foot-1","foot-2","row-sub","status","console"};
/* the printf conversions in a string, in order ("%s|%.34s|%ld|") */
static void fmt_sig(const char*s,char*o,size_t n){
    size_t k=0; o[0]=0;
    for(;*s;s++){ if(*s!='%') continue; if(s[1]=='%'){ s++; continue; }
        const char*e=s+1; while(*e&&!strchr("sduXxfc",*e)) e++; if(!*e) break;
        for(const char*q=s;q<=e&&k+2<n;q++) o[k++]=*q; if(k+2<n) o[k++]='|'; o[k]=0; s=e; }
}
/* worst-case fill: %s -> "888.8", integer -> "88888", %f -> "88.8" (for width measurement) */
static void fmt_fill(const char*s,char*o,size_t n){
    size_t k=0;
    for(;*s&&k+8<n;s++){
        if(*s!='%'){ o[k++]=*s; continue; } if(s[1]=='%'){ o[k++]='%'; s++; continue; }
        const char*e=s+1; while(*e&&!strchr("sduXxfc",*e)) e++; if(!*e) break;
        const char*r=*e=='s'?"888.8":(*e=='f'?"88.8":"88888"); while(*r) o[k++]=*r++; s=e; }
    o[k]=0;
}
/* number signature: all digits + '.'/',' that sit BETWEEN two digits — must be identical in all languages */
static void num_sig(const char*s,char*o,size_t n){
    size_t k=0;
    for(const char*p=s;*p&&k+1<n;p++){
        if(isdigit((unsigned char)*p)) o[k++]=*p;
        else if((*p=='.'||*p==',')&&p>s&&isdigit((unsigned char)p[-1])&&isdigit((unsigned char)p[1])) o[k++]=*p; }
    o[k]=0;
}
/* visible text (upper-cased, without code spans and control chars) — for name checks */
static void vis_text(const char*s,char*o,size_t n,int keep_code){
    size_t k=0; int lc=0;
    for(const unsigned char*p=(const unsigned char*)s;*p&&k+1<n;p++){
        if(*p==0x0E){ lc=1; continue; } if(*p==0x0F){ lc=0; continue; }
        if(lc&&!keep_code) continue;
        o[k++]=(char)toupper(*p); }
    o[k]=0;
}
/* does the stroke font have a glyph for every char? (unknown chars are otherwise drawn as '?') */
static int glyphs_ok(const char*s,char*bad,size_t bn){
    const unsigned char*p=(const unsigned char*)s; g_lc=0;
    while(*p){ const unsigned char*q=p; int g=glyph_next(&p);
        if(g<0) continue;
        if((g=='?'&&*q!='?')||g>=NG||!GD[g].path){ snprintf(bad,bn,"%.*s",(int)(p-q),(const char*)q); g_lc=0; return 0; } }
    g_lc=0; return 1;
}
static int put_bytes(const char*path,const char*d,int n){ int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd<0) return 0; int ok=n==0||write(fd,d,n)==n; close(fd); return ok; }
static int file_is(const char*path,const char*want){ char b[64]; int fd=open(path,O_RDONLY); if(fd<0) return 0; int n=(int)read(fd,b,sizeof b); close(fd); return n==(int)strlen(want)&&!memcmp(b,want,n); }
static int lang_from(const char*path,const char*d,int n){ put_bytes(path,d,n); ui_lang=LANG_DA; static_dirty=0; return lang_load(); }   /* starting point DA: an unknown value must actively yield EN */
/* ====================== v6.5: selftest — boot push on the device + RX resync ====================== */
static int mk_frame(uint8_t*o,uint8_t cmd,uint8_t sub,const uint16_t*w,int len){
    int n=0; o[n++]=0xAA; o[n++]=0x55; o[n++]=cmd; o[n++]=sub; o[n++]=(uint8_t)len;
    for(int k=0;k<len;k++){ o[n++]=w[k]&0xFF; o[n++]=w[k]>>8; }
    uint16_t c=crc16(o,n); o[n++]=c&0xFF; o[n++]=c>>8; return n;
}
static void rx_zero(void){ rxlen=0; rx_pend_ms=rx_last_ms=0; rx_ok=rx_bad=rx_resync=0; last_rx_ms=last_bad_ms=-100000; }
/* one iteration of main(): first whatever has arrived, then the watchdog call */
static void rx_tick(const uint8_t*d,int n,ms_t t){ if(n>0) rx_feed(d,n,t); rx_feed(NULL,0,t); }
/* status reply as the controller sends it: 0x83 sub0 len56 (119 bytes); word 55 = sequence number k */
#define RXV_N 119
static int rx_v(uint8_t*o,int k){ uint16_t w[56]; memset(w,0,sizeof w); w[0]=1; w[33]=0x0101; w[55]=(uint16_t)k; return mk_frame(o,0x83,0,w,56); }
static void st_status(uint16_t ws,ms_t now){ uint8_t f[32]; uint16_t w[7]={ws,0,0,0,0,0,0}; int n=mk_frame(f,0x83,33,w,7); rx_feed(f,n,now); }
/* the controller's ACK to the most recently sent cmd6 frame: 0x86 w/ the same sub/len and an echo of the payload */
static void st_ack_last(ms_t now){ uint8_t a[80]; int n=last_tx_len; if(n<7) return;   /* v6.13: nothing sent yet (the mutant tests) -> no ACK */ memcpy(a,last_tx,n); a[2]=0x86; uint16_t c=crc16(a,n-2); a[n-2]=c&0xFF; a[n-1]=c>>8; rx_feed(a,n,now); }
static void st_bp_reset(void){
    memset(&wq,0,sizeof wq); memset(&bp,0,sizeof bp); memset(&sav,0,sizeof sav); act.pending=ACT_NONE; txq_len=0; last_tx_len=0;
    memset(reg,0,sizeof reg); memset(reg_seen,0,sizeof reg_seen); rx_zero(); ro_mode=0;
}
/* fixture config in the stock UI's cJSON format; ALL boot-push fields differ from artifacts/config.json and from the MCU's power-on defaults */
static int st_fixture(const char*path,int brew_temp,int with_brew){
    char b[1400]; int n=snprintf(b,sizeof b,"{\n\t\"Temperature\":\t1,\n\t\"timemode\":\t1,\n\t\"Automatically\":\t1,\n\t\"timeZone\":\t48,\n\t\"backlight\":\t9,\n\t\"volume\":\t31,\n"
        "\t\"sleepMode\":\t1,\n\t\"sleepModeSetting\":\t0,\n\t\"clearTip\":\t0,\n\t\"brewMode\":\t0,\n");
    if(with_brew) n+=snprintf(b+n,sizeof b-n,"\t\"brewTemper\":\t%d,\n",brew_temp);
    n+=snprintf(b+n,sizeof b-n,"\t\"autoGrindWeight\":\t172,\n\t\"grindMode\":\t2,\n\t\"steamGears\":\t2,\n\t\"preInfusionPressure\":\t3,\n\t\"preInfusionTime\":\t9,\n"
        "\t\"breakTime\":\t7,\n\t\"headTemper\":\t60,\n\t\"preInfusionSwitch\":\t0,\n\t\"sumtime\":\t0,\n\t\"autobrewWeight\":\t420,\n\t\"language\":\t0,\n"
        "\t\"AtuoSteamTarge\":\t600,\n\t\"hotWaterShortcut\":\t1,\n\t\"autoHotWaterTarget\":\t1500,\n\t\"presetStore\":\t{\n\t},\n\t\"softversion\":\t\"1.1.1-69\",\n\t\"hotWaterMode\":\t1\n}");
    return put_bytes(path,b,n);
}
/* independent vectors (computed outside the program, own CRC): stock's boot push for the fixture config */
#define BPV_A "aa 55 06 01 1e 00 00 02 00 00 00 ac 00 3c 00 00 00 00 00 00 00 00 00 00 00 a2 03 00 00 3c 00 02 00 3c 00 00 00 dc 05 00 00 00 00 00 00 00 00 00 00 a4 01 00 00 58 02 00 00 00 00 00 00 00 00 00 00 88 89"
#define BPV_B "aa 55 06 70 03 03 00 00 09 3c 00 e8 da"        /* v6.7: word 113 lo byte = 0 (stock's captured boot push); CRC computed outside the program */
/* ... and for artifacts/config.json (870 / 185 / 1 / 3 / 400 / 360 / 650) */
#define BPV_A_REAL "aa 55 06 01 1e 00 00 01 00 00 00 b9 00 3c 00 00 00 00 00 00 00 00 00 00 00 66 03 00 00 3c 00 03 00 3c 00 00 00 90 01 00 00 00 00 00 00 00 00 00 00 68 01 00 00 8a 02 00 00 00 00 00 00 00 00 00 00 62 3e"
static void st_bootpush_device(const char*cfgsrc){
    printf("== selftest: v6.5 boot push on the device (activity guard ON; fixture config; ACKs through rx_feed) ==\n");
    __typeof__(wq) wq0=wq; uint16_t reg0[REGN]; int seen0[REGN]; memcpy(reg0,reg,sizeof reg); memcpy(seen0,reg_seen,sizeof reg_seen);
    ms_t rxl0=last_rx_ms, ok0=rx_ok, bad0=rx_bad; int tx0=txq_len, ro0=ro_mode, bpon0=bp_on;
    char dir[300], fx[340]; snprintf(dir,sizeof dir,"/tmp/merakiplus_v6_selftest_%d",(int)getpid()); mkdir(dir,0755); snprintf(fx,sizeof fx,"%s/bootpush_fixture.json",dir);
    ms_t T=30000000L;
    /* --- the real config: the block is stock's, byte for byte --- */
    st_bp_reset(); boot_push_start(1); st_status(0x0101,T); boot_push_step(T); wq_step(T);
    st_check("artifacts/config.json: sub1 len30 on the wire = independent vector (… 66 03 … b9 00 … 62 3e), 67 bytes",bp.state==BP_A&&hexeq(last_tx,last_tx_len,BPV_A_REAL)&&txq_len==67);
    /* --- fixture --- */
    int fxok=st_fixture(fx,930,1); snprintf(cfg_path,sizeof cfg_path,"%s",fx); settings_load();
    { uint16_t a[30],p[3]; boot_push_words(a,p);
      static const uint16_t ea[30]={0,2,0,172,60,0,0,0,0,0,930,0,60,2,60,0,1500,0,0,0,0,0,420,0,600,0,0,0,0,0};
      st_check("fixture config loaded; words 1..30 = 0,2,0,172,60,…,930,0,60,2,60,0,1500,…,420,0,600,0…; words 112..114 = 0003 0900 60 (v6.7: the breakTime byte is 0 in the boot push)",
               fxok&&cfg_ok&&boot_push_cfg_ok()&&!memcmp(a,ea,sizeof ea)&&p[0]==0x0003&&p[1]==0x0900&&p[2]==60); }
    /* --- the guard waits until work_sta is known --- */
    st_bp_reset(); boot_push_start(1); boot_push_step(T);
    int w1=bp.state==BP_WAIT_A&&bp.guard&&wq.n==0;                                    /* no link */
    { static const uint8_t vr[8]={0xAA,0x55,0x89,0x01,0x00,0x00,0x6F,0x81}; rx_feed(vr,8,T+5); }   /* the controller's version query: link up, work_sta unknown */
    boot_push_step(T+6); int w2=link_up(T+6)&&!reg_seen[33]&&bp.state==BP_WAIT_A&&bp.guard&&wq.n==0&&txq_len==15;   /* only the version reply has been sent */
    st_check("guard: nothing is sent without a link, nor with a link as long as work_sta (word 33) is unknown",w1&&w2);
    /* --- idle machine: sub1 len30 -> ACK -> sub112 len3 -> ACK --- */
    st_status(0x0101,T+10); boot_push_step(T+10);
    int qa=bp.state==BP_A&&!bp.guard&&wq.n==1&&wq.q[0].sub==1&&wq.q[0].len==30; const WFrame fa=wq.q[0];
    wq_step(T+10);
    st_check("idle (work_sta 0101): sub1 len30 on the wire = independent vector for the fixture (67 bytes)",qa&&hexeq(last_tx,last_tx_len,BPV_A)&&txq_len==15+67);
    { int z=1; static const int zero[]={1,3,6,7,8,9,10,12,16,18,19,20,21,22,24,26,27,28,29,30}; for(unsigned q=0;q<sizeof zero/sizeof zero[0];q++) if(fa.w[zero[q]-1]) z=0;
      st_check("trigger-word policy: words 26/27/30 = 0, start words 3/6/9/16/22/24 = 0, words 5/13/15 = 60, other unknown words = 0 (independent audit)",
               z&&frame_trig_audit(&fa,0)&&fa.w[4]==60&&fa.w[12]==60&&fa.w[14]==60); }
    st_ack_last(T+35); boot_push_step(T+36); int a1=bp.ra==BPR_OK&&bp.state==BP_WAIT_B;
    boot_push_step(T+36); wq_step(T+36); int b1=bp.state==BP_B&&hexeq(last_tx,last_tx_len,BPV_B)&&txq_len==15+67+13;
    st_ack_last(T+60); boot_push_step(T+61);
    st_check("ACK (0x86 w/ echo) -> sub112 len3 = independent vector (… 03 00 00 09 3c 00 e8 da) -> ACK -> done: OK / OK, 0 retransmits, 0 failures, 0 blocks",
             a1&&b1&&bp.state==BP_DONE&&bp.rb==BPR_OK&&bp.runs==1&&wq.n==0&&wq.n_ok==2&&wq.n_retx==0&&wq.n_fail==0&&wq.n_block==0&&txq_len==15+67+13);
    st_check("controller mirror after ACK = config: word 11 = 930, word 4 = 172, word 2 = 2, word 14 = 2, word 17 = 1500, word 23 = 420, word 25 = 600, words 112..114 = 0003 0900 60",
             reg[11]==930&&reg[4]==172&&reg[2]==2&&reg[14]==2&&reg[17]==1500&&reg[23]==420&&reg[25]==600&&reg[112]==0x0003&&reg[113]==0x0900&&reg[114]==60&&reg[5]==60&&reg[30]==0);
    { ms_t t=T+70; for(int k=0;k<5000;k++){ t+=2; rx_feed(NULL,0,t); wq_step(t); boot_push_step(t); }
      st_check("the boot push is sent ONCE: 10 s later nothing new has been sent",bp.state==BP_DONE&&bp.runs==1&&txq_len==15+67+13&&wq.n==0); }
    { uint32_t c0,c1; const char*s0=bp_text(0,&c0),*s1=bp_text(1,&c1);
      st_check("DIAGNOSE: BOOT-PUSH 1-30 = OK (green), BOOT-PUSH 112-114 = OK (green)",s0==TR(TX_DV_OK)&&s1==TR(TX_DV_OK)&&c0==C_OK&&c1==C_OK); }
    /* --- ACK wait + one retransmit --- */
    st_bp_reset(); boot_push_start(1); st_status(0x0101,T); boot_push_step(T); wq_step(T);
    wq_step(T+149); int r0=wq.n_retx==0&&txq_len==67; wq_step(T+150); int r1=wq.n_retx==1&&txq_len==2*67&&hexeq(last_tx,last_tx_len,BPV_A);
    wq_step(T+160); int r2=txq_len==2*67; st_ack_last(T+170); boot_push_step(T+171);
    st_check("sub1 len30 without ACK: no retransmit before 150 ms, exactly ONE (same bytes) at 150 ms; ACK to it -> OK",r0&&r1&&r2&&bp.ra==BPR_OK&&wq.n_fail==0);
    boot_push_step(T+171); wq_step(T+171); wq_step(T+171+150); wq_step(T+171+150+299); int n0=wq.n==1&&wq.n_retx==2; wq_step(T+171+150+300); boot_push_step(T+171+450);
    st_check("sub112 len3 with no ACK at all: 1 retransmit, given up after 150 + 300 ms -> NO ACK; the push is done (no further attempts on its own)",
             n0&&bp.state==BP_DONE&&bp.ra==BPR_OK&&bp.rb==BPR_NOACK&&wq.n==0&&wq.n_fail==1&&txq_len==2*67+2*13);
    st_bp_reset(); boot_push_start(1); st_status(0x0101,T); boot_push_step(T); wq_step(T); wq_step(T+150); wq_step(T+450); boot_push_step(T+450); boot_push_step(T+450); wq_step(T+450);
    st_check("sub1 len30 with no ACK at all: NO ACK, and sub112 len3 is sent anyway",bp.ra==BPR_NOACK&&bp.state==BP_B&&hexeq(last_tx,last_tx_len,BPV_B)&&wq.n_retx==1&&wq.n_fail==1);
    /* --- the activity guard --- */
    { static const uint16_t act_ws[]={0x0141,0x1107,0x1307,0x030B,0x4121}; int ok=1;   /* grind, pre-inf., extraction, hot water, steam (SESSION.md §6) */
      for(unsigned q=0;q<sizeof act_ws/sizeof act_ws[0];q++){
          st_bp_reset(); boot_push_start(1); st_status(act_ws[q],T); boot_push_step(T);
          int skip=bp.state==BP_DONE&&bp.ra==BPR_SKIP&&bp.rb==BPR_SKIP&&bp.runs==1&&wq.n==0&&txq_len==0;
          ms_t t=T; for(int k=0;k<300;k++){ t+=100; st_status(k<50?act_ws[q]:0x0101,t); wq_step(t); boot_push_step(t); }   /* the activity ends after 5 s; 25 s idle */
          boot_push_resave();                                                                                         /* a later SAVE does not resume it either */
          boot_push_step(t+100);
          if(!(skip&&bp.state==BP_DONE&&bp.ra==BPR_SKIP&&wq.n==0&&txq_len==0&&wq.n_block==0)){ ok=0; printf("     work_sta %04X: state=%d ra=%d wq.n=%d tx=%d\n",act_ws[q],bp.state,bp.ra,wq.n,txq_len); } }
      st_check("ACTIVITY GUARD: work_sta 0141/1107/1307/030B/4121 at startup -> the push is SKIPPED: 0 bytes sent — not after the activity or a SAVE either",ok);
      uint32_t c0; st_check("DIAGNOSE: BOOT-PUSH = SPRUNGET OVER / SKIPPED (neutral colour)",bp_text(0,&c0)==TR(TX_DV_BP_SKIP)&&c0==C_TXT&&bp_text(1,&c0)==TR(TX_DV_BP_SKIP)); }
    { static const uint16_t idle_ws[]={0x0101,0x0001,0x0000}; int ok=1;                 /* awake-idle, sleep/drain, MCU sleep task */
      for(unsigned q=0;q<3;q++){ st_bp_reset(); boot_push_start(1); st_status(idle_ws[q],T); boot_push_step(T); if(!(bp.state==BP_A&&wq.n==1&&wq.q[0].sub==1)) ok=0; }
      st_check("the guard does NOT block an idle machine: work_sta 0101 (awake), 0001 (sleep/drain) and 0000 -> the push is sent",ok); }
    st_bp_reset(); boot_push_start(1); st_status(0x0101,T); boot_push_step(T); wq_step(T); st_ack_last(T+25); boot_push_step(T+26);
    { int d=bp.state==BP_WAIT_B; ms_t t=T+30; for(int k=0;k<300;k++){ t+=100; st_status(0x1307,t); wq_step(t); boot_push_step(t); if(bp.state!=BP_WAIT_B||wq.n) d=0; }
      st_status(0x0101,t+100); boot_push_step(t+100); wq_step(t+100);
      st_check("activity started AFTER the decision (between the two blocks): sub112 len3 is DEFERRED 30 s and sent when the machine is idle",d&&bp.state==BP_B&&hexeq(last_tx,last_tx_len,BPV_B)); }
    /* --- config out of spec / missing / read-only / disabled --- */
    st_fixture(fx,1200,1); settings_load(); st_bp_reset(); boot_push_start(1); st_status(0x0101,T); boot_push_step(T);
    int blk=bp.ra==BPR_BLOCKED&&bp.state==BP_WAIT_B&&wq.n==0&&wq.n_block==1&&wq_rej==WJ_RANGE&&txq_len==0;
    boot_push_step(T); wq_step(T); blk&=hexeq(last_tx,last_tx_len,BPV_B); st_ack_last(T+25); boot_push_step(T+26);
    st_check("fixture w/ brewTemper=1200: sub1 len30 REJECTED by the frame policy (BLOK +1, 0 bytes), sub112 len3 sent and ACKed",blk&&bp.state==BP_DONE&&bp.rb==BPR_OK);
    { uint32_t c0; st_check("DIAGNOSE: BOOT-PUSH 1-30 = AFVIST / REJECTED (amber)",bp_text(0,&c0)==TR(TX_DV_BP_BLOCKED)&&c0==C_WARN); }
    SET[S_BREWT].val=930; boot_push_resave();                                             /* the user has corrected the value with a SAVE */
    { int rs=bp.state==BP_WAIT_A&&!bp.guard; boot_push_step(T+100); wq_step(T+100); rs&=hexeq(last_tx,last_tx_len,BPV_A); st_ack_last(T+125); boot_push_step(T+126);
      boot_push_step(T+126); wq_step(T+126); st_ack_last(T+150); boot_push_step(T+151);
      st_check("after a SAVE that brings config into spec: the push is repeated — sub1 len30 (now = the vector) + sub112 len3, both ACKed",rs&&bp.state==BP_DONE&&bp.ra==BPR_OK&&bp.rb==BPR_OK&&bp.runs==2&&wq.n_block==1);
      boot_push_resave(); st_check("a completed push (OK / OK) is NOT repeated by a later SAVE",bp.state==BP_DONE&&bp.runs==2); }
    st_fixture(fx,0,0); settings_load(); st_bp_reset(); boot_push_start(1); st_status(0x0101,T); boot_push_step(T);
    st_check("fixture without brewTemper: NO CONFIG — nothing is sent (no guessed value to the controller)",bp.state==BP_DONE&&bp.ra==BPR_NOCFG&&bp.rb==BPR_NOCFG&&wq.n==0&&txq_len==0);
    st_fixture(fx,930,1); settings_load(); st_bp_reset(); ro_mode=1; boot_push_start(1); st_status(0x0101,T); boot_push_step(T); int ro=wq.n==0&&txq_len==0&&bp.ra==BPR_NONE; ro_mode=0;
    st_bp_reset(); bp_on=0; boot_push_resave(); st_status(0x0101,T); boot_push_step(T); int off=bp.state==BP_OFF&&wq.n==0&&txq_len==0; uint32_t c0; off&=(bp_text(0,&c0)==TR(TX_DV_BP_OFF));
    st_check("MERAKIPLUS_READONLY: nothing is sent; MERAKIPLUS_NOBOOTPUSH (not started): nothing is sent, DIAGNOSE shows FRA / OFF",ro&&off);
    /* --- clean up: restore the real config --- */
    bp_on=bpon0; ro_mode=ro0; snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); settings_load(); unlink(fx);
    wq=wq0; memset(&bp,0,sizeof bp); memcpy(reg,reg0,sizeof reg); memcpy(reg_seen,seen0,sizeof reg_seen); rx_zero(); last_rx_ms=rxl0; rx_ok=ok0; rx_bad=bad0; txq_len=tx0; last_tx_len=0;
    st_check("the real config is loaded again after the fixture tests (0104 / 0605 / 70)",cfg_ok&&pack112(sv(S_PISW),sv(S_PIPRESS))==0x0104&&pack113(sv(S_PITIME),sv(S_BREAK))==0x0605&&sv(S_HEADT)==70);
}
static void st_rx_resync(void){
    printf("== selftest: v6.5 RX resync watchdog (RX_RESYNC_MS = %d; main() calls the parser every 2 ms) ==\n",RX_RESYNC_MS);
    __typeof__(wq) wq0=wq; uint16_t reg0[REGN]; int seen0[REGN]; memcpy(reg0,reg,sizeof reg); memcpy(seen0,reg_seen,sizeof reg_seen);
    ms_t rxl0=last_rx_ms, ok0=rx_ok, bad0=rx_bad, lb0=last_bad_ms; int tx0=txq_len;
    static uint8_t V[8][RXV_N], C[RXV_N], blob[8192]; ms_t T=40000000L, t;
    for(int k=0;k<8;k++) rx_v(V[k],k+1);
    /* --- baseline: valid frames, however they are split up --- */
    { int ok=1; static const int chunk[]={1,7,37,119,512};
      for(unsigned q=0;q<5;q++){ rx_zero(); int n=0; for(int k=0;k<5;k++){ memcpy(blob+n,V[k],RXV_N); n+=RXV_N; }
          t=T; for(int o=0;o<n;o+=chunk[q]){ int m=n-o<chunk[q]?n-o:chunk[q]; rx_tick(blob+o,m,t++); }
          if(!(rx_ok==5&&reg[55]==5&&rx_bad==0&&rx_resync==0&&rxlen==0)) ok=0; }
      st_check("baseline: 5 valid frames in chunks of 1 / 7 / 37 / 119 / 512 bytes -> 5 received, 0 CRC errors, 0 resync, empty buffer",ok); }
    /* --- corrupt length byte: impossible length (the v6.4 finding) --- */
    memcpy(C,V[7],RXV_N); C[4]=0xC8;                                                     /* LEN 56 -> 200 words: does not exist */
    rx_zero(); { int n=0; memcpy(blob,C,RXV_N); n=RXV_N; for(int k=0;k<3;k++){ memcpy(blob+n,V[k],RXV_N); n+=RXV_N; } rx_feed(blob,n,T); }
    st_check("corrupt length byte (LEN = 200 words): the candidate is discarded IMMEDIATELY — the 3 frames behind it are received in the same call (0 ms), RESYNC +1",rx_ok==3&&reg[55]==3&&rx_resync==1&&rxlen==0);
    { rx_zero(); rx_tick(C,RXV_N,T); ms_t gap=0,last=T; int k=0; for(t=T+2;t<=T+3000;t+=2){ ms_t o=rx_ok; if((t-T)%50==0){ rx_v(blob,++k); rx_tick(blob,RXV_N,t); } else rx_tick(NULL,0,t); if(rx_ok!=o){ if(t-last>gap) gap=t-last; last=t; } }
      st_check("the v6.4 scenario: corrupt LEN followed by 3 s of poll traffic -> all 60 replies received, largest gap 50 ms, link up the whole way (v6.4: approx. 3.5 s without RX)",rx_ok==60&&reg[55]==60&&gap<=50&&rx_resync==1&&link_up(T+3000)); }
    /* --- corrupt length byte: possible, but too long (56 -> 120 words = 247 bytes) --- */
    memcpy(C,V[7],RXV_N); C[4]=0x78;
    { rx_zero(); ms_t first=-1,all=-1; for(t=T;t<=T+300;t+=2){ const uint8_t*d=NULL; if(t==T) d=C; else if(t==T+50) d=V[0]; else if(t==T+100) d=V[1]; else if(t==T+150) d=V[2];
          rx_tick(d,d?RXV_N:0,t); if(first<0&&rx_ok>=1) first=t; if(all<0&&rx_ok>=3) all=t; }
      st_check("corrupt LEN (120 words = 247 bytes), traffic continues: the CRC rejects the candidate as soon as 247 bytes have arrived — frames behind it received at +100 ms, all 3 at +150 ms, 0 lost",
               first==T+100&&all==T+150&&reg[55]==3&&rx_bad==1&&rx_resync==0&&rxlen==0); }
    { rx_zero(); ms_t got=-1; for(t=T;t<=T+1000;t+=2){ const uint8_t*d=NULL; if(t==T) d=C; else if(t==T+50) d=V[0]; rx_tick(d,d?RXV_N:0,t); if(got<0&&rx_ok>=1) got=t; }
      st_check("corrupt LEN (247 bytes), then silence: the WATCHDOG discards the candidate after exactly RX_RESYNC_MS — the frame behind it is then received (+400 ms), RESYNC +1, empty buffer",
               got==T+RX_RESYNC_MS&&reg[55]==1&&rx_resync==1&&rxlen==0); }
    /* --- truncated frame --- */
    { rx_zero(); int held=0,gone=0; for(t=T;t<=T+998;t+=2){ rx_tick(t==T?V[7]:NULL,t==T?60:0,t); if(t==T+RX_RESYNC_MS-2) held=rxlen==60; if(t==T+RX_RESYNC_MS) gone=rxlen==0&&rx_resync==1; }
      rx_tick(V[0],RXV_N,T+1000); int a=rx_ok==1; rx_tick(V[1],RXV_N,T+1050);
      st_check("truncated frame (60 of 119 bytes), 1 s of silence: the fragment is held for 398 ms, discarded at 400 ms; the next frames are received as soon as they arrive",held&&gone&&a&&rx_ok==2&&reg[55]==2&&rx_resync==1&&rx_bad==0); }
    { rx_zero(); memset(reg,0,sizeof reg); uint8_t sf[32]; uint16_t w7[7]={0x0101,0,0,0,0,0,0}; int sn=mk_frame(sf,0x83,33,w7,7);
      rx_feed(V[7],60,T); rx_feed(sf,sn,T+1000);                                         /* 60 + 21 bytes: the fragment's 119 bytes are never filled up */
      st_check("truncated frame, and the parser is not called again until a short reply (21 bytes) arrives 1 s later: the watchdog is decided ON ARRIVAL — reply received in the same call (0 ms), RESYNC +1",
               rx_ok==1&&reg[33]==0x0101&&rx_resync==1&&rx_bad==0&&rxlen==0);
      rx_zero(); rx_feed(V[7],60,T); rx_feed(V[0],RXV_N,T+1000);                           /* 60 + 119 bytes: the fragment is filled up, and the CRC rejects it */
      st_check("truncated frame followed by a long reply 1 s later: the CRC rejects the fragment — reply received in the same call (0 ms), 1 CRC error",rx_ok==1&&reg[55]==1&&rx_bad==1&&rxlen==0); }
    rx_zero(); rx_tick(V[7],60,T); rx_tick(V[0],RXV_N,T+10); rx_tick(V[1],RXV_N,T+60);
    st_check("truncated frame followed by valid frames 10 ms later: the CRC rejects the fragment — both frames received immediately, 1 CRC error, 0 resync",rx_ok==2&&reg[55]==2&&rx_bad==1&&rx_resync==0&&rxlen==0);
    { rx_zero(); static const uint8_t st4[4]={0xAA,0x55,0x83,0x00}; int gone=0; for(t=T;t<=T+600;t+=2){ rx_tick(t==T?st4:NULL,t==T?4:0,t); if(t==T+RX_RESYNC_MS) gone=rxlen==0; } rx_tick(V[0],RXV_N,T+602);
      st_check("fragment under 7 bytes (AA 55 83 00): discarded at 400 ms; next frame received",gone&&rx_ok==1&&rx_resync==1); }
    /* --- noise burst: random bytes + the worst prefixes, in chunks of 1..24 bytes per 10 ms (like the sim's garbage) --- */
    { static const uint8_t nasty[9][5]={{0xAA},{0xAA,0x55},{0xAA,0x55,0x03},{0xAA,0x55,0x08},{0xAA,0x55,0x88,0x12,0x34},{0xAA,0x55,0x83,0x00,0x38},{0xAA,0x55,0x09,0xFF},{0xAA,0x55,0x86,0x0B,0x81},{0xAA,0x55,0x83,0x00,0xC8}};
      static const int nlen[9]={1,2,3,4,5,5,4,5,5};
      rx_zero(); uint32_t x=0x6502u; int maxbuf=0, sent=0, nn=0; t=T;
      while(sent<4096){ int m=1+(int)((x=x*1103515245u+12345u)>>16)%24; for(int k=0;k<m;k++) blob[k]=(uint8_t)((x=x*1103515245u+12345u)>>16);
          if(sent/200!=(sent+m)/200){ memcpy(blob+m,nasty[nn%9],nlen[nn%9]); m+=nlen[nn%9]; nn++; }
          for(int q=0;q<5;q++){ rx_tick(q==0?blob:NULL,q==0?m:0,t); t+=2; } sent+=m; if(rxlen>maxbuf) maxbuf=rxlen; }
      int quiet=rx_ok==0; ms_t tv=t, first=-1;
      for(int k=0;k<5;k++){ for(int q=0;q<25;q++){ rx_tick(q==0?V[k]:NULL,q==0?RXV_N:0,t); if(first<0&&rx_ok>=1) first=t; t+=2; } }
      char nm[260]; snprintf(nm,sizeof nm,"noise burst: %d random bytes with %d inserted worst-case prefixes (AA 55 03 / 08 / 88 / long LEN …): 0 false frames, buffer at most %d bytes; the 5 frames right after are ALL received, the first %ld ms after arrival",sent,nn,maxbuf,(long)(first-tv));
      st_check(nm,quiet&&rx_ok==5&&reg[55]==5&&first>=0&&first-tv<=RX_RESYNC_MS&&maxbuf<=265+29+RXV_N);
      /* each prefix on its own, right before valid frames: (a) traffic continues, (b) one frame and then silence */
      int ok=1; ms_t worst=0;
      for(int q=0;q<9;q++) for(int mode=0;mode<2;mode++){
          rx_zero(); ms_t got=-1;
          for(t=T;t<=T+700;t+=2){ const uint8_t*d=NULL; int m=0;
              if(t==T){ d=nasty[q]; m=nlen[q]; } else if(t==T+10){ d=V[0]; m=RXV_N; } else if(!mode&&t==T+60){ d=V[1]; m=RXV_N; } else if(!mode&&t==T+110){ d=V[2]; m=RXV_N; }
              rx_tick(d,m,t); if(got<0&&rx_ok>=1) got=t; }
          int want=mode?1:3; if(got<0||got-T>RX_RESYNC_MS||rx_ok!=want||reg[55]!=want||rxlen!=0){ ok=0; printf("     prefix %d mode %d: rx_ok=%u got=%ld rxlen=%d\n",q,mode,rx_ok,(long)(got-T),rxlen); }
          if(got-T>worst) worst=got-T; }
      snprintf(nm,sizeof nm,"each of the 9 worst prefixes right before valid frames (traffic continues / ONE frame and then silence): no frame lost, first frame received at most %ld ms after the noise (limit %d)",(long)worst,RX_RESYNC_MS);
      st_check(nm,ok&&worst<=RX_RESYNC_MS); }
    /* --- valid frames interleaved with errors: each valid frame exactly once, in order --- */
    { rx_zero(); memset(&wq,0,sizeof wq); txq_len=0; int ok=1, nbad=0, prev=0; uint32_t x=77u; t=T; ms_t tack=-1;
      for(int k=1;k<=40;k++){ int n=0; uint8_t f[RXV_N]; rx_v(f,k);
          if(k%3==0){ for(int j=0;j<9;j++) blob[n++]=(uint8_t)((x=x*1103515245u+12345u)>>16); }
          if(k%5==0){ memcpy(blob+n,f,RXV_N); blob[n+40]^=0x10; n+=RXV_N; nbad++; }               /* bit error in payload -> CRC error */
          if(k%7==0){ memcpy(blob+n,f,RXV_N); blob[n+4]=0xC8; n+=RXV_N; }                         /* impossible length */
          if(k%11==0){ memcpy(blob+n,f,RXV_N); blob[n+4]=0x78; n+=RXV_N; nbad++; }                /* too long a length -> CRC error once 247 bytes have arrived */
          if(k%13==0){ memcpy(blob+n,f,45); n+=45; nbad++; }                                      /* truncated -> CRC error when the next frame fills it up */
          if(k%17==0){ blob[n++]=0xAA; blob[n++]=0x55; blob[n++]=0x03; nbad++; }                  /* cmd 3 header + 4 bytes of the next frame -> CRC error (v6.4 swallowed them without CRC) */
          memcpy(blob+n,f,RXV_N); n+=RXV_N;
          if(k==20){ wq_push(11,(uint16_t[1]){880},1); wq_step(t); }                              /* SAVE write is sent … */
          if(k==21){ uint8_t a[16]; uint16_t w=880; int an=mk_frame(a,0x86,11,&w,1); memcpy(blob+n,a,an); n+=an; }   /* … and the ACK arrives mid-stream */
          for(int o=0,q=0;q<25;q++){ int m=n-o<37?n-o:37; rx_tick(m>0?blob+o:NULL,m>0?m:0,t); o+=m>0?m:0; wq_step(t); if(tack<0&&wq.result==WR_OK) tack=t;
              if(reg[55]<prev||reg[55]>k) ok=0; prev=reg[55]; t+=2; } }
      for(int q=0;q<300;q++){ rx_tick(NULL,0,t); t+=2; }
      char nm[260]; snprintf(nm,sizeof nm,"interleaved: 40 valid replies + 1 ACK mixed with noise, CRC errors, impossible/too long LEN, truncated frames and AA 55 03: all 41 received ONCE in order, %d CRC errors counted, the ACK released the write queue",nbad);
      if(!(ok&&rx_ok==41&&rx_bad==nbad)) printf("     ok=%d rx_ok=%u reg55=%u rx_bad=%u (expected %d) resync=%u rxlen=%d tack=%ld\n",ok,rx_ok,reg[55],rx_bad,nbad,rx_resync,rxlen,(long)(tack-T));
      st_check(nm,ok&&rx_ok==41&&reg[55]==40&&rx_bad==nbad&&rxlen==0&&tack>=0&&wq.result==WR_OK&&wq.n_fail==0&&wq.n_retx==0); memset(&wq,0,sizeof wq); txq_len=0; }
    /* --- slow but valid traffic must not be discarded --- */
    { rx_zero(); static const int gaps[]={100,300,456,1000,1400}; t=T; int k=0; rx_tick(V[k++],RXV_N,t);
      for(unsigned q=0;q<5;q++){ ms_t e=t+gaps[q]; for(t+=2;t<e;t+=2) rx_tick(NULL,0,t); rx_tick(V[k++],RXV_N,t); }
      st_check("pauses BETWEEN frames of 100 / 300 / 456 / 1000 / 1400 ms (poll cadence and longest measured poll gap): all 6 received, 0 resync",rx_ok==6&&reg[55]==6&&rx_resync==0&&rx_bad==0); }
    { int ok=1; static const int pause[]={20,300,398}; for(unsigned q=0;q<3;q++){ rx_zero(); for(t=T;t<=T+pause[q];t+=2){ const uint8_t*d=NULL; int m=0; if(t==T){ d=V[0]; m=60; } else if(t==T+pause[q]){ d=V[0]+60; m=RXV_N-60; } rx_tick(d,m,t); }
          if(!(rx_ok==1&&rx_resync==0&&rx_bad==0&&rxlen==0)) ok=0; }
      st_check("pauses IN THE MIDDLE of a frame of 20 / 300 / 398 ms (longest measured MCU silence: 279 ms): the frame is received, 0 resync",ok); }
    { rx_zero(); for(t=T;t<=T+450;t+=2){ const uint8_t*d=NULL; int m=0; if(t==T){ d=V[0]; m=60; } else if(t==T+450){ d=V[0]+60; m=RXV_N-60; } rx_tick(d,m,t); }
      int lost=rx_ok==0&&rx_resync==1; rx_tick(V[1],RXV_N,T+500);
      st_check("pause in the middle of a frame of 450 ms (beyond the window): THAT frame is dropped (RESYNC +1) — the next one is received",lost&&rx_ok==1&&reg[55]==2&&rxlen==0); }
    { rx_zero(); txq_len=0; static const uint8_t vr[8]={0xAA,0x55,0x89,0x01,0x00,0x00,0x6F,0x81}; rx_tick(vr,3,T); rx_tick(vr+3,5,T+20); int one=txq_len==15&&rx_ok==1; rx_tick(vr,8,T+40);
      static const uint8_t q3[7]={0xAA,0x55,0x03,0x00,0x38,0xDC,0x06}; rx_tick(q3,7,T+60);
      st_check("version request 0x89 (split in two) is answered ONCE per request; a valid cmd 3 frame (AA 55 03 00 38 DC 06) is skipped without error",one&&txq_len==30&&rx_ok==2&&rx_bad==0&&rx_resync==0&&rxlen==0); txq_len=0; }
    /* --- a stuck candidate in front of an ACK must not cost a SAVE (window < ACK budget 450 ms) --- */
    { rx_zero(); memset(&wq,0,sizeof wq); txq_len=0; wq_push(11,(uint16_t[1]){890},1); ms_t tok=-1; uint8_t a[16]; uint16_t w=890; int an=mk_frame(a,0x86,11,&w,1);
      static uint8_t stuck[25]={0xAA,0x55,0x83,0x00,0x81};                                 /* promises 265 bytes, delivers 25 */
      for(t=T;t<=T+460;t+=2){ const uint8_t*d=NULL; int m=0; if(t==T+4){ d=stuck; m=25; } else if(t==T+24){ d=a; m=an; } rx_tick(d,m,t); wq_step(t); if(tok<0&&wq.result==WR_OK) tok=t; }
      st_check("ACK caught behind a stuck candidate, no other traffic: released by the watchdog 400 ms after the candidate (+404 ms) — before the write queue gives up at 450 ms; SAVE succeeds",
               tok==T+4+RX_RESYNC_MS&&wq.n_fail==0&&wq.n_retx==1&&reg[11]==890&&rx_resync==1); memset(&wq,0,sizeof wq); txq_len=0; }
    st_check("the window lies between the longest measured MCU silence (279 ms) and the write queue's ACK budget (150 + 300 ms), and below the OFFLINE limit (1500 ms)",RX_RESYNC_MS>279&&RX_RESYNC_MS<WQ_ACK1_MS+WQ_ACK2_MS&&RX_RESYNC_MS<1500);
    wq=wq0; memcpy(reg,reg0,sizeof reg); memcpy(reg_seen,seen0,sizeof reg_seen); rx_zero(); last_rx_ms=rxl0; rx_ok=ok0; rx_bad=bad0; last_bad_ms=lb0; txq_len=tx0; last_tx_len=0;
}
/* ====================================================================== */
/* ============= v6.6: phase semantics · 64-bit clock · re-push ========= */
/* ====================================================================== */
/* the controller's replies as they arrive on the line: 0x83 sub0 len56 (words 0..55) and 0x83 sub33 len7 (words 33..39) respectively */
static void st_s0(uint16_t ws,uint16_t w24,uint16_t w44,uint16_t w46,uint16_t w48,ms_t t){
    uint8_t f[128]; uint16_t w[56]; memcpy(w,reg,sizeof w); w[33]=ws; w[24]=w24; w[44]=w44; w[46]=w46; w[48]=w48; rx_feed(f,mk_frame(f,0x83,0,w,56),t);
}
static void st_s33(uint16_t ws,ms_t t){ uint8_t f[32]; uint16_t w[7]; memcpy(w,reg+33,sizeof w); w[0]=ws; rx_feed(f,mk_frame(f,0x83,33,w,7),t); }
static int st_has(const char*hay,const char*needle){ return strstr(hay,needle)!=NULL; }
/* idle machine with a fresh link at time t (HOME, no toast, empty write queue) */
static ms_t st_home(void){
    sim_reset(); sim_run(400); memset(&wq,0,sizeof wq); txq_len=0; toast.dur=0;
    return sim_now;
}
static void st_phases(void){
    printf("== selftest: v6.6 phase semantics per STEAMLIVE.md (two steam paths via word 24 + work_sta + words 44/46; concurrent phases) ==\n");
    char a[32],b[96]; ms_t t;
    /* ---- (1) LATCHED auto steam: STEAMLIVE §4.1 + §5.1, row by row ---- */
    t=st_home(); reg[25]=600;                                   /* the MCU's target (word 25) = 60.0° — deliberately different from config (65.0°) */
    st_s0(0x0101,1,0x000C,0,123,t+=100); ui_frame(t);            /* the start write is ACKed: word 24 = 1, bits 5+14 not yet set */
    int pre=stm.kind==SK_NONE&&act.ph!=PH_STEAM;
    st_s33(0x4121,t+=220); ui_frame(t);
    fmt_t10(a,sizeof a,600,1); snprintf(b,sizeof b,TR(TX_H_AUTOSTOP_T),a);
    st_check("LATCHED (word 24 = 1, work_sta 4121, wand switch 0): chip = STEAM·AUTO / DAMP·AUTO, phase STEAM",
             pre&&stm.kind==SK_AUTO&&!stm.wand&&act.ph==PH_STEAM&&phase_raw(t)==PH_STEAM&&hm.chip==TR(TX_PH_STEAM_AUTO)&&!hm.conc&&!hm.badge);
    st_check("LATCHED: the target is WORD 25 (60.0°, not the config's 65.0°) — the auto-stop expectation ('STOPS AT 60.0°', said once: the label is just 'MILK') and the gauge marker",
             !strcmp(hm.label,TR(TX_H_MILK))&&!st_has(hm.label,a)&&st_has(hm.sub,a)&&!strcmp(hm.sub,b)&&fabsf(hm.gmark-(600-200)/600.f)<1e-4f&&steam_target()==600&&sv(S_STEAMT)==650);
    st_check("LATCHED: the STOP button is shown and hittable (124,342,232x60; v6.6-v6.16: 150,344,180x56); no start buttons",steam_stop_visible()&&hit_test(240,372,t)==T_STOP&&hit_test(150,372,t)==T_STOP&&hit_test(330,372,t)==T_STOP&&hit_test(120,372,t)==T_NONE&&hit_test(360,372,t)==T_NONE&&hit_test(240,340,t)==T_NONE);
    { int ok=1; static const uint16_t pulse[]={0x6121,0x4521,0x4121,0x6121};      /* bit 10/13 pulses on top of steam (STEAMLIVE §3) */
      for(unsigned q=0;q<4;q++){ st_s0(pulse[q],1,0x000C,0,(uint16_t)(288+q*40),t+=100); ui_frame(t); if(stm.kind!=SK_AUTO||hm.chip!=TR(TX_PH_STEAM_AUTO)) ok=0; }
      st_check("LATCHED: maintenance pulses (6121 / 4521) change nothing — still STEAM·AUTO",ok); }
    /* manual STOP: the ACK echo sets word 24 = 0 BEFORE the MCU clears bits 5+14 */
    { __typeof__(stm) k=stm; uint16_t r24=reg[24]; int ok=1;
      for(int q=0;q<3;q++){ st_s0(0x4121,0,0x000C,0,430,t+=50); ui_frame(t); if(stm.kind!=SK_AUTO||hm.chip!=TR(TX_PH_STEAM_AUTO)||!steam_stop_visible()) ok=0; }
      st_check("LATCHED is sticky: word 24 = 0 (ACK of a STOP write) while bits 5+14 are still set -> still STEAM·AUTO, never a WAND flash",ok);
      stm=k; reg[24]=r24; }
    /* auto-stop, exactly as in the log: 96.363 / 96.437 / 96.676 / 96.838 */
    st_s0(0x6121,1,0x000C,0,646,t+=100); ui_frame(t); st_s33(0x6121,t+=74); ui_frame(t);
    int on=stm.kind==SK_AUTO&&act.ph==PH_STEAM;
    st_s33(0x2101,t+=239); ui_frame(t);                                            /* bits 5+14 cleared, word 24 still 1 */
    int stop1=stm.kind==SK_NONE&&stm.last_kind==SK_AUTO&&act.ph==PH_DONE&&act.last_ph==PH_STEAM&&!steam_stop_visible()&&hit_test(240,372,t)!=T_STOP;
    st_s0(0x0501,0,0x000C,0,652,t+=162); ui_frame(t);                              /* the MCU resets word 24 itself; milk 65.2° */
    st_check("AUTO-STOP (STEAMLIVE §5.1: 6121 -> 2101 with word 24 = 1 -> 0501 with word 24 = 0): DONE, STOP button gone, and merakiplus has sent NOTHING (no stop write)",
             on&&stop1&&act.ph==PH_DONE&&wq.n==0&&txq_len==0&&wq.n_ok==0);
    { char d[8]; fmt_fix(d,sizeof d,use_c()?MILK_RISE10:MILK_RISE10*9/5,-1); char need[16]; snprintf(need,sizeof need,"\xE2\x89\x88%s\xC2\xB0",d);
      int note=st_has(hm.sub,need)&&st_has(hm.label,a);
      for(int q=0;q<12;q++){ st_s0(0x0101,0,0x000C,0,652,t+=100); ui_frame(t); }  /* v6.9: the hero is conditioned (median-3 + EMA) — let it settle */
      fmt_t10(a,sizeof a,652,1); int h1=!strcmp(hm.hero,a);
      st_s0(0x0101,0,0x000C,0,689,t+=4000); ui_frame(t); fmt_t10(a,sizeof a,689,1);
      st_check("after auto-stop: the subline states the post-stop rise (approx. +4 °C), and the hero keeps following the probe (65.2 -> 68.9) — information only",note&&h1&&!strcmp(hm.hero,a)&&act.ph==PH_DONE&&!strcmp(d,"4"));
      for(int q=0;q<50;q++){ st_s33(0x0101,t+=100); ui_frame(t); }
      st_check("8 s after stop: back to READY; the steam tracking is reset",act.ph==PH_READY&&stm.kind==SK_NONE); }
    /* ---- (2) MOMENTARY wand/flush alone ---- */
    t=st_home();
    st_s0(0x0101,0,0x000C,0,514,t+=100); ui_frame(t);
    st_s33(0x4121,t+=76); ui_frame(t);                                             /* bits set; words 44/46 only follow ~0.4 s later */
    int w0=stm.kind==SK_WAND&&!stm.wand&&hm.chip==TR(TX_PH_STEAM_WAND);
    st_s0(0x4121,0,0x000D,1,523,t+=391); ui_frame(t);
    st_check("WAND (work_sta 4121 WITHOUT word 24 = 1): chip = STEAM·WAND / DAMP·WAND immediately; word 44 bit0 + word 46 = 1 confirm 0.4 s later",
             w0&&stm.kind==SK_WAND&&stm.wand&&wand_sw()&&act.ph==PH_STEAM&&phase_raw(t)==PH_STEAM&&hm.chip==TR(TX_PH_STEAM_WAND));
    st_check("WAND: no auto-stop expectation — label 'MILK PROBE · NO TARGET', subline 'NO AUTO-STOP · RELEASE WAND', no target marker on the gauge",
             !strcmp(hm.label,TR(TX_H_MILK_PROBE))&&!strcmp(hm.sub,TR(TX_H_WAND_NOSTOP))&&hm.gmark<0&&!st_has(hm.sub,"AUTO-STOP A")&&!st_has(hm.sub,"AUTO-STOP V"));
    st_check("WAND: NO STOP button (a STOP write would do nothing) and no start buttons; start is blocked (ACTIVITY IN PROGRESS)",
             !steam_stop_visible()&&hit_test(240,372,t)==T_NONE&&hit_test(210,372,t)==T_NONE&&act_guard(ACT_BREW,t)==TR(TX_G_BUSY)&&act_guard(ACT_STEAM,t)==TR(TX_G_BUSY));
    { char v[DG_N][28]; uint32_t col[DG_N]; diag_top_text(v,col,t); st_check("DIAGNOSE: the PHASE cell shows the steam path (STEAM·WAND)",!strcmp(v[1],TR(TX_PH_STEAM_WAND))); }
    /* ---- (3) wand over the target: no auto-stop ---- */
    { int ok=1, never_done=1, sent=0; static const uint16_t milk[]={560,610,650,669,683,695,695,695,691,688};   /* past 650 and beyond, as in STEAMLIVE §6.2 */
      for(unsigned q=0;q<10;q++){ for(int k=0;k<3;k++){ st_s0(0x4121,0,0x000D,1,milk[q],t+=100); ui_touch(TE_DOWN,240,372,t); ui_frame(t); ui_touch(TE_UP,240,372,t); ui_frame(t+=33); wq_step(t);
              if(stm.kind!=SK_WAND||hm.chip!=TR(TX_PH_STEAM_WAND)||strcmp(hm.sub,TR(TX_H_WAND_NOSTOP))||hm.gmark>=0||steam_stop_visible()) ok=0;
              if(act.ph!=PH_STEAM) never_done=0; if(wq.n||txq_len||toast.dur) sent=1; } }
      st_check("WAND OVER TARGET: probe 56.0 -> 69.5° (target 65.0°) in 4 s — STEAM·WAND the whole way, never DONE, never an auto-stop text or a target",ok&&never_done&&reg[48]>steam_target());
      st_check("WAND OVER TARGET: a tap where the STOP button would be does nothing — merakiplus sends no frame (no stop write, no toast)",!sent&&wq.n_ok==0&&wq.n_block==0);
      st_s33(0x0101,t+=100); ui_frame(t);
      st_check("wand released (bits 5+14 cleared): DONE with a plain 'DONE IN …' — no post-stop rise note (that belongs to auto-stop)",
               act.ph==PH_DONE&&stm.kind==SK_NONE&&stm.last_kind==SK_WAND&&!st_has(hm.sub,"\xE2\x89\x88")&&!strcmp(hm.label,TR(TX_H_MILK_PROBE))); }
    /* ---- (4) CONCURRENT phases: flush during brew, STEAMLIVE §6.2 row by row ---- */
    t=st_home(); reg[39]=0; reg[51]=360; reg[52]=360;
    st_s0(0x1107,0,0x000C,0,514,t+=100); ui_frame(t);                              /* brew: pre-infusion */
    ms_t tb0=act.t_act0; int pre_ok=act.ph==PH_PRE&&tb0==t;
    reg[39]=216; reg[51]=1301; reg[52]=191;
    st_s0(0x1307,0,0x000C,0,514,t+=20500); ui_frame(t);                            /* 239.726: extraction, wand OFF */
    int e0=act.ph==PH_EXT&&!hm.conc&&!hm.badge&&hm.sl[2]==TR(TX_ST_FLOW)&&hm.chip==ph_name(PH_EXT);
    st_s33(0x5327,t+=76); ui_frame(t);                                             /* 239.802: steam bits added on top of the brew */
    st_check("CONCURRENT (work_sta 5327 = brew 1+2+9+12 + steam 5+14, word 24 = 0): the contract's phase_raw() is STEAM (unchanged); HOME's main phase is EXTRACTION",
             pre_ok&&e0&&phase_raw(t)==PH_STEAM&&phase_main(t)==PH_EXT&&act.ph==PH_EXT&&act.raw==PH_EXT);
    st_check("CONCURRENT: BOTH named — chip = EXTRACTION, steam badge = STEAM·WAND (words 44/46 still 0: the wand path follows from word 24 = 0)",
             hm.conc&&hm.chip==ph_name(PH_EXT)&&hm.badge==TR(TX_PH_STEAM_WAND)&&stm.kind==SK_WAND&&!stm.wand);
    reg[39]=223; st_s0(0x5327,0,0x000D,1,523,t+=391); ui_frame(t);                 /* 240.193: wand ON */
    for(int q=0;q<14;q++){ st_s0(0x5327,0,0x000D,1,523,t+=100); ui_frame(t); }     /* v6.9: the hero is conditioned — let it settle at 22.3 (1.4 s is taken from the next gap) */
    { char h[16],m1[16]; fmt_fix(h,sizeof h,223,1); fmt_t10(a,sizeof a,523,1); snprintf(m1,sizeof m1,"%s\xC2\xB0",a); fmt_fix(a,sizeof a,sv(S_BREWW),1); snprintf(b,sizeof b,TR(TX_H_AUTOSTOP_G),a);
      st_check("CONCURRENT: the brew's gauge/weight stays (hero 22.3 G, progress ring, AUTO-STOP AT 36.0 G), and the milk temperature is in the stat row (TIME · ≈BAR* · MILK 52.3°)",
               stm.wand&&!strcmp(hm.hero,h)&&hm.unit==TR(TX_U_G)&&hm.pvis&&!strcmp(hm.sub,b)&&hm.sl[0]==TR(TX_ST_TIME)&&hm.sl[1]==TR(TX_ST_BAR)&&hm.sl[2]==TR(TX_ST_MILK)&&!strcmp(hm.svv[2],m1)); }
    reg[39]=262; st_s0(0x5327,0,0x000D,1,669,t+=1473-1400); ui_frame(t); reg[39]=272; st_s0(0x5327,0,0x000D,1,695,t+=480); ui_frame(t);   /* 241.666 / 242.146: probe above 650 */
    { fmt_t10(a,sizeof a,695,1); char m1[16]; snprintf(m1,sizeof m1,"%s\xC2\xB0",a);
      int sk[2], sn=stop_btns(t,sk);   /* v6.13: the brew has its own STOP button — the wand still has none */
      st_check("CONCURRENT + wand over the target (69.5° > 65.0°): still EXTRACTION + STEAM·WAND; no STEAM STOP (only v6.13's STOP BREW), no frame sent; the brew timer is not reset",
               hm.conc&&hm.badge==TR(TX_PH_STEAM_WAND)&&!strcmp(hm.svv[2],m1)&&act.ph==PH_EXT&&!steam_stop_visible()&&sn==1&&sk[0]==ACT_BREW&&hit_test(240,372,t)==T_STOP&&wq.n==0&&txq_len==0&&act.t_act0==tb0); }
    st_s33(0x1307,t+=768); ui_frame(t);                                            /* 242.914: wand released, the brew continues */
    st_check("wand released during the brew: badge gone, EXTRACTION continues without a DONE view; 3rd stat field is FLOW again; same brew timer",
             !hm.conc&&!hm.badge&&act.ph==PH_EXT&&stm.kind==SK_NONE&&hm.sl[2]==TR(TX_ST_FLOW)&&act.t_act0==tb0);
    reg[39]=352; st_s33(0x2101,t+=2936); ui_frame(t); st_s0(0x0101,0,0x000C,0,600,t+=180); ui_frame(t);
    { char tbx[24]; snprintf(tbx,sizeof tbx,"%ld.%ld",(long)(act.done_ms/1000),(long)((act.done_ms%1000)/100));
      st_check("the brew stops: DONE for the BREW (weight in hero, 'DONE IN' = the whole brew time from pre-infusion — not the steam time)",
               act.ph==PH_DONE&&act.last_ph==PH_EXT&&hm.unit==TR(TX_U_G)&&st_has(hm.sub,tbx)&&act.done_ms>26000&&act.done_ms<27500); }
    /* ---- (5) variants ---- */
    t=st_home(); reg[39]=120;
    st_s0(0x1307,0,0x000C,0,300,t+=100); ui_frame(t); st_s0(0x5327,1,0x000C,0,310,t+=100); ui_frame(t);
    { int sk[2], sn=stop_btns(t,sk);   /* v6.13: two buttons — left STOP BREW, right STOP STEAM */
      st_check("concurrent brew + LATCHED steam (word 24 = 1): the badge is STEAM·AUTO, and the STOP button is shown (latched steam can always be stopped) — v6.13: next to STOP BREW",
             hm.conc&&hm.badge==TR(TX_PH_STEAM_AUTO)&&act.ph==PH_EXT&&steam_stop_visible()&&sn==2&&sk[0]==ACT_BREW&&sk[1]==ACT_STEAM&&hit_test(160,372,t)==T_STOP&&hit_test(320,372,t)==T_STOPS); }
    t=st_home(); reg_seen[24]=0;
    st_s33(0x4121,t+=100); ui_frame(t);
    st_check("word 24 not read yet (sub33 replies only): neutral chip STEAM / DAMP, no auto-stop promise — and the STOP button is shown (always safe)",
             stm.kind==SK_UNKNOWN&&hm.chip==TR(TX_PH_STEAM)&&hm.sub[0]==0&&steam_stop_visible()&&hit_test(240,372,t)==T_STOP);
    st_s0(0x4121,0,0x000D,1,400,t+=50); ui_frame(t);
    st_check("… as soon as word 24 = 0 is read: STEAM·WAND",stm.kind==SK_WAND&&hm.chip==TR(TX_PH_STEAM_WAND)&&!steam_stop_visible());
    t=st_home(); reg[38]=123; reg[39]=7; reg[58]=640; reg_seen[51]=1; memset(&lt,0,sizeof lt); g_n=0; g_head=0;
    st_s0(0x0141,0,0x000C,0,300,t+=100); ui_frame(t); loop_graph(t);
    for(int q=0;q<4;q++){ st_s0(0x4161,0,0x000D,1,340,t+=100); ui_frame(t); loop_graph(t); }   /* v6.9: a source switch takes 300 ms (no UI start here) */
    st_check("grind + wand (0141 | 4020): the main phase is GRINDING with a steam badge, 3rd stat field = MILK, and the graph still takes the GRIND weight (word 38; v6.9: after 300 ms debounce; v6.15: the 300 ms of brew-scale samples are re-anchored away — 2 samples left, all word 38)",
             act.ph==PH_GRIND&&hm.conc&&hm.badge==TR(TX_PH_STEAM_WAND)&&hm.sl[2]==TR(TX_ST_MILK)&&hm.sl[1]==TR(TX_ST_CURRENT)&&g_n==2&&g_w[0]==123&&g_w[1]==123&&gr.resets==1&&gr.reanchors==1&&wsl.src==38&&wsl.n_sw==1);
    t=st_home(); reg[39]=200;
    st_s0(0x5327,0,0x000D,1,560,t+=100); ui_frame(t); ms_t t_st=stm.t0;
    st_s0(0x4121,0,0x000D,1,640,t+=9000); ui_frame(t);                             /* brew finished, wand still open */
    { char tbx[24]; ms_t el=t-t_st; snprintf(tbx,sizeof tbx,"%ld.%ld",(long)(el/1000),(long)((el%1000)/100));
      st_check("the brew ends while the wand is still open: the main phase becomes STEAM·WAND with no DONE in between; the TIME field is the STEAM's time",
               act.ph==PH_STEAM&&!hm.conc&&hm.chip==TR(TX_PH_STEAM_WAND)&&st_has(hm.svv[0],tbx)&&t_st==stm.t0); }
    last_rx_ms=t-2000; ui_frame(t+=10);
    st_check("the link drops during steam: OFFLINE, and the steam tracking is reset (no lingering STOP button)",act.ph==PH_OFFLINE&&stm.kind==SK_NONE&&!steam_stop_visible());
    t=st_home(); act.pending=ACT_STEAM; act.t_req=t; ui_frame(t+=33);
    snprintf(b,sizeof b,TR(TX_H_STARTING),TR(ACT_NAME[ACT_STEAM]));
    st_check("steam requested from the UI, bits not yet set: 'STARTING STEAM…' (display steam starts by itself ~0.2 s after the write; the wand does not need to be opened)",!strcmp(hm.sub,b));
    act.pending=ACT_NONE;
    { int ok=1; for(int lg=0;lg<NLANG;lg++){ int kl=ui_lang; lang_set(lg);
          for(int k=SK_UNKNOWN;k<=SK_WAND;k++){ float w=text_wf(steam_label(k),F_XS)+30; LAY(w,254,"steam badge (label field 260)",steam_label(k)); }
          lang_set(kl); }
      st_check("the steam badge fits in the label field in both languages (all three texts)",ok); }
    sim_reset();
}

/* ---- 64-bit clock: wraparound test. B31 = 2^31 ms (where v6.5's 32-bit 'long' wrapped: 24.86 days), B32 = 2^32 ms (49.7 days) ---- */
#define B31 ((ms_t)2147483648LL)
#define B32 ((ms_t)4294967296LL)
typedef struct { int n_fast,n_slow,n_ext,frames; ms_t gap_max,last_fast; } WrapStat;
/* one iteration of main(): poll timers + (if due) a frame — with the SAME functions main() uses */
static void wrap_iter(ms_t t,WrapStat*s){
    int l0=txq_len; loop_poll(t);
    for(int i=l0;i+7<=txq_len;i+=7){ uint8_t sub=txq[i+3];
        if(sub==0||sub==33){ s->n_fast++; if(s->last_fast&&t-s->last_fast>s->gap_max) s->gap_max=t-s->last_fast; s->last_fast=t; }
        else if(sub==56) s->n_slow++; else if(sub==112) s->n_ext++; }
    txq_len=0;
    last_rx_ms=t;                                               /* the link answers */
    if(loop_render_due(t)){ ui_frame(t); loop_rendered(t,1.0f); s->frames++; }
}
/* run the main loop from B-3000 to B+5000: 2 ms steps, and ONE long step (a slow frame) from B-12 to B+28, across
 * the poll, slow and ext deadlines, which are all due at B-10 */
static WrapStat wrap_loop(ms_t B){
    WrapStat s; memset(&s,0,sizeof s); st_home(); memset(&lt,0,sizeof lt); txq_len=0; ui_last=B-3010-33;   /* same UI state in every run: only the clock differs */
    ms_t t=B-3010;                                             /* first iteration: all timers fire; ext is then due at B-2010, B-1010, B-10 */
    for(;t<B-12;t+=2) wrap_iter(t,&s);
    wrap_iter(B-12,&s);
    for(t=B+28;t<=B+5000;t+=2) wrap_iter(t,&s);
    return s;
}
static int wrap_hold(ms_t t0){          /* hold-to-start across t0+650: 1 = started exactly at 650 ms */
    st_home(); ms_t t=t0; last_rx_ms=t; ui_frame(t);
    ui_touch(TE_DOWN,210,372,t); int early=0;
    for(;t<t0+649;t+=7){ last_rx_ms=t; ui_frame(t); if(tch.fired) early=1; }
    last_rx_ms=t0+649; ui_frame(t0+649); int before=!tch.fired&&act.pending==ACT_NONE;
    last_rx_ms=t0+650; ui_frame(t0+650); int at=tch.fired&&act.pending==ACT_BREW&&wq.n>=1&&wq.q[wq.n-1].sub==5&&wq.q[wq.n-1].len==18;
    ui_touch(TE_UP,210,372,t0+660); return !early&&before&&at;
}
static int wrap_stepper(ms_t t0){       /* hold + for 2.6 s on BREW WEIGHT: returns the editor value (repeat with acceleration) */
    st_home(); page=PG_T0; ms_t t=t0; last_rx_ms=t; ui_frame(t); detail_open(S_HOTW,t); tr.type=0; edit_val=SET[S_HOTW].lo;
    ui_touch(TE_DOWN,396,184,t);
    for(;t<=t0+2600;t+=33){ last_rx_ms=t; ui_frame(t); }
    int v=edit_val; ui_touch(TE_UP,396,184,t); detail_close(t); tr.type=0; return v;
}
static void st_wrap(void){
    printf("== selftest: v6.6 64-bit clock — wraparound at 2^31 ms (24.86 days: where v6.5's 32-bit 'long' wrapped) and 2^32 ms (49.7 days) ==\n");
    char nm[420];
    { struct timespec a={2592000,999000000}, b={4294967,296000000}, c={3155760000LL,0};      /* 30 days · 2^32 ms · 100 years */
      st_check("the time base is 64 bit, and second->ms is computed in 64 bit: 30 days = 2 592 000 999 ms, 4 294 967.296 s = 2^32 ms, 100 years = 3 155 760 000 000 ms",
               sizeof(ms_t)==8&&sizeof now_ms()==8&&ts_ms(&a)==(ms_t)2592000999LL&&ts_ms(&b)==B32&&ts_ms(&c)==(ms_t)3155760000000LL); }
    { ms_t a=now_ms(), b=now_ms(); st_check("now_ms() is monotonic (CLOCK_MONOTONIC) and non-negative",b>=a&&a>=0); }
    st_home();
    /* ---- the main loop's timers ---- */
    WrapStat ref=wrap_loop((ms_t)50000000), w31=wrap_loop(B31), w32=wrap_loop(B32), wfar=wrap_loop((ms_t)1<<45);
    snprintf(nm,sizeof nm,"the main loop's poll across 2^31 ms (a 40 ms step skips B-10, where the poll, slow and ext deadlines are): %d + %d + %d queries in 8 s, largest gap %ld ms — same as far from the boundary (%d + %d + %d, %ld ms)",
             w31.n_fast,w31.n_slow,w31.n_ext,(long)w31.gap_max,ref.n_fast,ref.n_slow,ref.n_ext,(long)ref.gap_max);
    st_check(nm,w31.n_fast==ref.n_fast&&w31.n_slow==ref.n_slow&&w31.n_ext==ref.n_ext&&w31.gap_max==ref.gap_max&&ref.n_fast>=155&&ref.n_slow>=38&&ref.n_ext>=8&&ref.gap_max<=50+40);   /* the gap = 50 ms + the long step */
    snprintf(nm,sizeof nm,"the render timer across 2^31 ms: %d frames in 8 s (far from the boundary: %d) — no frozen screen",w31.frames,ref.frames);
    st_check(nm,w31.frames==ref.frames&&ref.frames>=75);
    st_check("same across 2^32 ms and at 2^45 ms (1115 years): poll and frame counts are identical to the reference",
             w32.n_fast==ref.n_fast&&w32.n_slow==ref.n_slow&&w32.n_ext==ref.n_ext&&w32.frames==ref.frames&&w32.gap_max==ref.gap_max&&wfar.n_fast==ref.n_fast&&wfar.frames==ref.frames);
    /* ---- keep-awake ---- */
    { int ok=1; static const ms_t Bs[2]={B31,B32}; ms_t at[2][4]; int n[2];
      for(int q=0;q<2;q++){ ms_t B=Bs[q], t=B-20000; st_home(); memset(&wq,0,sizeof wq); txq_len=0; act.pending=ACT_NONE; n[q]=0;
          last_rx_ms=t; proto_keepawake(t,1); wq_step(t); st_ack_last(t+10); wq_step(t+12); st_ack_last(t+20); wq_step(t+22);      /* startup: PowerOn + awake; next deadline B+10000 */
          int k0=(int)wq.n_ok;
          for(t+=24;t<=B+45000;t+=2){ last_rx_ms=t; proto_keepawake(t,0); wq_step(t);
              if(wq.n&&wq.state==WQ_WAIT&&wq.sent_ms==t&&wq.tries==1){ if(wq.q[0].sub!=1||wq.q[0].len!=1||wq.q[0].w[0]!=0) ok=0; if(n[q]<4) at[q][n[q]]=t-B; n[q]++; }
              if(wq.n&&wq.state==WQ_WAIT&&t-wq.sent_ms>=20) st_ack_last(t); }
          if(wq.n_ok-k0!=n[q]||wq.n_fail||wq.n_retx) ok=0; }
      snprintf(nm,sizeof nm,"keep-awake (word 1 = 0 every 30 s) across 2^31 and 2^32 ms: exactly 2 writes in 65 s, at B+%ld and B+%ld ms (2^32: B+%ld / B+%ld) — no pause, no stream of writes",
               (long)at[0][0],(long)at[0][1],(long)at[1][0],(long)at[1][1]);
      st_check(nm,ok&&n[0]==2&&n[1]==2&&at[0][0]>=10000&&at[0][0]<=10004&&at[0][1]-at[0][0]>=30000&&at[0][1]-at[0][0]<=30004&&at[1][0]==at[0][0]&&at[1][1]==at[0][1]); }
    /* ---- ACK budget and RX resync window ---- */
    { st_home(); memset(&wq,0,sizeof wq); txq_len=0; ms_t t=B31-100; wq_push(11,(uint16_t[1]){880},1); wq_step(t);
      wq_step(B31+49); int r0=wq.n_retx==0; wq_step(B31+50); int r1=wq.n_retx==1&&wq.tries==2; wq_step(B31+349); int f0=wq.n_fail==0&&wq.n==1; wq_step(B31+350);
      st_check("the write queue's ACK budget across 2^31 ms: sent B-100, retransmit exactly at +150 ms (B+50), given up exactly 300 ms later (B+350)",r0&&r1&&f0&&wq.n_fail==1&&wq.n==0&&wq.result==WR_FAIL);
      rx_zero(); static const uint8_t cand[9]={0xAA,0x55,0x83,0x00,0x38,1,0,2,0};                 /* candidate promising 119 bytes */
      rx_feed(cand,9,B32-200); rx_tick(NULL,0,B32+199); int w0=rx_resync==0&&rxlen==9; rx_tick(NULL,0,B32+200);
      st_check("the RX resync window (400 ms) across 2^32 ms: incomplete candidate from B-200 is still there at B+199 and is discarded exactly at B+200",w0&&rx_resync==1&&rxlen==0);
      rx_zero(); }
    /* ---- hold-to-start + stepper repeat ---- */
    st_check("hold-to-start (650 ms) across 2^31 and 2^32 ms: nothing at 649 ms, the brew block (sub5 len18) queued exactly at 650 ms",wrap_hold(B31-300)&&wrap_hold(B32-300)&&wrap_hold((ms_t)70000000));
    { int vr=wrap_stepper((ms_t)80000000), v1=wrap_stepper(B31-1000), v2=wrap_stepper(B32-1000);
      snprintf(nm,sizeof nm,"hold on + for 2.6 s (repeat with acceleration) across 2^31 and 2^32 ms: same value as far from the boundary (%d = %d = %d)",v1,v2,vr);
      st_check(nm,v1==vr&&v2==vr&&vr>SET[S_HOTW].lo+10*SET[S_HOTW].step); }
    /* ---- toast, page transition, frame time ---- */
    { st_home(); ms_t t=B31-1000; last_rx_ms=t; toast_show("WRAP",C_OK,t); int on0=!r_empty(toast_rect(t+1))&&!r_empty(toast_rect(B31+1399)), off=r_empty(toast_rect(B31+1400));
      st_check("toast (2400 ms) across 2^31 ms: visible from B-1000 to B+1399, gone at B+1400",on0&&off);
      st_home(); t=B32-100; last_rx_ms=t; ui_frame(t); go_page(PG_T0,1,t); int in0=tr.type==TR_SLIDE; last_rx_ms=B32+100; ui_frame(B32+100); int mid=tr.type==TR_SLIDE; last_rx_ms=B32+161; ui_frame(B32+161);
      st_check("page transition (260 ms) across 2^32 ms: in progress at B+100, finished at B+161 — the page has changed",in0&&mid&&tr.type==TR_NONE&&page==PG_T0);
      memset(&lt,0,sizeof lt); ft_max=0; loop_rendered(B31-1000,42.0f); loop_rendered(B31+3999,1.0f); int keep=ft_max==42.0f; loop_rendered(B31+4002,1.0f);
      st_check("the frame time statistics (MAX reset every 5 s) across 2^31 ms: max is held until B+3999 and reset after 5 s",keep&&ft_max==1.0f&&ft_last==1.0f); }
    /* ---- differences over 2^31 ms: v6.5's 32-bit differences went negative after 24.86 days ---- */
    { const ms_t D=B31+60000;                                   /* 24.86 days + 1 min. later */
      ms_t t0=st_home(); last_rx_ms=t0; int up0=link_up(t0+10), dead=!link_up(t0+D);
      st_check("a link that has been silent for 24.86 days is still DOWN (32-bit: the difference went negative and the link looked 'up')",up0&&dead);
      t0=st_home(); toast_show("OLD",C_OK,t0); last_rx_ms=t0+D; int gone=r_empty(toast_rect(t0+D));
      act.t_done=t0; act.raw=PH_READY; act.ph=PH_READY; ui_frame(t0+D);
      st_check("24.86 days after the last activity and the last toast: no DONE chip and no toast coming back",gone&&act.ph==PH_READY&&!(toast.dur&&(t0+D)-toast.t0<toast.dur+50));
      rx_bad=3; last_bad_ms=t0; int crc=(t0+D)-last_bad_ms<10000&&rx_bad>0; rx_bad=0; last_bad_ms=-100000;
      st_check("24.86 days after the last CRC error: the CRC warning on HOME does not come back",!crc); }
    /* ---- seconds since boot + /proc start time ---- */
    { char root[300], p[400]; snprintf(root,sizeof root,"/tmp/merakiplus_v6_selftest_%d/wrap",(int)getpid()); snprintf(p,sizeof p,"%s/proc/4711",root); mk_p(root,"/proc/4711");
      char ksr[sizeof sysroot]; strcpy(ksr,sysroot); snprintf(sysroot,sizeof sysroot,"%s",root);
      mock_put(root,"/proc/4711/stat","4711 (demo_v9) T 1 4711 4711 0 -1 4194560 0 0 0 0 0 0 0 0 20 0 1 0 300000000000 1404928 91 4294967295\n",120,0644);
      char stt='?'; int64_t s=proc_stat(4711,&stt); strcpy(sysroot,ksr);
      int64_t kf=mono_fake; mono_fake=(int64_t)5000000000LL; int64_t m=mono_s(); mono_fake=kf;
      st_check("/proc start time and seconds since boot are 64 bit: 300 000 000 000 ticks = 3 000 000 000 s (32-bit atol: saturated at 2^31 after 248 days); mono_s 5 000 000 000",
               s==(int64_t)3000000000LL&&stt=='T'&&m==(int64_t)5000000000LL); }
    { uint32_t k=rx_ok; rx_ok=0xFFFFFFFFu; rx_zero(); rx_ok=0xFFFFFFFFu; uint8_t f[128]; rx_feed(f,rx_v(f,1),(ms_t)1000); int z=rx_ok==0; rx_zero(); rx_ok=k;
      st_check("the RX counters are uint32 and wrap in a well-defined way (4 294 967 295 + 1 = 0) — no signed overflow after years in service",z); }
    sim_reset();
}

/* ---- MCU restart -> re-push ---- */
#define BPV_B_REAL "aa 55 06 70 03 04 01 00 06 46 00 c7 ce"   /* v6.7: = stock's captured frame (boot/ondevice/GROUND-TRUTH.md) */
typedef struct { ms_t t; uint16_t ws, p[3]; int have_p, ack, deaf; int n_a, n_b, n_other, n_verq, n_vera; ms_t t_a[12]; } RpSim;
static const uint8_t ST_VREQ[8]={0xAA,0x55,0x89,0x01,0x00,0x00,0x6F,0x81};
/* one iteration of main() in main()'s order: RX, write queue, boot push, re-push. The "simulated MCU": status (word 33) every
 * 50 ms, words 112..114 every 1 s, ACK 20 ms after a write (ack=1) — and it keeps what is written to words 112..114 */
static void rp_tick(RpSim*r){
    r->t+=2;
    if(r->t%50==0) st_status(r->ws,r->t);
    if(r->have_p&&r->t%1000==0){ uint8_t f[16]; rx_feed(f,mk_frame(f,0x83,112,r->p,3),r->t); }
    if(r->ack&&wq.n&&wq.state==WQ_WAIT&&r->t-wq.sent_ms>=20){ if(wq.q[0].sub==112&&wq.q[0].len==3&&!r->deaf) memcpy(r->p,wq.q[0].w,sizeof r->p); st_ack_last(r->t); }
    rx_feed(NULL,0,r->t); wq_step(r->t); boot_push_step(r->t); repush_step(r->t);
    if(wq.n&&wq.state==WQ_WAIT&&wq.sent_ms==r->t&&wq.tries==1){
        if(wq.q[0].sub==1&&wq.q[0].len==30){ if(r->n_a<12) r->t_a[r->n_a]=r->t; r->n_a++; } else if(wq.q[0].sub==112&&wq.q[0].len==3) r->n_b++; else r->n_other++; }
    for(int i=0;i+2<txq_len;i++) if(txq[i]==0xAA&&txq[i+1]==0x55&&txq[i+2]==0x09) r->n_vera++;
    txq_len=0;
}
static void rp_run(RpSim*r,ms_t ms){ ms_t e=r->t+ms; while(r->t<e) rp_tick(r); }
/* MCU restart: the register file at its start values, and n version requests 20 ms apart */
static void rp_reboot(RpSim*r,int n){
    r->p[0]=0; r->p[1]=0; r->p[2]=50; uint8_t f[16]; rx_feed(f,mk_frame(f,0x83,112,r->p,3),r->t);
    for(int k=0;k<n;k++){ rx_feed(ST_VREQ,8,r->t); r->n_verq++; rp_run(r,20); }
}
static void rp_init(RpSim*r,ms_t T,int guard){
    st_bp_reset(); memset(&rps,0,sizeof rps); ver_n=0; ver_ms=0; rx_n112=0; bp_guard_cfg=guard; bp_on=1;
    memset(r,0,sizeof *r); r->t=T; r->ws=0x0101; r->ack=1; r->have_p=1; r->p[2]=50;            /* the MCU is at its start values (slot B boot) */
    boot_push_start(guard);
}
static void st_repush(const char*cfgsrc){
    printf("== selftest: v6.6 MCU restart -> re-push (0x89 again / start-value signature; same blocks, same frame policy, same activity guard) ==\n");
    int bpon0=bp_on, g0=bp_guard_cfg, ro0=ro_mode; RpSim r; char nm[420]; const ms_t T=60000000;
    char dir[300], fx[340]; snprintf(dir,sizeof dir,"/tmp/merakiplus_v6_selftest_%d",(int)getpid()); mkdir(dir,0755); snprintf(fx,sizeof fx,"%s/repush_fixture.json",dir);
    snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); settings_load();
    /* ---- (1) the first handshake is NOT a restart ---- */
    rp_init(&r,T,1);
    for(int k=0;k<3;k++){ rx_feed(ST_VREQ,8,r.t); r.n_verq++; rx_feed(NULL,0,r.t); boot_push_step(r.t); repush_step(r.t); for(int i=0;i+2<txq_len;i++) if(txq[i]==0xAA&&txq[i+2]==0x09) r.n_vera++; txq_len=0; r.t+=20; }
    int quiet=wq.n==0&&bp.state==BP_WAIT_A&&!bp.t0_set;
    rp_run(&r,60);                                             /* status -> the guard is decided -> sub1 len30 queued */
    int started=bp.t0_set&&r.n_a==1;
    rx_feed(ST_VREQ,8,r.t); r.n_verq++;                        /* straggler from the first handshake, < 200 ms after the push went out */
    rp_run(&r,12000);
    st_check("startup: 3 version requests BEFORE the boot push + a straggler right after it = the first handshake — answered (4 x cmd 9), no re-push",
             quiet&&started&&r.n_vera==4&&r.n_a==1&&r.n_b==1&&bp.state==BP_DONE&&bp.ra==BPR_OK&&bp.rb==BPR_OK&&bp.runs==1&&repush_count()==0&&!rps.armed);
    st_check("the controller was at its start values (words 112..114 = 0 / 0 / 50) and now has the config's (0104 / 0600 / 70 — word 113 lo = 0 like stock); 12 s later nothing new has been sent",
             r.p[0]==0x0104&&r.p[1]==0x0600&&r.p[2]==70&&reg[112]==0x0104&&r.n_other==0&&wq.n_ok==2);
    /* ---- (2) MCU restart mid-session: 0x89 comes again ---- */
    int a0=r.n_a, b0=r.n_b; ms_t t_rb=r.t;
    rp_reboot(&r,5);                                           /* 5 requests in 100 ms; the last at t_rb+80 */
    int during=r.n_a==a0&&rps.armed&&repush_count()==0;
    while(r.t<t_rb+80+RP_SETTLE_MS-2) rp_tick(&r);
    int settle=r.n_a==a0&&repush_count()==0;                   /* 298 ms after the last 0x89: nothing yet */
    rp_run(&r,4); int begun=repush_count()==1&&rps.n_ver==1&&rps.last==RPC_VER&&bp.state!=BP_DONE;
    rp_run(&r,60);
    int a_sent=r.n_a==a0+1&&hexeq(last_tx,last_tx_len,BPV_A_REAL);
    rp_run(&r,40); int b_sent=r.n_b==b0+1&&hexeq(last_tx,last_tx_len,BPV_B_REAL);
    rp_run(&r,3000);
    st_check("MCU RESTART (0x89 again, 5 of them): all answered; nothing is sent until the round has been quiet for 300 ms and a FRESH work_sta has been read",
             during&&settle&&begun&&r.n_vera==4+5);
    st_check("RE-PUSH = the boot push: sub1 len30 (independent vector, 67 bytes) -> ACK -> sub112 len3 (… 04 01 00 06 46 00 c7 ce) -> ACK; OK / OK, 0 retransmit, 0 block",
             a_sent&&b_sent&&bp.state==BP_DONE&&bp.ra==BPR_OK&&bp.rb==BPR_OK&&bp.runs==2&&wq.n_ok==4&&wq.n_retx==0&&wq.n_fail==0&&wq.n_block==0&&r.n_other==0);
    st_check("after the re-push the controller is back on config (words 112..114 = 0104 / 0600 / 70, word 11 = 870); counter: 1 (cause 0x89); 3 s later nothing new",
             r.p[0]==0x0104&&r.p[1]==0x0600&&r.p[2]==70&&reg[11]==870&&reg[25]==650&&repush_count()==1&&rps.n_def==0&&r.n_a==a0+1);
    { char v[DG_N][28],lb[48],lb2[48],want[48]; uint32_t col[DG_N]; last_rx_ms=r.t; diag_top_text(v,col,r.t); uint32_t lc=diag_label(14,lb,sizeof lb), lc2=diag_label(15,lb2,sizeof lb2); snprintf(want,sizeof want,TR(TX_DL_BP_A_RE),1);
      st_check("DIAGNOSE: the boot push row is now called 'RE-PUSH 1 · 1-30' (accent colour) | '112-114'; the cells show the outcome of the latest push: OK / OK (green)",
               !strcmp(lb,want)&&st_has(lb,"RE-PUSH 1 ")&&lc==C_ACC&&!strcmp(lb2,TR(TX_DL_BP_B_RE))&&lc2==C_TXT3&&!strcmp(v[14],TR(TX_DV_OK))&&!strcmp(v[15],TR(TX_DV_OK))&&col[14]==C_OK&&col[15]==C_OK); }
    /* ---- (3) two restarts close together: at least RP_MIN_MS between two re-pushes ---- */
    { ms_t ta=r.t_a[r.n_a-1]; a0=r.n_a; rp_reboot(&r,3); rp_run(&r,1000); int held=r.n_a==a0&&rps.armed;   /* 4.2 s after the previous one: the round has settled, but the spacing is not reached */
      rp_run(&r,6000);
      snprintf(nm,sizeof nm,"new restart 3.2 s after the previous re-push: held back until %d ms have passed (here %ld ms between the two sub1 len30), then sent — counter 2",RP_MIN_MS,(long)(r.t_a[r.n_a-1]-ta));
      st_check(nm,held&&r.n_a==a0+1&&repush_count()==2&&r.t_a[r.n_a-1]-ta>=RP_MIN_MS&&r.t_a[r.n_a-1]-ta<RP_MIN_MS+200&&bp.ra==BPR_OK&&bp.rb==BPR_OK); }
    /* ---- (4) the activity guard also applies to the re-push ---- */
    rp_run(&r,6000); a0=r.n_a; b0=r.n_b;
    r.ws=0x4121; rp_reboot(&r,4); rp_run(&r,2000);              /* restart, and the user has the wand open (steam bits set) */
    { uint32_t c0; int skip=bp.state==BP_DONE&&bp.ra==BPR_SKIP&&bp.rb==BPR_SKIP&&r.n_a==a0&&r.n_b==b0&&rps.n_ver==3&&bp_text(0,&c0)==TR(TX_DV_BP_SKIP);
      rp_run(&r,6000); int still=r.n_a==a0&&rps.n_def==0;       /* the signature is present (0 / 0), but the machine is not idle */
      st_check("THE GUARD: restart while an activity is running (work_sta 4121) -> the re-push is SKIPPED, 0 bytes sent; the signature triggers nothing either while the activity runs",skip&&still);
      r.ws=0x0101; rp_run(&r,1900); int notyet=r.n_a==a0;       /* idle: 2 reads are not enough */
      rp_run(&r,2400);
      st_check("… and when the machine is idle, the START-VALUE SIGNATURE (word 112 = 0, word 113 = 0 in 3 reads) catches it: the push is sent — cause 'start values', OK / OK",
               notyet&&r.n_a==a0+1&&r.n_b==b0+1&&rps.n_def==1&&rps.last==RPC_DEF&&bp.ra==BPR_OK&&bp.rb==BPR_OK&&r.p[0]==0x0104); }
    /* ---- (5) the signature alone (0x89 round lost) ---- */
    rp_run(&r,6000); a0=r.n_a; int d0=rps.n_def;
    r.p[0]=0; r.p[1]=0; r.p[2]=50;                               /* restart without merakiplus seeing a 0x89 */
    { ms_t ts=r.t; while(r.n_a==a0&&r.t<ts+10000) rp_tick(&r); ms_t took=r.t-ts; rp_run(&r,3000);
      snprintf(nm,sizeof nm,"restart WITHOUT 0x89 (round lost): the signature triggers the re-push after 3 fresh reads (%ld ms) — controller back on config, counter +1",(long)took);
      st_check(nm,r.n_a==a0+1&&rps.n_def==d0+1&&took>=3000&&took<=4100&&r.p[0]==0x0104&&r.p[1]==0x0600&&bp.ra==BPR_OK); }
    { a0=r.n_a; d0=rps.n_def; rp_run(&r,6000); uint16_t keep[3]; memcpy(keep,r.p,sizeof keep);
      r.p[0]=0; r.p[1]=0; rp_run(&r,2100); r.p[0]=keep[0]; r.p[1]=keep[1]; rp_run(&r,1000); r.p[0]=0; r.p[1]=0; rp_run(&r,2000); int no=r.n_a==a0;
      memcpy(r.p,keep,sizeof keep); rp_run(&r,3000);
      st_check("the signature must hold for 3 reads IN A ROW: 2 x (0 / 0), 1 x config, 2 x (0 / 0) triggers nothing",no&&r.n_a==a0&&rps.n_def==d0); }
    /* ---- (6) budget: a controller that does not accept ---- */
    { rp_init(&r,T+10000000,1); rp_run(&r,3000); int base=r.n_a==1&&bp.ra==BPR_OK;
      r.deaf=1; r.p[0]=0; r.p[1]=0; r.p[2]=50;                   /* ACKs, but the values do not stick */
      ms_t ts=r.t; rp_run(&r,RP_WINDOW_MS-30000); int n1=rps.n_def, a1=r.n_a;
      ms_t gap=r.t_a[2]-r.t_a[1];
      rp_run(&r,30000+gap-1000); int n2=rps.n_def;               /* 10 min. after the FIRST one its slot is free again (the next one only 'gap' later) */
      snprintf(nm,sizeof nm,"BUDGET: the signature persists (the controller does not accept): %d re-pushes in the first %d.5 min. (%ld ms apart), then quiet; %d min. after the first, ONE new one is allowed — no endless stream of writes",
               n1,(int)((RP_WINDOW_MS-30000)/60000),(long)gap,(int)(RP_WINDOW_MS/60000));
      if(!(base&&n1==RP_BUDGET&&a1==1+RP_BUDGET&&n2==RP_BUDGET+1)) printf("     base=%d n1=%d a1=%d n2=%d gap=%ld\n",base,n1,a1,n2,(long)gap);
      st_check(nm,base&&n1==RP_BUDGET&&a1==1+RP_BUDGET&&gap>=RP_MIN_MS&&n2==RP_BUDGET+1&&r.t-ts>RP_WINDOW_MS); }
    /* ---- (7) restart IN THE MIDDLE of a push ---- */
    { rp_init(&r,T+20000000,1); rp_run(&r,60); int a_out=r.n_a==1&&bp.state==BP_A;
      rp_run(&r,24); int a_ok=bp.ra==BPR_OK;                     /* block A ACKed */
      r.ack=0; rp_run(&r,230);                                   /* the MCU restarts: block B gets no ACK … */
      rx_feed(ST_VREQ,8,r.t); r.n_verq++; rp_run(&r,20); rx_feed(ST_VREQ,8,r.t); r.n_verq++;   /* … and it asks for the version again (> 200 ms after the push started) */
      int armed=rps.armed&&bp.state!=BP_DONE;
      rp_run(&r,400); int nb=bp.rb==BPR_NOACK||bp.state!=BP_DONE;
      r.ack=1; rp_run(&r,6000);
      st_check("restart IN THE MIDDLE of the boot push (block A ACKed, block B without ACK, 0x89 again): the push is completed (NO ACK), and then BOTH blocks are sent again from the start — OK / OK",
               a_out&&a_ok&&armed&&nb&&r.n_a==2&&r.n_b==2&&rps.n_ver==1&&bp.state==BP_DONE&&bp.ra==BPR_OK&&bp.rb==BPR_OK&&r.p[0]==0x0104); }
    /* ---- (8) the SIM contract (no guard): postpone instead of skipping ---- */
    { rp_init(&r,T+30000000,0); rp_run(&r,3000); a0=r.n_a;
      r.ws=0x1307; rp_reboot(&r,3); rp_run(&r,8000); int wait=r.n_a==a0&&bp.state==BP_WAIT_A&&rps.n_ver==1;
      r.ws=0x0101; rp_run(&r,500);
      st_check("without the guard (the SIM contract): a re-push during an activity is POSTPONED and sent when the machine is idle",wait&&r.n_a==a0+1&&bp.ra==BPR_OK&&bp.rb==BPR_OK); }
    /* ---- (9) where there is NO re-push ---- */
    { int ok=1;
      st_fixture(fx,1200,1); snprintf(cfg_path,sizeof cfg_path,"%s",fx); settings_load();           /* config out of spec */
      rp_init(&r,T+40000000,1); rp_run(&r,3000); int blk=bp.ra==BPR_BLOCKED; long nb0=wq.n_block; a0=r.n_a;
      r.p[0]=0; r.p[1]=0; rp_run(&r,20000); if(!(blk&&r.n_a==a0&&rps.n_def==0&&wq.n_block==nb0&&!repush_cfg_sane())) ok=0;
      snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); settings_load(); unlink(fx);
      rp_init(&r,T+50000000,1); ro_mode=1; rp_run(&r,3000); rp_reboot(&r,3); rp_run(&r,20000); if(r.n_a||r.n_b||repush_count()||r.n_vera!=3) ok=0; ro_mode=0;
      rp_init(&r,T+60000000,1); memset(&bp,0,sizeof bp); bp_on=0; rp_run(&r,3000); rp_reboot(&r,3); rp_run(&r,20000); if(r.n_a||r.n_b||repush_count()||r.n_vera!=3) ok=0; bp_on=1;
      st_check("no re-push: config out of spec (the block would be rejected again — no stream of BLOCK counts), MERAKIPLUS_READONLY, MERAKIPLUS_NOBOOTPUSH; 0x89 is still answered",ok); }
    { char la[48],lb[48]; int ok=1;
      for(int lg=0;lg<NLANG;lg++){ int kl=ui_lang; lang_set(lg); rps.n_ver=600; rps.n_def=600; diag_label(14,la,sizeof la); diag_label(15,lb,sizeof lb);
          float a=text_wf(la,F_XS), b2=text_wf(lb,F_XS); LAY(a/2+b2/2+8,DG_COL_R-DG_COL_L,"diagnose label pair with re-push counter",la); LAY(a,2*(DG_COL_L-40),"diagnose label L",la);
          if(!st_has(la,"RE-PUSH 999 ")) ok=0; rps.n_ver=rps.n_def=0; diag_label(14,la,sizeof la); diag_label(15,lb,sizeof lb); if(strcmp(la,TR(TX_DL_BP_A))||strcmp(lb,TR(TX_DL_BP_B))) ok=0;
          lang_set(kl); }
      st_check("DIAGNOSE: the label pair with the re-push counter (cap 999) does not touch in any language; without a re-push the row is unchanged 'BOOT PUSH 1-30' | 'BOOT PUSH 112-114'",ok); }
    /* ---- clean up ---- */
    bp_on=bpon0; bp_guard_cfg=g0; ro_mode=ro0; snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); settings_load();
    st_bp_reset(); memset(&rps,0,sizeof rps); ver_n=0; rx_n112=0; sim_reset();
}

/* ============================ v6.7: the net guard (wifi at startup + self-heal) ============================ */
/* The model (nm_*) stands in for wlan0/wpa_supplicant/udhcpc; the state machine is the same code as on the device.
 * The conf vectors are computed outside the program (python hashlib) — same template as the device's .conf_bak (61 bytes). */
#define NETV_HDR  "ctrl_interface=/tmp/wifi/run/wpa_supplicant\nupdate_config=1\n"
#define NETV_PSK  NETV_HDR "\nnetwork={\n\tssid=\"TestNet\"\n\tpsk=\"abcdefgh12\"\n\tkey_mgmt=WPA-PSK\n}\n"
#define NETV_PSK_MD5 "5a970359d863288e79358cae148dc9a9"
#define NETV_OPEN_MD5 "c1a621ab7f10b7829bb2b6865d9284b0"
static void st_net_run(ms_t*t,int ms){ for(int k=0;k<ms;k+=20){ *t+=20; net_step(*t); } }
static int st_net_cfg(const char*path,int on,const char*ssid_json,const char*pw_json){
    char b[600]; int n=snprintf(b,sizeof b,"{\n\t\"Temperature\":\t1,\n\t\"wifionoff\":\t%d,\n\t\"ssid\":\t\"%s\",\n\t\"password\":\t\"%s\",\n\t\"presetStore\":\t{\n\t\t\"ssid\":\t\"NOT-THIS-ONE\"\n\t},\n\t\"init\":\t1\n}",on,ssid_json,pw_json);
    return put_bytes(path,b,n);
}
static int st_file_md5(const char*path,char out[33]){
    static char b[8192]; int fd=open(path,O_RDONLY); if(fd<0){ strcpy(out,"-"); return -1; } int n=(int)read(fd,b,sizeof b); close(fd); if(n<0) n=0;
    md5_hex((const uint8_t*)b,(size_t)n,out); return n;
}
static void st_net(const char*dir){
    printf("== selftest: v6.7 the net guard (wifi at startup + self-heal; model of wlan0/wpa_supplicant/udhcpc) ==\n");
    char ksr[sizeof sysroot],kcfg[sizeof cfg_path],root[360],p[460],cf[460],conf[460],lg[460],m[33]; char t1[64]; uint32_t col;
    strcpy(ksr,sysroot); strcpy(kcfg,cfg_path);
    snprintf(root,sizeof root,"%s/netroot",dir); mkdir(root,0755);
    snprintf(p,sizeof p,"%s/tmp",root); mkdir(p,0755); snprintf(p,sizeof p,"%s/appconfigs",root); mkdir(p,0755);
    snprintf(sysroot,sizeof sysroot,"%s",root);
    snprintf(cf,sizeof cf,"%s/config.json",root); snprintf(cfg_path,sizeof cfg_path,"%s",cf);
    snprintf(conf,sizeof conf,"%s" NET_CONF,root); snprintf(lg,sizeof lg,"%s" NET_LOG,root);
    snprintf(p,sizeof p,"%s" NET_CONF_BAK,root); put_bytes(p,NETV_HDR "\n",61);
    ms_t T=50000000L, t;
    /* --- the conf format --- */
    st_net_cfg(cf,1,"TestNet","abcdefgh12"); net_cfg_load();
    { char b[4096]; int n=net_conf_build(b,sizeof b); md5_hex((const uint8_t*)b,(size_t)n,m);
      st_check("conf = the save_config format: the template's global lines + empty line + network={ ssid=\"…\" psk=\"…\" key_mgmt=WPA-PSK } (125 bytes, independent md5 5a970359…)",
               net.cfg==NC_OK&&n==125&&!memcmp(b,NETV_PSK,125)&&!strcmp(m,NETV_PSK_MD5)); }
    st_net_cfg(cf,1,"Caf\\u00e9",""); net_cfg_load();
    { char b[4096]; int n=net_conf_build(b,sizeof b); md5_hex((const uint8_t*)b,(size_t)n,m);
      st_check("ssid with JSON escape (\\u00e9 -> UTF-8) is written as hex without quotes, open network -> key_mgmt=NONE (105 bytes, md5 c1a621ab…)",net.cfg==NC_OK&&n==105&&!strcmp(m,NETV_OPEN_MD5)); }
    { int ok=1;
      st_net_cfg(cf,0,"TestNet","abcdefgh12"); net_cfg_load(); ok&=net.cfg==NC_WIFIOFF;
      st_net_cfg(cf,1,"","abcdefgh12"); net_cfg_load(); ok&=net.cfg==NC_NOSSID;
      st_net_cfg(cf,1,"TestNet","kort"); net_cfg_load(); ok&=net.cfg==NC_BADPSK;
      st_net_cfg(cf,1,"TestNet","abc\\u0001defghij"); net_cfg_load(); ok&=net.cfg==NC_BADPSK;
      unlink(cf); net_cfg_load(); ok&=net.cfg==NC_NOCFG;
      st_net_cfg(cf,1,"TestNet","abcdefgh12"); net_cfg_load(); ok&=net.cfg==NC_OK&&!strcmp(net.ssid,"TestNet");   /* presetStore's "ssid" (depth 2) is ignored */
      st_check("config guard: wifionoff 0 / empty ssid / password < 8 or with control characters / no config -> the guard is OFF; only the top-level keys are read",ok); }
    /* --- 1. healthy stack at start (runtime B): left alone --- */
    unlink(conf); unlink(lg); nm_reset(0); t=T; net_start(t,1); st_net_run(&t,60000);
    diag_net_text(t1,sizeof t1,&col);
    st_check("HEALTHY STACK at start (stock's wpa_supplicant 841 + ip): UP on the first probe, and for 60 s: 0 init, 0 wpa starts, 0 udhcpc, the conf file not even written",
             net.state==NS_UP&&nm.n_init==0&&nm.n_wpa==0&&nm.n_dhcp==0&&nm.n_prep==0&&nm.n_ifup==0&&access(conf,F_OK)!=0&&file_has(lg,"UP at start: wpa_supplicant pid 841"));
    st_check("DIAGNOSE NET row: '841 / 192.168.1.100 / UP' (green)",st_has(t1,"841 / 192.168.1.100 / ")&&st_has(t1,TR(TX_NS_UP))&&col==C_OK);
    /* --- 2. slot B cold start: no wlan0, no supplicant, no IP --- */
    nm_reset(1); t=T; net_start(t,1); net_step(t);
    int init1=nm.n_init==1&&net.step==NB_INIT&&net.state==NS_STARTING;
    diag_net_text(t1,sizeof t1,&col); int dstart=st_has(t1,"- / - / ")&&st_has(t1,TR(TX_NS_STARTING))&&col==C_WARN;
    ms_t up_at=0; for(int k=0;k<1500&&!up_at;k++){ t+=20; net_step(t); if(net.state==NS_UP) up_at=t; }
    st_file_md5(conf,m);
    st_check("COLD START (slot B): the init script runs ONCE (like MI_WLAN_Init), conf is written from config.json (= the vector), wpa_supplicant + udhcpc are started ONCE each",
             init1&&nm.n_init==1&&nm.n_wpa==1&&nm.n_dhcp==1&&net.n_conf==1&&!strcmp(m,NETV_PSK_MD5));
    snprintf(t1,sizeof t1,"UP %d ms after startup",(int)(up_at-T));
    st_check("COLD START: UP (ip 192.168.1.100) within 10 s in the model; DIAGNOSE showed STARTING along the way; the log has every step",
             up_at&&up_at-T<10000&&dstart&&net.n_boot==1&&net.p.ip==nm.lease&&file_has(lg,"missing: running \"source /config/wifi/ssw01bInit.sh dummy1 dummy2\"")&&file_has(lg,"written from config.json")&&file_has(lg,"started: wpa_supplicant -i wlan0 -Dnl80211 -c /appconfigs/wpa_supplicant.conf")&&file_has(lg,"started udhcpc -i wlan0 -s /etc/init.d/udhcpc.script -a -q -b -t 20 -T 1")&&file_has(lg,t1));
    st_check("the password never appears in the log",!file_has(lg,"abcdefgh12"));
    /* --- 3. wpa_supplicant killed (IP + route stay on wlan0) -> self-heal, same IP, no udhcpc --- */
    { int w0=nm.n_wpa,d0=nm.n_dhcp,c0=net.n_conf; uint32_t ip0=net.p.ip; st_net_run(&t,10000); ms_t tk=t;
      nm.wpa_pid=0; nm.dhcp_pid=0; nm.carrier=0;
      ms_t det=0,back=0; int pid0=0; for(int k=0;k<1500&&!back;k++){ t+=20; net_step(t); if(!det&&net.dead) det=t; if(det&&net.state==NS_UP){ back=t; pid0=net.p.wpa_pid; } }
      snprintf(t1,sizeof t1,"SELF-HEAL done in");
      st_check("KILL wpa_supplicant: detected within 2 s, bring-up after 3 s; NEW supplicant, SAME IP, no udhcpc (ip + route kept), conf not rewritten",
               det&&det-tk<=2100&&back&&nm.n_wpa==w0+1&&nm.n_dhcp==d0&&net.n_conf==c0&&net.p.ip==ip0&&pid0!=841&&pid0>0&&net.n_heal==1&&file_has(lg,"kept (no udhcpc)")&&file_has(lg,t1));
      snprintf(t1,sizeof t1,"heal in %d ms (limit 10 s in the model)",(int)(back-tk));
      st_check(t1,back-tk<10000&&net.heal_ms>0); }
    /* --- 4. wpa killed AND IP gone -> wpa + udhcpc (like stock) --- */
    { int w0=nm.n_wpa,d0=nm.n_dhcp; nm.wpa_pid=0; nm.carrier=0; nm.ip=0; nm.gw=0;
      for(int k=0;k<1500&&!(net.state==NS_UP&&nm.n_wpa>w0);k++){ t+=20; net_step(t); }
      st_check("KILL wpa_supplicant + IP gone: wpa_supplicant and then udhcpc (stock's order) -> UP, heal no. 2",net.state==NS_UP&&nm.n_wpa==w0+1&&nm.n_dhcp==d0+1&&net.n_heal==2); }
    /* --- 5. the supplicant is alive, the IP disappears -> udhcpc after 8 s; the supplicant is left alone --- */
    { int w0=nm.n_wpa,d0=nm.n_dhcp; ms_t tk=t; nm.ip=0; nm.gw=0; ms_t dh=0;
      for(int k=0;k<1500&&!(dh&&net.state==NS_UP);k++){ t+=20; net_step(t); if(!dh&&nm.n_dhcp>d0) dh=t; }
      st_check("NO IP (the supplicant is alive): udhcpc is started after approx. 8 s (not before), NO new wpa_supplicant -> UP",dh&&dh-tk>=8000&&dh-tk<=10500&&nm.n_wpa==w0&&nm.n_dhcp==d0+1&&net.state==NS_UP); }
    /* --- 6. ifconfig wlan0 down (the supplicant is alive, the routes gone) -> ifconfig up -> udhcpc for the route --- */
    { int w0=nm.n_wpa,d0=nm.n_dhcp,u0=nm.n_ifup; ms_t tk=t; nm.ifup=0; nm.carrier=0; nm.gw=0; ms_t upa=0;
      for(int k=0;k<2000&&!(upa&&net.state==NS_UP);k++){ t+=20; net_step(t); if(!upa&&nm.n_ifup>u0) upa=t; }
      st_check("WLAN0 DOWN (the supplicant is alive): 'ifconfig wlan0 up' after approx. 3 s, then udhcpc for the lost default route -> UP; NO new wpa_supplicant",
               upa&&upa-tk>=3000&&upa-tk<=5200&&nm.n_ifup==u0+1&&nm.n_dhcp==d0+1&&nm.n_wpa==w0&&net.state==NS_UP&&net.p.gw&&file_has(lg,"ifconfig wlan0 up (like MI_WLAN_Open)")); }
    /* --- 7. AP gone for 2 min: the supplicant reconnects by itself -> no intervention --- */
    { int w0=nm.n_wpa,d0=nm.n_dhcp,u0=nm.n_ifup,i0=nm.n_init; nm.ap=0; nm.carrier=0; st_net_run(&t,120000);
      int quiet=nm.n_wpa==w0&&nm.n_dhcp==d0&&nm.n_ifup==u0&&nm.n_init==i0&&net.state==NS_CONNECTING;
      nm.ap=1; nm.assoc_at=t+NM_ASSOC_MS; st_net_run(&t,4000);
      st_check("AP GONE for 2 min (no carrier, IP stays): no intervention at all — the supplicant reconnects by itself; then 'UP again … without intervention'",quiet&&net.state==NS_UP&&file_has(lg,"without intervention")); }
    /* --- 8. a network without a gateway is healthy too (no udhcpc stream) --- */
    { nm_reset(1); nm.gw_offer=0; t=T; net_start(t,1); st_net_run(&t,90000);
      st_check("network WITHOUT a default route (DHCP gives no router): UP after cold start, and for 90 s only the one udhcpc (no stream of restarts)",net.state==NS_UP&&nm.n_dhcp==1&&!net.gw_seen); }
    /* --- 9. the init script hangs: no kill, no second insmod --- */
    { nm_reset(1); nm.init_hang=1; t=T; net_start(t,1); st_net_run(&t,60000);
      int hung=nm.n_init==1&&net.step==NB_INIT&&net.init_hung&&file_has(lg,"it is not killed");
      nm.iface=1; st_net_run(&t,8000);
      st_check("THE INIT SCRIPT HANGS: after 25 s it is logged (no kill call, no new insmod); when wlan0 appears, it continues to wpa_supplicant -> UP",hung&&nm.n_init==1&&nm.n_wpa==1&&net.state==NS_UP&&net.orphan); }
    /* --- 10. wpa_supplicant will not start: backoff 5, 10, 20, 40, 60 s --- */
    { nm_reset(0); nm.wpa_pid=0; nm.carrier=0; nm.wpa_fail=1; t=T; net_start(t,1); st_net_run(&t,300000);
      st_check("wpa_supplicant does not start: backoff 5 -> 10 -> 20 -> 40 -> 60 s (cap) — 8 attempts in 5 min, not hundreds",nm.n_wpa>=6&&nm.n_wpa<=9&&net.backoff==NET_BACKOFF_MAX_MS&&net.state==NS_DOWN); }
    /* --- 11. wifi turned off in config: the guard touches nothing, not even a cold stack --- */
    { st_net_cfg(cf,0,"TestNet","abcdefgh12"); unlink(conf); nm_reset(1); t=T; net_start(t,1); st_net_run(&t,120000);
      diag_net_text(t1,sizeof t1,&col);
      st_check("wifionoff = 0: OFF — 0 init, 0 wpa, 0 udhcpc, conf not written; DIAGNOSE 'OFF' (neutral)",net.state==NS_OFF&&nm.n_init==0&&nm.n_wpa==0&&nm.n_dhcp==0&&access(conf,F_OK)!=0&&st_has(t1,TR(TX_NS_OFF))&&col==C_TXT2);
      net_start(t,0); nm_reset(1); st_net_run(&t,30000);
      st_check("MERAKIPLUS_NONET / test root: the guard is fully disabled (no probe, nothing started)",!net.on&&nm.n_init==0&&nm.n_wpa==0); }
    /* --- the NET row fits in both languages, also in the worst case (5-digit pid, longest IP, longest state) --- */
    { int ok=1, kl=ui_lang;
      for(int lgx=0;lgx<NLANG;lgx++){ lang_set(lgx); for(int st=NS_OFF;st<=NS_UP;st++){ char w[96]; snprintf(w,sizeof w,"32767 / 255.255.255.255 / %s",TR(TX_NS_OFF+st)); LAY(text_wf(w,diag_net_font(w)),DG_FULL_W,"NET row",w); }
        LAY(text_wf(TR(TX_DG_L_NET),F_XS),DG_FULL_W,"NET-label",TR(TX_DG_L_NET)); }
      lang_set(kl);
      { char w[64]; snprintf(w,sizeof w,"841 / 192.168.1.100 / %s",TR(TX_NS_UP)); ok&=diag_net_font(w)==F_S; }   /* the healthy line is in F_S */
      st_check("DIAGNOSE NET row fits in EN and DA: the healthy line in F_S; in the worst case ('32767 / 255.255.255.255 / CONNECTING') it switches to F_XS and stays within the full-width field",ok); }
    /* cleanup: back to the preview default (healthy stack) */
    strcpy(sysroot,ksr); strcpy(cfg_path,kcfg);
    nm_reset(0); memset(&net,0,sizeof net); net.on=1; net.cfg_logged=-1; nh_probe(&net.p,sim_now,1); net.state=NS_UP; net.booted=net.was_up=net.gw_seen=1;
}
/* ====================================================================== */
/* ===== v6.9: display conditioning + stable weight source + deadband ===== */
/* ====================================================================== */
/* synthetic sequences (like sim/mcu_sim.py + BREW.md/STEAMLIVE.md): brew plateau with ADC noise and deadhead pulses, steps, idle with
 * maintenance pulses. The tests read the ring buffer (raw g_* and displayed g_*d), the hero/stat model and the chart scale (cst). */
static int cs_ws=0x1307, cs_p=1320, cs_w39=0, cs_w38=0, cs_pulse=1, cs_glitch=0;
static void tick_cs(ms_t t){
    reg[33]=(uint16_t)cs_ws; reg[39]=(uint16_t)cs_w39; reg[38]=(uint16_t)cs_w38;
    int p=cs_p+((t/97)%41)-20;                                         /* ADC noise ±20 */
    if(cs_pulse&&t>=500&&(t%4000)<100) p=16000;                                /* deadhead pulse, 1 sample every 4 s */
    if(cs_pulse&&(t%6000)>=2500&&(t%6000)<2700) p=15500;               /* deadhead pulse, 2 samples every 6 s (together < 10 % of samples) */
    reg[51]=(uint16_t)(p<0?0:p); reg[52]=cs_ws==0x0101?0:360;
}
/* idle + maintenance pulses (work_sta 0x0101 | 0x0400/0x0800/0x2000/0x0C00/0x2800 — BREW.md §5, mcu_sim.py) and optionally one
 * single work_sta sample with bit 6 (glitch) — the weight words stay put: 38 = last grind, 39 = last cup */
static void tick_idle_maint(ms_t t){
    static const uint16_t PB[5]={0x0400,0x0800,0x2000,0x0C00,0x2800};
    int k=(int)((t/1700)%5), on=(t%1700)<250;
    reg[33]=(uint16_t)(0x0101|(on?PB[k]:0)); reg[51]=on?(uint16_t)(3000+k*2700):(uint16_t)(t%2); reg[52]=0;
    if(cs_glitch&&t>=cs_glitch&&t<cs_glitch+100) reg[33]=0x0141;         /* ONE sample with bit 6 */
    reg[38]=(uint16_t)cs_w38; reg[39]=(uint16_t)cs_w39;
}
static int g_last(const int*a){ return a[(g_head+GN-1)%GN]; }
static float g_lastf(const float*a){ return a[(g_head+GN-1)%GN]; }
static void st_chart_now(void){ home_model(sim_now); static_dirty=1; force_all=1; ui_frame(sim_now+1); }
static void st_cond(void){
    printf("== selftest: v6.9 display conditioning (median-3 + EMA) + robust chart scale + stable weight source + weight deadband ==\n");
    char a[32],b[64];
    /* ---- (1) spike train: brew plateau 1320 ± 20 with deadhead pulses 16000 (1 sample / 4 s) and 15500 (2 samples / 6 s) ----
     *      (the preview harness's sample rate is 132 ms; the pulses are together approx. 8 % of the active samples — p90 tolerates < 10 %) */
    sim_reset(); cs_ws=0x1307; cs_p=1320; cs_w39=200; cs_pulse=1; sim_tick=tick_cs; sim_run(20000); st_chart_now();
    { float dmax=0; int rmax=0; for(int i=0;i<110;i++){ int k=(g_head+GN-1-i)%GN; if(g_pd[k]>dmax) dmax=g_pd[k]; if(g_p[k]>rmax) rmax=g_p[k]; }
      snprintf(b,sizeof b,"%.0f",(double)dmax);
      st_check("SPIKE TRAIN: raw peak >= 15500, DISPLAYED peak <= plateau + 5 % (median-3 removes the 1-sample pulse, the pulse gate the 2-sample pulse)",rmax>=15500&&dmax<=1340*1.05f);
      printf("       raw max %d · displayed max %s (last 110 samples = 14.5 s)\n",rmax,b);
      st_check("SPIKE TRAIN: chart top = p90 of active samples x 1.15 (1340..1800) — the curve is not flattened; the pulses are shown as ticks at the top edge",
               cst.active&&cst.mode==0&&cst.top>=1340&&cst.top<=1800&&cst.ticks>0&&cst.disp_max<=1400&&cst.na>140);
      { int keep=cd.gate; cd.gate=0; sim_reset(); cs_ws=0x1307; cs_p=1320; cs_w39=200; cs_pulse=1; sim_tick=tick_cs; sim_run(20000); float g0=0; for(int i=0;i<110;i++){ int k=(g_head+GN-1-i)%GN; if(g_pd[k]>g0) g0=g_pd[k]; }
        cd.gate=keep; sim_reset(); cs_ws=0x1307; cs_p=1320; cs_w39=200; cs_pulse=1; sim_tick=tick_cs; sim_run(20000); st_chart_now();
        st_check("… control: without the pulse gate (cd.gate=0) the 2-sample pulse gets through median-3 (displayed > 3000) — the gate is what keeps it down",g0>3000);
        printf("       without gate: displayed max %.0f\n",(double)g0); }
      int top_pct=cst.top; cd.raw=1; st_chart_now(); int rawtop=cst.top, rawd=cst.disp_max; cd.raw=0; st_chart_now();
      printf("       chart top: smooth (p90) %d · RAW = v6.8 (raw series + max scale) %d\n",top_pct,rawtop);
      st_check("… vs v6.8 = DIAGNOSE → GRAPH = RAW (unconditioned series, max scale): top >= 16000 x 1.08, so the plateau filled less than 1/10 of the height; back to SMOOTH afterwards",
               rawd>=15500&&rawtop>=16000*1.08f-1&&1340*10<rawtop&&!cd.raw&&cst.top==top_pct); }
    /* ---- (1b) the first seconds of an extraction while the window is mostly NOT brew. v6.15: idle -> brew clears the graph, so this
     * now only happens when the brew starts during steam (no idle -> active transition): 36 s of steam (pressure 0), then brew + steam ---- */
    sim_reset(); cs_ws=0x4121; cs_p=0; cs_w39=0; cs_pulse=0; sim_tick=tick_cs; sim_run(36000); cs_ws=0x5327; cs_p=1320; sim_run(3000); st_chart_now();
    { int all[GN],n=0; ms_t t0=sim_now-40000; for(int i=0;i<g_n;i++){ int k=(g_head+GN-g_n+i)%GN; if(g_t[k]>=t0) all[n++]=g_p[k]; } qsort(all,(size_t)n,sizeof all[0],cmp_int);
      int p90all=all[(n-1)*90/100];
      st_check("3 s into an extraction: p90 of ACTIVE samples keeps the plateau in scale (top >= 1320), where p90 of the WHOLE window would be ~0 and squash the curve against the ceiling",
               cst.top>=1320&&cst.top<=1800&&p90all<40&&cst.clip==0); printf("       p90 whole window %d · top %d\n",p90all,cst.top); }
    /* ---- (2) step within the same phase: 360 -> 1320 (no phase change = no snap) ---- */
    sim_reset(); cs_ws=0x1307; cs_p=360; cs_w39=100; cs_pulse=0; sim_tick=tick_cs; sim_run(5000);
    { int i0=g_n; cs_p=1320; sim_run(4000); int n=g_n-i0, settle=-1, over=0; float first=g_pd[(g_head+GN-n)%GN];
      for(int i=0;i<n;i++){ int k=(g_head+GN-n+i)%GN; if(settle<0&&fabsf(g_pd[k]-(float)g_p[k])<=0.02f*1320) settle=i; if(g_pd[k]>1340+0.5f) over=1; }
      st_check("STEP 360 -> 1320 within the same phase: first displayed sample is still at 360 (median-3), no overshoot, within 2 % in <= 2.5 s (25 samples)",first<400&&!over&&settle>=0&&settle<=25);
      printf("       settled after %d samples (%.1f s)\n",settle,settle/10.0); }
    /* ---- (3) snap on phase change and set-point change ---- */
    sim_reset(); cs_ws=0x1107; cs_p=358; cs_w39=0xFFFD; cs_pulse=0; sim_tick=tick_cs; sim_run(3000);
    { cs_ws=0x1307; cs_p=1320; int i0=g_n; sim_run(400); int n=g_n-i0; float v[4]; int r[4]; for(int i=0;i<n&&i<4;i++){ int k=(g_head+GN-n+i)%GN; v[i]=g_pd[k]; r[i]=g_p[k]; }
      int ok=n>=3&&v[0]<400&&fabsf(v[1]-(float)med3(358,r[0],r[1]))<0.01f&&v[2]>1290;
      st_check("PHASE CHANGE (PRE-INF. 1107 -> EXTRACTION 1307): the first 2 samples follow median-3 directly (no EMA) — the step is complete on the 2nd sample, nothing is smeared out",ok);
      printf("       displayed %.0f / %.0f / %.0f  (raw %d / %d / %d)\n",(double)v[0],(double)v[1],(double)v[2],r[0],r[1],r[2]); }
    sim_reset(); cs_ws=0x1307; cs_p=1320; cs_w39=300; cs_pulse=0; sim_tick=tick_cs; sim_run(4000);
    { reg_seen[51]=1; cs_ws=0x0101; cs_p=0; sim_tick=NULL; reg[33]=0x0101; reg[51]=16000; reg[52]=0; int i0=g_n; sim_run(132); reg[51]=12; sim_run(1500);
      float mx=0; for(int i=g_n-i0-1;i>=0;i--){ int k=(g_head+GN-1-i)%GN; if(g_pd[k]>mx) mx=g_pd[k]; }
      st_check("PHASE CHANGE right in a deadhead pulse (brew stops, first idle sample = 16000): the pulse does not get through (median-3 at the change) — displayed max <= the plateau",mx<=1345); }
    sim_reset(); sim_tick=NULL; reg[40]=870; reg[11]=870; sim_run(3000); home_model(sim_now);
    { reg[40]=905; sim_run(100); home_model(sim_now); fmt_t10(a,sizeof a,905,1); int lag=strcmp(hm.hero,a)!=0;
      reg[11]=900; home_model(sim_now); int snap=!strcmp(hm.hero,a);
      st_check("SET-POINT CHANGE (boiler 87.0 -> 90.0): the hero jumps to the raw value (90.5) in the same frame; without a change it glides (median-3 + EMA)",lag&&snap); }
    sim_reset(); cs_ws=0x0101; cs_w38=0; cs_w39=367; sim_tick=tick_idle_maint; sim_run(3000);
    { cs_w38=12; tick_idle_maint(0); reg[33]=0x0141; sim_run(100); home_model(sim_now); }
    /* ---- (3b) idle after a brew with maintenance pulses: the pressure curve is flat, the pulses are ticks on the baseline ---- */
    sim_reset(); cs_ws=0x1307; cs_p=1320; cs_w39=300; cs_pulse=0; sim_tick=tick_cs; sim_run(10000);
    { cs_w38=0; cs_w39=367; sim_tick=tick_idle_maint; sim_run(20000); st_chart_now(); int ok=act.ph==PH_READY&&cst.active&&cst.mode==0;
      float mx=0; int rmx=0; for(int i=0;i<140;i++){ int k=(g_head+GN-1-i)%GN; float v=g_a[k]?g_pd[k]:0; if(v>mx) mx=v; if(g_p[k]>rmx) rmx=g_p[k]; }
      st_check("IDLE AFTER BREW with maintenance pulses (0.25 s, up to 13800): the chart still shows the brew; the 20 s of idle are drawn flat (0), the pulses as ticks on the baseline; scale = the brew plateau",
               ok&&rmx>=8000&&mx==0&&cst.bticks>0&&cst.top>=1320&&cst.top<=1800);
      sim_run(31000); st_chart_now();
      st_check("… when the brew is out of the 40 s window: 'NO ACTIVITY' (v6.8: every maintenance pulse lit up a spike chart)",!cst.active&&cst.bticks>0); }
    /* ---- (4) the weight source: maintenance pulses + one bit-6 glitch while idle — no flicker ---- */
    sim_reset(); cs_w38=186; cs_w39=367; cs_glitch=7300; sim_tick=tick_idle_maint;
    { int nphg=0,n0=g_n,bad=0,pulses=0; for(int q=0;q<200;q++){ sim_run(100); if(phase_main(sim_now)==PH_GRIND) nphg++; if((reg[33]&0x2C00)) pulses++; if(g_last(g_w)!=367) bad++; }
      cs_glitch=0;
      st_check("IDLE + maintenance pulses (0x0400/0x0800/0x2000/0x0C00/0x2800) + ONE bit-6 sample: the weight source stays word 39 (0 switches), all 200 samples = 36.7 g — no 18.6 g spike",
               g_n-n0>=195&&nphg>=1&&pulses>=20&&bad==0&&wsl.src==39&&wsl.n_sw==0);
      printf("       %d samples with phase GRIND (glitch) · %d with maintenance bits · %d samples from the wrong word\n",nphg,pulses,bad);
      st_chart_now(); snprintf(b,sizeof b,"36.7 %s",TR(TX_U_G));
      st_check("… and the WEIGHT stat field while idle shows 36.7 g (conditioned)",!strcmp(hm.svv[2],b)); }
    /* real grind (physical start, no UI request): switch after 300 ms, DONE keeps word 38, back to 39 afterwards */
    { cs_w38=0; int bad=0,n_early=0; ms_t t0=sim_now;
      for(int q=0;q<50;q++){ cs_w38=q*4; tick_idle_maint(0); sim_tick=NULL; reg[33]=0x0141; reg[38]=(uint16_t)cs_w38; reg[39]=367; sim_run(100);
          if(sim_now-t0<=300&&g_last(g_w)==367) n_early++; if(sim_now-t0>=500&&g_last(g_w)!=(int)(int16_t)reg[38]) bad++; }
      int sw1=wsl.n_sw==1&&wsl.src==38;
      st_check("REAL GRIND without UI start (bit 6 for 6.6 s): the source switches ONCE to word 38 after 300 ms debounce; after that every sample is the grind weight",sw1&&bad==0&&n_early>=2);
      reg[33]=0x0101; int dbad=0; for(int q=0;q<50;q++){ sim_run(100); if(g_last(g_w)!=196) dbad++; }   /* sim_run(100) = 4 x 33 ms: 50 x 132 ms = 6.6 s */
      st_check("… the DONE screen (8 s; measured over the first 6.6 s) keeps word 38 (the grind result 19.6 g) — no switch",act.ph==PH_DONE&&wsl.src==38&&wsl.n_sw==1&&dbad==0);
      sim_run(2000);
      st_check("… DONE -> READY: back to word 39 (ONE switch); 2 switches in total for the whole grind",act.ph==PH_READY&&wsl.src==39&&wsl.n_sw==2&&g_last(g_w)==367);
      { int ok=1; tick_idle_maint(0); reg[38]=196; reg[39]=367; sim_tick=NULL; sim_reset(); reg[38]=196; reg[39]=367; reg[33]=0x0141; sim_run(1000);   /* grind -> 38 latched */
        reg[33]=0x0101; for(int q=0;q<75;q++){ sim_run(100); home_model(sim_now); if(act.ph==PH_READY&&g_lsrc==39&&fabsf(g_lastf(g_wd)-367.f)>0.5f) ok=0;
            if(act.ph==PH_READY&&strstr(hm.svv[2],"19.6")) ok=0; }
        st_check("… the source switch 38 -> 39 carries no history over: the first sample from word 39 is shown as 36.7 (no 19.6 g flash in the stat field)",ok&&wsl.src==39); } }
    /* UI-started grind: immediately on the first sample with bit 6; then a brew (UI-started) immediately back to 39 */
    sim_reset(); sim_tick=NULL; reg[38]=0xFFFF; reg[39]=367; sim_run(1000);
    { act_request(ACT_GRIND,sim_now); int req=act.pending==ACT_GRIND; sim_run(150); reg[33]=0x0141; reg[38]=0; sim_run(100);
      st_check("UI-started GRIND (act_request): the source switches IMMEDIATELY on the first sample with bit 6 — the hero shows 0.0 g (tare), not the cup's 36.7 g",req&&wsl.src==38&&wsl.n_sw==1&&g_last(g_w)==0);
      for(int q=0;q<20;q++){ reg[38]=(uint16_t)(q*9); sim_run(100); } reg[33]=0x0101; sim_run(1500);
      memset(&wq,0,sizeof wq); act.pending=ACT_NONE; reg[39]=0xFFFD; act_request(ACT_BREW,sim_now); sim_run(150); reg[33]=0x1107; sim_run(100);
      st_check("… and a UI-started BREW right after (grind DONE still showing): immediately back to word 39 — the brew hero never shows the grind weight",wsl.src==39&&wsl.n_sw==2&&g_last(g_w)==-3); }
    /* cup off / on while idle: the stat field jumps (without a 2 s tail) */
    sim_reset(); sim_tick=NULL; reg[39]=364; sim_run(3000);
    { reg[39]=0xFFFE; sim_run(300); home_model(sim_now); snprintf(b,sizeof b,"0.0 %s",TR(TX_U_G)); int off=!strcmp(hm.svv[2],b);
      reg[39]=91; sim_run(300); home_model(sim_now); snprintf(a,sizeof a,"9.1 %s",TR(TX_U_G));
      st_check("CUP OFF / ON while idle (36.4 g -> -0.2 g -> 9.1 g): the stat field shows the new level after 2 samples (median-3 + step snap > 2 g) — without a 2 s tail",off&&!strcmp(hm.svv[2],a)); }
    /* ---- (5) deadband: |weight| < 0.25 g is shown as 0 (hero, chart, stat field); raw is kept ---- */
    sim_reset(); sim_tick=NULL;
    { static const int V[]={-2,-1,0,1,2,3,-3,25}; static const char*const S[]={"0.0","0.0","0.0","0.0","0.0","0.3","-0.3","2.5"}; int ok=1;
      for(int q=0;q<8;q++){ reg[33]=0x0101; reg[39]=(uint16_t)V[q]; sim_run(4000); home_model(sim_now); snprintf(b,sizeof b,"%s %s",S[q],TR(TX_U_G));
          if(strcmp(hm.svv[2],b)){ ok=0; printf("       idle, word39=%d: stat '%s', expected '%s'\n",V[q],hm.svv[2],b); } }
      st_check("DEADBAND (idle, stat field): -0.2 / -0.1 / 0 / 0.1 / 0.2 g -> 0.0 g; 0.3 g and -0.3 g are shown; 2.5 g is shown",ok); }
    { int ok=1; static const int V[]={2,-2,1,3,24}; static const char*const S[]={"0.0","0.0","0.0","0.3","2.4"};
      for(int q=0;q<5;q++){ reg[33]=0x1307; reg[51]=1300; reg[39]=(uint16_t)V[q]; sim_run(2500); st_chart_now();
          float cw=wdb(g_lastf(g_wd));
          if(strcmp(hm.hero,S[q])||(abs(V[q])<3&&cw!=0.f)){ ok=0; printf("       brew, word39=%d: hero '%s' (expected '%s'), chart weight %.2f\n",V[q],hm.hero,S[q],(double)cw); } }
      st_check("DEADBAND (brew: hero + chart weight): 0.2 / -0.2 / 0.1 g -> 0.0 and the chart line at 0; 0.3 g and 2.4 g are shown",ok); }
    { reg[39]=2; sim_run(1000); int raw=g_last(g_w)==2&&s16(39)==2&&reg[39]==2;
      cd.raw=1; st_chart_now(); int rv=!strcmp(hm.hero,"0.2"); cd.raw=0; st_chart_now();
      st_check("… display only: the ring buffer, reg[] and the state JSON's regs are raw (0.2 g); DIAGNOSE → GRAPH = RAW shows 0.2",raw&&rv&&!strcmp(hm.hero,"0.0")); }
    /* ---- (6) signed tare: 0xFFFD (-0.3 g) mixed with 0 / 2 / 0xFFFF — no smearing towards 6553.3 g ---- */
    sim_reset(); sim_tick=NULL; reg[33]=0x1107; reg[51]=358;
    { static const uint16_t T[]={0xFFFD,0x0000,0xFFFD,0x0002,0xFFFF,0xFFFD,0xFFFD,0x0000}; float lo=1e9f,hi=-1e9f; int ok=1;
      for(int q=0;q<80;q++){ reg[39]=T[q%8]; sim_run(100); float v=g_lastf(g_wd); if(v<lo) lo=v; if(v>hi) hi=v; if(g_last(g_w)<-3||g_last(g_w)>2) ok=0; }
      st_chart_now(); int cw=(int)lroundf(wdb(g_lastf(g_wd)));
      Cond u; cond_snap(&u,0); float um=0; for(int q=0;q<80;q++){ float v=cond_step(&u,(int)T[q%8],cd.a_chart); if(v>um) um=v; }   /* same filter on UNSIGNED words: that is what the sim bug looked like */
      st_check("SIGNED TARE: word 39 = 0xFFFD/0/2/0xFFFF is filtered as -3..2 (shown -0.3..0.2 g) — never towards 65533; hero 0.0, chart weight 0",ok&&lo>=-3.01f&&hi<=2.01f&&!strcmp(hm.hero,"0.0")&&cw==0);
      printf("       signed: displayed %.2f..%.2f · same filter on unsigned words: up to %.0f (= %.1f g)\n",(double)lo,(double)hi,(double)um,(double)um/10);
      st_check("… control: the same filter on UNSIGNED words smears the tare out to > 1000 g — the test has teeth",um>10000); }
    sim_reset(); sim_tick=NULL;
}
/* ============================ v6.10: pressure estimate ============================ */
static int str_any(const char*needle){ for(int lg=0;lg<NLANG;lg++) for(int i=0;i<NSTR;i++) if(STR[lg][i]&&strstr(STR[lg][i],needle)) return 1; return 0; }
static void st_press(const char*cfgsrc,const char*dir){
    printf("== selftest: v6.10 pressure estimate (anchor: pre-infusion %d raw in the capture = config preInfusionPressure; k = anchor / P0; > %.0f bar = pulse) ==\n",P_ANCHOR_RAW,(double)P_EST_MAX);
    char b[96],e[96];
    /* ---- (1) the scale math ---- */
    sim_reset();
    { float k=p_k(); int ok=P_ANCHOR_RAW==358&&SET[S_PIPRESS].loaded&&sv(S_PIPRESS)==4&&p_anchor_bar()==4&&fabsf(k-4.f/358.f)<1e-7f;
      ok&=fabsf(p_bar(358)-4.f)<0.001f&&fabsf(p_bar(1346)-15.039f)<0.01f&&fabsf(p_bar(1379)-15.408f)<0.01f&&p_bar(0)==0.f;
      snprintf(b,sizeof b,"%.1f / %.1f / %.1f",(double)p_bar(358),(double)p_bar(1346),(double)p_bar(1379));
      st_check("SCALE: P0 = 358 raw, anchor = config preInfusionPressure 4 -> k = 4/358 = 0.011173 bar/raw; 358 -> 4.0, 1346 -> 15.0, 1379 -> 15.4, 0 -> 0",ok&&!strcmp(b,"4.0 / 15.0 / 15.4"));
      printf("       k = %.6f bar/raw · pre 358 -> %.2f · ext 1346 -> %.2f · ext-top 1379 -> %.2f\n",(double)k,(double)p_bar(358),(double)p_bar(1346),(double)p_bar(1379)); }
    /* ---- (2) HOME: the BAR field during pre-infusion and extraction ---- */
    sim_reset(); cs_ws=0x1107; cs_p=358; cs_w39=0xFFFD; cs_w38=0; cs_pulse=0; cs_glitch=0; sim_tick=tick_cs; sim_run(4000); st_chart_now();
    { float v=(float)atof(hm.svv[1]);
      st_check("HOME, PRE-INF. (plateau 358 +- 20 noise): the BAR field shows 4.0 (+- 0.1) under the label '≈BAR'",act.ph==PH_PRE&&hm.sl[1]==TR(TX_ST_BAR)&&fabsf(v-4.f)<=0.1f&&strstr(TR(TX_ST_BAR),"\xE2\x89\x88"));
      printf("       pre: '%s' = '%s'\n",hm.sl[1],hm.svv[1]); }
    cs_ws=0x1307; cs_p=1346; sim_run(6000); st_chart_now();
    { float v=(float)atof(hm.svv[1]);
      st_check("HOME, EXTRACTION (plateau 1346): the BAR field shows 15.0 (+- 0.2) — linear anchor, pump side (UI_v6.md §16)",act.ph==PH_EXT&&hm.sl[1]==TR(TX_ST_BAR)&&fabsf(v-15.f)<=0.2f);
      printf("       ext: '%s' = '%s'\n",hm.sl[1],hm.svv[1]); }
    st_check("v6.17: the BAR field's label is '≈BAR' (EN + DA; the ≈ says 'estimate' — v6.11-v6.16 added 'EST.' on top of it), the chart caption is short ('PRESSURE' / 'TRYK'); 'UKALIBRERET' / 'UNCAL' no longer appear in any string",
             !strcmp(STR[LANG_EN][TX_ST_BAR],"≈BAR")&&!strcmp(STR[LANG_DA][TX_ST_BAR],"≈BAR")&&!strcmp(STR[LANG_EN][TX_C_PRESS],"PRESSURE")&&!strcmp(STR[LANG_DA][TX_C_PRESS],"TRYK")&&!str_any("UKALIB")&&!str_any("UNCAL"));
    /* ---- (3) the basis: toast on tapping the chart / the BAR field (no frames, no phase change) ---- */
    { snprintf(e,sizeof e,TR(TX_T_PINFO),4,358); int n0=wq.n; long ok0=wq.n_ok;
      toast.dur=0; tap(240,268); int t1=toast.dur>0&&!strcmp(toast.msg,e); sim_run(3000);
      toast.dur=0; tap(CX,318); int t2=toast.dur>0&&!strcmp(toast.msg,e);
      st_check("BASIS: tapping the chart and the BAR field during a brew -> toast 'PRE-INF. 4 BAR = RAW 358 · LINEAR' (no frames sent, brew unchanged)",t1&&t2&&wq.n==n0&&wq.n_ok==ok0&&act.ph==PH_EXT);
      printf("       toast: '%s'\n",toast.msg);
      sim_reset(); sim_run(1500); toast.dur=0; tap(240,268); int t3=toast.dur==0;
      st_check("… while idle with no brew in the window ('NO ACTIVITY') the chart gives no toast",t3&&!cst.active); }
    /* ---- (4) the config anchor: preInfusionPressure 3 in a config copy changes the scale ---- */
    { char op[400],kb[400]; snprintf(op,sizeof op,"%s/press_anchor.json",dir); snprintf(kb,sizeof kb,"%s",cfg_bakdir);
      int ok=copy_file(cfgsrc,op); snprintf(cfg_path,sizeof cfg_path,"%s",op); snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s/pressbak",dir);
      ok&=cfg_set_int("preInfusionPressure",3);
      sim_reset(); cs_ws=0x1307; cs_p=1346; cs_pulse=0; sim_tick=tick_cs; sim_run(6000); st_chart_now();
      char anc[64],nw[64]; uint32_t c; diag_press_text(anc,sizeof anc,nw,sizeof nw,&c);
      snprintf(b,sizeof b,TR(TX_DV_PANCH),3,358,(double)(3.f/358.f)); snprintf(e,sizeof e,TR(TX_T_PINFO),3,358); toast.dur=0; tap(240,268);
      float v=(float)atof(hm.svv[1]);
      st_check("CONFIG ANCHOR: preInfusionPressure = 3 (config copy, settings_load) -> k = 3/358; ext 1346 -> 11.3; the DIAGNOSE anchor and the toast say 3 BAR",
               ok&&p_anchor_bar()==3&&fabsf(p_k()-3.f/358.f)<1e-7f&&fabsf(v-11.3f)<=0.2f&&!strcmp(anc,b)&&!strcmp(toast.msg,e));
      printf("       anchor 3: BAR '%s' · '%s'\n",hm.svv[1],anc);
      snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s",kb);
      sim_reset(); cs_ws=0x1307; cs_p=1346; sim_tick=tick_cs; sim_run(6000); st_chart_now(); v=(float)atof(hm.svv[1]);
      int kv=SET[S_PIPRESS].val; SET[S_PIPRESS].val=0; int d0=p_anchor_bar(); SET[S_PIPRESS].val=99; int d99=p_anchor_bar(); SET[S_PIPRESS].val=kv;
      st_check("… back on the real config (4) -> 15.0; an anchor value outside 1..15 (0 / 99) gives the default 4 bar",p_anchor_bar()==4&&fabsf(v-15.f)<=0.2f&&d0==4&&d99==4); }
    /* ---- (5) deadhead / maintenance: never a false bar figure ---- */
    { float lim=P_EST_MAX/p_k(); int ok=p_bar(16670)<0.f&&p_bar(15716)<0.f&&p_bar(floorf(lim))>=0.f&&p_bar(ceilf(lim)+1)<0.f;
      st_check("LIMIT: linear > 20 bar (raw > 1790) is a pulse -> no figure; 16670 (186 bar linear) and 15716 (176) are pulses; 1790 = 20.0 is shown",ok); }
    sim_reset(); cs_ws=0x1307; cs_p=1346; cs_pulse=0; sim_tick=tick_cs; sim_run(5000); cs_p=16670; sim_run(1500); st_chart_now();
    { int no186=1; for(int i=0;i<3;i++) if(strstr(hm.svv[i],"186")||strstr(hm.svv[i],"17")==hm.svv[i]) no186=0;
      st_check("HOME, sustained deadhead (16670 for 1.5 s during EXTRACTION): the BAR field shows 'PULSE' — not '186.2'",act.ph==PH_EXT&&!strcmp(hm.svv[1],TR(TX_ST_PULSE))&&no186);
      printf("       deadhead: '%s' = '%s' (raw %u)\n",hm.sl[1],hm.svv[1],reg[51]);
      char anc[64],nw[64]; uint32_t c; diag_press_text(anc,sizeof anc,nw,sizeof nw,&c); snprintf(e,sizeof e,TR(TX_DV_PPULSE),(int)reg[51]);
      st_check("… the DIAGNOSE now line during a brew: 'RAW 16670 · PULSE, NO PRESSURE' (amber), no bar figure",!strcmp(nw,e)&&c==C_WARN&&!strstr(nw,"BAR")); }
    sim_reset(); cs_w38=0; cs_w39=367; cs_glitch=0; sim_tick=tick_idle_maint; sim_run(3000); sim_tick=NULL; reg[33]=0x0101; reg[51]=16670; sim_run(100); st_chart_now();
    { char anc[64],nw[64]; uint32_t c; diag_press_text(anc,sizeof anc,nw,sizeof nw,&c); snprintf(e,sizeof e,TR(TX_DV_PMAINT),16670);
      st_check("IDLE + maintenance pulse 16670: DIAGNOSE shows 'RAW 16670 · MAINTENANCE PULSE' (amber) — not 186 bar; HOME shows no BAR (the chart: ticks on the baseline)",
               act.ph==PH_READY&&!strcmp(nw,e)&&c==C_WARN&&!strstr(nw,"186")&&hm.sl[1]!=TR(TX_ST_BAR));
      printf("       idle: '%s'\n",nw);
      reg[51]=12; diag_press_text(anc,sizeof anc,nw,sizeof nw,&c); snprintf(e,sizeof e,TR(TX_DV_PNOW),12,(double)(12*4.f/358.f));
      st_check("… and idle without a pulse (raw 12): 'RAW 12 = ≈0.1 BAR'",!strcmp(nw,e)&&c==C_TXT); }
    /* ---- (6) the RAW view (DIAGNOSE → GRAPH) is untouched: raw figures, raw ring buffer ---- */
    sim_reset(); cs_ws=0x1307; cs_p=1346; cs_pulse=0; sim_tick=tick_cs; sim_run(6000); st_chart_now();
    { char glat[16]; snprintf(glat,sizeof glat,"%s",hm.svv[1]); int rb=g_last(g_p)==(int)reg[51];
      cd.raw=1; hf.ok=0; st_chart_now(); snprintf(b,sizeof b,"%d",(int)reg[51]); int r1=!strcmp(hm.svv[1],b)&&hm.sl[1]==TR(TX_ST_PRAW)&&cst.mode==0&&cst.raw_max>=1326;
      printf("       SMOOTH '%s' · RAW '%s' = '%s'\n",glat,hm.sl[1],hm.svv[1]);
      cs_p=16670; sim_run(400); st_chart_now(); snprintf(b,sizeof b,"%d",(int)reg[51]); int r2=!strcmp(hm.svv[1],b);
      cd.raw=0; hf.ok=0; cs_p=1346; sim_run(6000); st_chart_now();
      printf("       raw pulse '%s' · back in SMOOTH '%s' = '%s' (rb %d r1 %d r2 %d)\n",b,hm.sl[1],hm.svv[1],rb,r1,r2);
      st_check("RAW VIEW: the BAR field shows word 51 as a raw figure (label 'PRESSURE RAW'), also a pulse (16670 — no clamping in raw); the ring buffer is raw; back to SMOOTH -> ≈bar again",
               rb&&r1&&r2&&g_last(g_p)==(int)reg[51]&&hm.sl[1]==TR(TX_ST_BAR)&&fabsf((float)atof(hm.svv[1])-15.f)<=0.2f); }
    sim_reset(); sim_tick=NULL;
}
/* ============================ v6.11: UX review (touch targets, contrast, swipe guard, close chevron, gear, hints, plain language, DIAGNOSE groups) ============================ */
static float st_lum(uint32_t c){ float v[3]={(float)((c>>16)&255),(float)((c>>8)&255),(float)(c&255)}; for(int i=0;i<3;i++){ v[i]/=255.f; v[i]=v[i]<=0.03928f?v[i]/12.92f:powf((v[i]+0.055f)/1.055f,2.4f); } return 0.2126f*v[0]+0.7152f*v[1]+0.0722f*v[2]; }
static float st_cr(uint32_t a,uint32_t b){ float x=st_lum(a),y=st_lum(b); if(x<y){ float t=x; x=y; y=t; } return (x+0.05f)/(y+0.05f); }
/* the touch area for target t, MEASURED through hit_test() (not the drawing constants): bounding box and pixel count */
static int st_hitbox(int t,int*w,int*h){ int x0=W,y0=H,x1=-1,y1=-1,n=0;
    for(int y=0;y<H;y++) for(int x=0;x<W;x++) if(hit_test(x,y,sim_now)==t){ n++; if(x<x0)x0=x; if(x>x1)x1=x; if(y<y0)y0=y; if(y>y1)y1=y; }
    *w=n?x1-x0+1:0; *h=n?y1-y0+1:0; return n; }
static void st_gest(const int*p,int np){ ui_touch(TE_DOWN,p[0],p[1],sim_now); for(int i=1;i<np;i++){ sim_now+=16; ui_touch(TE_MOVE,p[2*i],p[2*i+1],sim_now); } ui_touch(TE_UP,p[2*np-2],p[2*np-1],sim_now); }
static void st_settle(void){ sim_run(400); }
/* ---- v6.15: the graph resets when an activity starts (display only) ---- */
static ms_t g_oldest(void){ return g_n?g_t[(g_head+GN-g_n)%GN]:-1; }
static int g_maxw(void){ int mx=-100000; for(int i=0;i<g_n;i++){ int v=g_w[(g_head+GN-g_n+i)%GN]; if(v>mx) mx=v; } return mx; }
static void tick_cupgrind(ms_t t){                     /* cup (250 g) on the brew scale, portafilter tared on the grind scale; a PHYSICAL grind start at 10 s */
    reg[39]=2500;
    if(t<10000){ reg[33]=0x0101; reg[38]=0; reg[58]=5; }
    else if(t<19000){ reg[33]=0x0141; reg[38]=(uint16_t)((t-10000)*185/9000); reg[58]=640+(t/113)%40; }
    else { reg[33]=0x0101; reg[38]=187; reg[58]=5; }
}
static void st_graph(void){
    printf("== selftest: v6.15 graph reset on activity start (idle -> active clears the ring + chart filters; a concurrent activity does not) ==\n");
    /* (1) 250 g cup at rest, then a brew started at the machine (no UI frames at all) */
    sim_reset(); cb_late=0; cb_flush0=30000; cb_flush1=36000; sim_tick=tick_cupbrew; sim_t0=sim_now; sim_run(19900); st_chart_now();
    int n_idle=g_n, cup=0; for(int i=0;i<g_n;i++) if(g_w[(g_head+GN-g_n+i)%GN]==2500) cup++;
    st_check("at rest with a 250 g cup: the ring holds the idle history incl. the cup (20 s, >= 140 samples of word 39 = 2500), no reset yet",n_idle>=140&&cup==n_idle&&gr.resets==0);
    ms_t ts=sim_t0+20000; int wq0=wq.n, txq0=txq_len;
    sim_run(1100); st_chart_now();
    st_check("1 s after the brew starts: ONE reset; the ring holds only the ~8 samples since the start, none with the cup's 250 g",
             gr.resets==1&&gr.reanchors==0&&g_n>=6&&g_n<=11&&g_oldest()>=ts&&g_maxw()<2000);
    printf("       1 s in: ring %d samples · weight top %d · pressure top %d\n",g_n,cst.wtop,cst.top);
    st_check("… the y-autoscale is recomputed from the fresh ring: weight axis top = the 10 g floor x 1.08 (108), not 250 g x 1.08 (2700); pressure top from the plateau (< 600)",
             cst.active&&cst.mode==0&&cst.wtop>=100&&cst.wtop<=110&&cst.top>=400&&cst.top<600);
    sim_run(12000); st_chart_now();                                               /* t = 33.0 s: wand flush (0x5327) during the extraction */
    st_check("a wand flush DURING the brew (secondary, concurrent): NO reset — the brew curve is kept (oldest sample = the brew start, ring keeps growing)",
             hm.conc&&act.ph==PH_EXT&&gr.resets==1&&gr.reanchors==0&&g_oldest()>=ts&&g_oldest()<ts+150&&g_n>=90);
    sim_run(10500); st_chart_now();                                               /* t = 43.5 s: ~35 g, flush over */
    { int pct=cst.wtop?(int)(wdb(g_lastf(g_wd))*100/cst.wtop):0, tare_ok=1;
      for(int i=0;i<3;i++){ int v=g_w[(g_head+GN-g_n+i)%GN]; if(v<-5||v>5) tare_ok=0; }
      printf("       43.5 s: weight now %.0f, max %d (0.1 g) · axis top %d · curve height %d %%\n",(double)g_lastf(g_wd),cst.wmax,cst.wtop,pct);
      st_check("0 -> 35 g renders at FULL SCALE: the weight series starts at the tare (first samples -0.3..0 g) and the line's current point is at >= 85 % of the weight axis",
               tare_ok&&cst.wmax>=330&&cst.wmax<=360&&pct>=85&&gr.resets==1);
      st_check("… display only: word 39 is raw (~350), no frame was queued or sent by the reset, the brew phase is untouched",
               abs((int)reg[39]-350)<=3&&wq.n==wq0&&txq_len==txq0&&act.ph==PH_EXT);
      cd.raw=1; st_chart_now(); int rawmax=cst.wmax==g_maxw(); cd.raw=0; st_chart_now();
      st_check("… DIAGNOSE → GRAPH = RAW still shows the RAW (unconditioned) series of the same post-start ring",rawmax&&!cd.raw);
      /* the same session with v6.14's behaviour (no reset): the bug */
      gr_off=1; sim_reset(); cb_late=0; cb_flush0=cb_flush1=0; sim_tick=tick_cupbrew; sim_t0=sim_now; sim_run(43500); st_chart_now(); gr_off=0;
      int pct14=cst.wtop?(int)(wdb(g_lastf(g_wd))*100/cst.wtop):0;
      printf("       v6.14 behaviour: weight max %d · axis top %d · curve height %d %%\n",cst.wmax,cst.wtop,pct14);
      st_check("… vs v6.14 (no reset): the cup step (250 g) sets the weight axis (top >= 2700) and the 35 g point sits at < 15 % of the height — the user's flat curve",
               cst.wtop>=2700&&pct14<15); }
    /* (2) the brew ends -> DONE; the next activity (water) starts within the DONE screen: a second reset */
    sim_reset(); cb_flush0=cb_flush1=0; sim_tick=tick_cupbrew; sim_t0=sim_now; sim_run(46000);
    { int done=act.ph==PH_DONE; ms_t tw=sim_now; sim_tick=tick_water; sim_t0=sim_now; sim_run(2000); st_chart_now();
      st_check("DONE -> the next activity (water) 2 s later: a second reset; the ring starts at the water start (no brew/cup samples)",
               done&&act.ph==PH_WATER&&gr.resets==2&&g_oldest()>=tw+300&&g_maxw()<100); }
    /* (3) the tare lands late (word 39 stays 250 g for 300 ms after the bits): re-anchored once */
    sim_reset(); cb_late=300; sim_tick=tick_cupbrew; sim_t0=sim_now; sim_run(21500); st_chart_now(); cb_late=0;
    st_check("late tare (+300 ms): ONE re-anchor within 1.5 s of the start; no 250 g sample survives, weight axis top 108",
             gr.resets==1&&gr.reanchors==1&&g_maxw()<2000&&cst.wtop<=110&&g_oldest()>=sim_t0+20300);
    /* (4) a link gap during the brew is not an idle -> active transition */
    sim_reset(); sim_tick=tick_cupbrew; sim_t0=sim_now; sim_run(30000); ms_t o0=g_oldest(); sim_link=0; sim_run(2500); int off=phase_main(sim_now)==PH_OFFLINE; sim_link=1; sim_run(1500);
    st_check("OFFLINE for 2.5 s during the extraction, then back: no reset (the curve since the brew start is kept)",off&&gr.resets==1&&g_oldest()==o0&&act.ph==PH_EXT);
    /* (5) a PHYSICAL grind start with a cup on the brew scale: the source latches to word 38 after 300 ms -> re-anchored */
    sim_reset(); sim_tick=tick_cupgrind; sim_t0=sim_now; sim_run(14000); st_chart_now();
    { int no39=1; for(int i=0;i<g_n;i++) if(g_w[(g_head+GN-g_n+i)%GN]==2500) no39=0;
      printf("       grind 4 s in: ring %d · reanchors %d · top %d (target %d)\n",g_n,gr.reanchors,cst.top,sv(S_GRINDW));
      st_check("physical grind start (cup on the brew scale): reset + ONE re-anchor at the 300 ms source latch; only word 38 in the ring; grind scale = target x 1.08 (not the cup)",
               act.ph==PH_GRIND&&cst.mode==1&&gr.resets==1&&gr.reanchors==1&&no39&&cst.top<=sv(S_GRINDW)*1.2f&&wsl.src==38); }
    /* (6) steam alone is a main activity (idle -> steam = reset); a brew started DURING steam is not idle -> active (no reset) */
    sim_reset(); sim_tick=NULL; sim_run(5000); reg[33]=0x4121; reg[24]=0; sim_run(1000); int r1=gr.resets; ms_t os=g_oldest();
    reg[33]=0x5327; sim_run(2000);
    st_check("idle -> steam: reset (the milk curve starts at the steam start); brew started during steam: no second reset, the steam samples stay",
             r1==1&&gr.resets==1&&g_oldest()==os&&phase_main(sim_now)==PH_EXT);
    sim_reset();
}
static void st_v611(void){
    printf("== selftest: v6.11 UX review (touch targets >= 44/48 px measured via hit_test, contrast, swipe guard, close chevron, gear, empty-state hint, plain language, DIAGNOSE groups) ==\n");
    char b[160]; int w,h,ok;
    /* ---- (1) touch targets: measured touch area (width x height) ---- */
    sim_reset(); sim_run(400);
    ok=1; for(int i=0;i<4;i++){ st_hitbox(T_ACT0+i,&w,&h); printf("       HOME icon %d: %d x %d px\n",i,w,h); ok&=w>=48&&h>=48; }
    st_check("touch: the four activity icons each have a touch area >= 48 x 48 px (circle r 30 = 61 px; the drawn icon is 50 px)",ok);
    st_hitbox(T_GEAR,&w,&h); printf("       HOME gear: %d x %d px\n",w,h);
    st_check("touch: the gear (settings) has a touch area >= 44 x 44 px, without overlapping the icons",w>=44&&h>=44&&hit_test(ACT_X[1],ACT_Y+28,sim_now)==T_ACT1&&hit_test(CX,GEAR_Y,sim_now)==T_GEAR);
    page=PG_BREW; static_dirty=1; sim_run(300);
    ok=1; for(int k=0;k<3;k++){ st_hitbox(T_ROW0+k,&w,&h); printf("       BREW row %d: %d x %d px\n",k,w,h); ok&=w>=300&&h>=48; }
    st_check("touch: list rows have a touch area >= 48 px high (50 px row + 4 px above/below) and >= 300 px wide",ok);
    activate(T_ROW0+1,sim_now); sim_run(400);
    { int r[6]={T_MINUS,T_PLUS,T_CANCEL,T_SAVE,T_BACK,0}; const char*nm[5]={"-","+","CANCEL","SAVE","close chevron"}; ok=1;
      for(int i=0;i<5;i++){ st_hitbox(r[i],&w,&h); printf("       detail %s: %d x %d px\n",nm[i],w,h); ok&=w>=(i==4?44:48)&&h>=(i==4?44:48); }
      st_check("touch: detail −/+ (circle r 42), CANCEL/SAVE (136 x 64) >= 48 px; the close chevron >= 44 px",ok); }
    detail_close(sim_now); sim_run(400);
    page=PG_DIAG; scroll[PG_DIAG]=DG_GRAPH-LIST_VY0-60; static_dirty=1; sim_run(300); st_hitbox(T_GRAPH,&w,&h); printf("       DIAGNOSE GRAPH pill: %d x %d px\n",w,h);
    st_check("touch: the DIAGNOSE GRAPH pill has a touch area >= 44 px high (v6.10: 40)",w>=280&&h>=44);
    /* ---- (2) contrast (WCAG 2.x) ---- */
    { uint32_t bg=bgpure[CY*W+CX], bge=bgpure[(CY+150)*W+CX]; float c3[3]={st_cr(C_TXT3,bg),st_cr(C_TXT3,C_SURF),st_cr(C_TXT3,C_SURF2)}, c2[3]={st_cr(C_TXT2,bg),st_cr(C_TXT2,C_SURF),st_cr(C_TXT2,C_SURF2)};
      float d=st_cr(C_DIS,bg), o3=st_cr(0x00686F7Eu,bg);
      printf("       C_TXT3: %.2f / %.2f / %.2f (background / SURF / SURF2; v6.10's 104,111,126: %.2f on background) · C_TXT2: %.2f / %.2f / %.2f · C_DIS (disabled): %.2f · background edge %.2f\n",c3[0],c3[1],c3[2],o3,c2[0],c2[1],c2[2],d,st_cr(C_TXT3,bge));
      st_check("contrast: small captions (C_TXT3) >= 4.5:1 on background, SURF and SURF2 (list rows); C_TXT2 >= 7:1 on background and SURF, >= 6:1 on SURF2",c3[0]>=4.5f&&c3[1]>=4.5f&&c3[2]>=4.5f&&st_cr(C_TXT3,bge)>=4.5f&&c2[0]>=7.f&&c2[1]>=7.f&&c2[2]>=6.f);
      st_check("contrast: disabled (C_DIS) is visibly weaker than C_TXT3, but >= 3:1 (large glyphs: icon, stepper sign, offline hero)",d>=3.f&&d<c3[0]-1.f&&o3<4.5f); }
    /* ---- (3) swipe guard (v6.17: the only horizontal gesture is "swipe right = back") ---- */
    sim_reset(); page=PG_BREW; static_dirty=1; sim_run(300);
    { int g[]={240,200, 262,205, 310,245}; st_gest(g,3); st_settle(); int a=page==PG_BREW;                    /* diagonal: dx 70, dy 45 (~33°) */
      int g2[]={240,200, 222,203, 140,210}; st_gest(g2,3); st_settle(); int b2=page==PG_BREW;                 /* horizontal LEFT: dx -100, dy 10 -> nothing (no carousel) */
      float s0=scroll[PG_BREW];
      int g3[]={240,200, 243,220, 330,250}; st_gest(g3,3); st_settle(); int c=page==PG_BREW;                  /* starts vertical (scroll), ends horizontal right */
      scroll[PG_BREW]=0; int g5[]={140,200, 158,203, 240,210}; st_gest(g5,3); st_settle(); int d=page==PG_HOME;   /* horizontal RIGHT: dx 100, dy 10 -> back */
      printf("       diagonal 70/45: page %s · left -100/10: %s · vertical start + horizontal end: page %s (scroll %.0f -> %.0f) · right 100/10: %s\n",a?"unchanged":"CHANGED",b2?"nothing":"PAGE CHANGED",c?"unchanged":"CHANGED",(double)s0,(double)scroll[PG_BREW],d?"back to HOME":"NOT BACK");
      st_check("swipe guard: a diagonal drag (dx 70, dy 45) on a page does nothing; a horizontal swipe LEFT does nothing (v6.17: no carousel); a horizontal swipe RIGHT (dx 100, dy 10) goes back to HOME",a&&b2&&d);
      st_check("swipe guard: a gesture that starts vertical (scroll) never closes the page, even if it ends horizontal",c);
      sim_reset(); sim_run(300); int g4[]={240,200, 242,222, 120,236}; st_gest(g4,3); st_settle(); int h1=page==PG_HOME&&!tr.type;
      int g6[]={140,200, 160,202, 260,206}; st_gest(g6,3); st_settle(); int g7[]={300,200, 280,202, 180,206}; st_gest(g7,3); st_settle();
      st_check("swipe guard: HOME reacts to no swipe at all (left, right, vertical start) — everything on HOME is a visible button",h1&&page==PG_HOME&&!tr.type&&view==V_MAIN); }
    /* ---- (4) close chevron + swipe down ---- */
    sim_reset(); page=PG_BREW; static_dirty=1; sim_run(300); activate(T_ROW0+1,sim_now); sim_run(400);
    { int i=sel, v0=SET[i].val; tap(396,184); sim_run(10); int chg=edit_val!=v0, ht=hit_test(CX,57,sim_now);
      tap(CX,57); sim_run(400); int a=view==V_MAIN&&SET[i].val==v0&&!sav.active;
      printf("       chevron: sel %d v0 %d edit %d chg %d hit %d view %d val %d\n",i,v0,edit_val,chg,ht,view,SET[i].val);
      activate(T_ROW0+1,sim_now); sim_run(400); int g[]={240,250, 242,268, 246,340}; st_gest(g,3); sim_run(400); int c=view==V_MAIN;
      activate(T_ROW0+1,sim_now); sim_run(400); int e0=edit_val; int g2[]={84,184, 86,204, 90,280}; st_gest(g2,3); sim_run(400); int d=view==V_DETAIL&&edit_val==e0-SET[i].step;
      int g3[]={240,250, 262,268, 310,320}; st_gest(g3,3); sim_run(400); int e=view==V_DETAIL;                      /* diagonal (45°) -> do not close */
      int g8[]={150,250, 170,252, 260,256}; st_gest(g8,3); sim_run(400); int f=view==V_MAIN&&page==PG_BREW&&SET[i].val==v0;   /* v6.17: swipe right = back (to the page the editor was opened from) */
      st_check("close chevron: a tap at the top (240,57) closes the detail like CANCEL — a changed value is NOT saved",chg&&a);
      st_check("swipe down (>= 70 px, vertical) closes the detail; started on − it does not close (the press has already counted a step); diagonal does not close; v6.17: swipe right closes it too (nothing saved)",c&&d&&e&&f); }
    sim_reset(); sys_open(X_BOOTUI,sim_now); sim_run(400); tap(CX,57); sim_run(400);
    st_check("the close chevron also works in the system view (BOOT UI): back to the list, the flag untouched",view==V_MAIN&&xsel==-1);
    /* ---- (5) gear ---- */
    sim_reset(); sim_run(400); { int n0=wq.n; int q0=txq_len; tap(CX,GEAR_Y); st_settle(); int a=page==PG_SYS&&wq.n==n0&&txq_len==q0;
      int e1=hit_test(20,240,sim_now)==T_NONE&&hit_test(460,240,sim_now)==T_NONE; tap(20,240); st_settle(); tap(460,240); st_settle(); int c=page==PG_SYS;
      st_check("the gear on HOME opens SYSTEM (no frame); v6.17: there are no invisible edge zones any more (a tap at the left / right edge does nothing)",a&&e1&&c); }
    /* ---- (6) empty-state hint + subline ---- */
    sim_reset(); sim_run(1500); home_model(sim_now);
    { int a=!strcmp(hm.hint1,TR(TX_C_HINT))&&hm.hint2==NULL&&!cst.active; char t[40]; fmt_t10(t,sizeof t,870,1); snprintf(b,sizeof b,TR(TX_H_SET_TO),t);
      int s1=!strcmp(hm.sub,b)&&sub_font(&hm)==F_S;
      reg[35]=1; sim_run(400); home_model(sim_now); int e=act.ph==PH_ERROR&&strstr(hm.hint1,"E2")&&!strstr(hm.sub,"E2")&&!strcmp(hm.sub,TR(TX_ER_E2_BREW)); reg[35]=0;
      sim_link=0; sim_run(3000); home_model(sim_now); int o=act.ph==PH_OFFLINE&&!strcmp(hm.hint1,TR(TX_C_DIAGREF))&&!strcmp(hm.sub,TR(TX_H_NORESP)); sim_link=1; sim_run(400);
      reg[1]=1; sim_run(400); home_model(sim_now); int z=act.ph==PH_SLEEP&&!hm.hint1[0]&&!strcmp(hm.sub,TR(TX_H_WAKING)); reg[1]=0; sim_run(400);
      printf("       ready: '%s' · subline '%s' (F_S %d) · error '%s' / '%s' · offline '%s' / '%s'\n",TR(TX_C_HINT),b,s1,TR(TX_ER_E2_BREW),TR(TX_C_ERRCODE),TR(TX_H_NORESP),TR(TX_C_DIAGREF));
      st_check("HOME idle: the chart field says what you can DO in ONE line ('HOLD = START · TAP = SETTINGS'; v6.11-v6.16: two lines) instead of 'NO ACTIVITY'; the subline is 'TARGET 87.0°' in F_S",a&&s1);
      st_check("error: the subline is plain language (no E code); the code is shown dimmed in the chart field with a reference to DIAGNOSE · offline: 'NO CONNECTION TO THE MACHINE' + DIAGNOSE reference · asleep: 'WAKING UP…'",e&&o&&z); }
    { sim_reset(); reg[33]=0x1307; sim_run(1500); home_model(sim_now); int a=cst.active;
      st_check("the hint is shown only in the empty chart (during a brew the curve is drawn as before)",a&&act.ph==PH_EXT); }
    /* ---- (7) plain language: no word numbers / raw codes in user-facing strings (only DIAGNOSE and console) ---- */
    ok=1; for(int i=0;i<NSTR;i++){ int k=STR_K[i]; if(k==KDL||k==KDV||k==KDF||k==KCON||!strncmp(STR_ID[i],"TX_DL_",6)||!strncmp(STR_ID[i],"TX_DV_",6)||!strncmp(STR_ID[i],"TX_DG_",6)) continue;
        for(int lg=0;lg<NLANG;lg++){ const char*s=STR[lg][i]; if(!s) continue;
            for(const char*p=s;(p=strstr(p,lg==LANG_DA?"ORD ":"WORD "));p++){ const char*q=p+(lg==LANG_DA?4:5); if(isdigit((unsigned char)*q)&&(p==s||!isalpha((unsigned char)p[-1]))){ ok=0; printf("     [%s] %s: '%s'\n",LANG_CODE[lg],STR_ID[i],s); break; } }
            if(strstr(s," ACK")||strstr(s,"FRAME-POLI")||strstr(s,"FRAME POLI")||strstr(s,"WRITE-K")||strstr(s,"WRITE QUEUE")){ ok=0; printf("     [%s] %s: '%s'\n",LANG_CODE[lg],STR_ID[i],s); } } }
    st_check("plain language: no user-facing string (HOME, toasts, notes, footers) mentions word numbers, ACK, frame policy or write queue — they appear only on DIAGNOSE",ok);
    /* ---- (8) DIAGNOSE groups ---- */
    { int perm[DG_N]={0}, okp=1; for(int i=0;i<DG_N;i++){ if(DG_SLOT[i]<0||DG_SLOT[i]>=DG_N||perm[DG_SLOT[i]]++) okp=0; }
      static const int st[]={0,1,5,7,8,13,14,15}; int s8=1; for(int i=0;i<8;i++) s8&=DG_SLOT[st[i]]<8;
      int order=DG_Y0+3*DG_P+30<DG_BOOT-28&&DG_GRAPH<DG_PRESS&&DG_PRESS+28+DG_P+30<DG_RAW-11&&DG_RAW+28+3*DG_P+30<DG_ABOUT&&dg_cell_y(2)>DG_RAW&&dg_cell_y(12)>DG_RAW;
      printf("       STATUS y %d..%d · BOOT %d · GRAPH %d · PRESSURE %d · RAW COUNTERS %d · ABOUT %d · bottom %d\n",DG_Y0,DG_Y0+3*DG_P,DG_BOOT,DG_GRAPH,DG_PRESS,DG_RAW,DG_ABOUT,DG_BOTTOM);
      st_check("DIAGNOSE: STATUS at the top (link, phase, E1/E2/E3, awake, net, version, boot push), then boot/net/graph/pressure, RAW COUNTERS (rx, crc, work_sta, cmd6, current, frame, mirror, md5) in their own section, ABOUT at the bottom; every cell has exactly one slot",okp&&s8&&order); }
    sim_reset(); sim_tick=NULL;
}
/* ============================ v6.17: IA consolidation (docs/ux-audit-v617.md) ============================ */
static int st_q_snap(char*o,size_t n){   /* the write queue as hex, frame by frame (what WILL go out) */
    size_t l=0; o[0]=0;
    for(int k=0;k<wq.n;k++){ uint8_t b[80]; int m=build_write(b,wq.q[k].sub,wq.q[k].w,wq.q[k].len); for(int i=0;i<m&&l+4<n;i++) l+=(size_t)snprintf(o+l,n-l,"%02x",b[i]); if(l+2<n){ o[l++]='|'; o[l]=0; } }
    return wq.n;
}
static int st_hold(int x,int y,ms_t ms){   /* a real hold: DOWN, frames for ms (the write queue is NOT stepped, so what was queued stays), UP */
    ms_t t=sim_now; ui_touch(TE_DOWN,x,y,t); for(ms_t e=t+ms;t<e;t+=11){ if(sim_link) last_rx_ms=t; ui_frame(t); } int f=tch.fired; ui_touch(TE_UP,x,y,t); sim_now=t; return f;
}
/* ============================ v6.18: sound (start jingle + per-boiler ready tones; docs/audio.md) ============================ */
static int st_own_save(int i,int val,ms_t t);
static int st_sn(int ev){ return snd.n[ev]; }
static int st_sp(int ev){ return snd.n_played[ev]; }
static int st_slast(int ev,int out){ return snd.log_n&&snd.log[0].ev==ev&&snd.log[0].out==out; }
static void st_v618(const char*dir){
    printf("== selftest: v6.18 sound (stock: deal_play 0x13725C, page_logo power.mp4, preheatCompele once per power-on when words 62 AND 63 > 99; MCU boiler_heat 0x3502 heat stages) ==\n");
    char keep_sp[256]; snprintf(keep_sp,sizeof keep_sp,"%s",snd_path); snprintf(snd_path,sizeof snd_path,"%s/sound",dir); unlink(snd_path);
    int tx0, sq0;
    /* ---- (1) settings: own file, DISPLAY · SOUND section, volume = stock config ---- */
    settings_load();
    st_check("SOUND (OFF / ON / STOCK SOUNDS) and READY TONE (OFF / BREW BOILER / STEAM BOILER / BOTH BOILERS) are merakiplus's own (SF_SND): no config key, no MCU word; defaults ON + BOTH",
             !SET[S_SOUND].key&&!SET[S_SNDRDY].key&&SET[S_SOUND].mcu==M_NONE&&SET[S_SNDRDY].mcu==M_NONE&&(SET[S_SOUND].flags&SF_SND)&&(SET[S_SNDRDY].flags&SF_SND)
             &&SET[S_SOUND].val==1&&SET[S_SNDRDY].val==3&&sndc.src==SCS_DEFAULT&&SET[S_SNDRDY].max==3&&SET[S_SOUND].max==2);
    st_check("DISPLAY · SOUND holds UI LANGUAGE, BRIGHTNESS, TEMP. UNIT, SOUND, READY TONE, VOLUME (VOLUME moved from STOCK UI: it sets Meraki+'s sounds too; still config \"volume\")",
             sec_of_set(S_SOUND)==SEC_DISPLAY&&sec_of_set(S_SNDRDY)==SEC_DISPLAY&&sec_of_set(S_VOL)==SEC_DISPLAY&&!strcmp(SET[S_VOL].key,"volume")&&SEC_ROWS[SEC_DISPLAY][3]==S_SOUND&&SEC_ROWS[SEC_DISPLAY][5]==S_VOL);
    { sim_reset(); int a=st_own_save(S_SOUND,0,sim_now); char rb[96]; int fd=open(snd_path,O_RDONLY); int rn=fd>=0?(int)read(fd,rb,sizeof rb-1):-1; if(fd>=0) close(fd); if(rn<0) rn=0; rb[rn]=0;
      int b=st_own_save(S_SNDRDY,1,sim_now); fd=open(snd_path,O_RDONLY); char rb2[96]; int rn2=fd>=0?(int)read(fd,rb2,sizeof rb2-1):-1; if(fd>=0) close(fd); if(rn2<0) rn2=0; rb2[rn2]=0;
      char tp[300]; snprintf(tp,sizeof tp,"%s.tmp",snd_path);
      st_check("SAVE writes the sound file atomically: \"sound=off\\nready=both\\n\", then \"sound=off\\nready=brew\\n\"; toast SOUND SAVED; no tmp file left",
               a&&b&&!strcmp(rb,"sound=off\nready=both\n")&&!strcmp(rb2,"sound=off\nready=brew\n")&&access(tp,F_OK)!=0&&!strcmp(toast.msg,TR(TX_T_SND_SAVED)));
      settings_load(); int c=SET[S_SOUND].val==0&&SET[S_SNDRDY].val==1&&sndc.src==SCS_FILE;
      put_bytes(snd_path,"sound=loud\nready=7\nvolume=99\n",28); settings_load(); int d=SET[S_SOUND].val==1&&SET[S_SNDRDY].val==3;
      put_bytes(snd_path,"sound=stock\nready=steam\n",24); settings_load(); int e=SET[S_SOUND].val==2&&SET[S_SNDRDY].val==2;
      unlink(snd_path); settings_load(); int f=SET[S_SOUND].val==1&&SET[S_SNDRDY].val==3&&sndc.src==SCS_DEFAULT;
      st_check("read back after a restart; unknown values -> the default; \"stock\"/\"steam\" parse; no file -> ON + BOTH",c&&d&&e&&f); }
    /* ---- (2) a cold boot: jingle once, then one tone per boiler as each reaches its heat stage 100 ---- */
    sim_reset(); snd_uptime_ovr=25; reg[62]=30; reg[63]=0; reg[40]=520; reg[47]=310; tx0=txq_len; sq0=(int)wq.seq;
    sim_run(1500);
    st_check("cold boot of the display (uptime 25 s < 120 s): the START JINGLE plays once on the first \"on\"; cold boilers (stage 30 / 0) -> no ready tone",
             st_sn(SE_JINGLE)==1&&st_sp(SE_JINGLE)==1&&st_sn(SE_READY_E)==0&&st_sn(SE_READY_S)==0&&snd.armed[0]&&snd.armed[1]&&!strcmp(snd.last_file,SND_OURS_(SE_JINGLE)));
    reg[62]=60; reg[40]=850; sim_run(600); int e60=st_sn(SE_READY_E)==0;
    reg[62]=100; reg[40]=871; sim_run(300);
    st_check("EXTRACT boiler: stage 60 (60 °C .. set temp) -> nothing; stage 100 (MCU: e boiler >= word 11) -> its OWN tone once, the steam boiler still heating",
             e60&&st_sp(SE_READY_E)==1&&st_sn(SE_READY_S)==0&&st_slast(SE_READY_E,SO_PLAYED)&&!strcmp(snd.last_file,SND_OURS_(SE_READY_E)));
    reg[63]=60; sim_run(600); int s60=st_sn(SE_READY_S)==0; reg[63]=100; reg[47]=1255; sim_run(300);
    st_check("STEAM boiler: stage 100 (MCU: s boiler >= steam target - 10 °C) -> a DIFFERENT tone (ready_steam.wav vs ready_extract.wav), once",
             s60&&st_sp(SE_READY_S)==1&&st_slast(SE_READY_S,SO_PLAYED)&&!strcmp(snd.last_file,SND_OURS_(SE_READY_S))&&strcmp(SND_OURS_(SE_READY_S),SND_OURS_(SE_READY_E))&&strcmp(SND_OURS_(SE_JINGLE),SND_OURS_(SE_READY_S)));
    /* ---- (3) no spam: brew/steam dips, link glitches ---- */
    for(int k=0;k<6;k++){ reg[62]=60; reg[40]=835; reg[63]=60; reg[47]=1215; sim_run(400); reg[62]=100; reg[40]=872; reg[63]=100; reg[47]=1262; sim_run(400); }
    st_check("no spam: six dips to stage 60 (3.5 °C below the brew set temp, 4 °C below the steam threshold — a shot, a steam session) re-trigger nothing",
             st_sn(SE_READY_E)==1&&st_sn(SE_READY_S)==1&&!snd.armed[0]&&!snd.armed[1]);
    sim_link=0; last_rx_ms=sim_now-5000; sim_run(3000); int off_held=snd.st==2; sim_link=1; sim_run(800);
    st_check("a link glitch (OFFLINE 3 s) is not a power cycle: no second jingle, nothing re-armed",off_held&&st_sn(SE_JINGLE)==1&&st_sn(SE_READY_E)==1&&!snd.armed[0]);
    /* ---- (4) re-arm only after real cooling ---- */
    reg[62]=60; reg[40]=(uint16_t)(reg[11]-SND_REARM_DROP+5); sim_run(600); int still=!snd.armed[0];
    reg[40]=(uint16_t)(reg[11]-SND_REARM_DROP-5); sim_run(400); int re=snd.armed[0]&&!snd.armed[1];
    reg[62]=100; reg[40]=reg[11]; sim_run(300);
    st_check("re-arm: 14.5 °C below the set temp -> still announced; 15.5 °C below -> re-armed; back at stage 100 -> the extract tone again (2), the steam tone not",
             still&&re&&st_sp(SE_READY_E)==2&&st_sp(SE_READY_S)==1);
    reg[63]=30; sim_run(300); int sre=snd.armed[1]; reg[63]=100; sim_run(300);
    st_check("re-arm by the MCU stage: steam stage <= 30 (< 80 °C) -> re-armed; stage 100 -> the steam tone again",sre&&st_sp(SE_READY_S)==2);
    st_check("the jingle played exactly ONCE across all of the above, and no audio event queued a UART frame (txq + write queue unchanged)",
             st_sn(SE_JINGLE)==1&&txq_len==tx0&&(int)wq.seq==sq0);
    /* ---- (5) off / sleep: silent; power-on: jingle + both tones (stock: the logo latch resets at every power-on) ---- */
    { int n0=snd.n[SE_JINGLE]+snd.n[SE_READY_E]+snd.n[SE_READY_S];
      reg[1]=1; reg[33]=0x0001; sim_run(800); int sl=snd.st==1&&snd.armed[0]&&snd.armed[1];
      reg[62]=30; reg[63]=30; sim_run(500); reg[62]=100; reg[63]=100; sim_run(500);
      int quiet=snd.n[SE_JINGLE]+snd.n[SE_READY_E]+snd.n[SE_READY_S]==n0&&snd.qn==0;
      reg[1]=0; reg[33]=0x0101; sim_run(SND_SETTLE_MS+800);
      st_check("SLEEP: nothing plays (heat stages change while asleep: no event); WAKE = power-on: the jingle again — and (v6.19) NO ready tone, both boilers are already hot",
               sl&&quiet&&st_sn(SE_JINGLE)==2&&st_sn(SE_READY_E)==2&&st_sn(SE_READY_S)==2&&snd.log[0].ev==SE_JINGLE&&!snd.armed[0]&&!snd.armed[1]); }
    { pw.state=PW_OFF; view=V_OFF; sim_link=0; reg[0]=0; last_rx_ms=sim_now-5000; sim_run(600);   /* OFF: the MCU sleeps and does not reply */
      int n0=snd.n[SE_JINGLE], ne0=snd.n[SE_READY_E]; reg[62]=30; sim_run(300); reg[62]=100; sim_run(300);
      int quiet=snd.n[SE_JINGLE]==n0&&snd.n[SE_READY_E]==ne0&&snd.st==1;
      int n1=st_sn(SE_READY_E)+st_sn(SE_READY_S);
      sim_link=1; reg[0]=1; reg[62]=60; sim_run(2500);                         /* the controller answers with word 0 = 1 -> merakiplus powers on (pw_step); extract still heating */
      int j3=st_sn(SE_JINGLE)==3&&st_sn(SE_READY_E)+st_sn(SE_READY_S)==n1&&snd.armed[0]&&!snd.armed[1];
      reg[62]=100; sim_run(300);
      st_check("the UI's OFF (power button): silent while OFF; ON again -> the jingle (3rd power-on) only; the extract boiler (stage 60 at the power-on) heats up -> its tone, the hot steam boiler stays quiet",
               quiet&&j3&&st_sp(SE_READY_E)==3&&st_sn(SE_READY_S)==2); }
    /* ---- (6) a Meraki+ start on a machine that is already on (switch from the stock UI): no jingle, hot boilers count as announced ---- */
    sim_reset(); sim_run(1500);
    st_check("Meraki+ started on a running machine (uptime 1 day): no jingle, both boilers already hot -> no ready tone",st_sn(SE_JINGLE)==0&&st_sn(SE_READY_E)==0&&st_sn(SE_READY_S)==0&&!snd.armed[0]&&!snd.armed[1]);
    sim_reset(); reg[63]=60; reg[47]=1100; sim_run(2200); reg[63]=100; reg[47]=1300; sim_run(300);
    st_check("... a boiler still heating then is armed: its tone plays when it gets hot (steam), the hot one stays quiet",st_sn(SE_JINGLE)==0&&st_sp(SE_READY_S)==1&&st_sn(SE_READY_E)==0);
    sim_reset(); reg[62]=0; reg[63]=0; sim_run(500); reg[62]=100; reg[63]=100; sim_run(2500);
    st_check("... stale first samples (words 62/63 = 0 for 0.5 s, then 100) inside the 1.5 s settle window do not count as \"heated up\": no tone",
             st_sn(SE_READY_E)==0&&st_sn(SE_READY_S)==0&&!snd.settle&&!snd.armed[0]&&!snd.armed[1]);
    /* ---- (7) fallback thresholds when words 62/63 were never received ---- */
    sim_reset(); snd_uptime_ovr=10; reg_seen[62]=reg_seen[63]=0; reg[11]=900; reg[40]=880; reg[14]=1; reg[47]=1050; sim_run(SND_SETTLE_MS+800);
    int f0=st_sn(SE_READY_E)==0&&st_sn(SE_READY_S)==0;
    reg[40]=900; sim_run(300); int f1=st_sp(SE_READY_E)==1;
    reg[47]=1099; sim_run(300); int f2=st_sn(SE_READY_S)==0; reg[47]=1100; sim_run(300); int f3=st_sp(SE_READY_S)==1;
    reg[14]=4; reg[40]=900-SND_REARM_DROP-1; sim_run(300); reg[40]=900; sim_run(300);
    st_check("fallback without words 62/63: extract = word 40 >= word 11 (88.0 < 90.0 -> no, 90.0 -> tone); steam = word 47 >= gear target - 10 °C (gear 1: 110.0 °C)",
             f0&&f1&&f2&&f3&&snd_steam_thr()==1250&&st_sp(SE_READY_E)==2);
    /* ---- (8) mute, off, per boiler, volume cap, stock files ---- */
    sim_reset(); snd_uptime_ovr=10; SET[S_VOL].val=0; reg[62]=reg[63]=30; sim_run(SND_SETTLE_MS+800); reg[62]=reg[63]=100; sim_run(400);
    st_check("config volume 0 = mute (stock: 0 -> MI_AO mute): jingle + both tones are logged \"muted\", nothing is played",
             st_sn(SE_JINGLE)==1&&st_sp(SE_JINGLE)==0&&snd.n_out[SO_MUTED]==3&&st_sp(SE_READY_E)==0&&st_sp(SE_READY_S)==0);
    sim_reset(); snd_uptime_ovr=10; SET[S_SOUND].val=0; reg[62]=reg[63]=30; sim_run(SND_SETTLE_MS+800); reg[62]=reg[63]=100; sim_run(400);
    st_check("SOUND OFF: no jingle, no tone (logged \"off\")",snd.n_out[SO_OFF]==3&&snd.n_out[SO_PLAYED]==0);
    sim_reset(); snd_uptime_ovr=10; SET[S_SNDRDY].val=1; reg[62]=reg[63]=30; sim_run(SND_SETTLE_MS+800); reg[62]=reg[63]=100; sim_run(400);
    int pb=st_sp(SE_JINGLE)==1&&st_sp(SE_READY_E)==1&&st_sn(SE_READY_S)==1&&st_sp(SE_READY_S)==0;
    sim_reset(); snd_uptime_ovr=10; SET[S_SNDRDY].val=2; reg[62]=reg[63]=30; sim_run(SND_SETTLE_MS+800); reg[62]=reg[63]=100; sim_run(400);
    st_check("READY TONE per boiler: BREW BOILER -> only the extract tone; STEAM BOILER -> only the steam tone; the jingle is independent",pb&&st_sp(SE_READY_E)==0&&st_sp(SE_READY_S)==1&&st_sp(SE_JINGLE)==1);
    sim_reset(); SET[S_VOL].val=100; int c1=snd_vol()==SND_VOL_CAP; SET[S_VOL].val=40; int c2=snd_vol()==40; SET[S_VOL].loaded=0; int c3=snd_vol()==SET[S_VOL].def;
    st_check("volume = config \"volume\" capped at 70 like stock deal_play (100 -> 70, 40 -> 40; no config -> the default 40)",c1&&c2&&c3);
    sim_reset(); SET[S_SOUND].val=2; int s1=!strcmp(snd_file(SE_JINGLE),SND_OURS_(SE_JINGLE));
    { char d1[400],d2[400]; snprintf(d1,sizeof d1,"%s/customer",sysroot); mkdir(d1,0755); snprintf(d2,sizeof d2,"%s/customer/audio",sysroot); mkdir(d2,0755);
      put_bytes(rp(SND_STOCK[SE_JINGLE]),"ID3",3); int s2=!strcmp(snd_file(SE_JINGLE),SND_STOCK[SE_JINGLE]); unlink(rp(SND_STOCK[SE_JINGLE]));
      st_check("STOCK SOUNDS: the stock UI's own /customer/audio/*.mp3 when present (never shipped), else Meraki+'s tones",s1&&s2&&!strcmp(SND_STOCK[SE_READY_E],"/customer/audio/preheatCompele.mp3")); }
    /* ---- (9) activity: a tone that comes due while something runs is dropped (stock: no sound during its animations) ---- */
    sim_reset(); snd_uptime_ovr=10; reg[63]=60; sim_run(SND_SETTLE_MS+800); reg[33]=0x1307; reg[63]=100; sim_run(400); int sup=snd.n_out[SO_SUPPRESSED]==1&&st_sp(SE_READY_S)==0;
    reg[33]=0x0101; sim_run(800);
    st_check("brew running when the steam boiler gets hot: the tone is dropped (logged \"suppressed\"), and NOT played late after the shot",sup&&st_sp(SE_READY_S)==0&&!snd.armed[1]);
    /* ---- (10) preview tone on SAVE ---- */
    sim_reset(); snprintf(snd_path,sizeof snd_path,"%s/sound",dir); sim_run(800); int n0=st_sn(SE_PREVIEW); st_own_save(S_SNDRDY,3,sim_now); int pv1=st_sn(SE_PREVIEW)==n0;   /* unchanged value: no save, no tone */
    st_own_save(S_SNDRDY,2,sim_now); int pv2=st_sp(SE_PREVIEW)==n0+1;
    st_own_save(S_SOUND,0,sim_now); int pv3=st_sn(SE_PREVIEW)==n0+2&&st_sp(SE_PREVIEW)==n0+1&&st_slast(SE_PREVIEW,SO_OFF);
    st_check("SAVE of SOUND / READY TONE plays a short preview at the current volume (unless the result is OFF); an unchanged value plays nothing",pv1&&pv2&&pv3);
    /* ---- (11) names + DIAGNOSTICS ---- */
    st_check("event / outcome names (state JSON \"audio\", /tmp/merakiplus_snd.log) = start_jingle, boiler_ready_extract, boiler_ready_steam, preview / played, off, muted, suppressed",
             !strcmp(SE_KEY[SE_JINGLE],"start_jingle")&&!strcmp(SE_KEY[SE_READY_E],"boiler_ready_extract")&&!strcmp(SE_KEY[SE_READY_S],"boiler_ready_steam")&&!strcmp(SE_KEY[SE_PREVIEW],"preview")
             &&!strcmp(SO_KEY[SO_PLAYED],"played")&&!strcmp(SO_KEY[SO_MUTED],"muted")&&!strcmp(SO_KEY[SO_SUPPRESSED],"suppressed")&&!strcmp(SO_KEY[SO_OFF],"off"));
    { unlink(snd_path); sim_reset(); snd_uptime_ovr=20; reg[63]=60; sim_run(SND_SETTLE_MS+800); reg[63]=100; sim_run(400); char m1[40],l1[64],m2[40],l2[64],e1[48],e2[64];
      uint32_t c1=snd_diag_text(m1,sizeof m1,l1,sizeof l1); SET[S_VOL].val=0; uint32_t c2=snd_diag_text(m2,sizeof m2,l2,sizeof l2); SET[S_VOL].val=31;
      snprintf(e1,sizeof e1,TR(TX_DV_SND_ON),31); snprintf(e2,sizeof e2,"%s · %s",TR(TX_DV_SE_STEAM),TR(TX_DV_SO_PLAYED));
      printf("       DIAGNOSTICS SOUND: '%s' | '%s' | muted '%s' (DG_SND %d, ABOUT %d)\n",m1,l1,m2,DG_SND,DG_ABOUT);
      st_check("DIAGNOSTICS › SOUND: mode · volume (\"ON · 31\"; amber \"VOLUME 0\" when muted) and the last event + outcome (\"STEAM · PLAYED\"); the block sits between TIME and ABOUT",
               !strcmp(m1,e1)&&c1==C_TXT&&!strcmp(m2,TR(TX_DV_SND_MUTE))&&c2==C_WARN&&!strcmp(l1,e2)&&DG_SND>DG_TIME+28+2*DG_P+30&&DG_SND+28+DG_P<DG_ABOUT); }
    unlink(snd_path); snprintf(snd_path,sizeof snd_path,"%s",keep_sp); sim_reset(); settings_load();
}
/* ============ v6.19: no ready tone at a power-on with hot boilers (user: jingle + 2 tones back-to-back is redundant) ============ */
static void st_v619(void){
    printf("== selftest: v6.19 boot-already-hot (a boiler at stage 100 at power-on counts as announced; only the jingle plays) ==\n");
    int tx0, sq0;
    /* boot-already-hot: a cold boot of the display, both boilers at stage 100 */
    sim_reset(); snd_uptime_ovr=25; reg[62]=reg[63]=100; tx0=txq_len; sq0=(int)wq.seq; sim_run(SND_SETTLE_MS+1500);
    st_check("boot already hot (uptime 25 s, words 62 = 63 = 100): ONLY the start jingle plays — no ready tone (not even logged); both latches already fired",
             st_sn(SE_JINGLE)==1&&st_sp(SE_JINGLE)==1&&st_sn(SE_READY_E)==0&&st_sn(SE_READY_S)==0&&!snd.armed[0]&&!snd.armed[1]&&snd.log_n==1&&!snd.settle);
    /* the dips of normal use do not re-arm; real cooling does, and a cold -> hot transition DURING the session plays the tone */
    reg[62]=60; reg[40]=(uint16_t)(reg[11]-40); sim_run(600); reg[62]=100; reg[40]=reg[11]; sim_run(400); int dip=st_sn(SE_READY_E)==0;
    reg[62]=30; sim_run(400); int re=snd.armed[0]&&!snd.armed[1]; reg[62]=60; sim_run(400); reg[62]=100; sim_run(400);
    st_check("... a shot's dip (stage 60) re-arms nothing; real cooling (stage 30) re-arms the extract boiler; cold -> hot during the session -> its tone (once), the steam boiler stays quiet",
             dip&&re&&st_sp(SE_READY_E)==1&&st_sn(SE_READY_S)==0&&st_slast(SE_READY_E,SO_PLAYED));
    reg[63]=0; sim_run(400); reg[63]=100; sim_run(400);
    st_check("... the same for the steam boiler (stage 0 -> 100): its own tone; the jingle never repeats; no UART frame from the sound engine",
             st_sp(SE_READY_S)==1&&st_sn(SE_JINGLE)==1&&txq_len==tx0&&(int)wq.seq==sq0);
    /* cold boot with cold boilers: the tones still come as each boiler heats */
    sim_reset(); snd_uptime_ovr=25; reg[62]=30; reg[63]=0; sim_run(SND_SETTLE_MS+500); int arm=snd.armed[0]&&snd.armed[1]&&st_sn(SE_JINGLE)==1;
    reg[62]=100; sim_run(400); int e=st_sp(SE_READY_E)==1&&st_sn(SE_READY_S)==0; reg[63]=100; sim_run(400);
    st_check("cold boot with cold boilers (stage 30 / 0): jingle, both armed; each boiler's tone when it reaches stage 100 (cold -> hot)",arm&&e&&st_sp(SE_READY_S)==1);
    /* mixed: extract hot, steam still heating at the power-on */
    sim_reset(); snd_uptime_ovr=25; reg[62]=100; reg[63]=60; sim_run(SND_SETTLE_MS+500); int mx=!snd.armed[0]&&snd.armed[1];
    reg[63]=100; sim_run(400);
    st_check("power-on with the extract boiler hot and the steam boiler at stage 60: jingle + only the STEAM tone (when it gets hot)",
             mx&&st_sn(SE_JINGLE)==1&&st_sn(SE_READY_E)==0&&st_sp(SE_READY_S)==1);
    /* stale samples at the power-on: the decision is the LAST sample of the window */
    sim_reset(); snd_uptime_ovr=25; reg[62]=reg[63]=100; sim_run(400); reg[62]=60; sim_run(SND_SETTLE_MS+300); int st1=snd.armed[0]&&!snd.armed[1];
    reg[62]=100; sim_run(400);
    st_check("stale \"hot\" samples from before the power-on (100 for 0.4 s, then the real stage 60) do not latch: armed, tone when really hot",st1&&st_sp(SE_READY_E)==1&&st_sn(SE_READY_S)==0);
    /* sleep -> wake, already hot: no ready spam; heating off while asleep (stages 0) and stale 0s right after the wake do not count */
    sim_reset(); snd_uptime_ovr=25; reg[62]=reg[63]=100; sim_run(SND_SETTLE_MS+500);
    for(int k=0;k<3;k++){
        reg[1]=1; reg[33]=0x0001; reg[62]=reg[63]=0; sim_run(1500);                    /* asleep: heating off -> the MCU writes stage 0 */
        reg[1]=0; reg[33]=0x0101; sim_run(400); reg[62]=reg[63]=100; sim_run(SND_SETTLE_MS+400);   /* awake: 0.4 s of stale 0s, then hot */
    }
    st_check("3 x sleep -> wake with hot boilers (stages 0 while asleep, 0.4 s stale after the wake): 4 jingles (boot + 3 wakes), NO ready tone at all",
             st_sn(SE_JINGLE)==4&&st_sn(SE_READY_E)==0&&st_sn(SE_READY_S)==0&&!snd.armed[0]&&!snd.armed[1]);
    reg[1]=1; reg[33]=0x0001; reg[62]=reg[63]=0; sim_run(1500); reg[1]=0; reg[33]=0x0101; reg[62]=30; reg[63]=60; sim_run(SND_SETTLE_MS+500);
    int cold=snd.armed[0]&&snd.armed[1]; reg[62]=reg[63]=100; sim_run(400);
    st_check("... a long sleep that let both boilers cool (stage 30 / 60 at the wake): armed, and both tones play as they heat back up",cold&&st_sp(SE_READY_E)==1&&st_sp(SE_READY_S)==1&&st_sn(SE_JINGLE)==5);
    sim_reset();
}
static void st_v617(void){
    printf("== selftest: v6.17 IA consolidation (HOME = live root; one page per feature w. its settings + HOLD TO START; SYSTEM in sections; DIAGNOSTICS behind SYSTEM; one way back) ==\n");
    int ok,w,h; char a[96],b[96];
    /* ---- (1) the row model: every parameter has exactly one home ---- */
    { int seen[NSET]={0}, tier_ok=1, n_feat=0, n_sys=0, n_x=0;
      for(int f=0;f<4;f++) for(const signed char*q=FEAT_ROWS[f];*q!=-1;q++){ seen[(int)*q]++; n_feat++; if(SET[(int)*q].tier!=f) tier_ok=0; }
      for(int sc=0;sc<NSEC;sc++) for(const signed char*q=SEC_ROWS[sc];*q!=-1;q++){ if(*q<-1){ n_x++; continue; } seen[(int)*q]++; n_sys++; if(SET[(int)*q].tier!=4) tier_ok=0; }
      ok=1; for(int i=0;i<NSET;i++) if(seen[i]!=(i==S_HOTT?0:1)){ ok=0; printf("     SET[%d] (%s) is listed %d time(s)\n",i,TR(SET[i].label),seen[i]); }
      printf("       %d settings: GRIND %d · BREW %d · WATER %d · STEAM %d · SYSTEM %d in %d sections + %d actions (v6.16: BREW 9 · STEAM·WATER 5 · SYSTEM 15 + 2 actions, three flat lists)\n",NSET,
             page_rows(PG_GRIND),page_rows(PG_BREW),page_rows(PG_WATER),page_rows(PG_STEAM),n_sys,NSEC,n_x);
      st_check("row model: every one of the 31 parameters (v6.18: + SOUND, READY TONE) is on exactly ONE page (its SET[].tier), nothing is dropped; the only one that is not a row is the read-only WATER SENSOR (it is the WATER page's live line)",
               ok&&tier_ok&&n_feat+n_sys==NSET-1&&n_x==2&&(SET[S_HOTT].flags&SF_SENSOR)&&SET[S_HOTT].tier==2);
      st_check("feature pages carry what the feature uses: GRIND 2 rows, BREW 6, WATER 2, STEAM 2 — each at most 2 rows away from fitting without scrolling (4 rows are in view above HOLD TO START)",
               page_rows(PG_GRIND)==2&&page_rows(PG_BREW)==6&&page_rows(PG_WATER)==2&&page_rows(PG_STEAM)==2&&scroll_max(PG_GRIND)==0&&scroll_max(PG_WATER)==0&&scroll_max(PG_STEAM)==0&&scroll_max(PG_BREW)<=2*ROW_P); }
    /* ---- (2) reachability with real taps: every setting in <= 3 taps from HOME, and back with the chevron — no frame on the way ---- */
    { int maxt=0, maxd=0; ok=1;
      for(int i=0;i<NSET;i++){ if(i==S_HOTT) continue;
          st_home(); int taps=0, drags=0, pg=PG_T0+SET[i].tier;
          if(pg_feat(pg)){ tap(ACT_X[pg-PG_GRIND],ACT_Y); taps++; st_settle(); }
          else { tap(CX,GEAR_Y); taps++; st_settle(); tap(240,row_y0(PG_SYS,sec_of_set(i))+25); taps++; st_settle(); }
          int k=row_find(page,RK_SET,i);
          if(page!=pg||k<0){ ok=0; printf("     %s: page %d (want %d), row %d\n",TR(SET[i].label),page,pg,k); continue; }
          for(int q=0;q<8;q++){ int y=row_y0(page,k)-(int)scroll[page]; if(y>=LIST_VY0+10&&y+ROW_H<=list_vy1(page)-10) break; int g[]={240,300, 240,280, 240,200}; st_gest(g,3); sim_run(100); drags++; }
          tap(240,row_y0(page,k)-(int)scroll[page]+25); taps++; st_settle();
          if(!(view==V_DETAIL&&sel==i)){ ok=0; printf("     %s: the row tap did not open its editor (view %d sel %d)\n",TR(SET[i].label),view,sel); }
          tap(CX,57); st_settle(); if(!(view==V_MAIN&&page==pg)) ok=0;
          tap(CX,PG_TITLE_Y+6); st_settle(); if(page!=PG_HOME) ok=0;
          if(wq.n||txq_len){ ok=0; printf("     %s: navigating queued/sent a frame\n",TR(SET[i].label)); }
          if(taps>maxt) maxt=taps; if(drags>maxd) maxd=drags; }
      printf("       worst case: %d taps (+ %d scroll drag(s)) from HOME to an editor\n",maxt,maxd);
      st_check("reachability: every setting's editor opens by TAPS from HOME — feature settings in 2 (icon, row), SYSTEM settings in 3 (gear, section, row) — and the chevron leads back the same way; no frame is sent while navigating",ok&&maxt<=3);
      st_home(); tap(CX,GEAR_Y); st_settle(); tap(240,row_y0(PG_SYS,SEC_STOCK)+25); st_settle(); int kb=row_find(PG_SYS,RK_X,X_BOOTUI);
      tap(240,row_y0(PG_SYS,kb)-(int)scroll[PG_SYS]+25); st_settle(); int x1=view==V_SYS&&xsel==X_BOOTUI; tap(CX,57); st_settle();
      tap(240,row_y0(PG_SYS,kb+1)-(int)scroll[PG_SYS]+25); st_settle(); int x2=view==V_SYS&&xsel==X_SWITCH; tap(CX,57); st_settle();
      tap(240,row_y0(PG_SYS,SEC_STOCK)-(int)scroll[PG_SYS]+25); st_settle(); int cl=sec_open==-1&&scroll[PG_SYS]==0&&page_rows(PG_SYS)==NSEC+1;
      tap(240,row_y0(PG_SYS,NSEC)+25); st_settle(); int dg=page==PG_DIAG&&diag_from==PG_SYS; tap(CX,PG_TITLE_Y+6); st_settle(); int bs1=page==PG_SYS; tap(CX,PG_TITLE_Y+6); st_settle();
      st_check("SYSTEM: BOOT UI and SWITCH TO ORIGINAL NOW open from STOCK UI; a tap on the open header closes the section again; the DIAGNOSTICS row opens DIAGNOSTICS, whose chevron leads back to SYSTEM and from there to HOME",x1&&x2&&cl&&dg&&bs1&&page==PG_HOME&&wq.n==0&&txq_len==0);
      st_home(); tap(CX,GEAR_Y); st_settle(); tap(240,row_y0(PG_SYS,SEC_DISPLAY)+25); st_settle(); int o1=sec_open==SEC_DISPLAY&&row_find(PG_SYS,RK_SET,S_UILANG)==1;
      { int g[]={240,360, 240,330, 240,100}; st_gest(g,3); sim_run(100); }                                      /* DISPLAY is open: SCHEDULE's header is below the fold -> scroll (v6.18: 6 rows in DISPLAY · SOUND) */
      tap(240,row_y0(PG_SYS,row_find(PG_SYS,RK_SEC,SEC_SCHED))-(int)scroll[PG_SYS]+25); st_settle();
      int o2=sec_open==SEC_SCHED&&row_find(PG_SYS,RK_SET,S_UILANG)<0&&row_find(PG_SYS,RK_SET,S_PREHEAT)==SEC_SCHED+1&&(int)scroll[PG_SYS]==(int)clampf((float)(SEC_SCHED*ROW_P),0,scroll_max(PG_SYS));
      tap(CX,PG_TITLE_Y+6); st_settle(); tap(CX,GEAR_Y); st_settle();
      st_check("SYSTEM sections: one open at a time (opening SCHEDULE closes DISPLAY and brings its header to the top); leaving SYSTEM and coming back shows it closed again",o1&&o2&&page==PG_SYS&&sec_open==-1&&scroll[PG_SYS]==0); }
    /* ---- (3) tap vs hold: the same icon / pill, two meanings, one frame set ---- */
    { char qi[4][400], qs[4][400]; int okt=1, okh=1, oks=1;
      for(int k=0;k<4;k++){
          st_home(); int f=st_hold(ACT_X[k],ACT_Y,700); int n1=st_q_snap(qi[k],sizeof qi[k]); if(!(f&&act.pending==k&&page==PG_HOME&&view==V_MAIN&&n1>=1)) okh=0;
          st_home(); tap(ACT_X[k],ACT_Y); st_settle(); if(!(page==PG_GRIND+k&&view==V_MAIN&&wq.n==0&&txq_len==0&&!toast.dur&&act.pending==ACT_NONE)) okt=0;
          tap(CX,START_Y+START_H/2); sim_run(100); if(!(page==PG_GRIND+k&&wq.n==0&&txq_len==0&&toast.dur&&!strcmp(toast.msg,TR(TX_T_HOLD_HINT)))) oks=0; toast.dur=0;
          int f2=st_hold(CX,START_Y+START_H/2,700); int n2=st_q_snap(qs[k],sizeof qs[k]); int pend=act.pending==k; sim_run(500);
          if(!(f2&&pend&&n2==n1&&!strcmp(qi[k],qs[k])&&page==PG_HOME)){ oks=0; printf("     %s: icon hold '%s' vs HOLD TO START '%s' (page %d)\n",TR(ACT_NAME[k]),qi[k],qs[k],page); } }
      st_check("HOME icon: a TAP opens that feature's page — no frame, no toast, nothing started (v6.16: a toast 'HOLD THE BUTTON TO START' and a dead end)",okt);
      st_check("HOME icon: a HOLD (650 ms) still starts the activity directly, HOME stays",okh);
      st_check("feature page: HOLD TO START queues byte for byte the SAME frames as the icon hold (grind, brew, water, steam) and returns to HOME (the live view w. STOP); a short tap on it only says 'KEEP HOLDING TO START'",oks);
      st_hitbox(T_ACT0,&w,&h); st_home(); page=PG_BREW; static_dirty=1; sim_run(300); st_hitbox(T_START,&w,&h); printf("       HOLD TO START: %d x %d px (pill %d x %d)\n",w,h,START_W,START_H);
      st_check("touch: HOLD TO START has a touch area >= 48 px high and >= 200 px wide, clear of the list above it",w>=200&&h>=48&&START_HIT_Y>=FEAT_VY1&&hit_test(CX,FEAT_VY1-6,sim_now)!=T_START);
      st_hitbox(T_BACK,&w,&h); printf("       page header (close): %d x %d px\n",w,h);
      st_check("touch: a page's close target is its whole header (chevron + title), >= 48 px high and >= 120 px wide, clear of the first row",w>=120&&h>=48&&PBACK_Y0+PBACK_H<=LIST_VY0&&hit_test(CX,LIST_Y0+25,sim_now)==T_ROW0); }
    /* ---- (4) the live line: values when a start is possible, the reason when it is not ---- */
    { st_home(); page=PG_BREW; uint32_t c=page_live_text(PG_BREW,a,sizeof a,sim_now); char t1[16],t2[16]; fmt_t10(t1,sizeof t1,reg[40],1); fmt_t10(t2,sizeof t2,reg[42],0); snprintf(b,sizeof b,TR(TX_LV_BREW),t1,t2);
      int v1=!strcmp(a,b)&&c==C_TXT2; page_live_text(PG_WATER,a,sizeof a,sim_now); snprintf(b,sizeof b,TR(TX_LV_WATER),t1); int v2=!strcmp(a,b);
      int kg=SET[S_GRINDMODE].val; SET[S_GRINDMODE].val=2; c=page_live_text(PG_GRIND,a,sizeof a,sim_now); int g1=!strcmp(a,TR(TX_G_GRINDTIME))&&c==C_WARN; c=page_live_text(PG_BREW,a,sizeof a,sim_now); int g2=c==C_TXT2; SET[S_GRINDMODE].val=kg;
      uint16_t w1[1]={(uint16_t)sv(S_BREWW)}; wq_push(23,w1,1); c=page_live_text(PG_BREW,a,sizeof a,sim_now); int bz=act_guard(ACT_BREW,sim_now)==TR(TX_G_WAIT)&&c==C_TXT2&&strcmp(a,TR(TX_G_WAIT)); memset(&wq,0,sizeof wq);
      sim_link=0; sim_run(3000); c=page_live_text(PG_STEAM,a,sizeof a,sim_now); int off=!strcmp(a,TR(TX_G_NOLINK))&&c==C_WARN; int f=st_hold(CX,START_Y+START_H/2,700); int nf=wq.n==0&&act.pending==ACT_NONE&&toast.dur&&!strcmp(toast.msg,TR(TX_G_NOLINK)); (void)f; sim_link=1; sim_run(400);
      st_check("feature page live line: BREW 'BOILER 99.9° · GROUP 70°', WATER 'WATER 99.9°' (word 40 — the row that looked like a setting in v6.16); GRIND MODE = TIME shows the reason on GRIND only; a write in transit does not flicker 'ONE MOMENT…'",v1&&v2&&g1&&g2&&bz);
      st_check("feature page offline: the live line says NO CONNECTION (amber), HOLD TO START starts nothing and says why",off&&nf); }
    /* ---- (5) something starts running while a page is open: HOME (with STOP) takes over ---- */
    { st_home(); tap(ACT_X[ACT_GRIND],ACT_Y); st_settle(); int on=page==PG_GRIND; reg[33]=0x1307; sim_run(700); int h1=page==PG_HOME&&view==V_MAIN&&hit_test(160,372,sim_now)==T_STOP;   /* v6.20: STOP BREW is the left slot (HOLD · STEAM right) */
      st_home(); tap(CX,GEAR_Y); st_settle(); reg[33]=0x1307; sim_run(700); int s1=page==PG_SYS;
      st_home(); tap(ACT_X[ACT_BREW],ACT_Y); st_settle(); tap(240,LIST_Y0+25); st_settle(); int ed=view==V_DETAIL&&sel==S_BREWW; reg[33]=0x1307; sim_run(700); int e1=view==V_DETAIL; tap(CX,57); sim_run(900); int e2=view==V_MAIN&&page==PG_HOME;
      st_home(); reg[33]=0x1307; sim_run(400); int ic=hit_test(ACT_X[0],ACT_Y,sim_now)!=T_ACT0;
      st_check("an activity starts (on the machine) while a feature page is open: the UI slides to HOME, where STOP is; SYSTEM stays (deliberate); an open editor stays until it is closed — then HOME; no feature page can be opened while something runs",on&&h1&&s1&&ed&&e1&&e2&&ic); }
    /* ---- (6) one way back: chevron/header, swipe right, pull the header down; DIAGNOSTICS from HOME's error hint ---- */
    { st_home(); tap(CX,GEAR_Y); st_settle(); int g1[]={240,40, 240,60, 240,130}; st_gest(g1,3); st_settle(); int p1=page==PG_HOME;
      tap(ACT_X[ACT_BREW],ACT_Y); st_settle(); int g2[]={240,150, 240,170, 240,260}; st_gest(g2,3); st_settle(); int p2=page==PG_BREW&&scroll[PG_BREW]==0;
      int g3[]={240,300, 240,280, 240,200}; st_gest(g3,3); st_settle(); int p3=page==PG_BREW&&scroll[PG_BREW]>0;
      st_home(); int nohint=hit_test(240,262,sim_now)!=T_DIAG; reg[35]=1; sim_run(500); int hd=hit_test(240,262,sim_now)==T_DIAG&&strstr(hm.hint1,"E2")!=NULL; tap(240,262); st_settle(); int d1=page==PG_DIAG&&diag_from==PG_HOME;
      tap(CX,PG_TITLE_Y+6); st_settle(); int d2=page==PG_HOME; reg[35]=0; sim_run(300);
      sim_link=0; sim_run(3000); int ho=hit_test(240,262,sim_now)==T_DIAG&&!strcmp(hm.hint1,TR(TX_C_DIAGREF)); sim_link=1; sim_run(400);
      st_check("back: pulling a page's header down closes it; a vertical drag inside the list scrolls (never closes)",p1&&p2&&p3);
      st_check("error / offline on HOME: the hint 'CODE E2 · TAP FOR DETAILS' IS the way to DIAGNOSTICS (tap -> DIAGNOSTICS, chevron -> back to HOME); when all is well that area is no target",nohint&&hd&&d1&&d2&&ho); }
    /* ---- (7) STOP: the biggest, brightest thing under the numbers ---- */
    { Rect r1=stop_pill(1,0), r2=stop_pill(2,0), r3=stop_pill(2,1); uint32_t bg=bgpure[372*W+CX];
      float ct=st_cr(C_STOP_FG,C_STOP_T), cb=st_cr(C_STOP_FG,C_STOP_B), cbg=st_cr(C_STOP_T,bg), old=st_cr(rgb(120,40,44),bg);
      st_home(); reg[33]=0x030B; sim_run(400); st_hitbox(T_STOP,&w,&h); int gw,gh; st_hitbox(T_GEAR,&gw,&gh);   /* v6.20: water — a brew now has two buttons */
      printf("       STOP: pill %d x %d (v6.16: 160 x 44), touch %d x %d · white on red %.2f / %.2f · red on background %.2f (v6.16's dark red: %.2f)\n",r1.x1-r1.x0,r1.y1-r1.y0,w,h,ct,cb,cbg,old);
      st_check("STOP: one pill 216 x 54 (v6.16: 160 x 44 — 66 % larger), two pills 152 x 54 with a gap, all inside the widget and the circle's chord; touch area >= 232 x 60 and the largest target on HOME while something runs",
               r1.x1-r1.x0==216&&r1.y1-r1.y0==54&&r2.x1-r2.x0==152&&r3.x0-r2.x1>=8&&r2.x0>=72&&r3.x1<=408&&(r1.x1-r1.x0)/2<=chord_half(STOP_PILL_Y,STOP_PILL_H)&&r3.x1-CX<=chord_half(STOP_PILL_Y,STOP_PILL_H)&&w>=232&&h>=60&&w*h>gw*gh);
      st_check("STOP contrast: white text >= 4.5:1 on the whole red gradient; the red >= 3:1 against the background (v6.16's dark red was below 2:1 — it read as disabled)",ct>=4.5f&&cb>=4.5f&&cbg>=3.f&&old<2.f); }
    /* ---- (8) less text: what is on screen, measured ---- */
    { st_home(); page=PG_BREW; static_dirty=1; sim_run(300); int ne=1;
      static const int probe[]={S_BREWT,S_HOTW,S_UILANG,S_PHAT,S_TZ,S_VOL};
      for(unsigned q=0;q<sizeof probe/sizeof probe[0];q++){ detail_open(probe[q],sim_now); sim_run(500); force_all=1; ui_frame(sim_now+1);
          for(int y=362;y<426;y++) for(int x=140;x<340;x++) if(canvas[y*W+x]!=bgpure[y*W+x]){ ne=0; y=H; break; }
          detail_close(sim_now); sim_run(400); }
      st_check("editor: nothing is drawn below CANCEL / SAVE any more (v6.16: two footer lines on every editor — 'CONTROLLER NOW + CONFIG', 'STOCK UI: AFTER RESTART', file paths)",ne);
      detail_open(S_HOTW,sim_now); sim_run(400); uint32_t c=detail_status(a,sizeof a); int s1=!strcmp(a,TR(TX_D_HOLDFAST))&&c==C_TXT3; tap(396,184); sim_run(50); detail_status(a,sizeof a); int s2=strstr(a,TR(TX_D_HOLDFAST))==NULL&&a[0]; detail_close(sim_now); sim_run(400);
      detail_open(S_PISW,sim_now); sim_run(400); detail_status(a,sizeof a); int s3=!a[0]&&!detail_holdable(&SET[S_PISW])&&!detail_holdable(&SET[S_BREWT])&&!detail_holdable(&SET[S_TZ])&&detail_holdable(&SET[S_VOL])&&detail_holdable(&SET[S_PHAT]); detail_close(sim_now); sim_run(400);
      st_check("editor status line: untouched + a long range -> 'HOLD - OR + TO GO FAST' (the stepper's hidden ability, said where it applies); changed -> 'CHANGED · WAS …'; an ON/OFF or short range -> empty ('SAVED VALUE' is gone)",s1&&s2&&s3);
      int bad=0; for(int i=0;i<NSTR;i++){ int k=STR_K[i]; if(k==KCON||k==KDL||k==KDV||k==KDF) continue; for(int lg=0;lg<NLANG;lg++){ const char*t=STR[lg][i];
            if(strstr(t,"CONFIG ONLY")||strstr(t,"KUN CONFIG")||strstr(t,"CMD8")||strstr(t,"CONFIG.JSON")||strstr(t,"OR STOP")||strstr(t,"ELLER STOP")||strstr(t,"EST.")){ bad++; printf("     [%s] %s: '%s'\n",LANG_CODE[lg],STR_ID[i],t); } } }
      printf("       string table: %d ids (v6.16: 449)\n",NSTR);
      st_check("text: no user-facing string says CONFIG ONLY / CONFIG.JSON / CMD8 / '· OR STOP' / 'EST.' any more; the string table is smaller than v6.16's 449 ids although v6.17 adds pages (v6.18's sound block aside)",bad==0&&NSTR-(TX_DV_SO_DROP-TX_SL_SOUND+1)-(TX_DV_WD_FAULT-TX_ER_E2_SCALE+1)<449);   /* v6.18: the sound feature's own strings (one contiguous block) are not counted; v6.22: nor the water gate's */
      st_home(); home_model(sim_now); int one=!strcmp(hm.hint1,TR(TX_C_HINT))&&hm.hint2==NULL&&!strcmp(hm.label,TR(TX_H_BOILER))&&!st_has(hm.label,"\xC2\xB7");
      reg[33]=0x0141; sim_run(600); home_model(sim_now); fmt_fix(a,sizeof a,sv(S_GRINDW),1); snprintf(b,sizeof b,TR(TX_H_AUTOSTOP_G),a); int g=!strcmp(hm.label,TR(TX_H_DOSE))&&!st_has(hm.label,a)&&!strcmp(hm.sub,b)&&sub_font(&hm)==F_S;
      st_check("HOME: one hint line; the label is the name of the number only ('BREW BOILER', 'DOSE' — no '· NOW', no repeated target); during an activity the target is said once, in F_S ('STOPS AT 18.5 G')",one&&g); }
    /* ---- (9) the icons say what they will do ---- */
    { st_home(); home_model(sim_now); char e[4][16]; char u[16];
      fmt_fix(u,sizeof u,sv(S_GRINDW),1); snprintf(e[0],16,"%s %s",u,TR(TX_U_G)); fmt_fix(u,sizeof u,sv(S_BREWW),-1); snprintf(e[1],16,"%s %s",u,TR(TX_U_G));
      fmt_fix(u,sizeof u,sv(S_HOTW),-1); snprintf(e[2],16,"%s %s",u,TR(TX_U_G)); fmt_t10(u,sizeof u,sv(S_STEAMT),0); snprintf(e[3],16,"%s\xC2\xB0",u);
      ok=1; for(int k=0;k<4;k++) ok&=!strcmp(hm.alab[k],e[k]);
      printf("       icon targets: %s · %s · %s · %s\n",hm.alab[0],hm.alab[1],hm.alab[2],hm.alab[3]);
      int kv=SET[S_BREWW].val, ktu=SET[S_TUNIT].val; SET[S_BREWW].val=420; SET[S_TUNIT].val=0; home_model(sim_now); int chg=!strcmp(hm.alab[1],"42 G")&&strstr(hm.alab[3],"149")!=NULL; SET[S_BREWW].val=kv; SET[S_TUNIT].val=ktu;
      int kl=SET[S_HOTW].loaded; SET[S_HOTW].loaded=0; home_model(sim_now); int nl=!strcmp(hm.alab[2],"--"); SET[S_HOTW].loaded=kl; home_model(sim_now);
      st_check("HOME icons carry their target underneath (grind dose, brew weight, water amount, milk temperature in the chosen unit) and follow the settings; a value that is not loaded shows '--'",ok&&chg&&nl);
      char sm[40]; sec_summary(SEC_DISPLAY,sm,sizeof sm); int q1=!strcmp(sm,TR(TX_UL_EN+ui_lang)); sec_summary(SEC_CLOCK,sm,sizeof sm); char ck[24]; home_clock_text(ck,sizeof ck); int q2=!strcmp(sm,ck);
      int kon=sch.on,khm=sch.hm; sch.on=0; sec_summary(SEC_SCHED,sm,sizeof sm); int q3=!strcmp(sm,TR(TX_E_OFF)); sch.on=1; sch.hm=390; sec_summary(SEC_SCHED,sm,sizeof sm); int q4=!strcmp(sm,"06:30"); sch.on=kon; sch.hm=khm; sec_summary(SEC_STOCK,sm,sizeof sm);
      uint32_t dc=diag_row_text(a,sizeof a,sim_now); int q5=!strcmp(a,TR(TX_DV_OK))&&dc==C_OK; reg[35]=1; dc=diag_row_text(a,sizeof a,sim_now); int q6=!strcmp(a,"E2")&&dc==C_ERR; reg[35]=0;
      st_check("SYSTEM's closed headers say what is inside: DISPLAY = the UI language's own name, CLOCK = the time, SCHEDULE = OFF / the pre-heat time; the DIAGNOSTICS row shows OK / the error code",q1&&q2&&q3&&q4&&!sm[0]&&q5&&q6); }
    sim_reset(); sim_tick=NULL;
}
/* ============================ v6.14: the power button ============================ */
#define PW_V_CMD8 "aa 55 08 ea 07 0b 12 10 23 1c 00 00 00 cc df"   /* 2026-11-18 16:35:28, alarm 0/0/0 (independently computed CRC) */
#define PW_V_OFF  "aa 55 06 00 02 00 00 00 00 56 94"               /* stock powerOff: usart_send(6,0,2), word 0 = 0, word 1 = 0 */
#define PW_V_ON   "aa 55 06 00 01 01 00 c5 5f"                     /* PowerOn 0x146EF8: usart_send(6,0,1), word 0 = 1 */
#define PW_V_KA   "aa 55 06 01 01 00 00 c5 33"                     /* awake: word 1 = 0 */
/* txq -> list of frames (hex), in the order they went out */
static int st_txq_frames(char out[][96],int max){
    int n=0, i=0;
    while(i+5<=txq_len&&n<max){
        if(txq[i]!=0xAA||txq[i+1]!=0x55){ i++; continue; }
        int c=txq[i+2], L= c==0x08?15:(c==0x06?2*txq[i+4]+7:(c==0x03?7:(c==0x09?txq[i+3]+7:7)));
        if(i+L>txq_len) break;
        char*p=out[n]; p[0]=0; for(int k=0;k<L&&k<31;k++) p+=sprintf(p,k?" %02x":"%02x",txq[i+k]);
        n++; i+=L;
    }
    return n;
}
/* main loop in 10 ms steps: link (as the controller's replies), write queue, auto-ACK of every cmd6, button state, keep-awake */
static ms_t st_pw_loop(ms_t t,ms_t dur,int link,int ack){
    ms_t end=t+dur;
    for(;t<end;t+=10){
        if(link) last_rx_ms=t;
        wq_step(t);
        if(ack&&wq.n&&wq.state==WQ_WAIT&&t-wq.sent_ms>=20) st_ack_last(t);
        pw_step(t); proto_keepawake(t,0);
    }
    ui_frame(t); return t;
}
static ms_t st_pw_home(void){ ms_t t=st_home(); ka_next=t+30000; return t; }   /* keep-awake: next deadline in 30 s (as after startup) */
/* ================= v6.16: TIME & SCHEDULING selftest ================= */
#define TM_V_ARM "aa 55 08 ea 07 0b 12 10 23 1c 13 06 1e be b2"   /* 2026-11-18 16:35:28 local, alarm day 19 06:30 (CRC computed independently) */
#define TM_V_OFF0 PW_V_CMD8                                       /* the same time, alarm 0/0/0 */
static ms_t st_tm_loop(ms_t t,ms_t dur,int link){
    ms_t end=t+dur;
    for(;t<end;t+=10){
        if(link) last_rx_ms=t;
        wq_step(t);
        if(wq.n&&wq.state==WQ_WAIT&&t-wq.sent_ms>=20) st_ack_last(t);
        pw_step(t); sched_step(t); proto_keepawake(t,0);
    }
    ui_frame(t); return t;
}
static int st_cmd8_frames(char fr[][96],int max,char only[][96]){   /* only the cmd8 frames in the TX queue, in order */
    char all[64][96]; int n=st_txq_frames(all,64), k=0;
    for(int i=0;i<n&&k<max;i++) if(!strncmp(all[i],"aa 55 08",8)) strcpy(only[k++],all[i]);
    (void)fr; return k;
}
static int st_own_save(int i,int val,ms_t t){ detail_open(i,t); edit_val=val; save_begin(t); return view==V_MAIN&&SET[i].val==val; }
static int64_t st_utc(int y,int mo,int d,int h,int mi,int s){ return days_civil(y,mo,d)*86400+h*3600LL+mi*60LL+s; }
static void st_time(const char*dir){
    printf("== selftest: v6.16 time & scheduling (stock update_time 0xD5944, _refresh_systime 0x135548, performSleepActions 0x148324, powerOffSend 0x14700C; MCU 0x9C88 + sleep task 0x2F52) ==\n");
    char b[64], fr[16][96]; int n; ms_t t;
    int keep_tm=SET[S_TIMEMODE].val, keep_tz=SET[S_TZ].val, keep_dst=SET[S_DST].val;
    char keep_sp[256]; snprintf(keep_sp,sizeof keep_sp,"%s",sched_path); snprintf(sched_path,sizeof sched_path,"%s/schedule",dir); unlink(sched_path);
    /* ---- (1) TIME FORMAT = stock ---- */
    st_check("timemode semantics = stock: OPT_TIME[0] = 12-HOUR, OPT_TIME[1] = 24-HOUR (update_time XORs model[0x59] = timemode != 0 with 1: 1 -> \"%02d:%02d\", 0 -> A.M./P.M.)",
             OPT_TIME[0]==TX_E_12H&&OPT_TIME[1]==TX_E_24H&&SET[S_TIMEMODE].min==0&&SET[S_TIMEMODE].max==1&&!strcmp(SET[S_TIMEMODE].key,"timemode"));
    { SET[S_TIMEMODE].val=1; set_fmt(S_TIMEMODE,1,b,sizeof b); int a=!strcmp(b,TR(TX_E_24H)); set_fmt(S_TIMEMODE,0,b,sizeof b); a&=!strcmp(b,TR(TX_E_12H));
      st_check("the TIME FORMAT row shows 24-HOUR for config timemode = 1 (the stock switch \"24-Hour Format\" is ON) and 12-HOUR for 0",a); }
    { static const struct { int mode,h,m; const char*want; } V[]={{1,0,5,"00:05"},{1,13,7,"13:07"},{1,23,59,"23:59"},{0,0,5,"12:05 A.M."},{0,11,59,"11:59 A.M."},
          {0,12,0,"12:00 P.M."},{0,13,7,"01:07 P.M."},{0,23,59,"11:59 P.M."}}; int ok=1;
      for(unsigned k=0;k<sizeof V/sizeof V[0];k++){ SET[S_TIMEMODE].val=V[k].mode; fmt_hm(b,sizeof b,V[k].h,V[k].m,1); if(strcmp(b,V[k].want)){ ok=0; printf("     timemode %d %02d:%02d -> '%s' (want '%s')\n",V[k].mode,V[k].h,V[k].m,b,V[k].want); } }
      st_check("clock format = stock update_time byte for byte: 24 h \"%02d:%02d\"; 12 h 0 -> 12 A.M., 1..11 A.M., 12 -> 12 P.M., 13..23 -> 1..11 P.M. (8 vectors)",ok); }
    /* ---- (2) local time + DST ---- */
    { int ok=TZ_N==136&&TZT[48].off==3600&&!strcmp(TZT[48].utc,"UTC+01:00")&&TZT[43].off==0&&TZT[0].off==-43200&&TZT[135].off==50400&&TZT[30].off==-9000;
      st_check("zone table = stock 0x1A5890: 136 entries (#0 UTC-12, #43 UTC, #48 UTC+01:00 Amsterdam/Berlin, the stock quirks #30 = -2:30 and #135 = +14:00 kept)",ok); }
    { SET[S_TZ].val=48; SET[S_DST].val=0; clk_fake=PV_CLK; struct tm lt; int v=clk_now(&lt);
      st_check("local = SoC UTC + zone offset (+ DST): 15:35:28 UTC in zone #48 = 16:35:28 Wednesday 2026-11-18",v&&lt.tm_hour==16&&lt.tm_min==35&&lt.tm_sec==28&&lt.tm_wday==3&&lt.tm_mday==18); }
    { static const struct { int tz; int64_t u; int want; const char*what; } D[]={
        {48,0,0,"EU 2026-03-29 00:59:59 UTC: standard"},{48,1,1,"EU 01:00:00 UTC: DST"},{48,2,1,"EU 2026-10-25 00:59:59 UTC: DST"},{48,3,0,"EU 01:00:00 UTC: standard"},
        {13,4,0,"US Central 2026-03-08 07:59:59 UTC: standard"},{13,5,1,"US 08:00 UTC (02:00 CST): DST"},{13,6,1,"US 2026-11-01 06:59:59 UTC: DST"},{13,7,0,"US 07:00 UTC: standard"},
        {116,8,1,"AU Sydney January: DST (southern)"},{116,9,0,"AU July: standard"},{101,10,0,"Beijing (no rule): never"}};
      int64_t U[11]={st_utc(2026,3,29,0,59,59),st_utc(2026,3,29,1,0,0),st_utc(2026,10,25,0,59,59),st_utc(2026,10,25,1,0,0),
                     st_utc(2026,3,8,7,59,59),st_utc(2026,3,8,8,0,0),st_utc(2026,11,1,6,59,59),st_utc(2026,11,1,7,0,0),
                     st_utc(2026,1,15,12,0,0),st_utc(2026,7,15,12,0,0),st_utc(2026,7,15,12,0,0)}; int ok=1;
      for(unsigned k=0;k<sizeof D/sizeof D[0];k++){ int g=dst_on(D[k].tz,U[D[k].u]); if(g!=D[k].want){ ok=0; printf("     %s: got %d\n",D[k].what,g); } }
      SET[S_TZ].val=48; SET[S_DST].val=1; struct tm lt; clk_local_at(U[1],&lt); int a=lt.tm_hour==3&&lt.tm_min==0; SET[S_DST].val=0; clk_local_at(U[1],&lt); a&=lt.tm_hour==2;
      st_check("DST windows (sumtime = 1): EU last Sun Mar/Oct 01:00 UTC, US 2nd Sun Mar / 1st Sun Nov 02:00 local, AU southern; zones without a rule never (11 vectors); sumtime = 0 -> never +1 h",ok&&a); }
    /* ---- (3) stock's next-occurrence algorithm (performSleepActions, one entry) ---- */
    { struct V { int y,mo,d,h,mi,hm,mask,k,ad; const char*what; };
      static const struct V V[]={
        {2026,11,18,16,35,390,0x7F,1,19,"daily 06:30 at Wed 16:35 -> tomorrow 19"},{2026,11,18,16,35,1020,0x7F,0,18,"daily 17:00 at 16:35 -> today"},
        {2026,11,18,6,30,390,0x7F,1,19,"daily 06:30 at 06:30 -> strictly after now: tomorrow"},{2026,11,20,16,35,390,0x3E,3,23,"Mon-Fri at Fri 16:35 -> Mon 23"},
        {2026,11,18,16,35,480,0x41,3,21,"Sat-Sun 08:00 at Wed -> Sat 21"},{2026,11,18,16,35,390,0x00,1,19,"once 06:30 at 16:35 -> tomorrow"},
        {2026,11,18,16,35,1020,0x00,0,18,"once 17:00 at 16:35 -> today"},{2026,11,30,20,0,390,0x7F,1,1,"Nov 30 -> Dec 1 (month wrap, stock biz_days_in_month)"},
        {2026,12,31,20,0,390,0x7F,1,1,"Dec 31 -> Jan 1"},{2028,2,28,20,0,390,0x7F,1,29,"2028-02-28 -> 29 (leap year)"},{2026,11,18,16,35,390,0x08,7,25,"Wed-only 06:30 at Wed 16:35 -> next Wed 25"}};
      int ok=1; for(unsigned k=0;k<sizeof V/sizeof V[0];k++){ struct tm tm; memset(&tm,0,sizeof tm); tm.tm_year=V[k].y-1900; tm.tm_mon=V[k].mo-1; tm.tm_mday=V[k].d; tm.tm_hour=V[k].h; tm.tm_min=V[k].mi; tm.tm_wday=dow_civil(V[k].y,V[k].mo,V[k].d);
          int ad; int kk=sched_next(&tm,V[k].hm,V[k].mask,&ad); if(kk!=V[k].k||ad!=V[k].ad){ ok=0; printf("     %s: got +%d day %d\n",V[k].what,kk,ad); } }
      st_check("next occurrence = stock performSleepActions (today if the weekday matches and hh:mm > now, else the next flagged weekday; one-shot today/tomorrow; day wrapped by the month's length) — 11 vectors",ok); }
    /* ---- (4) the alarm frame byte for byte ---- */
    t=st_pw_home(); SET[S_TIMEMODE].val=1;
    { sch.on=1; sch.hm=390; sch.rep=1; sch.once_at=0; struct tm tm; int ad,ah,am; pw_now_tm(&tm); int w=sched_alarm(clk_utc(),&ad,&ah,&am,NULL); uint8_t f[15]; pw_cmd8(f,&tm,ad,ah,am);
      st_check("pre-heat daily 06:30 at Wed 16:35:28 -> cmd8 = " TM_V_ARM " (= stock powerOffSend(19, 6, 30): alarm day = day of month, hour, minute)",w&&hexeq(f,15,TM_V_ARM));
      sch.on=0; w=sched_alarm(clk_utc(),&ad,&ah,&am,NULL); pw_cmd8(f,&tm,ad,ah,am);
      st_check("no pre-heat -> alarm 0/0/0 = " TM_V_OFF0 " (stock: no entry -> powerOffSend(0, 0, 0); the MCU skips day+hour+minute = 0)",!w&&hexeq(f,15,TM_V_OFF0));
      sch.on=1; clk_fake=10000; w=sched_alarm(clk_utc(),&ad,&ah,&am,NULL); clk_fake=PV_CLK;
      st_check("SoC clock not set (before 2025): no alarm (0/0/0) — a garbage alarm would wake the machine at a random time",!w&&!ad&&!ah&&!am); sch.on=0; }
    /* ---- (5) the schedule file + SAVE path (no config.json write, no MCU word) ---- */
    { cfg_read(); char m0[33]; strcpy(m0,cfg_md5); int w0=cfg_writes; t=st_pw_home(); txq_len=0;
      int a=st_own_save(S_PREHEAT,1,t)&&st_own_save(S_PHAT,395,t)&&st_own_save(S_PHREP,2,t)&&st_own_save(S_AUTOOFF,3,t);
      char rb[256]; int fd=open(sched_path,O_RDONLY); int rn=fd>=0?(int)read(fd,rb,sizeof rb-1):-1; if(fd>=0) close(fd); if(rn<0) rn=0; rb[rn]=0;
      cfg_read(); char tp[300]; snprintf(tp,sizeof tp,"%s.tmp",sched_path);
      st_check("SAVE of the 4 time rows -> " LC "merakiplus/schedule" UC " = preheat=1, at=06:35, repeat=mon-fri, autooff=30; atomic (no tmp left); toast SAVED · MERAKI+ SCHEDULE",
               a&&!strcmp(rb,"preheat=1\nat=06:35\nrepeat=mon-fri\nautooff=30\nonce_at=0\n")&&access(tp,F_OK)!=0&&!strcmp(toast.msg,TR(TX_T_OWN_SAVED)));
      st_check("... and config.json is NOT written (md5 unchanged, 0 config writes) and no cmd6 frame was queued for it",!strcmp(cfg_md5,m0)&&cfg_writes==w0&&wq.n==0);
      sch.on=0; sch.hm=0; sch.rep=0; sch.ao=0; settings_load();
      st_check("reload: the file gives the rows back (PRE-HEAT ON, 06:35, MON-FRI, 30 MIN)",sch.on==1&&sch.hm==395&&sch.rep==2&&sch.ao==3&&SET[S_PHAT].val==395&&SET[S_AUTOOFF].val==3&&sch.src==SCS_FILE);
      put_bytes(sched_path,"preheat=7\nat=25:99\nrepeat=hourly\nautooff=45\n",44); settings_load();
      st_check("bad values -> defaults (pre-heat OFF, 06:30, EVERY DAY, auto off OFF) — never a surprise alarm",sch.on==0&&sch.hm==390&&sch.rep==1&&sch.ao==0);
      unlink(sched_path); settings_load(); st_check("no file -> defaults, source DEFAULT",sch.on==0&&sch.src==SCS_DEFAULT);
      st_check("the 4 time rows are merakiplus's own (SF_OWN): no config key, no MCU word, not in --dump-table; the stepper for PRE-HEAT AT is 5 min, 00:00..23:55",
               !SET[S_PREHEAT].key&&!SET[S_PHAT].key&&SET[S_PHAT].mcu==M_NONE&&(SET[S_PHAT].flags&SF_OWN)&&SET[S_PHAT].step==5&&SET[S_PHAT].max==1435); }
    /* ---- (6) pre-arm while on ---- */
    t=st_pw_home(); txq_len=0; t=st_tm_loop(t,8000,1); n=st_cmd8_frames(NULL,16,fr);
    st_check("no schedule: nothing is sent while on (v6.15 traffic unchanged)",n==0&&arm.n_sent==0);
    st_own_save(S_PHREP,1,t); st_own_save(S_PREHEAT,1,t); txq_len=0; t=st_tm_loop(t,200,1); n=st_cmd8_frames(NULL,16,fr);
    st_check("schedule ON (every day 06:30): ONE cmd8 pre-arm = " TM_V_ARM,n==1&&!strcmp(fr[0],TM_V_ARM)&&arm.sent&&arm.ad==19&&arm.ah==6&&arm.am==30);
    { uint8_t a[15]; memcpy(a,arm.f8,15); a[2]=0x88; uint16_t c=crc16(a,13); a[13]=c&0xFF; a[14]=c>>8; rx_feed(a,15,t); }
    t=st_tm_loop(t,20000,1); n=st_cmd8_frames(NULL,16,fr);
    st_check("... no repeat while the alarm is unchanged (20 s); the 0x88 echo is counted",n==1&&arm.n_ack==1);
    rps.n_ver++; t=st_tm_loop(t,6000,1); n=st_cmd8_frames(NULL,16,fr);
    st_check("MCU restart (re-push counter): the RAM alarm is gone -> re-armed once",n==2&&!strcmp(fr[1],TM_V_ARM));
    ro_mode=1; arm.sent=0; t=st_tm_loop(t,6000,1); n=st_cmd8_frames(NULL,16,fr); ro_mode=0;
    st_check("read-only mode: no pre-arm",n==2);
    t=st_tm_loop(t,6000,1); n=st_cmd8_frames(NULL,16,fr);
    st_own_save(S_PREHEAT,0,t); t=st_tm_loop(t,6000,1); int n2=st_cmd8_frames(NULL,16,fr); t=st_tm_loop(t,8000,1); int n3=st_cmd8_frames(NULL,16,fr);
    st_check("schedule OFF: ONE disarm frame with alarm 0/0/0, then nothing",n2==n+1&&!strcmp(fr[n2-1],TM_V_OFF0)&&n3==n2&&!arm.sent);
    /* ---- (7) power-off carries the alarm (stock), no pre-arm while off, re-arm after the wake ---- */
    t=st_pw_home(); st_own_save(S_PREHEAT,1,t); t=st_tm_loop(t,300,1); txq_len=0;
    pw_key(116,1,t); t=st_tm_loop(t,1510,1); pw_key(116,0,t); t=st_tm_loop(t,200,1);
    { char all[16][96]; int na=st_txq_frames(all,16);
      st_check("power-off with a schedule: cmd8 = " TM_V_ARM " (alarm 19 06:30, stock performSleepActions) -> cmd6 sub0 len2 " PW_V_OFF " — nothing else",na==2&&!strcmp(all[0],TM_V_ARM)&&!strcmp(all[1],PW_V_OFF)&&pw.state==PW_OFF); }
    txq_len=0; t=st_tm_loop(t,12000,0); n=st_cmd8_frames(NULL,16,fr);
    st_check("OFF: no pre-arm (the power-off frame carries the alarm; the MCU does not listen while asleep)",n==0&&pw.state==PW_OFF);
    reg[0]=1; t=st_tm_loop(t,1000,1); n=st_cmd8_frames(NULL,16,fr);   /* the controller answers again (alarm wake, word 0 = 1) -> pw_on */
    t=st_tm_loop(t,6000,1); n=st_cmd8_frames(NULL,16,fr);
    st_check("wake (word 0 = 1 again = MCU reset): PowerOn + the alarm is re-armed for the NEXT occurrence",pw.state==PW_ON&&n==1&&!strcmp(fr[0],TM_V_ARM));
    /* ---- (8) one-shot ---- */
    t=st_pw_home(); st_own_save(S_PHREP,0,t); st_own_save(S_PREHEAT,1,t);
    { int64_t want=st_utc(2026,11,19,5,30,0); int ad,ah,am; sched_alarm(clk_utc(),&ad,&ah,&am,NULL);
      st_check("ONCE 06:30 saved at Wed 16:35: once_at = Thu 19 05:30 UTC (06:30 local) -> alarm 19 06:30; the file has once_at",sch.once_at==want&&ad==19&&ah==6&&am==30);
      clk_fake=want+30; t=st_tm_loop(t,100,1); int still=sch.on;
      clk_fake=want+61; t=st_tm_loop(t,100,1); settings_load();
      st_check("ONCE: still ON within its minute; a minute later it switches itself OFF and saves it (stock deal_preheat_data), toast ONE-TIME PRE-HEAT USED",still&&!sch.on&&!SET[S_PREHEAT].val&&!strcmp(toast.msg,TR(TX_T_ONCE_USED)));
      clk_fake=PV_CLK; }
    /* ---- (9) auto off ---- */
    t=st_pw_home(); st_own_save(S_PREHEAT,0,t); st_own_save(S_AUTOOFF,1,t); txq_len=0; toast.dur=0; idle_kick(t);
    t=st_tm_loop(t,14*60000+29000,1); int quiet=pw.state==PW_ON&&!toast.dur;
    t=st_tm_loop(t,2000,1); int warn=toast.dur&&!strcmp(toast.msg,TR(TX_T_AUTOOFF_SOON));
    ui_touch(TE_DOWN,240,240,t); ui_touch(TE_UP,240,240,t+50); t=st_tm_loop(t,60000,1); int reset=pw.state==PW_ON&&!toast.dur;
    reg[33]=0x0101|0x1000; reg[22]=1; t=st_tm_loop(t,16*60000,1); int busy=pw.state==PW_ON; reg[33]=0x0101; reg[22]=0;
    txq_len=0; t=st_tm_loop(t,15*60000+500,1); static char all[64][96]; int na=st_txq_frames(all,64);
    st_check("AUTO OFF 15 MIN: quiet until 14:29; the warning toast 30 s before; a touch restarts the timer and dismisses it",quiet&&warn&&reset);
    st_check("... an activity (brew bits) keeps it on (stock isExtractionOrSteamSwitchOn -> recodeTime)",busy);
    st_check("... after 15 min idle: the stock power-off sequence (cmd8 w. alarm 0/0/0 -> cmd6 sub0 len2 " PW_V_OFF ")",pw.state!=PW_ON&&ao_n>=1&&na>=2&&!strcmp(all[na-2],TM_V_OFF0)&&!strcmp(all[na-1],PW_V_OFF));
    st_own_save(S_AUTOOFF,0,st_pw_home());
    /* ---- (10) HOME clock ---- */
    { t=st_pw_home(); SET[S_TIMEMODE].val=1; char c[24]; int v=home_clock_text(c,sizeof c); int a=v&&!strcmp(c,"16:35");
      SET[S_TIMEMODE].val=0; v=home_clock_text(c,sizeof c); a&=v&&!strcmp(c,"04:35 P.M."); SET[S_TIMEMODE].val=1;
      clk_fake=10000; v=home_clock_text(c,sizeof c); clk_fake=PV_CLK;
      st_check("HOME top line: \"16:35\" (timemode 1) / \"04:35 P.M.\" (timemode 0); clock not set -> the wordmark",a&&!v); }
    { t=st_pw_home(); sch.on=1; sch.rep=1; sch.hm=390; sch.once_at=0; char h[72]; int v=home_preheat_text(h,sizeof h); char want[72]; snprintf(want,sizeof want,TR(TX_C_PREHEAT),TR(TX_WD_THU),"06:30");
      home_model(t); st_check("HOME hint: a planned pre-heat is shown as the second hint line \"{clock} PRE-HEAT · THU 06:30\" (the next occurrence)",v&&!strcmp(h,want)&&hm.hint2&&!strcmp(hm.hint2,want));
      sch.on=0; home_model(t); st_check("... no pre-heat: no second line (v6.17: one hint line)",hm.hint2==NULL&&!strcmp(hm.hint1,TR(TX_C_HINT))); }
    unlink(sched_path); snprintf(sched_path,sizeof sched_path,"%s",keep_sp);
    SET[S_TIMEMODE].val=keep_tm; SET[S_TZ].val=keep_tz; SET[S_DST].val=keep_dst; clk_fake=PV_CLK; settings_load();
}
static void st_power(void){
    printf("== selftest: v6.14 the power button (k62091 / event1; stock app_powerKey_thread 0x140AB4 + dealPowerState 0x14911C + powerOff 0x149074; MCU sleep-task 0x2F52) ==\n");
    char fr[16][96]; int nf; ms_t t, t0; uint8_t b[16];
    /* ---- (1) frames byte for byte ---- */
    { struct tm tm; pw_now_tm(&tm); pw_cmd8(b,&tm,0,0,0);
      st_check("cmd8 time/alarm frame = stock powerOffSend(0,0,0): " PW_V_CMD8 " (year u16 LE, month 1-12, day, hour, minute, second, alarm 0/0/0 = none; CRC-16/MODBUS over 13 bytes)",hexeq(b,15,PW_V_CMD8)); }
    { uint16_t w[2]={0,0}; int n=build_write(b,0,w,2); st_check("PowerOff = cmd6 sub0 len2 [word 0 = 0, word 1 = 0]: " PW_V_OFF " (stock powerOff: mirror word 0 = 0, usart_send(6,0,2); word 1 = awake)",hexeq(b,n,PW_V_OFF)); }
    { uint16_t w=1; int n=build_write(b,0,&w,1); st_check("PowerOn = cmd6 sub0 len1 [word 0 = 1]: " PW_V_ON " (stock PowerOn 0x146EF8; same frame as merakiplus's startup)",hexeq(b,n,PW_V_ON)); }
    /* ---- (2) frame policy: word 0 = 0 only with power-off intent ---- */
    { uint16_t z2[2]={0,0}; wq.n=0; int a=wq_push_t(0,z2,2,I_POWEROFF)==1&&wq.n==1; wq.n=0;
      st_check("policy: sub0 len2 [0,0] WITH I_POWEROFF is queued; WITHOUT intent it is rejected (WJ_TRIG) — also as sub0 len1 [0]",a&&rej(0,z2,2,0,WJ_TRIG)&&rej(0,W1(0),1,0,WJ_TRIG));
      st_check("policy: I_POWEROFF allows nothing else — start word (word 9 = 1), reboot (word 30 = 1) and word 1 = 1 (sleep/drain) are still rejected; PowerOn (word 0 = 1) without intent is unchanged OK",
               rej(9,W1(1),1,I_POWEROFF,WJ_TRIG)&&rej(30,W1(1),1,I_POWEROFF,WJ_TRIG)&&rej(1,W1(1),1,I_POWEROFF,WJ_RANGE)&&rej(0,W1(2),1,I_POWEROFF,WJ_RANGE)&&wq_push(0,W1(1),1)&&(wq.n=0,1)); }
    /* ---- (3) short press: nothing ---- */
    t=st_pw_home(); pw_key(116,1,t); t=st_pw_loop(t,1400,1,1); pw_key(116,0,t); t=st_pw_loop(t,3000,1,1);
    st_check("short press (1.4 s < 1.5 s): no frames, ON, counted as a short press (stock: only wakes its screensaver)",txq_len==0&&pw.state==PW_ON&&pw.n_short==1&&pw.n_off==0&&view==V_MAIN);
    /* ---- (4) HOLD: cmd8 -> 80 ms -> cmd6 sub0 len2 -> ACK -> OFF ---- */
    t0=t=st_pw_home(); page=PG_STEAM; static_dirty=1; pw_key(116,1,t); t=st_pw_loop(t,1490,1,0);
    int before=txq_len==0&&pw.state==PW_ON;
    t=st_pw_loop(t,20,1,0); nf=st_txq_frames(fr,16);
    int at=nf==1&&!strcmp(fr[0],PW_V_CMD8)&&pw.state==PW_OFFING&&view==V_OFF&&pw.t_req-t0>=PW_HOLD_MS&&pw.t_req-t0<PW_HOLD_MS+20;
    ms_t treq=pw.t_req; t=st_pw_loop(t,60,1,0); int gap1=st_txq_frames(fr,16)==1;
    t=st_pw_loop(t,30,1,0); nf=st_txq_frames(fr,16); int gap2=nf==2&&!strcmp(fr[1],PW_V_OFF)&&wq.sent_ms-treq>=PW_GAP_MS&&pw.state==PW_OFFING;
    st_check("HOLD: nothing before 1.5 s; at 1.5 s cmd8 (" PW_V_CMD8 ") goes out, and TURNING OFF… is shown (the power-off view, not the settings page)",before&&at);
    st_check("HOLD: cmd6 sub0 len2 = " PW_V_OFF " goes out >= 80 ms after cmd8 (stock: usleep(80000) between the two); TURNING OFF… until ACK",gap1&&gap2);
    st_ack_last(t+5); t=st_pw_loop(t,40,1,0);
    st_check("ACK (0x86 with echo) -> OFF; HOME on the next power-on",pw.state==PW_OFF&&pw.n_off==1&&wq.n==0&&view==V_OFF);
    { uint8_t a[15]; memcpy(a,pw.f8,15); a[2]=0x88; uint16_t c=crc16(a,13); a[13]=c&0xFF; a[14]=c>>8; rx_feed(a,15,t); }
    st_check("the controller's 0x88 receipt for cmd8 (echo) is counted for DIAGNOSE (stock does not wait for it — neither does merakiplus)",pw.n_ack8==1);
    int blon=!bl_is_off; t=st_pw_loop(t,treq+PW_BL_MS-t+10,0,0);
    st_check("the backlight is turned off 1.5 s after the power-off press (stock: lv_tick_elaps > 0x5DC) — not before",blon&&bl_is_off);
    nf=st_txq_frames(fr,16);
    st_check("EXACTLY two frames in the power-off sequence: cmd8 + cmd6 sub0 len2 — no stop frames, no keep-awake, nothing else",nf==2&&!strcmp(fr[0],PW_V_CMD8)&&!strcmp(fr[1],PW_V_OFF));
    /* ---- (5) keep-awake stays idle while powered off ---- */
    txq_len=0; t=st_pw_loop(t,65000,0,1); int quiet0=txq_len==0;
    reg[0]=0; t=st_pw_loop(t,65000,1,1); int quiet1=txq_len==0&&pw.state==PW_OFF;
    st_check("keep-awake paused while OFF: 2 x 65 s (controller silent / answering with word 0 = 0) -> 0 frames (no word 1 = 0, which would fight it)",quiet0&&quiet1);
    { ms_t u=st_pw_home(); u=st_pw_loop(u,65000,1,1); nf=st_txq_frames(fr,16); int nka=0; for(int k=0;k<nf;k++) nka+=!strcmp(fr[k],PW_V_KA);
      st_check("control: ON for 65 s -> keep-awake runs as before (2 x word 1 = 0)",nka==2&&nf==2); }
    /* ---- (6) tap on the powered-off screen ---- */
    t=st_pw_home(); pw_key(116,1,t); t=st_pw_loop(t,1600,1,1); pw_key(116,0,t); t=st_pw_loop(t,2000,0,1); txq_len=0;
    int off=pw.state==PW_OFF&&bl_is_off;
    ui_touch(TE_DOWN,240,240,t); int peek=!bl_is_off; ui_touch(TE_UP,240,240,t+60); t=st_pw_loop(t,200,0,1);
    int sw=view==V_OFF&&page==PG_HOME&&hit_test(240,372,t)==T_NONE&&hit_test(CX,GEAR_Y,t)==T_NONE&&txq_len==0;
    ui_touch(TE_DOWN,100,240,t); ui_touch(TE_MOVE,300,240,t+30); ui_touch(TE_UP,300,240,t+60); int noswipe=view==V_OFF&&page==PG_HOME&&tr.type==0;
    t=st_pw_loop(t,PW_PEEK_MS+100,0,1);
    st_check("tap on the powered-off screen: the light turns on (the OFF page for 8 s), the tap/swipe is swallowed (no page, no frames), the light turns off again",off&&peek&&sw&&noswipe&&bl_is_off&&txq_len==0);
    /* ---- (7) POWER ON: the button -> TURNING ON… -> the controller answers with word 0 = 1 -> PowerOn + awake -> HOME ---- */
    reg[0]=0; pw_key(116,1,t); int wk=pw.state==PW_WAKING&&!bl_is_off; t=st_pw_loop(t,800,1,1);
    int still=pw.state==PW_WAKING&&txq_len==0;                                     /* word 0 = 0 (old reply): not awake */
    reg[0]=1; t=st_pw_loop(t,PW_SEEN_MS-100,1,0); int wait=pw.state==PW_WAKING;
    t=st_pw_loop(t,200,1,1); t=st_pw_loop(t,200,1,1); pw_key(116,0,t); nf=st_txq_frames(fr,16);
    st_check("POWER ON: the button while OFF -> light on + TURNING ON… (no frames: the controller is asleep and cannot receive)",wk&&still);
    st_check("POWER ON: only once the controller has answered with word 0 = 1 for 0.6 s -> PowerOn " PW_V_ON " + awake " PW_V_KA " (as at startup) -> ON, HOME, green toast",
             wait&&nf==2&&!strcmp(fr[0],PW_V_ON)&&!strcmp(fr[1],PW_V_KA)&&pw.state==PW_ON&&view==V_MAIN&&page==PG_HOME&&!strcmp(toast.msg,TR(TX_T_PW_ON))&&pw.n_on==1&&!bl_is_off);
    txq_len=0; t=st_pw_loop(t,31000,1,1); nf=st_txq_frames(fr,16);
    st_check("after POWER ON keep-awake runs again (word 1 = 0 after 30 s)",nf==1&&!strcmp(fr[0],PW_V_KA));
    /* ---- (8) POWER ON without a reply: back to OFF after 10 s ---- */
    t=st_pw_home(); pw_key(116,1,t); t=st_pw_loop(t,1600,1,1); pw_key(116,0,t); t=st_pw_loop(t,2000,0,1); txq_len=0;
    pw_key(116,1,t); t=st_pw_loop(t,300,0,1); pw_key(116,0,t); t=st_pw_loop(t,PW_WAKE_MS,0,1);
    st_check("POWER ON, but the controller does not answer (button released too early): after 10 s back to OFF, light off, 0 frames",pw.state==PW_OFF&&pw.n_wto==1&&bl_is_off&&txq_len==0);
    /* ---- (9) the controller wakes up by itself (alarm / button merakiplus did not see) ---- */
    reg[0]=1; t=st_pw_loop(t,PW_SEEN_MS+100,1,1); nf=st_txq_frames(fr,16);
    st_check("OFF, and the controller answers by itself with word 0 = 1 -> ON + PowerOn/awake (no eternal OFF screen)",pw.state==PW_ON&&pw.n_auto==1&&nf>=1&&!strcmp(fr[0],PW_V_ON)&&view==V_MAIN);
    /* ---- (10) blocked as in stock: brew / steam / grind; hot water does NOT block and is not stopped ---- */
    { static const struct { int w; uint16_t v, ws; const char*nm; } B[]={{22,1,0x1307,"brew (word 22, work_sta 1307)"},{24,1,0x4121,"steam (word 24, work_sta 4121)"},{6,1,0x0141,"grind (word 6)"},{0,0,0x1307,"bit 12 alone (physical brew switch)"}};
      int ok=1;
      for(unsigned q=0;q<sizeof B/sizeof B[0];q++){
          t=st_pw_home(); if(B[q].w) reg[B[q].w]=B[q].v; reg[33]=B[q].ws; pw_key(116,1,t); t=st_pw_loop(t,4000,1,1);
          int blk=txq_len==0&&pw.state==PW_ON&&pw.n_blk==1&&!strcmp(toast.msg,TR(TX_T_PW_BUSY))&&toast.col==C_WARN;
          if(B[q].w) reg[B[q].w]=0; reg[33]=0x0101; t=st_pw_loop(t,1400,1,1); int notyet=txq_len==0;
          t=st_pw_loop(t,300,1,1); nf=st_txq_frames(fr,16); pw_key(116,0,t);
          printf("       %-36s blocked %s · after the activity + full hold: %s\n",B[q].nm,blk?"yes":"NO",nf>=1&&!strcmp(fr[0],PW_V_CMD8)?"off":"-");
          if(!(blk&&notyet&&nf>=1&&!strcmp(fr[0],PW_V_CMD8))) ok=0; }
      st_check("BLOCKED during brew / steam / grind (stock isExtractionOrSteamSwitchOn 0x1481E0: work_sta bit 12/14, words 22/24/6): nothing is sent, toast 'STOP THE ACTIVITY FIRST'; once it has ended, a NEW full hold powers off",ok); }
    t=st_pw_home(); reg[16]=1; reg[33]=0x030B; pw_key(116,1,t); t=st_pw_loop(t,1600,1,1); pw_key(116,0,t); t=st_pw_loop(t,300,1,1); nf=st_txq_frames(fr,16);
    { int stop=0; for(int k=0;k<nf;k++) if(strstr(fr[k],"aa 55 06 10")||strstr(fr[k],"aa 55 06 16")||strstr(fr[k],"aa 55 06 06")) stop=1;
      st_check("hot water does NOT block (not in stock either) and is not stopped by merakiplus: only cmd8 + sub0 len2 — the MCU's sleep task resets work_sta + outputs (0x2F52/0x2F0C)",
               nf==2&&!strcmp(fr[0],PW_V_CMD8)&&!strcmp(fr[1],PW_V_OFF)&&!stop&&pw.state==PW_OFF); }
    /* ---- (11) no connection / word 0 = 0 / read-only: nothing is sent ---- */
    t=st_pw_home(); pw_key(116,1,t); t=st_pw_loop(t,1600,0,1); pw_key(116,0,t); int nl=txq_len==0&&pw.state==PW_ON&&!strcmp(toast.msg,TR(TX_T_PW_NOLINK));
    t=st_pw_home(); reg[0]=0; pw_key(116,1,t); t=st_pw_loop(t,1600,1,1); pw_key(116,0,t); int z0=txq_len==0&&pw.state==PW_ON;
    t=st_pw_home(); ro_mode=1; pw_key(116,1,t); t=st_pw_loop(t,1600,1,1); pw_key(116,0,t); int ro=txq_len==0&&pw.state==PW_ON&&!strcmp(toast.msg,TR(TX_G_RO)); ro_mode=0;
    st_check("no connection -> toast, nothing is sent (the button may just be waking a sleeping controller); word 0 = 0 -> nothing (like stock); READ-ONLY -> nothing",nl&&z0&&ro);
    /* ---- (12) no ACK -> back to ON with an error toast ---- */
    t=st_pw_home(); pw_key(116,1,t); t=st_pw_loop(t,1600,1,0); pw_key(116,0,t); t=st_pw_loop(t,600,1,0); nf=st_txq_frames(fr,16);
    st_check("no ACK (not on the retransmit either): back to ON/HOME, light never turned off, toast 'POWER OFF: NO REPLY'",
             pw.state==PW_ON&&pw.n_fail==1&&!bl_is_off&&view==V_MAIN&&!strcmp(toast.msg,TR(TX_T_PW_NOACK))&&toast.col==C_ERR&&nf==3&&!strcmp(fr[1],PW_V_OFF)&&!strcmp(fr[2],PW_V_OFF));
    /* ---- (13) open detail with brightness preview: reverted before powering off ---- */
    t=st_pw_home(); page=PG_SYS; sec_open=SEC_DISPLAY; static_dirty=1; view=V_DETAIL; sel=S_BL; edit_val=SET[S_BL].val+1; pw_key(116,1,t); t=st_pw_loop(t,1600,1,1); pw_key(116,0,t); t=st_pw_loop(t,300,1,1);
    st_check("power off in the middle of an open detail: the detail is closed without SAVE (sel -1), TURNING OFF…/OFF is shown",view==V_OFF&&sel<0&&pw.state==PW_OFF&&cfg_writes>=0);
    /* ---- (14) texts and layout ---- */
    { int ok=1; static const short T[]={TX_PW_OFFING,TX_PW_OFF,TX_PW_WAKING,TX_PW_SUB_OFFING,TX_PW_SUB_OFF,TX_PW_SUB_WAKING,TX_PW_NOTE_OFF};
      for(int lg=0;lg<NLANG;lg++) for(unsigned k=0;k<sizeof T/sizeof T[0];k++){ float w=text_wf(STR[lg][T[k]],k<3?F_M:F_XS); if(w>340){ ok=0; printf("     [%s] %s: %.0f px\n",LANG_CODE[lg],STR_ID[T[k]],w); } }
      st_check("TURNING OFF / OFF / TURNING ON + sublines fit in the field (<= 340 px) in EN and DA",ok); }
    { int ok=DG_RAW+28+4*DG_P<=DG_PWR&&DG_PWR+28+3*DG_P<=DG_ABOUT&&DG_ABOUT+76+24<DG_BOTTOM;
      char a[28],c[28],d[28],e[40],f[80]; uint32_t col; diag_pw_text(a,&col,c,d,e,f,sizeof f,t);
      printf("       DIAGNOSE POWER: y %d (RAW COUNTERS %d, ABOUT %d) · '%s' · word 0 '%s' · '%s' · '%s' · '%s'\n",DG_PWR,DG_RAW,DG_ABOUT,a,c,d,e,f);
      st_check("DIAGNOSE: POWER BUTTON block between RAW COUNTERS and ABOUT (state, word 0, presses · off · on, latest key, node)",ok&&!strcmp(a,TR(TX_DV_PW_OFF))&&strstr(f,KBD_NAME)!=NULL); }
    t=st_pw_home(); pw_key(116,1,t); t=st_pw_loop(t,1600,1,1); pw_key(116,0,t); t=st_pw_loop(t,3000,0,1); static_dirty=1; force_all=1; ui_frame(t);
    { int black=1; static const int P[][2]={{240,40},{240,440},{40,240},{440,240},{240,370}}; for(unsigned k=0;k<5;k++) if((canvas[P[k][1]*W+P[k][0]]&0xFFFFFF)!=0) black=0;
      st_check("the OFF page is black outside the symbol and text (no arcs, no HOME leftovers)",black&&!garc.vis&&!parc.vis); }
    sim_reset(); sim_tick=NULL;
}
/* ============================ v6.13: manual STOP for grind / brew / water ============================ */
#define ST_V_GRIND "aa 55 06 06 01 00 00 c4 47"
#define ST_V_BREW  "aa 55 06 16 01 00 00 c0 87"
#define ST_V_WATER "aa 55 06 10 01 00 00 c0 0f"
#define ST_V_WMAN  "aa 55 06 13 01 00 00 c0 4b"
#define ST_V_STEAM "aa 55 06 18 08 00 00 8a 02 00 00 00 00 00 00 00 00 00 00 00 00 0c f7"
static int st_tx_is(const char*hex){ return hexeq(last_tx,last_tx_len,hex); }
/* tap (down + up) at (x,y) like a finger: activation happens on release, as on the screen */
static ms_t st_tap(int x,int y,ms_t t){ ui_touch(TE_DOWN,x,y,t); ui_frame(t+=33); ui_touch(TE_UP,x,y,t+=40); ui_frame(t+=33); return t; }
/* the touch area's width along y and height along x for a given target (measured via hit_test, like v6.11's UX check) */
static void st_hit_size(int tgt,int x,int y,ms_t t,int*w,int*h){ *w=*h=0; for(int i=0;i<W;i++) if(hit_test(i,y,t)==tgt) (*w)++; for(int i=0;i<H;i++) if(hit_test(x,i,t)==tgt) (*h)++; }
static void st_stop(void){
    printf("== selftest: v6.13 manual STOP for grind / brew / water (stop words reversed from the MCU firmware + the stock UI's own stop calls) ==\n");
    ms_t t; int sk[2], sn;
    char stops[40]; snprintf(stops,sizeof stops,"%s",STR[ui_lang][TX_H_AUTOSTOP_G]); { char*q=strstr(stops,"%s"); if(q) *q=0; }   /* "STOPS AT " / "STOPPER VED " */
    /* ---- (1) frames byte for byte ---- */
    t=st_home();
    act_push_stop(ACT_GRIND); st_check("GRIND stop = cmd6 sub6 len1, word 6 = 0: " ST_V_GRIND " (MCU grind-ctl 0x19C6 runs while word 6 != 0; stock onClick 0x10928C usart_send(6,6,1))",wq.n==1&&q_hex(0,ST_V_GRIND)); wq.n=0;
    act_push_stop(ACT_BREW);  st_check("BREW stop  = cmd6 sub22 len1, word 22 = 0: " ST_V_BREW " (MCU brew-ctl 0x1E9C: running = word 22 != 0 on change; auto-stop 0x1F92 writes the same)",wq.n==1&&q_hex(0,ST_V_BREW)); wq.n=0;
    reg_seen[19]=1; reg[19]=0;
    act_push_stop(ACT_WATER); st_check("WATER stop = cmd6 sub16 len1, word 16 = 0: " ST_V_WATER " (MCU hotwater-ctl 0x1B8C: active = word 19 | word 16; stock hotWaterStartStop 0x11468C usart_send(6,16,1))",wq.n==1&&q_hex(0,ST_V_WATER)); wq.n=0;
    reg[19]=1; act_push_stop(ACT_WATER);
    st_check("WATER stop with word 19 != 0 (manual mode, stock): + sub19 len1 word 19 = 0: " ST_V_WMAN " (stock 0x114720 usart_send(6,19,1)) — both in the same queue round",wq.n==2&&q_hex(0,ST_V_WATER)&&q_hex(1,ST_V_WMAN)); wq.n=0; reg[19]=0;
    act_push_stop(ACT_STEAM); st_check("STEAM stop = unchanged (sub24 len8, word 24 = 0)",wq.n==1&&q_hex(0,ST_V_STEAM)); wq.n=0;
    /* ---- (2) policy: a stop frame can never start anything and is never rejected ---- */
    { int ok=1; for(int k=ACT_GRIND;k<=ACT_STEAM;k++){ wq.n=0; reg_seen[19]=1; reg[19]=1; if(!act_push_stop(k)){ ok=0; continue; }
          for(int q=0;q<wq.n;q++){ const WFrame*f=&wq.q[q]; if(!frame_trig_audit(f,0)||frame_policy(f->sub,f->w,f->len,I_NONE)) ok=0;
              for(int i=0;i<f->len;i++) if(f->w[i]!=0&&!(f->sub==24&&i==1)) ok=0;                       /* zeros only (steam: the target word) */
              if(f->sub!=24&&(frame_covers(f,26)||frame_covers(f,27)||frame_covers(f,30))) ok=0; } }   /* the steam block covers 24..31 — there they are zeros (checked above) */
      wq.n=0; reg[19]=0;
      st_check("policy: all stop frames are zeros (steam: + the target word), pass frame_policy WITHOUT intent and cover words 26/27/30 only in the steam block (as zeros)",ok); }
    st_check("policy: the same words with value 1 WITHOUT intent are still rejected (sub6=1, sub22=1, sub16=1, v6.22: sub19=1 — all WJ_TRIG) — the STOP path cannot start anything",
             rej(6,W1(1),1,0,WJ_TRIG)&&rej(22,W1(1),1,0,WJ_TRIG)&&rej(16,W1(1),1,0,WJ_TRIG)&&rej(19,W1(1),1,0,WJ_TRIG));
    /* ---- (3) visible per phase + touch area >= 48 px + subline "· OR STOP" ---- */
    { static const struct { uint16_t ws; int kind; const char*nm; } P[]={{0x0141,ACT_GRIND,"grind 0141"},{0x1107,ACT_BREW,"pre-inf. 1107"},{0x1307,ACT_BREW,"extraction 1307"},{0x030B,ACT_WATER,"water 030B"}};
      int ok=1, okw=1, oks=1;
      for(unsigned q=0;q<sizeof P/sizeof P[0];q++){
          t=st_home(); st_s0(P[q].ws,0,0x000C,0,300,t+=100); ui_frame(t); sn=stop_btns(t,sk);
          int br=P[q].kind==ACT_BREW, sx=br?160:240, w,h; st_hit_size(T_STOP,sx,372,t,&w,&h);   /* v6.20: during a brew STOP BREW is the left slot, HOLD · STEAM the right */
          printf("       %-17s STOP button: %s · touch area %d x %d px\n",P[q].nm,sn?TR(STOP_BTN[sk[0]]):"(none)",w,h);
          if(!(sn==1&&sk[0]==P[q].kind&&hit_test(sx,372,t)==T_STOP&&(hit_test(320,372,t)==T_STEAMH)==br&&hit_test(ACT_X[0],ACT_Y,t)!=T_ACT0&&hit_test(ACT_X[2],ACT_Y,t)!=T_ACT2)) ok=0;
          if(w<48||h<48) okw=0;
          if(strncmp(hm.sub,stops,strlen(stops))||st_has(hm.sub,"\xC2\xB7")||sub_font(&hm)!=F_S) oks=0; }
      st_check("STOP is shown during GRIND, PRE-INF., EXTRACTION and WATER (one STOP button; no start icons; v6.20: during a brew STOP BREW on the left, HOLD · STEAM on the right)",ok);
      st_check("touch: the STOP button's touch area is >= 48 x 48 px (232 x 60; brew 160 x 60)",okw);
      st_check("the subline is the auto-stop expectation and nothing else ('STOPS AT 36.0 G' / 'STOPPER VED 36.0 G' in F_S — v6.13-v6.16: 'AUTO-STOP AT 36.0 G · OR STOP' in F_XS; the STOP button is right below)",oks); }
    t=st_home(); st_s0(0x0101,0,0x000C,0,300,t+=100); ui_frame(t); sn=stop_btns(t,sk);
    st_check("idle (0101): no STOP button — the start icons are there",sn==0&&hit_test(ACT_X[0],ACT_Y,t)==T_ACT0&&hit_test(240,372,t)!=T_STOP);
    st_s0(0x4121,1,0x000C,0,300,t+=100); ui_frame(t); sn=stop_btns(t,sk);
    st_check("latched steam alone: one button = STOP STEAM (unchanged behaviour, same touch area as the other STOP buttons)",sn==1&&sk[0]==ACT_STEAM&&hit_test(240,372,t)==T_STOP&&hit_test(150,372,t)==T_STOP&&hit_test(330,372,t)==T_STOP&&hit_test(360,372,t)==T_NONE);
    /* ---- (4) WATER all the way: tap -> STOPPING WATER… -> frame -> retransmit -> ACK -> bits cleared -> WATER STOPPED ---- */
    t=st_home(); reg[16]=1; reg[17]=400; reg_seen[19]=1; reg[19]=0;
    st_s0(0x030B,0,0x000C,0,300,t+=100); ui_frame(t);
    t=st_tap(240,372,t);
    int q1=wq.n==1&&q_hex(0,ST_V_WATER)&&!strcmp(toast.msg,TR(TX_T_STOP_WATER))&&toast.col==C_TXT&&stp.active&&stp.kind==ACT_WATER;
    wq_step(t); int tx1=st_tx_is(ST_V_WATER); int l0=txq_len;
    wq_step(t+100); int noretx=txq_len==l0; wq_step(t+160); int retx=txq_len==l0+9&&wq.n_retx==1&&st_tx_is(ST_V_WATER);
    st_ack_last(t+190); ui_frame(t+=200);
    int acked=wq.n==0&&wq.result==WR_OK&&reg[16]==0&&stp.active&&!strcmp(toast.msg,TR(TX_T_STOP_WATER));   /* an ACK alone is not a confirmation: the bits are still set */
    st_check("WATER: tap STOP -> toast 'STOPPING WATER…' / 'STOPPER VAND…' immediately, and the stop frame is queued",q1);
    st_check("WATER: the frame is sent byte for byte; no ACK within 150 ms -> exactly one retransmit (same bytes); ACK with echo -> queue free",tx1&&noretx&&retx&&acked);
    st_s0(0x0101,0,0x000C,0,300,t+=150); ui_frame(t); sn=stop_btns(t,sk);
    st_check("WATER: the MCU clears bits 1+3 (030B -> 0101) -> toast 'WATER STOPPED' / 'VAND STOPPET' (green), DONE view, STOP button gone",
             !strcmp(toast.msg,TR(TX_T_STOPPED_WATER))&&toast.col==C_OK&&!stp.active&&stp.n_ok==1&&stp.n_fail==0&&act.ph==PH_DONE&&act.last_ph==PH_WATER&&sn==0);
    /* ---- (5) BREW and GRIND the same way ---- */
    { static const struct { uint16_t ws; int kind; const char*v; short tx_s, tx_d; } P[]={{0x1307,ACT_BREW,ST_V_BREW,TX_T_STOP_BREW,TX_T_STOPPED_BREW},{0x0141,ACT_GRIND,ST_V_GRIND,TX_T_STOP_GRIND,TX_T_STOPPED_GRIND}};
      for(unsigned q=0;q<2;q++){
          t=st_home(); if(P[q].kind==ACT_BREW) reg[22]=1; else reg[6]=1;
          st_s0(P[q].ws,0,0x000C,0,300,t+=100); ui_frame(t); t=st_tap(P[q].kind==ACT_BREW?160:240,372,t);
          int a=wq.n==1&&q_hex(0,P[q].v)&&!strcmp(toast.msg,TR(P[q].tx_s)); wq_step(t); int b=st_tx_is(P[q].v); st_ack_last(t+25); ui_frame(t+=40);
          int c=wq.n==0&&wq.result==WR_OK&&(P[q].kind==ACT_BREW?reg[22]==0:reg[6]==0)&&stp.active;
          st_s0(0x0101,0,0x000C,0,300,t+=150); ui_frame(t);
          char nm[200]; snprintf(nm,sizeof nm,"%s: tap STOP -> '%s' -> %s sent + ACK -> bits cleared -> '%s'",P[q].kind==ACT_BREW?"BREW (1307)":"GRIND (0141)",TR(P[q].tx_s),P[q].kind==ACT_BREW?"sub22":"sub6",TR(P[q].tx_d));
          st_check(nm,a&&b&&c&&!strcmp(toast.msg,TR(P[q].tx_d))&&toast.col==C_OK&&!stp.active&&act.ph==PH_DONE); } }
    /* brew: the MCU's valve pulse after stop (BREW.md §4: 0105 for 0.21 s) is not a brew — no STOP button, no new phase */
    t=st_home(); reg[22]=1; st_s0(0x1307,0,0x000C,0,300,t+=100); ui_frame(t); t=st_tap(160,372,t); wq_step(t); st_ack_last(t+25);
    st_s0(0x0101,0,0x000C,0,300,t+=150); ui_frame(t); st_s33(0x0105,t+=100); ui_frame(t); sn=stop_btns(t,sk);
    st_check("BREW: the valve pulse after stop (work_sta 0105, only bit 2) gives no STOP button and no new phase",sn==0&&act.ph==PH_DONE&&stp.n_ok==1);
    /* ---- (6) failure: no ACK -> error toast, the STOP button stays (the bits are still set) ---- */
    t=st_home(); st_s0(0x030B,0,0x000C,0,300,t+=100); ui_frame(t); t=st_tap(240,372,t);
    wq_step(t); wq_step(t+160); wq_step(t+470); st_s33(0x030B,t+=480); ui_frame(t); sn=stop_btns(t,sk);
    st_check("no ACK (not on the retransmit either): toast 'STOP: NO REPLY · TRY AGAIN' (red); the STOP button stays, so you can try again",
             wq.result==WR_FAIL&&!strcmp(toast.msg,TR(TX_T_STOP_NOACK))&&toast.col==C_ERR&&!stp.active&&stp.n_fail==1&&sn==1&&sk[0]==ACT_WATER);
    t=st_tap(240,372,t); wq_step(t); st_ack_last(t+20); st_s0(0x0101,0,0x000C,0,300,t+=150); ui_frame(t);
    st_check("… a second tap works: new frame, ACK, bits cleared -> 'WATER STOPPED'",!strcmp(toast.msg,TR(TX_T_STOPPED_WATER))&&stp.n_ok==1&&stp.n_req==2);
    /* ---- (7) ACK, but the bits are not cleared (e.g. a brew from a physical switch: word 22 was 0) -> after 5 s 'STOP NOT CONFIRMED' ---- */
    t=st_home(); st_s0(0x1307,0,0x000C,0,300,t+=100); ui_frame(t); t=st_tap(160,372,t); wq_step(t); st_ack_last(t+20);
    { int mid=1; for(int q=0;q<40&&t-stp.t_req<STOP_CONFIRM_MS-250;q++){   /* at most 40 rounds (10 s), even if STOP was never requested */ st_s33(0x1307,t+=250); ui_frame(t); if(!stp.active) mid=0; }
      for(int q=0;q<3;q++){ st_s33(0x1307,t+=250); ui_frame(t); }
      st_check("ACK but the bits are not cleared within 5 s: toast 'STOP NOT CONFIRMED · TRY AGAIN' (yellow) — no false 'STOPPED'",
               mid&&!stp.active&&!strcmp(toast.msg,TR(TX_T_STOP_UNCONF))&&toast.col==C_WARN&&stp.n_ok==0&&stp.n_fail==1); }
    /* ---- (8) double tap while the frame is in the queue: no extra frame ---- */
    t=st_home(); st_s0(0x030B,0,0x000C,0,300,t+=100); ui_frame(t); t=st_tap(240,372,t); wq_step(t); t=st_tap(240,372,t+30);
    st_check("double tap: a second tap while the stop frame waits for its ACK does not queue another frame (only the toast is repeated)",wq.n==1&&stp.n_req==1&&!strcmp(toast.msg,TR(TX_T_STOP_WATER)));
    /* ---- (9) two buttons: brew + latched steam ---- */
    t=st_home(); reg[22]=1; st_s0(0x1307,0,0x000C,0,300,t+=100); ui_frame(t); st_s0(0x5327,1,0x000C,0,310,t+=100); ui_frame(t); sn=stop_btns(t,sk);
    { int wl,hl,wr,hr; st_hit_size(T_STOP,160,372,t,&wl,&hl); st_hit_size(T_STOPS,320,372,t,&wr,&hr);
      printf("       two buttons: %s %d x %d px · %s %d x %d px\n",sn>0?TR(STOP_BTN[sk[0]]):"-",wl,hl,sn>1?TR(STOP_BTN[sk[1]]):"-",wr,hr);
      st_check("brew + latched steam (5327, word 24 = 1): TWO buttons — left STOP BREW, right STOP STEAM — each >= 48 x 48 px",sn==2&&sk[0]==ACT_BREW&&sk[1]==ACT_STEAM&&wl>=48&&hl>=48&&wr>=48&&hr>=48); }
    t=st_tap(160,372,t); int lft=wq.n==1&&q_hex(0,ST_V_BREW)&&!strcmp(toast.msg,TR(TX_T_STOP_BREW)); wq_step(t); st_ack_last(t+20); ui_frame(t+=30);
    t=st_tap(320,372,t); int rgt=wq.n==1&&q_hex(0,ST_V_STEAM)&&!strcmp(toast.msg,TR(TX_T_STOP_SENT));
    st_check("the left button sends the brew stop (sub22), the right the steam stop (sub24) — a frame each",lft&&rgt);
    /* ---- (10) AUTO-STOP is unchanged: the MCU stops by itself -> merakiplus sends nothing and shows no stop toast ---- */
    { int ok=1; static const uint16_t ws[]={0x030B,0x1307,0x0141};
      for(int q=0;q<3;q++){ t=st_home(); st_s0(ws[q],0,0x000C,0,300,t+=100); ui_frame(t); for(int k=0;k<5;k++){ st_s33(ws[q],t+=200); ui_frame(t); wq_step(t); }
          st_s0(0x0101,0,0x000C,0,300,t+=200); ui_frame(t); wq_step(t); if(!(act.ph==PH_DONE&&wq.n==0&&txq_len==0&&wq.n_ok==0&&stp.n_req==0&&toast.dur==0)) ok=0; }
      st_check("AUTO-STOP unchanged (water / brew / grind): the MCU clears the bits itself -> DONE; merakiplus has not sent a single frame and shows no stop toast",ok); }
    /* ---- (11) read-only mode ---- */
    t=st_home(); st_s0(0x030B,0,0x000C,0,300,t+=100); ui_frame(t); ro_mode=1; t=st_tap(240,372,t); ro_mode=0;
    st_check("MERAKIPLUS_READONLY=1: STOP sends nothing, the toast says why (read-only)",wq.n==0&&stp.n_req==0&&!strcmp(toast.msg,TR(TX_G_RO)));
    sim_reset();
}
/* ============ v6.20: steam during a brew (docs/concurrent-brew-steam.md §9; hardware-verified §11) ============ */
static void st_v620(void){
    printf("== selftest: v6.20 steam during a brew (the relaxed guard, HOLD · STEAM, independent stops) ==\n");
    ms_t t; int sk[2], sn, ok;
    /* (1) the guard */
    t=st_home(); reg[22]=1; st_s0(0x1107,0,0x000C,0,300,t+=100); ui_frame(t); int gp=act_guard(ACT_STEAM,t)==NULL;
    st_s0(0x1307,0,0x000C,0,300,t+=100); ui_frame(t); int ge=act_guard(ACT_STEAM,t)==NULL;
    int go=act_guard(ACT_BREW,t)==TR(TX_G_BUSY)&&act_guard(ACT_GRIND,t)==TR(TX_G_BUSY)&&act_guard(ACT_WATER,t)==TR(TX_G_BUSY);
    st_check("guard: a steam start is allowed while a brew (pre-infusion 1107 or extraction 1307) is the only activity; grind / brew / water stay blocked",gp&&ge&&go);
    { static const struct { uint16_t ws,w24; const char*nm; } B[]={{0x0141,0,"grind 0141"},{0x030B,0,"water 030B"},{0x4121,1,"latched steam 4121"},{0x4121,0,"wand 4121"},
          {0x5327,0,"brew + wand flush 5327"},{0x5327,1,"brew + latched steam 5327"},{0x1307,1,"brew, word 24 = 1 before the bits"},{0x134F,0,"brew + grind + water bits 134F"},{0x130F,0,"brew + hot-water bit 3 130F"}};
      ok=1; for(unsigned q=0;q<sizeof B/sizeof B[0];q++){ t=st_home(); st_s0(B[q].ws,B[q].w24,0x000C,0,300,t+=100); ui_frame(t);
          if(act_guard(ACT_STEAM,t)!=TR(TX_G_BUSY)){ ok=0; printf("     steam not blocked: %s\n",B[q].nm); } }
      st_check("guard: steam stays blocked (ACTIVITY IN PROGRESS) during grind, hot water, steam (latched or wand), brew + steam, and while word 24 is already set",ok); }
    t=st_home(); reg[35]=1; st_s0(0x1307,0,0x000C,0,300,t+=100); ui_frame(t); int er=act_guard(ACT_STEAM,t)==TR(TX_G_ERROR);
    sim_now=t; int f=st_hold(320,372,700); t=sim_now; int er2=f&&wq.n==0&&act.pending==ACT_NONE&&!strcmp(toast.msg,TR(TX_G_ERROR)); reg[35]=0;
    t=st_home(); st_s0(0x1307,0,0x000C,0,300,t+=100); ui_frame(t); uint16_t w1[1]={(uint16_t)sv(S_BREWW)}; wq_push(23,w1,1); int wt=act_guard(ACT_STEAM,t)==TR(TX_G_WAIT); memset(&wq,0,sizeof wq);
    st_check("guard: brew + an active error (word 35) -> ERROR (HOLD · STEAM sends nothing, the toast says why); a write in transit -> ONE MOMENT",er&&er2&&wt);
    /* (2) HOME during a brew: STOP BREW left, HOLD · STEAM right; a tap explains, a hold starts */
    t=st_home(); reg[22]=1; st_s0(0x1307,0,0x000C,0,300,t+=100); ui_frame(t); sn=stop_btns(t,sk); ms_t tb0=act.t_act0;
    int lay=sn==1&&sk[0]==ACT_BREW&&steam_hold_shown(t)&&hit_test(160,372,t)==T_STOP&&hit_test(320,372,t)==T_STEAMH&&hit_test(ACT_X[3],ACT_Y,t)!=T_ACT3;
    t=st_tap(320,372,t); int tp=wq.n==0&&!strcmp(toast.msg,TR(TX_T_HOLD_HINT));
    st_check("HOME during a brew: STOP BREW on the left, HOLD · STEAM on the right (each 160 x 60); a TAP sends nothing and says 'KEEP HOLDING TO START'",lay&&tp);
    toast.dur=0; sim_now=t; f=st_hold(320,372,700); t=sim_now;
    int hs=f&&act.pending==ACT_STEAM&&wq.n==1&&wq.q[0].sub==24&&wq.q[0].len==8&&wq.q[0].w[0]==1&&wq.q[0].w[1]==(uint16_t)sv(S_STEAMT)&&page==PG_HOME;
    st_check("HOLD · STEAM (650 ms) during the brew: the stock steam start (cmd6 sub24 len8, word 24 = 1, word 25 = target) — the same frame as HOME's steam icon",hs);
    wq_step(t); st_ack_last(t+20); ui_frame(t+=30);
    st_s0(0x5327,1,0x000C,0,310,t+=200); ui_frame(t); sn=stop_btns(t,sk);
    int both=act.pending==ACT_NONE&&stm.kind==SK_AUTO&&act.ph==PH_EXT&&hm.conc&&hm.badge==TR(TX_PH_STEAM_AUTO)&&sn==2&&sk[0]==ACT_BREW&&sk[1]==ACT_STEAM&&hit_test(320,372,t)==T_STOPS&&!steam_hold_shown(t);
    int quiet=1; for(int q=0;q<70;q++){ st_s0(0x5327,1,0x000C,0,320,t+=250); ui_frame(t); if(toast.dur&&!strcmp(toast.msg,TR(TX_T_NOSTART))) quiet=0; }
    st_check("the MCU latches steam on top of the brew (5327, word 24 = 1): the start is confirmed (no 'NO START' toast after 15 s), EXTRACTION + STEAM·AUTO, and the slot is STOP STEAM next to STOP BREW",both&&quiet&&wq.n==0);
    /* (3) STOP STEAM while the brew continues */
    long n0=stp.n_ok; t=st_tap(320,372,t); int ss=wq.n==1&&q_hex(0,ST_V_STEAM)&&!strcmp(toast.msg,TR(TX_T_STOP_SENT)); wq_step(t); st_ack_last(t+20); ui_frame(t+=30);
    st_s0(0x1307,0,0x000C,0,320,t+=200); ui_frame(t); sn=stop_btns(t,sk);
    st_check("STOP STEAM mid-brew: word 24 = 0 (sub24 len8), steam stopped and confirmed; the brew keeps running (EXTRACTION, same brew timer) and HOLD · STEAM is back",
             ss&&stp.n_ok==n0+1&&!strcmp(toast.msg,TR(TX_T_STOPPED_STEAM))&&act.ph==PH_EXT&&act.t_act0==tb0&&sn==1&&sk[0]==ACT_BREW&&steam_hold_shown(t)&&hit_test(320,372,t)==T_STEAMH);
    /* (4) STOP BREW while the steam continues */
    st_s0(0x5327,1,0x000C,0,330,t+=200); ui_frame(t); n0=stp.n_ok; t=st_tap(160,372,t); int sb=wq.n==1&&q_hex(0,ST_V_BREW)&&!strcmp(toast.msg,TR(TX_T_STOP_BREW)); wq_step(t); st_ack_last(t+20); ui_frame(t+=30);
    st_s0(0x4121,1,0x000C,0,340,t+=200); ui_frame(t); sn=stop_btns(t,sk);
    st_check("STOP BREW while steaming: only word 22 = 0 is sent; the brew stops, the latched steam runs on (STEAM·AUTO, one STOP STEAM, no HOLD · STEAM)",
             sb&&stp.n_ok==n0+1&&!strcmp(toast.msg,TR(TX_T_STOPPED_BREW))&&wq.n==0&&stm.kind==SK_AUTO&&sn==1&&sk[0]==ACT_STEAM&&!steam_hold_shown(t)&&act.ph==PH_STEAM);
    /* (5) the label fits (both languages) */
    ok=1; for(int lg=0;lg<NLANG;lg++){ int kl=ui_lang; lang_set(lg); LAY(text_wf(TR(TX_B_HOLD_STEAM),F_S),STOP_PILL2_W-28,"HOLD · STEAM (pill 152, F_S)",TR(TX_B_HOLD_STEAM)); lang_set(kl); }
    st_check("HOLD · STEAM / HOLD · DAMP fits its 152 px pill in both languages",ok);
    { ms_t u=st_home(); st_s0(0x5327,0,0x000D,1,300,u+=100); ui_frame(u); int s2[2], n2=stop_btns(u,s2);
      st_check("brew + wand flush (5327, word 24 = 0): no HOLD · STEAM (steam is already flowing) — STOP BREW alone in the middle",
               n2==1&&s2[0]==ACT_BREW&&!steam_hold_shown(u)&&hit_test(240,372,u)==T_STOP&&hit_test(320,372,u)==T_STOP); }
    sim_reset();
}
/* ============================ v6.21: OTA phase 1b (launcher environment, healthy marker, SYSTEM line, vendor notice) ============================ */
static void st_v621(const char*dir){
    printf("== selftest: v6.21 OTA phase 1b (release directory, healthy marker, SYSTEM line, vendor notice, idle gate) ==\n");
    char root[360],ksr[sizeof sysroot],p[600],b[400]; strcpy(ksr,sysroot); ms_t klr=last_rx_ms; snprintf(root,sizeof root,"%s/root",dir); snprintf(sysroot,sizeof sysroot,"%s",root);
    mock_reset(root); mk_p(root,"/new_customer/merakiplus/ota"); mk_p(root,"/new_customer/merakiplus/rel/6.21-y");
    st_check("release exe = slot B: rel/<name>/demo_v9 (also '(deleted)'); not a stage dir, a nested path, another name or the install dir",
             ota_rel_exe("/new_customer/merakiplus/rel/6.21-42cd7b9d/demo_v9")&&ota_rel_exe("/new_customer/merakiplus/rel/6.21-x/demo_v9 (deleted)")&&
             !ota_rel_exe("/new_customer/merakiplus/rel/.stage-6.21/demo_v9")&&!ota_rel_exe("/new_customer/merakiplus/rel/a/b/demo_v9")&&
             !ota_rel_exe("/new_customer/merakiplus/rel/6.21/demo_v9x")&&!ota_rel_exe("/new_customer/merakiplus/rel//demo_v9")&&!ota_rel_exe("/new_customer/merakiplus/merakiplus"));
    mock_slot_b(root,1,MB_OURS); mock_self(root,"/new_customer/merakiplus/rel/6.21-x/demo_v9"); boot_refresh();
    st_check("a UI started by the launcher counts as slot B (DIAGNOSTICS, BOOT UI)",bs.self_slot==SLOT_B);
    /* where the UI's own files are */
    setenv("MERAKIPLUS_REL","/new_customer/merakiplus/rel/6.21-test",1); setenv("MERAKIPLUS_RELNAME","6.21-test",1); setenv("MERAKIPLUS_TRIAL","1",1);
    ota_init(sim_now);
    st_check("launcher environment: release dir + name + trial; the sound helper and the WAVs come from the release dir",
             !strcmp(ota.dir,"/new_customer/merakiplus/rel/6.21-test")&&!strcmp(ota.name,"6.21-test")&&ota.trial&&!ota.fallback&&
             !strcmp(SND_HELPER,"/new_customer/merakiplus/rel/6.21-test/merakiplus_snd")&&!strcmp(snd_path_of(SE_READY_S),"/new_customer/merakiplus/rel/6.21-test/audio/ready_steam.wav")&&
             sizeof snd.last_file>strlen(snd_path_of(SE_READY_E))+40);
    unsetenv("MERAKIPLUS_REL"); unsetenv("MERAKIPLUS_RELNAME"); unsetenv("MERAKIPLUS_TRIAL");
    snprintf(p,sizeof p,"%s/new_customer/merakiplus/rel/6.21-y/demo_v9",root); mock_self(root,p); ota_init(sim_now); int legacy=!strcmp(ota.dir,OTA_DIR)&&!ota.name[0]&&!ota.trial;
    snprintf(p,sizeof p,"%s/new_customer/merakiplus/rel/6.21-y/merakiplus_snd",root); put_bytes(p,"#!",2); chmod(p,0755); ota_init(sim_now);
    snprintf(p,sizeof p,"%s/new_customer/merakiplus/rel/6.21-y",root);
    st_check("without the launcher: the own exe dir when it holds the sound helper, else the v6.18..v6.20 install dir",legacy&&!strcmp(ota.dir,p));
    /* the healthy marker: >= 60 s running AND >= 30 s of continuous controller link */
    setenv("MERAKIPLUS_RELNAME","6.21-test",1); setenv("MERAKIPLUS_TRIAL","1",1); ms_t t0=sim_now; ota_init(t0); unsetenv("MERAKIPLUS_RELNAME"); unsetenv("MERAKIPLUS_TRIAL");
    char hm[440],hp[440]; snprintf(hm,sizeof hm,"%s" OTA_HMARK,root); snprintf(hp,sizeof hp,"%s" OTA_HEALTHY,root); unlink(hm); unlink(hp);
    ms_t at=-1; for(ms_t t=t0;t<=t0+70000;t+=100){ last_rx_ms=t; ota_step(t); if(at<0&&ota.marked) at=t; }
    snprintf(b,sizeof b,"pid=%d\nrel=6.21-test\n",(int)getpid());
    st_check("healthy after exactly 60 s with a live link — marker in /tmp with boot_id + PID + release, and on flash (trial)",
             at==t0+60000&&file_has(hm,"boot=0f6c1b1e-52a7-4f0b-9a3d-7e1c5a9b2d44\n")&&file_has(hm,b)&&file_has(hm,"state=healthy")&&file_has(hp,b));
    ota_init(t0); unlink(hm); unlink(hp); at=-1;
    for(ms_t t=t0;t<=t0+90000;t+=100){ if(t<t0+50000||t>t0+52000) last_rx_ms=t; ota_step(t); if(at<0&&ota.marked) at=t; }
    st_check("a link gap (50..52 s) restarts the 30 s: healthy 30 s after the link is back (82.1 s); not a trial -> no flash marker",at==t0+82100&&access(hm,F_OK)==0&&access(hp,F_OK)!=0);
    ota_init(t0); unlink(hm); at=-1; for(ms_t t=t0;t<=t0+120000;t+=100){ last_rx_ms=t0-5000; ota_step(t); if(ota.marked) at=t; }
    st_check("no controller link: never healthy",at<0&&access(hm,F_OK)!=0);
    ota_init(t0); st_check("a deliberate exit writes the marker with state=exit (not a failed start)",ota_mark_write(1)&&file_has(hm,"state=exit")&&access(hp,F_OK)!=0);
    /* SYSTEM's line, both languages */
    for(int lg=0;lg<NLANG;lg++){ lang_set(lg); char o[160],x[64]; uint32_t c; int ok=1,fit=1;
        memset(&ota,0,sizeof ota); c=ota_line(o,sizeof o,0); ok&=!strcmp(o,TR(TX_DV_VERSION))&&c==C_TXT3;
        ota.trial=1; c=ota_line(o,sizeof o,0); snprintf(b,sizeof b,"%s · %s",TR(TX_DV_VERSION),TR(TX_OTA_TRIAL)); ok&=!strcmp(o,b)&&c==C_TXT3; if(text_wf(o,F_XS)>LIVE_W){ fit=0; printf("     too wide: '%s' %d px\n",o,(int)text_wf(o,F_XS)); }
        c=ota_line(o,sizeof o,1); if(text_wf(o,F_XS)>LIVE_W){ fit=0; printf("     too wide: '%s' %d px\n",o,(int)text_wf(o,F_XS)); }
        ota.marked=1; c=ota_line(o,sizeof o,0); ok&=!strcmp(o,TR(TX_DV_VERSION));
        ota.st_ok=1; snprintf(ota.cur,sizeof ota.cur,"6.21-a"); snprintf(ota.name,sizeof ota.name,"6.21-a"); snprintf(ota.last,sizeof ota.last,"rolled_back:6.22.1-abcdef12:crashloop");
        c=ota_line(o,sizeof o,1); snprintf(x,sizeof x,TR(TX_OTA_FAILED),"V6.22.1"); snprintf(b,sizeof b,"%s · %s",TR(TX_DV_VERSION_RO),x); ok&=!strcmp(o,b)&&c==C_WARN; if(text_wf(o,F_XS)>LIVE_W){ fit=0; printf("     too wide: '%s' %d px\n",o,(int)text_wf(o,F_XS)); }
        snprintf(ota.cur,sizeof ota.cur,"6.20-b"); c=ota_line(o,sizeof o,0); ok&=!strcmp(o,TR(TX_DV_VERSION));     /* the failure note belongs to the current release only */
        ota.fallback=1; c=ota_line(o,sizeof o,1); snprintf(b,sizeof b,"%s · %s",TR(TX_DV_VERSION_RO),TR(TX_OTA_FALLBACK)); ok&=!strcmp(o,b)&&c==C_WARN; if(text_wf(o,F_XS)>LIVE_W){ fit=0; printf("     too wide: '%s' %d px\n",o,(int)text_wf(o,F_XS)); }
        ota.vendor=1; c=ota_line(o,sizeof o,0); ok&=!strcmp(o,TR(TX_OTA_VENDOR))&&c==C_WARN; if(text_wf(o,F_XS)>LIVE_W){ fit=0; printf("     too wide: '%s' %d px\n",o,(int)text_wf(o,F_XS)); }
        char nm[200]; snprintf(nm,sizeof nm,"[%s] SYSTEM line: VERSION · TRIAL until healthy, · V6.22 FAILED (amber), · FALLBACK (amber), the vendor notice first — each fits %d px untruncated",LANG_CODE[lg],LIVE_W);
        st_check(nm,ok&&fit);
        memset(&ota,0,sizeof ota); ota.t0=sim_now-5000; ota.vendor=1; view=V_MAIN; toast.dur=0; ota_step(sim_now); int v1=toast.dur&&!strcmp(toast.msg,TR(TX_T_UPD_VENDOR)); toast.dur=0; ota_step(sim_now+100);
        memset(&ota,0,sizeof ota); ota.t0=sim_now-5000; snprintf(ota.note,sizeof ota.note,"6.22-abcdef12"); ota_step(sim_now); snprintf(x,sizeof x,TR(TX_T_UPD_ROLLED),"V6.22",MERAKIPLUS_VERSION);
        int v2=toast.dur&&!strcmp(toast.msg,x)&&text_wf(x,F_XS)<=TOAST_TW;
        snprintf(nm,sizeof nm,"[%s] notices once per start, after the splash: vendor update / 'V6.22 FAILED · BACK ON " MERAKIPLUS_VERSION "'",LANG_CODE[lg]); st_check(nm,v1&&v2); toast.dur=0; }
    lang_set(LANG_EN);
    /* vendor-update traces (docs/ota-plan.md §7 layer 3) */
    { char sh[440],tt[440],dt[440],sw[440]; snprintf(sh,sizeof sh,"%s/customer/demo.sh",root); snprintf(tt,sizeof tt,"%s/customer/temp.tar.gz",root); snprintf(dt,sizeof dt,"%s/customer/demo_temp",root);
      snprintf(sw,sizeof sw,"%s" OTA_GUARD_KEY,root); mk_p(root,"/new_customer/merakiplus/boot");
      put_bytes(sh,"#!/bin/sh\n# kernel_mod_list\n",28); ota_vendor_scan(); int n0=!ota.vendor;
      put_bytes(tt,"x",1); ota_vendor_scan(); int a=ota.vendor; unlink(tt); put_bytes(dt,"x",1); ota_vendor_scan(); int c=ota.vendor; unlink(dt);
      put_bytes(sw,"x",1); ota_vendor_scan(); int g=ota.vendor;
      put_bytes(sh,"#!/bin/sh\n[ -x " OTA_GUARD_KEY " ] && " OTA_GUARD_KEY " &\n",(int)strlen("#!/bin/sh\n[ -x " OTA_GUARD_KEY " ] && " OTA_GUARD_KEY " &\n")); ota_vendor_scan(); int g2=!ota.vendor; unlink(sw);
      st_check("vendor traces: temp.tar.gz / demo_temp waiting for the next boot, or the gesture installed but demo.sh lost its guard line — nothing otherwise",n0&&a&&c&&g&&g2); }
    /* the exec-reload's idle gate */
    { int kv=view,kp=page,ks=sel,kt=tr.type; uint16_t k33=reg[33]; ms_t t=sim_now; last_rx_ms=t;
      view=V_MAIN; page=PG_HOME; sel=-1; tr.type=0; tch.down=0; snd.pid=0; memset(&wq,0,sizeof wq); act.pending=ACT_NONE; sav.active=0; reg[33]=0x0101;
      int i0=ota_idle(t); page=PG_SYS; int i1=!ota_idle(t); page=PG_HOME;
      reg[33]=0x1307; int i2=!ota_idle(t); reg[33]=0x0101;
      uint16_t w1[1]={(uint16_t)sv(S_BREWW)}; wq_push(23,w1,1); int i3=!ota_idle(t); memset(&wq,0,sizeof wq); txq_len=0; int i4=ota_idle(t);
      view=kv; page=kp; sel=ks; tr.type=kt; reg[33]=k33;
      if(!(i0&&i1&&i2&&i3&&i4)) printf("     idle %d %d %d %d %d guard=%s view=%d page=%d sel=%d tr=%d snd=%d\n",i0,i1,i2,i3,i4,switch_guard(sim_now)?switch_guard(sim_now):"-",view,page,sel,tr.type,snd.pid);
      st_check("reload only when idle: HOME + nothing running; not on another page, not during a brew, not with a write pending",i0&&i1&&i2&&i3&&i4); }
    memset(&ota,0,sizeof ota); ota.link0=-1; snprintf(ota.dir,sizeof ota.dir,"%s",OTA_DIR); toast.dur=0;
    strcpy(sysroot,ksr); bs.m_valid=0; last_rx_ms=klr;
}
/* ============================ v6.22: water dosing gate + MANUAL hot water (docs/water-dosing-compat.md) ============================ */
static int st_rows_water(void){ Row r[ROW_MAX]; return rows_get(PG_WATER,r); }
static void st_v622(void){
    printf("== selftest: v6.22 water dosing gate (stock mcu_has_hot_water_weight_ui 0x10885C: words 80..87 > 5.1.2; scale fault word 35 & 0x1400) + MANUAL hot water (sub19 len1) ==\n");
    char a[96],b[96]; int v[3], ok; ms_t t;
    /* (1) the parser: <= 6 letters + N.N.N, nothing else (stricter than stock = fail-safe) */
    { static const struct { const char*s; int ok,a,b,c; } P[]={{"V5.1.4",1,5,1,4},{"5.1.4",1,5,1,4},{"V5.1.2",1,5,1,2},{"V10.0.0",1,10,0,0},{"v4.1.16",1,4,1,16},{"MCUVER5.1.4",1,5,1,4},
          {"",0,0,0,0},{"V",0,0,0,0},{"V5.1",0,0,0,0},{"V5.1.4x",0,0,0,0},{"V5.1.4.1",0,0,0,0},{"V5..4",0,0,0,0},{"VERSION5.1.4",0,0,0,0},{"V 5.1.4",0,0,0,0},{"V5.1.12345",0,0,0,0},{"-5.1.4",0,0,0,0},{"\x01\x02",0,0,0,0},{"V5,1,4",0,0,0,0}};
      ok=1; for(unsigned q=0;q<sizeof P/sizeof P[0];q++){ int r=ver_parse(P[q].s,v); if(r!=P[q].ok||(r&&(v[0]!=P[q].a||v[1]!=P[q].b||v[2]!=P[q].c))){ ok=0; printf("     ver_parse('%s') = %d\n",P[q].s,r); } }
      st_check("version parse: V5.1.4 / 5.1.4 / V10.0.0 / v4.1.16 / MCUVER5.1.4 parse; empty, V, V5.1, V5.1.4x, V5.1.4.1, V5..4, a 7-letter prefix, a space, a 5-digit part, '-', control bytes, commas do not",ok); }
    /* (2) the gate on the register words (lo byte first), incl. the copy and 'not polled yet' */
    { static const struct { const char*a,*b; int st, dose; } G[]={{"V5.1.4",NULL,MV_OK,1},{"V5.1.3",NULL,MV_OK,1},{"V6.0.0",NULL,MV_OK,1},{"V10.0.0",NULL,MV_OK,1},{"V5.2.0",NULL,MV_OK,1},
          {"V5.1.2",NULL,MV_OK,0},{"V5.0.9",NULL,MV_OK,0},{"V4.1.6",NULL,MV_OK,0},{"V3.8.5",NULL,MV_OK,0},{"",NULL,MV_NONE,0},{"V5.1.4","V5.1.3",MV_COPY,0},{"V5.1.4","",MV_COPY,0},{"V5.1.4 ",NULL,MV_BAD,0},{"GARBAGE",NULL,MV_BAD,0}};
      ok=1; t=st_home();
      for(unsigned q=0;q<sizeof G/sizeof G[0];q++){ pv_mcu_ver(G[q].a,G[q].b); int st=mcu_ver_read(a,sizeof a,v);
          if(st!=G[q].st||wd_ver_dosing()!=G[q].dose||wd_dosing()!=G[q].dose||water_manual()==G[q].dose||(st_rows_water()==2)!=G[q].dose){ ok=0; printf("     gate '%s'/'%s': state %d dosing %d rows %d\n",G[q].a,G[q].b?G[q].b:"=",st,wd_dosing(),st_rows_water()); } }
      pv_mcu_ver("V5.1.4",NULL); reg_seen[87]=0; int np=mcu_ver_read(a,sizeof a,v)==MV_NONE&&!wd_dosing(); reg_seen[87]=1; reg_seen[95]=0; np&=!wd_dosing(); reg_seen[95]=1; np&=wd_dosing();
      st_check("gate: > 5.1.2 = dosing (5.1.3, 5.1.4, 5.2.0, 6.0.0, 10.0.0); <= 5.1.2, empty, copy (88..95) differs, garbage, a word not polled yet -> MANUAL ONLY",ok&&np); }
    /* (3) MANUAL ONLY (V5.1.2): rows hidden, the manual frame, never word 16 = 1, the legacy stop, config untouched */
    t=st_home(); pv_mcu_ver("V5.1.2",NULL); int cw0=cfg_writes; char md0[33]; strcpy(md0,cfg_md5); int hm0=SET[S_HOTMODE].val, hw0=SET[S_HOTW].val;
    page=PG_WATER; static_dirty=1; sim_run(300); memset(&wq,0,sizeof wq);
    int rows0=st_rows_water()==0&&page_rows(PG_WATER)==0&&scroll_max(PG_WATER)==0;
    act_push(ACT_WATER); int fr=wq.n==1&&wq.q[0].sub==19&&wq.q[0].len==1&&wq.q[0].w[0]==1&&frame_trig_audit(&wq.q[0],I_WATERMAN)&&!frame_trig_audit(&wq.q[0],0)&&q_hex(0,"aa 55 06 13 01 01 00 c1 db");
    wq.n=0;
    st_check("no dosing (V5.1.2): the WATER page has NO rows (HOT WATER + WATER MODE hidden); a start = cmd6 sub19 len1 word 19 = 1: aa 55 06 13 01 01 00 c1 db (stock 0x105118 usart_send(6,19,1))",rows0&&fr);
    { uint16_t h[11]={1,0,870,0,60,3,60,1,400,0,0}; int r1=rej(9,h,11,I_WATER,WJ_TRIG), r2=rej(16,W1(1),1,WBIT(16),WJ_TRIG), r3=rej(19,W1(1),1,0,WJ_TRIG);
      pv_mcu_ver("V5.1.4",NULL); int a1=wq_push_t(9,h,11,I_WATER); wq.n=0; pv_mcu_ver("V5.1.2",NULL);
      st_check("frame policy: word 16 = 1 is rejected without a dosing controller EVEN WITH the water intent (the AUTO block, a bare sub16); word 19 = 1 needs its own intent; the same AUTO block passes on V5.1.4",r1&&r2&&r3&&a1); }
    reg[19]=1; reg[16]=0; act_push_stop(ACT_WATER); int sp=wq.n==1&&q_hex(0,"aa 55 06 13 01 00 00 c0 4b"); wq.n=0; reg[19]=0; act_push_stop(ACT_WATER); sp&=wq.n==1&&wq.q[0].sub==19&&wq.q[0].w[0]==0; wq.n=0;
    st_check("no dosing: STOP WATER = word 19 = 0 ONLY (aa 55 06 13 01 00 00 c0 4b, the legacy page's stop) — never a word 16 frame to a controller that is not known to be > 5.1.2",sp);
    /* the whole UI path: HOLD TO START on the manual WATER page -> frame -> ACK -> the controller runs -> HOME (MANUAL, no target) -> STOP */
    toast.dur=0; sim_now=t=sim_now+100; int f=st_hold(CX,START_Y+START_H/2,700); t=sim_now;
    int hs=f&&act.pending==ACT_WATER&&wq.n==1&&q_hex(0,"aa 55 06 13 01 01 00 c1 db")&&page==PG_HOME;
    wq_step(t); st_ack_last(t+20); ui_frame(t+=30); reg[19]=1;
    reg[39]=0; st_s0(0x030B,0,0x000C,0,300,t+=200); ui_frame(t); home_model(t);
    int run=act.pending==ACT_NONE&&act.ph==PH_WATER&&wd.man&&!strcmp(hm.sub,TR(TX_H_WATER_MAN))&&hm.gmark<0&&hit_test(240,372,t)==T_STOP;
    int quiet=1; for(int q=0;q<60;q++){ reg[39]=(uint16_t)(q*20); st_s0(0x030B,0,0x000C,0,300,t+=2000); ui_frame(t); if(wq.n||(toast.dur&&!strcmp(toast.msg,TR(TX_T_NOSTART)))) quiet=0; }
    st_check("manual WATER page: HOLD TO START sends the sub19 frame and goes HOME; running: 'MANUAL · RUNS UNTIL STOP', no target mark, STOP shown — and NOTHING is sent for 120 s (no time limit, no weight stop: decision 2)",hs&&run&&quiet&&act.ph==PH_WATER);
    t=st_tap(240,372,t); int st1=wq.n==1&&q_hex(0,"aa 55 06 13 01 00 00 c0 4b")&&!strcmp(toast.msg,TR(TX_T_STOP_WATER)); wq_step(t); st_ack_last(t+20); ui_frame(t+=30);
    reg[19]=0; st_s0(0x0101,0,0x000C,0,300,t+=200); ui_frame(t);
    st_check("STOP during manual water: word 19 = 0 -> bits cleared -> 'WATER STOPPED', DONE",st1&&!strcmp(toast.msg,TR(TX_T_STOPPED_WATER))&&act.ph==PH_DONE&&act.last_ph==PH_WATER&&stp.n_ok==1);
    settings_load();
    st_check("config.json is never written by the gate: hotWaterMode / autoHotWaterTarget unchanged, no config write, same md5 (config hotWaterMode = 1 on an old controller -> manual in the UI only)",
             cfg_writes==cw0&&!strcmp(md0,cfg_md5)&&SET[S_HOTMODE].val==hm0&&SET[S_HOTW].val==hw0&&hm0==1);
    /* (4) dosing controller (V5.1.4): AUTO unchanged; WATER MODE = MANUAL now starts manual water (no more 'use stock UI') */
    t=st_home(); act_push(ACT_WATER); int au=wq.n==1&&q_hex(0,"aa 55 06 09 0b 01 00 00 00 66 03 00 00 3c 00 03 00 3c 00 01 00 90 01 00 00 00 00 08 3e"); wq.n=0;
    int kh=SET[S_HOTMODE].val; SET[S_HOTMODE].val=2; int g2=act_guard(ACT_WATER,t)==NULL&&st_rows_water()==2; act_push(ACT_WATER); int mn=wq.n==1&&q_hex(0,"aa 55 06 13 01 01 00 c1 db"); wq.n=0;
    reg[19]=1; act_push_stop(ACT_WATER); int ms2=wq.n==2&&q_hex(0,ST_V_WATER)&&q_hex(1,ST_V_WMAN); wq.n=0; reg[19]=0;
    home_model(t); int lab=!strcmp(hm.alab[ACT_WATER],TR(TX_A_WATER_MAN)); SET[S_HOTMODE].val=kh; home_model(t); int lab2=strcmp(hm.alab[ACT_WATER],TR(TX_A_WATER_MAN))!=0;
    st_check("V5.1.4: AUTO = the unchanged HOTWATER.md W1 frame; WATER MODE = MANUAL: no guard (v6.21: 'USE STOCK UI'), both rows stay, start = sub19 len1, stop = word 16 + word 19 = 0 as since v6.13; HOME's water label says 'MAN.'",au&&g2&&mn&&ms2&&lab&&lab2);
    /* (5) scale fault: manual only + a running AUTO dose is stopped once (stock hotWaterUpdate 0x115B80..0x115BD8) */
    t=st_home(); reg[16]=1; reg[19]=0; st_s0(0x030B,0,0x000C,0,300,t+=100); ui_frame(t); int nf0=wd.n_fault;
    reg[35]=0x1000; st_s0(0x030B,0,0x000C,0,300,t+=100); ui_frame(t);
    int fs=wq.n==1&&q_hex(0,ST_V_WATER)&&wd.n_stop==1&&wd.n_fault==nf0+1&&!strcmp(toast.msg,TR(TX_T_WATER_SCALE))&&!wd_dosing()&&water_manual()&&st_rows_water()==0;
    for(int q=0;q<10;q++){ st_s0(0x030B,0,0x000C,0,300,t+=100); ui_frame(t); } int once=wq.n==1&&wd.n_stop==1;
    diag_mcu_text(a,&(uint32_t){0},b,&(uint32_t){0}); int dg=!strcmp(a,"V5.1.4")&&!strcmp(b,TR(TX_DV_WD_FAULT));
    st_check("scale fault (word 35 0x1000) during an AUTO dose: word 16 = 0 sent ONCE + 'SCALE FAULT · WATER STOPPED'; manual only (rows hidden), DIAGNOSTICS 'MANUAL (SCALE)'",fs&&once&&dg);
    wq.n=0; reg[35]=0; reg[16]=0; st_s0(0x0101,0,0x000C,0,300,t+=100); ui_frame(t); int back=wd_dosing()&&st_rows_water()==2&&!wd.fault;
    t=st_home(); reg[16]=0; reg[19]=1; st_s0(0x030B,0,0x000C,0,300,t+=100); ui_frame(t); reg[35]=0x0400; st_s0(0x030B,0,0x000C,0,300,t+=100); ui_frame(t); int man=wq.n==0&&wd.n_stop==0;
    reg[35]=0; reg[19]=0; t=st_home(); pv_mcu_ver("V5.1.2",NULL); reg[16]=1; st_s0(0x030B,0,0x000C,0,300,t+=100); ui_frame(t); reg[35]=0x1000; st_s0(0x030B,0,0x000C,0,300,t+=100); ui_frame(t); int old=wq.n==0;
    reg[35]=0; reg[16]=0;
    st_check("scale fault: cleared -> AUTO again; a MANUAL run is not stopped (stock neither); on a controller not > 5.1.2 no word 16 frame is ever sent; err text 'SCALE FAULT', code E2",back&&man&&old&&(reg[35]=0x0400,!strcmp(err_text(),TR(TX_ER_E2_SCALE))&&!strcmp(err_code(),"E2"))); reg[35]=0;
    /* (6) DIAGNOSTICS CONTROLLER block + texts that fit (both languages) */
    t=st_home(); uint32_t c1,c2; diag_mcu_text(a,&c1,b,&c2); int d1=!strcmp(a,"V5.1.4")&&c1==C_TXT&&!strcmp(b,TR(TX_DV_WD_AUTO))&&c2==C_OK;
    pv_mcu_ver("",NULL); diag_mcu_text(a,&c1,b,&c2); int d2=!strcmp(a,TR(TX_DV_MCU_NONE))&&!strcmp(b,TR(TX_DV_WD_MAN));
    pv_mcu_ver("V5.1.4","V5.1.3"); diag_mcu_text(a,&c1,b,&c2); int d3=!strcmp(a,TR(TX_DV_MCU_COPY))&&c1==C_WARN;
    pv_mcu_ver("v5.1.2",NULL); diag_mcu_text(a,&c1,b,&c2); int d4=!strcmp(a,"V5.1.2")&&!strcmp(b,TR(TX_DV_WD_MAN));
    pv_mcu_ver("V5.1.4",NULL); page=PG_DIAG; scroll[PG_DIAG]=scroll_max(PG_DIAG); sim_run(300); int d5=DG_ABOUT+76+36-(int)scroll[PG_DIAG]<=LIST_VY1+40;
    st_check("DIAGNOSTICS › CONTROLLER: FIRMWARE 'V5.1.4' · HOT WATER 'AUTO + MANUAL' (green); empty -> NOT REPORTED / MANUAL ONLY; copy differs (amber); 'v5.1.2' shown upper case; ABOUT still reachable",d1&&d2&&d3&&d4&&d5);
    ok=1; for(int lg=0;lg<NLANG;lg++){ int kl=ui_lang; lang_set(lg);
        LAY(text_wf(TR(TX_A_WATER_MAN),F_XS),60-8,"icon target (pitch 60)",TR(TX_A_WATER_MAN));
        LAY(text_wf(TR(TX_W_MAN_T),F_S),2*chord_half(WMAN_Y0,20)-24,"manual WATER page title (F_S)",TR(TX_W_MAN_T));
        static const short L[]={TX_W_MAN_RUN,TX_W_WHY_OLD,TX_W_WHY_UNK,TX_W_WHY_FAULT}; for(unsigned q=0;q<sizeof L/sizeof L[0];q++) LAY(text_wf(TR(L[q]),F_XS),336-8,"manual WATER page line (list 336)",TR(L[q]));
        LAY(text_wf(TR(TX_H_HOLD_WATER_MAN),F_XS),KB[KSUB].maxw,"HOME subline",TR(TX_H_HOLD_WATER_MAN));
        lang_set(kl); }
    st_check("v6.22 texts fit: 'MAN.' under the icon (52 px), the manual WATER page's title + lines, the HOME sublines (both languages)",ok);
    sim_reset();
}
static int selftest(const char*cfgsrc){
    int base_lang=ui_lang;
    printf("== selftest (UI language during the test: %s) ==\n",LANG_CODE[ui_lang]);
    printf("== selftest: frame vectors (observed bytes) ==\n");
    snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); settings_load();
    st_check("config loaded",cfg_ok);
    memset(reg_seen,0,sizeof reg_seen); memset(&wq,0,sizeof wq); pv_mcu_ver("V5.1.4",NULL);   /* v6.22: the AUTO water frame needs a dosing controller */
    uint8_t b[80]; int n;
    uint16_t aw=0; n=build_write(b,1,&aw,1);
    st_check("keep-awake word1=0 = AA 55 06 01 01 00 00 C5 33 (SESSION §10.1)",hexeq(b,n,"aa 55 06 01 01 00 00 c5 33"));
    act_push(ACT_GRIND);
    st_check("GRIND sub3 len4    = BREW.md W1",wq.n==1&&q_hex(0,"aa 55 06 03 04 01 00 b9 00 3c 00 01 00 5d 6e")); wq.n=0;
    act_push(ACT_BREW);
    st_check("BREW sub5 len18    = BREW.md W3",wq.n==1&&q_hex(0,"aa 55 06 05 12 3c 00 00 00 00 00 00 00 01 00 00 00 66 03 00 00 3c 00 03 00 3c 00 00 00 90 01 00 00 00 00 00 00 00 00 01 00 21 df")); wq.n=0;
    act_push(ACT_WATER);
    st_check("WATER sub9 len11   = HOTWATER.md W1",wq.n==1&&q_hex(0,"aa 55 06 09 0b 01 00 00 00 66 03 00 00 3c 00 03 00 3c 00 01 00 90 01 00 00 00 00 08 3e")); wq.n=0;
    act_push(ACT_STEAM);
    st_check("STEAM start sub24  = SESSION §10.2",wq.n==1&&q_hex(0,"aa 55 06 18 08 01 00 8a 02 00 00 00 00 00 00 00 00 00 00 00 00 cd 67")); wq.n=0;
    act_push_steam_stop();
    st_check("STEAM stop sub24   = SESSION §10.3",wq.n==1&&q_hex(0,"aa 55 06 18 08 00 00 8a 02 00 00 00 00 00 00 00 00 00 00 00 00 0c f7")); wq.n=0;
    uint16_t bad=1; st_check("write band: word 33 rejected",rej(33,&bad,1,0,WJ_BAND)); st_check("write band: word 32 rejected",rej(32,&bad,1,0,WJ_BAND));
    st_check("write band: sub30 len4 rejected",rej(30,(uint16_t[4]){0,0,0,0},4,0,WJ_BAND)); st_check("write band: sub115 rejected",rej(115,&bad,1,0,WJ_BAND));
    st_check("write band: sub112 len3 ok",wq_push(112,(uint16_t[3]){0x104,0x605,70},3)); wq.n=0;
    /* ACK + one retransmit + failure */
    ms_t t=1000; wq_push(11,(uint16_t[1]){880},1); wq_step(t); int l0=txq_len; wq_step(t+100); int l1=txq_len; wq_step(t+160); int l2=txq_len;
    st_check("retransmit: none before 150 ms, exactly one after",l1==l0&&l2==l0+9);
    wq_step(t+300); wq_step(t+470); st_check("failure after the 2nd timeout (queue emptied, result=FAIL)",wq.n==0&&wq.result==WR_FAIL&&txq_len==l2);
    wq_push(11,(uint16_t[1]){880},1); wq_step(t+500);
    uint8_t ack[9]={0xAA,0x55,0x86,11,1,0x70,0x03,0,0}; uint16_t c=crc16(ack,7); ack[7]=c&0xFF; ack[8]=c>>8; rx_feed(ack,9,t+520);
    st_check("ACK 0x86 with echo releases the queue (result=OK)",wq.n==0&&wq.result==WR_OK&&reg[11]==880);
    memset(reg_seen,0,sizeof reg_seen); memset(reg,0,sizeof reg); memset(&wq,0,sizeof wq); txq_len=0;

    printf("== selftest: packing of words 112/113/114 (PARITY §3.7) ==\n");
    st_check("word112 = (switch<<8)|pressure: (1,4)=0104 (0,4)=0004 (1,3)=0103 (0,3)=0003",pack112(1,4)==0x0104&&pack112(0,4)==0x0004&&pack112(1,3)==0x0103&&pack112(0,3)==0x0003);
    st_check("word113 = (time<<8)|pause:   (6,5)=0605 (15,0)=0F00 (5,255)=05FF",pack113(6,5)==0x0605&&pack113(15,0)==0x0F00&&pack113(5,255)==0x05FF);
    { int ok=1; for(int sw=0;sw<=1;sw++) for(int pr=3;pr<=4;pr++) for(int tt=5;tt<=60;tt++) for(int br=0;br<=255;br++){
          uint16_t a=pack112(sw,pr), d=pack113(tt,br); if((a>>8)!=sw||(a&0xFF)!=pr||(d>>8)!=tt||(d&0xFF)!=br) ok=0; }
      st_check("pack/unpack round trip for all (switch, pressure 3-4, time 5-60, pause 0-255)",ok); }
    st_check("the current config packs to 0104 / 0605 / 70",pack112(sv(S_PISW),sv(S_PIPRESS))==0x0104&&pack113(sv(S_PITIME),sv(S_BREAK))==0x0605&&sv(S_HEADT)==70);
    setting_mcu_push(S_PISW,0);    st_check("SAVE pre-infusion OFF = sub112 len2: aa 55 06 70 02 04 00 05 06 df c6",wq.n==1&&q_hex(0,"aa 55 06 70 02 04 00 05 06 df c6")); wq.n=0;
    setting_mcu_push(S_PIPRESS,3); st_check("SAVE pre-inf. pressure 3 = sub112 len2: aa 55 06 70 02 03 01 05 06 8f 72",wq.n==1&&q_hex(0,"aa 55 06 70 02 03 01 05 06 8f 72")); wq.n=0;
    setting_mcu_push(S_PITIME,7);  st_check("SAVE pre-inf. time 7  = sub113 len1: aa 55 06 71 01 05 07 9d 61",wq.n==1&&q_hex(0,"aa 55 06 71 01 05 07 9d 61")); wq.n=0;
    setting_mcu_push(S_HEADT,80);  st_check("SAVE group temp 80    = sub114 len1: aa 55 06 72 01 50 00 e3 b7",wq.n==1&&q_hex(0,"aa 55 06 72 01 50 00 e3 b7")); wq.n=0;
    setting_mcu_push(S_GRINDMODE,1); st_check("SAVE grind mode       = sub2 len1 (NOT stock's len2 with start word 3)",wq.n==1&&wq.q[0].sub==2&&wq.q[0].len==1&&q_hex(0,"aa 55 06 02 01 01 00 c4 e7")); wq.n=0;
    st_check("pre-inf. pressure: only 3 and 4 can be selected (MCU 0x1E10)",SET[S_PIPRESS].lo==3&&SET[S_PIPRESS].hi==4&&setting_mcu_push(S_PIPRESS,5)==-1&&wq_rej==WJ_RANGE&&setting_mcu_push(S_PIPRESS,2)==-1); wq.n=0;

    printf("== selftest: trigger word audit (PARITY §1.5/§6.2: words 3/6/9 + 26/27/30) ==\n");
    { int ok=1, nfr=0;
      for(int i=0;i<NSET;i++){ Setting*s=&SET[i]; if(s->mcu==M_NONE) continue;
          int vals[3]={s->lo,s->hi,s->val};
          for(int k=0;k<3;k++){ wq.n=0; int r=setting_mcu_push(i,vals[k]);
              if(r!=1||wq.n!=1){ ok=0; printf("     %s=%d: push r=%d rej=%d\n",s->key,vals[k],r,wq_rej); continue; }
              const WFrame*f=&wq.q[0]; nfr++;
              if(!frame_trig_audit(f,0)) ok=0;
              static const int never[]={3,6,9,16,22,24,26,27,30}; for(unsigned q=0;q<sizeof never/sizeof never[0];q++) if(frame_covers(f,never[q])){ ok=0; printf("     %s: frame covers word %d\n",s->key,never[q]); } } }
      wq.n=0; char nm[120]; snprintf(nm,sizeof nm,"settings SAVE: %d frames (min/max/current) NEVER cover a trigger/start word",nfr); st_check(nm,ok&&nfr>=30); }
    { uint16_t on=1,awk=0; int ok=wq_push(0,&on,1)&&wq_push(1,&awk,1)&&wq.n==2&&frame_trig_audit(&wq.q[0],0)&&frame_trig_audit(&wq.q[1],0)&&q_hex(0,"aa 55 06 00 01 01 00 c5 5f")&&q_hex(1,"aa 55 06 01 01 00 00 c5 33");
      wq.n=0; st_check("keep-awake/PowerOn: sub0=[1], sub1=[0] — no trigger words",ok); }
    act_push(ACT_GRIND); st_check("GRIND: only start words 3+6 set (explicit intent)",wq.n==1&&frame_trig_audit(&wq.q[0],I_GRIND)&&!frame_trig_audit(&wq.q[0],0)); wq.n=0;
    pv_mcu_ver("V5.1.4",NULL); act_push(ACT_WATER); st_check("WATER:  only start words 9+16 set; word 19 (manual) = 0",wq.n==1&&frame_trig_audit(&wq.q[0],I_WATER)&&wq.q[0].w[10]==0); wq.n=0;
    act_push(ACT_BREW);  st_check("BREW:   only start words 9+22 set; words 6/16 = 0",wq.n==1&&frame_trig_audit(&wq.q[0],I_BREW)&&wq.q[0].w[1]==0&&wq.q[0].w[11]==0); wq.n=0;
    act_push(ACT_STEAM);
    { const WFrame*f=&wq.q[0]; st_check("STEAM start: words 26/27/28/29/30/31 = 0 (atlas §3.4)",wq.n==1&&f->sub==24&&f->len==8&&f->w[0]==1&&!f->w[2]&&!f->w[3]&&!f->w[4]&&!f->w[5]&&!f->w[6]&&!f->w[7]&&frame_trig_audit(f,I_STEAM)); } wq.n=0;
    act_push_steam_stop();
    { const WFrame*f=&wq.q[0]; st_check("STEAM stop:  word 24 = 0 and words 26..31 = 0",wq.n==1&&f->w[0]==0&&!f->w[2]&&!f->w[3]&&!f->w[4]&&!f->w[5]&&!f->w[6]&&!f->w[7]&&frame_trig_audit(f,0)); } wq.n=0;
    /* brew with a diverging mirror: sync frames + start, all audited */
    sim_ready(5000); reg[23]=300; reg[112]=0x0003; act_push(ACT_BREW);
    st_check("BREW with a diverging mirror: sub23 len1 + sub112 len3 + sub5 len18, only the last has start words",wq.n==3&&wq.q[0].sub==23&&wq.q[1].sub==112&&wq.q[1].len==3&&wq.q[2].sub==5
             &&frame_trig_audit(&wq.q[0],0)&&frame_trig_audit(&wq.q[1],0)&&frame_trig_audit(&wq.q[2],I_BREW)&&q_hex(1,"aa 55 06 70 03 04 01 05 06 46 00 c7 02")); wq.n=0;
    sim_ready(5000); reg[113]=0x0606; act_push(ACT_BREW);
    st_check("BREW after the MCU has overwritten word113 lo (0606): no re-sync (only the hi byte is compared)",wq.n==1&&wq.q[0].sub==5); wq.n=0;
    /* negative: the policy rejects */
    ms_t nb0=wq.n_block;
    st_check("REJECTED: steam block with word30=1 (MCU REBOOT)",rej(24,(uint16_t[8]){1,650,0,0,0,0,1,0},8,I_STEAM,WJ_TRIG));
    st_check("REJECTED: steam block with word26=100 (grind calibration)",rej(24,(uint16_t[8]){1,650,100,0,0,0,0,0},8,I_STEAM,WJ_TRIG));
    st_check("REJECTED: steam block with word27=500 (extraction calibration)",rej(24,(uint16_t[8]){1,650,0,500,0,0,0,0},8,I_STEAM,WJ_TRIG));
    st_check("REJECTED: sub30 len1 = 1, sub26 len1 = 100, sub27 len1 = 200",rej(30,W1(1),1,0,WJ_TRIG)&&rej(26,W1(100),1,0,WJ_TRIG)&&rej(27,W1(200),1,0,WJ_TRIG));
    st_check("REJECTED: intent naming word 30 — even with a zero payload",rej(24,(uint16_t[8]){0,650,0,0,0,0,0,0},8,I_STEAM|WBIT(30),WJ_TRIG));
    st_check("REJECTED: start words without intent (sub3=1, sub9=1, sub24=1, sub22=1, sub16=1)",rej(3,W1(1),1,0,WJ_TRIG)&&rej(9,W1(1),1,0,WJ_TRIG)&&rej(24,W1(1),1,0,WJ_TRIG)&&rej(22,W1(1),1,0,WJ_TRIG)&&rej(16,W1(1),1,0,WJ_TRIG));
    st_check("REJECTED: brew block with the wrong intent (I_GRIND)",rej(5,(uint16_t[18]){60,0,0,0,1,0,870,0,60,3,60,0,400,0,0,0,0,1},18,I_GRIND,WJ_TRIG));
    st_check("REJECTED: stock's sub2 len2 with word3=1 (grind mode + start)",rej(2,(uint16_t[2]){1,1},2,0,WJ_TRIG));
    st_check("REJECTED: service words — word1=1 (sleep/drain), word0=0 (power off) WITHOUT power-off intent (v6.14: WJ_TRIG), word8=1 (self-test)",rej(1,W1(1),1,0,WJ_RANGE)&&rej(0,W1(0),1,0,WJ_TRIG)&&rej(8,W1(1),1,0,WJ_RANGE));
    st_check("REJECTED: service words — word20=1 (back-flush), word28/29=1 (drain), word21=1; word19=1 (manual water) WITHOUT the manual-water intent (v6.22: WJ_TRIG)",rej(20,W1(1),1,0,WJ_RANGE)&&rej(28,W1(1),1,0,WJ_RANGE)&&rej(29,W1(1),1,0,WJ_RANGE)&&rej(19,W1(1),1,0,WJ_TRIG)&&rej(21,W1(1),1,0,WJ_RANGE));
    st_check("REJECTED: values out of spec — temp 1200/840, group 110, milk 710, dose 310/9",rej(11,W1(1200),1,0,WJ_RANGE)&&rej(11,W1(840),1,0,WJ_RANGE)&&rej(114,W1(110),1,0,WJ_RANGE)&&rej(25,W1(710),1,0,WJ_RANGE)&&rej(4,W1(310),1,0,WJ_RANGE)&&rej(4,W1(9),1,0,WJ_RANGE));
    st_check("REJECTED: values out of spec — water 250/5100, brew weight 95/1000, level 0/5, mode 0/3",rej(17,W1(250),1,0,WJ_RANGE)&&rej(17,W1(5100),1,0,WJ_RANGE)&&rej(23,W1(95),1,0,WJ_RANGE)&&rej(23,W1(1000),1,0,WJ_RANGE)&&rej(14,W1(0),1,0,WJ_RANGE)&&rej(14,W1(5),1,0,WJ_RANGE)&&rej(2,W1(0),1,0,WJ_RANGE)&&rej(2,W1(3),1,0,WJ_RANGE));
    st_check("REJECTED: word112 with pressure 5 / switch byte 2; word113 with time 4; recipe word 13 != 60",rej(112,(uint16_t[2]){0x0105,0x0605},2,0,WJ_RANGE)&&rej(112,(uint16_t[2]){0x0204,0x0605},2,0,WJ_RANGE)&&rej(113,W1(0x0405),1,0,WJ_RANGE)&&rej(13,W1(340),1,0,WJ_RANGE));
    st_check("rejected frames are counted (DIAGNOSE 'BLK') and nothing is queued",wq.n_block>nb0+30&&wq.n==0);
    { int keep=SET[S_STEAMT].val; SET[S_STEAMT].val=9999; int r=act_push_steam_stop(); int ok=r&&wq.n==1&&wq.q[0].w[0]==0&&wq.q[0].w[1]==700; wq.n=0; SET[S_STEAMT].val=keep;
      st_check("STOP STEAM cannot be blocked: the target word is clamped to spec (9999 -> 700)",ok); }

    printf("== selftest: boot push equivalence (PARITY §1.3-1.4) ==\n");
    { /* stock: model_data_init fills the mirror from config; fuart_thread sends sub1 len30 + sub112 len3.
         Independent model straight from the config fields (not via SET[]). */
      int m[129]; memset(m,0,sizeof m); int v,sw=0,pr=0,ti=0,br=0,ok=1;
      ok&=cfg_get_int("brewTemper",&v); m[11]=v; ok&=cfg_get_int("autoGrindWeight",&v); m[4]=v; ok&=cfg_get_int("grindMode",&v); m[2]=v;
      ok&=cfg_get_int("steamGears",&v); m[14]=v; ok&=cfg_get_int("autobrewWeight",&v); m[23]=v; ok&=cfg_get_int("AtuoSteamTarge",&v); m[25]=v;
      ok&=cfg_get_int("autoHotWaterTarget",&v); m[17]=v; ok&=cfg_get_int("preInfusionSwitch",&sw); ok&=cfg_get_int("preInfusionPressure",&pr);
      ok&=cfg_get_int("preInfusionTime",&ti); ok&=cfg_get_int("breakTime",&br); ok&=cfg_get_int("headTemper",&v); m[114]=v;
      m[112]=(sw?0x100:0)|(pr&0xFF); m[113]=(ti<<8)|(br&0xFF); m[5]=m[13]=m[15]=60;
      st_check("config fields for the boot push model found",ok);
      int same=1,pol=1,nw=0;
      for(int i=0;i<NSET;i++){ Setting*s=&SET[i]; if(s->mcu==M_NONE) continue; wq.n=0;
          if(setting_mcu_push(i,s->val)!=1){ same=0; continue; }
          const WFrame*f=&wq.q[0]; for(int k=0;k<f->len;k++){ nw++; if(f->w[k]!=(uint16_t)m[f->sub+k]){ same=0; printf("     %s: word %d = %u, boot push would give %d\n",s->key,f->sub+k,f->w[k],m[f->sub+k]); } } }
      wq.n=0;
      st_check("SAVE writes exactly the words/values that stock's boot push would set from the same config",same&&nw>=12);
      static const int bw[]={2,4,5,11,13,14,15,17,23,25,112,113,114}; for(unsigned q=0;q<sizeof bw/sizeof bw[0];q++) if(!word_ok(bw[q],(uint16_t)m[bw[q]])) pol=0;
      st_check("all boot push words from the current config are within the frame policy",pol);
      uint16_t p3[3]={(uint16_t)m[112],(uint16_t)m[113],(uint16_t)m[114]}; n=build_write(b,112,p3,3);
      st_check("the SAVE path's packing (PARITY §1.3: time<<8 | breakTime) of the same config = aa 55 06 70 03 04 01 05 06 46 00 c7 02 (word 113 = 0605)",hexeq(b,n,"aa 55 06 70 03 04 01 05 06 46 00 c7 02"));
      /* v6.7: stock's OWN boot push was captured on the device (boot/ondevice/GROUND-TRUTH.md): word 113 lo byte = 0, not breakTime */
      { uint16_t g3[3]={(uint16_t)m[112],(uint16_t)(ti<<8),(uint16_t)m[114]}; n=build_write(b,112,g3,3);
        st_check("v6.7 VECTOR 1: stock's captured boot push sub112 len3 = aa 55 06 70 03 04 01 00 06 46 00 c7 ce (word 113 = 0600: the breakTime byte is 0)",br==5&&hexeq(b,n,"aa 55 06 70 03 04 01 00 06 46 00 c7 ce"));
        uint16_t a30g[30],p3g[3]; boot_push_words(a30g,p3g); n=build_write(b,1,a30g,30);
        st_check("v6.7 VECTOR 2: the boot push's sub1 len30 = stock's captured 67 bytes (… 62 3e), byte for byte",
                 hexeq(b,n,"aa 55 06 01 1e 00 00 01 00 00 00 b9 00 3c 00 00 00 00 00 00 00 00 00 00 00 66 03 00 00 3c 00 03 00 3c 00 00 00 90 01 00 00 00 00 00 00 00 00 00 00 68 01 00 00 8a 02 00 00 00 00 00 00 00 00 00 00 62 3e"));
        n=build_write(b,112,p3g,3);
        st_check("v6.7: boot_push_words() gives words 112..114 = 0104 / 0600 / 70 = stock's captured sub112 len3, byte for byte",p3g[0]==0x0104&&p3g[1]==0x0600&&p3g[2]==70&&hexeq(b,n,"aa 55 06 70 03 04 01 00 06 46 00 c7 ce"));
        int bk=SET[S_BREAK].val, okb=1;
        for(int q=0;q<3;q++){ static const int tv[3]={0,9,255}; SET[S_BREAK].val=tv[q]; boot_push_words(a30g,p3g); okb&=(p3g[1]==0x0600);
            wq.n=0; setting_mcu_push(S_PITIME,7); okb&=(wq.n==1&&wq.q[0].sub==113&&wq.q[0].w[0]==(uint16_t)(0x0700|tv[q])); wq.n=0; }
        SET[S_BREAK].val=bk;
        st_check("v6.7: breakTime 0 / 9 / 255 in config -> the boot push's word 113 is still 0600; the SAVE path still packs breakTime (07xx)",okb);
        st_check("v6.7: word 113 = 0600 is within the frame policy, and is not the MCU's start-value signature (hi byte >= 5)",word_ok(113,0x0600)&&SET[S_PITIME].lo>=5); }
      /* v6.4: the SIM build's boot push (boot_push_step WITHOUT the activity guard = the contract mode) — same code as in
         SIM, run against the write queue here. v6.5: the device uses the same code WITH the guard — see "boot push on the device" below. */
      { __typeof__(wq) wq0=wq; ms_t rx0=last_rx_ms; uint16_t ws0=reg[33]; int tx0=txq_len, bt0=SET[S_BREWT].val; ms_t T=sim_now+500000;
        uint16_t a30[30],p3b[3]; boot_push_words(a30,p3b); int eq=1;
        for(int k=1;k<=30;k++) if(a30[k-1]!=(uint16_t)m[k]) eq=0;
        for(int k=0;k<3;k++) if(p3b[k]!=(uint16_t)(k==1?(ti<<8):m[112+k])) eq=0;      /* v6.7: word 113 lo = 0 in the boot push */
        st_check("v6.4 SIM boot push: words 1..30 + 112..114 = the independent config model (start/trigger words = 0, words 5/13/15 = 60)",eq&&a30[0]==0&&!a30[2]&&!a30[5]&&!a30[8]&&!a30[15]&&!a30[21]&&!a30[23]&&!a30[25]&&!a30[26]&&!a30[29]);
        memset(&wq,0,sizeof wq); wq.sim_ack=1; memset(&bp,0,sizeof bp); memset(&sav,0,sizeof sav); act.pending=ACT_NONE; reg[33]=0x0101; txq_len=0;
        last_rx_ms=T-5000; boot_push_start(0); boot_push_step(T);
        int off=(bp.state==BP_WAIT_A&&wq.n==0);                                  /* no link: wait */
        last_rx_ms=T; reg[33]=0x0141; boot_push_step(T); off&=(bp.state==BP_WAIT_A&&wq.n==0);   /* grind running: wait (the block zeroes the start words) */
        reg[33]=0x0101;
        st_check("v6.4 SIM boot push: postponed without a link and while an activity is running",off);
        boot_push_step(T);
        int okA=bp.state==BP_A&&wq.n==1&&wq.q[0].sub==1&&wq.q[0].len==30&&frame_trig_audit(&wq.q[0],0);
        wq_step(T); wq_step(T+50); boot_push_step(T+50); boot_push_step(T+50);
        int okB=bp.ra==BPR_OK&&bp.state==BP_B&&wq.n==1&&q_hex(0,"aa 55 06 70 03 04 01 00 06 46 00 c7 ce");
        wq_step(T+60); wq_step(T+110); boot_push_step(T+110);
        st_check("v6.4 SIM boot push: sub1 len30 (no start/trigger words) -> ACK -> sub112 len3 = the stock vector -> done",okA&&okB&&bp.state==BP_DONE&&bp.rb==BPR_OK&&wq.n==0&&wq.n_ok==2&&wq.n_block==0);
        /* config out of spec: the WHOLE sub1 block is rejected by the frame policy and counted; nothing is sent */
        SET[S_BREWT].val=1200; memset(&wq,0,sizeof wq); wq.sim_ack=1; txq_len=0; boot_push_start(0); boot_push_step(T+200);
        int blk=bp.ra==BPR_BLOCKED&&bp.state==BP_WAIT_B&&wq.n==0&&wq.n_block==1&&wq_rej==WJ_RANGE&&txq_len==0;
        boot_push_step(T+200); blk&=(bp.state==BP_B&&wq.q[0].sub==112); wq_step(T+200); wq_step(T+250); boot_push_step(T+250);
        SET[S_BREWT].val=bt0;
        st_check("v6.4 SIM boot push: brewTemper=1200 -> sub1 len30 REJECTED by the frame policy (BLK +1, 0 bytes sent); sub112 len3 is sent",blk&&bp.state==BP_DONE&&bp.rb==BPR_OK);
        /* no ACK: the block fails after one retransmit, the next block is sent anyway */
        memset(&wq,0,sizeof wq); txq_len=0; boot_push_start(0); boot_push_step(T+400); wq_step(T+400); wq_step(T+550); wq_step(T+860); boot_push_step(T+860);
        int na=bp.ra==BPR_NOACK&&bp.state==BP_WAIT_B&&wq.n_retx==1&&wq.n_fail==1; boot_push_step(T+860); na&=(bp.state==BP_B&&wq.n==1&&wq.q[0].sub==112);
        st_check("v6.4 SIM boot push: without ACK -> 1 retransmit, FAILED, and sub112 len3 is sent anyway",na);
        st_check("v6.5: the write queue's block limit is 30 words in ALL build types (the v6.4 device: 24)",WQ_WMAX==30&&sizeof(((WFrame*)0)->w)==60&&sizeof last_tx>=2*WQ_WMAX+7);
        wq=wq0; memset(&bp,0,sizeof bp); last_rx_ms=rx0; reg[33]=ws0; txq_len=tx0; } }
    st_bootpush_device(cfgsrc);
    st_rx_resync();

    printf("== selftest: settings table, limits and editor ==\n");
    { int ok=1; static const char*const gone[]={"brewMode","sleepMode","sleepModeSetting","ssid","password","wifionoff","theme","presetStore","init","patch"};
      for(int i=0;i<NSET;i++) for(unsigned q=0;q<sizeof gone/sizeof gone[0];q++) if(SET[i].key&&!strcmp(SET[i].key,gone[q])) ok=0;
      st_check("not in the table: brewMode, sleepMode, sleepModeSetting, wifi fields, theme, presetStore",ok); }
    st_check("hot water TEMPERATURE is a sensor (word 40): no config key, no write, locked",SET[S_HOTT].key==NULL&&SET[S_HOTT].mcu==M_NONE&&(SET[S_HOTT].flags&SF_SENSOR)&&(SET[S_HOTT].flags&SF_LOCK)&&SET[S_HOTT].def==40);
    st_check("breakTime is locked (no editor) but is still packed into word113 lo",(SET[S_BREAK].flags&SF_LOCK)&&SET[S_BREAK].lo==SET[S_BREAK].hi&&SET[S_BREAK].mcu==M_W113);
    st_check("language unlocked 0..10 (11 names); time zone still locked",!(SET[S_LANG].flags&SF_LOCK)&&SET[S_LANG].lo==0&&SET[S_LANG].hi==10&&sizeof OPT_LANG/sizeof OPT_LANG[0]==11&&(SET[S_TZ].flags&SF_LOCK));
    { int ok=1; for(int i=0;i<NSET;i++){ Setting*s=&SET[i]; if(s->kind==K_ENUM&&(s->flags&SF_LOCK)) ok=0; if((s->flags&SF_EST)&&!s->hint) ok=0;
          if(!(s->flags&(SF_LOCK|SF_SENSOR))&&((s->max-s->min)%s->step||s->min%s->step)) ok=0; }
      st_check("table invariants: estimates have a hint, max lies on the step grid",ok); }
    { int ok=1; for(int i=0;i<NSET;i++){ Setting*s=&SET[i]; if(set_oor(s)){ printf("     %s=%d outside %d..%d\n",s->key,s->val,s->lo,s->hi); ok=0; } if(!(s->flags&SF_SENSOR)&&!s->loaded){ printf("     %s missing from config\n",s->key); ok=0; } }
      st_check("all config values are loaded and within the spec limits",ok); }
    { int ok=1; view=V_DETAIL;
      sel=S_BREWT; edit_val=960; edit_step(1,1); ok&=edit_val==960; edit_val=850; edit_step(-1,1); ok&=edit_val==850; edit_val=856; edit_step(-1,1); ok&=edit_val==850; edit_val=856; edit_step(1,1); ok&=edit_val==860;
      sel=S_HOTW; edit_val=400; edit_step(1,1); ok&=edit_val==500; edit_step(1,5); ok&=edit_val==1000; edit_val=5000; edit_step(1,5); ok&=edit_val==5000; edit_val=300; edit_step(-1,5); ok&=edit_val==300;
      sel=S_GRINDW; edit_val=309; edit_step(1,1); ok&=edit_val==309; edit_val=10; edit_step(-1,1); ok&=edit_val==10; edit_val=185; edit_step(1,1); ok&=edit_val==186;
      sel=S_BREWW; edit_val=360; edit_step(1,1); ok&=edit_val==370; edit_val=990; edit_step(1,1); ok&=edit_val==990;
      sel=S_PIPRESS; edit_val=4; edit_step(1,1); ok&=edit_val==4; edit_step(-1,1); ok&=edit_val==3; edit_step(-1,1); ok&=edit_val==3;
      sel=S_BREAK; edit_val=5; edit_step(1,1); ok&=edit_val==5; sel=S_TZ; edit_val=48; edit_step(1,1); ok&=edit_val==48; sel=S_HOTT; edit_val=900; edit_step(1,1); ok&=edit_val==900;
      sel=-1; view=V_MAIN; st_check("stepper: clamp to the spec interval, step grid, locked rows cannot be changed",ok); }
    for(int lg=0;lg<NLANG;lg++){ /* layout: all texts in the detail view lie within ring/chord (otherwise they are silently clipped) — both languages */
      int ok=1, nell=0; char l[112]; lang_set(lg);
      int ktu=SET[S_TUNIT].val;
      for(int i=0;i<NSET;i++) for(int tu=0;tu<2;tu++){ const Setting*s=&SET[i]; const char*lab=TR(s->label);
          if(tu&&!(s->flags&(SF_TEMP10|SF_TEMP1))) continue;                 /* temperature rows: both °C and °F */
          SET[S_TUNIT].val=(s->flags&(SF_TEMP10|SF_TEMP1))?tu:ktu; detail_text(i,l,sizeof l);
          char iv[200]; snprintf(iv,sizeof iv,"%s%s%s",l,(s->hint&&!(s->flags&SF_LOCK))?" · ":"",(s->hint&&!(s->flags&SF_LOCK))?TR(s->hint):"");
          LAY(text_wf(lab,F_S),2*ring_half(82,14)-12,"detail label",lab);
          LAY(text_wf(iv,F_XS),2*ring_half(106,11)-12,"interval+hint (inside the gauge ring)",iv);
          LAY(text_wf(TR(s->note),F_XS),KB[KNOTE].maxw,"note",TR(s->note));
          if(detail_holdable(s)) LAY(text_wf(TR(TX_D_HOLDFAST),F_XS),KB[KST].maxw,"hold hint (status line)",TR(TX_D_HOLDFAST));
          char vb[40]; int xr=(s->flags&SF_LOCK)?388:372;
          if(s->kind==K_ENUM){
              LAY(94+text_w(lab,F_S)+12+60,xr,"list row: label leaves < 60 px for the value",lab);
              for(int v=s->min;v<=s->max;v++){ set_fmt(i,v,vb,sizeof vb-4); LAY(text_w(vb,F_M),228,"enum-hero (F_M)",vb);
                  int room; if(row_val_font(lab,vb,xr,&room)==F_XS&&text_w(vb,F_XS)>room){ nell++; char full[40]; strcpy(full,vb); fit_text(vb,F_XS,room); LAY(text_w(vb,F_XS),room,"truncated enum name",vb);
                      printf("     [%s] %s: '%s' is shown truncated in the list as '%s' (full name in the detail)\n",LANG_CODE[ui_lang],lab,full,vb); } }
          } else {   /* number row: label + widest value + unit (+ estimate marker) */
              const char*u=set_unit(i); int uw=*u?text_w(u,F_XS)+6:0, vw=0;
              int probe[3]={s->lo,s->hi,(s->flags&SF_SENSOR)?1200:s->val}; for(int q=0;q<3;q++){ int kl=s->loaded; ((Setting*)s)->loaded=1; set_fmt(i,probe[q],vb,sizeof vb); ((Setting*)s)->loaded=kl; if(text_w(vb,F_M)>vw) vw=text_w(vb,F_M); }
              LAY(94+text_w(lab,F_S)+12+vw+uw+((s->hint&&!(s->flags&SF_LOCK))?18:0),xr,"list row (label + value + unit)",lab);
              set_fmt(i,s->hi,vb,sizeof vb); LAY(text_wf(vb,F_M)+(*u?text_wf(u,F_M)+10:0),228,"number hero (smallest font F_M)",vb);
          } }
      SET[S_TUNIT].val=ktu;
      char nm[200]; snprintf(nm,sizeof nm,"layout: label, interval+hint, note, hold hint, hero and list row fit for all %d rows (%d enum name(s) truncated in the list)",NSET,nell);
      st_check(stl(nm),ok&&nell<=1); }
    lang_set(base_lang);

    printf("== selftest: config persist (atomic, byte-exact, md5 log) ==\n");
    char dir[300]; snprintf(dir,sizeof dir,"/tmp/merakiplus_v6_selftest_%d",(int)getpid()); mkdir(dir,0755);
    snprintf(cfg_path,sizeof cfg_path,"%s/config.json",dir); snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s/cfgbak",dir);
    st_check("copy of config.json",copy_file(cfgsrc,cfg_path));
    cfg_read(); cfg_writes=0; char m0[33]; strcpy(m0,cfg_md5); char orig[CFG_MAX]; int on=cfglen; memcpy(orig,cfgbuf,on);
    printf("     md5 baseline = %s (%d bytes)\n",m0,on);
    st_check("brewTemper 870 -> 880",cfg_set_int("brewTemper",880));
    int diff=0; if(cfglen!=on) diff=-1; else for(int i=0;i<on;i++) if(cfgbuf[i]!=orig[i]) diff++;
    st_check("exactly 1 byte changed, same length (number token only)",diff==1);
    char m1[33]; strcpy(m1,cfg_md5); printf("     md5 after    = %s\n",m1);
    char p[400]; char bb[CFG_MAX]; snprintf(p,sizeof p,"%s/config.json.orig",cfg_bakdir); int fd=open(p,O_RDONLY); int bn=fd>=0?read(fd,bb,sizeof bb):-1; if(fd>=0)close(fd);
    st_check("pristine backup == baseline",bn==on&&!memcmp(bb,orig,on));
    st_check("volume 31 -> 100 (longer token)",cfg_set_int("volume",100)&&cfglen==on+1);
    st_check("volume 100 -> 31",cfg_set_int("volume",31));
    st_check("hotWaterMode (field after presetStore) 1 -> 2 -> 1",cfg_set_int("hotWaterMode",2)&&cfg_set_int("hotWaterMode",1));
    st_check("unknown field rejected (merakiplus never adds keys)",!cfg_set_int("doesNotExist",1)&&!cfg_set_int("theme",0));
    st_check("string field (ssid) rejected",!cfg_set_int("ssid",1));
    st_check("brewTemper 880 -> 870 (revert)",cfg_set_int("brewTemper",870));
    st_check("md5 after revert == baseline",!strcmp(cfg_md5,m0));
    /* the whole table: every editable field is saved as min, max and back */
    { int ok=1,nf=0; for(int i=0;i<NSET;i++){ Setting*s=&SET[i]; if(s->flags&(SF_LOCK|SF_SENSOR|SF_LOCAL|SF_OWN|SF_SND)) continue; int o; nf++;
          if(!cfg_get_int(s->key,&o)||!cfg_set_int(s->key,s->min)||!cfg_set_int(s->key,s->max)||!cfg_set_int(s->key,o)){ ok=0; printf("     %s: %s\n",s->key,cfg_err); } }
      char nm[120]; snprintf(nm,sizeof nm,"all %d editable fields: save min -> max -> original",nf); st_check(nm,ok&&nf==21);
      st_check("md5 after a full table round trip == baseline",!strcmp(cfg_md5,m0)&&cfglen==on&&!memcmp(cfgbuf,orig,on)); }
    snprintf(p,sizeof p,"%s/config.json.orig",cfg_bakdir); fd=open(p,O_RDONLY); bn=fd>=0?read(fd,bb,sizeof bb):-1; if(fd>=0)close(fd);
    st_check("pristine backup still untouched",bn==on&&!memcmp(bb,orig,on));
    snprintf(p,sizeof p,"%s/.merakiplus_cfg.tmp",dir); st_check("no tmp file left behind",access(p,F_OK)!=0);
    /* a key hidden inside presetStore must not be mistaken for the top level */
    const char*nest="{\n\t\"presetStore\":\t{\n\t\t\"a\":\t{\"brewTemper\":\t111}\n\t},\n\t\"brewTemper\":\t870\n}";
    int a,e2; st_check("nested key ignored (depth 1)",cfg_find(nest,strlen(nest),"brewTemper",&a,&e2)&&atoi(nest+a)==870);
    snprintf(p,sizeof p,"%s/config_writes.log",cfg_bakdir); FILE*lf=fopen(p,"r"); int lines=0; char ln[400];
    if(lf){ while(fgets(ln,sizeof ln,lf)){ lines++; if(lines<=2||lines==6) printf("     log: %s",ln); } fclose(lf); }
    { char nm[80]; snprintf(nm,sizeof nm,"md5 log has one line per write (%d)",cfg_writes); st_check(nm,lines==cfg_writes&&lines>=6+63); }
    snprintf(p,sizeof p,"%s/RESTART_NEEDED",cfg_bakdir); st_check("RESTART_NEEDED flag written",access(p,F_OK)==0);

    printf("== selftest: config out of spec (changed bypassing the stock UI) ==\n");
    { char op[400]; snprintf(op,sizeof op,"%s/oor.json",dir); char keepb[256]; strcpy(keepb,cfg_bakdir);
      int ok=copy_file(cfgsrc,op); snprintf(cfg_path,sizeof cfg_path,"%s",op); snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s/oorbak",dir);
      ok&=cfg_set_int("brewTemper",990)&&cfg_set_int("headTemper",90)&&cfg_set_int("preInfusionTime",20)&&cfg_set_int("preInfusionPressure",6)&&cfg_set_int("grindMode",0)&&cfg_set_int("language",11);
      st_check("test copy with brewTemper=990, headTemper=90, preInfusionTime=20, pressure=6, grindMode=0, language=11",ok);
      settings_load(); char vb[24];
      st_check("a proven limit is NOT widened: brewTemper 990 is marked out of spec (850..960)",set_oor(&SET[S_BREWT])&&SET[S_BREWT].hi==960);
      st_check("an estimated max is widened to the stock-saved value: group temp 90 (<= 100), pre-inf. time 20 (<= 60)",!set_oor(&SET[S_HEADT])&&SET[S_HEADT].hi==90&&!set_oor(&SET[S_PITIME])&&SET[S_PITIME].hi==20);
      set_fmt(S_GRINDMODE,sv(S_GRINDMODE),vb,sizeof vb); int e1=!strcmp(vb,"(0)"); set_fmt(S_LANG,sv(S_LANG),vb,sizeof vb);
      st_check("an enum outside the table is shown as a raw number — no lookup outside the name table",e1&&!strcmp(vb,"(11)")&&set_oor(&SET[S_GRINDMODE])&&set_oor(&SET[S_LANG])&&set_oor(&SET[S_PIPRESS]));
      sim_ready(9000); memset(&wq,0,sizeof wq); memset(&sav,0,sizeof sav); act.pending=ACT_NONE;
      const char*g=act_guard(ACT_BREW,9000); st_check("activity start blocked: CONFIG OUT OF SPEC",g&&g==TR(TX_G_OOR)&&act_guard(ACT_STEAM,9000)==g);
      st_check("the frame policy rejects the brew block even if the UI guard is bypassed",!act_push(ACT_BREW)&&wq_rej==WJ_RANGE&&wq.n==0);
      view=V_DETAIL; sel=S_BREWT; edit_val=sv(S_BREWT); edit_step(-1,1); int e3=edit_val==960; edit_val=sv(S_BREWT); edit_step(1,1); e3&=edit_val==960; sel=-1; view=V_MAIN;
      st_check("the stepper brings an out-of-spec value into the interval (990 -> 960)",e3);
      ok=cfg_set_int("headTemper",120); settings_load();
      st_check("an estimate above the cap is not widened: group temp 120 > 100 is out of spec",ok&&set_oor(&SET[S_HEADT])&&SET[S_HEADT].hi==80);
      strcpy(cfg_bakdir,keepb); snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); settings_load(); }
    /* ============================ v6.2 ============================ */
    printf("== selftest: boot slot + BOOT UI flag (mock root) ==\n");
    char root[360]; snprintf(root,sizeof root,"%s/root",dir); snprintf(sysroot,sizeof sysroot,"%s",root);
    char keepbak[256]; strcpy(keepbak,cfg_bakdir); snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s/bootbak",dir);
    char fl[440],bl[440],rn[440],cdir[440],tx[400];
    snprintf(fl,sizeof fl,"%s%s",root,BOOT_FLAG); snprintf(bl,sizeof bl,"%s/boot_ui.log",cfg_bakdir);
    snprintf(rn,sizeof rn,"%s/RESTART_NEEDED",cfg_bakdir); snprintf(cdir,sizeof cdir,"%s/customer",root);
    bs.self_pid=MOCK_SELF_PID;
    mock_reset(root); mock_slot_b(root,0,MB_NONE); mock_self(root,"/new_customer/merakiplus/merakiplus"); boot_refresh();
    st_check("mock root: the flag path is <root>/customer/new; the stock binary exists in slot A",!strcmp(rp(BOOT_FLAG),fl)&&bs.a_bin);
    st_check("slot B not mounted: LOCKED — touch rejected, the flag is not created",bs.lock==BL_NOMOUNT&&!boot_flag_set(1)&&access(fl,F_OK)!=0&&strstr(boot_err,"not mounted"));
    mock_slot_b(root,1,MB_NONE);
    st_check("mounted, but demo_v9 missing in slot B: LOCKED",!boot_flag_set(1)&&bs.lock==BL_NOBIN&&access(fl,F_OK)!=0);
    mock_slot_b(root,1,MB_OURS_NOEXEC);
    st_check("merakiplus in slot B without the x bit (cannot boot): LOCKED",!boot_flag_set(1)&&bs.lock==BL_NOBIN&&access(fl,F_OK)!=0);
    mock_slot_b(root,1,MB_VENDOR); mock_flag(root,1);
    { int a=boot_flag_set(0), e1=access(fl,F_OK)==0, b2=boot_flag_set(1);
      st_check("vendor binary in slot B: LOCKED in BOTH directions — the vendor's OTA flag is not touched; the next boot is shown as SLOT B",!a&&!b2&&e1&&bs.lock==BL_VENDOR&&boot_next_kind()==BN_SLOTB&&boot_next_name()==TR(TX_BN_SLOTB)); }
    mock_flag(root,0); mock_slot_b(root,1,MB_OURS); boot_refresh();
    int nc0=dir_count(cdir);
    st_check("merakiplus in slot B + mounted: UNLOCKED; no flag = ORIGINAL on the next boot",bs.lock==BL_OK&&!bs.flag&&boot_next_kind()==BN_ORIGINAL);
    { struct stat fs; int ok=boot_flag_set(1)&&stat(fl,&fs)==0&&S_ISREG(fs.st_mode)&&fs.st_size==0&&bs.flag==1&&dir_count(cdir)==nc0+1;
      st_check("choose MERAKIPLUS: touch /customer/new (empty file) — nothing else in /customer changed",ok&&boot_next_kind()==BN_OURS&&!strcmp(boot_next_name(),PRODUCT)); }
    st_check("choose MERAKIPLUS again: idempotent",boot_flag_set(1)&&bs.flag==1&&dir_count(cdir)==nc0+1);
    st_check("choose ORIGINAL: rm /customer/new — /customer is exactly as before",boot_flag_set(0)&&access(fl,F_OK)!=0&&!bs.flag&&dir_count(cdir)==nc0);
    st_check("choose ORIGINAL again: idempotent (a missing file is not an error)",boot_flag_set(0)&&!bs.flag);
    { chmod(cdir,0555); int r1=boot_flag_set(1), f1=bs.flag, e1=boot_err[0]!=0; chmod(cdir,0755);
      mock_flag(root,1); boot_refresh(); chmod(cdir,0555); int r2=boot_flag_set(0), f2=bs.flag; chmod(cdir,0755); mock_flag(root,0); boot_refresh();
      if(geteuid()==0) printf("     (running as root — the write-protection test is not meaningful, skipped)\n");
      else st_check("read-only /customer (§9.1): the error is reported, the flag and the displayed state are unchanged — in both directions",!r1&&!f1&&e1&&!r2&&f2==1); }
    st_check("boot_ui.log: one line per ACTUAL change (2), none for the idempotent calls",file_lines(bl)==2&&file_has(bl,"created")&&file_has(bl,"removed"));
    file_print(bl,"     log: ");
    { mock_flag(root,1); boot_refresh(); int a=bs.flag; mock_flag(root,0); boot_refresh();
      st_check("the flag state is read live (stat): an external change (telnet touch/rm) is seen",a==1&&bs.flag==0); }
    boot_flag_set(1); mock_slot_b(root,0,MB_OURS);
    st_check("slot B disappears after the flag is set: LOCKED, the flag is not removed behind the user's back; the next boot is shown honestly as ORIGINAL",!boot_flag_set(0)&&access(fl,F_OK)==0&&bs.lock==BL_NOMOUNT&&boot_next_kind()==BN_ORIGINAL);
    mock_slot_b(root,1,MB_OURS); boot_flag_set(0);
    mock_slot_b(root,1,MB_V61); boot_refresh(); st_check("v6.1 binary in slot B is recognised (the exit string) -> unlocked",bs.lock==BL_OK);
    mock_slot_b(root,1,MB_OURS_EDGE); boot_refresh(); st_check("a marker straddling the 64 KiB read boundary is still found",bs.lock==BL_OK);
    { char vb[400]; snprintf(vb,sizeof vb,"%s",cfgsrc); char*q=strrchr(vb,'/'); char*t=q?q+1:vb; snprintf(t,sizeof vb-(size_t)(t-vb),"demo_v9");
      if(access(vb,R_OK)==0){ bs.m_valid=0; st_check("the real vendor binary (artifacts/demo_v9) is NOT recognised as merakiplus",!bin_is_merakiplus(vb)); bs.m_valid=0; }
      else printf("     (no demo_v9 next to config — check of the real vendor binary skipped)\n"); }
    mock_slot_b(root,1,MB_OURS);
    { mock_self(root,SLOT_B_EXE); boot_refresh(); int sB=bs.self_slot; mock_self(root,SLOT_A_EXE); boot_refresh(); int sA=bs.self_slot; mock_self(root,"/new_customer/merakiplus/merakiplus"); boot_refresh();
      st_check("exe slot via /proc/self/exe: /new_customer/demo_v9 = B, /customer/demo_v9 = A, anything else = run.sh/manual",sB==SLOT_B&&sA==SLOT_A&&bs.self_slot==SLOT_OTHER&&!strcmp(bs.self_exe,"/new_customer/merakiplus/merakiplus")); }
    st_check("no stock process, no switcher -> a switch would START stock (launch)",bs.n_stock==0&&bs.sw_pid==0&&switch_mode()==SWM_LAUNCH);
    mock_proc(root,MOCK_STOCK_PID,SLOT_A_EXE,"demo_v9",'T',40); mock_proc(root,MOCK_SW_PID,"/new_customer/merakiplus/boot/switcher","switcher",'S',12);
    mock_proc(root,MOCK_SELF_PID,SLOT_A_EXE,"demo_v9",'R',95); mock_proc(root,77,"/bin/busybox","sh",'S',3); boot_refresh();
    st_check("SIGSTOPped stock process + switcher found (pidof); own pid and foreign processes do not count -> switch = SIGCONT only",bs.n_stock==1&&bs.stock[0]==MOCK_STOCK_PID&&bs.stock_run==0&&bs.sw_pid==MOCK_SW_PID&&switch_mode()==SWM_CONT);
    { mock_proc(root,MOCK_STOCK_PID,SLOT_A_EXE,"demo_v9",'S',40); boot_refresh(); int runs=bs.stock_run;
      mock_proc(root,MOCK_STOCK_PID,SLOT_A_EXE " (deleted)","demo_v9",'T',40); boot_refresh();
      st_check("a RUNNING stock UI next to merakiplus is flagged; exe '... (deleted)' is still stock",runs==1&&bs.n_stock==1&&bs.stock_run==0); }
    mock_proc(root,MOCK_STOCK_PID,SLOT_B_EXE,"demo_v9",'S',40); boot_refresh();
    st_check("a demo_v9 from slot B is not stock",bs.n_stock==0);
    { mock_swlog(root,NULL); boot_refresh(); int g0=bs.sw_log;
      mock_swlog(root,"[2026-11-17 12:40:41 UTC] switcher started (pid 301)\n[2026-11-17 12:40:41 UTC] input: /dev/input/event0\n"); boot_refresh(); int g1=bs.sw_log;
      mock_swlog(root,MOCK_SWLOG); boot_refresh();
      st_check("switcher.log: missing -> 'not readable'; no switch -> 'no event'; otherwise the last switch line with time of day",g0==0&&g1==1&&bs.sw_log==2&&!strncmp(bs.sw_evt,"12:41:07 switch A",17)&&strstr(bs.sw_evt,"no reboot"));
      printf("     event: %s\n",bs.sw_evt);
      mock_swlog(root,"[2026-11-17 12:41:07 UTC] skift A\xE2\x86\x92" "B F\xC3\x86RDIG: merakiplus k\xC3\xB8rer, stock-A exit'et, ingen reboot\n"); boot_refresh();
      st_check("switcher.log from a pre-English switcher build ('skift …'): still recognised as a switch event",bs.sw_log==2&&!strncmp(bs.sw_evt,"12:41:07 skift A",16)); }
    { static char big[9000]; int o=0,k=0; while(o<6000) o+=snprintf(big+o,sizeof big-o,"[2026-11-17 12:50:%02d UTC] gesture: valid corner long-press counted (1/3)\n",(k++)%60);
      o+=snprintf(big+o,sizeof big-o,"[2026-11-17 12:59:59 UTC] switch FAILED: B (pid 9) did not come up \xE2\x80\x94 fallback: SIGCONT A, kill B\n[2026-11-17 13:00:00 UTC] input: POLLERR/HUP \xE2\x80\x94 reopening\n");
      mock_swlog(root,big); boot_refresh(); st_check("large log (> 4 KiB): only the tail is read, the last switch line is found",bs.sw_log==2&&!strncmp(bs.sw_evt,"12:59:59 switch FAILED",22)); }
    { char o[512]; int ok=1;
      ld_path_for_a(NULL,o,sizeof o); ok&=!strcmp(o,"/customer/libs:/config/wifi");
      ld_path_for_a("",o,sizeof o); ok&=!strcmp(o,"/customer/libs:/config/wifi");
      ld_path_for_a(":/new_customer/libs:/config/wifi",o,sizeof o); ok&=!strcmp(o,"/customer/libs:/config/wifi");
      ld_path_for_a("/new_customer/libs:/config/wifi",o,sizeof o); ok&=!strcmp(o,"/customer/libs:/config/wifi");
      ld_path_for_a("/lib:/config/lib:/new_customer/libs:/config/wifi",o,sizeof o); ok&=!strcmp(o,"/lib:/config/lib:/customer/libs:/config/wifi");
      ld_path_for_a("/lib:/customer/libs:/config/wifi",o,sizeof o); ok&=!strcmp(o,"/lib:/customer/libs:/config/wifi");
      ld_path_for_a("/a'b:/lib:/x y",o,sizeof o); ok&=!strcmp(o,"/lib:/customer/libs:/config/wifi");
      st_check("LD_LIBRARY_PATH for slot A: B's libs out, system prefix kept, A's libs + /config/wifi last (like demo.sh's A branch)",ok); }

    printf("== selftest: the exit reminder — when is it true? ==\n");
#define RN_PUT(txt) do{ FILE*rf=fopen(rn,"w"); if(rf){ fputs(txt,rf); fclose(rf); } }while(0)
    char bid[64]; boot_id_read(bid,sizeof bid);
    unlink(rn); mock_proc(root,MOCK_STOCK_PID,SLOT_A_EXE,"demo_v9",'T',40); boot_refresh();
    st_check("no RESTART_NEEDED -> no reminder",bid[0]&&!restart_pending());
    snprintf(tx,sizeof tx,"config.json changed by merakiplus.\nboot_id=%s mono_s=5000\n",bid); RN_PUT(tx);
    st_check("stock UI started (t=40 s) BEFORE the latest SAVE (t=5000 s), same boot -> reminder",restart_pending());
    mock_proc(root,MOCK_STOCK_PID,SLOT_A_EXE,"demo_v9",'T',6000); boot_refresh();
    st_check("stock UI started AFTER the latest SAVE -> no reminder (it has read config itself)",!restart_pending());
    mock_proc(root,MOCK_STOCK_PID,SLOT_A_EXE,"demo_v9",'T',40); boot_refresh();
    RN_PUT("config.json changed by merakiplus.\nboot_id=aaaaaaaa-0000-0000-0000-000000000000 mono_s=5000\n");
    st_check("the machine has rebooted since SAVE (different boot_id) -> no reminder",!restart_pending());
    RN_PUT("config.json aendret af merakiplus. (v6.1 format, no stamp)\n");
    st_check("v6.1 flag without a stamp -> assume the reminder applies",restart_pending());
    mock_proc_rm(root,MOCK_STOCK_PID); boot_refresh();
    st_check("no stock UI alive (slot B boot) -> no reminder, regardless of the flag",!restart_pending());
    { char cp[440],kp[256]; snprintf(cp,sizeof cp,"%s/config_rn.json",dir); copy_file(cfgsrc,cp); strcpy(kp,cfg_path); snprintf(cfg_path,sizeof cfg_path,"%s",cp);
      mono_fake=7777; int ok=cfg_set_int("volume",32); mono_fake=-1; snprintf(tx,sizeof tx,"boot_id=%s mono_s=7777\n",bid); ok&=file_has(rn,tx); strcpy(cfg_path,kp);
      st_check("SAVE stamps RESTART_NEEDED with boot_id + monotonic time",ok); }

    printf("== selftest: runtime switch B->A per BOOT.md §9.2 (mock root; 'stock' is a copy of this test binary) ==\n");
    { char ss[440],rel[440],real[700]="",want[800],er[72]=""; ms_t tn=20000; int pp[2]={-1,-1};
      snprintf(ss,sizeof ss,"%s/tmp/stock_started",root); snprintf(rel,sizeof rel,"%s/tmp/released",root);
#define SW_PREP() do{ sim_ready(tn); memset(&wq,0,sizeof wq); memset(&sav,0,sizeof sav); act.pending=ACT_NONE; view=V_MAIN; page=PG_SYS; sec_open=SEC_STOCK; sel=-1; xsel=-1; tr.type=0; run=1; toast.dur=0; rip.on=0; \
                      sw_child=0; sw_seq=sw_seq_frame=sw_seq_release=sw_seq_launch=0; st_rel=st_reacq=0; unlink(ss); unlink(rel); }while(0)
      st_relfile=rel; dev_release=st_release_hook; dev_reacquire=st_reacq_hook; sw_sim=0; sw_dry=0;
      if(!self_bin||!strchr(self_bin,'/')||access(self_bin,X_OK)!=0){ printf("     (own binary path unknown — launch tests skipped)\n"); st_check("launch test could run",0); }
      else {
        /* --- successful switch --- */
        mock_boot_b(root); snprintf(tx,sizeof tx,"%s%s",root,SLOT_A_EXE); int cp=copy_big(self_bin,tx,0755);
        if(realpath(cdir,real)==NULL) snprintf(real,sizeof real,"%s",cdir);
        mkdir(cfg_bakdir,0755); snprintf(tx,sizeof tx,"x\nboot_id=%s mono_s=5000\n",bid); RN_PUT(tx); unlink(bl);
        setenv("MERAKIPLUS_MOCK_STOCK",ss,1); setenv("MERAKIPLUS_MOCK_RELEASED",rel,1); setenv("LD_LIBRARY_PATH","/lib:/config/lib:/new_customer/libs:/config/wifi",1);
        if(pipe(pp)!=0){} fcntl(pp[0],F_SETFL,O_NONBLOCK); st_relfd=pp[1];          /* pp[1] plays merakiplus's UART/fb/touch fd */
        SW_PREP(); sys_open(X_SWITCH,tn); tr.type=0;
        double t0=msnow(); sys_confirm(tn); double el=msnow()-t0;
        st_check("mock stock placed as <root>/customer/demo_v9; SWITCH NOW completed: run=0, last frame shown, stock pid known",cp&&run==0&&view==V_BYE&&bye_mode==SWM_LAUNCH&&sw_child>0);
        st_check("order (§9.2): last frame -> step 1 release UART/touch/fb -> step 2 start stock; fb is not taken back",sw_seq_frame==1&&sw_seq_release==2&&sw_seq_launch==3&&st_rel==1&&st_reacq==0);
        { char nm[160]; snprintf(nm,sizeof nm,"step 3: merakiplus waits %d ms before exit (measured %.0f ms; requirement 300-500 ms)",SW_WAIT_MS,el); st_check(nm,el>=300&&el<=520&&SW_WAIT_MS>=300&&SW_WAIT_MS<=500); }
        snprintf(want,sizeof want,"pid=%d sid=%d argv0=./demo_v9 cwd=%s extra_fds=0 stdio_null=1 released_before_start=1\n",sw_child,sw_child,real);
        st_check("the stock process: own session (setsid), argv0 = ./demo_v9, cwd = /customer, stdio = /dev/null, 0 inherited fds, started AFTER step 1",file_has(ss,want));
        st_check("LD_LIBRARY_PATH for stock: B's libs out, prefix kept, A's libs last",file_has(ss,"\nLD=/lib:/config/lib:/customer/libs:/config/wifi\n"));
        { char c1; int r=(int)read(pp[0],&c1,1); st_check("merakiplus's 'device fd' is closed everywhere (EOF on the pipe) — touch grab/fb/UART cannot survive into the stock UI",r==0); }
        st_check("step 4: the flag /customer/new is untouched by the runtime switch",access(fl,F_OK)==0);
        st_check("step 6: boot_ui.log has the switch with time + reason=user-button; RESTART_NEEDED removed (stock started fresh)",file_has(bl,"RUNTIME SWITCH B->A: reason=user-button, stock UI pid")&&file_has(bl,"RESTART_NEEDED removed")&&access(rn,F_OK)!=0);
        file_print(ss,"     mock-stock: "); file_print(bl,"     boot_ui.log: ");
        { int ws=0; waitpid(sw_child,&ws,0); st_check("mock stock kept running after merakiplus's 'exit' and ended by itself (rc 0) — no signals sent",WIFEXITED(ws)&&WEXITSTATUS(ws)==0); }
        close(pp[0]);
        /* without an inherited prefix: exactly the BOOT.md value */
        unsetenv("LD_LIBRARY_PATH"); SW_PREP(); st_relfd=-1; sys_open(X_SWITCH,tn); tr.type=0; sys_confirm(tn);
        st_check("without an inherited path stock gets EXACTLY LD_LIBRARY_PATH=/customer/libs:/config/wifi (BOOT.md §9.2)",run==0&&sw_child>0&&file_has(ss,"\nLD=/customer/libs:/config/wifi\n"));
        if(sw_child>0) waitpid(sw_child,NULL,0);
        /* --- stock dies immediately: merakiplus must NOT exit --- */
        setenv("MERAKIPLUS_MOCK_STOCK_FAIL","1",1); RN_PUT("x\n"); unlink(bl);
        SW_PREP(); sys_open(X_SWITCH,tn); tr.type=0; sys_confirm(tn);
        st_check("stock UI dies immediately: merakiplus does NOT exit, takes UART/fb/touch back, shows the error; RESTART_NEEDED untouched",run==1&&view==V_SYS&&xsel==X_SWITCH&&sw_child==0&&st_rel==1&&st_reacq==1&&sw_fail==SWF_DIED&&!strcmp(toast.msg,TR(TX_T_SW_FAIL_DIED))&&access(rn,F_OK)==0&&file_has(bl,"RUNTIME SWITCH FAILED"));
        unsetenv("MERAKIPLUS_MOCK_STOCK_FAIL");
        /* --- MERAKIPLUS_SWITCH_DRY --- */
        sw_dry=1; unlink(bl); SW_PREP(); sys_open(X_SWITCH,tn); tr.type=0; sys_confirm(tn); sw_dry=0;
        st_check("MERAKIPLUS_SWITCH_DRY=1: nothing is released, nothing is started, merakiplus keeps running; the log says what WOULD happen",run==1&&st_rel==0&&sw_child==0&&access(ss,F_OK)!=0&&file_has(bl,"DRY-RUN: would close UART/touch/fb")&&!strcmp(toast.msg,TR(TX_T_DRYRUN)));
        /* --- run.sh mode: stock exists (paused) -> nothing is started, no signal; clean exit --- */
        mock_runsh(root,6000); snprintf(tx,sizeof tx,"%s%s",root,SLOT_A_EXE); copy_big(self_bin,tx,0755); unlink(bl); unlink(rn);
        SW_PREP(); sys_open(X_SWITCH,tn); tr.type=0; sys_confirm(tn);
        st_check("run.sh mode: SWITCH NOW = clean exit like REVERT — NOTHING is started (never two demo_v9), run.sh SIGCONTs",run==0&&view==V_BYE&&bye_mode==SWM_CONT&&sw_child==0&&st_rel==0&&access(ss,F_OK)!=0&&file_has(bl,"run.sh mode"));
        unsetenv("MERAKIPLUS_MOCK_STOCK"); unsetenv("MERAKIPLUS_MOCK_RELEASED");
      }
      /* --- the stock binary is missing entirely --- */
      mock_boot_b(root); mock_rm(root,SLOT_A_EXE); boot_refresh();
      st_check("/customer/demo_v9 missing: switch_launch refuses before fork",switch_locked()&&!switch_launch(er,sizeof er)&&strstr(er,"missing")&&sw_fail==SWF_NOBIN);
      dev_release=NULL; dev_reacquire=NULL; st_relfile=NULL; st_relfd=-1; view=V_MAIN; xsel=-1; run=1; sw_child=0; }

    printf("== selftest: UI routes (SWITCH NOW, BOOT UI; v6.8: no REVERT button) ==\n");
    { ms_t tn=20000; int ns;
      sw_sim=1; sim_ready(tn); memset(&wq,0,sizeof wq); memset(&sav,0,sizeof sav); act.pending=ACT_NONE; view=V_MAIN; page=PG_HOME; sel=-1; xsel=-1; tr.type=0; run=1; toast.dur=0; rip.on=0;
      mock_boot_b(root); unlink(rn);
      sys_open(X_SWITCH,tn); tr.type=0;
      st_check("SWITCH TO ORIGINAL NOW in a slot B boot (no stock UI): the switch confirmation opens (SWITCH + BACK are hittable)",view==V_SYS&&xsel==X_SWITCH&&run==1&&switch_mode()==SWM_LAUNCH&&hit_test(310,316,tn)==T_SAVE&&hit_test(168,316,tn)==T_CANCEL);
      reg[33]=0x4121; sys_confirm(tn);
      st_check("SWITCH NOW during an activity (steam): refused with a message, merakiplus keeps running",run==1&&view==V_SYS&&!strcmp(toast.msg,TR(TX_T_WAIT_ACT)));
      reg[33]=0x0101; rip.on=1; sys_confirm(tn);
      st_check("SWITCH NOW: last frame (without ripple/toast) + run=0 — mode 'start stock'",run==0&&view==V_BYE&&bye_mode==SWM_LAUNCH&&!rip.on&&!toast.dur);
      run=1; view=V_MAIN; xsel=-1; tr.type=0; mock_runsh(root,6000); unlink(rn);
      sys_open(X_SWITCH,tn); tr.type=0; sys_confirm(tn);
      st_check("SWITCH NOW in run.sh mode without saved changes: clean exit (run.sh SIGCONTs) — what the REVERT button did up to and including v6.7",run==0&&view==V_BYE&&bye_mode==SWM_CONT&&sw_child==0);
      run=1; view=V_MAIN; xsel=-1; tr.type=0; mock_runsh(root,40); snprintf(tx,sizeof tx,"x\nboot_id=%s mono_s=5000\n",bid); RN_PUT(tx);
      sys_open(X_SWITCH,tn); tr.type=0; sys_confirm(tn); tr.type=0;
      int remind=view==V_SYS&&xsel==X_EXIT&&x_from_switch&&run==1;
      activate(T_CANCEL,tn); tr.type=0;
      st_check("  after SAVE: the exit reminder is shown BEFORE exit; BACK cancels, nothing is started",remind&&view==V_MAIN&&run==1&&sw_child==0);
      run=1; view=V_MAIN; xsel=-1; tr.type=0;
      sys_open(X_SWITCH,tn); tr.type=0; sys_confirm(tn); tr.type=0;
      int viaexit=view==V_SYS&&xsel==X_EXIT&&x_from_switch&&run==1; sys_confirm(tn);
      st_check("SWITCH NOW in run.sh mode after SAVE: the reminder first, then exit (run.sh SIGCONTs)",viaexit&&run==0&&view==V_BYE&&bye_mode==SWM_CONT);
      run=1; view=V_MAIN; xsel=-1; tr.type=0; x_from_switch=0;
      mock_boot_b(root); mock_rm(root,SLOT_A_EXE); sys_open(X_SWITCH,tn); tr.type=0;
      int lk=sys_locked()&&hit_test(310,316,tn)==T_NONE&&hit_test(240,316,tn)==T_CANCEL; sys_confirm(tn); lk&=run==1;
      sys_close(tn); tr.type=0;
      st_check("the stock binary is missing: SWITCH is locked (BACK only), merakiplus keeps running",lk&&run==1&&view==V_MAIN);
      run=1; view=V_MAIN; xsel=-1; tr.type=0;
      { int z=1; page=PG_HOME; sel=-1; static const uint16_t ws[2]={0x0101,0x1307};          /* v6.8: the REVERT pill is gone */
        for(int q=0;q<2;q++){ reg[33]=ws[q]; for(int y=404;y<480;y+=2) for(int x=150;x<330;x+=2){ int t=hit_test(x,y,tn);
            if(t!=T_NONE&&!(t==T_GEAR&&in_r(x,y,CX-GEAR_HW,GEAR_Y-22,2*GEAR_HW,46))) z=0; } }   /* v6.11: only the gear (page change, no frame) */
        reg[33]=0x0101; int nr=1; for(int lg=0;lg<NLANG;lg++) for(int i=0;i<NSTR;i++) if(STR[lg][i]&&strstr(STR[lg][i],"REVERT")) nr=0;
        st_check("v6.8 HOME: no touch zone where the REVERT pill used to be (the area 150..329 x 404..479 gives T_NONE — idle and during a brew — except v6.11's gear, which only changes page); no UI string (EN/DA) mentions REVERT",z&&nr); }
      /* v6.17: SYSTEM > STOCK UI. Closed: 5 rows (4 sections + DIAGNOSTICS), no scrolling. A tap on the STOCK UI header opens it and
       * brings it to the top; BOOT UI and SWITCH TO ORIGINAL NOW are its first two rows. */
      mock_boot_b(root); page=PG_SYS; sec_open=-1; scroll[PG_SYS]=0;
      int closed=page_rows(PG_SYS)==NSEC+1&&scroll_max(PG_SYS)==0&&row_find(PG_SYS,RK_X,X_BOOTUI)<0&&row_find(PG_SYS,RK_DIAG,0)==NSEC;
      activate(T_ROW0+SEC_STOCK,tn); ns=row_find(PG_SYS,RK_X,X_BOOTUI);
      int yb=(int)(row_y0(PG_SYS,ns)-scroll[PG_SYS])+25, ys=(int)(row_y0(PG_SYS,ns+1)-scroll[PG_SYS])+25;
      st_check("SYSTEM: closed = 4 section headers + DIAGNOSTICS (5 rows, no scrolling); tapping STOCK UI opens it at the top: BOOT UI and SWITCH TO ORIGINAL NOW are its first two rows, both hittable (v6.16: rows 16 and 17 of one list)",
               closed&&sec_open==SEC_STOCK&&ns==SEC_STOCK+1&&row_find(PG_SYS,RK_X,X_SWITCH)==ns+1&&hit_test(240,yb,tn)==T_ROW0+ns&&hit_test(240,ys,tn)==T_ROW0+ns+1&&ys+25<LIST_VY1&&yb-25>=LIST_VY0);
      activate(T_ROW0+ns,tn); tr.type=0;
      st_check("BOOT UI opens with the current choice (flag set -> MERAKIPLUS)",view==V_SYS&&xsel==X_BOOTUI&&x_edit==1&&x_orig==1);
      ui_touch(TE_DOWN,84,184,tn); ui_touch(TE_UP,84,184,tn);
      st_check("− selects ORIGINAL; nothing is written before SAVE",x_edit==0&&x_orig==1&&access(fl,F_OK)==0);
      activate(T_SAVE,tn); tr.type=0;
      st_check("SAVE removes the flag, closes the view and confirms",access(fl,F_OK)!=0&&view==V_MAIN&&!strcmp(toast.msg,TR(TX_T_BOOT_SAVED_ORIG)));
      activate(T_ROW0+ns,tn); tr.type=0; ui_touch(TE_DOWN,396,184,tn); ui_touch(TE_UP,396,184,tn); int e1=x_edit; activate(T_CANCEL,tn); tr.type=0;
      st_check("+ and then CANCEL: the flag is unchanged",e1==1&&access(fl,F_OK)!=0&&view==V_MAIN);
      if(geteuid()!=0){   /* §9.1: write error -> show the error, keep the old state */
          activate(T_ROW0+ns,tn); tr.type=0; ui_touch(TE_DOWN,396,184,tn); ui_touch(TE_UP,396,184,tn);
          chmod(cdir,0555); activate(T_SAVE,tn); chmod(cdir,0755);
          st_check("SAVE on a read-only file system: error message, the view stays with the OLD state, no flag",view==V_SYS&&xsel==X_BOOTUI&&!strcmp(toast.msg,TR(TX_T_BOOT_ERR))&&x_edit==0&&x_orig==0&&access(fl,F_OK)!=0);
          tr.type=0; activate(T_CANCEL,tn); tr.type=0; toast.dur=0; }
      mock_slot_b(root,1,MB_VENDOR); activate(T_ROW0+ns,tn); tr.type=0;
      int lk2=sys_locked()&&hit_test(84,184,tn)==T_NONE&&hit_test(396,184,tn)==T_NONE&&hit_test(310,316,tn)==T_NONE&&hit_test(240,316,tn)==T_CANCEL;
      x_edit=1; sys_confirm(tn);
      st_check("locked BOOT UI: no −/+/SAVE in the hit test (BACK only) — and a direct SAVE call is refused by the guard",lk2&&access(fl,F_OK)!=0);
      view=V_MAIN; xsel=-1; tr.type=0; page=PG_HOME; run=1; toast.dur=0; sw_sim=0; }

    st_v621(dir);
    printf("== selftest: layout (all states, both languages: text within ring/circle/field) ==\n");
    for(int lg=0;lg<NLANG;lg++){ int ok=1; SysT t; view=V_SYS; lang_set(lg);
      for(int x=X_BOOTUI;x<=X_EXIT;x++) for(int lock=0;lock<4;lock++) for(int f=0;f<2;f++) for(int e=0;e<2;e++) for(int m=0;m<4;m++){
          xsel=x; bs.lock=lock; bs.flag=f; x_orig=f; x_edit=e; bs.a_bin=!(m==3); bs.n_stock=(m==1||m==2)?1:0; bs.stock[0]=18712; x_pending=(m==2); bs.sw_pid=(lock&1)?301:0;
          sys_text(&t);
          float tw=text_wf(t.title,F_S)+(x==X_EXIT?text_wf(IC_WARN,F_IC)+9:0);
          LAY(tw,2*ring_half(78,20),"title",t.title); LAY(text_wf(t.sub,F_XS),2*ring_half(106,11),"subline",t.sub);
          if(t.hero) LAY(text_wf(t.hero,sys_hero_font(t.hero)),x==X_BOOTUI?212:2*ring_half(151,44),"hero",t.hero);
          if(t.status) LAY(text_wf(t.status,F_XS),KB[KST].maxw,"status (field 360 px)",t.status);
          LAY(text_wf(t.note,F_XS),KB[KNOTE].maxw,"note (field 400 px)",t.note);
          LAY(text_wf(t.l1,F_XS),KB[KF1].maxw,"footer 1 (clear of the gauge ends)",t.l1); LAY(text_wf(t.l2,F_XS),2*chord_half(416,11)-12,"footer 2",t.l2);
          if(t.ok) LAY(text_wf(t.ok,x==X_SWITCH?F_XS:F_S),104,"button",t.ok); LAY(text_wf(t.cancel,F_XS),104,"button",t.cancel);
      }
      LAY(text_wf(TR(TX_Y_EXIT_L1),F_S),2*ring_half(138,14)-12,"exit line 1",TR(TX_Y_EXIT_L1)); LAY(text_wf(TR(TX_Y_EXIT_L2),F_S),2*ring_half(162,17)-12,"exit line 2",TR(TX_Y_EXIT_L2));
      LAY(text_wf(TR(TX_Y_EXIT_CMD),F_M)+40,2*ring_half(188,38)-12,"command field",TR(TX_Y_EXIT_CMD));
      st_check(stl("the system views (BOOT UI / SWITCH / exit reminder): title, hero, status, note, footers and buttons fit in all states"),ok);
      ok=1; view=V_MAIN;
      for(int lock=0;lock<4;lock++) for(int f=0;f<2;f++) for(int a=0;a<2;a++) for(int x=0;x<=X_SWITCH;x++){
          const char*label,*sub,*val; int lk; bs.lock=lock; bs.flag=f; bs.a_bin=a; bs.n_stock=0; xrow_text(x,&label,&sub,&val,&lk);
          LAY(text_wf(sub,F_XS),KB[KXSUB].maxw,"row subline",sub); LAY(94+text_wf(label,F_S)+12+(val?text_wf(val,F_M):0),lk?388:372,"row label+value",label);
      }
      /* v6.17: section headers (name + the widest summary + chevron) and the DIAGNOSTICS entry */
      for(int q=0;q<NSEC;q++){ char sm[40]; int ktm=SET[S_TIMEMODE].val, kon=sch.on, khm=sch.hm; SET[S_TIMEMODE].val=0; sch.on=1; sch.hm=22*60+45; sec_summary(q,sm,sizeof sm); SET[S_TIMEMODE].val=ktm; sch.on=kon; sch.hm=khm;
          LAY(94+text_wf(TR(SEC_NAME[q]),F_S)+14+text_wf(sm,F_XS),366,"section header (name + summary)",TR(SEC_NAME[q])); }
      LAY(94+text_wf(TR(TX_PG_DIAG),F_S)+14+text_wf(TR(TX_DV_DOWN),F_S),366,"DIAGNOSTICS entry (name + status)",TR(TX_PG_DIAG));
      st_check(stl("SYSTEM rows BOOT UI / SWITCH: label, value and subline (incl. all lock reasons) fit in the row"),ok);
      /* ---- DIAGNOSE: the top 14 cells, the boot block and the ABOUT block ---- */
      ok=1; { char v[DG_NB][28]; uint32_t col[DG_NB]; char exe[160],evt[128],pids[48],lng[96]; char tv[DG_N][28]; uint32_t tc[DG_N];
      for(int i=0;i<DG_N+DG_NB;i+=2){ const char*la=TR(DG_LAB[i]),*lb=TR(DG_LAB[i+1]); float a=text_wf(la,F_XS),b2=text_wf(lb,F_XS);
          LAY(a/2+b2/2+8,DG_COL_R-DG_COL_L,"diagnose label pair (must not touch)",la); LAY(a,2*(DG_COL_L-40),"diagnose label L",la); LAY(b2,2*(440-DG_COL_R),"diagnose label R",lb); }
      { int kro=ro_mode,kw=cfg_writes,kok=cfg_ok,kph=act.ph; ms_t ko=rx_ok,kb=rx_bad; uint16_t kr[REGN]; memcpy(kr,reg,sizeof reg); float kf=ft_last,km=ft_max;
        ro_mode=1; cfg_writes=1; rx_ok=4294967295u; rx_bad=9999; rx_resync=9999; reg[33]=0xFFFF; reg[34]=reg[35]=reg[36]=0xFF; reg[49]=255; reg[61]=60; reg[50]=reg[58]=9999; reg[112]=reg[113]=0xFFFF; reg[114]=100; reg_seen[112]=1; ft_last=99.9f; ft_max=99.9f;
        wq.n_ok=9999; wq.n_retx=99; wq.n_fail=99; wq.n_block=99;
        for(int c=0;c<2;c++) for(int ph=0;ph<NPHASE;ph++) for(int br=0;br<=BPR_SKIP+2;br++){ act.ph=ph; cfg_ok=c; diag_top_text(tv,tc,1000000000L);
            memset(&bp,0,sizeof bp); if(br<=BPR_SKIP){ bp.ra=bp.rb=br; bp.state=br?BP_DONE:BP_OFF; } else bp.state=br==BPR_SKIP+1?BP_WAIT_A:BP_A;
            diag_top_text(tv,tc,1000000000L);
            for(int i=0;i<DG_N;i+=2){ float a=text_wf(tv[i],F_S),b2=text_wf(tv[i+1],F_S); LAY(a/2+b2/2+6,DG_COL_R-DG_COL_L,"diagnose cell pair",tv[i]); LAY(a,2*(DG_COL_L-40),"diagnose cell L",tv[i]); LAY(b2,2*(440-DG_COL_R),"diagnose cell R",tv[i+1]); } }
        ro_mode=kro; cfg_writes=kw; cfg_ok=kok; act.ph=kph; rx_ok=ko; rx_bad=kb; rx_resync=0; memset(&bp,0,sizeof bp); memcpy(reg,kr,sizeof reg); ft_last=kf; ft_max=km; memset(&wq,0,sizeof wq); }
      for(int i=0;i<DG_NB;i++) LAY(text_wf(TR(DG_LAB[DG_N+i]),F_XS),KB[KDL].maxw,"diagnose label",TR(DG_LAB[DG_N+i]));
      for(int lock=0;lock<4;lock++) for(int f=0;f<2;f++) for(int sl=0;sl<3;sl++) for(int m=0;m<3;m++) for(int g=0;g<3;g++){
          bs.lock=lock; bs.flag=f; bs.self_slot=sl; bs.self_pid=32767; bs.sw_pid=m?32766:0; bs.n_stock=m?1:0; bs.stock[0]=32765; bs.stock_run=(m==2); bs.sw_log=g;
          snprintf(bs.self_exe,sizeof bs.self_exe,"%s",sl==SLOT_B?SLOT_B_EXE:(g?"/new_customer/merakiplus/a/very/long/path/that/cannot/ever/fit/on/one/line/merakiplus_v6_testbuild":""));
          snprintf(bs.sw_evt,sizeof bs.sw_evt,"12:59:59 switch FAILED: B (pid 32767) did not come up \xE2\x80\x94 fallback: SIGCONT A, kill B");
          bs.n_stock=m?(m==2?2:1):0;
          diag_boot_text(v,col,exe,sizeof exe,evt,sizeof evt,pids,sizeof pids);
          for(int i=0;i<DG_NB;i++) LAY(text_wf(v[i],F_S),KB[KDV].maxw,"diagnose cell",v[i]);
          LAY(text_wf(pids,F_S),DG_FULL_W,"diagnose pid line",pids);
          LAY(text_wf(exe,F_XS),DG_FULL_W,"diagnose exe",exe); LAY(text_wf(evt,F_XS),DG_FULL_W,"diagnose event",evt);
      }
      LAY(text_wf(TR(TX_DG_CAP),F_XS),250,"diagnose heading",TR(TX_DG_CAP)); LAY(text_wf(TR(TX_DG_ABOUT),F_XS),250,"diagnose heading",TR(TX_DG_ABOUT));
      { int ks=lang_src; for(lang_src=LS_DEFAULT;lang_src<=LS_SESSION;lang_src++){ diag_lang_text(lng,sizeof lng); LAY(text_wf(lng,F_S),DG_FULL_W,"ABOUT: UI language line",lng); } lang_src=ks; }
      LAY(text_wf(PRODUCT " " MERAKIPLUS_VERSION,F_S),DG_FULL_W,"ABOUT: wordmark + version",PRODUCT " " MERAKIPLUS_VERSION);
      /* v6.10: the PRESSURE · ESTIMATE block — anchor 1..15 bar, the now line with a 5-digit raw value in all three forms, the notes, the toast */
      { uint16_t k51=reg[51]; int kph=act.ph,kv=SET[S_PIPRESS].val; char anc[64],nw[64],tb[96]; uint32_t c;
        LAY(text_wf(TR(TX_DG_PCAP),F_XS),250,"diagnose heading",TR(TX_DG_PCAP));
        for(int av=1;av<=15;av+=14) for(int q=0;q<4;q++){ SET[S_PIPRESS].val=av; reg[51]=(uint16_t)(q==0?1790*av/4:(q==1?1790*4/av:65535)); act.ph=q==3?PH_EXT:PH_READY;
            diag_press_text(anc,sizeof anc,nw,sizeof nw,&c);
            LAY(text_wf(anc,F_S),DG_FULL_W,"PRESSURE · ESTIMATE: anchor line",anc); LAY(text_wf(nw,F_S),DG_FULL_W,"PRESSURE · ESTIMATE: now line",nw);
            snprintf(tb,sizeof tb,TR(TX_T_PINFO),p_anchor_bar(),P_ANCHOR_RAW); LAY(text_wf(tb,F_XS),TOAST_TW,"basis toast",tb); }
        reg[51]=k51; act.ph=kph; SET[S_PIPRESS].val=kv; }
      LAY(text_wf(TR(TX_ST_PULSE),F_M),STAT_PITCH-8,"BAR field: PULSE",TR(TX_ST_PULSE)); LAY(text_wf("65535",F_M),STAT_PITCH-8,"BAR field: raw value","65535"); }
      st_check(stl("DIAGNOSE: label/cell pairs do not touch, boot cells <= column width, long paths/events truncated with …, PRESSURE · ESTIMATE and the ABOUT block fit"),ok);
      ok=1; for(bye_mode=0;bye_mode<2;bye_mode++) LAY(text_wf(bye_sub(),F_XS),340,"goodbye",bye_sub()); bye_mode=0;
      LAY(text_wf(TR(TX_Y_BYE_T),F_M),340,"goodbye",TR(TX_Y_BYE_T)); LAY(text_wf(TR(TX_Y_BYE_CLOSED),F_XS),340,"goodbye",TR(TX_Y_BYE_CLOSED));
      { char q[64]="LC-test " LC "kill $(pidof demo_v9)" UC " OK"; int w0=text_w(q,F_XS); fit_text(q,F_XS,90); ok&=text_w(q,F_XS)<=90&&w0>90&&strstr(q,"\xE2\x80\xA6")!=NULL;
        ok&=text_w(LC "demo_v9",F_S)<text_w("DEMO_V9",F_S)&&text_w(LC UC,F_S)==0; }
      st_check(stl("switch screen + code spans (lowercase only between LC/UC, control characters take 0 px, truncation with …)"),ok);
      /* ---- HOME + splash (v6.3) ---- */
      ok=1;
      for(int ss=0;ss<NSS;ss++){ float w[3]; for(int i=0;i<3;i++) w[i]=text_wf(TR(STAT_SET[ss][i]),F_XS);
          LAY(w[0],2*(CX-STAT_PITCH-96),"stat label L (field 96..384)",TR(STAT_SET[ss][0])); LAY(w[2],2*(384-CX-STAT_PITCH),"stat label R",TR(STAT_SET[ss][2]));
          LAY(w[0]/2+w[1]/2+STAT_GAP,STAT_PITCH,"stat label pair (v6.11: >= 16 px gap)",TR(STAT_SET[ss][1])); LAY(w[1]/2+w[2]/2+STAT_GAP,STAT_PITCH,"stat label pair (v6.11: >= 16 px gap)",TR(STAT_SET[ss][2]));
          if(STAT_SET[ss][1]==TX_ST_BAR){ float wr=text_wf(TR(TX_ST_PRAW),F_XS); LAY(w[0]/2+wr/2+STAT_GAP,STAT_PITCH,"stat label pair (RAW, v6.10)",TR(TX_ST_PRAW)); LAY(wr/2+w[2]/2+STAT_GAP,STAT_PITCH,"stat label pair (RAW, v6.10)",TR(TX_ST_PRAW)); } }
      { static const short cp[3][2]={{TX_C_PRESS,TX_C_WEIGHT},{TX_C_WEIGHT,TX_C_TARGET},{TX_C_MILK,TX_C_TARGET}};
        for(int i=0;i<3;i++) LAY(text_wf(TR(cp[i][0]),F_XS)+text_wf(TR(cp[i][1]),F_XS)+12,236,"chart captions (field 236)",TR(cp[i][0]));
        for(int i=0;i<3;i++) LAY(text_wf(TR(cp[i][0]),F_XS)+text_wf(TR(TX_C_RAW),F_XS)+12,236,"chart caption + RAW tag (v6.9)",TR(TX_C_RAW));
        /* v6.11: the hint lines in the empty chart field (field HINT_W, 4 px margin on each side) — every text that can appear there */
        { static const short hl[]={TX_C_HINT,TX_C_DIAGREF,TX_G_RO,TX_G_BUSY,TX_G_WAIT,TX_G_NOCFG,TX_G_OOR};
          for(size_t i=0;i<sizeof hl/sizeof hl[0];i++) LAY(text_wf(TR(hl[i]),F_XS),HINT_W-8,"chart hint (field 304, v6.11)",TR(hl[i]));
          char eb[96]; snprintf(eb,sizeof eb,TR(TX_C_ERRCODE),"E3"); LAY(text_wf(eb,F_XS),HINT_W-8,"chart hint (field 304, v6.11)",eb); } }
      for(int k=0;k<4;k++){ LAY(text_wf(TR(STOP_BTN[k]),stop_font(1)),STOP_PILL1_W-28,"STOP button (v6.17: pill 216, F_M)",TR(STOP_BTN[k]));
                            LAY(text_wf(TR(STOP_BTN[k]),stop_font(2)),STOP_PILL2_W-28,"STOP button, two side by side (v6.17: pill 152, F_S)",TR(STOP_BTN[k])); }
      /* v6.17: HOLD TO START (icon + text in the pill), the page titles (icon + name, in the chord at y 44..62), the live lines (widest
       * values, °C and °F; every guard text that can stand there) and the icons' targets on HOME (pitch 60 px, >= 8 px between two) */
      for(int k=0;k<4;k++) LAY(text_wf(ACT_ICON[k],F_IC)+10+text_wf(TR(TX_B_START),F_S),START_W-32,"HOLD TO START pill",TR(TX_B_START));
      for(int pg=PG_GRIND;pg<=PG_DIAG;pg++) LAY(page_title_w(pg),2*chord_half(PG_TITLE_Y,20)-16,"page title",page_title(pg));
      { int ktu=SET[S_TUNIT].val; uint16_t kr[REGN]; int ks[REGN]; memcpy(kr,reg,sizeof reg); memcpy(ks,reg_seen,sizeof reg_seen); ms_t klr=last_rx_ms; char lv[96];
        for(int tu=0;tu<2;tu++){ SET[S_TUNIT].val=tu; reg[38]=(uint16_t)-9999; reg[40]=reg[42]=reg[47]=reg[48]=1500; reg_seen[38]=reg_seen[40]=reg_seen[42]=reg_seen[47]=reg_seen[48]=1; reg[33]=0x0101; reg[34]=reg[35]=reg[36]=0; reg[1]=0; last_rx_ms=1000000000L;
            for(int pg=PG_GRIND;pg<=PG_SYS;pg++){ page_live_text(pg,lv,sizeof lv,1000000000L); LAY(text_wf(lv,F_XS),LIVE_W,"page live line",lv); LAY(text_wf(lv,F_XS),2*chord_half(PG_LIVE_Y,11)-12,"page live line (chord)",lv); } }
        { static const short gl[]={TX_G_RO,TX_G_NOLINK,TX_G_ERROR,TX_G_SLEEP,TX_G_BUSY,TX_G_NOCFG,TX_G_OOR,TX_G_GRINDTIME,TX_DV_VERSION_RO};
          for(size_t i=0;i<sizeof gl/sizeof gl[0];i++) LAY(text_wf(TR(gl[i]),F_XS),LIVE_W,"page live line (guard text)",TR(gl[i])); }
        memcpy(reg,kr,sizeof reg); memcpy(reg_seen,ks,sizeof reg_seen); last_rx_ms=klr;
        for(int tu=0;tu<2;tu++){ SET[S_TUNIT].val=tu; int kv[4]={SET[S_GRINDW].val,SET[S_BREWW].val,SET[S_HOTW].val,SET[S_STEAMT].val};
            SET[S_GRINDW].val=SET[S_GRINDW].hi; SET[S_BREWW].val=SET[S_BREWW].hi; SET[S_HOTW].val=SET[S_HOTW].hi; SET[S_STEAMT].val=SET[S_STEAMT].hi; home_model(1000000000L);
            for(int k=0;k<4;k++){ LAY(text_wf(hm.alab[k],F_XS),60-8,"icon target (pitch 60)",hm.alab[k]); }
            LAY(text_wf(hm.alab[0],F_XS)/2+(ACT_X[1]-ACT_X[0])*1.5f,chord_half(ACT_LAB_Y,11)-4,"icon targets (outermost within the chord)",hm.alab[0]);
            SET[S_GRINDW].val=kv[0]; SET[S_BREWW].val=kv[1]; SET[S_HOTW].val=kv[2]; SET[S_STEAMT].val=kv[3]; }
        SET[S_TUNIT].val=ktu; }
      for(int k=0;k<4;k++){ char sb[96]; snprintf(sb,sizeof sb,TR(TX_H_STARTING),TR(ACT_NAME[k])); LAY(text_wf(sb,F_XS),KB[KSUB].maxw,"HOME subline",sb); }
      LAY(hypotf(brand_wf(F_XS)/2,(float)(CY-HOME_BRAND_Y)),196.5f-4,"HOME title line (must lie within the progress ring r 196.5)",PRODUCT);
      LAY(HOME_BRAND_Y+(int)FS[F_XS].cap+8,76,"HOME title line (clear of the phase chip y 76)",PRODUCT);
      LAY(brand_wf(F_XL),SPLASH_W-8,"splash wordmark",PRODUCT);
      { int kok=cfg_ok; char sb[96]; for(cfg_ok=0;cfg_ok<2;cfg_ok++){ splash_status(sb,sizeof sb); LAY(text_wf(sb,F_XS),SPLASH_W-8,"splash status",sb); } cfg_ok=kok; }
      st_check(stl("HOME + splash: stat labels, chart texts, buttons, 'STARTING …', title line and wordmark fit"),ok);
      xsel=-1; view=V_MAIN; }
    lang_set(base_lang);

    /* ============================ v6.3 ============================ */
    printf("== selftest: i18n — the string table (%d ids x %d languages) ==\n",NSTR,NLANG);
    { int ok=1,same=0;
      for(int i=0;i<NSTR;i++) for(int lg=0;lg<NLANG;lg++){ const char*s=STR[lg][i]; if(!s||(!*s&&i!=TX_NONE)){ ok=0; printf("     %s missing in %s\n",STR_ID[i],LANG_CODE[lg]); } }
      st_check("every string id has both EN and DA (no NULL, no empty ones apart from TX_NONE)",ok&&NSTR>300&&!STR[LANG_EN][TX_NONE][0]);
      ok=1; for(int i=0;i<NSTR;i++){ char a[64],b[64]; fmt_sig(STR[LANG_EN][i],a,sizeof a); fmt_sig(STR[LANG_DA][i],b,sizeof b); if(strcmp(a,b)){ ok=0; printf("     %s: printf conversions differ: EN '%s' / DA '%s'\n",STR_ID[i],a,b); } }
      st_check("same printf conversions in the same order in EN and DA (format safety)",ok);
      ok=1; for(int i=0;i<NSTR;i++){ char a[96],b[96]; num_sig(STR[LANG_EN][i],a,sizeof a); num_sig(STR[LANG_DA][i],b,sizeof b); if(strcmp(a,b)){ ok=0; printf("     %s: numbers differ: EN '%s' / DA '%s'\n",STR_ID[i],a,b); }
          if(!strncmp(STR_ID[i],"TX_U_",5)&&strcmp(STR[LANG_EN][i],STR[LANG_DA][i])){ ok=0; printf("     %s: unit differs\n",STR_ID[i]); } }
      { static const short u[]={TX_U_G,TX_U_S,TX_U_BAR,TX_U_C,TX_U_F}; static const char*const w[]={"G","S","BAR","\xC2\xB0" "C","\xC2\xB0" "F"}; for(int q=0;q<5;q++) for(int lg=0;lg<NLANG;lg++) if(strcmp(STR[lg][u[q]],w[q])) ok=0; }
      st_check("numbers, decimal point and units (G, S, BAR, °C, °F) are identical in EN and DA — only words are translated",ok);
      ok=1; { static const char*const ban[]={"GEN 1","GEN1","GEN-1","GEN 2","GEN2","GEN-2","GENERATION","MERAKITECH"};
        char vt[700];
        for(int i=0;i<NSTR;i++) for(int lg=0;lg<NLANG;lg++){ vis_text(STR[lg][i],vt,sizeof vt,1);
            for(unsigned q=0;q<sizeof ban/sizeof ban[0];q++) if(strstr(vt,ban[q])){ ok=0; printf("     %s [%s] contains '%s'\n",STR_ID[i],LANG_CODE[lg],ban[q]); } }
        vis_text(MERAKIPLUS_MARK,vt,sizeof vt,1); for(unsigned q=0;q<sizeof ban/sizeof ban[0];q++) if(strstr(vt,ban[q])) ok=0;
        vis_text(PRODUCT " " MERAKIPLUS_VERSION,vt,sizeof vt,1); for(unsigned q=0;q<sizeof ban/sizeof ban[0];q++) if(strstr(vt,ban[q])) ok=0; }
      st_check("no generation name (see the ban list) in any string, in the binary marker or in the product name — in both tables",ok&&!strcmp(PRODUCT,"MERAKI+"));
      ok=1; { char vt[700]; for(int i=0;i<NSTR;i++) for(int lg=0;lg<NLANG;lg++){ if(STR_K[i]==KCON) continue; vis_text(STR[lg][i],vt,sizeof vt,0);
            if(strstr(vt,"MERAKIPLUS")){ ok=0; printf("     %s [%s]: the identifier appears in visible text\n",STR_ID[i],LANG_CODE[lg]); } } }
      st_check("the product name is " PRODUCT ": the identifier 'merakiplus' appears only in code spans (paths) and on the console",ok);
      ok=1; { char ex[700],bad[16]; for(int i=0;i<NSTR;i++) for(int lg=0;lg<NLANG;lg++){ if(STR_K[i]==KCON) continue; fmt_fill(STR[lg][i],ex,sizeof ex);
            if(!glyphs_ok(ex,bad,sizeof bad)){ ok=0; printf("     %s [%s]: the character '%s' does not exist in the stroke font\n",STR_ID[i],LANG_CODE[lg],bad); } } }
      st_check("every character in both tables has a glyph in the stroke font (nothing is drawn as '?')",ok);
      ok=1; { char ex[700]; int nk=0;
        for(int i=0;i<NSTR;i++){ int k=STR_K[i]; if(k==K0||k==KCON) continue; nk++;
            for(int lg=0;lg<NLANG;lg++){ fmt_fill(STR[lg][i],ex,sizeof ex); int w=text_w(ex,KB[k].font);
                if(w>KB[k].maxw){ ok=0; printf("     %s [%s] (%s): '%s' is %d px, room %d px\n",STR_ID[i],LANG_CODE[lg],KB[k].what,ex,w,KB[k].maxw); } } }
        char nm[160]; snprintf(nm,sizeof nm,"pixel budget per class: %d classified strings x 2 languages fit in their field (toasts, notes, footers, buttons, titles, cells)",nk); st_check(nm,ok&&nk>150); }
      for(int i=0;i<NSTR;i++) if(!strcmp(STR[LANG_EN][i],STR[LANG_DA][i])) same++;
      st_check("endonyms: ENGLISH / DANSK are named the same in both tables (you can always recognise your own language)",!strcmp(STR[LANG_EN][TX_UL_DA],"DANSK")&&!strcmp(STR[LANG_DA][TX_UL_DA],"DANSK")&&!strcmp(STR[LANG_EN][TX_UL_EN],STR[LANG_DA][TX_UL_EN]));
      printf("     %d of %d strings are identical in EN and DA (units, proper names, technical words like OFFLINE/FLOW/SLOT B, commands)\n",same,NSTR);
      { int k=ui_lang; ui_lang=LANG_EN; const char*e=TR(TX_B_SAVE); ui_lang=LANG_DA; const char*d=TR(TX_B_SAVE); ui_lang=7; const char*u=TR(TX_B_SAVE); const char*o=TR(99999); ui_lang=k;
        st_check("TR(): lookup per call (EN 'SAVE' / DA 'GEM'); unknown language -> English; unknown id -> empty string",!strcmp(e,"SAVE")&&!strcmp(d,"GEM")&&u==e&&o&&!*o); } }

    printf("== selftest: i18n — the language file (%s; here: <work dir>/lang) ==\n",lang_path);
    { char lp[440],lt[460],keepl[256],nodir[460]; strcpy(keepl,lang_path); int keeps=lang_src;
      st_check("default path = /new_customer/merakiplus/lang (merakiplus's own directory — not /customer, not config.json)",!strcmp(keepl,"/new_customer/merakiplus/lang")||getenv("MERAKIPLUS_LANGFILE")!=NULL||strstr(keepl,"_preview_lang")!=NULL);
      snprintf(lp,sizeof lp,"%s/lang",dir); snprintf(lt,sizeof lt,"%s.tmp",lp); snprintf(lang_path,sizeof lang_path,"%s",lp);
      unlink(lp); ui_lang=LANG_DA; lang_load();
      st_check("file missing -> ENGLISH (default), source = default",ui_lang==LANG_EN&&lang_src==LS_DEFAULT);
      st_check("\"da\" -> DANISH, \"en\" -> ENGLISH (source = file)",lang_from(lp,"da",2)==LANG_DA&&lang_src==LS_FILE&&lang_from(lp,"en",2)==LANG_EN&&lang_src==LS_FILE);
      st_check("tolerant reading: \"da\\n\" (echo da > lang), \" DA \\r\\n\", \"Da\" -> DANISH; \"EN\\n\" -> ENGLISH",lang_from(lp,"da\n",3)==LANG_DA&&lang_from(lp," DA \r\n",6)==LANG_DA&&lang_from(lp,"Da",2)==LANG_DA&&lang_from(lp,"EN\n",3)==LANG_EN);
      { int ok=1; static const struct { const char*d; int n; } bad[]={{"de",2},{"dansk",5},{"d",1},{"daa",3},{"danish\n",7},{"e n",3},{"sv",2},{"\xff\xfe",2},{"da da",5},{"0",1},{"da\0x",4},{"en;rm -rf /",11},{"da..............................",32}};
        for(unsigned q=0;q<sizeof bad/sizeof bad[0];q++){ if(lang_from(lp,bad[q].d,bad[q].n)!=LANG_EN||lang_src!=LS_UNKNOWN){ ok=0; printf("     '%s' did not give EN/unknown\n",bad[q].d); } }
        st_check("anything other than en/da (de, dansk, d, daa, sv, binary, too long, …) -> ENGLISH, source = unknown value",ok);
        st_check("empty file -> ENGLISH (unknown value)",lang_from(lp,"",0)==LANG_EN&&lang_src==LS_UNKNOWN); }
      unlink(lp); mkdir(lp,0755); ui_lang=LANG_DA; lang_load(); int e1=ui_lang==LANG_EN&&lang_src==LS_DEFAULT; rmdir(lp);
      st_check("unreadable (the path is a directory: read fails) -> ENGLISH",e1);
      if(geteuid()!=0){ put_bytes(lp,"da",2); chmod(lp,0000); ui_lang=LANG_DA; lang_load(); int e2=ui_lang==LANG_EN&&lang_src==LS_DEFAULT; chmod(lp,0644); st_check("unreadable (no read permission) -> ENGLISH",e2); }
      unlink(lp); lang_load();
      st_check("lang_save(DA): the file contains EXACTLY \"da\" (2 bytes), no tmp file left behind, reads back as DANISH",lang_save(LANG_DA)&&file_is(lp,"da")&&access(lt,F_OK)!=0&&lang_load()==LANG_DA&&lang_src==LS_FILE);
      st_check("lang_save(EN) on top: EXACTLY \"en\"; an invalid language (-1, 2, 7) is rejected and does not touch the file",lang_save(LANG_EN)&&file_is(lp,"en")&&!lang_save(-1)&&!lang_save(NLANG)&&!lang_save(7)&&file_is(lp,"en"));
      snprintf(nodir,sizeof nodir,"%s/does-not-exist/lang",dir); snprintf(lang_path,sizeof lang_path,"%s",nodir);
      st_check("the directory does not exist: lang_save reports an error and creates nothing",!lang_save(LANG_DA)&&access(nodir,F_OK)!=0);
      snprintf(lang_path,sizeof lang_path,"%s",lp);
      if(geteuid()!=0){ char rd[460],rl[480]; snprintf(rd,sizeof rd,"%s/rolang",dir); mkdir(rd,0755); snprintf(rl,sizeof rl,"%s/lang",rd); put_bytes(rl,"en",2); chmod(rd,0555);
          snprintf(lang_path,sizeof lang_path,"%s",rl); int r=lang_save(LANG_DA); chmod(rd,0755); snprintf(lang_path,sizeof lang_path,"%s",lp);
          char rt[500]; snprintf(rt,sizeof rt,"%s.tmp",rl);
          st_check("read-only directory: lang_save fails, the old file is intact (\"en\"), no tmp file",!r&&file_is(rl,"en")&&access(rt,F_OK)!=0); }

      /* ---- through the UI: SYSTEM -> UI LANGUAGE ---- */
      printf("== selftest: i18n — the UI LANGUAGE row (live switch, CANCEL, SAVE, independent of the stock UI's config.language) ==\n");
      ms_t tn=40000; char cp[460],kcfg[256],kbak[256],rnl[500]; strcpy(kcfg,cfg_path); strcpy(kbak,cfg_bakdir);
      snprintf(cp,sizeof cp,"%s/config_lang.json",dir); copy_file(cfgsrc,cp); snprintf(cfg_path,sizeof cfg_path,"%s",cp); snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s/langbak",dir); snprintf(rnl,sizeof rnl,"%s/RESTART_NEEDED",cfg_bakdir);
      unlink(lp); lang_load(); settings_load(); char md0[33]; strcpy(md0,cfg_md5); int cw0=cfg_writes, stock0=sv(S_LANG);
#define UL_PREP() do{ sim_ready(tn); memset(&wq,0,sizeof wq); memset(&sav,0,sizeof sav); act.pending=ACT_NONE; view=V_MAIN; page=PG_SYS; sec_open=SEC_DISPLAY; sel=-1; xsel=-1; tr.type=0; run=1; toast.dur=0; rip.on=0; memset(scroll,0,sizeof scroll); txq_len=0; }while(0)
#define UL_ROW (T_ROW0+1)   /* v6.17: SYSTEM > DISPLAY is open: row 0 = the DISPLAY header, row 1 = UI LANGUAGE */
      UL_PREP();
      st_check("UI LANGUAGE / UI-SPROG is the FIRST row of SYSTEM's first section (DISPLAY; the closed header shows the language's own name), is not a config key and has no MCU word; the stock row (config.language) still exists (STOCK UI section)",
               SEC_ROWS[SEC_DISPLAY][0]==S_UILANG&&row_find(PG_SYS,RK_SET,S_UILANG)==1&&SET[S_UILANG].key==NULL&&SET[S_UILANG].mcu==M_NONE&&(SET[S_UILANG].flags&SF_LOCAL)&&hit_test(240,row_y0(PG_SYS,1)+25,tn)==UL_ROW&&!strcmp(SET[S_LANG].key,"language")&&!(SET[S_LANG].flags&SF_LOCAL)&&sec_of_set(S_LANG)==SEC_STOCK);
      activate(UL_ROW,tn); tr.type=0;
      st_check("the row opens the stepper detail with the active language (ENGLISH); the options are ENGLISH / DANSK",view==V_DETAIL&&sel==S_UILANG&&edit_val==LANG_EN&&SET[S_UILANG].lo==0&&SET[S_UILANG].hi==1&&hit_test(396,184,tn)==T_PLUS&&hit_test(310,316,tn)==T_SAVE);
      const char*t_en=TR(TX_B_CANCEL); static_dirty=0;
      ui_touch(TE_DOWN,396,184,tn); ui_touch(TE_UP,396,184,tn);
      st_check("+ : LIVE — the whole UI switches to Danish immediately (lookup per render, static layer redrawn); nothing is written yet",edit_val==LANG_DA&&ui_lang==LANG_DA&&static_dirty==1&&TR(TX_B_CANCEL)!=t_en&&!strcmp(TR(TX_B_CANCEL),"ANNULLER")&&SET[S_UILANG].val==LANG_EN&&access(lp,F_OK)!=0);
      ui_frame(tn+40); tr.type=0;
      { char l[112], sm[40]; detail_text(S_UILANG,l,sizeof l); sec_summary(SEC_DISPLAY,sm,sizeof sm); st_check("  the detail shows 'ENGLISH / DANSK' (endonyms); the DISPLAY header's summary is the live language's own name",!strcmp(l,"ENGLISH / DANSK")&&!strcmp(sm,"DANSK")); }
      activate(T_CANCEL,tn); tr.type=0;
      st_check("CANCEL: back to English, no file",view==V_MAIN&&ui_lang==LANG_EN&&SET[S_UILANG].val==LANG_EN&&access(lp,F_OK)!=0);
      activate(UL_ROW,tn); tr.type=0; ui_touch(TE_DOWN,396,184,tn); ui_touch(TE_UP,396,184,tn); activate(T_SAVE,tn); tr.type=0;
      st_check("SAVE: file = \"da\", the language is Danish, the confirmation is in the NEW language, the view closes",file_is(lp,"da")&&ui_lang==LANG_DA&&SET[S_UILANG].val==LANG_DA&&lang_src==LS_FILE&&view==V_MAIN&&!sav.active&&!strcmp(toast.msg,STR[LANG_DA][TX_T_LANG_SAVED]));
      cfg_read(); int v_lang=-1; cfg_get_int("language",&v_lang);
      st_check("SAVE does NOT touch stock: config.json md5 unchanged, config.language unchanged, 0 config writes, no RESTART_NEEDED, no backup directory",!strcmp(cfg_md5,md0)&&v_lang==stock0&&cfg_writes==cw0&&access(rnl,F_OK)!=0&&access(cfg_bakdir,F_OK)!=0);
      st_check("SAVE sends NO cmd6 frame (write queue empty, 0 bytes to the UART) — the frame policy is untouched",wq.n==0&&txq_len==0&&wq.n_block==0);
      ui_lang=LANG_EN; lang_src=LS_DEFAULT; lang_load(); settings_load();
      st_check("'restart' (lang_load + settings_load): Danish is remembered from the file",ui_lang==LANG_DA&&lang_src==LS_FILE&&SET[S_UILANG].val==LANG_DA);
      UL_PREP(); activate(UL_ROW,tn); tr.type=0; ui_touch(TE_DOWN,84,184,tn); ui_touch(TE_UP,84,184,tn); int live_en=ui_lang==LANG_EN; activate(T_SAVE,tn); tr.type=0;
      st_check("− and SAVE: back to English, file = \"en\", confirmation in English",live_en&&file_is(lp,"en")&&ui_lang==LANG_EN&&!strcmp(toast.msg,STR[LANG_EN][TX_T_LANG_SAVED]));
      { int ok=cfg_set_int("language",3); settings_load(); int a=sv(S_LANG)==3&&ui_lang==LANG_EN&&file_is(lp,"en"); ok&=cfg_set_int("language",stock0); settings_load();
        st_check("conversely: the stock LANGUAGE (config.language 0 -> 3 -> back) changes neither the UI language nor the lang file",ok&&a&&sv(S_LANG)==stock0&&ui_lang==LANG_EN); }
      snprintf(lang_path,sizeof lang_path,"%s",nodir); UL_PREP(); activate(UL_ROW,tn); tr.type=0; ui_touch(TE_DOWN,396,184,tn); ui_touch(TE_UP,396,184,tn); activate(T_SAVE,tn); tr.type=0;
      st_check("the file cannot be written: the language applies anyway (this session), warning in a toast, DIAGNOSE source = not saved",ui_lang==LANG_DA&&lang_src==LS_SESSION&&view==V_MAIN&&!strcmp(toast.msg,STR[LANG_DA][TX_T_LANG_NOFILE])&&access(nodir,F_OK)!=0);
      { int ok=1; for(int lg=0;lg<NLANG;lg++){ lang_set(lg); settings_load(); UL_PREP(); static_dirty=1; force_all=1; ui_frame(tn+100);
            const char*g=act_guard(ACT_BREW,tn+100); ok&=(g==NULL); reg[35]=1; ok&=!strcmp(err_text(),STR[lg][TX_ER_E2_BREW])&&!strcmp(ph_name(PH_ERROR),STR[lg][TX_PH_ERROR]); reg[35]=0;
            home_model(tn+100); ok&=!strcmp(hm.sl[2],STR[lg][TX_ST_WEIGHT])&&!strcmp(hm.label,STR[lg][TX_H_BOILER]); }
        st_check("live lookup everywhere: phase name, error text, HOME label and stat labels follow the language without a restart",ok); }
      snprintf(lang_path,sizeof lang_path,"%s",keepl); lang_src=keeps; lang_set(base_lang); strcpy(cfg_path,kcfg); strcpy(cfg_bakdir,kbak); toast.dur=0; view=V_MAIN; sel=-1; page=PG_HOME; }
    mock_boot_b(root); boot_refresh();
    strcpy(cfg_bakdir,keepbak);    mock_boot_b(root); boot_refresh();
    strcpy(cfg_bakdir,keepbak); snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); settings_load();
    lang_set(base_lang);
    /* ============================ v6.6 ============================ */
    st_phases();
    st_stop();                         /* v6.13 */
    st_power();                        /* v6.14 */
    st_time(dir);                      /* v6.16 */
    st_wrap();
    st_repush(cfgsrc);
    lang_set(base_lang);
    /* ============================ v6.7 ============================ */
    st_net(dir);
    /* ============================ v6.9 ============================ */
    st_cond();
    /* ============================ v6.10 ============================ */
    st_press(cfgsrc,dir);
    /* ============================ v6.11 ============================ */
    st_v611();
    st_v617();
    st_v618(dir);                      /* v6.18 */
    st_v619();                         /* v6.19 */
    st_v620();                         /* v6.20 */
    st_v622();                         /* v6.22 */
    /* ============================ v6.15 ============================ */
    st_graph();
    printf("== selftest [%s]: %s (%d checks, %d failed) — work dir %s ==\n",LANG_CODE[base_lang],st_fail?"FAILED":"ALL OK",st_n,st_fail,dir);
    return st_fail?1:0;
}
static void dump_table(void){
    printf("#key\tlabel\tpage\tspec_tier\tmin\tmax\tstep\tdefault\thard\tlock\test\tsensor\tmcu\thint\n");
    static const char*const MW[]={"-","4","23","11","112","113","114","25","14","17","2"};
    for(int i=0;i<NSET;i++){ const Setting*s=&SET[i];
        if(s->flags&(SF_LOCAL|SF_OWN|SF_SND)) continue;   /* merakiplus's own settings (UI language, v6.16 schedule, v6.18 sound) are not spec parameters */
        printf("%s\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%s\n",s->key?s->key:"-",TR(s->label),TR(TIER_NAME[s->tier]),s->ptier,s->min,s->max,s->step,s->def,s->hard,
               !!(s->flags&SF_LOCK),!!(s->flags&SF_EST),!!(s->flags&SF_SENSOR),MW[s->mcu],s->hint?TR(s->hint):"-"); }
}
/* the string table as TSV (id, class, EN, DA). Code spans are shown in `backticks`, icons as {iconN}, line breaks as \\n. */
static void dump_str1(const char*s){
    int lc=0;
    for(const unsigned char*p=(const unsigned char*)s;*p;p++){
        if(*p==0x0E){ if(!lc){ putchar('`'); lc=1; } }
        else if(*p==0x0F){ if(lc){ putchar('`'); lc=0; } }
        else if(*p=='\n') fputs("\\n",stdout);
        else if(*p<32) printf("{icon%d}",*p);
        else putchar(*p);
    }
    if(lc) putchar('`');
}
static void dump_strings(void){
    printf("#id\tclass\tEN\tDA\n");
    for(int i=0;i<NSTR;i++){ printf("%s\t%s\t",STR_ID[i],KNAME[STR_K[i]]); dump_str1(STR[LANG_EN][i]); putchar('\t'); dump_str1(STR[LANG_DA][i]); putchar('\n'); }
}
static void bench(void){
    sim_reset(); sim_tick=tick_brew; sim_run(8000);
    double t0=msnow(), mx=0; int n=0;
    for(int i=0;i<600;i++){ double a=msnow(); sim_run(33); double d=msnow()-a; if(d>mx)mx=d; n++; }
    double avg=(msnow()-t0)/n;
    printf("bench brew (animated gauge+chart+numbers): avg %.3f ms/frame, max %.3f ms (host)\n",avg,mx);
    t0=msnow(); for(int i=0;i<200;i++){ force_all=1; static_dirty=1; compose(sim_now); } printf("bench full rebuild (static+all):       %.3f ms/frame (host)\n",(msnow()-t0)/200);
    sim_reset(); sim_run(500); t0=msnow(); n=0; go_page(PG_BREW,1,sim_now); while(tr.type){ sim_run(33); n++; } printf("bench slide transition:               %.3f ms/frame over %d frames (host)\n",(msnow()-t0)/(n?n:1),n);
    sim_reset(); sim_run(3000); t0=msnow(); for(int i=0;i<300;i++) sim_run(33); printf("bench idle (no change):               %.3f ms/frame (host)\n",(msnow()-t0)/300);
}
int main(int argc,char**argv){
    if(getenv("MERAKIPLUS_MOCK_STOCK")) return mock_stock_main(argv[0]);
    self_bin=argv[0];
    int ai=1;
    if(argc>2&&!strcmp(argv[1],"--lang")){ int l=lang_parse(argv[2],(int)strlen(argv[2])); if(l<0){ fprintf(stderr,"usage: --lang en|da\n"); return 2; } pv_lang=l; ai=3; }
    const char*out=argc>ai?argv[ai]:"/tmp/merakiplus_v6_preview_out";
    const char*cfgsrc=argc>ai+1?argv[ai+1]:"artifacts/config.json";
    gfx_init(); lang_set(pv_lang);
    snprintf(sched_path,sizeof sched_path,"%s/_preview_schedule",out);   /* v6.16: never the host's /new_customer (the selftest uses its work dir) */
    snprintf(snd_path,sizeof snd_path,"%s/_preview_sound",out);           /* v6.18: likewise */
    if(!strcmp(out,"--selftest")){ snprintf(sched_path,sizeof sched_path,"/nonexistent/merakiplus_preview_schedule"); snprintf(snd_path,sizeof snd_path,"/nonexistent/merakiplus_preview_sound"); return selftest(cfgsrc); }
    if(!strcmp(out,"--dump-table")){ dump_table(); return 0; }
    if(!strcmp(out,"--dump-strings")){ dump_strings(); return 0; }
    if(!strcmp(out,"--textw")){ for(int i=ai+1;i<argc;i++) printf("%4d %4d %4d %4d %4d  %s\n",text_w(argv[i],F_XS),text_w(argv[i],F_S),text_w(argv[i],F_M),text_w(argv[i],F_L),text_w(argv[i],F_XL),argv[i]); return 0; }
    snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc);
    if(!strcmp(out,"--bench")){ bench(); return 0; }
    if(!strcmp(out,"--is-merakiplus")){ int all=1; for(int i=ai+1;i<argc;i++){ bs.m_valid=0; int o=bin_is_merakiplus(argv[i]); printf("%d %s\n",o,argv[i]); all&=o; } return all?0:1; }
    mkdir(out,0755);
    /* v6.2: the boot state is read from a mock root in the out directory — never from the host's file system */
    static char pvr[400]; snprintf(pvr,sizeof pvr,"%s/_preview_root",out); snprintf(sysroot,sizeof sysroot,"%s",pvr);
    mock_boot_b(pvr); sw_sim=1; mono_fake=5000;
    /* save scenarios work on a COPY of config.json in the out directory */
    char cpy[400]; snprintf(cpy,sizeof cpy,"%s/_preview_config.json",out); copy_file(cfgsrc,cpy);
    snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s/_preview_cfgbak",out);
    /* v6.3: the language file also lives in the out directory. English = no file (default); Danish = the file "da" */
    snprintf(lang_path,sizeof lang_path,"%s/_preview_lang",out);
#define PV_LANG_BASE() do{ unlink(lang_path); lang_src=LS_DEFAULT; if(pv_lang!=LANG_EN) lang_save(pv_lang); lang_set(pv_lang); }while(0)
    PV_LANG_BASE();
    printf("preview language: %s (%s)\n",LANG_CODE[pv_lang],STR[pv_lang][TX_UL_EN+pv_lang]);

    sim_reset(); view=V_SPLASH; ui_start=sim_now; sim_run(900); dump(out,"00_splash");
    sim_reset(); sim_run(1500); dump(out,"01_home_ready");
    sim_reset(); reg[40]=612; reg[41]=612; reg[42]=431; reg[47]=884; sim_run(1500); dump(out,"02_home_heating");
    sim_reset(); sim_run(400); sim_tick=tick_grind; sim_t0=sim_now; sim_run(6600); dump(out,"03_home_grind");
    sim_reset(); sim_run(400); sim_tick=tick_brew; sim_t0=sim_now; sim_run(4200); dump(out,"04_home_brew_pre");
    sim_run(15300); dump(out,"05_home_brew_ext");
    sim_run(12000); dump(out,"08_home_done");
    sim_reset(); sim_run(400); sim_tick=tick_water; sim_t0=sim_now; sim_run(5200); dump(out,"06_home_hotwater");
    sim_reset(); sim_run(400); sim_tick=tick_steam; sim_t0=sim_now; sim_run(18500); dump(out,"07_home_steam");
    sim_reset(); sim_run(1500); ui_touch(TE_DOWN,210,372,sim_now); sim_run(380); dump(out,"09_home_hold_brew"); ui_touch(TE_UP,210,372,sim_now);
    sim_reset(); sim_link=0; last_rx_ms=sim_now-9000; sim_run(1500); dump(out,"10_home_offline");
    sim_reset(); reg[1]=1; reg[33]=0x0001; reg[40]=852; reg[47]=1025; sim_run(1500); dump(out,"11_home_sleep");
    sim_reset(); reg[35]=0x01; sim_run(1500); dump(out,"12_home_error");
    sim_reset(); rx_bad=3; last_bad_ms=sim_now; sim_run(1500); dump(out,"13_home_crc_warning");
#define PV_SEC(sc_) do{ page=PG_SYS; sec_open=(sc_); scroll[PG_SYS]=clampf((float)((sc_)*ROW_P),0,scroll_max(PG_SYS)); static_dirty=1; }while(0)   /* v6.17: SYSTEM with one section opened */
    /* v6.17: the BREW page (tap on the cup icon): title, live line, the feature's six settings (four in view), HOLD TO START */
    sim_reset(); page=PG_BREW; sim_run(600); dump(out,"14_page_brew");
    sim_reset(); page=PG_BREW; sim_run(300); ui_touch(TE_DOWN,240,300,sim_now); sim_run(60); ui_touch(TE_MOVE,240,220,sim_now); sim_run(60); ui_touch(TE_MOVE,240,150,sim_now); sim_run(120); dump(out,"15_page_brew_scrolled"); ui_touch(TE_UP,240,150,sim_now);
    sim_reset(); page=PG_STEAM; sim_run(300); ui_touch(TE_DOWN,240,180,sim_now); sim_run(100); dump(out,"16_page_steam_row_pressed"); memset(&tch,0,sizeof tch);
    sim_reset(); page=PG_SYS; sim_run(600); dump(out,"17_system");                 /* v6.17: SYSTEM closed — four sections + DIAGNOSTICS, no scrolling */
    sim_reset(); page=PG_BREW; sim_run(300); detail_open(S_BREWT,sim_now); sim_run(900); dump(out,"18_detail_brewtemp");
    tap(396,184); sim_run(120); tap(396,184); sim_run(200); ui_touch(TE_DOWN,396,184,sim_now); sim_run(150); dump(out,"19_detail_brewtemp_changed"); ui_touch(TE_UP,396,184,sim_now);
    sim_reset(); page=PG_BREW; sim_run(300); detail_open(S_PISW,sim_now); sim_run(900); dump(out,"20_detail_enum");
    sim_reset(); PV_SEC(SEC_CLOCK); sim_run(300); detail_open(S_TZ,sim_now); sim_run(900); dump(out,"21_detail_locked");
    sim_reset(); page=PG_GRIND; sim_run(300); detail_open(S_GRINDW,sim_now); sim_run(700);
    ui_touch(TE_DOWN,84,184,sim_now); sim_run(2600); dump(out,"22_detail_hold_accel"); ui_touch(TE_UP,84,184,sim_now);
    /* save OK: MCU ACK simulated + real config persist on a copy */
    snprintf(cfg_path,sizeof cfg_path,"%s",cpy);
    sim_reset(); page=PG_BREW; sim_run(300); detail_open(S_BREWT,sim_now); sim_run(600); tap(396,184); sim_run(100); tap(396,184); sim_run(200);
    tap(310,316); sim_run(700); dump(out,"23_saved_toast");
    printf("   (save scenario: brewTemper=%d, md5 %s, cmd6 ok=%ld)\n",sv(S_BREWT),cfg_md5,wq.n_ok);
    /* save FAILED: no ACK */
    sim_reset(); wq.sim_ack=0; page=PG_BREW; sim_run(300); detail_open(S_BREWW,sim_now); sim_run(600); tap(396,184); sim_run(200);
    tap(310,316); sim_run(160); dump(out,"24_detail_saving"); sim_run(700); dump(out,"25_save_fail_no_ack");
    printf("   (failure scenario: autobrewWeight=%d unchanged, retx=%ld fail=%ld)\n",sv(S_BREWW),wq.n_retx,wq.n_fail);
    snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc);
    sim_reset(); page=PG_DIAG; sim_run(600); dump(out,"26_diag");
    sim_reset(); sim_run(1500); tap(ACT_X[ACT_BREW],ACT_Y); sim_run(77); dump(out,"27_slide_transition");   /* v6.17: a tap on the cup icon slides the BREW page in */
    sim_reset(); sim_run(1500); tap(150,372); sim_run(700); dump(out,"28_page_grind_from_tap");   /* v6.17: a tap on the bean icon opens the GRIND page (v6.16: a toast "HOLD THE BUTTON TO START") */
    sim_reset(); sim_run(1500); ui_touch(TE_DOWN,330,372,sim_now); sim_run(800); ui_touch(TE_UP,330,372,sim_now); sim_run(500); dump(out,"29_home_steam_armed");
    /* ---- v6.1: spec alignment ---- */
    sim_reset(); page=PG_BREW; sim_run(300); detail_open(S_PITIME,sim_now); sim_run(900); dump(out,"30_detail_estimate_hint");
    sim_reset(); page=PG_WATER; sim_run(300); detail_open(S_HOTT,sim_now); sim_run(900); dump(out,"31_detail_sensor_vandtemp");
    sim_reset(); PV_SEC(SEC_STOCK); sim_run(300); detail_open(S_BREAK,sim_now); sim_run(900); dump(out,"32_detail_breaktime_locked");
    sim_reset(); PV_SEC(SEC_STOCK); sim_run(300); detail_open(S_LANG,sim_now); sim_run(600); for(int k=0;k<7;k++){ tap(396,184); sim_run(80); } sim_run(300); dump(out,"33_detail_language");
    sim_reset(); page=PG_STEAM; sim_run(300); detail_open(S_STEAMG,sim_now); sim_run(900); dump(out,"34_detail_steam_gear");
    sim_reset(); page=PG_SYS; sim_run(300); tap(240,row_y0(PG_SYS,SEC_STOCK)+25); sim_run(500);
    dump(out,"35_system_stock_open");         /* v6.17: a tap on the STOCK UI header opens the section at the top: BOOT UI + SWITCH first, then the stock-only config rows */
    /* config out of spec (copy with brewTemper=990): marking, blocked start, the stepper brings it back in */
    { char oor[400]; snprintf(oor,sizeof oor,"%s/_preview_config_oor.json",out); copy_file(cfgsrc,oor);
      snprintf(cfg_path,sizeof cfg_path,"%s",oor); cfg_set_int("brewTemper",990); cfg_set_int("preInfusionPressure",6);
      sim_reset(); page=PG_BREW; sim_run(600); dump(out,"36_page_brew_out_of_spec");
      sim_reset(); page=PG_BREW; sim_run(300); detail_open(S_BREWT,sim_now); sim_run(900); dump(out,"37_detail_out_of_spec");
      sim_reset(); sim_run(1500); ui_touch(TE_DOWN,210,372,sim_now); sim_run(900); ui_touch(TE_UP,210,372,sim_now); sim_run(300); dump(out,"38_home_start_blocked_out_of_spec");
      printf("   (out-of-spec scenario: brew start %s, cmd6 queued=%d, blocked=%ld)\n",act.pending==ACT_NONE?"BLOCKED":"STARTED",wq.n,wq.n_block);
      snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); }
    /* ---- v6.2: boot slot, runtime switch, exit reminder (all against the mock root) ---- */
#define SYS_BOTTOM() do{ sim_reset(); PV_SEC(SEC_STOCK); }while(0)                                  /* v6.17: SYSTEM > STOCK UI (v6.2-v6.16: the bottom of the SYSTEM list) */
#define ROW_TAP(k) tap(240,(int)(row_y0(PG_SYS,(k))-scroll[PG_SYS])+25)
    int nsys=SEC_STOCK+1;                                                                              /* the row of BOOT UI when STOCK UI is open */
    mock_reset(pvr); mock_slot_b(pvr,0,MB_NONE); mock_flag(pvr,0); mock_self(pvr,"/new_customer/merakiplus/merakiplus"); mock_proc(pvr,MOCK_STOCK_PID,SLOT_A_EXE,"demo_v9",'T',40); mock_rm(pvr,SLOT_A_EXE);
    SYS_BOTTOM(); sim_run(600); dump(out,"39_system_stock_boot_locked");
    mock_boot_b(pvr);
    SYS_BOTTOM(); sim_run(300); ROW_TAP(nsys+X_BOOTUI); sim_run(900); dump(out,"40_bootui_detail");
    tap(84,184); sim_run(500); dump(out,"41_bootui_changed");
    tap(310,316); sim_run(700); dump(out,"42_bootui_saved_toast");
    printf("   (BOOT UI scenario: flag %s after SAVE, %s)\n",mock_exists(pvr,BOOT_FLAG)?"EXISTS":"removed",bs.flag?"MERAKIPLUS":"ORIGINAL on next restart");
    mock_boot_b(pvr); mock_slot_b(pvr,1,MB_VENDOR);
    SYS_BOTTOM(); sim_run(300); ROW_TAP(nsys+X_BOOTUI); sim_run(900); dump(out,"43_bootui_locked");
    mock_boot_b(pvr);
    SYS_BOTTOM(); sim_run(300); ROW_TAP(nsys+X_SWITCH); sim_run(900); dump(out,"44_switch_confirm_boot");
    tap(310,316); sim_run(200); dump(out,"47_switching_bye");
    printf("   (switch scenario: view=%s, run=%d, stock started=%s)\n",view==V_BYE?"BYE":"?",(int)run,sw_child?"YES":"no (preview)");
    mock_runsh(pvr,6000);
    SYS_BOTTOM(); sim_run(300); ROW_TAP(nsys+X_SWITCH); sim_run(900); dump(out,"45_switch_confirm_runsh");
    mock_boot_b(pvr);
    SYS_BOTTOM(); sim_tick=tick_steam; sim_t0=sim_now; sim_run(4000); ROW_TAP(nsys+X_SWITCH); sim_run(600); tap(310,316); sim_run(400); dump(out,"46_switch_blocked_activity");
    /* exit reminder: run.sh mode, stock UI started BEFORE the latest SAVE (RESTART_NEEDED from scenario 23) */
    mock_runsh(pvr,40);
    /* v6.8: via SWITCH NOW (the REVERT button is gone). Afterwards the clock is set to where the v6.6 scenario ended (sim_run(1500) + tap + sim_run(900)),
     * so the later scenarios' animation phase (pulse, ripple) is unchanged and can be compared byte for byte with v6.6. */
    { sim_reset(); ms_t t48=sim_now; SYS_BOTTOM(); sim_run(1500); ROW_TAP(nsys+X_SWITCH); sim_run(900); tap(310,316); sim_run(900); dump(out,"48_exit_banner"); sim_now=t48+1518+99+924; }   /* sim_run moves in 33 ms steps: 1500 -> 1518, tap 70 -> 99, 900 -> 924 */
    printf("   (exit scenario: reminder %s, run=%d)\n",(view==V_SYS&&xsel==X_EXIT)?"SHOWN":"not shown",(int)run);
    mock_boot_b(pvr);
#define DG_SC_BOOT (DG_BOOT-28-LIST_VY0-26)   /* scroll that shows the whole boot block (the heading at the top of the field) */
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_SC_BOOT; sim_run(600); dump(out,"49_diag_boot");
    mock_runsh(pvr,40);
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_SC_BOOT; sim_run(600); dump(out,"50_diag_boot_runsh");
    mock_boot_b(pvr);
    /* ---- v6.3: UI language (own file), live switch, ABOUT block ---- */
    { int other=pv_lang==LANG_EN?LANG_DA:LANG_EN, bx=other>pv_lang?396:84;       /* + towards DANSK, − towards ENGLISH */
      sim_reset(); PV_SEC(SEC_DISPLAY); sim_run(300); detail_open(S_UILANG,sim_now); sim_run(900); dump(out,"51_detail_ui_language");
      tap(bx,184); sim_run(500); dump(out,"52_ui_language_live_preview");
      printf("   (language scenario: live preview = %s, saved = %s, file %s)\n",LANG_CODE[ui_lang],LANG_CODE[SET[S_UILANG].val],access(lang_path,F_OK)==0?"exists":"does not exist");
      tap(310,316); sim_run(700); dump(out,"53_ui_language_saved_toast");
      { char lb[8]=""; int fd=open(lang_path,O_RDONLY); int n=fd>=0?(int)read(fd,lb,sizeof lb-1):0; if(fd>=0) close(fd); lb[n>0?n:0]=0;
        printf("   (language scenario: SAVE -> %s contains \"%s\", UI language %s, cmd6 queued=%d, config writes=%d)\n",lang_path,lb,LANG_CODE[ui_lang],wq.n,cfg_writes); }
      sim_run(2600); page=PG_HOME; static_dirty=1; sim_run(600); dump(out,"54_home_other_language");   /* same run, HOME in the newly chosen language */
      PV_LANG_BASE(); (void)other; }
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=scroll_max(PG_DIAG); sim_run(600); dump(out,"55_diag_about");
    /* v6.5: DIAGNOSE scrolled by one row: RX CRC / RESYNC at the top (1 CRC error, 2 resync) and the boot push row at the bottom */
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_P; rx_bad=1; rx_resync=2; sim_run(600); dump(out,"56_diag_bootpush"); rx_bad=0; rx_resync=0;
    /* v6.6: phase semantics per STEAMLIVE (two steam paths, concurrent phases) + the re-push counter */
    sim_reset(); sim_run(400); sim_tick=tick_wand; sim_t0=sim_now; sim_run(2000); dump(out,"57_home_steam_wand");
    sim_run(2600); dump(out,"58_home_wand_over_target");                       /* probe 69.5° > target 65.0°: still STEAM·WAND, no auto-stop */
    sim_reset(); sim_run(400); sim_tick=tick_brew_wand; sim_t0=sim_now; sim_run(22400); dump(out,"59_home_brew_wand");   /* work_sta 5327 */
    sim_reset(); sim_run(400); sim_tick=tick_steam; sim_t0=sim_now; sim_run(34000); dump(out,"60_home_steam_done");       /* auto-stop: DONE + post-stop rise */
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_P; rps.n_ver=1; rps.n_def=1; rps.last=RPC_VER; sim_run(600); dump(out,"61_diag_repush"); memset(&rps,0,sizeof rps);
    /* v6.7: the NET row in the middle of a self-heal (wpa_supplicant killed, the IP still on wlan0) */
    /* v6.10: DIAGNOSE is longer (PRESSURE · ESTIMATE) — 62 and 66 are scrolled to v6.9's bottom (DG_SC_V69), so the NET and GRAPH rows are in the same place */
#define DG_SC_V69 (DG_PRESS+133-(LIST_VY1-14))
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_SC_V69; nm.wpa_pid=0; nm.carrier=0; nh_probe(&net.p,sim_now,1); net.state=NS_STARTING; sim_run(600); dump(out,"62_diag_net_heal");
    /* v6.9: display conditioning — brew with deadhead pulses (SMOOTH + RAW), idle after a brew with maintenance pulses, DIAGNOSE GRAPH = RAW */
    sim_reset(); cs_ws=0x1107; cs_p=358; cs_w39=0xFFFD; cs_w38=0; cs_pulse=0; cs_glitch=0; sim_tick=tick_cs; sim_t0=sim_now; sim_run(3000);
    cs_ws=0x1307; cs_p=1320; cs_pulse=1; for(int q=0;q<14;q++){ cs_w39=q*22; sim_run(1000); } dump(out,"63_home_brew_spikes");
    cd.raw=1; sim_run(200); dump(out,"64_home_brew_spikes_raw"); cd.raw=0;
    cs_w39=367; sim_tick=tick_idle_maint; sim_run(20000); dump(out,"65_home_idle_after_brew_pulses");
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_SC_V69; cd.raw=1; sim_run(600); dump(out,"66_diag_graph_raw"); cd.raw=0;
    /* v6.10: pressure estimate — DIAGNOSE PRESSURE · ESTIMATE during extraction (1346 = ≈15.0 BAR) and idle with a maintenance pulse (16670 = MAINTENANCE PULSE),
     * and the basis toast on tapping the chart during a brew */
#define DG_SC_PRESS (DG_PRESS-LIST_VY0-62)
    sim_reset(); cs_ws=0x1107; cs_p=358; cs_w39=0xFFFD; cs_w38=0; cs_pulse=0; cs_glitch=0; sim_tick=tick_cs; sim_t0=sim_now; sim_run(3000);
    cs_ws=0x1307; cs_p=1346; for(int q=0;q<6;q++){ cs_w39=q*30; sim_run(1000); } page=PG_DIAG; scroll[PG_DIAG]=DG_SC_PRESS; static_dirty=1; sim_run(600); dump(out,"67_diag_pressure_ext");
    sim_reset(); cs_w38=0; cs_w39=367; sim_tick=tick_idle_maint; sim_run(3000); sim_tick=NULL; reg[33]=0x0101|0x0800; reg[51]=16670; reg[52]=0;
    page=PG_DIAG; scroll[PG_DIAG]=DG_SC_PRESS; sim_run(600); dump(out,"68_diag_pressure_maint_pulse");
    sim_reset(); cs_ws=0x1107; cs_p=358; cs_w39=0xFFFD; cs_w38=0; cs_pulse=0; sim_tick=tick_cs; sim_t0=sim_now; sim_run(3000);
    cs_ws=0x1307; cs_p=1346; for(int q=0;q<8;q++){ cs_w39=q*25; sim_run(1000); } tap(240,268); sim_run(500); dump(out,"69_home_brew_pinfo_toast");
    /* v6.11: UX review — press feedback on the gear and the close chevron (held down), and DIAGNOSE's RAW COUNTERS section */
    sim_reset(); sim_run(1500); ui_touch(TE_DOWN,CX,GEAR_Y,sim_now); sim_run(120); dump(out,"70_home_gear_pressed"); memset(&tch,0,sizeof tch);
    sim_reset(); page=PG_BREW; static_dirty=1; sim_run(400); activate(T_ROW0+1,sim_now); sim_run(400); ui_touch(TE_DOWN,CX,57,sim_now); sim_run(120); dump(out,"71_detail_back_pressed"); memset(&tch,0,sizeof tch);
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_RAW-LIST_VY0-12; sim_run(600); dump(out,"72_diag_raw_counters");
    /* v6.13: manual STOP — water: the button held down, "STOPPING WATER…" after release, "WATER STOPPED" once the MCU has cleared the bits;
     * two buttons (brew + latched steam); and "STOP NOT CONFIRMED" when the bits stay set for 5 s */
    sim_reset(); sim_run(400); sim_tick=tick_water; sim_t0=sim_now; sim_run(4200); ui_touch(TE_DOWN,240,372,sim_now); sim_run(120); dump(out,"73_home_stop_water_pressed");
    ui_touch(TE_UP,240,372,sim_now); sim_run(170); dump(out,"74_home_stop_water_sending");
    sim_tick=NULL; reg[33]=0x0101; reg[16]=0; reg[51]=3; reg[52]=0; sim_run(400); dump(out,"75_home_stop_water_stopped");
    sim_reset(); sim_run(400); sim_tick=tick_brew_latched; sim_t0=sim_now; sim_run(12000); dump(out,"76_home_stop_dual_brew_steam");
    sim_reset(); sim_run(400); sim_tick=tick_water; sim_t0=sim_now; sim_run(1200); tap(240,372); sim_run(5300); dump(out,"77_home_stop_unconfirmed");
    /* v6.14: the power button — HOLD 1.5 s: TURNING OFF… (cmd8 + sub0 len2 on the way), OFF (controller silent), TURNING ON… (the button again);
     * blocked during a brew (toast); DIAGNOSE's POWER BUTTON block */
    sim_reset(); sim_run(400); pw_key(116,1,sim_now); sim_run(1560); dump(out,"78_power_turning_off");
    pw_key(116,0,sim_now); sim_link=0; sim_run(2000); dump(out,"79_power_off");
    pw_key(116,1,sim_now); sim_run(300); dump(out,"80_power_turning_on"); pw_key(116,0,sim_now);
    sim_reset(); sim_run(400); sim_tick=tick_brew; sim_t0=sim_now; sim_run(6000); pw_key(116,1,sim_now); sim_run(1700); dump(out,"81_home_power_blocked_brew"); pw_key(116,0,sim_now);
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_PWR-LIST_VY0-12; pw.n_ev=6; pw.n_press=3; pw.n_off=1; pw.n_on=1; pw.key_code=116; pw.key_val=0; pw.t_key=sim_now-4000; sim_run(600); dump(out,"82_diag_power");
    /* v6.15: 250 g cup at rest, then a brew — the graph starts from the tare (0 -> 36 g fills the weight axis) */
    sim_reset(); cb_late=0; cb_flush0=cb_flush1=0; sim_tick=tick_cupbrew; sim_t0=sim_now; sim_run(43800); dump(out,"83_home_brew_after_cup");
    /* v6.16: time & scheduling — the preview clock is 2026-11-18 16:35 local (Wednesday), zone #48 */
#define PV_SCHED(a_,b_,c_,d_) do{ sch.on=(a_); sch.hm=(b_); sch.rep=(c_); sch.ao=(d_); sch.once_at=0; SET[S_PREHEAT].val=sch.on; SET[S_PHAT].val=sch.hm; SET[S_PHREP].val=sch.rep; SET[S_AUTOOFF].val=sch.ao; }while(0)
    sim_reset(); SET[S_TIMEMODE].val=0; PV_SCHED(1,390,1,0); sim_run(1500); dump(out,"84_home_clock12_preheat");
    sim_reset(); PV_SCHED(1,390,2,3); PV_SEC(SEC_SCHED); sim_run(600); dump(out,"85_system_schedule_open");
    sim_reset(); PV_SCHED(1,390,1,0); PV_SEC(SEC_SCHED); sim_run(300); detail_open(S_PHAT,sim_now); sim_run(400); edit_val=405; sim_run(200); dump(out,"86_detail_preheat_at");
    sim_reset(); PV_SEC(SEC_CLOCK); sim_run(300); detail_open(S_TZ,sim_now); sim_run(400); dump(out,"87_detail_timezone");
    sim_reset(); PV_SCHED(1,390,2,3); arm.n_sent=2; arm.n_ack=2; arm.ad=19; arm.ah=6; arm.am=30; idle_t0=sim_now-754000; page=PG_DIAG; scroll[PG_DIAG]=DG_TIME-LIST_VY0-12; sim_run(600); dump(out,"88_diag_time");
    sim_reset(); PV_SCHED(0,390,1,1); pv_sched=1; idle_t0=sim_now-(15*60000-20000); sim_run(1200); dump(out,"89_home_autooff_warn"); pv_sched=0;
    /* v6.17: the per-feature pages and the consolidated SYSTEM (docs/ux-audit-v617.md) */
    sim_reset(); page=PG_BREW; sim_run(600); ui_touch(TE_DOWN,CX,START_Y+START_H/2,sim_now); sim_run(400); dump(out,"90_page_brew_hold_start"); memset(&tch,0,sizeof tch);
    sim_reset(); page=PG_WATER; sim_run(600); dump(out,"91_page_water");
    sim_reset(); page=PG_STEAM; sim_run(600); dump(out,"92_page_steam");
    sim_reset(); sim_link=0; last_rx_ms=sim_now-9000; page=PG_BREW; sim_run(1500); dump(out,"93_page_brew_offline");                 /* the live line says why HOLD TO START is off */
    sim_reset(); SET[S_GRINDMODE].val=2; page=PG_GRIND; sim_run(600); dump(out,"94_page_grind_mode_time");
    sim_reset(); page=PG_SYS; sim_run(300); tap(240,row_y0(PG_SYS,SEC_DISPLAY)+25); sim_run(500); dump(out,"95_system_display_open");
    sim_reset(); SET[S_TIMEMODE].val=0; PV_SCHED(1,390,2,3); page=PG_SYS; sim_run(600); dump(out,"96_system_summaries_12h");            /* closed headers: language, clock (12 h), next pre-heat */
    sim_reset(); PV_SEC(SEC_CLOCK); sim_run(600); dump(out,"97_system_clock_open");
    sim_reset(); reg[35]=0x01; sim_run(1500); tap(240,262); sim_run(700); dump(out,"98_diag_from_home_error");                         /* HOME's "CODE E2 · TAP FOR DETAILS" -> DIAGNOSTICS */
    sim_reset(); page=PG_SYS; sim_run(400); ui_touch(TE_DOWN,CX,PG_TITLE_Y+6,sim_now); sim_run(120); dump(out,"99_page_back_pressed"); memset(&tch,0,sizeof tch);
    /* v6.18: sound — DISPLAY · SOUND scrolled to its sound rows, the two own editors and the (moved) VOLUME editor */
    sim_reset(); PV_SEC(SEC_DISPLAY); scroll[PG_SYS]=clampf((float)(3*ROW_P),0,scroll_max(PG_SYS)); sim_run(600); dump(out,"100_system_display_sound");
    sim_reset(); PV_SEC(SEC_DISPLAY); sim_run(300); detail_open(S_SOUND,sim_now); sim_run(900); dump(out,"101_detail_sound");
    sim_reset(); PV_SEC(SEC_DISPLAY); sim_run(300); detail_open(S_SNDRDY,sim_now); sim_run(900); dump(out,"102_detail_ready_tone");
    sim_reset(); PV_SEC(SEC_DISPLAY); sim_run(300); detail_open(S_VOL,sim_now); sim_run(900); dump(out,"103_detail_volume");
    sim_reset(); PV_SEC(SEC_DISPLAY); sim_run(300); detail_open(S_SNDRDY,sim_now); edit_val=1; save_begin(sim_now); sim_run(500); dump(out,"104_sound_saved_toast");
    unlink(snd_path); sim_reset(); snd_uptime_ovr=20; reg[63]=60; sim_run(SND_SETTLE_MS+800); reg[63]=100; sim_run(400); page=PG_DIAG; scroll[PG_DIAG]=DG_SND-LIST_VY0-12; static_dirty=1; force_all=1; sim_run(600); dump(out,"105_diag_sound");
    /* v6.22: water dosing gate — the manual-only WATER page (old / unknown controller, scale fault), HOME with manual water, DIAGNOSTICS › CONTROLLER */
    sim_reset(); pv_mcu_ver("V5.1.2",NULL); page=PG_WATER; sim_run(600); dump(out,"106_page_water_manual_old");
    sim_reset(); pv_mcu_ver("",NULL); page=PG_WATER; sim_run(600); dump(out,"107_page_water_manual_unknown");
    sim_reset(); SET[S_HOTMODE].val=2; page=PG_WATER; sim_run(600); dump(out,"108_page_water_mode_manual");
    sim_reset(); pv_mcu_ver("V5.1.2",NULL); sim_run(600); dump(out,"109_home_water_manual_label");
    sim_reset(); pv_mcu_ver("V5.1.2",NULL); sim_run(400); reg[19]=1; sim_tick=tick_water; sim_t0=sim_now; sim_run(5200); dump(out,"110_home_water_manual_running");
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_MCU-LIST_VY0-12; sim_run(600); dump(out,"111_diag_controller");
    sim_reset(); reg[35]=0x1000; page=PG_DIAG; scroll[PG_DIAG]=DG_MCU-LIST_VY0-12; sim_run(600); dump(out,"112_diag_controller_scale_fault");
    return 0;
}
#endif

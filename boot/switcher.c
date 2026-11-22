/* switcher.c — boot-persistence daemon for MerakiTech Gen 1 (Sanbon SAT62091M).
 *
 * PURPOSE: Observe touch READ-ONLY and trigger a runtime switch A→B (stock UI → merakiplus)
 * on a secret gesture. v6.12 (GESTUS-FIX.md): ONE continuous hold ≥5000 ms
 * in the screen CENTER (within radius R of (240,240)) — no multi-press.
 * The corner gesture (3× long-press ≥1.5 s at x<90,y<90 within 6 s) is kept as
 * fallback (SWITCHER_GESTURE=corner).
 *
 * GRAB BYPASS (FINDINGS 'Gestus-debug': stock demo_v9 grabs event0 EXCLUSIVELY
 * with EVIOCGRAB → a read-only switcher sees no events):
 *  (a) DUPLICATE NODE: opens ALL touch nodes (event0..9 with ABS_MT_POSITION_X,
 *      name TECHWIN/tw31xx or the same abs range) read-only at the same time and reads
 *      from all of them. If there is a duplicate node that stock does not grab, it delivers
 *      events → the gesture works without intervention. (Stock's grab only affects evdev
 *      clients on the grabbed node.)
 *  (b) SYSFS FALLBACK: the tw31xx driver exposes lu_coordinate_x/lu_coordinate_y/
 *      point_num in sysfs (a grab does not affect sysfs). If evdev is blind (0 events
 *      while sysfs sees touches) it switches automatically to sysfs polling (50 ms).
 *  (c) LD_PRELOAD SHIM (grab_shim.so): no-ops ioctl(EVIOCGRAB) for the stock process
 *      (/customer/demo_v9) via the env in demo.sh's guard line. Prepared locally; used
 *      by boot/ondevice/verify_gesture_v612.py if (a)+(b) fail on the device.
 *  Source select: SWITCHER_SOURCE=auto|evdev|sysfs (default auto).
 *
 * DESIGN (see BOOT.md for full documentation):
 *  - Opens input nodes O_RDONLY|O_NONBLOCK. NEVER EVIOCGRAB, NEVER writes to
 *    input/UART/fb/config. The stock UI's input queue is not affected.
 *  - In B mode merakiplus grabs touch exclusively (EVIOCGRAB) → the switcher gets no events
 *    and idles naturally. Switching B→A is handled by merakiplus's own button (BOOT.md §UI-krav).
 *  - Mode is verified via readlink(/proc/<pid>/exe) for each pidof demo_v9:
 *      /customer/demo_v9     → A mode (stock)  → the gesture may switch to B
 *      /new_customer/demo_v9 → B mode (merakiplus)  → does nothing
 *      /new_customer/merakiplus/rel/<name>/demo_v9 → B mode (a release started by the v6.21 launcher)
 *      no demo_v9            → does nothing
 *  - The watchdog race is avoided with SIGSTOP-first: test.sh only checks ps-grep "demo_v9"
 *    (1 s loop). Kill→wait→launch would leave a window where the watchdog
 *    restarts A on top of B. Therefore: SIGSTOP A (PID still exists) → launch B →
 *    verify B's exe link → SIGKILL A → wait for exit. If B fails → SIGCONT A (fallback).
 *  - Robustness: reopens the input node on error, 50 ms poll loop, flock single instance,
 *    log rotation at 64 KB, no reboot, no writes outside /tmp.
 *
 * RAW RANGE: auto-detected via EVIOCGABS on open (also works on a grabbed
 * node — a grab does not block ioctl). The device's actual range is 0..480 (FINDINGS
 * 'Gestus-debug'); the fallback constant is therefore 480 (not 4095). Same formula as
 * tinj.c/merakiplus: screen = (raw−min)·480/(max−min+1). The SIM keeps its declared
 * 0..4095 (sim/proto.py) and passes SWITCHER_RAW_MAX=4095 in file mode.
 *
 * BUILD (ARM, static, 0 warnings):
 *   zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 \
 *       -Wall -Wextra -Werror -o switcher switcher.c
 * SELF-TEST (host, deterministic synthetic input):
 *   zig cc -DSWITCHER_SELFTEST -O2 -Wall -Wextra -Werror -o switcher_selftest switcher.c
 * HOST DAEMON (host, for the file-stream test via test_switcher.py):
 *   zig cc -O2 -Wall -Wextra -Werror -o switcher_host switcher.c
 *
 * ENV (test/override):
 *   SWITCHER_GESTURE     center (default) | corner (fallback: 3×long-press in the corner)
 *   SWITCHER_HOLD_MS     minimum hold time (default 5000 center / 1500 corner)
 *   SWITCHER_CENTER_X/Y  gesture center (default 240/240)
 *   SWITCHER_CENTER_R    gesture radius in px (default 40; the stock buttons' edge
 *                        is ~45 px from center — 60 would overlap, see GESTUS-FIX.md)
 *   SWITCHER_INPUT       explicit input node (otherwise autodiscovery, all touch nodes)
 *   SWITCHER_INPUT_FILE  read a raw input_event stream from a file/FIFO instead of a node
 *                        (gesture timing follows event timestamps, not wall clock)
 *   SWITCHER_RAW_MAX     raw axis max in file mode (default 480 = the device's constant;
 *                        sim-fed streams use 4095)
 *   SWITCHER_SOURCE      auto (default) | evdev | sysfs
 *   SWITCHER_SYSFS_DIR   sysfs dir with lu_coordinate_x/y + point_num (otherwise auto-scan)
 *   SWITCHER_SYSFS_SCREEN=1  sysfs coordinates are already screen pixels (otherwise raw)
 *   SWITCHER_LOG         log file (default /tmp/switcher.log)
 *   SWITCHER_LOCK        lock file (default /tmp/switcher.lock)
 *
 * Own evdev definitions (no linux headers → can be built on a Mac for tests).
 * struct input_event is 16 bytes on the device's kernel 4.9/arm (32-bit timeval) —
 * proven via strace: read(6</dev/input/event0>, ..., 16).
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <poll.h>
#include <dirent.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* ---- evdev constants (linux/input.h values, asm-generic ioctl encoding) ---- */
#define EV_SYN_        0x00
#define EV_KEY_        0x01
#define EV_ABS_        0x03
#define SYN_REPORT_    0x00
#define BTN_TOUCH_     0x14a
#define ABS_X_         0x00
#define ABS_Y_         0x01
#define ABS_MT_POSITION_X_ 0x35
#define ABS_MT_POSITION_Y_ 0x36
#define ABS_MT_TRACKING_ID_ 0x39

struct sw_input_event {
    int32_t  tv_sec;
    int32_t  tv_usec;
    uint16_t type;
    uint16_t code;
    int32_t  value;
};

#define SW_IOC_READ  2u
#define SW_IOC(dir,type,nr,size) (((dir)<<30)|((size)<<16)|((type)<<8)|(nr))
#define SW_EVIOCGNAME(len) SW_IOC(SW_IOC_READ,'E',0x06,(len))
#define SW_EVIOCGABS(abs)  SW_IOC(SW_IOC_READ,'E',0x40+(abs),24)
struct sw_input_absinfo { int32_t value, minimum, maximum, fuzz, flat, resolution; };

/* ---- gesture parameters (BOOT.md §2 + GESTUS-FIX.md) ---- */
#define SCREEN_W      480
#define SCREEN_H      480
#define CORNER_X      90     /* fallback: top-left corner x<90 && y<90 */
#define CORNER_Y      90
#define CORNER_HOLD_MS 1500  /* fallback: each press ≥1.5 s */
#define WINDOW_MS     6000   /* fallback: 3 presses within 6 s */
#define COOLDOWN_MS   10000  /* after a trigger: ignore the gesture for 10 s */
#define GEST_CENTER   0
#define GEST_CORNER   1
#define CENTER_X_D    240    /* default center-hold point */
#define CENTER_Y_D    240
#define CENTER_R_D    40     /* safe radius: stock button edge ~45 px from center */
#define HOLD_MS_D     5000   /* center: one continuous hold ≥5 s */
#define RAW_MAX_D     480    /* the device's actual ABS_MT max (FINDINGS Gestus-debug) */

/* Debug hook: NULL in the self-test, logline in the daemon (shared by the gesture state machine). */
static void (*sw_dbg)(const char *fmt, ...) = NULL;

typedef struct {
    int raw_min_x, raw_max_x, raw_min_y, raw_max_y;
    int cur_x, cur_y;          /* screen pixels */
    int down;                  /* finger down */
    int pending;               /* down position not latched yet (waiting for SYN) */
    int64_t down_ms;
    int down_in_zone;
    int stayed_in_zone;
    int count;                 /* corner: number of valid long-presses in the window */
    int64_t window_start_ms;   /* corner: down time of the first valid press */
    /* configuration */
    int mode;                  /* GEST_CENTER | GEST_CORNER */
    int cx, cy, r, r2;         /* center zone: radius + radius squared */
    int hold_ms;               /* minimum hold/press time */
} Gesture;

static void gesture_reset_state(Gesture *g)
{
    g->cur_x = -1; g->cur_y = -1;
    g->down = 0; g->pending = 0; g->down_ms = 0;
    g->down_in_zone = 0; g->stayed_in_zone = 0;
    g->count = 0; g->window_start_ms = 0;
}

static void gesture_init(Gesture *g, int rminx, int rmaxx, int rminy, int rmaxy)
{
    memset(g, 0, sizeof *g);
    g->raw_min_x = rminx; g->raw_max_x = rmaxx;
    g->raw_min_y = rminy; g->raw_max_y = rmaxy;
    g->mode = GEST_CENTER;
    g->cx = CENTER_X_D; g->cy = CENTER_Y_D;
    g->r = CENTER_R_D; g->r2 = CENTER_R_D * CENTER_R_D;
    g->hold_ms = HOLD_MS_D;
    gesture_reset_state(g);
}

static void gesture_set_center(Gesture *g, int cx, int cy, int r, int hold_ms)
{
    g->mode = GEST_CENTER;
    g->cx = cx; g->cy = cy; g->r = r; g->r2 = r * r;
    g->hold_ms = hold_ms > 0 ? hold_ms : HOLD_MS_D;
    gesture_reset_state(g);
}

static void gesture_set_corner(Gesture *g, int hold_ms)
{
    g->mode = GEST_CORNER;
    g->hold_ms = hold_ms > 0 ? hold_ms : CORNER_HOLD_MS;
    gesture_reset_state(g);
}

/* ENV configuration (daemon + file mode; the self-test uses the setters directly). */
static void gesture_config_env(Gesture *g)
{
    const char *e;
    int hold = 0;
    if ((e = getenv("SWITCHER_HOLD_MS")) && *e) hold = atoi(e);
    if ((e = getenv("SWITCHER_GESTURE")) && *e && strcmp(e, "corner") == 0) {
        gesture_set_corner(g, hold);
        return;
    }
    int cx = CENTER_X_D, cy = CENTER_Y_D, r = CENTER_R_D;
    if ((e = getenv("SWITCHER_CENTER_X")) && *e) cx = atoi(e);
    if ((e = getenv("SWITCHER_CENTER_Y")) && *e) cy = atoi(e);
    if ((e = getenv("SWITCHER_CENTER_R")) && *e) r = atoi(e);
    if (r < 1) r = 1;
    if (r > 240) r = 240;
    gesture_set_center(g, cx, cy, r, hold);
}

static int scale_axis(int raw, int rmin, int rmax, int screen)
{
    if (rmax <= rmin) return 0;
    long v = (long)(raw - rmin) * screen / (rmax - rmin + 1);
    if (v < 0) v = 0;
    if (v >= screen) v = screen - 1;
    return (int)v;
}

/* Raw range choice (pure logic, unit-tested): EVIOCGABS results if present,
 * otherwise the device's constant 0..480. MT axes take precedence over ST axes. */
typedef struct {
    int have_x, have_y, have_mx, have_my;
    struct sw_input_absinfo x, y, mx, my;
} AbsProbe;

static void pick_ranges(const AbsProbe *p, int *rminx, int *rmaxx, int *rminy, int *rmaxy)
{
    *rminx = 0; *rmaxx = RAW_MAX_D; *rminy = 0; *rmaxy = RAW_MAX_D;
    if (p->have_x)  { *rminx = p->x.minimum;  *rmaxx = p->x.maximum; }
    if (p->have_y)  { *rminy = p->y.minimum;  *rmaxy = p->y.maximum; }
    if (p->have_mx) { *rminx = p->mx.minimum; *rmaxx = p->mx.maximum; }
    if (p->have_my) { *rminy = p->my.minimum; *rmaxy = p->my.maximum; }
}

static int in_zone(const Gesture *g)
{
    if (g->cur_x < 0 || g->cur_y < 0) return 0;
    if (g->mode == GEST_CORNER)
        return g->cur_x < CORNER_X && g->cur_y < CORNER_Y;
    long dx = g->cur_x - g->cx, dy = g->cur_y - g->cy;
    return dx * dx + dy * dy <= g->r2;
}

static void pos_update(Gesture *g, int x, int y)
{
    g->cur_x = x; g->cur_y = y;
    if (g->down && !g->pending && !in_zone(g)) g->stayed_in_zone = 0;
}

static void press_begin(Gesture *g, int64_t now_ms)
{
    if (g->down) return;
    g->down = 1;
    g->down_ms = now_ms;
    /* the position is latched at the first SYN_REPORT after down: tracking-id/BTN_TOUCH
     * arrive BEFORE the position events in the same batch (cf. tinj.c's ordering). */
    g->pending = 1;
    g->down_in_zone = 0;
    g->stayed_in_zone = 0;
}

static void latch_zone(Gesture *g)
{
    g->down_in_zone = in_zone(g);
    g->stayed_in_zone = g->down_in_zone;
    g->pending = 0;
}

/* Returns 1 exactly when the gesture triggers:
 *  center: one valid hold ≥hold_ms that stayed in the zone (on release).
 *  corner: third valid release within the window (legacy). */
static int press_end(Gesture *g, int64_t now_ms)
{
    if (!g->down) return 0;
    if (g->pending) latch_zone(g);   /* no SYN seen → best effort with the current position */
    int64_t dur = now_ms - g->down_ms;
    int valid = g->down_in_zone && g->stayed_in_zone && dur >= g->hold_ms;
    g->down = 0;
    if (g->mode == GEST_CENTER) {
        if (!valid && g->down_in_zone && dur >= 1000 && sw_dbg)
            sw_dbg("gesture: center hold %d ms invalid (too short or drifted out of the zone)",
                   (int)dur);
        return valid;
    }
    if (!valid) return 0;
    if (g->count == 0) g->window_start_ms = g->down_ms;
    g->count++;
    if (g->count >= 3) {
        int ok = (now_ms - g->window_start_ms) <= WINDOW_MS;
        g->count = 0;
        return ok;
    }
    return 0;
}

/* Feed one evdev event. now_ms = event timestamp (ms). Returns 1 on trigger. */
static int gesture_feed(Gesture *g, uint16_t type, uint16_t code, int32_t value,
                        int64_t now_ms)
{
    /* window expiry (corner): reset the count (press tracking is kept) */
    if (g->mode == GEST_CORNER && g->count > 0 &&
        now_ms - g->window_start_ms > WINDOW_MS)
        g->count = 0;

    if (type == EV_ABS_) {
        if (code == ABS_X_ || code == ABS_MT_POSITION_X_) {
            pos_update(g, scale_axis(value, g->raw_min_x, g->raw_max_x, SCREEN_W), g->cur_y);
        } else if (code == ABS_Y_ || code == ABS_MT_POSITION_Y_) {
            pos_update(g, g->cur_x, scale_axis(value, g->raw_min_y, g->raw_max_y, SCREEN_H));
        } else if (code == ABS_MT_TRACKING_ID_) {
            if (value >= 0) press_begin(g, now_ms);
            else            return press_end(g, now_ms);
        }
    } else if (type == EV_KEY_ && code == BTN_TOUCH_) {
        if (value) press_begin(g, now_ms);
        else       return press_end(g, now_ms);
    } else if (type == EV_SYN_ && code == SYN_REPORT_) {
        if (g->down && g->pending) latch_zone(g);
    }
    return 0;
}

/* Sysfs sample (bypass b): point_num > 0 = finger down at (x,y) SCREEN pixels.
 * The driver only updates on touch changes → the polling caller passes mono time.
 * Returns 1 on trigger (release after a valid hold). */
static int gesture_sysfs_sample(Gesture *g, int x, int y, int points, int64_t now_ms)
{
    if (points > 0) {
        if (!g->down) {
            press_begin(g, now_ms);
            pos_update(g, x, y);
            latch_zone(g);
        } else {
            pos_update(g, x, y);
        }
    } else if (g->down) {
        return press_end(g, now_ms);
    }
    return 0;
}

/* ---- mode classification (pure logic, unit-tested) ---- */
typedef enum { MODE_NONE = 0, MODE_A, MODE_B } Mode;
typedef enum { ACT_SWITCH_A = 0, ACT_IDLE_B, ACT_IDLE_NONE, ACT_IDLE_MIXED } Action;

/* v6.21 (OTA phase 1b): /new_customer/demo_v9 is the launcher, which exec()s a release
 * /new_customer/merakiplus/rel/<name>/demo_v9 (same PID) — both are B mode. */
#define REL_PREFIX "/new_customer/merakiplus/rel/"
static Mode classify_exe(const char *exe)
{
    if (!exe) return MODE_NONE;
    if (strcmp(exe, "/customer/demo_v9") == 0)     return MODE_A;
    if (strcmp(exe, "/new_customer/demo_v9") == 0) return MODE_B;
    size_t n = strlen(exe), pl = strlen(REL_PREFIX), sl = strlen("/demo_v9");
    if (n > pl + sl && strncmp(exe, REL_PREFIX, pl) == 0 && strcmp(exe + n - sl, "/demo_v9") == 0
        && exe[pl] != '.' && exe[pl] != '/' && strchr(exe + pl, '/') == exe + n - sl)
        return MODE_B;
    return MODE_NONE;
}

/* Decision based on the exe links of all demo_v9 PIDs.
 * Mixed A+B (must not happen) → no action (safest). */
static Action decide_action(const Mode *modes, int n)
{
    int na = 0, nb = 0;
    for (int i = 0; i < n; i++) {
        if (modes[i] == MODE_A) na++;
        else if (modes[i] == MODE_B) nb++;
    }
    if (nb > 0 && na > 0) return ACT_IDLE_MIXED;
    if (nb > 0)           return ACT_IDLE_B;
    if (na == 1)          return ACT_SWITCH_A;
    return ACT_IDLE_NONE;
}

#ifndef SWITCHER_SELFTEST
/* ============================ DAEMON (device/host) ============================ */
#include <sys/ioctl.h>

#define MAX_NODES 4
typedef struct {
    int fd;
    char path[40];
    long ev_count;
    int logged_first;
} Node;

typedef struct {
    int ok;
    char dir[160];
    int fx, fy, fn;            /* fds: lu_coordinate_x, lu_coordinate_y, point_num */
    int rminx, rmaxx, rminy, rmaxy;
    int last_x, last_y, last_points;
    int64_t touch_ms;          /* mono time of the last seen touch activity */
} Sysfs;

static const char *LOG_PATH  = "/tmp/switcher.log";
static const char *LOCK_PATH = "/tmp/switcher.lock";
static FILE *logf = NULL;
static volatile sig_atomic_t stopflag = 0;

static int64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void logline(const char *fmt, ...)
{
    if (!logf) return;
    if (ftell(logf) > 65536) {           /* simple rotation: truncate */
        fclose(logf);
        logf = fopen(LOG_PATH, "w");
        if (!logf) return;
    }
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
    fprintf(logf, "[%s UTC] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fputc('\n', logf);
    fflush(logf);
}

static void on_signal(int sig) { (void)sig; stopflag = 1; }

/* Find demo_v9 PIDs via pidof (busybox). Returns the count; pids[i] + exe[i]. */
static int find_demos(int *pids, char exes[][256], int max)
{
    int n = 0;
    FILE *p = popen("pidof demo_v9 2>/dev/null", "r");
    if (!p) return 0;
    int pid;
    while (n < max && fscanf(p, "%d", &pid) == 1) {
        char path[64], buf[256];
        snprintf(path, sizeof path, "/proc/%d/exe", pid);
        ssize_t r = readlink(path, buf, sizeof buf - 1);
        if (r > 0) { buf[r] = 0; pids[n] = pid; snprintf(exes[n], 256, "%s", buf); n++; }
    }
    pclose(p);
    return n;
}

static int pid_alive(int pid) { return kill(pid, 0) == 0 || errno != ESRCH; }

/* Wait until /proc/<pid>/exe is a Meraki+ (B-mode) executable (ms budget). 1=ok. */
static int wait_exe(int pid, int budget_ms)
{
    int64_t deadline = mono_ms() + budget_ms;
    while (mono_ms() < deadline) {
        char path[64], buf[256];
        snprintf(path, sizeof path, "/proc/%d/exe", pid);
        ssize_t r = readlink(path, buf, sizeof buf - 1);
        if (r > 0) {
            buf[r] = 0;
            if (classify_exe(buf) == MODE_B && pid_alive(pid)) return 1;
        }
        if (!pid_alive(pid)) return 0;
        usleep(50000);
    }
    return 0;
}

/* SIGSTOP-first switch A→B (see the file header for the watchdog-race rationale). */
static void do_switch(int pid_a)
{
    logline("switch A→B: SIGSTOP pid %d (watchdog still sees the PID)", pid_a);
    if (kill(pid_a, SIGSTOP) != 0) {
        logline("switch FAILED: SIGSTOP pid %d: %s", pid_a, strerror(errno));
        return;
    }
    pid_t child = fork();
    if (child < 0) {
        logline("switch FAILED: fork: %s — SIGCONT pid %d", strerror(errno), pid_a);
        kill(pid_a, SIGCONT);
        return;
    }
    if (child == 0) {
        /* child: detach + start B's demo_v9 (merakiplus) with the B libs */
        setsid();
        if (chdir("/new_customer") != 0) _exit(126);
        setenv("LD_LIBRARY_PATH", "/new_customer/libs:/config/wifi", 1);
        setenv("MERAKIPLUS_LAUNCH", "gesture", 1);   /* the launcher: an explicit start, try Meraki+ again */
        int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) { dup2(nul, 0); close(nul); }
        int out = open("/tmp/merakiplus_boot.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (out >= 0) { dup2(out, 1); dup2(out, 2); close(out); }
        execl("/new_customer/demo_v9", "./demo_v9", (char *)NULL);
        _exit(127);
    }
    logline("switch A→B: launched B pid %d (/new_customer/demo_v9)", (int)child);
    if (wait_exe((int)child, 2000)) {
        logline("switch A→B: B verified (exe link) — SIGKILL pid %d", pid_a);
        kill(pid_a, SIGKILL);
        int64_t deadline = mono_ms() + 2000;
        while (pid_alive(pid_a) && mono_ms() < deadline) usleep(50000);
        if (pid_alive(pid_a))
            logline("switch WARNING: pid %d still alive after SIGKILL", pid_a);
        else
            logline("switch A→B DONE: merakiplus running, stock A exited, no reboot");
    } else {
        logline("switch FAILED: B (pid %d) did not come up — fallback: SIGCONT A, kill B",
                (int)child);
        if (pid_alive((int)child)) kill((int)child, SIGKILL);
        kill(pid_a, SIGCONT);
    }
}

static void handle_trigger(void)
{
    int pids[16];
    char exes[16][256];
    Mode modes[16];
    int n = find_demos(pids, exes, 16);
    for (int i = 0; i < n; i++) modes[i] = classify_exe(exes[i]);
    Action a = decide_action(modes, n);
    for (int i = 0; i < n; i++)
        logline("mode-check: pid %d exe=%s → %s", pids[i], exes[i],
                modes[i] == MODE_A ? "A" : modes[i] == MODE_B ? "B" : "unknown");
    switch (a) {
    case ACT_SWITCH_A:
        logline("GESTURE TRIGGERED + mode A → runtime switch to merakiplus");
        for (int i = 0; i < n; i++)
            if (modes[i] == MODE_A) { do_switch(pids[i]); break; }
        break;
    case ACT_IDLE_B:
        logline("GESTURE TRIGGERED but mode B (merakiplus running) → no action "
                "(merakiplus has its own switch button)");
        break;
    case ACT_IDLE_MIXED:
        logline("GESTURE TRIGGERED but A+B running at the same time → no action (safest)");
        break;
    default:
        logline("GESTURE TRIGGERED but no demo_v9 → no action");
        break;
    }
}

/* Open ONE node and probe abs ranges + name. Returns fd or -1. */
static int probe_node(const char *path, AbsProbe *p, char *name, size_t namesz, int *has_mt)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    memset(p, 0, sizeof *p);
    p->have_x  = ioctl(fd, SW_EVIOCGABS(ABS_X_), &p->x) == 0;
    p->have_y  = ioctl(fd, SW_EVIOCGABS(ABS_Y_), &p->y) == 0;
    p->have_mx = ioctl(fd, SW_EVIOCGABS(ABS_MT_POSITION_X_), &p->mx) == 0;
    p->have_my = ioctl(fd, SW_EVIOCGABS(ABS_MT_POSITION_Y_), &p->my) == 0;
    *has_mt = p->have_mx;
    name[0] = 0;
    ioctl(fd, SW_EVIOCGNAME(namesz), name);
    return fd;
}

/* Input discovery (bypass a): ALL touch nodes are opened read-only at the same time.
 * event0 first (proven touch node), then event1..9; duplicates (same name
 * or same abs range) are kept — a duplicate that stock does not grab delivers events.
 * Returns the number of nodes; gesture ranges are set from the preferred node. */
static int open_nodes(Node *nodes, Gesture *g)
{
    const char *override = getenv("SWITCHER_INPUT");
    char path[64];
    int n = 0;
    int first = -1;
    for (int i = 0; i < 10 && n < MAX_NODES; i++) {
        if (override && *override) {
            if (i > 0) break;
            snprintf(path, sizeof path, "%s", override);
        } else {
            snprintf(path, sizeof path, "/dev/input/event%d", i);
        }
        AbsProbe p;
        char name[64];
        int has_mt = 0;
        int fd = probe_node(path, &p, name, sizeof name, &has_mt);
        if (fd < 0) continue;
        int name_ok = strcasestr(name, "TECHWIN") || strcasestr(name, "tw31xx");
        if (!has_mt && !name_ok) { close(fd); continue; }
        int rminx, rmaxx, rminy, rmaxy;
        pick_ranges(&p, &rminx, &rmaxx, &rminy, &rmaxy);
        logline("input: %s (name '%s', raw x %d..%d y %d..%d) — READ-ONLY, no grab",
                path, name, rminx, rmaxx, rminy, rmaxy);
        if (first < 0 && (has_mt || name_ok)) {
            first = n;
            gesture_init(g, rminx, rmaxx, rminy, rmaxy);
        }
        nodes[n].fd = fd;
        snprintf(nodes[n].path, sizeof nodes[n].path, "%s", path);
        nodes[n].ev_count = 0;
        nodes[n].logged_first = 0;
        n++;
    }
    return n;
}

/* ---- sysfs probe (bypass b): tw31xx's lu_coordinate_x/y + point_num ---- */
static int sysfs_open_dir(Sysfs *s, const char *dir)
{
    char p[200];
    snprintf(p, sizeof p, "%s/lu_coordinate_x", dir);
    if (access(p, R_OK) != 0) return 0;
    snprintf(p, sizeof p, "%s/lu_coordinate_y", dir);
    if (access(p, R_OK) != 0) return 0;
    snprintf(p, sizeof p, "%s/point_num", dir);
    if (access(p, R_OK) != 0) return 0;
    snprintf(p, sizeof p, "%s/lu_coordinate_x", dir);
    s->fx = open(p, O_RDONLY);
    snprintf(p, sizeof p, "%s/lu_coordinate_y", dir);
    s->fy = open(p, O_RDONLY);
    snprintf(p, sizeof p, "%s/point_num", dir);
    s->fn = open(p, O_RDONLY);
    if (s->fx < 0 || s->fy < 0 || s->fn < 0) {
        if (s->fx >= 0) close(s->fx);
        if (s->fy >= 0) close(s->fy);
        if (s->fn >= 0) close(s->fn);
        s->fx = s->fy = s->fn = -1;
        return 0;
    }
    snprintf(s->dir, sizeof s->dir, "%s", dir);
    s->last_x = s->last_y = -1; s->last_points = 0; s->touch_ms = 0;
    s->ok = 1;
    return 1;
}

static void sysfs_probe(Sysfs *s, const Gesture *g)
{
    memset(s, 0, sizeof *s);
    s->fx = s->fy = s->fn = -1;
    /* range: evdev-detected if available (ioctl also works on a grabbed node),
     * otherwise the device's constant 0..480. SCREEN=1: coordinates are already pixels. */
    if (getenv("SWITCHER_SYSFS_SCREEN")) {
        s->rminx = 0; s->rmaxx = SCREEN_W - 1;
        s->rminy = 0; s->rmaxy = SCREEN_H - 1;
    } else {
        s->rminx = g->raw_min_x; s->rmaxx = g->raw_max_x;
        s->rminy = g->raw_min_y; s->rmaxy = g->raw_max_y;
    }
    const char *env = getenv("SWITCHER_SYSFS_DIR");
    if (env && *env) {
        if (sysfs_open_dir(s, env))
            logline("input: sysfs %s (env) — grab bypass (b) ready", s->dir);
        return;
    }
    DIR *d = opendir("/sys/bus/i2c/devices");
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char dir[200];
        snprintf(dir, sizeof dir, "/sys/bus/i2c/devices/%s", de->d_name);
        if (sysfs_open_dir(s, dir)) {
            logline("input: sysfs %s (auto-scan) — grab bypass (b) ready", s->dir);
            break;
        }
    }
    closedir(d);
}

static int sysfs_read_int(int fd, int *out)
{
    char b[32];
    if (lseek(fd, 0, SEEK_SET) < 0) return 0;
    ssize_t n = read(fd, b, sizeof b - 1);
    if (n <= 0) return 0;
    b[n] = 0;
    *out = (int)strtol(b, NULL, 10);
    return 1;
}

/* Read a sysfs sample; returns 1 if read. x/y in screen pixels. */
static int sysfs_sample(Sysfs *s, int *x, int *y, int *points)
{
    int rx = 0, ry = 0, pn = 0;
    if (!sysfs_read_int(s->fn, &pn)) return 0;
    if (!sysfs_read_int(s->fx, &rx)) return 0;
    if (!sysfs_read_int(s->fy, &ry)) return 0;
    *points = pn;
    *x = scale_axis(rx, s->rminx, s->rmaxx, SCREEN_W);
    *y = scale_axis(ry, s->rminy, s->rmaxy, SCREEN_H);
    return 1;
}

static void sysfs_close(Sysfs *s)
{
    if (s->fx >= 0) close(s->fx);
    if (s->fy >= 0) close(s->fy);
    if (s->fn >= 0) close(s->fn);
    s->fx = s->fy = s->fn = -1;
    s->ok = 0;
}

#define SRC_NONE 0
#define SRC_EV   1
#define SRC_SYS  2

int main(void)
{
    const char *e;
    if ((e = getenv("SWITCHER_LOG")) && *e)  LOG_PATH = e;
    if ((e = getenv("SWITCHER_LOCK")) && *e) LOCK_PATH = e;

    logf = fopen(LOG_PATH, "a");
    sw_dbg = logline;
    /* single instance: flock (non-blocking) */
    int lockfd = open(LOCK_PATH, O_CREAT | O_RDWR, 0644);
    if (lockfd >= 0 && flock(lockfd, LOCK_EX | LOCK_NB) != 0) {
        logline("another switcher instance is already running — exit");
        if (logf) fclose(logf);
        return 0;
    }
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);

    Gesture g;
    gesture_init(&g, 0, RAW_MAX_D, 0, RAW_MAX_D);
    gesture_config_env(&g);
    if (g.mode == GEST_CENTER)
        logline("switcher started (pid %d): gesture = CENTER hold ≥%d ms at (%d,%d) "
                "radius %d px (one finger, stay in the zone, release after ≥%d ms)",
                (int)getpid(), g.hold_ms, g.cx, g.cy, g.r, g.hold_ms);
    else
        logline("switcher started (pid %d): gesture = CORNER fallback 3×long-press "
                "≥%d ms at x<%d,y<%d within %d s", (int)getpid(), g.hold_ms,
                CORNER_X, CORNER_Y, WINDOW_MS / 1000);

    Node nodes[MAX_NODES];
    int nnodes = 0;
    Sysfs sys;
    memset(&sys, 0, sizeof sys);
    sys.fx = sys.fy = sys.fn = -1;
    int fd = -1;
    int filemode = (e = getenv("SWITCHER_INPUT_FILE")) && *e;
    int source = SRC_NONE;
    const char *src_env = getenv("SWITCHER_SOURCE");
    int force_src = SRC_NONE;
    if (src_env && strcmp(src_env, "evdev") == 0) force_src = SRC_EV;
    if (src_env && strcmp(src_env, "sysfs") == 0) force_src = SRC_SYS;
    int64_t last_trigger = -COOLDOWN_MS;
    int64_t start_ms = mono_ms();
    int64_t last_sysfs_probe = 0;
    long ev_total = 0, ev_at_switch = 0;
    struct sw_input_event evs[64];

    while (!stopflag) {
        if (filemode) {
            if (fd < 0) {
                fd = open(getenv("SWITCHER_INPUT_FILE"), O_RDONLY | O_NONBLOCK);
                if (fd < 0) { usleep(500000); continue; }
                int rmax = RAW_MAX_D;
                if ((e = getenv("SWITCHER_RAW_MAX")) && *e) rmax = atoi(e);
                gesture_init(&g, 0, rmax, 0, rmax);
                gesture_config_env(&g);
                logline("input: FILE mode %s (raw 0..%d)", getenv("SWITCHER_INPUT_FILE"), rmax);
                source = SRC_EV;
            }
        } else if (nnodes == 0 && fd < 0) {
            nnodes = open_nodes(nodes, &g);
            if (nnodes > 0) gesture_config_env(&g);   /* range from node + env gesture */
            if (!sys.ok) sysfs_probe(&sys, &g);
            if (nnodes == 0 && !sys.ok) {
                /* open error (node gone/permissions) → idle loop, no crash */
                usleep(500000);
                continue;
            }
            if (force_src == SRC_SYS && sys.ok) source = SRC_SYS;
            else if (force_src == SRC_EV) source = SRC_EV;
            else if (nnodes > 0) source = SRC_EV;
            else source = SRC_SYS;   /* no evdev nodes, but sysfs found */
            logline("input sources: %d evdev nodes, sysfs=%s → source=%s", nnodes,
                    sys.ok ? sys.dir : "none",
                    source == SRC_SYS ? "sysfs" : "evdev");
        }
        if (!filemode && !sys.ok && mono_ms() - last_sysfs_probe > 5000) {
            last_sysfs_probe = mono_ms();
            sysfs_probe(&sys, &g);
            if (sys.ok && (source == SRC_NONE || force_src == SRC_SYS)) source = SRC_SYS;
        }

        struct pollfd pfds[MAX_NODES];
        int npfd = 0;
        if (filemode) {
            if (fd >= 0) { pfds[npfd].fd = fd; pfds[npfd].events = POLLIN; pfds[npfd].revents = 0; npfd++; }
        } else {
            for (int i = 0; i < nnodes; i++) {
                pfds[npfd].fd = nodes[i].fd;
                pfds[npfd].events = POLLIN;
                pfds[npfd].revents = 0;
                npfd++;
            }
        }
        int pr = npfd ? poll(pfds, (nfds_t)npfd, 50) : (usleep(50000), 0);
        if (pr < 0) {
            if (errno == EINTR) continue;
            for (int i = 0; i < nnodes; i++) close(nodes[i].fd);
            nnodes = 0;
            if (fd >= 0) { close(fd); fd = -1; }
            usleep(500000);
            continue;
        }
        int64_t now = mono_ms();
        /* --- evdev: read ALL nodes (bypass a); feed only if source=evdev --- */
        for (int i = 0; i < npfd; i++) {
            if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                if (filemode) {
                    logline("input: POLLERR/HUP — reopening");
                    close(fd); fd = -1; usleep(500000);
                } else {
                    logline("input: %s POLLERR/HUP — closing node", nodes[i].path);
                    close(nodes[i].fd);
                    nodes[i].fd = -1;   /* compacted after the loop (pfds index is preserved) */
                }
                continue;
            }
            if (!(pfds[i].revents & POLLIN)) continue;
            ssize_t n = read(pfds[i].fd, evs, sizeof evs);
            if (n < 0) {
                if (errno == EAGAIN || errno == EINTR) continue;
                logline("input: read error %s — reopening node", strerror(errno));
                if (filemode) { close(fd); fd = -1; }
                else { close(nodes[i].fd); nodes[i].fd = -1; }
                usleep(500000);
                continue;
            }
            if (n == 0) {
                if (filemode) { logline("input: EOF (file mode) — exit"); goto done; }
                logline("input: EOF — reopening node");
                close(nodes[i].fd);
                nodes[i].fd = -1;
                usleep(500000);
                continue;
            }
            size_t cnt = (size_t)n / sizeof(struct sw_input_event);
            ev_total += (long)cnt;
            if (!filemode && !nodes[i].logged_first) {
                nodes[i].logged_first = 1;
                logline("input: %s is delivering events (%d) — read-only bypass (a) active",
                        nodes[i].path, (int)cnt);
            }
            if (source != SRC_EV) continue;
            for (size_t k = 0; k < cnt; k++) {
                int64_t ev_ms = (int64_t)evs[k].tv_sec * 1000 + evs[k].tv_usec / 1000;
                int before = g.count;
                if (gesture_feed(&g, evs[k].type, evs[k].code, evs[k].value, ev_ms)) {
                    if (now - last_trigger < COOLDOWN_MS) {
                        logline("gesture triggered but cooldown active — ignored");
                    } else {
                        last_trigger = now;
                        handle_trigger();
                    }
                } else if (g.mode == GEST_CORNER && g.count > before) {
                    logline("gesture: valid corner long-press counted (%d/3)", g.count);
                }
            }
        }
        if (!filemode) {   /* compact the node list (closed fds marked -1) */
            int w = 0;
            for (int i = 0; i < nnodes; i++)
                if (nodes[i].fd >= 0) nodes[w++] = nodes[i];
            nnodes = w;
        }
        /* --- sysfs (bypass b): poll + source arbitration --- */
        if (sys.ok) {
            int sx = 0, sy = 0, pn = 0;
            if (sysfs_sample(&sys, &sx, &sy, &pn)) {
                if (pn > 0 || sx != sys.last_x || sy != sys.last_y)
                    sys.touch_ms = now;
                sys.last_x = sx; sys.last_y = sy; sys.last_points = pn;
                if (source == SRC_SYS) {
                    if (gesture_sysfs_sample(&g, sx, sy, pn, now)) {
                        if (now - last_trigger < COOLDOWN_MS) {
                            logline("gesture triggered but cooldown active — ignored");
                        } else {
                            last_trigger = now;
                            handle_trigger();
                        }
                    }
                }
            } else if (source == SRC_SYS) {
                logline("input: sysfs read error — falling back to evdev");
                source = SRC_EV;
                gesture_init(&g, g.raw_min_x, g.raw_max_x, g.raw_min_y, g.raw_max_y);
                gesture_config_env(&g);
            }
            if (source == SRC_EV && force_src == SRC_NONE && ev_total == 0 &&
                now - start_ms > 3000 && now - sys.touch_ms < 2000) {
                logline("input: evdev BLIND (0 events while sysfs sees touches) — "
                        "stock is probably grabbing event0 → switching to sysfs (bypass b)");
                source = SRC_SYS;
                ev_at_switch = ev_total;
                gesture_init(&g, g.raw_min_x, g.raw_max_x, g.raw_min_y, g.raw_max_y);
                gesture_config_env(&g);
            }
            if (source == SRC_SYS && force_src == SRC_NONE && ev_total > ev_at_switch) {
                logline("input: evdev is delivering events again (shim/duplicate?) — back to evdev");
                source = SRC_EV;
                gesture_init(&g, g.raw_min_x, g.raw_max_x, g.raw_min_y, g.raw_max_y);
                gesture_config_env(&g);
            }
        }
        waitpid(-1, NULL, WNOHANG);      /* reap a possible B child */
    }
done:
    logline("switcher stopping (signal/EOF)");
    for (int i = 0; i < nnodes; i++) close(nodes[i].fd);
    if (fd >= 0) close(fd);
    sysfs_close(&sys);
    if (logf) fclose(logf);
    return 0;
}

#else
/* ============================ SELF-TEST (host) ============================ */
static int tests_run = 0, tests_fail = 0;
#define CHECK(cond, name) do { \
    tests_run++; \
    if (cond) { printf("  OK   %s\n", name); } \
    else { tests_fail++; printf("  FAIL %s\n", name); } \
} while (0)

static int64_t T; /* synthetic time axis (ms) */
static void ev(Gesture *g, uint16_t type, uint16_t code, int32_t v, int *trig)
{
    if (gesture_feed(g, type, code, v, T)) *trig = 1;
}
static void raw_xy(Gesture *g, int x, int y, int *trig)
{
    /* screen→raw like tinj.c: raw = px*(max-min+1)/480 */
    int rx = x * (g->raw_max_x - g->raw_min_x + 1) / SCREEN_W;
    int ry = y * (g->raw_max_y - g->raw_min_y + 1) / SCREEN_H;
    ev(g, EV_ABS_, ABS_MT_POSITION_X_, rx, trig);
    ev(g, EV_ABS_, ABS_MT_POSITION_Y_, ry, trig);
    ev(g, EV_ABS_, ABS_X_, rx, trig);
    ev(g, EV_ABS_, ABS_Y_, ry, trig);
}
static void down(Gesture *g, int x, int y, int *trig)
{
    ev(g, EV_ABS_, ABS_MT_TRACKING_ID_, 1, trig);
    raw_xy(g, x, y, trig);
    ev(g, EV_KEY_, BTN_TOUCH_, 1, trig);
    ev(g, EV_SYN_, SYN_REPORT_, 0, trig);
}
static void move(Gesture *g, int x, int y, int *trig)
{
    raw_xy(g, x, y, trig);
    ev(g, EV_SYN_, SYN_REPORT_, 0, trig);
}
static void up(Gesture *g, int *trig)
{
    ev(g, EV_ABS_, ABS_MT_TRACKING_ID_, -1, trig);
    ev(g, EV_KEY_, BTN_TOUCH_, 0, trig);
    ev(g, EV_SYN_, SYN_REPORT_, 0, trig);
}
/* hold for ms; returns 1 if the gesture triggered along the way */
static int hold(Gesture *g, int x, int y, int ms, int gap_after)
{
    int trig = 0;
    down(g, x, y, &trig);
    T += ms;
    up(g, &trig);
    T += gap_after;
    return trig;
}

int main(void)
{
    Gesture g;
    int trig;

    printf("switcher self-test (center hold + corner fallback + scaling + mode logic)\n");

    /* 1: center happy path — one hold 5200 ms at (240,240), raw 0..480 (device) */
    gesture_init(&g, 0, 480, 0, 480); T = 100000; trig = 0;
    trig |= hold(&g, 240, 240, 5200, 0);
    CHECK(trig == 1, "center: one hold 5.2 s at (240,240) → trigger on release");

    /* 2: center too short (3000 ms < 5000) */
    gesture_init(&g, 0, 480, 0, 480); T = 200000; trig = 0;
    trig |= hold(&g, 240, 240, 3000, 0);
    CHECK(trig == 0, "center: hold 3.0 s (<5 s) → no trigger");

    /* 3: center exactly 5000 ms → trigger (boundary) */
    gesture_init(&g, 0, 480, 0, 480); T = 250000; trig = 0;
    trig |= hold(&g, 240, 240, 5000, 0);
    CHECK(trig == 1, "center: hold exactly 5000 ms → trigger");

    /* 4: off-center (300,240): distance 60 > radius 40 */
    gesture_init(&g, 0, 480, 0, 480); T = 300000; trig = 0;
    trig |= hold(&g, 300, 240, 5200, 0);
    CHECK(trig == 0, "center: hold at (300,240) outside radius → no trigger");

    /* 5: radius boundary (raw 480: screen 279 = radius 39 valid; 281 = 41 invalid) */
    gesture_init(&g, 0, 480, 0, 480); T = 350000; trig = 0;
    trig |= hold(&g, 280, 240, 5200, 0);
    CHECK(trig == 1, "center: (280,240) → screen 279, radius 39 ≤ 40 → trigger");
    gesture_init(&g, 0, 480, 0, 480); T = 360000; trig = 0;
    trig |= hold(&g, 282, 240, 5200, 0);
    CHECK(trig == 0, "center: (282,240) → screen 281, radius 41 > 40 → no trigger");

    /* 6: finger drifts out of the zone during the hold → invalid */
    gesture_init(&g, 0, 480, 0, 480); T = 400000; trig = 0;
    down(&g, 240, 240, &trig); T += 2000;
    move(&g, 350, 350, &trig); T += 2000;
    move(&g, 240, 240, &trig); T += 1500;
    up(&g, &trig);
    CHECK(trig == 0, "center: hold that leaves the zone midway → no trigger");

    /* 7: center with raw 0..4095 (sim-declared range) — same formula */
    gesture_init(&g, 0, 4095, 0, 4095); T = 500000; trig = 0;
    trig |= hold(&g, 240, 240, 5200, 0);
    CHECK(trig == 1, "center: raw 0..4095 stream → trigger (scaling consistent)");

    /* 8: multi-press does not break center (only one valid hold counts) */
    gesture_init(&g, 0, 480, 0, 480); T = 600000; trig = 0;
    trig |= hold(&g, 240, 240, 80, 200);     /* tap */
    trig |= hold(&g, 240, 240, 80, 200);     /* tap */
    CHECK(trig == 0, "center: two short taps → no trigger");
    trig |= hold(&g, 240, 240, 5200, 0);
    CHECK(trig == 1, "center: taps followed by one 5.2 s hold → trigger");

    /* 9: corner fallback — 3×1600 ms at (45,45), 300 ms gap → span 5400 ≤6000 */
    gesture_init(&g, 0, 4095, 0, 4095); gesture_set_corner(&g, 0); T = 700000; trig = 0;
    trig |= hold(&g, 45, 45, 1600, 300);
    trig |= hold(&g, 45, 45, 1600, 300);
    trig |= hold(&g, 45, 45, 1600, 0);
    CHECK(trig == 1, "corner: 3×1.6 s corner hold within 6 s → trigger");
    CHECK(g.count == 0, "corner: count reset after trigger");

    /* 10: corner only 2 holds → no trigger */
    gesture_init(&g, 0, 4095, 0, 4095); gesture_set_corner(&g, 0); T = 800000; trig = 0;
    trig |= hold(&g, 45, 45, 1600, 300);
    trig |= hold(&g, 45, 45, 1600, 0);
    CHECK(trig == 0 && g.count == 2, "corner: 2 holds → no trigger (2/3)");

    /* 11: corner presses too short (1000 ms < 1500) */
    gesture_init(&g, 0, 4095, 0, 4095); gesture_set_corner(&g, 0); T = 900000; trig = 0;
    trig |= hold(&g, 45, 45, 1000, 200);
    trig |= hold(&g, 45, 45, 1000, 200);
    trig |= hold(&g, 45, 45, 1000, 0);
    CHECK(trig == 0 && g.count == 0, "corner: 3×1.0 s (too short) → no trigger");

    /* 12: a center hold does NOT count in corner mode (and vice versa) */
    gesture_init(&g, 0, 480, 0, 480); gesture_set_corner(&g, 0); T = 1000000; trig = 0;
    trig |= hold(&g, 240, 240, 5200, 0);
    CHECK(trig == 0 && g.count == 0, "corner: center hold does not count");
    gesture_init(&g, 0, 4095, 0, 4095); T = 1050000; trig = 0;
    trig |= hold(&g, 45, 45, 1600, 300);
    trig |= hold(&g, 45, 45, 1600, 300);
    trig |= hold(&g, 45, 45, 1600, 0);
    CHECK(trig == 0, "center: 3×1.6 s corner hold → no trigger (only one hold counts)");

    /* 13: scaling raw→screen (the tinj.c formula, both ranges) */
    CHECK(scale_axis(0, 0, 4095, 480) == 0,        "raw 0 → screen 0 (4095-range)");
    CHECK(scale_axis(4095, 0, 4095, 480) == 479,   "raw 4095 → screen 479");
    CHECK(scale_axis(2048, 0, 4095, 480) == 240,   "raw 2048 → screen 240 (center)");
    CHECK(scale_axis(0, 0, 480, 480) == 0,         "raw 0 → screen 0 (480-range)");
    CHECK(scale_axis(480, 0, 480, 480) == 479,     "raw 480 → screen 479 (device-max)");
    CHECK(scale_axis(240, 0, 480, 480) == 239,     "raw 240 → screen 239 (±1 px, tinj formula)");
    CHECK(scale_axis(767, 0, 4095, 480) == 89,     "raw 767 → screen 89 (corner)");
    CHECK(scale_axis(768, 0, 4095, 480) == 90,     "raw 768 → screen 90 (outside)");

    /* 14: raw range auto choice (pick_ranges): fallback = device constant 480 */
    AbsProbe p;
    int a, b, c, d;
    memset(&p, 0, sizeof p);
    pick_ranges(&p, &a, &b, &c, &d);
    CHECK(a == 0 && b == 480 && c == 0 && d == 480, "pick_ranges: no ioctl answer → 0..480");
    p.have_mx = 1; p.mx.minimum = 0; p.mx.maximum = 480;
    p.have_my = 1; p.my.minimum = 0; p.my.maximum = 480;
    pick_ranges(&p, &a, &b, &c, &d);
    CHECK(a == 0 && b == 480 && c == 0 && d == 480, "pick_ranges: EVIOCGABS 0..480 → 0..480");
    p.mx.maximum = 4095; p.my.maximum = 4095;
    pick_ranges(&p, &a, &b, &c, &d);
    CHECK(b == 4095 && d == 4095, "pick_ranges: EVIOCGABS 0..4095 → 0..4095 (auto)");
    p.have_x = 1; p.x.minimum = 5; p.x.maximum = 4000;
    pick_ranges(&p, &a, &b, &c, &d);
    CHECK(b == 4095, "pick_ranges: MT axes take precedence over ST axes");
    memset(&p, 0, sizeof p);
    p.have_x = 1; p.x.minimum = 5; p.x.maximum = 4000;
    p.have_y = 1; p.y.minimum = 6; p.y.maximum = 4001;
    pick_ranges(&p, &a, &b, &c, &d);
    CHECK(a == 5 && b == 4000 && c == 6 && d == 4001, "pick_ranges: ST-only probe is used");

    /* 15: sysfs sample (bypass b): center hold via polling */
    gesture_init(&g, 0, 480, 0, 480); T = 1100000; trig = 0;
    trig |= gesture_sysfs_sample(&g, 240, 240, 1, T);
    T += 2600; trig |= gesture_sysfs_sample(&g, 240, 240, 1, T);
    T += 2600; trig |= gesture_sysfs_sample(&g, 240, 240, 0, T);
    CHECK(trig == 1, "sysfs: center hold 5.2 s via point_num → trigger");
    gesture_init(&g, 0, 480, 0, 480); T = 1200000; trig = 0;
    trig |= gesture_sysfs_sample(&g, 240, 240, 1, T);
    T += 3000; trig |= gesture_sysfs_sample(&g, 240, 240, 0, T);
    CHECK(trig == 0, "sysfs: hold 3.0 s → no trigger");
    gesture_init(&g, 0, 480, 0, 480); T = 1300000; trig = 0;
    trig |= gesture_sysfs_sample(&g, 320, 240, 1, T);
    T += 5200; trig |= gesture_sysfs_sample(&g, 320, 240, 0, T);
    CHECK(trig == 0, "sysfs: off-center (320,240) → no trigger");
    gesture_init(&g, 0, 480, 0, 480); T = 1400000; trig = 0;
    trig |= gesture_sysfs_sample(&g, 240, 240, 1, T);
    T += 2000; trig |= gesture_sysfs_sample(&g, 350, 350, 1, T);
    T += 3200; trig |= gesture_sysfs_sample(&g, 240, 240, 0, T);
    CHECK(trig == 0, "sysfs: drift out of the zone → no trigger");

    /* 16: env configuration */
    setenv("SWITCHER_GESTURE", "corner", 1);
    setenv("SWITCHER_HOLD_MS", "1700", 1);
    gesture_init(&g, 0, 480, 0, 480); gesture_config_env(&g);
    CHECK(g.mode == GEST_CORNER && g.hold_ms == 1700, "env: SWITCHER_GESTURE=corner + HOLD_MS");
    setenv("SWITCHER_GESTURE", "center", 1);
    setenv("SWITCHER_CENTER_X", "100", 1);
    setenv("SWITCHER_CENTER_Y", "120", 1);
    setenv("SWITCHER_CENTER_R", "55", 1);
    unsetenv("SWITCHER_HOLD_MS");
    gesture_init(&g, 0, 480, 0, 480); gesture_config_env(&g);
    CHECK(g.mode == GEST_CENTER && g.cx == 100 && g.cy == 120 && g.r2 == 55 * 55 &&
          g.hold_ms == 5000, "env: center X/Y/R + default hold 5000");
    unsetenv("SWITCHER_GESTURE"); unsetenv("SWITCHER_CENTER_X");
    unsetenv("SWITCHER_CENTER_Y"); unsetenv("SWITCHER_CENTER_R");
    gesture_init(&g, 0, 480, 0, 480); gesture_config_env(&g);
    CHECK(g.mode == GEST_CENTER && g.cx == 240 && g.cy == 240 &&
          g.r2 == 40 * 40 && g.hold_ms == 5000, "env: defaults = center (240,240) r40 5000 ms");

    /* 17: mode classification */
    CHECK(classify_exe("/customer/demo_v9") == MODE_A,     "exe /customer/demo_v9 → A");
    CHECK(classify_exe("/new_customer/demo_v9") == MODE_B, "exe /new_customer/demo_v9 → B");
    CHECK(classify_exe("/new_customer/merakiplus/rel/6.21-42cd7b9d/demo_v9") == MODE_B, "exe of a release (via the launcher) → B");
    CHECK(classify_exe("/new_customer/merakiplus/rel/.stage-6.21/demo_v9") == MODE_NONE, "a stage directory is not B");
    CHECK(classify_exe("/new_customer/merakiplus/rel/a/b/demo_v9") == MODE_NONE, "nested path is not B");
    CHECK(classify_exe("/new_customer/merakiplus/rel//demo_v9") == MODE_NONE, "empty release name is not B");
    CHECK(classify_exe("/new_customer/merakiplus/rel/6.21/demo_v9x") == MODE_NONE, "other file name is not B");
    CHECK(classify_exe("/tmp/andet") == MODE_NONE,         "unknown exe → NONE");
    CHECK(classify_exe(NULL) == MODE_NONE,                 "NULL exe → NONE");

    /* 18: decision logic */
    Mode mA[1] = { MODE_A };
    Mode mB[1] = { MODE_B };
    Mode mAB[2] = { MODE_A, MODE_B };
    Mode mAA[2] = { MODE_A, MODE_A };
    CHECK(decide_action(mA, 1) == ACT_SWITCH_A,    "[A] → SWITCH_A");
    CHECK(decide_action(mB, 1) == ACT_IDLE_B,      "[B] → IDLE_B (merakiplus has its own button)");
    CHECK(decide_action(mAB, 2) == ACT_IDLE_MIXED, "[A,B] → IDLE_MIXED (safest)");
    CHECK(decide_action(mAA, 2) == ACT_IDLE_NONE,  "[A,A] (transient fork) → IDLE_NONE");
    CHECK(decide_action(NULL, 0) == ACT_IDLE_NONE, "[] → IDLE_NONE");

    /* 19: a new sequence after a trigger works again (center) */
    gesture_init(&g, 0, 480, 0, 480); T = 1500000; trig = 0;
    trig |= hold(&g, 240, 240, 5200, 0);
    CHECK(trig == 1, "center: first trigger");
    T += 20000; trig = 0;
    trig |= hold(&g, 240, 240, 5200, 0);
    CHECK(trig == 1, "center: second hold after trigger → trigger again");

    /* 20: MT-only stream (tracking-id without BTN_TOUCH) drives center detection */
    gesture_init(&g, 0, 480, 0, 480); T = 1600000; trig = 0;
    ev(&g, EV_ABS_, ABS_MT_TRACKING_ID_, 7, &trig);
    raw_xy(&g, 240, 240, &trig);
    ev(&g, EV_SYN_, SYN_REPORT_, 0, &trig);
    T += 5200;
    ev(&g, EV_ABS_, ABS_MT_TRACKING_ID_, -1, &trig);
    ev(&g, EV_SYN_, SYN_REPORT_, 0, &trig);
    CHECK(trig == 1, "center: MT-only stream (tracking-id) → trigger");

    /* 21: corner window expiry + taps between holds (legacy kept) */
    gesture_init(&g, 0, 4095, 0, 4095); gesture_set_corner(&g, 0); T = 1700000; trig = 0;
    trig |= hold(&g, 45, 45, 1600, 300);
    trig |= hold(&g, 45, 45, 1600, 0);
    T += 7000; /* >6 s since the first down */
    trig |= hold(&g, 45, 45, 1600, 0);
    CHECK(trig == 0 && g.count == 1, "corner: expired window → count restarts (1/3)");
    gesture_init(&g, 0, 4095, 0, 4095); gesture_set_corner(&g, 0); T = 1800000; trig = 0;
    trig |= hold(&g, 45, 45, 1600, 100);
    trig |= hold(&g, 45, 45, 80, 100);
    trig |= hold(&g, 45, 45, 1600, 100);
    trig |= hold(&g, 45, 45, 80, 100);
    trig |= hold(&g, 45, 45, 1600, 0);
    CHECK(trig == 1, "corner: taps between valid holds → trigger (span 5.16 s)");

    printf("\n%d tests, %d failed\n", tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
#endif

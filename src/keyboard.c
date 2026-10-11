#define _GNU_SOURCE
#include "keyboard.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static int fd = -1, tried;
static char open_err[160] = "";

// US layout: the unshifted key row for each character class.
static const char *ROW_DIGITS = "1234567890";
static const char *ROW_SHIFT_DIGITS = "!@#$%^&*()";
static const int DIGIT_KEYS[10] = { KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0 };
static const int LETTER_KEYS[26] = {
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
};
static const struct { char plain, shifted; int key; } PUNCT[] = {
    { ' ', ' ', KEY_SPACE }, { '-', '_', KEY_MINUS }, { '=', '+', KEY_EQUAL }, { '[', '{', KEY_LEFTBRACE },
    { ']', '}', KEY_RIGHTBRACE }, { '\\', '|', KEY_BACKSLASH }, { ';', ':', KEY_SEMICOLON }, { '\'', '"', KEY_APOSTROPHE },
    { '`', '~', KEY_GRAVE }, { ',', '<', KEY_COMMA }, { '.', '>', KEY_DOT }, { '/', '?', KEY_SLASH },
};

int keyboard_char(int c, int *code, int *shift)
{
    const char *p;
    *shift = 0;
    if (c >= 'a' && c <= 'z') { *code = LETTER_KEYS[c - 'a']; return 0; }
    if (c >= 'A' && c <= 'Z') { *code = LETTER_KEYS[c - 'A']; *shift = 1; return 0; }
    if (c && (p = strchr(ROW_DIGITS, c))) { *code = DIGIT_KEYS[p - ROW_DIGITS]; return 0; }
    if (c && (p = strchr(ROW_SHIFT_DIGITS, c))) { *code = DIGIT_KEYS[p - ROW_SHIFT_DIGITS]; *shift = 1; return 0; }
    for (size_t i = 0; i < sizeof PUNCT / sizeof *PUNCT; i++) {
        if (c == PUNCT[i].plain) { *code = PUNCT[i].key; return 0; }
        if (c == PUNCT[i].shifted) { *code = PUNCT[i].key; *shift = 1; return 0; }
    }
    return -1;
}

static const struct { const char *name; int key; } NAMED[] = {
    { "ENTER", KEY_ENTER }, { "RETURN", KEY_ENTER }, { "BACKSPACE", KEY_BACKSPACE }, { "TAB", KEY_TAB },
    { "ESCAPE", KEY_ESC }, { "ESC", KEY_ESC }, { "SPACE", KEY_SPACE }, { "DELETE", KEY_DELETE }, { "UP", KEY_UP },
    { "DOWN", KEY_DOWN }, { "LEFT", KEY_LEFT }, { "RIGHT", KEY_RIGHT }, { "HOME", KEY_HOME }, { "END", KEY_END },
    { "PAGEUP", KEY_PAGEUP }, { "PAGEDOWN", KEY_PAGEDOWN }, { "F1", KEY_F1 }, { "F2", KEY_F2 }, { "F3", KEY_F3 },
    { "F4", KEY_F4 }, { "F5", KEY_F5 }, { "F6", KEY_F6 }, { "F7", KEY_F7 }, { "F8", KEY_F8 }, { "F9", KEY_F9 },
    { "F10", KEY_F10 }, { "F11", KEY_F11 }, { "F12", KEY_F12 },
};

int keyboard_key(const char *name, int *code)
{
    char up[16];
    size_t n = strlen(name);
    if (!n || n >= sizeof up) return -1;
    for (size_t i = 0; i <= n; i++) up[i] = (char)toupper((unsigned char)name[i]);
    for (size_t i = 0; i < sizeof NAMED / sizeof *NAMED; i++)
        if (!strcmp(up, NAMED[i].name)) { *code = NAMED[i].key; return 0; }
    int shift;
    return n == 1 && !keyboard_char(tolower((unsigned char)name[0]), code, &shift) ? 0 : -1;
}

static void emit(int type, int code, int value)
{
    struct input_event e;
    memset(&e, 0, sizeof e);
    e.type = (unsigned short)type; e.code = (unsigned short)code; e.value = value;
    if (write(fd, &e, sizeof e) < 0) { /* the device went away: the next call finds out */ }
}

static void sleep_ms(int ms) { nanosleep(&(struct timespec){ ms / 1000, (long)(ms % 1000) * 1000000L }, NULL); }

// Create the keyboard once. MPC (libinput) notices a new device through udev, which takes a moment.
static int ensure(char *err, size_t en)
{
    if (fd >= 0) return 0;
    if (tried) { snprintf(err, en, "%s", open_err); return -1; }
    tried = 1;
    int f = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (f < 0) f = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (f < 0) { snprintf(open_err, sizeof open_err, "No keyboard: /dev/uinput can't be opened (%s).", strerror(errno)); goto bad; }
    ioctl(f, UI_SET_EVBIT, EV_KEY);
    ioctl(f, UI_SET_EVBIT, EV_SYN);
    for (int k = 1; k < 249; k++) ioctl(f, UI_SET_KEYBIT, k);
    struct uinput_setup us;
    memset(&us, 0, sizeof us);
    us.id = (struct input_id){ .bustype = BUS_USB, .vendor = 0x1d6b, .product = 0x0104, .version = 1 };
    snprintf(us.name, sizeof us.name, "MPC Remote Keyboard");
    if (ioctl(f, UI_DEV_SETUP, &us) < 0 || ioctl(f, UI_DEV_CREATE) < 0) {
        snprintf(open_err, sizeof open_err, "No keyboard: the virtual device can't be created (%s).", strerror(errno));
        close(f);
        goto bad;
    }
    fd = f;
    sleep_ms(700);   // let udev and MPC's libinput pick the new keyboard up before the first key
    return 0;
bad:
    snprintf(err, en, "%s", open_err);
    return -1;
}

static void tap(int code, int shift)
{
    if (shift) { emit(EV_KEY, KEY_LEFTSHIFT, 1); emit(EV_SYN, SYN_REPORT, 0); }
    emit(EV_KEY, code, 1); emit(EV_SYN, SYN_REPORT, 0);
    sleep_ms(8);
    emit(EV_KEY, code, 0); emit(EV_SYN, SYN_REPORT, 0);
    if (shift) { emit(EV_KEY, KEY_LEFTSHIFT, 0); emit(EV_SYN, SYN_REPORT, 0); }
    sleep_ms(8);
}

int keyboard_type(const char *text, int *skipped, char *err, size_t en)
{
    int skip = 0;
    pthread_mutex_lock(&mtx);
    if (ensure(err, en)) { pthread_mutex_unlock(&mtx); return -1; }
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        int code, shift, c = *p;
        if (c == '\n') { tap(KEY_ENTER, 0); continue; }
        if (c == '\t') { tap(KEY_TAB, 0); continue; }
        if (c >= 0x80) { if (c >= 0xC0) skip++; continue; }   // one skip per UTF-8 character
        if (keyboard_char(c, &code, &shift)) { skip++; continue; }
        tap(code, shift);
    }
    pthread_mutex_unlock(&mtx);
    if (skipped) *skipped = skip;
    return 0;
}

int keyboard_press(const char *name, char *err, size_t en)
{
    int mods[3], nm = 0, code;
    char buf[64], *tok, *save = NULL;
    if (snprintf(buf, sizeof buf, "%s", name) >= (int)sizeof buf) { snprintf(err, en, "\"%s\" isn't a key.", name); return -1; }
    // "Ctrl+Shift+A": every part but the last is a modifier. A lone "+" is the plus key.
    char *last = strrchr(buf, '+');
    char *keypart = buf;
    if (last && last != buf) { *last = 0; keypart = last + 1; }
    else if (last == buf && buf[1]) keypart = buf + 1;
    if (keypart != buf) {
        for (tok = strtok_r(buf, "+", &save); tok && nm < 3; tok = strtok_r(NULL, "+", &save)) {
            if (!strcasecmp(tok, "shift")) mods[nm++] = KEY_LEFTSHIFT;
            else if (!strcasecmp(tok, "ctrl") || !strcasecmp(tok, "control")) mods[nm++] = KEY_LEFTCTRL;
            else if (!strcasecmp(tok, "alt")) mods[nm++] = KEY_LEFTALT;
            else { snprintf(err, en, "\"%s\" isn't a modifier (Shift, Ctrl, Alt).", tok); return -1; }
        }
    }
    if (keyboard_key(keypart, &code)) {
        int shift;
        if (!(strlen(keypart) == 1 && !keyboard_char((unsigned char)keypart[0], &code, &shift))) {
            snprintf(err, en, "\"%s\" isn't a key (Enter, Backspace, Tab, Escape, Delete, arrows, Home, End, PageUp, PageDown, F1-F12, or one character).", keypart);
            return -1;
        }
        if (shift && nm < 3) mods[nm++] = KEY_LEFTSHIFT;
    }
    pthread_mutex_lock(&mtx);
    if (ensure(err, en)) { pthread_mutex_unlock(&mtx); return -1; }
    for (int i = 0; i < nm; i++) { emit(EV_KEY, mods[i], 1); emit(EV_SYN, SYN_REPORT, 0); }
    tap(code, 0);
    for (int i = nm - 1; i >= 0; i--) { emit(EV_KEY, mods[i], 0); emit(EV_SYN, SYN_REPORT, 0); }
    pthread_mutex_unlock(&mtx);
    return 0;
}

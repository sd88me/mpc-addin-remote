// Keyboard injection: a virtual USB keyboard (uinput) that MPC reads like a plugged-in one, for its text fields.
#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stddef.h>

// Evdev key code and Shift for one US-layout character; 0 on success, -1 if it has no key (non-ASCII, control).
int keyboard_char(int c, int *code, int *shift);
// Evdev key code for a key name (Enter, Backspace, Tab, Escape, Space, Delete, Up, Down, Left, Right, Home, End,
// PageUp, PageDown, F1..F12, or one character, case-insensitive); 0 on success, -1 if unknown.
int keyboard_key(const char *name, int *code);
// Type UTF-8 text (characters without a key are skipped; *skipped says how many) / press one named key, with
// "+"-joined modifiers in front (Shift+Tab, Ctrl+A). 0 on success, else err says why.
int keyboard_type(const char *text, int *skipped, char *err, size_t en);
int keyboard_press(const char *name, char *err, size_t en);

#endif

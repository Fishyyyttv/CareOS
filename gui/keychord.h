/* gui/keychord.h -- normalise the letter of a modifier chord.
 * drivers/keyboard.c turns Ctrl+<letter> into its control code (letter & 0x1F),
 * so a Ctrl+Alt+M handler receives 0x0D, not 'm'. Chord checks compare
 * key_chord_letter(c) against lowercase letters instead. */
#ifndef CAREOS_KEYCHORD_H
#define CAREOS_KEYCHORD_H

static inline char key_chord_letter(char c) {
    if (c >= 1 && c <= 26) return (char)('a' + c - 1);   /* control code */
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    return c;
}

#endif

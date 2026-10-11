/* tests/host/test_keychord.c -- Ctrl+<letter> reaches the GUI as a control
 * code (drivers/keyboard.c: lc & 0x1F), so chord handlers must map it back. */
#include "pure.h"
#include "keychord.h"
#include <assert.h>
int main(void) {
    assert(key_chord_letter(0x0D) == 'm');   /* Ctrl+M */
    assert(key_chord_letter(0x08) == 'h');   /* Ctrl+H */
    assert(key_chord_letter(0x01) == 'a');
    assert(key_chord_letter(0x1A) == 'z');
    assert(key_chord_letter('m') == 'm');    /* plain letters pass through */
    assert(key_chord_letter('M') == 'm');
    assert(key_chord_letter('3') == '3');
    assert(key_chord_letter(0) == 0);
    puts("keychord OK");
    return 0;
}

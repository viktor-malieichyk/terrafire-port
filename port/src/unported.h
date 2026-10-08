/* Marker for original code paths that are not ported: UNPORTED() logs the
 * routine's address the first time it runs. Left: an effect kind without a
 * preset (0x110d0), attach-point placement (0x2c4a8), the copy after a view
 * shrink (0x2d88b) and callback 0x122bc, none of which the tested game
 * paths reach. */
#ifndef TF_UNPORTED_H
#define TF_UNPORTED_H

#include <stdbool.h>
#include <stdint.h>

/* Number of distinct unported routines reached so far. */
int unported_reached(void);
void unported_note(const char *addr, const char *what);

/* Log the first time an unported routine runs. */
#define UNPORTED(addr, what) do { \
        static bool unported_logged_; \
        if (!unported_logged_) { unported_logged_ = true; unported_note(addr, what); } \
    } while (0)

#endif

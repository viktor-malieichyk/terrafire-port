#include "unported.h"

#include <SDL3/SDL.h>

static int s_reached;

void unported_note(const char *addr, const char *what)
{
    s_reached++;
    SDL_Log("unported: %s (%s)", what, addr);
}

int unported_reached(void) { return s_reached; }


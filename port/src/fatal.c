#include "fatal.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdlib.h>

void tf_fatal(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "fatal: %s", msg);
    exit(1);
}

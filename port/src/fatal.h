#ifndef TF_FATAL_H
#define TF_FATAL_H

/* The original's fatal-error exit (0x264a0): report and quit. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn, format(printf, 1, 2)))
#endif
void tf_fatal(const char *fmt, ...);

#endif

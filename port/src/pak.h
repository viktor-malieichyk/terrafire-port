/* DATA.PAK access. The file has no directory: its table of contents is a
 * table of offsets compiled into the original PART2.EXE (0x43a60), which is
 * reproduced in pak.c so the port does not need the original executable. */
#ifndef TF_PAK_H
#define TF_PAK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAK_ENTRIES 107

/* Opens DATA.PAK, trying `dir` (may be NULL) and then the usual locations.
 * Fails if the file size does not match the original release. */
bool pak_open(const char *dir);
/* The folder DATA.PAK was found in (settings and saves live there too). */
const char *pak_dir(void);
void pak_close(void);

/* Reads a whole entry into a newly malloc'd buffer (caller frees). */
uint8_t *pak_read(int index, size_t *size_out);
size_t pak_entry_size(int index);

#endif

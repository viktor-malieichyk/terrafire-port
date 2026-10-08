#include "pak.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

/* Entry i spans offsets[i]..offsets[i+1]; the last value is the file size,
 * which the original checks at startup (0x1c5e4). */
static const uint32_t s_offsets[PAK_ENTRIES + 1] = {
          0,   42419,   89414,  122289,  178216,  189278,
     191781,  199154,  202611,  214676,  221168,  224550,
     244293,  249140,  252637,  258012,  259683,  261799,
     263880,  270032,  274524,  284236,  301799,  307622,
     320273,  340633,  551540,  555199,  584339,  644771,
     915752, 1300207, 1547992, 1824674, 2253953, 2306158,
    2491388, 2560542, 2625476, 2826105, 3173626, 3374255,
    3574884, 3922405, 3938433, 3971003, 3998047, 4027179,
    4067802, 4090222, 4295459, 4775971, 4809509, 4810279,
    4811049, 4811819, 4812589, 4813359, 4814129, 4814899,
    4815669, 4881205, 4885124, 4991307, 5010088, 5119833,
    5225006, 5296866, 5325499, 5330879, 5346605, 5346867,
    5348712, 5416482, 5516505, 5522460, 5530903, 5533139,
    5537866, 5549146, 5554202, 5555286, 5558843, 5563200,
    5641309, 5699875, 5714998, 5722259, 5724530, 5730130,
    5767873, 5835061, 5837123, 5886112, 5886919, 5945828,
    5971770, 6231394, 6232810, 6236407, 6239214, 6511696,
    6519727, 6519937, 6531866, 6543043, 6555208, 6582958,
};

static FILE *s_pak;
static char s_pak_dir[1024];

const char *pak_dir(void) { return s_pak_dir[0] ? s_pak_dir : NULL; }

static FILE *try_open(const char *dir, const char *name)
{
    char path[1024];
    if (dir && *dir)
        SDL_snprintf(path, sizeof path, "%s%s%s", dir,
                     dir[SDL_strlen(dir) - 1] == '/' || dir[SDL_strlen(dir) - 1] == '\\' ? "" : "/", name);
    else
        SDL_snprintf(path, sizeof path, "%s", name);
    return fopen(path, "rb");
}

bool pak_open(const char *dir)
{
    /* Original filename is upper case; try both for case-sensitive filesystems. */
    static const char *names[] = { "DATA.PAK", "data.pak" };
    const char *dirs[] = { dir, ".", SDL_GetBasePath(), NULL };
    for (int d = 0; d < 3 && !s_pak; d++) {
        if (d == 0 && !dir) continue;
        for (int n = 0; n < 2 && !s_pak; n++)
            s_pak = try_open(dirs[d], names[n]);
        if (s_pak) SDL_snprintf(s_pak_dir, sizeof s_pak_dir, "%s", dirs[d] ? dirs[d] : ".");
    }
    if (!s_pak) {
        SDL_Log("unable to find DATA.PAK");
        return false;
    }
    fseek(s_pak, 0, SEEK_END);
    long size = ftell(s_pak);
    if (size != (long)s_offsets[PAK_ENTRIES]) {
        SDL_Log("invalid resource file: DATA.PAK is %ld bytes, expected %u",
                size, (unsigned)s_offsets[PAK_ENTRIES]);
        pak_close();
        return false;
    }
    return true;
}

void pak_close(void)
{
    if (s_pak) fclose(s_pak);
    s_pak = NULL;
}

size_t pak_entry_size(int index)
{
    if (index < 0 || index >= PAK_ENTRIES) return 0;
    return s_offsets[index + 1] - s_offsets[index];
}

uint8_t *pak_read(int index, size_t *size_out)
{
    if (!s_pak || index < 0 || index >= PAK_ENTRIES) {
        SDL_Log("attempted to load invalid pak element (%d)", index);
        return NULL;
    }
    size_t size = pak_entry_size(index);
    uint8_t *buf = malloc(size ? size : 1);
    if (!buf) return NULL;
    if (fseek(s_pak, (long)s_offsets[index], SEEK_SET) != 0 || fread(buf, 1, size, s_pak) != size) {
        free(buf);
        return NULL;
    }
    if (size_out) *size_out = size;
    return buf;
}

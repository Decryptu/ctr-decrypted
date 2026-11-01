/* Apply an xdelta (VCDIFF) patch to a file, with the game's own decoder
   (platform/native_xdelta_decode.c), streaming so a whole disc image is never
   in memory. Used by convert_racer.py to read racer mods.

   Build: gcc -O2 -Iinclude tools/racers/xdelta_apply.c -o xdelta_apply
   Usage: xdelta_apply <source> <patch> <output> */
#include "../../platform/native_xdelta_decode.c"

struct ApplyFiles
{
    FILE *source;
    FILE *patch;
    FILE *output;
};

static long apply_read_patch(void *context, void *destination, size_t size)
{
    struct ApplyFiles *f = (struct ApplyFiles *)context;
    size_t n = fread(destination, 1, size, f->patch);
    return ferror(f->patch) ? -1 : (long)n;
}

static long apply_read_source(void *context, unsigned long long offset, void *destination, size_t size)
{
    struct ApplyFiles *f = (struct ApplyFiles *)context;
    if (fseeko64(f->source, (off64_t)offset, SEEK_SET) != 0)
        return -1;
    size_t n = fread(destination, 1, size, f->source);
    return ferror(f->source) ? -1 : (long)n;
}

static int apply_write_target(void *context, const void *bytes, size_t size)
{
    struct ApplyFiles *f = (struct ApplyFiles *)context;
    return fwrite(bytes, 1, size, f->output) == size;
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "usage: xdelta_apply <source> <patch> <output>\n");
        return 2;
    }
    struct ApplyFiles f = {fopen(argv[1], "rb"), fopen(argv[2], "rb"), fopen(argv[3], "wb")};
    if (!f.source || !f.patch || !f.output)
    {
        fprintf(stderr, "cannot open the files\n");
        return 1;
    }
    fseeko64(f.source, 0, SEEK_END);
    struct NativeXdeltaIo io = {apply_read_patch, apply_read_source, apply_write_target, &f, (unsigned long long)ftello64(f.source), NULL};
    char error[256] = "";
    int result = NativeXdelta_Decode(&io, error, sizeof error);
    fclose(f.output);
    if (result != NATIVE_XDELTA_OK)
    {
        fprintf(stderr, "xdelta: %s (%d)\n", error, result);
        return 1;
    }
    return 0;
}

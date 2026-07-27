#include "../include/types.h"
#include "../include/storage.h"
#include "../include/engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <storage_dir> <mountpoint> [fuse options...]\n"
        "\n"
        "Mounts a CAS-backed FUSE filesystem at <mountpoint>,\n"
        "using <storage_dir> for content-addressed segment files and LMDB metadata.\n",
        prog);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    const char *storage_dir = argv[1];
    const char *mountpoint  = argv[2];

    cas_storage_t *storage = cas_storage_init(storage_dir);
    if (!storage) {
        fprintf(stderr, "cas_fs: failed to initialize storage at '%s' (errno=%d)\n", storage_dir, errno);
        return 2;
    }

    int fuse_argc = argc - 1;
    char **fuse_argv = &argv[1];
    fuse_argv[0] = argv[0];

    int rc = cas_fuse_mount(mountpoint, storage, fuse_argc, fuse_argv);

    cas_storage_close(storage);
    return rc;
}

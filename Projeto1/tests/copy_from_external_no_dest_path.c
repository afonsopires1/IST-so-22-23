#include "fs/operations.h"
#include <assert.h>
#include <stdio.h>

int main() {
    char *path_src = "tests/file_to_copy.txt";

    assert(tfs_init(NULL) != -1);

    assert(tfs_copy_from_external_fs(path_src,"./unexistent") == -1);

    printf("Successful test.\n");

    return 0;
}

#include "../fs/operations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {
    const char *file_path = "/f1";
    const char *link_path = "/l1";

    assert(tfs_init(NULL) != -1);

    // Create file
    int fd = tfs_open(file_path, TFS_O_CREAT);
    assert(fd != -1);

    const char write_contents[] = "Hello World!";

    // Write to file
    assert(tfs_write(fd, write_contents, sizeof(write_contents)));

    assert(tfs_close(fd) != -1);

    assert(tfs_sym_link(file_path, link_path) != -1);

    // Unlink file
    assert(tfs_unlink(file_path) != -1);

    printf("Successful test.\n");
}

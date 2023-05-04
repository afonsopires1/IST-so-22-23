#include "fs/operations.h"
#include <assert.h>
#include <operations.h>
#include <stdio.h>

int main() {
    assert(tfs_init(NULL) != -1);

    // Create a file to use as the target of the soft link
    int f = tfs_open("/target_file", TFS_O_CREAT);
    assert(f != -1);
    assert(tfs_close(f) != -1);

    // Create a soft link to the file
    assert(tfs_link("/target_file", "/soft_link") != -1);

    // Try to create a hard link to the soft link
    int result = tfs_sym_link("/soft_link", "/hard_link");

    // Check the return value of tfs_link
    assert(result != -1); // expected behavior

    assert(tfs_destroy() != -1);

    printf("Successful test.\n");

    return 0;
}

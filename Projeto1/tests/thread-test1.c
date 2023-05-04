#include "../fs/operations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#define THREAD_COUNT 2
#define FILES_TO_CREATE_PER_THREAD 10

void *create_files(void *arg) {
    int file_id = *((int *)arg);

    for (int i = 0; i < FILES_TO_CREATE_PER_THREAD; i++) {
        char file_path[MAX_FILE_NAME] = {'/'};
        sprintf(file_path + 1, "%d", file_id + i);

        int fd = tfs_open(file_path, TFS_O_CREAT);
        assert(fd != -1);

        assert(tfs_write(fd, file_path,  strlen(file_path) + 1));

        assert(tfs_close(fd) != -1);
    }

    return NULL;
}

/* Create files according to the number of threads and then checks their contents  
*/
int main() {
    pthread_t thread_ids[THREAD_COUNT]; 

    assert(tfs_init(NULL) != -1);

    int values[THREAD_COUNT];

    for (int i = 0; i < THREAD_COUNT; ++i) {
        // For each thread can start creating files with a unique starting index
        values[i] = i * FILES_TO_CREATE_PER_THREAD + 1;
    }

    for (int i = 0; i < THREAD_COUNT; ++i) {
        assert(pthread_create(&thread_ids[i], NULL, create_files, &values[i]) == 0);
    }
    
    // Wait for all the threads to be terminated
    for (int i = 0; i < THREAD_COUNT; ++i) {
        pthread_join(thread_ids[i], NULL);
    }

    // Check the content of the files
    for (int i = 0; i < THREAD_COUNT * FILES_TO_CREATE_PER_THREAD; ++i) {
        char file_path[MAX_FILE_NAME] = {'/'};
        sprintf(file_path + 1, "%d", i + 1);

        int fd = tfs_open(file_path, 0);
        assert(fd != -1);

        char read_contents[MAX_FILE_NAME];
        assert(tfs_read(fd, read_contents, MAX_FILE_NAME) != -1);

        assert(strcmp(file_path, read_contents) == 0);

        assert(tfs_close(fd) != -1);
    }

    assert(tfs_destroy() != -1);

    printf("Successful test.\n");

    return 0;
}

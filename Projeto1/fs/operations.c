#include "operations.h"
#include "config.h"
#include "state.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "betterassert.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <dirent.h>

#include <pthread.h>


tfs_params tfs_default_params() {
    tfs_params params = {
        .max_inode_count = 64,
        .max_block_count = 1024,
        .max_open_files_count = 16,
        .block_size = 1024,
    };
    return params;
}


int tfs_init(tfs_params const *params_ptr) {
    tfs_params params;
    if (params_ptr != NULL) {
        params = *params_ptr;
    } else {
        params = tfs_default_params();
    }

    if (state_init(params) != 0) {
        return -1;
    }

    // create root inode
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }
    return 0;
}


int tfs_destroy() {
    if (state_destroy() != 0) {
        return -1;
    }
    return 0;
}

static bool valid_pathname(char const *name) { 
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}

/**
 * Looks for a file.
 *
 * Note: as a simplification, only a plain directory space (root directory only)
 * is supported.
 *
 * Input:
 *   - name: absolute path name
 *   - root_inode: the root directory inode
 * Returns the inumber of the file, -1 if unsuccessful.
 */
static int tfs_lookup(char const *name, inode_t const *root_inode) {
    
    pthread_mutex_t open_file_table_mutex = PTHREAD_MUTEX_INITIALIZER;
    // TODO: assert that root_inode is the root directory
    if (!valid_pathname(name)) {
        return -1;
    }

    // Lock the mutex to protect the open file table
    pthread_mutex_lock(&open_file_table_mutex);

    // skip the initial '/' character
    name++;

    int inum = find_in_dir(root_inode, name);

    // Unlock the mutex
    pthread_mutex_unlock(&open_file_table_mutex);

    return inum;
}

/**
 * Opens a file.
 *
 * Input:
 *   - name: absolute path name
 *   - mode: mode to open (TRUNC, APPEND, CREAT)
 */
int tfs_open(char const *name, tfs_file_mode_t mode) {
    // Checks if the path name is valid
    if (!valid_pathname(name)) {
        return -1;
    }
    
    pthread_mutex_t root_inode_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);
    
    pthread_mutex_lock(&root_inode_mutex);
    
    ALWAYS_ASSERT(root_dir_inode != NULL,
                  "tfs_open: root dir inode must exist");
    int inum = tfs_lookup(name, root_dir_inode);
    size_t offset;

    if (inum >= 0) {
        // The file already exists
        inode_t *inode = inode_get(inum);
        ALWAYS_ASSERT(inode != NULL,
                      "tfs_open: directory files must have an inode");

        // Truncate (if requested) file to zero length

        if (inode->i_node_type == T_SOFT_LINK) { 
            inum = tfs_lookup(inode->i_target, root_dir_inode);
            if (inum == -1) {
            // tfs_lookup failed, return -1 to indicate an error
                pthread_mutex_unlock(&root_inode_mutex);
                return -1;
            }
        }
        if (mode & TFS_O_TRUNC) {
            if (inode->i_size > 0) {
                data_block_free(inode->i_data_block);
                inode->i_size = 0;
            }
        }
        // Determine initial offset (position to start r/w)
        // Append set offset to the end of the file
        if (mode & TFS_O_APPEND) {
            offset = inode->i_size;
        } else {
            offset = 0;
        }
    } else if (mode & TFS_O_CREAT) {
        // The file does not exist; the mode specified that it should be created
        // Create inode
        inum = inode_create(T_FILE);
        if (inum == -1) {
            pthread_mutex_unlock(&root_inode_mutex);
            return -1; // no space in inode table
        }

        // Add entry in the root directory
        if (add_dir_entry(root_dir_inode, name + 1, inum) == -1) {
            inode_delete(inum);
            pthread_mutex_unlock(&root_inode_mutex);
            return -1; // no space in directory
        }

        offset = 0;
    } else {
        pthread_mutex_unlock(&root_inode_mutex);
        return -1;
    }

    // Finally, add entry to the open file table and return the corresponding
    // handle
    pthread_mutex_unlock(&root_inode_mutex);
    return add_to_open_file_table(inum, offset);

    // Note: for simplification, if file was created with TFS_O_CREAT and there
    // is an error adding an entry to the open file table, the file is not
    // opened but it remains created
}

/**
 * Creates a soft link
 *
 * Input:
 *   - target_file: file that the soft link will store a reference of 
 *   - link_name: name of the hard link to be created
 */
int tfs_sym_link(char const *target_file, char const *link_name) {
    // Declare and initialize a mutex
    pthread_mutex_t root_inode_mutex = PTHREAD_MUTEX_INITIALIZER;

    inode_t *root_inode = inode_get(ROOT_DIR_INUM);
    // Acquire the mutex before accessing the root directory inode
    pthread_mutex_lock(&root_inode_mutex);
    // Find the inode number of the target file
    int target_file_inumber = tfs_lookup(target_file, root_inode);
    if (target_file_inumber == -1) {
        // tfs_lookup failed, release the mutex and return -1 to indicate an error
        pthread_mutex_unlock(&root_inode_mutex);
        return -1;
    }
    // Creates inode for the soft link
    int link_inode_inumber = inode_create(T_SOFT_LINK);

    if (link_inode_inumber == -1) {
        // inode_create failed, release the mutex and return -1 to indicate an error
        pthread_mutex_unlock(&root_inode_mutex);
        return -1;
    }
    inode_t *link_inode = inode_get(link_inode_inumber);
    // Copy the name of the target file to the i-target field of the link inode
    // Use MAX_FILE_NAME - 1 to make sure that the name is not too long
    strncpy(link_inode->i_target, target_file, MAX_FILE_NAME - 1);
    // Set the final character of the string to the null character ('\0') to terminate the string
    link_inode->i_target[MAX_FILE_NAME - 1] = '\0';


    // Add an entry to the directory inode to create the link
    if (add_dir_entry(root_inode, link_name + 1, link_inode_inumber) == -1) {
        // add_dir_entry failed, delete the inode and return -1 to indicate an error
        inode_delete(link_inode_inumber);
        pthread_mutex_unlock(&root_inode_mutex);
        return -1;
    }
    pthread_mutex_unlock(&root_inode_mutex);
    return 0;    
}


/**
 * Creates an hard link
 *
 * Input:
 *   - target_file: file that the hard link will point to 
 *   - link_name: name of the hard link to be created
 */
int tfs_link(const char *target_file, const char *link_name) {
  // Initialize the mutex
    pthread_mutex_t root_inode_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    inode_t *root_inode = inode_get(ROOT_DIR_INUM);
    
    // Lock the mutex before accessing the critical section
    pthread_mutex_lock(&root_inode_mutex);
    
    // Find the inode number of the target file
    int target_file_inumber = tfs_lookup(target_file, root_inode);
    if (target_file_inumber == -1) {
    // tfs_lookup failed, return -1 to indicate an error
        pthread_mutex_unlock(&root_inode_mutex);
        return -1;
    }

    // Find the inode for the target file
    inode_t *target_inode = inode_get(target_file_inumber);
    if (target_inode->i_node_type == T_SOFT_LINK) {
        // Can't create hard links for soft links
        pthread_mutex_unlock(&root_inode_mutex);
        return -1;
    }

    // Add an entry to the directory inode to create the link
    if (add_dir_entry(root_inode, link_name + 1, target_file_inumber) == -1) {
        // add_dir_entry failed, return -1 to indicate an error
        pthread_mutex_unlock(&root_inode_mutex);
        return -1;
    }

    // Increment the hard links count for the target file's inode
    target_inode->hard_links_count++;

    // Unlock the mutex after finishing the critical section
    pthread_mutex_unlock(&root_inode_mutex);

    // Return 0 to indicate success
    return 0;
}

int tfs_close(int fhandle) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1; // invalid fd
    }

    remove_from_open_file_table(fhandle);

    return 0;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
   // Initialize the mutex
    pthread_mutex_t open_file_table_mutex = PTHREAD_MUTEX_INITIALIZER;

    open_file_entry_t *file = get_open_file_entry(fhandle);
    
    if (file == NULL) {
        pthread_mutex_unlock(&open_file_table_mutex);
        return -1;
    }

    //  From the open file table entry, we get the inode
    inode_t *inode = inode_get(file->of_inumber);
    ALWAYS_ASSERT(inode != NULL, "tfs_write: inode of open file deleted");

    // Determine how many bytes to write
    size_t block_size = state_block_size();
    if (to_write + file->of_offset > block_size) {
        to_write = block_size - file->of_offset;
    }

    if (to_write > 0) {
        if (inode->i_size == 0) {
            // If empty file, allocate new block
            int bnum = data_block_alloc();
            if (bnum == -1) {
                pthread_mutex_unlock(&open_file_table_mutex);
                return -1; // no space
            }

            inode->i_data_block = bnum;
        }

        void *block = data_block_get(inode->i_data_block);
        ALWAYS_ASSERT(block != NULL, "tfs_write: data block deleted mid-write");

        // Perform the actual write
        memcpy(block + file->of_offset, buffer, to_write);

        // The offset associated with the file handle is incremented accordingly
        file->of_offset += to_write;
        if (file->of_offset > inode->i_size) {
            inode->i_size = file->of_offset;
        }
    }
    pthread_mutex_unlock(&open_file_table_mutex);
    return (ssize_t)to_write;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    
    pthread_mutex_t open_file_table_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    // Lock the mutex before accessing the open file table
    pthread_mutex_lock(&open_file_table_mutex);

    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        // Unlock the mutex after accessing the open file table
        pthread_mutex_unlock(&open_file_table_mutex);
        return -1;
    }

    // From the open file table entry, we get the inode
    inode_t const *inode = inode_get(file->of_inumber);
    ALWAYS_ASSERT(inode != NULL, "tfs_read: inode of open file deleted");

    // Determine how many bytes to read
    size_t to_read = inode->i_size - file->of_offset;
    if (to_read > len) {
        to_read = len;
    }

    if (to_read > 0) {
        void *block = data_block_get(inode->i_data_block);
        ALWAYS_ASSERT(block != NULL, "tfs_read: data block deleted mid-read");

        // Perform the actual read
        memcpy(buffer, block + file->of_offset, to_read);
        // The offset associated with the file handle is incremented accordingly
        file->of_offset += to_read;
    }

    // Unlock the mutex after accessing the open file table
    pthread_mutex_unlock(&open_file_table_mutex);

    return (ssize_t)to_read;
}

/**
 * Removes a link
 *
 * Input:
 *   - target_file: file to be removed
 */
int tfs_unlink(char const *target_file) {
    if(target_file == NULL){
        return -1;
    }
    pthread_mutex_t root_inode_mutex = PTHREAD_MUTEX_INITIALIZER;

    inode_t *root_inode = inode_get(ROOT_DIR_INUM);
    // Find the inode number of the target file
    
    // Lock the mutex
    pthread_mutex_lock(&root_inode_mutex);
    int target_file_inumber = tfs_lookup(target_file, root_inode);
    if (target_file_inumber == -1) {
    // tfs_lookup failed, return -1 to indicate an error
        pthread_mutex_unlock(&root_inode_mutex); // unlock the mutex
        return -1;
    }
    inode_t *target_inode = inode_get(target_file_inumber);
    // Remove the file entry from the root directory
    if (clear_dir_entry(root_inode, target_file + 1) == -1) {
        pthread_mutex_unlock(&root_inode_mutex); // unlock the mutex
        return -1;
    }
    // Delete a soft link
    if (target_inode->i_node_type == T_SOFT_LINK) {
        inode_delete(target_file_inumber);
    }
    target_inode->hard_links_count--;
    if (target_inode->hard_links_count == 0) {
        // delete inode
        inode_delete(target_file_inumber);
    }
    // Unlock the mutex
    pthread_mutex_unlock(&root_inode_mutex);
    return 0;
}


int tfs_copy_from_external_fs(const char *source_path, const char *dest_path) {
    // Declare a global mutex variable
    pthread_mutex_t file_system_mutex = PTHREAD_MUTEX_INITIALIZER;

    // Lock the mutex before accessing any file system data structures
    pthread_mutex_lock(&file_system_mutex);

    // Open the source file on the external file system in read-only mode
    int fhandle_src = open(source_path, O_RDONLY);
    if (fhandle_src == -1) {
        // Unlock the mutex before returning
        pthread_mutex_unlock(&file_system_mutex);
        return -1;
    }

    // Open the destination file on the internal file system in write-only mode, truncating any existing data
    int fhandle_dst = tfs_open(dest_path, TFS_O_TRUNC | TFS_O_APPEND | TFS_O_CREAT);
    if (fhandle_dst == -1) {
        // An error occurred while opening the destination file
        close(fhandle_src);
        // Unlock the mutex before returning
        pthread_mutex_unlock(&file_system_mutex);
        return -1;
    }

    const int BUFFER_SIZE = 1024;
    char buffer[BUFFER_SIZE];

    ssize_t bytes_read;
    // read(file to read from, buffer where the data will be stored, number of bytes to read)
    while ((bytes_read = read(fhandle_src,buffer,BUFFER_SIZE)) > 0 ) {
        // Writes the data to the destination file
        if(tfs_write(fhandle_dst,buffer,(size_t)bytes_read) == -1){
            close(fhandle_src);
            tfs_close(fhandle_dst);
            // Unlock the mutex before returning
            pthread_mutex_unlock(&file_system_mutex);
            return -1;
        }
    }

    close(fhandle_src);
    tfs_close(fhandle_dst);
    // Unlock the mutex before returning
    pthread_mutex_unlock(&file_system_mutex);
    return 0;
}





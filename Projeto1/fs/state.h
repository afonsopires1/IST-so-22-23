#ifndef STATE_H
#define STATE_H

#include "config.h"
#include "operations.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

/**
 * Directory entry in the file system
 * d_name - file name
 * d_inumber - inode number
 */
typedef struct {
    char d_name[MAX_FILE_NAME];
    int d_inumber;
} dir_entry_t;

typedef enum { T_FILE, T_DIRECTORY, T_SOFT_LINK } inode_type; // Different types of inodes 

/**
 * Inode 
 * i_node_type - type of inode
 * hard_links_count - number of hard links
 * i_size - size of the file or directory
 * i_data_block - data block number
 * i_target - stores the name of the file that the soft link points to 
 * 
 */
typedef struct {
    inode_type i_node_type;
    int hard_links_count;
    size_t i_size;
    int i_data_block;
    char i_target[MAX_FILE_NAME];
} inode_t;

typedef enum { FREE = 0, TAKEN = 1 } allocation_state_t; // State of a data block

/**
 * Open file entry (in open file table)
 * of_inumber - inode number (unique identifier for a file)
 * of_offset - offset (the current position within the file at which the next read or write operation will take place)
 * Offset is used to keep track of the current location in the file when r/w, to know where to pick up when it resumes r/w
 */
typedef struct {
    int of_inumber;
    size_t of_offset;
    pthread_mutex_t lock;
} open_file_entry_t;


int state_init(tfs_params);  // Initializing and clean up file system state
int state_destroy(void);

size_t state_block_size(void); // Size of a data block

int inode_create(inode_type n_type); // Create, delete and get inodes
void inode_delete(int inumber);
inode_t *inode_get(int inumber);

int clear_dir_entry(inode_t *inode, char const *sub_name); // Manipulate directory entries
int add_dir_entry(inode_t *inode, char const *sub_name, int sub_inumber);

int find_in_dir(inode_t const *inode, char const *sub_name); // Find a file or a directory in a directory

int data_block_alloc(void); // Alocate or free data blocks
void data_block_free(int block_number);
void *data_block_get(int block_number); // Get the data stored in a data block
 
int add_to_open_file_table(int inumber, size_t offset); // Add and remove entries from the open file table
void remove_from_open_file_table(int fhandle);

open_file_entry_t *get_open_file_entry(int fhandle); // Retrieve an entry from the open file table  

#endif // STATE_H

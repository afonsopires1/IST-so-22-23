#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include "logging.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "operations.h"
#include "config.h"
#include "state.h"
#include "send_msg.h"

// Number of inodes
#define MAX_BOXES 64

//global variables
int num_boxes = 0;

// Message box data structure
typedef struct message_box {
    char name[32]; 
    int num_subscribers;
    int num_messages;
    int num_publishers;   
    char subscriber_pipes[32][256];
    char publisher_pipe[256];
    pthread_mutex_t subscribers_lock;
} message_box_t;


// Array of message boxes
message_box_t *boxes[MAX_BOXES];

// Threads
pthread_t *worker_threads;

// Register pipe file descriptor
int reg_pipe;

int box_exists(const char *name) {
    //lookup for the box with the given name
    int inode_num = tfs_lookup(name, inode_get(ROOT_DIR_INUM));
    if (inode_num != -1) {
        return 1; // box with this name already exists
    }
    return 0; // box with this name does not exist
}

// Create a new message box
int create_message_box(const char *name) {
    char full_name[32];
    snprintf(full_name, 32, "/%s", name);    
    // Check if a box with the same name already exists
    if (tfs_lookup(full_name, inode_get(ROOT_DIR_INUM)) == -1) {
        fprintf(stdout, "ERROR Box %s already exists\n", name);
        return -1;
    }
    if (num_boxes >= MAX_BOXES) {
        fprintf(stdout, "ERROR Cannot create more message boxes. Maximum limit reached.\n");
        return -1;
    }

    int handle = tfs_open(full_name, TFS_O_CREAT);
    if (handle == -1) {
        fprintf(stdout, "ERROR Failed to create message box %s\n", name);
        return -1;
    }
    // Initialize variables
    message_box_t *box = malloc(sizeof(message_box_t));
    snprintf(box->name, 32, "%s", full_name);
    memset(box->subscriber_pipes, 0, sizeof(box->subscriber_pipes));
    memset(box->publisher_pipe, 0, sizeof(box->publisher_pipe));
    box->num_subscribers = 0;
    box->num_publishers = 0;
    box->num_messages = 0;
    boxes[handle] = box;
    num_boxes++;
    pthread_mutex_init(&box->lock, NULL);
    fprintf(stdout, "OK\n");
    return handle;
}

// Remove message box
int remove_message_box(const char *name) {
    char full_name[32];
    snprintf(full_name, 32, "/%s", name);
    int result = tfs_unlink(full_name);
    if (result == -1) {
        fprintf(stdout, "ERROR removing Box %s\n", name);
        return -1;
    }
    return 0;
}

// Find a message box 
int find_box(const char *name) {
    char full_name[32];
    snprintf(full_name, 32, "/%s", name); 
    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);
    int inum = tfs_lookup(full_name, root_dir_inode);
    if (inum >= 0) {
        inode_t *inode = inode_get(inum);
        if (inode != NULL) {
            return inum;
        }
    }
    return -1;
}

// Associates a subscriber with a message box
int add_subscriber(const char *box_name, const char *pipe_name) {
    // Find the message box with the given name
    char full_box_name[32];
    snprintf(full_box_name, 32, "/%s", box_name); 
    int box_index = find_box(full_box_name);
    if (box_index == -1) {
        WARN("Error: message box %s does not exist\n", box_name);
        return -1;
    }

    pthread_mutex_lock(&boxes[box_index]->subscribers_lock);
    // Check if the subscriber pipe is already registered
    for (int i = 0; i < boxes[box_index]->num_subscribers; i++) {
        if (strcmp(boxes[box_index]->subscriber_pipes[i], pipe_name) == 0) {
            WARN("Error: subscriber pipe %s is already registered to message box %s\n", pipe_name, box_name);
            pthread_mutex_unlock(&boxes[box_index]->subscribers_lock);
            return -1;
        }
    }

    // Add the subscriber pipe to the message box
    pthread_mutex_lock(&boxes[box_index]->subscribers_lock);
    if (strcpy(boxes[box_index]->subscriber_pipes[boxes[box_index]->num_subscribers], pipe_name) == NULL) {
        WARN("Error: strcpy failed when adding subscriber pipe %s to message box %s\n", pipe_name, box_name);
        pthread_mutex_unlock(&boxes[box_index]->subscribers_lock);
        return -1;
    }
    boxes[box_index]->num_subscribers++;

    WARN("Successfully added subscriber pipe %s to message box %s\n", pipe_name, box_name);
    return 0;
}

// Removes a subscriber from the message box
int remove_subscriber(const char *box_name, const char *pipe_name) {
    char full_box_name[32];
    snprintf(full_box_name, 32, "/%s", box_name); 
    // find the index of the message box in the array of boxes
    int box_index = find_box(full_box_name);
    if (box_index == -1) {
        WARN("Error: message box with name %s does not exist\n", box_name);
        return -1;
    }

    // check if there are no subscribers
    if(boxes[box_index]->num_subscribers == 0){
        WARN("Error: There are no subscribers in message box %s\n", box_name);
        return -1;
    }

    // Check if the subscriber pipe is already registered with the box
    int pipe_index = -1;
    pthread_mutex_lock(&boxes[box_index]->subscribers_lock);
    for (int i = 0; i < boxes[box_index]->num_subscribers; i++) {
        if (strcmp(boxes[box_index]->subscriber_pipes[i], pipe_name) == 0) {
            pthread_mutex_unlock(&boxes[box_index]->subscribers_lock);
            pipe_index = i;
            break;
        }
    }
    if (pipe_index == -1) {
        WARN("Error: subscriber pipe %s is not registered with message box %s\n", pipe_name, box_name);
        return -1;
    }

    // Remove the subscriber pipe
    pthread_mutex_lock(&boxes[box_index]->subscribers_lock);
    for (int i = pipe_index; i < boxes[box_index]->num_subscribers - 1; i++) {
        if (strcpy(boxes[box_index]->subscriber_pipes[i], boxes[box_index]->subscriber_pipes[i + 1]) != boxes[box_index]->subscriber_pipes[i]) {
            WARN("Error: failed to copy the subscriber pipes\n");
            pthread_mutex_unlock(&boxes[box_index]->subscribers_lock);
            return -1;
        }
    }
    boxes[box_index]->num_subscribers--;

    WARN("Successfully removed subscriber pipe %s from message box %s\n", pipe_name, box_name);
    return 0;
}

// Add a publisher to the message box
int add_publisher(const char *box_name, const char *pipe_name) {
    // Find the message box with the given name
    char full_box_name[32];
    snprintf(full_box_name, 32, "/%s", box_name); 
    int box_index = find_box(full_box_name);
    if (box_index == -1) {
        WARN("Error: message box %s does not exist\n", box_name);   
        return -1;
    }

    // Try to open the publisher's pipe
    int publisher_pipe = open(pipe_name, O_WRONLY);
    if (publisher_pipe < 0) {
        if (errno == EOF) {
            // Publisher has closed their end of the pipe
            fprintf(stderr, "Publisher has closed their end of the pipe. Exiting session.\n");
            return -1;
        } else {
            WARN("Error: Failed to open publisher pipe %s: %s\n", pipe_name, strerror(errno));
            return -1;
        }
    }

    // Check if the pipe already has a publisher
    if (boxes[box_index]->num_publishers == 1) {
        WARN("Error: Message box %s already has a publisher \n", box_name);
        return -1;
    }

    // Add the publisher pipe to the message box
    if (strcpy(boxes[box_index]->publisher_pipe, pipe_name) == NULL) {
        WARN("Error: strcpy failed when adding publisher pipe %s to message box %s\n", pipe_name, box_name);
        return -1;
    }

    boxes[box_index]->num_publishers = 1;

    WARN("Successfully added publisher pipe %s to message box %s\n", pipe_name, box_name);
    return 0;
}

// The publisher sends a message to the message box
int publish_message(const char *box_name, const char *message) {
    char full_box_name[32];
    snprintf(full_box_name, 32, "/%s", box_name); 
    // find the index of the message box in the array of boxes
    int box_index = find_box(full_box_name);
    if (box_index == -1) {
        WARN("Error: message box %s not found\n", box_name);
        return -1;
    }
    // checks if the box has no subscribers
    if (boxes[box_index]->num_subscribers == 0) {
        WARN("Error: no subscribers to message box %s\n", box_name);
        return -1;
    }

    int fd = tfs_open(full_box_name, TFS_O_APPEND);
    if (fd == -1) {
        WARN("Error: failed to open message box %s: %s\n", full_box_name, strerror(errno));
        return -1;
    }
    // Write the message to the message box file
    if (tfs_write(fd, message, strlen(message)) == -1) {
        WARN("Error: failed to write message to message box %s: %s\n", box_name, strerror(errno));
        tfs_close(fd);
        return -1;
    }

    pthread_mutex_lock(&boxes[box_index]->subscribers_lock);
    //iterate over the subscribers
    for (int i = 0; i < boxes[box_index]->num_subscribers; i++) {
        //open subscriber pipe
        int subscriber_fd = open(boxes[box_index]->subscriber_pipes[i], O_WRONLY);
        //if pipe cant be opened throw error
        if (subscriber_fd == -1) {
            WARN("Error: failed to open subscriber pipe %s: %s\n", boxes[box_index]->subscriber_pipes[i], strerror(errno));
            pthread_mutex_unlock(&boxes[box_index]->subscribers_lock);
            close(fd);
            return -1;
        }
        //if pipe cant write throw error
        if (write(subscriber_fd, message, strlen(message)) == -1) {
            WARN("Error: failed to write to subscriber pipe %s: %s\n", boxes[box_index]->subscriber_pipes[i], strerror(errno));
            close(subscriber_fd);
            pthread_mutex_unlock(&boxes[box_index]->subscribers_lock);
            close(fd);
            return -1;
        }
        close(subscriber_fd);
    }
}

//Threads
void* worker_thread(void* arg) {
    int register_pipe = *(int *)arg;
    char buffer[1024];
    while (1) {
        // read from the register pipe
        int bytes_read = read(register_pipe, buffer, 1024);
        if (bytes_read < 0) {
            fprintf(stderr, "Error reading from register pipe: %s\n", strerror(errno));
            pthread_exit(NULL);
        }

        uint8_t code = buffer[0];
        char client_pipe[256];
        char box_name[32];
        char error_message[1024];
        char* box_name = buffer + 1;
        int32_t return_code;
        switch (code) {
            case 1:
                // code for publisher registration
                memcpy(client_pipe, buffer + 1, sizeof(client_pipe));
                memcpy(box_name, buffer + 1 + sizeof(client_pipe), sizeof(box_name));
                int result = add_publisher(box_name, client_pipe);
                if (result == -1) {
                    fprintf(stderr, "Error: Failed to register publisher for message box %s with pipe %s\n", box_name, client_pipe);
                } else {
                    fprintf(stderr, "Success: Registered publisher for message box %s with pipe %s\n", box_name, client_pipe);
                }
                break;
            case 2: 
                // code for subscriber registration
                memcpy(client_pipe, buffer + 1, sizeof(client_pipe));
                memcpy(box_name, buffer + 1 + sizeof(client_pipe), sizeof(box_name));
                int result_add_sub = add_subscriber(box_name, client_pipe);
                if (result_add_sub == -1) {
                    fprintf(stderr, "Error: Failed to register subscriber for message box %s with pipe %s\n", box_name, client_pipe);
                } else {
                    fprintf(stderr, "Success: Registered subscriber for message box %s with pipe %s\n", box_name, client_pipe);
                }
                break;
            case 3:
                // code for box creation;
                memcpy(client_pipe, buffer + 1, sizeof(client_pipe));
                memcpy(box_name, buffer + 1 + sizeof(client_pipe), sizeof(box_name));
                int result_create_box = create_message_box(box_name);
                if (result_create_box == -1) {
                    return_code = -1;
                } else {
                    return_code = 0;
                }
                int manager_pipe = open(client_pipe, O_WRONLY);
                if (manager_pipe < 0) {
                    fprintf(stderr, "Error opening manager pipe %s: %s\n", client_pipe, strerror(errno));
                    return_code = -1;
                    snprintf(error_message, 1024, "Error opening manager pipe %s: %s", client_pipe, strerror(errno));
                } else {
                    // send response to the manager
                    uint8_t response_code = 4;
                    char message[1040];
                    memcpy(message, &response_code, sizeof(response_code));
                    memcpy(message + sizeof(response_code), &return_code, sizeof(return_code));
                    memcpy(message + sizeof(response_code) + sizeof(return_code), error_message, sizeof(error_message));
                    send_msg(manager_pipe, message);
                    close(manager_pipe);
                }
                break;
            case 5:
                // code for box removal
                memcpy(client_pipe, buffer + 1, sizeof(client_pipe));
                memcpy(box_name, buffer + 1 + sizeof(client_pipe), sizeof(box_name));
                int result = remove_message_box(box_name);
                if (result == -1) {
                    return_code = -1;
                } else {
                    return_code = 0;
                }

                // send response to manager
                uint8_t response_code = 6;
                char message[1040];
                memcpy(message, &response_code, sizeof(response_code));
                memcpy(message + sizeof(response_code), &return_code, sizeof(return_code));
                memcpy(message + sizeof(response_code) + sizeof(return_code), error_message, sizeof(error_message));
                send_msg(manager_pipe, message);
                close(manager_pipe);
                break;
            case 7: 
                // code for box listing
                // open the manager's named pipe for writing
                int manager_pipe = open(client_pipe, O_WRONLY);
                if (manager_pipe == -1) {
                    fprintf(stderr, "Error opening manager's named pipe %s\n", client_pipe);
                    return -1;
                }
                // iterate through all message boxes
                int last = 1;
                for (int i = 0; i < num_boxes; i++) {
                    if (boxes[i] != NULL) {
                        if (i == num_boxes - 1) {
                            last = 1;
                        } else {
                            last = 0;
                        }
                        // send response to the manager
                        uint8_t response_code = 8;
                        char message[1084];
                        memcpy(message, &response_code, sizeof(response_code));
                        memcpy(message + sizeof(response_code), &last, sizeof(last));
                        memcpy(message + sizeof(response_code) + sizeof(last), boxes[i]->name, sizeof(boxes[i]->name));
                        uint64_t box_size = 1024;
                        memcpy(message + sizeof(response_code) + sizeof(last) + sizeof(boxes[i]->name), &box_size, sizeof(box_size));
                        memcpy(message + sizeof(response_code) + sizeof(last) + sizeof(boxes[i]->name) + sizeof(box_size), &boxes[i]->num_publishers, sizeof(boxes[i]->num_publishers));
                        memcpy(message + sizeof(response_code) + sizeof(last) + sizeof(boxes[i]->name) + sizeof(box_size) + sizeof(boxes[i]->num_publishers), &boxes[i]->num_subscribers, sizeof(boxes[i]->num_subscribers));
                        send_msg(manager_pipe, message);
                    }

                }
                close(manager_pipe);
                break;
            default:
                fprintf(stderr, "Error: invalid code %d\n", code);
                break;
        }
    }
    pthread_exit(NULL);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: mbroker <pipename>\n");
        return -1;
    }
    char* register_pipe_name = argv[1];
    // Converts a string to an int
    int MAX_SESSIONS = atoi(argv[2]);
    pthread_t worker_threads[MAX_SESSIONS];

    // Initialize the tfs
    tfs_params params = tfs_default_params();

    if (tfs_init(&params) != 0) {
        fprintf(stderr, "Error initializing file system\n");
        return -1;
    }

    // Remove pipe if it does not exist
    if (unlink(register_pipe_name) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", register_pipe_name, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Create the register pipe
    int register_pipe = mkfifo(register_pipe_name, 0640);
    if (register_pipe < 0 && errno != EEXIST) {
        fprintf(stderr, "Error creating register pipe: %s\n", strerror(errno));
        return -1;
    }

    // Open the register pipe
    register_pipe = open(register_pipe_name, O_RDONLY);
    if (register_pipe < 0) {
        fprintf(stderr, "Error opening register pipe: %s\n", strerror(errno));
        return -1;
    }

    // Create worker threads
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (pthread_create(&worker_threads[i], NULL, worker_thread, (void*) &register_pipe) != 0) {
        fprintf(stderr, "Error creating worker thread: %s\n", strerror(errno));
        return -1;
        }
    }

    // Join worker threads
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (pthread_join(worker_threads[i], NULL) != 0) {
        fprintf(stderr, "Error joining worker thread: %s\n", strerror(errno));
        return -1;
        }
    }

    // Close the register pipe
    close(register_pipe);

    // Unlink the register pipe
    unlink(register_pipe_name);

    // Destroy the tfs at the end
    if (tfs_destroy() != 0) {
        fprintf(stderr, "Error destroying tfs\n");
        return -1;
    }

    // Exit
    return 0;
}

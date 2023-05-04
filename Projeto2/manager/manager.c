#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include "logging.h"
#include "send_msg.h"

#define CLIENT_PIPE_NAME_SIZE (256)
#define MESSAGE_SIZE (1+256+32)


static void print_usage() {
    fprintf(stderr, "usage: \n"
                    "   manager <register_pipe_name> create <box_name>\n"
                    "   manager <register_pipe_name> remove <box_name>\n"
                    "   manager <register_pipe_name> list\n");
}

int main(int argc, char **argv) {
    
    
    // Check if the correct number of arguments are passed in and Print usage if not and return an error code
    if (argc < 4) {
        print_usage();
        return 1;
    }

    char *register_pipe_name = argv[1]; // name of the pipe to register to mbroker
    char *operation = argv[2]; // operation to be performed (create/remove)
    char *box_name = argv[3]; // name of the box
    
    // Open the client pipe for reading
    char client_pipe_name[CLIENT_PIPE_NAME_SIZE];
    int client_pipe = mkfifo(client_pipe_name, 0640);
    if (client_pipe == -1) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Send request to mbroker
    int register_pipe = open(register_pipe_name, O_WRONLY);
    if (register_pipe == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (strcmp(operation, "create") == 0) {
        
        // Code for creating a box
        uint8_t code = 3;
        
        // Construct the message to be sent to the server
        char message[MESSAGE_SIZE];
        snprintf(message, MESSAGE_SIZE, "%d %s %s", code, client_pipe_name, box_name);
        
        // Send the message to the server
        send_msg(register_pipe, message);

        // Open the pipe for reading
        int rx = open(client_pipe_name, O_RDONLY);
        if (rx == -1) {
            fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

    char buffer[MESSAGE_SIZE];
    ssize_t ret = read(rx, buffer, MESSAGE_SIZE - 1);
    if (ret < 0) {
    // read function returned an error, print the error message and exit the program
    fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
}

    uint8_t response_code = buffer[0];  //Read the first byte of the buffer, which contains the response code.
    if (response_code == 4) { //Check if the response code is 4, which indicates a successful request
    
    // Extract the return code and error message from the buffer
    int32_t return_code;
    char error_message[1024];
    
    // Extract the return code and error message from the buffer
    sscanf(buffer + sizeof(uint8_t), "%d %s", &return_code, error_message); 
        
        // If the return code is 0, this means the request was successful
        if (return_code == 0) {
        fprintf(stdout, "OK\n"); 
        } 
        else {
            // If the return code is not 0, print out the error message
            fprintf(stdout, "ERROR %s\n", error_message); 
            }
        }

        close(rx); // close the reading pipe

    } else if (strcmp(operation, "remove") == 0) {
        // Send request to mbroker
        int register_pipe = open(register_pipe_name, O_WRONLY);
        if (register_pipe == -1) {
            fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        // Remove box code
        uint8_t code = 5; 
        char message[MESSAGE_SIZE];
        // [ code = 5 (uint8_t) ] | [ client_named_pipe_path (char[256]) ] | [ box_name (char[32]) ]
        snprintf(message, MESSAGE_SIZE, "%d %s %s", code, client_pipe_name, box_name);
        send_msg(register_pipe, message);

        // Open the pipe for reading
        int rx = open(client_pipe_name, O_RDONLY);
        if (rx == -1) {
            fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        // Read response from server
        char buffer[MESSAGE_SIZE];
        ssize_t ret = read(rx, buffer, MESSAGE_SIZE - 1);
        if (ret < 0) {
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        } 
        buffer[ret] = 0;

        // Extract the return code and error message from the buffer
        uint8_t message_code = buffer[0];
        int32_t return_code = *((int32_t *)(buffer + sizeof(uint8_t)));
        char *error_message = buffer + sizeof(uint8_t) + sizeof(int32_t);

        if (message_code == 4 && return_code == 0) {
            fprintf(stdout, "OK\n");
        } else {
            fprintf(stdout, "ERROR %s\n", error_message);
        }

        close(rx);
        close(register_pipe);
        unlink(client_pipe_name);
    } else if (strcmp(operation, "list") == 0) {
    // Send request to mbroker
    int register_pipe = open(register_pipe_name, O_WRONLY);
    if (register_pipe == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    // Send request to mbroker
    uint8_t code = 7;
    char message[MESSAGE_SIZE];
    // [ code = 7 (uint8_t) ] | [ client_named_pipe_path (char[256]) ]
    snprintf(message, MESSAGE_SIZE, "%d %s", code, client_pipe_name);
    send_msg(register_pipe, message);
    // Open the pipe for reading
    int rx = open(client_pipe_name, O_RDONLY);
    if (rx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Print out the list of boxes
    char buffer[MESSAGE_SIZE];
    ssize_t ret;
    int box_count = 0;
    while (true) {
        ret = read(rx, buffer, MESSAGE_SIZE - 1);
        if (ret < 0) {
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        } 
        else if (ret == 0) {
            break;
        }

        buffer[ret] = 0;

        // Check if the message is the end of list message
        if (strcmp(buffer, "/0") == 0) {
            break;
        }
        char box_name[32];
        size_t box_size;
        size_t n_publishers;
        size_t n_subscribers;
        sscanf(buffer, "%s %zu %zu %zu", box_name, &box_size, &n_publishers, &n_subscribers);
        fprintf("%s %zu %zu %zu\n", box_name, box_size, n_publishers, n_subscribers);
        box_count++;
    }
    if (box_count == 0) {
        fprintf(stdout, "NO BOXES FOUND\n");
    }
    // Close the pipe for reading
    close(rx);
    // Close the register pipe
    close(register_pipe);
    // Unlink the client pipe
    unlink(client_pipe_name);
    return 0;
    }
}

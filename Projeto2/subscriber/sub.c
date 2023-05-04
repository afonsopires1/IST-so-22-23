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

#define REGISTER_PIPE_NAME "mbroker_register.pipe"
#define CLIENT_PIPE_NAME_SIZE (256)
#define MESSAGE_SIZE (1+256+32)


int message_count = 0;

static void sig_handler(int sig) {
    if (sig == SIGINT) {
        if (signal(SIGINT, sig_handler) == SIG_ERR) {
            exit(EXIT_FAILURE);
        }
    }
    // Prints number of messages received before exiting
    fprintf(stderr, "Number of messages received before exiting - %d\n", message_count);
    // Leave session
    exit(EXIT_SUCCESS);
}


int main(int argc, char **argv) {
    
    //check if the number of arguments passed is less than 3
    if (argc < 3) {
        fprintf(stderr, "usage: sub <register_pipe_name> <box_name>\n");
        return 0;
    }
    
    // register the signal handler for SIGINT
    if (signal(SIGINT, sig_handler) == SIG_ERR) { 
        exit(EXIT_FAILURE);
    }

    // Register to the mbroker
    char *register_pipe_name = argv[1];
    // Box_name to subscribe 
    char *box_name = argv[2];

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
    
    // Subscribing code
    uint8_t code = 2; 
    char message[MESSAGE_SIZE];
    // [ code = 2 (uint8_t) ] | [ client_named_pipe_path (char[256]) ] | [ box_name (char[32]) ]
    snprintf(message, MESSAGE_SIZE, "%d %s %s", code, client_pipe_name, box_name);
    send_msg(register_pipe, message);

    // Open the pipe for reading
    int rx = open(client_pipe_name, O_RDONLY);
    if (rx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    // Print out any existing messages
    char buffer[MESSAGE_SIZE];
    ssize_t ret;
    while (true) {
        // read from the pipe into buffer
        ret = read(rx, buffer, MESSAGE_SIZE - 1);
        // Check for read error
        if (ret < 0) {
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        } 
        // check for end of file reached
        else if (ret == 0) {
            break;
        }
        // Check if the message is sent by the server with code 10
        uint8_t message_code = buffer[0];
        if (message_code == 10) {
            // Extract the message from the buffer
            char *message = buffer + sizeof(uint8_t);
            message_count++;
            fprintf(stdout, "%s\n", message);
        }
        // Check if the message is the end of subscription message
        if (strcmp(buffer, "/0") == 0) {
            break;
        }
    }

    // Close the pipe for reading
    close(rx);
    // Close the register pipe
    close(register_pipe);
    // Unlink the client pipe
    unlink(client_pipe_name);
    // Print out the number of messages received
    fprintf(stderr, "Number of messages received before exiting - %d\n", message_count);
    // Exit the program
    return 0;
}
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
#define MESSAGE_SIZE (1024)
#define SUBSCRIBER_MESSAGE_SIZE (1024)


int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: pub <register_pipe_name> <box_name>\n");
        return 0;
    }

    // Register to the mbroker
    char *register_pipe_name = argv[1];
    // Box_name to publish 
    char *box_name = argv[2];

    // Open the client pipe for writing
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

    // Publisher code
    uint8_t code = 1;
    char message[MESSAGE_SIZE];
    // [ code = 1 (uint8_t) ] | [ client_named_pipe_path (char[256]) ] | [ box_name (char[32]) ]
    snprintf(message, MESSAGE_SIZE, "%d %s %s", code, client_pipe_name, box_name);
    send_msg(register_pipe, message);
    close(register_pipe);
    // Open the pipe for writing
    int tx = open(client_pipe_name, O_WRONLY);
    if (tx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
   
    // Read response from server
    uint8_t response_code;
    int32_t return_code;
    char error_message[1024];

//Read the first byte of the response from the mbroker, which contains the response code
read(tx, &response_code, sizeof(response_code));

//Check if the response code is 2, which means the request was processed by the mbroker
if (response_code == 2) {
    //Read the next 4 bytes of the response, which contains the return code of the request
    read(tx, &return_code, sizeof(return_code));

    //Check if the return code is -1, which means an error occurred
    if (return_code == -1) {
        //Read the rest of the response, which contains the error message
        read(tx, error_message, sizeof(error_message));
        //Print the error message to the stderr
        fprintf(stderr, "[ERR]: %s\n", error_message);
    }
}

    // Send messages from stdin
    char buffer[MESSAGE_SIZE];
    while (fgets(buffer, MESSAGE_SIZE, stdin) != NULL) {
        code = 9;
        snprintf(message, MESSAGE_SIZE, "%d %s", code, buffer);
        send_msg(tx, message);
    }

    // Close the session
    close(tx);
    unlink(client_pipe_name);
    return 0;   
}
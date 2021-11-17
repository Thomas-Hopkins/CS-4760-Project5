#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include "shared.h"
#include "config.h"

extern struct oss_shm* shared_mem;
static struct message msg; 
static char* exe_name;
static int num_res;

void help() {
    printf("Operating System Simulator Child usage\n");
    printf("Runs as a child of the OSS. Not to be run alone.\n");
}

void init_child() {
    // Init rand gen, shared mem, and msg queues
    srand((int)time(NULL) + getpid());
    init_oss(false);

    // create a random number of needed resource requests
    num_res = (rand() % 10) + 1;
}

int main(int argc, char** argv) {
    int option;
    bool has_resource;
    exe_name = argv[0];

    while ((option = getopt(argc, argv, "h")) != -1) {
        switch (option)
        {
        case 'h':
            help();
            exit(EXIT_SUCCESS);
        case 'r':
            num_res = atoi(optarg);
            break;
        case '?':
            // Getopt handles error messages
            exit(EXIT_FAILURE);
        }
    }
    init_child();

    while (true) {
        strncpy(msg.msg_text, "", MSG_BUFFER_LEN);
        msg.msg_type = getpid();
        recieve_msg(&msg, PROC_MSG, true);

        char* cmd = strtok(msg.msg_text, " ");

        if (strncmp(cmd, "resource", MSG_BUFFER_LEN) == 0) {
            has_resource = true;
            num_res--;
        }
        else if (strncmp(cmd, "run", MSG_BUFFER_LEN) != 0) {
            perror("Did not recieve run message");
            continue;
        }

        // release active resource if we have one
        if (has_resource) {
            strncpy(msg.msg_text, "release", MSG_BUFFER_LEN);
            msg.msg_type = getpid();
            send_msg(&msg, OSS_MSG, false);
            has_resource = false;
        }

        // request resource if we need one
        if (num_res > 0) {
            // Request a random resource
            char msg_buf[MSG_BUFFER_LEN];
            snprintf(msg_buf, MSG_BUFFER_LEN, "request %d", (rand() % NUM_RESOURCE_DESC + 1));
            strcpy(msg.msg_text, msg_buf);
            msg.msg_type = getpid();
            send_msg(&msg, OSS_MSG, false);
        }
        // terminate if we don't need any more resources
        else {
            strncpy(msg.msg_text, "terminate", MSG_BUFFER_LEN);
            msg.msg_type = getpid();
            send_msg(&msg, OSS_MSG, false);
            break;
        }
    }

    exit(EXIT_SUCCESS);
}
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include "shared.h"
#include "config.h"

extern struct oss_shm* shared_mem;
static struct message msg;
static struct time_clock endtime;
static char* exe_name;
static int sim_pid;

void help() {
    printf("Operating System Simulator Child usage\n");
    printf("Runs as a child of the OSS. Not to be run alone.\n");
}

void init_child() {
    // Init rand gen, shared mem, and msg queues
    srand((int)time(NULL) + getpid());
    init_oss(false);

    // Calculate a random endtime about 1-5 seconds after current sys time
    endtime.nanoseconds = (rand() % 100000000) + 1000;
    endtime.seconds = (rand() % 5) + 1;
    add_time(&endtime, shared_mem->sys_clock.seconds, shared_mem->sys_clock.nanoseconds);
}

int main(int argc, char** argv) {
    int option;
    exe_name = argv[0];

    while ((option = getopt(argc, argv, "hp:")) != -1) {
        switch (option)
        {
        case 'h':
            help();
            exit(EXIT_SUCCESS);
        case 'p':
            sim_pid = atoi(optarg);
            break;
        case '?':
            // Getopt handles error messages
            exit(EXIT_FAILURE);
        }
    }
    init_child();

    bool has_resources = false;
    bool can_terminate = false;

    while (true) {
        strncpy(msg.msg_text, "", MSG_BUFFER_LEN);
        msg.msg_type = getpid();
        recieve_msg(&msg, PROC_MSG, true);

        char* cmd = strtok(msg.msg_text, " ");

        // See if enough time has passed to terminate process
        if (shared_mem->sys_clock.seconds > endtime.seconds && shared_mem->sys_clock.nanoseconds > endtime.nanoseconds) {
            can_terminate = true;
        }

        // If this process can terminate terminiate it
        if (can_terminate) {
            strncpy(msg.msg_text, "terminate", MSG_BUFFER_LEN);
            msg.msg_type = getpid();
            send_msg(&msg, OSS_MSG, false);
            exit(sim_pid);
        }
        // 50% chance to release resource if it has one
        else if ((rand() % 10 > 5) && has_resources) {
            strncpy(msg.msg_text, "release", MSG_BUFFER_LEN);
            msg.msg_type = getpid();
            send_msg(&msg, OSS_MSG, false);
            has_resources = false;
        }
        // Try to acquire a resource
        else {
            snprintf(msg.msg_text, MSG_BUFFER_LEN, "request");
            // Get a random resource
            for (int i = 0; i < MAX_RES_INSTANCES; i++) {
                char resource_requested[MSG_BUFFER_LEN];
                int max = shared_mem->process_table[sim_pid].max_res[i] - shared_mem->process_table[sim_pid].allow_res[i] + 1;
                snprintf(resource_requested, MSG_BUFFER_LEN, " %d", rand() % max);
                strncat(msg.msg_text, resource_requested, MSG_BUFFER_LEN - strlen(msg.msg_text));
            }
            // Send request for resource
            msg.msg_type = getpid();
            send_msg(&msg, OSS_MSG, false);

            // Wait for response back to see if we have acquired resource or not
            recieve_msg(&msg, PROC_MSG, true);
            if (strncmp(msg.msg_text, "acquired", MSG_BUFFER_LEN) == 0) {
                has_resources = true;
            }
        }
    }

    exit(EXIT_SUCCESS);
}
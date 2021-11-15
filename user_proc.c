#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include "shared.h"
#include "config.h"

extern struct oss_shm* shared_mem;
static char* exe_name;
static int mode;

void help() {
    printf("Operating System Simulator Child usage\n");
    printf("Runs as a child of the OSS. Not to be run alone.\n");
}

void init_child() {
    // Init rand gen, shared mem, and msg queues
    srand((int)time(NULL) + getpid());
    init_oss(false);
}

int main(int argc, char** argv) {
    int option;
    int sim_pid;
    exe_name = argv[0];

    while ((option = getopt(argc, argv, "h")) != -1) {
        switch (option)
        {
        case 'h':
            help();
            exit(sim_pid);
        case '?':
            // Getopt handles error messages
            exit(sim_pid);
        }
    }
    init_child();

    exit(sim_pid);
}
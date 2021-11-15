#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <wait.h>
#include <string.h>

#include "shared.h"
#include "config.h"

pid_t children[MAX_PROCESSES];
size_t num_children;
extern struct oss_shm* shared_mem;
static char* exe_name;
static int log_line = 0;
static int max_secs = 10;
static struct time_clock last_run;

void help();
void signal_handler(int signum);
void initialize();
int launch_child();
void try_spawn_child();
void remove_child(pid_t pid);
void save_to_log(char* text);

int main(int argc, char** argv) {
    int option;
    exe_name = argv[0];

    // Process arguments
    while ((option = getopt(argc, argv, "h")) != -1) {
        switch (option) {
            case 'h':
                help();
                exit(EXIT_SUCCESS);
            case '?':
                // Getopt handles error messages
                exit(EXIT_FAILURE);
        }
    }

    // Clear logfile
    FILE* file_ptr = fopen(LOG_FILE, "w");
    fclose(file_ptr);


    // Initialize
    initialize();

    // Keep track of the last time on the sys clock when we run a process
    last_run.nanoseconds = 0;
    last_run.seconds = 0;

    // Main OSS loop. We handle scheduling processes here.
    while (true) {
        // Simulate some passed time for this loop (1 second and [0, 1000] nanoseconds)
        add_time(&(shared_mem->sys_clock), 1, rand() % 1000);
        // try to spawn a new child if enough time has passed
        try_spawn_child();

        // See if any child processes have terminated
        pid_t pid = waitpid(-1, NULL, WNOHANG);
		if (pid > 0) {
            // Clear up this process for future use
            remove_child(pid);
            // num_children--;
		} 
    }
    dest_oss();
    exit(EXIT_SUCCESS);
}

void help() {
    printf("Operating System Simulator usage\n");
	printf("\n");
	printf("[-h]\tShow this help dialogue.\n");
	printf("\n");
}

void signal_handler(int signum) {
    // Issue messages
	if (signum == SIGINT) {
		fprintf(stderr, "\nRecieved SIGINT signal interrupt, terminating children.\n");
	}
	else if (signum == SIGALRM) {
		fprintf(stderr, "\nProcess execution timeout. Failed to finish in %d seconds.\n", max_secs);
	}

    // Kill active children
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (children[i] > 0) {
            kill(children[i], SIGKILL);
        }
    }

    // Cleanup oss shared memory
    dest_oss();

    if (signum == SIGINT) exit(EXIT_SUCCESS);
	if (signum == SIGALRM) exit(EXIT_FAILURE);
}

void initialize() {
    // Initialize random number gen
    srand((int)time(NULL) + getpid());

    // Attach to and initialize shared memory.
    init_oss(true);

    // Initialize children array
    for (int i = 0; i < MAX_PROCESSES; i++) {
        children[i] = 0;
    }

    // Setup signal handlers
	signal(SIGINT, signal_handler);
	signal(SIGALRM, signal_handler);

	// Terminate in max_secs	
	alarm(max_secs);
}

int launch_child() {
    char* program = "./user_proc";
    return execl(program, program, NULL);
}

void remove_child(pid_t pid) {
	// Remove pid from children list (slow linear search - but small list so inconsequential)
    for (int i = 0; i < num_children; i++) {
		if (children[i] == pid) {
			// If match, set pid to 0
			children[i] = 0;
		}
	}
}

void try_spawn_child() {
    // Check if enough time has passed on simulated sys clock to spawn new child
    if ((shared_mem->sys_clock.seconds - last_run.seconds > maxTimeBetweenNewProcsSecs) && 
    (shared_mem->sys_clock.nanoseconds - last_run.nanoseconds > maxTimeBetweenNewProcsNS)) {
        // Check process control block availablity
        if (num_children < MAX_PROCESSES) {
            // Find open slot to put pid
            int i;
            for (i = 0; i < MAX_PROCESSES; i++) {
                if (children[i] == 0) break;
            }

            // Fork and launch child process
            pid_t pid = fork();
            if (pid == 0) {
                if (launch_child() < 0) {
                    printf("Failed to launch process.\n");
                }
            } 
            else {
                children[i] = pid;
                num_children++;
            }
            // Add some time for generating a process
            add_time(&shared_mem->sys_clock, 2, rand() % 1000);
            add_time(&last_run, shared_mem->sys_clock.seconds, shared_mem->sys_clock.nanoseconds);
        }
    }
}

void save_to_log(char* text) {
	FILE* file_log = fopen(LOG_FILE, "a+");
    log_line++;
    if (log_line > LOG_FILE_MAX) {
        errno = EINVAL;
        perror("Log file has exceeded max length.");
    }

    // Make sure file is opened
	if (file_log == NULL) {
		perror("Could not open logfile");
        return;
	}

    fprintf(file_log, "%s: %ld.%ld: %s\n", exe_name, shared_mem->sys_clock.seconds, shared_mem->sys_clock.nanoseconds, text);

    fclose(file_log);
}
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
#include "queue.h"

static pid_t children[MAX_PROCESSES];
static size_t num_children = 0;
extern struct oss_shm* shared_mem;
static struct Queue proc_queue;
static struct message msg;
static char* exe_name;
static int log_line = 0;
static int max_secs = 100;
static struct time_clock last_run;

void help();
void signal_handler(int signum);
void initialize();
int launch_child();
void try_spawn_child();
void handle_processes();
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

        // Handle process requests 
        handle_processes();

        // See if any child processes have terminated
        pid_t pid = waitpid(-1, NULL, WNOHANG);
		if (pid > 0) {
            // Clear up this process for future use
            remove_child(pid);
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
            children[i] = 0;
            num_children--;
        }
    }

    // Cleanup oss shared memory
    dest_oss();

    if (signum == SIGINT) exit(EXIT_SUCCESS);
	if (signum == SIGALRM) exit(EXIT_SUCCESS);
}

void initialize() {
    // Initialize random number gen
    srand((int)time(NULL) + getpid());

    // Attach to and initialize shared memory.
    init_oss(true);

    // initialize process queue
    queue_init(&proc_queue);

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
            num_children--;
            break;
		}
	}
}

void try_spawn_child() {
    // Check if enough time has passed on simulated sys clock to spawn new child
    // Time needed is calculated randomly to give some random offset between processes
    int seconds = (rand() % (maxTimeBetweenNewProcsSecs + 1)) + minTimeBetweenNewProcsSecs;
    int nansecs = (rand() % (maxTimeBetweenNewProcsNS + 1)) + minTimeBetweenNewProcsNS;
    if ((shared_mem->sys_clock.seconds - last_run.seconds > seconds) && 
    (shared_mem->sys_clock.nanoseconds - last_run.nanoseconds > nansecs)) {
        // Check process control block availablity
        if (num_children < MAX_PROCESSES) {
            // Find open slot to put pid
            int sim_pid;
            for (sim_pid = 0; sim_pid < MAX_PROCESSES; sim_pid++) {
                if (children[sim_pid] == 0) break;
            }

            // Add to process table
            shared_mem->process_table[sim_pid].sim_pid = sim_pid;
            // initalize maxium and allocated resources for this process
            for (int i = 0; i < MAX_RES_INSTANCES; i++) {
                // Random maxium resources this process will use from any given resource descriptor
                shared_mem->process_table[sim_pid].max_res[i] = rand() % (shared_mem->descriptors[i].resource + 1);
                shared_mem->process_table[sim_pid].allow_res[i] = 0;
            }

            // Fork and launch child process
            pid_t pid = fork();
            if (pid == 0) {
                if (launch_child() < 0) {
                    printf("Failed to launch process.\n");
                }
            } 
            else {
                // keep track of child's real pid
                children[sim_pid] = pid;
                num_children++;
                // add to queue
                queue_insert(&proc_queue, sim_pid);
                shared_mem->process_table[sim_pid].actual_pid = pid;

            }
            // Add some time for generating a process (0.1ms)
            add_time(&shared_mem->sys_clock, 0, rand() % 100000);
            // Update last run
            add_time(&last_run, shared_mem->sys_clock.seconds, shared_mem->sys_clock.nanoseconds);
        }
    }
}

// Handle children processes requests over message queues
void handle_processes() {
    // Return if no process in queue
    int sim_pid = queue_pop(&proc_queue);
    if (sim_pid < 0) return;
    // Get message from queued process
    strncpy(msg.msg_text, "run", MSG_BUFFER_LEN);
    msg.msg_type = shared_mem->process_table[sim_pid].actual_pid;
    send_msg(&msg, PROC_MSG, false);

    strncpy(msg.msg_text, "", MSG_BUFFER_LEN);
    msg.msg_type = shared_mem->process_table[sim_pid].actual_pid;
    recieve_msg(&msg, OSS_MSG, true);

    char* cmd = strtok(msg.msg_text, " ");

    fprintf(stderr, "Ran %d and it returned %s\n", sim_pid, cmd);
    // If request command
    if (strncmp(cmd, "request", MSG_BUFFER_LEN) == 0) {
        int resources[MAX_RES_INSTANCES];
        // Get all resources requested
        for (int i = 0; i < MAX_RES_INSTANCES; i++) {
            cmd = strtok(NULL, " ");
            resources[i] = atoi(cmd);
        }
        fprintf(stderr, "\n");
        // TODO: Check for deadlocks

        // Update allocated 
        for (int i = 0; i < MAX_RES_INSTANCES; i++) {
            shared_mem->process_table->allow_res[i] = resources[i];
        }

        // Send acquired message
        strncpy(msg.msg_text, "acquired", MSG_BUFFER_LEN);
        msg.msg_type = shared_mem->process_table[sim_pid].actual_pid;
        send_msg(&msg, PROC_MSG, false);
    }
    else if (strncmp(cmd, "release", MSG_BUFFER_LEN) == 0) {
        // Release any allocated resources this process has and reset its max resources
        int num_res = 0;
        for (int i = 0; i < MAX_RES_INSTANCES; i++) {
            if (shared_mem->process_table[sim_pid].allow_res[i] > 0) {
                printf("Releasing resource %d with %d instances\n", i, shared_mem->process_table[sim_pid].allow_res[i]);
                shared_mem->process_table[sim_pid].allow_res[i] = 0;
                num_res++;
            }
        }

        // If we had no resources notify
        if (num_res <= 0) {
            printf("No resources to be released\n");
        }
    }
    else if (strncmp(cmd, "terminate", MSG_BUFFER_LEN) == 0) {
        // Release any allocated resources this process has and reset its max resources
        int num_res = 0;
        for (int i = 0; i < MAX_RES_INSTANCES; i++) {
            if (shared_mem->process_table[sim_pid].allow_res[i] > 0) {
                printf("Releasing resource %d with %d instances\n", i, shared_mem->process_table[sim_pid].allow_res[i]);
                shared_mem->process_table[sim_pid].max_res[i] = 0;
                shared_mem->process_table[sim_pid].allow_res[i] = 0;
                num_res++;
            }
        }

        // If we had no resources notify
        if (num_res <= 0) {
            printf("No resources to be released\n");
        }

        // Add some time for handling a process (0.1ms)
        add_time(&shared_mem->sys_clock, 0, rand() % 100000);

        // Do not requeue this process.
        return;
    }

    // Re-queue this process
    queue_insert(&proc_queue, sim_pid);

    // Add some time for handling a process (0.1ms)
    add_time(&shared_mem->sys_clock, 0, rand() % 100000);
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
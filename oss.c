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
static struct Queue copy_queue;
static struct message msg;
static char* exe_name;
static int log_line = 0;
static int total_procs = 0;
static struct time_clock last_run;

struct statistics {
    unsigned int granted_requests;
    unsigned int denied_requests;
    unsigned int terminations;
    unsigned int releases;
};

static struct statistics stats;

void help();
void signal_handler(int signum);
void initialize();
int launch_child();
void try_spawn_child();
bool is_safe(int sim_pid, int resources[MAX_RES_INSTANCES]);
void handle_processes();
void remove_child(pid_t pid);
void matrix_to_string(char* buffer, size_t buffer_size, int* matrix, int rows, int cols);
void output_stats();
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

        // If we've run all the processes we need and have no more children we can exit
        if (total_procs > MAX_RUN_PROCS && queue_is_empty(&proc_queue)) {
            break;
        } 
    }
    output_stats();
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
		fprintf(stderr, "\nProcess execution timeout. Failed to finish in %d seconds.\n", MAX_RUNTIME);
	}

    // Kill active children
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (children[i] > 0) {
            kill(children[i], SIGKILL);
            children[i] = 0;
            num_children--;
        }
    }

    output_stats();

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

    // init stats
    stats.granted_requests = 0;
    stats.denied_requests = 0;
    stats.releases = 0;
    stats.terminations = 0;

    // Setup signal handlers
	signal(SIGINT, signal_handler);
	signal(SIGALRM, signal_handler);

	// Terminate in MAX_RUNTIME	
	alarm(MAX_RUNTIME);
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
    if (total_procs > MAX_RUN_PROCS) return;
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
                    exit(EXIT_FAILURE);
                }
            } 
            else {
                // keep track of child's real pid
                children[sim_pid] = pid;
                num_children++;
                // add to queue
                queue_insert(&proc_queue, sim_pid);
                shared_mem->process_table[sim_pid].actual_pid = pid;
                total_procs++;
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
    char log_buf[100];
    // Return if no process in queue
    int sim_pid = queue_peek(&proc_queue);
    if (sim_pid < 0) return;
    // Get message from queued process
    strncpy(msg.msg_text, "run", MSG_BUFFER_LEN);
    msg.msg_type = shared_mem->process_table[sim_pid].actual_pid;
    send_msg(&msg, PROC_MSG, false);

    snprintf(log_buf, 100, "OSS sent run message to P%d at %ld:%ld", sim_pid, shared_mem->sys_clock.seconds, shared_mem->sys_clock.nanoseconds);
    save_to_log(log_buf);
    add_time(&shared_mem->sys_clock, 0, rand() % 10000);


    strncpy(msg.msg_text, "", MSG_BUFFER_LEN);
    msg.msg_type = shared_mem->process_table[sim_pid].actual_pid;
    recieve_msg(&msg, OSS_MSG, true);

    add_time(&shared_mem->sys_clock, 0, rand() % 10000);
    char* cmd = strtok(msg.msg_text, " ");

    // If request command
    if (strncmp(cmd, "request", MSG_BUFFER_LEN) == 0) {
        snprintf(log_buf, 100, "OSS recieved request from P%d for some resources at %ld:%ld", sim_pid, shared_mem->sys_clock.seconds, shared_mem->sys_clock.nanoseconds);
        save_to_log(log_buf);
        int resources[MAX_RES_INSTANCES];
        // Get all resources requested
        for (int i = 0; i < MAX_RES_INSTANCES; i++) {
            cmd = strtok(NULL, " ");
            resources[i] = atoi(cmd);
        }

        // Update allocated 
        for (int i = 0; i < MAX_RES_INSTANCES; i++) {
            shared_mem->process_table->allow_res[i] = resources[i];
        }
        add_time(&shared_mem->sys_clock, 0, rand() % 10000);

        // If we are deadlock safe then we can move on
        if (is_safe(sim_pid, resources)) {
            snprintf(log_buf, 100, "\tSafe state, granting request");
            save_to_log(log_buf);
            // Send acquired message
            strncpy(msg.msg_text, "acquired", MSG_BUFFER_LEN);
            msg.msg_type = shared_mem->process_table[sim_pid].actual_pid;
            send_msg(&msg, PROC_MSG, false);
            stats.granted_requests++;
        }
        else {
            snprintf(log_buf, 100, "\tUnsafe state, denying request");
            save_to_log(log_buf);
            strncpy(msg.msg_text, "denied", MSG_BUFFER_LEN);
            msg.msg_type = shared_mem->process_table[sim_pid].actual_pid;
            send_msg(&msg, PROC_MSG, false);
            stats.denied_requests++;
        }
    }
    else if (strncmp(cmd, "release", MSG_BUFFER_LEN) == 0) {
        snprintf(log_buf, 100, "OSS releasing resources for P%d at %ld:%ld", sim_pid, shared_mem->sys_clock.seconds, shared_mem->sys_clock.nanoseconds);
        save_to_log(log_buf);
        // Release any allocated resources this process has and reset its max resources
        int num_res = 0;
        for (int i = 0; i < MAX_RES_INSTANCES; i++) {
            if (shared_mem->process_table[sim_pid].allow_res[i] > 0) {
                snprintf(log_buf, 100, "\tReleasing resource %d with %d instances", i, shared_mem->process_table[sim_pid].allow_res[i]);
                save_to_log(log_buf);
                shared_mem->process_table[sim_pid].allow_res[i] = 0;
                num_res++;
                add_time(&shared_mem->sys_clock, 0, rand() % 100);
            }
        }
        stats.releases++;

        // If we had no resources notify
        if (num_res <= 0) {
            save_to_log("\tNo resources to release");
        }
    }
    else if (strncmp(cmd, "terminate", MSG_BUFFER_LEN) == 0) {
        // Release any allocated resources this process has and reset its max resources
        int num_res = 0;
        for (int i = 0; i < MAX_RES_INSTANCES; i++) {
            if (shared_mem->process_table[sim_pid].allow_res[i] > 0) {
                snprintf(log_buf, 100, "\tReleasing resource %d with %d instances", i, shared_mem->process_table[sim_pid].allow_res[i]);
                save_to_log(log_buf);
                shared_mem->process_table[sim_pid].max_res[i] = 0;
                shared_mem->process_table[sim_pid].allow_res[i] = 0;
                num_res++;
                add_time(&shared_mem->sys_clock, 0, rand() % 100);
            }
        }
        stats.terminations++;

        // If we had no resources notify
        if (num_res <= 0) {
            save_to_log("\tNo resources to release");
        }

        // Add some time for handling a process (0.1ms)
        add_time(&shared_mem->sys_clock, 0, rand() % 100000);

        // Do not requeue this process.
        sim_pid = queue_pop(&proc_queue);
        remove_child(shared_mem->process_table[sim_pid].actual_pid);
        return;
    }

    // Re-queue this process
    queue_pop(&proc_queue);
    queue_insert(&proc_queue, sim_pid);

    // Add some time for handling a process (0.1ms)
    add_time(&shared_mem->sys_clock, 0, rand() % 100000);
}

bool is_safe(int sim_pid, int requests[MAX_RES_INSTANCES]) {
    char log_buf[100];
    snprintf(log_buf, 100, "OSS running deadlock detection at %ld:%ld", shared_mem->sys_clock.seconds, shared_mem->sys_clock.nanoseconds);
    add_time(&shared_mem->sys_clock, 0, rand() % 1000000);
    save_to_log(log_buf);

    memcpy(&copy_queue, &proc_queue, sizeof(struct Queue));
    int size = copy_queue.size;
    int curr_elm = queue_pop(&copy_queue);
    int maximum[size][MAX_RES_INSTANCES];
    int allocated[size][MAX_RES_INSTANCES];
    int need[size][MAX_RES_INSTANCES];
    int available[MAX_RES_INSTANCES];
    int num_avail = 0;

    // Copy over available non-shared resource data
    for (int i = 0; i < MAX_RES_INSTANCES; i++) {
        // if (shared_mem->descriptors[i].is_shared) continue;
        available[i] = shared_mem->descriptors[i].resource;
        num_avail++;
    }

    // get all processes resource data into maximum and allocated matrixes
    for (int i = 0; i < size; i++) {

        for (int j = 0; j < MAX_RES_INSTANCES; j++) {
            maximum[i][j] = shared_mem->process_table[curr_elm].max_res[j];
            allocated[i][j] = shared_mem->process_table[curr_elm].allow_res[j];
        }
        curr_elm = queue_pop(&copy_queue);
    }

    // Calculate needed matrix
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < MAX_RES_INSTANCES; j++) {
            need[i][j] = maximum[i][j] - allocated[i][j]; 
        }
    }

    // Output if in verbose mode and every 20 successful requests
    if (VERBOSE_MODE && ((stats.granted_requests % 20) == 0)) {
        int buf_size = size * MAX_RES_INSTANCES * 8;
        char buf[buf_size];
        save_to_log("Need Matrix:");
        matrix_to_string(buf, buf_size, &need[0][0], size, MAX_RES_INSTANCES);
        save_to_log(buf);

        save_to_log("Maximum Matrix:");
        matrix_to_string(buf, buf_size, &maximum[0][0], size, MAX_RES_INSTANCES);
        save_to_log(buf);

        save_to_log("Allocated Matrix:");
        matrix_to_string(buf, buf_size, &allocated[0][0], size, MAX_RES_INSTANCES);
        save_to_log(buf);

        save_to_log("Available Array:");
        matrix_to_string(buf, buf_size, available, 1, MAX_RES_INSTANCES);
        save_to_log(buf);

        save_to_log("Request Array:");
        matrix_to_string(buf, buf_size, requests, 1, MAX_RES_INSTANCES);
        save_to_log(buf);
    }

    int index = 0;
    memcpy(&copy_queue, &proc_queue, sizeof(struct Queue));
    curr_elm = queue_pop(&copy_queue);
    while (!queue_is_empty(&copy_queue)) {
        if (curr_elm == sim_pid) break;
        index++;
        curr_elm = queue_pop(&copy_queue);
    }

    // resource request algo
    for (int i = 0; i < MAX_RES_INSTANCES; i++) {
        if (need[index][i] < requests[i]) {

            // Unsafe
            return false;
        }

        if (requests[i] <= available[i]) {
            available[i] -= requests[i];
            allocated[index][i] += requests[i];
            need[index][i] -= requests[i];
        }
        else {
            // Unsafe
            return false;
        }
    }

    return true;
}

void matrix_to_string(char* dest, size_t buffer_size, int* matrix, int rows, int cols) {
    strncpy(dest, "", buffer_size);
    char buffer[buffer_size];
    strncat(dest, "    ", buffer_size);
    for (int i = 1; i <= MAX_RES_INSTANCES; i++) {
        snprintf(buffer, buffer_size, "R%-2d ", i);
        strncat(dest, buffer, buffer_size);
    }
    strncat(dest, "\n", buffer_size);

    for (int i = 0; i < rows; i++) {
        snprintf(buffer, buffer_size, "P%-3d", i);
        strncat(dest, buffer, buffer_size);
        for (int j = 0; j < cols; j++) {
            snprintf(buffer, buffer_size, "%-3d ", matrix[i * cols + j]);
            strncat(dest, buffer, buffer_size);
        }
        if (i != rows - 1) strncat(dest, "\n", buffer_size);
    }
}

void output_stats() {
    printf("\n");
    printf("| STATISTICS |\n");
    printf("--REQUESTS\n");
    printf("\t%-12s %d\n", "DENIED:", stats.denied_requests);
    printf("\t%-12s %d\n", "GRANTED:", stats.granted_requests);
    printf("\t%-12s %d\n", "TOTAL:", stats.granted_requests + stats.denied_requests);
    printf("--TERMINATIONS\n");
    printf("\t%-12s %d\n", "TOTAL:", stats.terminations);
    printf("--RELEASES\n");
    printf("\t%-12s %d\n", "TOTAL:", stats.releases);
    printf("--SIMULATED TIME\n");
    printf("\t%-12s %ld\n", "SECONDS:", shared_mem->sys_clock.seconds);
    printf("\t%-12s %ld\n", "NANOSECONDS:", shared_mem->sys_clock.nanoseconds);
    printf("\n");
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

    fprintf(file_log, "%s\n", text);

    fclose(file_log);
}
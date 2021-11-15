#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <unistd.h>

#include "shared.h"

struct oss_shm* shared_mem = NULL;
static int semaphore_id = -1;
static int oss_msg_queue;

// Private function to get shared memory key
int get_shm(int token) {
	key_t key;

	// Get numeric key of shared memory file
	key = ftok(SHM_FILE, token);
	if (key == -1) return -1;

	// Get shared memory id from the key
	return shmget(key, sizeof(struct oss_shm), 0644 | IPC_CREAT);
}

// private function to get the specified number of semaphores from id
int getsemaphores(int token, int sem_num) {
	key_t key;
	// Get numeric key of shared memory file
	key = ftok(SHM_FILE, token);
	if (key == -1) return -1;

	// Get access to semaphore set with sem_num semaphores
	if ((semaphore_id = semget(key, sem_num, 0644 | IPC_CREAT)) == -1) return -1;

	return 0;
}

// private function to block use of critical resource until it has been unlocked
void lock(int num) {
	struct sembuf myop[1];
	myop->sem_num = (short)num;
	myop->sem_op = (short)-1;
	myop->sem_flg = (short)0;
	if ((semop(semaphore_id, myop, 1)) == -1) perror("Could not lock!");
	fprintf(stderr, "%d: Got lock on critical resource %d\n", getpid(), num);
}

// Private function to unlock critical resource
void unlock(int num) {
	struct sembuf myop[1];
	myop->sem_num = (short)num;
	myop->sem_op = (short)1;
	myop->sem_flg = (short)0;
	if ((semop(semaphore_id, myop, 1)) == -1) perror("Could not unlock!");
	fprintf(stderr, "%d: Released lock on critical resource %d\n", getpid(), num);
} 

// Public function to initalize the oss shared resources
// Pass create = true for intializalizing values
void init_oss(bool create) {
	// Get semaphores
	getsemaphores(OSS_SEM, FINAL_SEMIDS_SIZE); 

	// Get shared memory
    int mem_id = get_shm(OSS_SHM);
    if (mem_id < 0) {
        printf("Could not get shared memory file.");
    }
	shared_mem = shmat(mem_id, NULL, 0);
    if (shared_mem < 0) {
        printf("Could not attach to shared memory.");
    }

	// Get message queue
	key_t oss_msg_key = ftok(SHM_FILE, OSS_MSG);

	if (oss_msg_key < 0) {
        printf("Could not get message queue(s) file.");
	}

	if (create) {
		oss_msg_queue = msgget(oss_msg_key, 0644 | IPC_CREAT);
	}
	else {
		oss_msg_queue = msgget(oss_msg_key, 0644 | IPC_EXCL);
	}

	if (oss_msg_queue < 0) {
        printf("Could not attach to message queue(s).");
	}

	// If not creating, return out
	if (!create) return;
	
	// Setup system clock
	shared_mem->sys_clock.semaphore_id = SYSCLK_SEM;
	shared_mem->sys_clock.seconds = 0;
	shared_mem->sys_clock.nanoseconds = 0;

	// Intialize semaphores w/ initial value of 1
	union semun arg;
	arg.val = 1;
	for (int i = 0; i < FINAL_SEMIDS_SIZE; i++) {
		if ((semctl(semaphore_id, i, SETVAL, arg)) == -1) {
			perror("Failed to intialize a semaphore");
		}
	}

}

// Public function to destruct oss shared resources
void dest_oss() {
	// remove semaphores
	if ((semctl(semaphore_id, 0, IPC_RMID)) == -1) {
		perror("Failed to remove semaphores");
	}

	// Remove shared memory
	int mem_id = get_shm(OSS_SHM);
	if (mem_id < 0) {
        perror("Could not get shared memory file");
    }
	if (shmctl(mem_id, IPC_RMID, NULL) < 0) {
        perror("Could not remove shared memory");
	}

	shared_mem = NULL;

	// remove message queues
	if (msgctl(oss_msg_queue, IPC_RMID, NULL) < 0) {
		perror("Could not detach message queue");
	}
}

// Public function to add time to clock
void add_time(struct time_clock* Time, unsigned long seconds, unsigned long nanoseconds) {
	// Lock if managed by a semaphore
	if (Time->semaphore_id > 0) lock(Time->semaphore_id);
	Time->seconds += seconds;
	Time->nanoseconds += nanoseconds;
	if (Time->nanoseconds > 1000000000)
	{
		Time->seconds += 1;
		Time->nanoseconds -= 1000000000;
	}

	// unlock if managed by a semaphore
	if (Time->semaphore_id > 0) unlock(Time->semaphore_id);
}

// Private function to subtract time from clock
bool __sub_time(struct time_clock* Time, unsigned long seconds, unsigned long nanoseconds) {
	// If subtracting more nanoseconds then is on the clock
	if (nanoseconds > Time->nanoseconds) {
		// See if we can borrow a second
		if (Time->seconds < 1) return false;
		// Borrow a second
		Time->nanoseconds += 1000000000;
		Time->seconds -= 1;
	}
	// Subtract the nanoseconds
	Time->nanoseconds -= nanoseconds;

	// If subtracting more seconds than we have on clock
	if (seconds > Time->seconds) {
		// Add back the nanoseconds we took
		add_time(Time, 0, nanoseconds);
		return false;
	}
	// Subtract the seconds
	Time->seconds -= seconds;
	return true;
}

// public interface function to subtract time from clock
void sub_time(struct time_clock* Time, unsigned long seconds, unsigned long nanoseconds) {
	// Lock if managed by a semaphore
	if (Time->semaphore_id > 0) lock(Time->semaphore_id);
	if (!__sub_time(Time, seconds, nanoseconds)) {
		perror("Could not subtract time.");
	}
	// unlock if managed by a semaphore
	if (Time->semaphore_id > 0) unlock(Time->semaphore_id);
}

int recieve_msg(struct message* msg, int msg_queue, bool wait) {
	int msg_queue_id;
	if (msg_queue == OSS_MSG) {
		msg_queue_id = oss_msg_queue;
	}
	else {
		printf("Got unexpected message queue ID of %d\n", msg_queue);
	}
	return msgrcv(msg_queue_id, msg, sizeof(struct message), msg->msg_type, wait ? 0 : IPC_NOWAIT);
}

int send_msg(struct message* msg, int msg_queue, bool wait) {
	int msg_queue_id;
	if (msg_queue == OSS_MSG) {
		msg_queue_id = oss_msg_queue;
	}
	else {
		printf("Got unexpected message queue ID of %d\n", msg_queue);
	}
	return msgsnd(msg_queue_id, msg, sizeof(struct message), wait ? 0 : IPC_NOWAIT);
}
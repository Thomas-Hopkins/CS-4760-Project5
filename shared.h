#ifndef __SHARED_H
#define __SHARED_H

#include <stdbool.h>

#define MAX_PROCESSES 18
#define SHM_FILE "shmOSS.shm"
#define MSG_BUFFER_LEN 2048
#define NUM_RESOURCE_DESC 20

enum Shared_Mem_Tokens {OSS_SHM, OSS_SEM, OSS_MSG};
enum Semaphore_Ids {SYSCLK_SEM, FINAL_SEMIDS_SIZE};

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

struct time_clock {
    unsigned long nanoseconds;
    unsigned long seconds; 
    int semaphore_id;
};

struct message {
    long int msg_type;
    char msg_text[MSG_BUFFER_LEN];
};

struct res_descr {

};

struct oss_shm {
    struct time_clock sys_clock;
    struct res_descr resources[NUM_RESOURCE_DESC];
};

void dest_oss();
void init_oss(bool create);
void add_time(struct time_clock* Time, unsigned long seconds, unsigned long nanoseconds);
void sub_time(struct time_clock* Time, unsigned long seconds, unsigned long nanoseconds);
int recieve_msg(struct message* msg, int msg_queue, bool wait);
int send_msg(struct message* msg, int msg_queue, bool wait);


#endif

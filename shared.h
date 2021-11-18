#ifndef __SHARED_H
#define __SHARED_H

#include <stdbool.h>
#include "config.h"

enum Shared_Mem_Tokens {OSS_SHM, OSS_SEM, OSS_MSG, PROC_MSG};
enum Semaphore_Ids {BEGIN_SEMIDS, SYSCLK_SEM, FINAL_SEMIDS_SIZE};

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
    int resource;
    bool is_shared;
};

struct process_ctrl_block {
    unsigned int sim_pid;
    pid_t actual_pid;
    int max_res[MAX_RES_INSTANCES];
    int allow_res[MAX_RES_INSTANCES];
};

struct oss_shm {
    struct time_clock sys_clock;
    struct process_ctrl_block process_table[MAX_PROCESSES];
    struct res_descr descriptors[MAX_RES_INSTANCES];
};

void dest_oss();
void init_oss(bool create);
void add_time(struct time_clock* Time, unsigned long seconds, unsigned long nanoseconds);
void sub_time(struct time_clock* Time, unsigned long seconds, unsigned long nanoseconds);
void recieve_msg(struct message* msg, int msg_queue, bool wait);
void send_msg(struct message* msg, int msg_queue, bool wait);


#endif

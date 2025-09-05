#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <sys/types.h>
#include <stddef.h>
#include <sys/sem.h>
#include <unistd.h> // Per pid_t

/* --- IPC Paths & Keys --- */
#define MQ_PATH "/tmp/sha256_mq"
#define MQ_PROJ 'S'
#define SHM_PATH "/tmp/sha256_shm"
#define SHM_PROJ 'H'
#define SEM_PATH "/tmp/sha256_sem"
#define SEM_PROJ 'M'

/* --- Shared Memory --- */
#define SHM_SIZE (4 * 1024 * 1024) /* 4 MB */

/* --- Message Queue Message Types --- */
#define REQ_TYPE 1L   // Tipo di messaggio per le richieste di hash
#define MGMT_TYPE 2L  // Tipo di messaggio per le richieste di gestione
#define WORKER_JOB_TYPE 3L // Tipo di messaggio per il worker

/* --- Message Structures --- */
#define MAX_FILENAME 256
#define INFO_LEN 128

struct req_msg {
    long mtype;
    pid_t pid;
    size_t size;
    char filename[MAX_FILENAME];
};

struct resp_msg {
    long mtype;
    int status;
    char hash[65];
    char info[INFO_LEN];
};

/* --- System V Semaphore --- */
#define SEM_INDEX 0

/* Funzioni wrapper per semafori SysV P/V (wait/post) */
static inline int sem_wait1(int semid) {
    struct sembuf op = { .sem_num = SEM_INDEX, .sem_op = -1, .sem_flg = 0 };
    return semop(semid, &op, 1);
}

static inline int sem_post1(int semid) {
    struct sembuf op = { .sem_num = SEM_INDEX, .sem_op = +1, .sem_flg = 0 };
    return semop(semid, &op, 1);
}

#endif
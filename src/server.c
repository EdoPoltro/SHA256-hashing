#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>

#include <openssl/sha.h>
#include "protocol.h"

static int mq_id = -1;
static int shm_id = -1;
static int sem_id = -1;
static unsigned char *shm_ptr = NULL;

#define WORKER_POOL_SIZE 3
static int current_worker_pool = WORKER_POOL_SIZE;
static pthread_t *worker_threads = NULL;
static pthread_mutex_t worker_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t new_job_cond = PTHREAD_COND_INITIALIZER;

static struct req_msg *job_queue = NULL;
static int queue_size = 0;

static int is_sjf = 0; // 0 per FCFS, 1 per SJF

/* Gestisce la pulizia delle risorse IPC */
static void cleanup(int signum) {
    if (shm_ptr) { shmdt(shm_ptr); shm_ptr = NULL; }
    if (shm_id != -1) { shmctl(shm_id, IPC_RMID, NULL); shm_id = -1; }
    if (mq_id != -1) { msgctl(mq_id, IPC_RMID, NULL); mq_id = -1; }
    if (sem_id != -1) { semctl(sem_id, 0, IPC_RMID); sem_id = -1; }
    if (worker_threads) { free(worker_threads); worker_threads = NULL; }
    if (job_queue) { free(job_queue); job_queue = NULL; }
    if (signum) {
        (void)write(STDOUT_FILENO, "\n[server] Cleanup fatto. Bye!\n", 30);
        _exit(0);
    }
}

/* Converte un digest SHA256 in stringa esadecimale */
static void to_hex(const uint8_t *digest, char out[65]) {
    static const char *hex = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        out[i*2] = hex[(digest[i] >> 4) & 0xF];
        out[i*2+1] = hex[digest[i] & 0xF];
    }
    out[64] = '\0';
}

/* Funzione per confrontare le richieste per la schedulazione SJF */
int compare_requests_sjf(const void *a, const void *b) {
    const struct req_msg *req_a = (const struct req_msg *)a;
    const struct req_msg *req_b = (const struct req_msg *)b;
    return (req_a->size > req_b->size) - (req_a->size < req_b->size);
}

/* Funzione per confrontare le richieste per la schedulazione FCFS */
int compare_requests_fcfs(const void *a, const void *b) {
    // FCFS non richiede un ordinamento specifico.
    // Restituisce 0 per indicare che gli elementi sono considerati uguali
    // ai fini dell'ordinamento. L'ordine di arrivo è implicito.
    return 0;
}

/* Funzione eseguita dai worker per elaborare le richieste */
void *worker_function(void *arg) {
    while (1) {
        pthread_mutex_lock(&queue_mutex);
        while (queue_size == 0) {
            pthread_cond_wait(&new_job_cond, &queue_mutex);
        }

        struct req_msg req = job_queue[0];
        // Sposta tutti gli elementi di un posto in avanti
        for (int i = 0; i < queue_size - 1; i++) {
            job_queue[i] = job_queue[i + 1];
        }
        queue_size--;
        job_queue = realloc(job_queue, queue_size * sizeof(struct req_msg));
        pthread_mutex_unlock(&queue_mutex);

        printf("[worker %ld] Richiesta da pid=%d, file=\"%s\", size=%zu\n",
               (long)pthread_self(), req.pid, req.filename, req.size);

        struct resp_msg resp = {0};
        resp.mtype = req.pid;
        resp.status = 0;
        snprintf(resp.info, sizeof(resp.info), "ok");

        /* Il worker esegue il lock sulla SHM per calcolare l'hash */
        if (sem_wait1(sem_id) == -1) {
            perror("sem_wait (worker)");
            resp.status = -5;
            snprintf(resp.info, sizeof(resp.info), "failed to acquire semaphore");
        } else {
            if (req.size > SHM_SIZE) {
                resp.status = -2;
                snprintf(resp.info, sizeof(resp.info), "file troppo grande per SHM");
            } else {
                SHA256_CTX ctx;
                if (SHA256_Init(&ctx) != 1 ||
                    (req.size > 0 && SHA256_Update(&ctx, shm_ptr, req.size) != 1)) {
                    resp.status = -3;
                    snprintf(resp.info, sizeof(resp.info), "SHA256 update failed");
                } else {
                    uint8_t digest[32];
                    if (SHA256_Final(digest, &ctx) != 1) {
                        resp.status = -4;
                        snprintf(resp.info, sizeof(resp.info), "SHA256 final failed");
                    } else {
                        to_hex(digest, resp.hash);
                    }
                }
            }
            if (sem_post1(sem_id) == -1) {
                perror("sem_post (worker)");
            }
        }
        if (msgsnd(mq_id, &resp, sizeof(resp) - sizeof(long), 0) == -1) {
            perror("msgsnd(resp)");
        }
        printf("[worker %ld] Calcolo completato per %s, hash: %s\n", (long)pthread_self(), req.filename, resp.hash);
    }
    return NULL;
}

/* Funzione per modificare dinamicamente il numero di worker */
void change_worker_pool(int new_size) {
    pthread_mutex_lock(&worker_mutex);
    if (new_size > current_worker_pool) {
        worker_threads = realloc(worker_threads, new_size * sizeof(pthread_t));
        for (int i = current_worker_pool; i < new_size; i++) {
            pthread_create(&worker_threads[i], NULL, worker_function, NULL);
        }
        printf("[server] Aggiunti %d worker. Pool totale: %d: \n", new_size - current_worker_pool, new_size);
    } else if (new_size < current_worker_pool) {
        printf("[server] Riduzione del pool di worker non supportata. Pool rimane a %d\n", current_worker_pool);
    }
    current_worker_pool = new_size;
    pthread_mutex_unlock(&worker_mutex);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <FCFS|SJF>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "SJF") == 0) {
        is_sjf = 1;
    } else if (strcmp(argv[1], "FCFS") == 0) {
        is_sjf = 0;
    } else {
        fprintf(stderr, "Errore: algoritmo di schedulazione non valido. Scegli tra FCFS o SJF.\n");
        return 1;
    }

    /* Creazione e inizializzazione delle risorse IPC */
    int fd = open(MQ_PATH, O_CREAT | O_RDWR, 0666);
    if (fd == -1) { perror("open MQ_PATH"); return 1; }
    close(fd);
    fd = open(SHM_PATH, O_CREAT | O_RDWR, 0666);
    if (fd == -1) { perror("open SHM_PATH"); return 1; }
    close(fd);
    fd = open(SEM_PATH, O_CREAT | O_RDWR, 0666);
    if (fd == -1) { perror("open SEM_PATH"); return 1; }
    close(fd);

    key_t mq_key = ftok(MQ_PATH, MQ_PROJ);
    if (mq_key == -1) { perror("ftok MQ"); return 1; }
    key_t shm_key = ftok(SHM_PATH, SHM_PROJ);
    if (shm_key == -1) { perror("ftok SHM"); return 1; }
    key_t sem_key = ftok(SEM_PATH, SEM_PROJ);
    if (sem_key == -1) { perror("ftok SEM"); return 1; }

    mq_id = msgget(mq_key, IPC_CREAT | 0666);
    if (mq_id == -1) { perror("msgget"); return 1; }
    shm_id = shmget(shm_key, SHM_SIZE, IPC_CREAT | 0666);
    if (shm_id == -1) { perror("shmget"); cleanup(0); return 1; }
    shm_ptr = shmat(shm_id, NULL, 0);
    if (shm_ptr == (void*)-1) { perror("shmat"); cleanup(0); return 1; }
    sem_id = semget(sem_key, 1, IPC_CREAT | 0666);
    if (sem_id == -1) { perror("semget"); cleanup(0); return 1; }
    
    union semun { int val; struct semid_ds *buf; unsigned short *array; struct seminfo *__buf; };
    union semun arg; arg.val = 1;
    if (semctl(sem_id, SEM_INDEX, SETVAL, arg) == -1) {
        perror("semctl SETVAL");
        cleanup(0);
        return 1;
    }
    
    /* Gestione dei segnali */
    struct sigaction sa_clean = {0};
    sa_clean.sa_handler = cleanup;
    sigaction(SIGINT, &sa_clean, NULL);
    sigaction(SIGTERM, &sa_clean, NULL);

    printf("[server] Avviato con algoritmo %s. MQ=%d SHM=%d SEM=%d (size=%d). In attesa...\n",
           is_sjf ? "SJF" : "FCFS", mq_id, shm_id, sem_id, (int)SHM_SIZE);

    /* Inizializza e crea il pool di worker iniziali */
    worker_threads = malloc(WORKER_POOL_SIZE * sizeof(pthread_t));
    if (!worker_threads) { perror("malloc"); cleanup(0); return 1; }
    for (int i = 0; i < WORKER_POOL_SIZE; i++) {
        pthread_create(&worker_threads[i], NULL, worker_function, NULL);
    }

    /* Loop principale per gestire le richieste speciali e quelle normali */
    struct req_msg req;
    while (1) {
        ssize_t r = msgrcv(mq_id, &req, sizeof(req) - sizeof(long), -3, 0); 
        if (r == -1) {
            if (errno == EINTR) continue;
            perror("msgrcv");
            break;
        }

        if (req.mtype == MGMT_TYPE) {
            printf("[server] Ricevuta richiesta di gestione. Nuovo numero di worker: %zu\n", req.size);
            change_worker_pool(req.size);
        } else if (req.mtype == REQ_TYPE) {
            pthread_mutex_lock(&queue_mutex);
            // Aggiungi la richiesta alla coda interna
            job_queue = realloc(job_queue, (queue_size + 1) * sizeof(struct req_msg));
            job_queue[queue_size] = req;
            queue_size++;
            // Ordina la coda in base all'algoritmo selezionato
            if (is_sjf) {
                qsort(job_queue, queue_size, sizeof(struct req_msg), compare_requests_sjf);
            }
            pthread_mutex_unlock(&queue_mutex);
            // Segnala a un worker che c'è una nuova richiesta
            pthread_cond_signal(&new_job_cond);
        }
    }
    
    for (int i = 0; i < current_worker_pool; i++) {
        pthread_join(worker_threads[i], NULL);
    }
    
    cleanup(0);
    return 0;
}
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include "protocol.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <file>\n", argv[0]);
        return 1;
    }
    const char *filename = argv[1];

    /* Anchor files per ftok (in caso il server non abbia creato) */
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

    int mq_id = msgget(mq_key, 0666);
    if (mq_id == -1) { perror("msgget"); fprintf(stderr, "Hai avviato il server?\n"); return 1; }

    int shm_id = shmget(shm_key, SHM_SIZE, 0666);
    if (shm_id == -1) { perror("shmget"); fprintf(stderr, "Hai avviato il server?\n"); return 1; }

    unsigned char *shm_ptr = shmat(shm_id, NULL, 0);
    if (shm_ptr == (void*)-1) { perror("shmat"); return 1; }

    int sem_id = semget(sem_key, 1, 0666);
    if (sem_id == -1) { perror("semget"); shmdt(shm_ptr); return 1; }

    /* Leggi il file */
    int f = open(filename, O_RDONLY);
    if (f == -1) { perror("open file"); shmdt(shm_ptr); return 1; }

    struct stat st;
    if (fstat(f, &st) == -1) { perror("fstat"); close(f); shmdt(shm_ptr); return 1; }

    size_t fsize = (size_t)st.st_size;
    if (fsize > SHM_SIZE) {
        fprintf(stderr, "Errore: file troppo grande (%zu) rispetto a SHM (%d)\n", fsize, (int)SHM_SIZE);
        close(f); shmdt(shm_ptr); return 1;
    }

    /* === LOCK: attende che la SHM sia libera per scrivere === */
    if (sem_wait1(sem_id) == -1) {
        perror("sem_wait (client)");
        close(f); shmdt(shm_ptr); return 1;
    }

    /* Copia nella SHM */
    size_t copied = 0;
    while (copied < fsize) {
        ssize_t r = read(f, shm_ptr + copied, fsize - copied);
        if (r > 0) copied += (size_t)r;
        else if (r == 0) break;
        else { perror("read file"); close(f); shmdt(shm_ptr); return 1; }
    }
    close(f);

    /* Richiesta */
    struct req_msg req;
    req.mtype = REQ_TYPE;
    req.pid = getpid();
    req.size = fsize;
    snprintf(req.filename, sizeof(req.filename), "%s", filename);

    if (msgsnd(mq_id, &req, sizeof(req) - sizeof(long), 0) == -1) {
        perror("msgsnd(req)");
        sem_post1(sem_id); // Rilascia il semaforo anche in caso di errore
        shmdt(shm_ptr);
        return 1;
    }

    /* === UNLOCK: Rilascia il semaforo subito dopo aver inviato la richiesta === */
    if (sem_post1(sem_id) == -1) {
        perror("sem_post (client)");
    }

    /* Risposta */
    struct resp_msg resp;
    if (msgrcv(mq_id, &resp, sizeof(resp) - sizeof(long), (long)getpid(), 0) == -1) {
        perror("msgrcv(resp)");
        shmdt(shm_ptr);
        return 1;
    }

    shmdt(shm_ptr);

    if (resp.status != 0) {
        fprintf(stderr, "[client %d] Errore: %s\n", getpid(), resp.info);
        return 2;
    }

    // Modifica per un output più chiaro
    printf("Hash per il file '%s':\n%s\n", filename, resp.hash);

    return 0;
}
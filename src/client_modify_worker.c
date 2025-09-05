#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "protocol.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s <numero_nuovi_worker>\n", argv[0]);
        return 1;
    }
    int new_worker_count = atoi(argv[1]);
    if (new_worker_count <= 0) {
        printf("Numero di worker non valido\n");
        return 1;
    }
    key_t mq_key = ftok(MQ_PATH, MQ_PROJ);
    if (mq_key == -1) {
        perror("ftok");
        return 1;
    }
    int mq_id = msgget(mq_key, 0666);
    if (mq_id == -1) {
        perror("msgget");
        fprintf(stderr, "Hai avviato il server?\n");
        return 1;
    }
    struct req_msg req;
    req.mtype = MGMT_TYPE;
    req.size = (size_t)new_worker_count;
    
    if (msgsnd(mq_id, &req, sizeof(req) - sizeof(long), 0) == -1) {
        perror("msgsnd");
        return 1;
    }
    printf("Inviata richiesta di cambio numero worker a %d\n", new_worker_count);
    return 0;
}
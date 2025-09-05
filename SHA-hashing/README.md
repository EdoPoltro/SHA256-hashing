# 📂 Progetto di Hashing SHA-256 su Linux

Questo progetto implementa un sistema client-server per il calcolo dell'hash SHA-256 di file, sfruttando i meccanismi di comunicazione inter-processo (IPC) di tipo System V su un ambiente Linux. L'architettura è stata progettata per gestire più richieste simultaneamente in modo efficiente e scalabile.

## 🏗️ Architettura e Componenti

L'applicazione si basa su un'architettura **client-server** che utilizza la concorrenza tramite un **pool di worker thread**.

* **Server:** Un processo principale che inizializza le risorse IPC e gestisce un pool di **worker thread**. Il suo compito è ricevere richieste, assegnarle ai worker e gestire il pool in modo dinamico.
* **Worker:** Un insieme di **thread** che lavorano in parallelo. Ogni worker attende le richieste in arrivo, esegue il calcolo dell'hash SHA-256 sul file e invia la risposta al client.
* **Client Standard:** Un programma che si connette al server per richiedere il calcolo di un hash. Il client carica il file in memoria condivisa, invia un messaggio al server e attende la risposta.
* **Client di Gestione:** Un client speciale che invia un messaggio al server per **modificare dinamicamente** il numero di worker attivi.

---

## 🔗 Meccanismi IPC Utilizzati

Per garantire una comunicazione fluida e sicura tra i processi, il progetto impiega tre tipi di meccanismi IPC di System V.

1.  **Code di Messaggi (Message Queues):** Permettono la comunicazione asincrona tra i client e il server. Sono utilizzate per inviare due tipi di messaggi:
    * `REQ_TYPE`: Richieste di calcolo hash, elaborate dal pool di worker.
    * `MGMT_TYPE`: Richieste di gestione, elaborate dal processo principale del server per scalare il numero di worker.
2.  **Memoria Condivisa (Shared Memory):** Un'area di memoria a cui il client scrive il contenuto del file e da cui un worker lo legge per il calcolo. Questo metodo è estremamente efficiente per lo scambio di grandi quantità di dati.
3.  **Semafori (Semaphores):** Un semaforo binario funge da "lock" per la memoria condivisa. Un client lo acquisisce prima di scrivere il file e lo rilascia subito dopo aver inviato la richiesta. Un worker lo acquisisce per leggere i dati. Questo meccanismo previene la corruzione dei dati e garantisce l'accesso esclusivo alla memoria condivisa.

---

## 🚀 Compilazione ed Esecuzione

Assicurati di avere la libreria OpenSSL installata sul tuo sistema. Su distribuzioni come Ubuntu o WSL, puoi farlo con:

```bash
sudo apt update
sudo apt install libssl-dev
```

Compila il progetto con il Makefile fornito:
```bash
make
```

**Avvio del Server**

Apri un terminale e lancia il server in attesa di richieste.

```bash
./bin/server <FCFS|SJF>
```

**Calcolo dell'hash**

Per calcolare l'hash di un singolo file, usa il client standard.

```bash
./bin/client testfiles/a.txt
```

**Test di concorrenza**

Per verificare la capacità del server di gestire più richieste in parallelo, esegui più client in background.

```bash
./bin/client testfiles/a.txt &
./bin/client testfiles/b.txt &
./bin/client testfiles/c.txt &
./bin/client testfiles/d.txt &
./bin/client testfiles/e.txt &
./bin/client testfiles/f.txt &
wait
```

**Modifica Dinamica del Pool di Worker**

Per aumentare il numero di worker (ad esempio da 3 a 5), usa il client di gestione.

```bash
./bin/client_modify_worker 5
```
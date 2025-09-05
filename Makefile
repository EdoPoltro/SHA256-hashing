# Variabili per i file sorgenti e gli eseguibili
SRC_DIR := src
BIN_DIR := bin
SRC_FILES := $(wildcard $(SRC_DIR)/*.c)
BIN_FILES := $(patsubst $(SRC_DIR)/%.c, $(BIN_DIR)/%, $(SRC_FILES))

# Flags di compilazione
CFLAGS := -Wall -Wextra -O2 -std=c11

# Librerie necessarie (per l'OpenSSL)
LIBS := -lcrypto

# Target predefiniti
all: $(BIN_FILES)

# Regola per creare gli eseguibili direttamente
$(BIN_DIR)/%: $(SRC_DIR)/%.c
	@mkdir -p $(BIN_DIR)  # Crea la cartella bin se non esiste
	gcc $(CFLAGS) $< -o $@ $(LIBS)  # Compila e crea l'eseguibile

# Regola per il comando "make clean"
clean:
	rm -rf $(BIN_DIR)  # Rimuove la cartella bin e tutto ciò che contiene


# ====================================================================
# Makefile Unificato per Progetti C e Java
# ====================================================================

# --- Target di Default e Comuni ---

# Il target 'all' è quello eseguito di default con 'make'.
# Compila sia il programma C (in modalità release) che quello Java.
.PHONY: all
all: c_release java_compile

# Il target 'clean' rimuove TUTTI i file generati da entrambi i processi.
.PHONY: clean
clean:
	@echo "Pulizia dei file generati da C e Java..."
	# Pulisce i file del C
	rm -f $(C_TARGET) $(C_OBJS)
	# Pulisce i file .class dalla cartella corrente e gli altri file di output.
	rm -f *.class nomi.txt grafo.txt partecipazioni.txt
	# Rimuove la cartella 'bin' nel caso esista da esecuzioni precedenti.
	rm -rf bin
	@echo "Pulizia completata."


# ====================================================================
# Sezione per il Codice C
# ====================================================================

# --- Variabili di Compilazione C ---
CC = gcc
C_TARGET = cammini.out
C_SRCS = cammini.c
C_OBJS = $(C_SRCS:.c=.o)
LDLIBS = -pthread

# Flags per la versione di rilascio (ottimizzata)
CFLAGS_RELEASE = -Wall -Wextra -pedantic -O3

# Flags per la versione di debug
CFLAGS_DEBUG   = -Wall -Wextra -std=c11 -g -D_GNU_SOURCE

# --- Regole per il Codice C ---

# Target specifico per compilare il C in modalità release (ottimizzata).
# Eseguibile con 'make c_release' o semplicemente 'make'.
.PHONY: c_release
c_release: CFLAGS = $(CFLAGS_RELEASE)
c_release: $(C_TARGET)

# Target specifico per compilare il C in modalità debug.
# Eseguibile con 'make c_debug'.
.PHONY: c_debug
c_debug: CFLAGS = $(CFLAGS_DEBUG)
c_debug: $(C_TARGET)

# Regola di linking per creare l'eseguibile C
$(C_TARGET): $(C_OBJS)
	$(CC) -o $@ $^ $(LDLIBS)

# Regola di compilazione per creare i file oggetto C
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<


# ====================================================================
# Sezione per il Codice Java (MODIFICATA)
# ====================================================================

# --- Variabili di Compilazione/Esecuzione Java ---
JCC = javac
JAVA_SRCS = CreaGrafo.java
JAVA_MAIN_CLASS = CreaGrafo
# Rimuoviamo la flag -d bin per compilare nella directory corrente.
JFLAGS =
# Opzioni per la JVM (necessarie per la dimensione del grafo)
JVM_OPTS = -Xmx4g
# Argomenti per l'esecuzione del programma Java
JAVA_ARGS = name.basics.tsv title.principals.tsv title.basics.tsv


# --- Regole per il Codice Java ---

# Target per la compilazione del codice Java.
# Eseguibile con 'make java_compile'.
.PHONY: java_compile
java_compile:
	$(JCC) $(JFLAGS) $(JAVA_SRCS)
	@echo "Compilazione Java completata. I file .class sono nella directory corrente."

# Target per l'esecuzione del programma Java.
# Compila prima, se necessario. Eseguibile con 'make run_java'.
.PHONY: run_java
run_java: java_compile
	@echo "Esecuzione Java con opzioni JVM: $(JVM_OPTS)"
	# Il classpath ora è '.', la directory corrente.
	java -cp . $(JVM_OPTS) $(JAVA_MAIN_CLASS) $(JAVA_ARGS)

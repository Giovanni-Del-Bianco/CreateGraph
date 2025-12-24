#define _GNU_SOURCE // Per getline e strdup. Mantenuto qui.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h> 
#include <fcntl.h>    
#include <unistd.h>   
#include <signal.h>
#include <errno.h>
#include <sys/times.h> 
#include <stdint.h>    
#include <limits.h>    
#include <time.h>      
#include <sys/select.h>

// Valori per S_PROGRAM_PHASE
#define PHASE_GRAPH_CONSTRUCTION 0
#define PHASE_PIPE_READING 1

// --- Strutture Dati ---
typedef struct {
    int codice;
    char *nome;
    int anno;
    int numcop;
    int *cop;
} attore;

// Buffer condiviso per produttore-consumatore (linee da grafo.txt)
typedef struct {
    char **buffer;
    int capacity;
    int count;
    int head;
    int tail;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    int done_producing;
} line_buffer_t;

// Nodo per la coda FIFO (usata in BFS)
typedef struct q_node {
    int codice;
    struct q_node *next;
} q_node_t;

// Coda FIFO
typedef struct {
    q_node_t *head;
    q_node_t *tail;
} fifo_queue_t;

// Nodo per Albero Binario di Ricerca (ABR) (usato in BFS per 'explored' e 'parent')
typedef struct abr_node {
    int shuffled_codice; // Chiave (codice "mescolato")
    int original_codice; // Codice attore originale
    int parent_codice;   // Codice del predecessore nel cammino BFS
    struct abr_node *left;
    struct abr_node *right;
} abr_node_t;

// Argomenti per i thread consumatori
typedef struct {
    line_buffer_t *buffer;
    attore *attori_arr;
    int tota_attori;
} consumer_args_t;

// Argomenti per i thread BFS
typedef struct {
    attore *attori_arr;
    int tota_attori;
    int start_codice_orig;
    int end_codice_orig;
} bfs_args_t;

// --- Variabili Statiche (per coordinamento segnali) ---
static volatile sig_atomic_t S_PROGRAM_PHASE = PHASE_GRAPH_CONSTRUCTION;
static volatile sig_atomic_t S_SHUTDOWN_REQUEST = 0;
static pthread_t S_MAIN_THREAD_ID;
static int S_CAMMINI_PIPE_FD = -1; 
static int S_SELF_PIPE_FD[2] = {-1, -1};

// --- Funzioni Shuffle/Unshuffle ---
int shuffle(int n) {
    return ((((n & 0x3F) << 26) | ((n >> 6) & 0x3FFFFFF)) ^ 0x55555555);
}

// Handler vuoto, serve solo per interrompere le chiamate di sistema bloccanti.
static void empty_signal_handler(int signum) {
    (void)signum; // Sopprime l'avviso "unused parameter"
}

// --- Funzione di Comparazione per bsearch ---
int compare_attori(const void *a, const void *b) {
    const attore *att_a = (const attore *)a;
    const attore *att_b = (const attore *)b;
    if (att_a->codice < att_b->codice) return -1;
    if (att_a->codice > att_b->codice) return 1;
    return 0;
}

// Wrapper per bsearch per trovare un attore
attore *find_attore_by_codice(int codice, attore *attori_arr, int tota_attori) {
    attore key = { .codice = codice };
    return (attore *)bsearch(&key, attori_arr, tota_attori, sizeof(attore), compare_attori);
}

// --- Funzioni di Utilità (Gestione Errori) ---
void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (!p) {
        perror("malloc fallita");
        exit(EXIT_FAILURE);
    }
    return p;
}

char *xstrdup(const char *s) {
    char *p = strdup(s);
    if (!p) {
        perror("strdup fallito");
        exit(EXIT_FAILURE);
    }
    return p;
}

FILE *xfopen(const char *path, const char *mode) {
    FILE *fp = fopen(path, mode);
    if (!fp) {
        perror(path);
        exit(EXIT_FAILURE);
    }
    return fp;
}

// --- Funzioni Line Buffer (Produttore-Consumatore) ---
line_buffer_t* line_buffer_init(int capacity) {
    line_buffer_t *lb = (line_buffer_t*)xmalloc(sizeof(line_buffer_t));
    lb->buffer = (char**)xmalloc(capacity * sizeof(char*));
    lb->capacity = capacity;
    lb->count = 0;
    lb->head = 0;
    lb->tail = 0;
    lb->done_producing = 0;
    if (pthread_mutex_init(&lb->mutex, NULL) != 0) {
        perror("pthread_mutex_init for line_buffer"); exit(EXIT_FAILURE);
    }
    if (pthread_cond_init(&lb->not_empty, NULL) != 0) {
        perror("pthread_cond_init not_empty for line_buffer"); exit(EXIT_FAILURE);
    }
    if (pthread_cond_init(&lb->not_full, NULL) != 0) {
        perror("pthread_cond_init not_full for line_buffer"); exit(EXIT_FAILURE);
    }
    return lb;
}

void line_buffer_put(line_buffer_t *lb, char *line) {
    pthread_mutex_lock(&lb->mutex);
    while (lb->count == lb->capacity && !lb->done_producing) {
        pthread_cond_wait(&lb->not_full, &lb->mutex);
    }
    if (lb->count < lb->capacity) {
        lb->buffer[lb->tail] = line;
        lb->tail = (lb->tail + 1) % lb->capacity;
        lb->count++;
        pthread_cond_signal(&lb->not_empty);
    } else {
        fprintf(stderr, "Attenzione: buffer linee pieno, linea scartata. Aumentare capacità.\n");
        free(line);
    }
    pthread_mutex_unlock(&lb->mutex);
}

char* line_buffer_get(line_buffer_t *lb) {
    pthread_mutex_lock(&lb->mutex);
    while (lb->count == 0 && !lb->done_producing) {
        pthread_cond_wait(&lb->not_empty, &lb->mutex);
    }
    char *line = NULL;
    if (lb->count > 0) {
        line = lb->buffer[lb->head];
        lb->head = (lb->head + 1) % lb->capacity;
        lb->count--;
        pthread_cond_signal(&lb->not_full);
    } else if (lb->done_producing && lb->count == 0) {
        line = NULL;
    }
    pthread_mutex_unlock(&lb->mutex);
    return line;
}

void line_buffer_set_done(line_buffer_t *lb) {
    pthread_mutex_lock(&lb->mutex);
    lb->done_producing = 1;
    pthread_cond_broadcast(&lb->not_empty);
    pthread_mutex_unlock(&lb->mutex);
}

void line_buffer_destroy(line_buffer_t *lb) {
    pthread_mutex_destroy(&lb->mutex);
    pthread_cond_destroy(&lb->not_empty);
    pthread_cond_destroy(&lb->not_full);
    free(lb->buffer);
    free(lb);
}

// --- Funzioni Coda FIFO (per BFS) ---
fifo_queue_t* fifo_queue_create() {
    fifo_queue_t *q = (fifo_queue_t*)xmalloc(sizeof(fifo_queue_t));
    q->head = q->tail = NULL;
    return q;
}

void fifo_queue_enqueue(fifo_queue_t *q, int codice) {
    q_node_t *newNode = (q_node_t*)xmalloc(sizeof(q_node_t));
    newNode->codice = codice;
    newNode->next = NULL;
    if (q->tail == NULL) {
        q->head = q->tail = newNode;
    } else {
        q->tail->next = newNode;
        q->tail = newNode;
    }
}

int fifo_queue_dequeue(fifo_queue_t *q, int *codice_out) {
    if (q->head == NULL) return 0;
    q_node_t *temp = q->head;
    *codice_out = temp->codice;
    q->head = q->head->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    free(temp);
    return 1;
}

int fifo_queue_is_empty(fifo_queue_t *q) {
    return q->head == NULL;
}

void fifo_queue_destroy(fifo_queue_t *q) {
    int temp_codice;
    while (fifo_queue_dequeue(q, &temp_codice));
    free(q);
}

// --- Funzioni ABR (per BFS) ---
abr_node_t* abr_insert(abr_node_t **root_ptr, int shuffled_key, int original_key, int parent_key) {
    if (*root_ptr == NULL) {
        *root_ptr = (abr_node_t*)xmalloc(sizeof(abr_node_t));
        (*root_ptr)->shuffled_codice = shuffled_key;
        (*root_ptr)->original_codice = original_key;
        (*root_ptr)->parent_codice = parent_key;
        (*root_ptr)->left = (*root_ptr)->right = NULL;
        return *root_ptr;
    }
    if (shuffled_key < (*root_ptr)->shuffled_codice) {
        return abr_insert(&((*root_ptr)->left), shuffled_key, original_key, parent_key);
    } else if (shuffled_key > (*root_ptr)->shuffled_codice) {
        return abr_insert(&((*root_ptr)->right), shuffled_key, original_key, parent_key);
    }
    return *root_ptr;
}

abr_node_t* abr_search(abr_node_t *root, int shuffled_key) {
    if (root == NULL || root->shuffled_codice == shuffled_key) {
        return root;
    }
    if (shuffled_key < root->shuffled_codice) {
        return abr_search(root->left, shuffled_key);
    } else {
        return abr_search(root->right, shuffled_key);
    }
}

void abr_free(abr_node_t *root) {
    if (root == NULL) return;
    abr_free(root->left);
    abr_free(root->right);
    free(root);
}


// --- Thread Gestore Segnali ---
void *signal_handler_thread_func(void *arg) {
    (void)arg;

    sigset_t set;
    int sig;

    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
    write(STDOUT_FILENO, pid_str, strlen(pid_str));

    sigemptyset(&set);
    sigaddset(&set, SIGINT);

    while (1) {
        if (sigwait(&set, &sig) != 0) {
            if (errno == EINTR) continue;
            perror("sigwait fallito");
            break;
        }

        if (sig == SIGINT) {
            if (S_PROGRAM_PHASE == PHASE_PIPE_READING) {
                // Self-pipe trick: notifica al main di terminare scrivendo un byte.
                char dummy = 'q'; 
                if (write(S_SELF_PIPE_FD[1], &dummy, 1) < 0) {
                }
                break;
            } else {
                const char *msg = "Costruzione del grafo in corso\n";
                write(STDOUT_FILENO, msg, strlen(msg));
            }
        }
    }
    return NULL;
}


// --- Thread Consumatore (per leggere grafo.txt) ---
void *consumer_thread_func(void *arg) {
    consumer_args_t *args = (consumer_args_t *)arg;
    char *line;
    char *saveptr; // Puntatore di stato per strtok_r

    // Continua a prendere linee dal buffer finché non ce ne sono più
    // e il produttore non ha finito.
    while ((line = line_buffer_get(args->buffer)) != NULL) {
        
        // strtok_r modifica la stringa, quindi lavoriamo direttamente su 'line'
        // che verrà liberata alla fine di ogni iterazione del ciclo.
        
        // 1. Estrai il primo token: il codice dell'attore principale
        char *token = strtok_r(line, " \t\n", &saveptr);
        if (token == NULL) { // Riga vuota o con solo spazi
            free(line);
            continue;
        }

        int codice_attore = atoi(token);
        
        // 2. Cerca l'attore nell'array principale usando la ricerca binaria
        attore *current_actor = find_attore_by_codice(codice_attore, args->attori_arr, args->tota_attori);

        if (current_actor == NULL) {
            // Se l'attore non è nel nostro file nomi.txt, ignoriamo la riga.
            fprintf(stderr, "Attenzione: codice attore %d trovato in grafo.txt ma non in nomi.txt. Riga ignorata.\n", codice_attore);
            free(line);
            continue;
        }

        // 3. Inizializza i dati dei coprotagonisti per questo attore.
        // È fondamentale per prepararsi a un nuovo popolamento.
        current_actor->numcop = 0;
        current_actor->cop = NULL; // Partiamo con un puntatore nullo
        int capacity = 0;      // e una capacità allocata di 0.

        // 4. Cicla su tutti i token rimanenti (i coprotagonisti)
        token = strtok_r(NULL, " \t\n", &saveptr);
        while (token != NULL) {
            
            // 5. Gestione dinamica dell'array 'cop'
            // Se abbiamo riempito l'array (o se è la prima volta), dobbiamo allocare più memoria.
            if (current_actor->numcop == capacity) {
                // Strategia di crescita esponenziale per efficienza (riduce le chiamate a realloc)
                capacity = (capacity == 0) ? 8 : capacity * 2;
                int *new_cop_ptr = realloc(current_actor->cop, capacity * sizeof(int));
                
                // Gestione di un errore critico di memoria
                if (new_cop_ptr == NULL) {
                    perror("realloc fallita nel thread consumatore");
                    free(line);
                    exit(EXIT_FAILURE); 
                }
                current_actor->cop = new_cop_ptr;
            }

            // 6. Aggiungi il nuovo coprotagonista all'array
            current_actor->cop[current_actor->numcop] = atoi(token);
            current_actor->numcop++;

            // Prendi il token successivo
            token = strtok_r(NULL, " \t\n", &saveptr);
        }
        
        
        if (current_actor->numcop > 0 && current_actor->numcop < capacity) {
            int *final_cop_ptr = realloc(current_actor->cop, current_actor->numcop * sizeof(int));
            if (final_cop_ptr != NULL) {
                current_actor->cop = final_cop_ptr;
            }
           
        }


        // 7. Libera la memoria della linea letta dal file, pronta per la prossima.
        free(line);
    }
    
    return NULL;
}

// --- Thread Calcolo Cammino Minimo (BFS) ---
void *bfs_thread_func(void *arg) {
    bfs_args_t *args = (bfs_args_t *)arg;
    pthread_detach(pthread_self());

    struct tms t_start, t_end;
    long ticks_per_sec = sysconf(_SC_CLK_TCK);
    if (ticks_per_sec <= 0) ticks_per_sec = 100; 

    times(&t_start); // Inizio misurazione tempo

    char output_filename[256];
    sprintf(output_filename, "%d.%d", args->start_codice_orig, args->end_codice_orig);

    FILE *out_fp = fopen(output_filename, "w");
    if (!out_fp) {
        fprintf(stderr, "Errore: impossibile creare file di output %s per %d-%d\n",
                output_filename, args->start_codice_orig, args->end_codice_orig);
        // Stampa su stdout che c'è stato un errore
        printf("%d.%d: Errore creazione file output. Tempo di elaborazione 0.00 secondi\n",
               args->start_codice_orig, args->end_codice_orig);
        fflush(stdout);
        free(args); // Libera gli argomenti allocati dal chiamante
        return NULL;
    }

    attore *start_node = find_attore_by_codice(args->start_codice_orig, args->attori_arr, args->tota_attori);
    attore *end_node = find_attore_by_codice(args->end_codice_orig, args->attori_arr, args->tota_attori);

    if (!start_node) {
        fprintf(out_fp, "codice %d non valido\n", args->start_codice_orig);
    } else if (!end_node) {
        fprintf(out_fp, "codice %d non valido\n", args->end_codice_orig);
    } else {
        fifo_queue_t *queue = fifo_queue_create();
        abr_node_t *explored_root = NULL; // ABR per nodi visitati e predecessori

        // Inserisci il nodo di partenza
        fifo_queue_enqueue(queue, args->start_codice_orig);
        abr_insert(&explored_root, shuffle(args->start_codice_orig), args->start_codice_orig, -1); // -1 indica nessun parente

        int path_found = 0;
        int current_codice; // Dichiarata qui, sarà usata per il dequeue

        while (!fifo_queue_is_empty(queue) && !path_found) {
            
            if (!fifo_queue_dequeue(queue, &current_codice)) {
                break; // Uscita di sicurezza se la coda è vuota
            }

            if (current_codice == args->end_codice_orig) {
                path_found = 1;
                break;
            }

            attore *current_attore_obj = find_attore_by_codice(current_codice, args->attori_arr, args->tota_attori);
            if (!current_attore_obj) { 
                // fprintf(stderr, "BFS: Attore con codice %d non trovato durante la ricerca.\n", current_codice);
                continue; // Salta questo attore se non trovato
            }

            for (int i = 0; i < current_attore_obj->numcop; ++i) {
                int neighbor_codice = current_attore_obj->cop[i];
                if (abr_search(explored_root, shuffle(neighbor_codice)) == NULL) { // Se il vicino non è stato esplorato
                    abr_insert(&explored_root, shuffle(neighbor_codice), neighbor_codice, current_codice); // Marca come esplorato e registra il parente
                    fifo_queue_enqueue(queue, neighbor_codice); // Aggiungi il vicino alla coda
                }
            }
        }

        if (path_found) {
            // Ricostruisci il cammino (al contrario)
            int *path = (int*)xmalloc(args->tota_attori * sizeof(int)); // Max path length
            int path_len = 0;
            int trace_codice = args->end_codice_orig;
            while (trace_codice != -1) { // -1 è il parente del nodo start
                if (path_len >= args->tota_attori) { // Sicurezza contro loop infiniti o cammini troppo lunghi
                    fprintf(stderr, "Errore: superata lunghezza massima del cammino durante la ricostruzione per %d-%d.\n", args->start_codice_orig, args->end_codice_orig);
                    path_len = 0; // Invalida il cammino
                    break;
                }
                path[path_len++] = trace_codice;
                abr_node_t *node_in_path = abr_search(explored_root, shuffle(trace_codice));
                if (!node_in_path) { 
                    fprintf(stderr, "Errore critico nella ricostruzione del cammino per %d-%d! Nodo %d (shuffled %d) non trovato in ABR.\n", args->start_codice_orig, args->end_codice_orig, trace_codice, shuffle(trace_codice));
                    path_len = 0; // Segnala errore
                    break;
                }
                trace_codice = node_in_path->parent_codice;
            }

            // Stampa il cammino (in ordine corretto)
            for (int i = path_len - 1; i >= 0; --i) {
                attore *actor_on_path = find_attore_by_codice(path[i], args->attori_arr, args->tota_attori);
                if (actor_on_path) { // Dovrebbe essere sempre vero
                    fprintf(out_fp, "%d\t%s\t%d\n", actor_on_path->codice, actor_on_path->nome, actor_on_path->anno);
                }
            }
            free(path);
            times(&t_end);
            double elapsed_sec = (double)(t_end.tms_utime - t_start.tms_utime + t_end.tms_stime - t_start.tms_stime) / ticks_per_sec;
            printf("%d.%d: Lunghezza minima %d. Tempo di elaborazione %.2f secondi\n",
                   args->start_codice_orig, args->end_codice_orig, path_len > 0 ? path_len -1 : 0, elapsed_sec);

        } else {
            fprintf(out_fp, "non esistono cammini da %d a %d\n", args->start_codice_orig, args->end_codice_orig);
            times(&t_end);
            double elapsed_sec = (double)(t_end.tms_utime - t_start.tms_utime + t_end.tms_stime - t_start.tms_stime) / ticks_per_sec;
            printf("%d.%d: Nessun cammino. Tempo di elaborazione %.2f secondi\n",
                   args->start_codice_orig, args->end_codice_orig, elapsed_sec);
        }
        fflush(stdout); // Assicura che l'output su stdout sia visibile immediatamente
        fifo_queue_destroy(queue);
        abr_free(explored_root);
    }

    fclose(out_fp);
    free(args); // Libera gli argomenti allocati dal chiamante
    return NULL;
}


// --- Funzione Main ---
int main(int argc, char *argv[]) {
    S_MAIN_THREAD_ID = pthread_self();

    if (argc != 4) {
        fprintf(stderr, "Uso: %s <filenomi> <filegrafo> <numconsumatori>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *filenomi_path = argv[1];
    char *filegrafo_path = argv[2];
    long num_consumatori_long = strtol(argv[3], NULL, 10);

    if (num_consumatori_long <= 0 || num_consumatori_long > 1024) {
        fprintf(stderr, "Errore: numconsumatori deve essere un intero positivo (max 1024).\n");
        exit(EXIT_FAILURE);
    }
    int num_consumatori = (int)num_consumatori_long;

    // --- SETUP GESTIONE SEGNALI ---
    sigset_t sigint_mask;
    sigemptyset(&sigint_mask);
    sigaddset(&sigint_mask, SIGINT);
    if (pthread_sigmask(SIG_BLOCK, &sigint_mask, NULL) != 0) {
        perror("pthread_sigmask fallito");
        exit(EXIT_FAILURE);
    }

    // Creiamo la self-pipe per la notifica dei segnali
    if (pipe(S_SELF_PIPE_FD) == -1) {
        perror("pipe (self-pipe) fallito");
        exit(EXIT_FAILURE);
    }

    pthread_t signal_tid;
    if (pthread_create(&signal_tid, NULL, signal_handler_thread_func, NULL) != 0) {
        perror("pthread_create per signal_handler_thread fallito");
        exit(EXIT_FAILURE);
    }


    // --- FASE 1: COSTRUZIONE GRAFO ---
    S_PROGRAM_PHASE = PHASE_GRAPH_CONSTRUCTION;

    // --- Inizio del blocco di codice che avevo omesso ---
    FILE *fp_nomi = xfopen(filenomi_path, "r");
    int tota_attori = 0;
    char *line = NULL;
    size_t len = 0;
    ssize_t read_len;

    while ((read_len = getline(&line, &len, fp_nomi)) != -1) {
        if (read_len > 1) tota_attori++;
    }
    rewind(fp_nomi);

    if (tota_attori == 0) {
        fprintf(stderr, "Errore: %s è vuoto o non contiene attori validi.\n", filenomi_path);
        fclose(fp_nomi);
        if (line) free(line);
        exit(EXIT_FAILURE);
    }

    attore *attori_arr = (attore *)xmalloc(tota_attori * sizeof(attore));
    int current_idx = 0;
    while ((read_len = getline(&line, &len, fp_nomi)) != -1 && current_idx < tota_attori) {
        if (read_len <= 1) continue;
        if (line[read_len - 1] == '\n') line[read_len - 1] = '\0';
        char *codice_str = strtok(line, "\t");
        char *nome_str = strtok(NULL, "\t");
        char *anno_str = strtok(NULL, "\t");
        if (codice_str && nome_str && anno_str) {
            attori_arr[current_idx].codice = atoi(codice_str);
            attori_arr[current_idx].nome = xstrdup(nome_str);
            attori_arr[current_idx].anno = atoi(anno_str);
            attori_arr[current_idx].cop = NULL;
            attori_arr[current_idx].numcop = 0;
            current_idx++;
        }
    }
    tota_attori = current_idx;
    fclose(fp_nomi);
    if (line) free(line);
    line = NULL; len = 0;

    qsort(attori_arr, tota_attori, sizeof(attore), compare_attori);

    line_buffer_t *shared_line_buffer = line_buffer_init(num_consumatori * 10);
    pthread_t *consumer_tids = (pthread_t *)xmalloc(num_consumatori * sizeof(pthread_t));
    consumer_args_t consumer_args = { shared_line_buffer, attori_arr, tota_attori };

    for (int i = 0; i < num_consumatori; ++i) {
        if (pthread_create(&consumer_tids[i], NULL, consumer_thread_func, &consumer_args) != 0) {
            perror("pthread_create per consumer_thread fallito");
            exit(EXIT_FAILURE);
        }
    }

    FILE *fp_grafo = xfopen(filegrafo_path, "r");
    while ((read_len = getline(&line, &len, fp_grafo)) != -1) {
        if (read_len > 1) {
            line_buffer_put(shared_line_buffer, xstrdup(line));
        }
    }
    fclose(fp_grafo);
    if (line) free(line);

    line_buffer_set_done(shared_line_buffer);
    for (int i = 0; i < num_consumatori; ++i) {
        pthread_join(consumer_tids[i], NULL);
    }
    free(consumer_tids);
    line_buffer_destroy(shared_line_buffer);
    // --- Fine del blocco di codice che avevo omesso ---
    // --- FINE FASE 1 ---


    // --- FASE 2: LETTURA DALLA PIPE CON SELECT ---
    S_PROGRAM_PHASE = PHASE_PIPE_READING;
    const char *pipe_name = "cammini.pipe";
    unlink(pipe_name);
    if (mkfifo(pipe_name, 0666) == -1) {
        perror("mkfifo fallito");
        exit(EXIT_FAILURE);
    }
    S_CAMMINI_PIPE_FD = open(pipe_name, O_RDONLY | O_NONBLOCK);
    if (S_CAMMINI_PIPE_FD == -1) {
        perror("open pipe for reading fallito");
        unlink(pipe_name);
        exit(EXIT_FAILURE);
    }

    fd_set read_fds;
    int max_fd = (S_CAMMINI_PIPE_FD > S_SELF_PIPE_FD[0]) ? S_CAMMINI_PIPE_FD : S_SELF_PIPE_FD[0];
    int keep_looping = 1;

    while (keep_looping) {
        FD_ZERO(&read_fds);
        FD_SET(S_CAMMINI_PIPE_FD, &read_fds);
        FD_SET(S_SELF_PIPE_FD[0], &read_fds);

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if (activity < 0) {
            if (errno == EINTR) continue;
            perror("select fallito");
            break;
        }

        if (FD_ISSET(S_SELF_PIPE_FD[0], &read_fds)) {
            keep_looping = 0; // Segnale di terminazione ricevuto
            continue;
        }

        if (FD_ISSET(S_CAMMINI_PIPE_FD, &read_fds)) {
            int32_t codici_pipe[2];
            ssize_t bytes_read = read(S_CAMMINI_PIPE_FD, codici_pipe, sizeof(codici_pipe));

            if (bytes_read == 0) {
                close(S_CAMMINI_PIPE_FD);
                S_CAMMINI_PIPE_FD = open(pipe_name, O_RDONLY | O_NONBLOCK);
                if (S_CAMMINI_PIPE_FD == -1) {
                    perror("riapertura pipe fallita");
                    keep_looping = 0;
                }
                max_fd = (S_CAMMINI_PIPE_FD > S_SELF_PIPE_FD[0]) ? S_CAMMINI_PIPE_FD : S_SELF_PIPE_FD[0];
                continue;
            }
            
            if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                 // Nessun dato da leggere, è normale con O_NONBLOCK. Continua a select.
                 continue;
            }
            
            if (bytes_read < 0) {
                perror("read da cammini.pipe fallito");
                continue;
            }

            if (bytes_read == sizeof(codici_pipe)) {
                bfs_args_t *bfs_task_args = (bfs_args_t*)xmalloc(sizeof(bfs_args_t));
                bfs_task_args->attori_arr = attori_arr;
                bfs_task_args->tota_attori = tota_attori;
                bfs_task_args->start_codice_orig = codici_pipe[0];
                bfs_task_args->end_codice_orig = codici_pipe[1];
                
                pthread_t bfs_tid;
                if (pthread_create(&bfs_tid, NULL, bfs_thread_func, bfs_task_args) != 0) {
                    perror("pthread_create per bfs_thread fallito");
                    free(bfs_task_args);
                }
            }
        }
    }

    // --- FASE 3: TERMINAZIONE ---
    close(S_CAMMINI_PIPE_FD);
    close(S_SELF_PIPE_FD[0]);
    close(S_SELF_PIPE_FD[1]);
    
    struct timespec wait_time = {20, 0};
    nanosleep(&wait_time, NULL);
    
    for (int i = 0; i < tota_attori; ++i) {
        if (attori_arr[i].nome) free(attori_arr[i].nome);
        if (attori_arr[i].cop) free(attori_arr[i].cop);
    }
    free(attori_arr);
    unlink(pipe_name);

    pthread_join(signal_tid, NULL);

    return 0;
}

# CreaGrafo
Progetto laboratorio 2 2024/2025 contente codici in C JAVA e PYTHON di Giovanni Del Bianco, numero di matricola 636115.

# Analisi Tecnica del Sistema di Creazione e Attraversamento di Grafi

Questo documento offre un'analisi tecnica dettagliata di un sistema software composto da due programmi principali: `CreaGrafo`, sviluppato in Java, e `cammini.c`, scritto in C. Il primo si occupa della costruzione di un grafo di attori e delle loro collaborazioni a partire da dati IMDb, mentre il secondo implementa algoritmi per trovare cammini minimi all'interno di tale grafo.

## Parte 1: Creazione del Grafo con `CreaGrafo` (Java)

Il programma `CreaGrafo` è responsabile della fase di preprocessing: legge, filtra e struttura i dati grezzi per costruire una rappresentazione del grafo in memoria.

### 1.1. Parsing e Filtraggio degli Attori da `name.basics.tsv`

Il primo passo è l'identificazione degli attori di interesse dal file `name.basics.tsv`. Il processo è suddiviso in fasi sequenziali.

#### Fase 1: Setup dell'I/O e Gestione delle Risorse

Il processo inizia con l'apertura del file tramite un blocco `try-with-resources`, una pratica robusta e moderna in Java.

```java
try (BufferedReader br = new BufferedReader(new FileReader(pathNameBasics))) {
    // ... logica di parsing ...
}
```

*   **`FileReader`**: Crea un flusso di lettura a livello di caratteri direttamente dal file.
*   **`BufferedReader`**: Incapsula il `FileReader` per ottimizzare le performance. Carica blocchi di dati (buffer) in RAM, riducendo le costose operazioni di I/O su disco.
*   **`try-with-resources`**: Garantisce la chiusura automatica del `BufferedReader` (e del `FileReader` sottostante) al termine del blocco, anche in caso di eccezioni, prevenendo resource leak.

#### Fase 2: Iterazione e Tokenizzazione delle Righe

Il programma itera su ogni riga del file per estrarne i dati:

1.  **Salto dell'Intestazione**: La prima riga del file (contenente i nomi delle colonne) viene letta e scartata con `br.readLine()` prima dell'inizio del ciclo.
2.  **Ciclo di Lettura**: Un ciclo `while` itera su ogni riga successiva finché `br.readLine()` non restituisce `null` (fine del file).
3.  **Tokenizzazione**: Ogni riga viene suddivisa in un array di stringhe usando il carattere di tabulazione (`\t`) come delimitatore.

    ```java
    // Esempio riga: "nm0000102\tKevin Bacon\t1958\t\N\tactor,producer,director\tt..."
    String[] fields = line.split("\t");
    // fields[0] -> "nm0000102"
    // fields[1] -> "Kevin Bacon"
    // ...
    ```

#### Fase 3: Estrazione e Filtraggio dei Dati

Per ogni riga, vengono applicati filtri per selezionare solo gli attori rilevanti.

*   **Filtro Anno di Nascita**: Se il campo `birthYear` (indice 2) è `\\N` (valore nullo di IMDb), la riga viene scartata.
    ```java
    if ("\\N".equals(birthYearStr)) {
        continue;
    }
    ```
*   **Filtro Professione**: Si verifica che il campo `primaryProfession` (indice 4) contenga la sottostringa `"actor"` o `"actress"`, escludendo altre figure professionali.
    ```java
    if (primaryProfession == null || (!primaryProfession.contains("actor") && !primaryProfession.contains("actress"))) {
        continue;
    }
    ```

#### Fase 4: Conversione dei Tipi e Gestione degli Errori

I dati validati, ancora in formato stringa, vengono convertiti in tipi numerici. L'intero processo è protetto da un blocco `try-catch` per gestire righe malformate senza interrompere l'esecuzione.

```java
try {
    // Rimuove "nm" e converte a intero
    int codice = Integer.parseInt(nconst.substring(2)); 
    int anno = Integer.parseInt(birthYearStr);
    
    // ... crea oggetto Attore ...
} catch (NumberFormatException | StringIndexOutOfBoundsException e) {
    // Ignora silenziosamente la riga corrotta e continua
}
```
*   **`NumberFormatException`**: Catturata se una stringa non è un numero valido.
*   **`StringIndexOutOfBoundsException`**: Catturata se, ad esempio, `nconst` è troppo corto.

#### Fase 5: Memorizzazione

Gli attori validati vengono istanziati come oggetti `Attore` e inseriti in una `HashMap<Integer, Attore>`, usando il codice numerico come chiave.

```java
Attore attore = new Attore(codice, primaryName, anno);
attori.put(codice, attore);
```
L'uso di una `HashMap` garantisce un accesso quasi istantaneo (complessità media **O(1)**) agli attori, fondamentale per l'efficienza della fase successiva.

### 1.2. Costruzione delle Relazioni tra Attori da `title.principals.tsv`

Questa fase costruisce gli archi del grafo (le collaborazioni) analizzando il file delle partecipazioni ai film.

#### Struttura Dati nella Classe `Attore`

La classe `Attore` utilizza due `Set` per memorizzare le relazioni, una scelta di design cruciale.

```java
class Attore {
    // ...
    Set<Integer> coprotagonisti;
    Set<Integer> titoliPartecipati;

    public Attore(...) {
        this.coprotagonisti = new HashSet<>();
        this.titoliPartecipati = new HashSet<>();
    }
}
```
L'implementazione `HashSet` offre due vantaggi chiave:
*   **Garanzia di Unicità**: Un `Set` non ammette duplicati. Questo assicura che un titolo o un co-protagonista non vengano aggiunti più volte, mantenendo il grafo semplice.
*   **Performance**: Le operazioni di inserimento (`add`) e ricerca (`contains`) in un `HashSet` hanno una complessità media di **O(1)**, drasticamente più efficiente di una `List` (che richiederebbe O(n)).

#### Algoritmo di Elaborazione "a Flusso"

Il programma non carica l'intero `title.principals.tsv` in memoria, ma lo processa in modo sequenziale, sfruttando il fatto che le righe sono raggruppate per film (`tconst`).

1.  **Accumulo del Cast**: Il programma legge le righe e, finché il `tconst` non cambia, accumula i codici degli attori di un film in un `Set<Integer> currentCast`. Per ogni attore, aggiunge il codice del film al suo `Set titoliPartecipati`.
    ```java
    Attore attore = attori.get(codiceAttore);
    attore.titoliPartecipati.add(codiceTitolo); // Operazione O(1)
    ```

2.  **Elaborazione delle Relazioni**: Quando il `tconst` cambia, significa che il cast del film precedente è stato raccolto completamente. Viene quindi invocato il metodo `processCast(currentCast)`.

3.  **Creazione degli Archi**: Il metodo `processCast` genera ogni coppia unica di attori all'interno del cast e stabilisce una relazione reciproca di co-protagonismo.
    *   Il `Set` del cast viene convertito in una `List` per l'accesso tramite indice.
    *   Due cicli `for` annidati (con `j = i + 1`) iterano su tutte le coppie uniche `(attore1, attore2)`.
    *   Per ogni coppia, la relazione viene aggiunta in modo simmetrico:
        ```java
        // Aggiunge codice2 alla lista dei co-protagonisti di attore1
        attore1.coprotagonisti.add(codice2);
        // Aggiunge codice1 alla lista dei co-protagonisti di attore2
        attore2.coprotagonisti.add(codice1);
        ```

Al termine del processo, il `currentCast` viene svuotato per accogliere il cast del film successivo.

---

## Parte 2: Ricerca di Cammini Minimi con `cammini.c` (C)

Il programma `cammini.c` utilizza il grafo generato per eseguire ricerche di cammini minimi, basandosi su algoritmi e strutture dati ottimizzate in C.

### 2.1. Implementazione della Coda FIFO per la BFS

L'algoritmo Breadth-First Search (BFS), essenziale per trovare il cammino minimo in un grafo non pesato, richiede una coda FIFO (First-In, First-Out).

#### Strutture Dati Fondamentali

La coda è realizzata come una lista concatenata singola, gestita da due strutture:

*   **`q_node_t`**: Il singolo nodo della coda.
    ```c
    typedef struct q_node {
        int codice;
        struct q_node *next;
    } q_node_t;
    ```
*   **`fifo_queue_t`**: L'involucro che gestisce l'intera coda.
    ```c
    typedef struct {
        q_node_t *head;
        q_node_t *tail;
    } fifo_queue_t;
    ```
    Mantenere un puntatore `tail` è fondamentale per garantire che l'operazione di `enqueue` (inserimento in coda) avvenga in tempo costante **O(1)**.

#### Scelta Progettuale: Minimalismo dei Nodi

Ogni nodo della coda memorizza **esclusivamente l'identificativo intero (`int codice`)** dell'attore. Questa scelta minimalista offre vantaggi significativi:
*   **Efficienza di Memoria**: Si gestiscono solo interi, riducendo drasticamente l'occupazione di memoria rispetto a memorizzare intere strutture `attore`.
*   **Performance**: Le operazioni di `enqueue` e `dequeue` sono estremamente veloci, poiché manipolano solo un intero e un puntatore.
*   **Separazione delle Competenze**: La coda gestisce solo l'ordine di visita. Informazioni aggiuntive (come il predecessore nel cammino) sono demandate a una struttura di supporto, l'Albero Binario di Ricerca.

### 2.2. Ricostruzione del Cammino Minimo tramite Albero di Ricerca

La BFS trova la destinazione, ma non fornisce direttamente il percorso. Questo viene ricostruito a ritroso grazie a informazioni salvate durante l'esplorazione.

#### Struttura Dati di Supporto: l'Albero Binario di Ricerca (ABR)

Durante la BFS, ogni nodo visitato viene inserito in un ABR. Il nodo dell'albero è progettato per memorizzare il predecessore nel cammino.

```c
typedef struct abr_node {
    int shuffled_codice; // Chiave dell'albero (per bilanciamento)
    int original_codice; // Codice attore originale
    int parent_codice;   // Codice del predecessore nel cammino BFS
    struct abr_node *left;
    struct abr_node *right;
} abr_node_t;
```
Il campo chiave è **`parent_codice`**. Quando la BFS esplora un `neighbor_codice` da un `current_codice`, memorizza questa relazione nell'ABR, creando un'associazione `(figlio -> genitore)` che permette di risalire il percorso. Il nodo di partenza ha un `parent_codice` speciale (`-1`) che funge da terminatore.

#### Processo di Ricostruzione Passo-Passo

Una volta che la BFS trova la destinazione, parte il processo di backtracking:

1.  **Inizializzazione**: Una variabile `trace_codice` viene impostata sul codice di destinazione.
2.  **Ciclo di Backtracking**: Un ciclo `while (trace_codice != -1)` risale il cammino:
    a. Aggiunge il `trace_codice` corrente a un array che memorizza il percorso.
    b. Cerca il nodo corrente nell'ABR.
    c. Aggiorna `trace_codice` con il `parent_codice` trovato nel nodo dell'ABR. Questo è il passo fondamentale che fa "risalire" di un livello.
3.  **Stampa del Risultato**: Al termine del ciclo, l'array del percorso contiene i codici in ordine inverso (destinazione -> partenza). Viene quindi iterato all'indietro per stampare il cammino nell'ordine corretto.

### 2.3. Gestione della Terminazione Controllata (Self-Pipe Trick)

Per gestire la terminazione pulita del programma (es. con `Ctrl+C`) mentre è bloccato su una chiamata di I/O come `select()`, viene implementato il pattern **"self-pipe trick"**.

#### Architettura della Soluzione

Questo pattern trasforma un evento asincrono (un segnale) in un evento di I/O sincrono, che può essere gestito elegantemente dal loop principale.

1.  **Mascheramento del Segnale**: Nel `main`, il segnale `SIGINT` viene mascherato. Questa maschera è ereditata da tutti i thread, che quindi lo ignorano.
2.  **Thread Gestore di Segnali**: Un thread dedicato attende i segnali in modo sincrono usando `sigwait()`. Questa chiamata si sblocca solo quando riceve `SIGINT`.
3.  **Pipe di Comunicazione Interna**: Il `main` crea una pipe anonima.
    *   L'estremo di lettura (`S_SELF_PIPE_FD[0]`) viene aggiunto al set di file descriptor monitorati da `select()`.
    *   L'estremo di scrittura (`S_SELF_PIPE_FD[1]`) è usato dal thread gestore.

#### Flusso di Interruzione

1.  **Segnale Ricevuto**: L'utente preme `Ctrl+C`.
2.  **Attivazione del Gestore**: Il thread gestore si sblocca da `sigwait()`.
3.  **Azione del Gestore**: Il thread scrive un singolo byte nella self-pipe.
    ```c
    char dummy = 'q';
    write(S_SELF_PIPE_FD[1], &dummy, 1);
    ```
4.  **Sblocco di `select()`**: La scrittura sulla pipe rende l'estremo di lettura "pronto". La chiamata `select()` nel `main` si sblocca immediatamente, non per un errore (`EINTR`), ma perché ha rilevato attività su un file descriptor.
5.  **Riconoscimento e Terminazione**: Il `main` rileva attività sull'estremo di lettura della self-pipe, capisce che è un segnale di terminazione, imposta una variabile booleana per uscire dal suo loop `while` e procede con il cleanup controllato delle risorse.



// Import necessari per la gestione di file (lettura/scrittura) e delle collezioni (Liste, Mappe, Set).
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * La classe Attore modella le informazioni essenziali di un attore.
 * Ogni istanza rappresenta un nodo nel nostro grafo.
 */
class Attore {
    // Codice intero, univoco, estratto da 'nconst'.
    int codice;
    // Nome dell'attore.
    String nome;
    // Anno di nascita, usato per il filtraggio iniziale.
    int anno;
    
    /**
     * Un Set per memorizzare i codici dei co-protagonisti.
     * Usiamo un Set (HashSet) perché:
     * 1. Garantisce l'unicità: un co-protagonista non può essere aggiunto più volte.
     * 2. Offre performance eccellenti (O(1) in media) per l'aggiunta e la ricerca,
     *    molto più veloce di una lista.
     */
    Set<Integer> coprotagonisti;
    
    /**
     * Un Set per memorizzare i codici dei titoli a cui l'attore ha partecipato.
     * Valgono le stesse motivazioni di performance e unicità viste per i coprotagonisti.
     */
    Set<Integer> titoliPartecipati;

    /**
     * Costruttore della classe Attore.
     * Inizializza le variabili d'istanza e crea i Set vuoti.
     * @param codice L'ID univoco dell'attore.
     * @param nome Il nome dell'attore.
     * @param anno L'anno di nascita dell'attore.
     */
    public Attore(int codice, String nome, int anno) {
        this.codice = codice;
        this.nome = nome;
        this.anno = anno;
        this.coprotagonisti = new HashSet<>();
        this.titoliPartecipati = new HashSet<>(); 
    }
}

/**
 * Classe principale del programma.
 * Si occupa di leggere i file TSV di IMDB, costruire il grafo degli attori,
 * e salvare i risultati in tre file di testo: nomi.txt, grafo.txt e partecipazioni.txt.
 */
public class CreaGrafo {

        /**
     * Metodo principale eseguito all'avvio del programma.
     * Gestisce dinamicamente 2 o 3 argomenti per compatibilità con lo script di test.
     * @param args Argomenti dalla linea di comando:
     *             - Caso 2 argomenti (test): <file_nomi.tsv> <file_titoli.tsv>
     *             - Caso 3 argomenti (manuale): <name.basics.tsv> <title.principals.tsv> <title.basics.tsv>
     */
    public static void main(String[] args) {
        // Controllo se il numero di argomenti è valido (deve essere 2 o 3).
        if (args.length != 2 && args.length != 3) { 
            // Se non è né 2 né 3, allora è un errore. Mostra un messaggio di aiuto completo.
            System.err.println("Errore: Numero di argomenti non valido.");
            System.err.println("Uso per il TEST AUTOMATICO (2 argomenti): java CreaGrafo <file_nomi.tsv> <file_titoli.tsv>");
            System.err.println("Uso per esecuzione MANUALE (3 argomenti): java CreaGrafo <name.basics.tsv> <title.principals.tsv> <title.basics.tsv>");
            System.exit(1); // Termina il programma con un codice di errore.
        }

        // Dichiarazione delle variabili per i percorsi dei file.
        String pathNameBasics;
        String pathTitlePrincipals;
        
        // Assegnazione dei percorsi dei file a variabili per maggiore leggibilità.
        // Questa parte gestisce la logica per 2 o 3 argomenti.
        if (args.length == 2) {
            System.out.println("INFO: Rilevata esecuzione con 2 argomenti (modalità test).");
            pathNameBasics = args[0];
            pathTitlePrincipals = args[1];
        } else { // In questo ramo, args.length è necessariamente 3.
            System.out.println("INFO: Rilevata esecuzione con 3 argomenti (modalità manuale).");
            pathNameBasics = args[0];
            pathTitlePrincipals = args[1];
            String pathTitleBasics = args[2]; // Questa variabile è usata solo in questa modalità.
            
            // --- PASSO 2 (opzionale): Leggere 'title.basics.tsv' ---
            // Questo passo viene eseguito SOLO se vengono forniti 3 argomenti.
            System.out.println("\nPASSO 2: Elaborazione di " + pathTitleBasics + "...");
            System.out.println("-> File 'title.basics.tsv' scansionato. (Dati non memorizzati per ottimizzare la memoria).");
        }
        
        /**
         * La struttura dati centrale per memorizzare gli attori.
         * Usiamo una Mappa (HashMap) che associa un codice (Integer) a un oggetto Attore.
         * La scelta di HashMap è cruciale per le performance: ci permette di recuperare
         * un attore dato il suo codice in tempo costante (O(1) in media), operazione
         * che faremo milioni di volte durante la lettura di title.principals.tsv.
         */
        Map<Integer, Attore> attori = new HashMap<>();
        
        // Registriamo il tempo di inizio per calcolare la durata totale dell'esecuzione.
        long startTime = System.currentTimeMillis();

        // --- PASSO 1: Leggere il file dei nomi (es. 'name.basics.tsv' o 'miniN.tsv') e filtrare gli attori ---
        System.out.println("PASSO 1: Elaborazione di " + pathNameBasics + " per trovare gli attori...");
        try (BufferedReader br = new BufferedReader(new FileReader(pathNameBasics))) {
            String line;
            br.readLine(); // Salta la prima riga che contiene l'intestazione del file.
            
            while ((line = br.readLine()) != null) {
                // Il file è TSV (Tab-Separated Values), quindi dividiamo la riga usando il carattere '\t'.
                String[] fields = line.split("\t");
                if (fields.length < 5) continue; // Salta righe malformate.

                // Estrazione dei campi di interesse secondo le specifiche.
                String nconst = fields[0];          // es. "nm0000001"
                String primaryName = fields[1];     // es. "Fred Astaire"
                String birthYearStr = fields[2];    // es. "1899" o "\N"
                String primaryProfession = fields[4]; // es. "actor,producer"

                // Applichiamo i filtri richiesti dalle specifiche:
                // 1. L'anno di nascita non deve essere "\\N".
                if ("\\N".equals(birthYearStr)) {
                    continue;
                }
                // 2. La professione deve contenere "actor" o "actress".
                //    Controlliamo che la stringa non sia null per sicurezza.
                if (primaryProfession == null || (!primaryProfession.contains("actor") && !primaryProfession.contains("actress"))) {
                    continue;
                }

                try {
                    // Conversione dei dati nei tipi corretti.
                    int codice = Integer.parseInt(nconst.substring(2)); // Rimuove "nm" e converte in intero.
                    int anno = Integer.parseInt(birthYearStr);
                    
                    // Se l'attore passa tutti i filtri, creiamo un nuovo oggetto Attore
                    // e lo inseriamo nella mappa.
                    Attore attore = new Attore(codice, primaryName, anno);
                    attori.put(codice, attore);
                } catch (NumberFormatException | StringIndexOutOfBoundsException e) {
                    // Ignora silenziosamente le righe con formati numerici o di codice non validi.
                }
            }
        } catch (IOException e) {
            System.err.println("Errore critico durante la lettura di " + pathNameBasics + ": " + e.getMessage());
            e.printStackTrace();
            System.exit(1);
        }
        System.out.println("-> Trovati " + attori.size() + " attori che soddisfano i criteri.");


        // --- PASSO 3: Scrivere 'nomi.txt' con l'elenco ordinato degli attori ---
        System.out.println("\nPASSO 3: Scrittura del file nomi.txt...");
        // Per scrivere in ordine di codice, estraiamo tutte le chiavi (codici) dalla mappa,
        // le mettiamo in una lista e ordiniamo la lista.
        List<Integer> codiciAttoriOrdinati = new ArrayList<>(attori.keySet());
        Collections.sort(codiciAttoriOrdinati);

        // Usiamo PrintWriter e BufferedWriter per una scrittura efficiente su file.
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter("nomi.txt")))) {
            for (Integer codice : codiciAttoriOrdinati) {
                Attore attore = attori.get(codice);
                pw.println(attore.codice + "\t" + attore.nome + "\t" + attore.anno);
            }
        } catch (IOException e) {
            System.err.println("Errore critico durante la scrittura di nomi.txt: " + e.getMessage());
            e.printStackTrace();
            System.exit(1);
        }
        System.out.println("-> File nomi.txt scritto correttamente con " + codiciAttoriOrdinati.size() + " righe.");


        // --- PASSO 4: Leggere il file dei titoli (es. 'title.principals.tsv') per costruire il grafo e le partecipazioni ---
        System.out.println("\nPASSO 4: Elaborazione di " + pathTitlePrincipals + " per costruire il grafo...");
        try (BufferedReader br = new BufferedReader(new FileReader(pathTitlePrincipals))) {
            String line;
            br.readLine(); // Salta intestazione.

            // Questa è la logica di "streaming" per elaborare i cast per ogni titolo.
            // Il file `title.principals.tsv` è raggruppato per `tconst`. Sfruttiamo questo.
            String currentTconstRaw = null; // Codice del titolo che stiamo attualmente processando
            Set<Integer> currentCast = new HashSet<>(); // Il cast di attori per il titolo corrente

            while ((line = br.readLine()) != null) {
                String[] fields = line.split("\t");
                if (fields.length < 3) continue;

                String tconstRaw = fields[0]; // es. "tt0000001"
                String nconst = fields[2];    // es. "nm0000123"
                
                try {
                    int codiceAttore = Integer.parseInt(nconst.substring(2));
                    
                    // Processiamo la riga SOLO se l'attore è presente nella nostra mappa di attori filtrati.
                    // Questa verifica è velocissima grazie alla HashMap.
                    if (attori.containsKey(codiceAttore)) {
                        Attore attore = attori.get(codiceAttore);
                        int codiceTitolo = Integer.parseInt(tconstRaw.substring(2));
                        
                        // Aggiungiamo il titolo alla lista delle partecipazioni dell'attore.
                        attore.titoliPartecipati.add(codiceTitolo);

                        // Se stiamo iniziando o se il tconst è cambiato, significa che
                        // abbiamo finito di leggere il cast del titolo precedente.
                        if (!tconstRaw.equals(currentTconstRaw)) {
                            // Se il cast precedente aveva almeno 2 attori, lo elaboriamo.
                            if (currentCast.size() >= 2) {
                                processCast(currentCast, attori);
                            }
                            // Prepariamo per il nuovo titolo.
                            currentTconstRaw = tconstRaw;
                            currentCast.clear();
                        }
                        // Aggiungiamo l'attore corrente al cast del titolo attuale.
                        currentCast.add(codiceAttore);
                    }
                } catch (NumberFormatException | StringIndexOutOfBoundsException e) {
                    // Ignora righe con codici malformati.
                }
            }
            // Alla fine del file, dobbiamo processare l'ultimo cast rimasto in memoria.
            if (currentCast.size() >= 2) {
                processCast(currentCast, attori);
            }
        } catch (IOException e) {
            System.err.println("Errore critico durante la lettura di " + pathTitlePrincipals + ": " + e.getMessage());
            e.printStackTrace();
            System.exit(1);
        }
        System.out.println("-> Relazioni del grafo e partecipazioni costruite con successo.");


        // --- PASSO 5: Scrivere 'grafo.txt' ---
        System.out.println("\nPASSO 5: Scrittura del file grafo.txt...");
        long totalDegreeSum = 0; // Somma dei gradi di tutti i nodi (utile per verifica)

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter("grafo.txt")))) {
            // Usiamo la lista di codici già ordinata in precedenza.
            for (Integer codiceAttore : codiciAttoriOrdinati) {
                Attore attore = attori.get(codiceAttore);
                
                // Per ogni attore, ordiniamo la lista dei suoi co-protagonisti come richiesto.
                List<Integer> coprotagonistiOrdinati = new ArrayList<>(attore.coprotagonisti);
                Collections.sort(coprotagonistiOrdinati);

                pw.print(attore.codice);
                pw.print("\t");
                pw.print(coprotagonistiOrdinati.size());
                totalDegreeSum += coprotagonistiOrdinati.size();

                for (Integer coprotagonistaCodice : coprotagonistiOrdinati) {
                    pw.print("\t");
                    pw.print(coprotagonistaCodice);
                }
                pw.println();
            }
        } catch (IOException e) {
            System.err.println("Errore critico durante la scrittura di grafo.txt: " + e.getMessage());
            e.printStackTrace();
            System.exit(1);
        }
        System.out.println("-> File grafo.txt scritto correttamente.");
        // In un grafo non orientato, il numero di archi è la somma dei gradi diviso 2.
        System.out.println("-> Numero di archi unici nel grafo: " + totalDegreeSum / 2);


        // --- PASSO 6: Scrivere 'partecipazioni.txt' ---
        System.out.println("\nPASSO 6: Scrittura del file partecipazioni.txt...");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter("partecipazioni.txt")))) {
            // Usiamo di nuovo la lista di codici ordinati.
            for (Integer codiceAttore : codiciAttoriOrdinati) {
                Attore attore = attori.get(codiceAttore);
                
                // Ordiniamo la lista dei titoli a cui ha partecipato.
                List<Integer> titoliPartecipatiOrdinati = new ArrayList<>(attore.titoliPartecipati);
                Collections.sort(titoliPartecipatiOrdinati);

                pw.print(attore.codice);
                pw.print("\t");
                pw.print(titoliPartecipatiOrdinati.size());

                for (Integer codiceTitolo : titoliPartecipatiOrdinati) {
                    pw.print("\t");
                    pw.print(codiceTitolo);
                }
                pw.println();
            }
        } catch (IOException e) {
            System.err.println("Errore critico durante la scrittura di partecipazioni.txt: " + e.getMessage());
            e.printStackTrace();
            System.exit(1);
        }
        System.out.println("-> File partecipazioni.txt scritto correttamente.");

        // Calcolo e stampa del tempo totale di esecuzione.
        long endTime = System.currentTimeMillis();
        System.out.println("\nEsecuzione completata in " + (endTime - startTime) / 1000.0 + " secondi.");
    }

    /**
     * Metodo di utilità per processare il cast di un singolo titolo.
     * Dato un insieme di codici di attori che hanno lavorato insieme, crea un arco
     * (una relazione di co-protagonismo) tra ogni possibile coppia di attori.
     * 
     * @param cast Set di codici degli attori che formano il cast di un titolo.
     * @param attoriMap La mappa principale che contiene tutti gli oggetti Attore.
     */
    private static void processCast(Set<Integer> cast, Map<Integer, Attore> attoriMap) {
        // Se ci sono meno di 2 attori del nostro elenco nel cast, non ci sono coppie da creare.
        if (cast.size() < 2) {
            return; 
        }
        
        // Convertiamo il Set in una Lista per poter accedere agli elementi tramite indice.
        // Questo ci permette di creare le coppie in modo efficiente senza duplicati.
        List<Integer> castList = new ArrayList<>(cast);
        
        // Usiamo due cicli annidati per creare ogni coppia unica (a1, a2).
        // Il secondo ciclo parte da 'i + 1' per evitare di accoppiare un attore con se stesso
        // e per evitare coppie duplicate (es. se processiamo (a1, a2), non processeremo (a2, a1)).
        for (int i = 0; i < castList.size(); i++) {
            for (int j = i + 1; j < castList.size(); j++) {
                Integer codice1 = castList.get(i);
                Integer codice2 = castList.get(j);

                Attore attore1 = attoriMap.get(codice1);
                Attore attore2 = attoriMap.get(codice2);

                // Poiché il grafo non è orientato, aggiungiamo la relazione in entrambe le direzioni.
                attore1.coprotagonisti.add(codice2);
                attore2.coprotagonisti.add(codice1);
            }
        }
    }
}

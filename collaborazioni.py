#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
from collections import defaultdict

def carica_titoli(percorso_file):
    """
    Carica i titoli dal file TSV.
    Crea un dizionario che mappa l'ID NUMERICO (senza zeri iniziali)
    al nome del titolo, per essere compatibile con partecipazioni.txt.

    Args:
        percorso_file (str): Il percorso del file title.basics.tsv.

    Returns:
        dict: Un dizionario che mappa l'ID numerico (stringa) al nome del titolo.
    """
    titoli = {}
    print(f"INFO: Inizio lettura del file dei titoli: {percorso_file}")
    try:
        with open(percorso_file, 'r', encoding='utf-8') as f:
            next(f)  # Salta la riga dell'intestazione (header)
            for linea in f:
                campi = linea.strip().split('\t')
                if len(campi) > 2:
                    tconst = campi[0]          # es. "tt0061452"
                    primary_title = campi[2]   # es. "The Conversation"
                    
                    if tconst.startswith('tt'):
                        try:
                            # ESTRAE la parte numerica come stringa ("0061452")
                            id_numerico_con_zeri = tconst[2:]
                            # CONVERTE in intero (61452) per eliminare gli zeri...
                            id_come_int = int(id_numerico_con_zeri)
                            # ...E RICONVERTE in stringa ("61452") per usarla come chiave.
                            id_pulito_str = str(id_come_int)
                            
                            titoli[id_pulito_str] = primary_title
                        except ValueError:
                            # Ignora ID non numerici, per sicurezza
                            continue
                        
    except FileNotFoundError:
        print(f"ERRORE: File non trovato: {percorso_file}", file=sys.stderr)
        return None
    except Exception as e:
        print(f"ERRORE durante la lettura del file titoli: {e}", file=sys.stderr)
        return None
    
    print(f"INFO: Caricati {len(titoli)} titoli.")
    return titoli

def carica_partecipazioni(percorso_file):
    """
    Carica le partecipazioni dal file generato da CreaGrafo.java.
    Il formato atteso per ogni riga è: <id_attore> <num_titoli> <id_titolo1> ...
    Crea un dizionario che mappa ogni attore (ID stringa) all'insieme (set)
    degli ID dei titoli (stringhe) a cui ha partecipato.

    Args:
        percorso_file (str): Il percorso del file partecipazioni.txt.

    Returns:
        dict: Dizionario ID_attore -> {set di ID_titoli}.
    """
    partecipazioni_attore = {}
    print(f"INFO: Inizio lettura del file delle partecipazioni: {percorso_file}")
    try:
        with open(percorso_file, 'r', encoding='utf-8') as f:
            for linea in f:
                campi = linea.strip().split()
                if len(campi) < 2:
                    continue  # Salta righe vuote o malformate

                # Il primo campo è l'ID dell'attore
                id_attore = campi[0]
                # I campi dal terzo in poi sono gli ID dei titoli
                # campi[1] è il numero di titoli, che possiamo ignorare qui.
                id_titoli = campi[2:]

                # Aggiungiamo l'attore al dizionario con l'insieme dei suoi titoli.
                # Usare un set è efficiente e gestisce eventuali duplicati.
                partecipazioni_attore[id_attore] = set(id_titoli)

    except FileNotFoundError:
        print(f"ERRORE: File non trovato: {percorso_file}", file=sys.stderr)
        return None
    except Exception as e:
        print(f"ERRORE durante la lettura del file partecipazioni: {e}", file=sys.stderr)
        return None

    print(f"INFO: Caricate partecipazioni per {len(partecipazioni_attore)} attori.")
    return partecipazioni_attore

def main():
    """
    Funzione principale del programma.
    """
    if len(sys.argv) < 5:
        print("Uso: python3 collaborazioni.py <file_partecipazioni> <file_titoli> <id_attore1> <id_attore2> ...", file=sys.stderr)
        sys.exit(1)

    file_partecipazioni = sys.argv[1]
    file_titoli = sys.argv[2]
    id_attori = sys.argv[3:]

    # Caricamento dati con le funzioni corrette per il formato dei tuoi file
    mappa_titoli = carica_titoli(file_titoli)
    if mappa_titoli is None:
        sys.exit(1)

    partecipazioni = carica_partecipazioni(file_partecipazioni)
    if partecipazioni is None:
        sys.exit(1)

    print("-" * 20)

    # Elaborazione delle coppie di attori
    for i in range(len(id_attori) - 1):
        attore1_id = id_attori[i]
        attore2_id = id_attori[i+1]

        # Gli ID attori sono già stringhe numeriche, non serve alcuna conversione.
        titoli_attore1 = partecipazioni.get(attore1_id, set())
        titoli_attore2 = partecipazioni.get(attore2_id, set())

        # L'intersezione sui set è il modo più efficiente per trovare le collaborazioni
        collaborazioni_ids = titoli_attore1.intersection(titoli_attore2)

        # Stampa dei risultati nel formato richiesto
        if not collaborazioni_ids:
            print(f"{attore1_id}.{attore2_id} nessuna collaborazione\n")
        else:
            num_collaborazioni = len(collaborazioni_ids)
            print(f"{attore1_id}.{attore2_id}: {num_collaborazioni} collaborazioni:")
            # Ordina gli ID per un output consistente
            # Li ordiniamo numericamente per correttezza
            for titolo_id in sorted(list(collaborazioni_ids), key=int):
                # Ricerca O(1) nel dizionario dei titoli
                nome_titolo = mappa_titoli.get(titolo_id, "Titolo Sconosciuto")
                print(f" {titolo_id} {nome_titolo}")
            print() 

    print("=== Fine")

if __name__ == "__main__":
    main()

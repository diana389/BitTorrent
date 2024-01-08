# Protocolul BitTorrent (tema3 - APD)

Programul consta in implementarea unui tracker si a unor clienti cu 3 posibile roluri: seed, peer, leecher.

## ***TRACKER:***

- Detine un vector de informatii pentru fisiere, cu rol de baza de date.
- Structura fiecarui fisier (`file_info`), contine numele fisierului si un vector de segmente.
- Structura unui segment contine hash-ul, indexul segamentului in fisier si clientii care il detin.

### add_files_to_tracker_database
- Initial, trackerul primeste informatii despre fisiere de la clienti si le completeaza in baza de date, trimitand apoi un mesaj de confirmare catre clienti (`FILES OK`).

###
- Cat timp programul ruleaza, tracker-ul poate primi cereri de tipul `OWNERS REQ`, urmate de numele unui fisier, la care raspunde prin a trimite clientului informatii: numarul de segmente, clientii care detin fiecare segment, si indexul segmentului in fisier.
- In cazul in care primeste un mesaj de tip `READY`, inseamna ca un client si-a terminat de completat fisierele si il marcheaza in vector.
- Daca toti clientii au terminat, trimite mesajul `DONE` catre toate thread-urile de upload, pentru a le anunta ca isi pot termina executia.

## ***PEER:***

- Detine cate un vector de informatii pentru fisierele detinute si pentru cele dorite.
- Structura `file_requested` contine numele fisierului, segmentele primite si numarul lor.
- Fisierele detinute sunt de tipul `file_info`, descris mai sus. Similar pentru segmente.
- Citeste din fisierul sau informatiile despre ce segmente detine si pe care le doreste si le trimite catre tracker.
- Dupa primirea mesajului de confirmare, se creeaza doua thread-uri: `download_thread` si `upload_thread`.

### **upload_thread**
- Ruleaza pana la primirea mesajului `DONE` de la tracker.
- Poate primi cereri de tipul `CHUNK REQ`, alaturi de numele fisierului si indexul segmentului, in urma carora trimite segmentul cerut clientului.

### **download_thread**
- Cat timp mai sunt fisiere incomplete, se cer pentru fiecare segmentele lipsa.
- Daca toate fisierele au fost create, se trimite catre tracker mesajul `READY`.

### manage_file_requested
- Functie care se ocupa de cererea informatiilor despre un fisier de la tracker (`request_file_info_from_tracker`), cererea hash-ului pentru fiecare segment (`request_chunk_from_peer) si crearea fisierului de output daca s-au obtinut toate informatiile(`create_client_file`).

### request_file_info_from_tracker
- Trimite tracker-ului o cerere `OWNERS REQ`, urmata de numele fisierului.
- Primeste inapoi informatii despre fiecare segment: clientii care il detin si indexul.

### request_chunk_from_peer
- Alege clientul de la care sa se ceara segemntul.
- Trimite mesajul `CHUNK REQ`, urmat de numele fisierului si de index.
- Primeste hash-ul si completeaza datele in structura.

### create_client_file
- Daca inca nu exista, creeaza fisierul, altfel doar adauga hash-ul curent.
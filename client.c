#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>

#define BUFFER_SIZE 1024 // Grandezza massima del buffer

int client_socket; // Socket del client

//Handler che gestisce in modo opportuno la chiusura improvvisa del client (CRTL+C o chiusura terminale)
void handle_sigint(int sig);
void run_client(int port);

int main(int argc, char *argv[]) {
    // Controlla che il numero di argomenti sia corretto
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Converti l'argomento porta in un numero intero
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Errore: Porta non valida.\n");
        exit(EXIT_FAILURE);
    }

    run_client(port);
    return 0;
}

// Funzione che chiude correttamente il socket prima di terminare il programma da terminale o con CRTL+C
void handle_sigint(int sig){
    printf("\nChiusura da terminale del client!\n\n");

    close(client_socket);
    exit(0);
}

void run_client(int port){

    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    // Gestione handler per segnali di chiusura terminale o CRTL+C
    signal(SIGINT, handle_sigint);
    signal(SIGHUP,handle_sigint);

    // Creazione del socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Errore creazione socket");
        exit(EXIT_FAILURE);
    }

    // Configurazione dell'indirizzo del server
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // Indirizzo IP del server (modifica con l'indirizzo effettivo del server)
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("Errore conversione indirizzo IP");
        close(client_socket);
        exit(EXIT_FAILURE);
    }
    
    // Stampa a video del Client il primo messaggio per decidere se iniziare il quiz
    printf("Trivia Quiz\n+++++++++++++++++++++++++++++++++++++++\nMenù:\n1 - Comincia una sessione di Trivia\n2 - Esci\n+++++++++++++++++++++++++++++++++++++++\nLa tua scelta: ");
    memset(buffer, 0, BUFFER_SIZE);
    fgets(buffer, BUFFER_SIZE, stdin);
    if(strcmp("2\n",buffer)==0){ // Risposta = 2, termino client
        printf("\nEsecuzione Client Terminata\n");
        return ;
    }
    else if(strcmp("1\n",buffer)!=0){ // Caso in cui si inserisca qualcosa diverso da 1 o 2 = termino client
        printf("\nEsecuzione Client Terminata\n");
        return ;
    }
    else{// Risposta = 1
        // Connessione al server
        if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("Errore connessione al server");
            close(client_socket);
            exit(EXIT_FAILURE);
        }
    }

    printf("\n");
   
    // Loop principale per ricevere domande e inviare risposte
    while (1) {
        // Ricezione Messaggio dal Server

        // Lettura numero byte del messaggio da ricevere
        int message_lenght;
        int net_message_lenght;
        int bytes_to_read = read(client_socket, &message_lenght, sizeof(message_lenght));
        
        if (bytes_to_read <= 0) {
            printf("Connessione chiusa dal server.\n");
            break;
        }

        memset(buffer, 0, BUFFER_SIZE);
        net_message_lenght = ntohl(message_lenght);

        // Lettura messaggio 
        int bytes_read = read(client_socket, &buffer, net_message_lenght);
        if (bytes_read <= 0) {
            printf("Connessione chiusa dal server.\n");
            break;
        }
        
        if(strcmp(buffer,"endquiz")==0){
            printf("\n\n");
            close(client_socket);
            run_client(port);
            return ;
        }

        buffer[bytes_read] = '\0'; // Terminazione stringa

        // Stampa il messaggio ricevuto (es. domanda o messaggi vari)
        printf("%s", buffer);

        // Invio risposta al server
        do {
            memset(buffer, 0, BUFFER_SIZE);
            fgets(buffer, BUFFER_SIZE, stdin);
        }
        while(strcmp("\n",buffer)==0);

        // Rimuove newline dalla risposta
        buffer[strcspn(buffer, "\r\n")] = '\0';

        // Invio numero di byte da inviare
        message_lenght = strlen(buffer);
        net_message_lenght = htonl(message_lenght); // Conversione a network

        if (send(client_socket,&net_message_lenght,sizeof(net_message_lenght),0) <=0) {
            perror("Errore invio numero di byte della risposta");
            break;
        }

        // Invio messaggio
        if (send(client_socket, buffer, strlen(buffer), 0) <=0) {
            perror("Errore invio risposta");
            break;
        }

    }

    // Chiusura del socket
    close(client_socket);
    printf("Connessione terminata.\n");

    return;
}


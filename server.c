#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include <ctype.h>

#define MAX_CLIENTS 100 // Numero massimo di clienti attivi in uno stesso momento
#define PORT 12345 // Porta a cui i client si connettono
#define BUFFER_SIZE 1024 // Grandezza massima del buffer
#define MAX_QUESTIONS 5 // NUmero massimo di domande/risposte per i quiz
#define MAX_LENGTH 256 // Numero massimo di caratteri per ciascuna domanda/risposta

// Tipo enum che descrive in che stato si trova un client tra i seguenti
typedef enum{
    nickname, // Stato in cui dal client ci si aspetta di ricevere un nickname univoco
    sceltaquiz, // Stato in cui dal client ci si aspetta di ricevere 1 o 2 per la scelta del tipo di quiz
    game, // Stato in cui dal client ci si aspetta di ricevere risposte alle domande
    quiz_terminato // Stato in cui il client ha finito il quiz
} clientstate;

// Struttura dedicata a contenere le informazioni di un client relative solamente ai punteggi per le classifiche
typedef struct Client_points{
    char name[50];    //nickname univoco
    int score;  //punteggio di risposte corrette per quiz
    int terminated_quiz; // indica se è stato terminato il quiz
    struct Client_points * prec;
    struct Client_points * next;
} Client_points;

// Struttura dedicata a contenere le informazioni di un client 
typedef struct Client{
    int socket; // Numero del socket del client
    clientstate state;
    char name[50];    // Nickname univoco
    int score;  // Punteggio di risposte corrette per il quiz attivo in quel momento
    int type_quiz; // Indica il tipo di quiz ( 1 = cultura generale, 2 = cultura informatica )
    int current_question; // Indice della domanda corrente per il client
    struct Client* next; // Puntatore al prossimo client della lista
    struct Client_points* node1; // Puntatore al nodo corrispondete della classifica del tema 1
    struct Client_points* node2; // Puntatore al nodo corrispondete della classifica del tema 2
} Client;

//dichiarazioni variabili globali
char message1[40] = "Curiosità sulla tecnologia"; // Nome quiz 1 
char message2[40] = "Cultura Generale"; // Nome quiz 2 
int active_clients = 0; // Numero clients con cui il server ha stabilito una connessione
int active_clients_registered = 0; // Numero clients effettivamenti registrati con Nickname
Client* head_clients = NULL;  // Head della lista di descrittori client
Client_points* head_classifica1 = NULL; // Head classifica tema 1
Client_points* head_classifica2 = NULL; // Head classifica tema 2
int server_socket = 0; // Dichiarazione socket del server


// Dichiarazione delle funzioni
// I commenti sulle informazioni di ciascuna è stato fatto subito prima della definizione di ciascuna
void handle_new_connection(int server_socket, fd_set *read_fds, int *max_fd);
void handle_client_message(Client* client, fd_set *read_fds, char questions1[][MAX_LENGTH], char questions2[][MAX_LENGTH],char answers1[][MAX_LENGTH], char answers2[][MAX_LENGTH],int question_count);
int load_questions_and_answers(const char *question_file, char questions1[][MAX_LENGTH], char questions2[][MAX_LENGTH]);
int check_answer(const char *client_answer, const char *correct_answer);
void showscore(Client* client,char questions[][MAX_LENGTH]);
void endquiz(Client* target,fd_set *read_fds);
void outputserver();
char (*get_set(int choice, char questions1[MAX_QUESTIONS][MAX_LENGTH], char questions2[MAX_QUESTIONS][MAX_LENGTH]))[MAX_LENGTH];
Client* crea_nodo(int socket);
void elimina_client(Client* target);
Client_points* crea_nodo_classifica(const char* name,Client_points** head); 
void riordina_classifica(Client_points** head, Client_points* target);
void elimina_client_classifica(Client_points** head, Client_points* target);
char* remove_spaces(char * stringa);
void handle_sigint(int sig);


int main() {
    // Funzioni per collegare la mia funzione handle_sigint in risposta ai segnali di chiusura da terminale o CRLT+C
    signal(SIGINT, handle_sigint);
    signal(SIGHUP,handle_sigint);

    //Strutture per i socket e i set 
    int max_fd, activity, question_count;
    struct sockaddr_in server_addr;
    fd_set read_fds;

    // Array per le domande e le risposte
    char questions1[MAX_QUESTIONS][MAX_LENGTH];
    char questions2[MAX_QUESTIONS][MAX_LENGTH];
    char answers1[MAX_QUESTIONS][MAX_LENGTH];
    char answers2[MAX_QUESTIONS][MAX_LENGTH];

    // Carica domande e risposte dai file
    question_count = load_questions_and_answers("domande.txt", questions1, questions2);
    if (question_count < 0) {
        fprintf(stderr, "Errore nel caricamento delle domande o risposte.\n");
        return EXIT_FAILURE;
    }
    printf("Caricate %d domande\n", question_count);
    
    question_count = 0;
    question_count = load_questions_and_answers("risposte.txt", answers1, answers2);
    if (question_count < 0) {
        fprintf(stderr, "Errore nel caricamento delle domande o risposte.\n");
        return EXIT_FAILURE;
    }
    printf("Caricate %d risposte\n", question_count);

    question_count = 5;

    // Creazione del socket server
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("\nErrore creazione socket");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // set del socket in modalità REUSE, per poter riavviare il server subito dopo averlo chiuso
    // settaggio fatto per motivi di testing, in questo modo è più veloce il riavvio del server
    int  opt = 1;
    if(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR,&opt,sizeof(opt)) < 0){
        perror("\nErrore in setsockopt");
        exit(EXIT_FAILURE);
    }

    // Binding del socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("\nErrore bind");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Socket in ascolto
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        perror("\nErrore listen");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server in ascolto sulla porta %d...\n\n", PORT);

    FD_ZERO(&read_fds); // Pulisce il set di socket
    FD_SET(server_socket, &read_fds); // Aggiunge il socket del server al set
    max_fd = server_socket;

    outputserver();

    while (1) {

        fd_set copy_fds = read_fds;

        activity = select(max_fd + 1, &copy_fds, NULL, NULL, NULL); // Usa select per vedere se ci sono attività
        if (activity < 0 && errno != EINTR) {
            perror("\nErrore select");
            break;
        }

        if (FD_ISSET(server_socket, &copy_fds)) { // Se ci sono nuove connessioni si entra nell'if e si gestiscono
            handle_new_connection(server_socket, &read_fds, &max_fd);
        }

        // Altrimenti scorro i client già registrati e controllo se ci sono nuovi messaggi da ciascuno di essi
        Client* temp = head_clients;
        while (temp!=NULL){
            if(temp->socket!=0 && FD_ISSET(temp->socket,&copy_fds)){
                handle_client_message(temp, &read_fds, questions1,questions2,answers1,answers2, question_count);
            }
            temp = temp->next;
        }
    }
    
    close(server_socket);
    return 0;
}


// Funzioni:


// Gestisce le connessioni di nuovi client
void handle_new_connection(int server_socket, fd_set *read_fds, int *max_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int new_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

    if (new_socket < 0) {
        perror("\nErrore accept");
        return;
    }

    if (active_clients<MAX_CLIENTS) {
        // Creazione nuovo client e inserimento nella lista
        crea_nodo(new_socket);
        // Inserimento del nuovo socket nel set
        FD_SET(new_socket, read_fds);
        if (new_socket > *max_fd) *max_fd = new_socket;
        // Invio primo messaggio di scelta nickname
        char welcome_message[BUFFER_SIZE];
        active_clients++;
        snprintf(welcome_message, sizeof(welcome_message),"\nTrivia Quiz\n+++++++++++++++++++++++++++++++++++++++\nScegli un nickname (deve essere univoco):\n");
        
        // Invio numero byte da inviare
        int message_lenght = strlen(welcome_message);
        int net_message_lenght = htonl(message_lenght); //conversione a network
        send(new_socket,&net_message_lenght,sizeof(net_message_lenght),0);

        // Invio messaggio
        send(new_socket, welcome_message, message_lenght, 0);
        return;

    }

    // Troppi client connessi, invio un messaggio al client e poi chiudo il socket
    char bye_message[BUFFER_SIZE];
    snprintf(bye_message, sizeof(bye_message),"\nErrore! Troppi client connessi, riprova più tardi!\n");

    // Invio numero byte da inviare
    int message_lenght = strlen(bye_message);
    int net_message_lenght = htonl(message_lenght); //conversione a network
    send(new_socket, &net_message_lenght,sizeof(net_message_lenght),0);

    // Invio messaggio
    send(new_socket, bye_message, message_lenght, 0);
    return;

    close(new_socket);
}

// Funzione che restituisce il titolo del quiz corretto a seconda del parametro type
char* typemessage(int type){
    if(type == 1){
        return message1;
    }
    else if(type == 2){
        return message2;
    }

    return NULL;
}

// Funzione che gestisce un nuovo messaggio di un cliente già registrato
void handle_client_message(Client* client, fd_set *read_fds, char questions1[][MAX_LENGTH], char questions2[][MAX_LENGTH],char answers1[][MAX_LENGTH], char answers2[][MAX_LENGTH],int question_count){
    char buffer[BUFFER_SIZE];
    int message_lenght;
    int net_message_lenght;

    // Lettura del numero di byte da leggere
    int bytes_to_read = read(client->socket, &message_lenght, sizeof(message_lenght));

    if (bytes_to_read <= 0) { //errore nella ricezione del numero di byte da leggere
        active_clients--;
        if(client->state!=nickname){
            active_clients_registered--;
        }
        // Il client ha chiuso la connessione

        // Chiusura socket
        printf("Disconnessione: %s (socket %d)\n", client->name, client->socket);
        close(client->socket);
        FD_CLR(client->socket, read_fds);

        // Eliminazione del nodo dalle eventuali classifiche in cui era
        if(client->node1!=NULL){
            elimina_client_classifica(&head_classifica1,client->node1);
        }
        if(client->node2!=NULL){
            elimina_client_classifica(&head_classifica2,client->node2);
        }

        // Eliminazione del nodo dalla lista di client
        elimina_client(client);
        outputserver();

        return;
    }


    // Numero di byte valido
    net_message_lenght = ntohl(message_lenght);
    // Lettura messaggio 
    int bytes_read = read(client->socket, &buffer, net_message_lenght);
    
    if (bytes_read <= 0) { //errore nella ricezione del messaggio
        active_clients--;
        if(client->state!=nickname){
            active_clients_registered--;
        }
        // Il client ha chiuso la connessione

        // Chiusura socket
        printf("Disconnessione: %s (socket %d)\n", client->name, client->socket);
        close(client->socket);
        FD_CLR(client->socket, read_fds);

        // Eliminazione del nodo dalle eventuali classifiche in cui era
        if(client->node1!=NULL){
            elimina_client_classifica(&head_classifica1,client->node1);
        }
        if(client->node2!=NULL){
            elimina_client_classifica(&head_classifica2,client->node2);
        }

        // Eliminazione del nodo dalla lista di client
        elimina_client(client);
        outputserver();

        return;
    }

    buffer[bytes_read] = '\0';
    buffer[strcspn(buffer, "\r\n")] = '\0'; // Rimuovi newline


    // Controllo lo stato del client e a seconda di questo, interpreto la risposta arrivata dal client

    if(client->state == nickname){
        bool univoco=true;
        Client* temp = head_clients;
        // Rimuovo eventuali spazi nel nome

        char* nickname_clear = remove_spaces(buffer);

        while(temp!=NULL){
            if(strcmp(nickname_clear,temp->name)==0){
                // Ho trovato già un nickname di un client già presente uguale a quello nuovo
                univoco = false;
            }
            temp = temp->next;
        }

        if(!univoco || strcmp(nickname_clear,"\0")==0){
            // Nickname da richiedere nuovamente
            char welcome_message[BUFFER_SIZE];
            snprintf(welcome_message, sizeof(welcome_message),"\nNickname già utilizzato!\nTrivia Quiz\n+++++++++++++++++++++++++++++++++++++++\nScegli un nickname (deve essere univoco):\n");
            
            // Invio numero byte da inviare
            message_lenght = strlen(welcome_message);
            net_message_lenght = htonl(message_lenght); //conversione a network
            send(client->socket, &net_message_lenght,sizeof(net_message_lenght),0);

            // Invio messaggio
            send(client->socket, welcome_message, message_lenght, 0);
            return;
        }


        strcpy(client->name,nickname_clear);
        // Nickname nuovo registrato
        active_clients_registered++;
        client->state = sceltaquiz;
        outputserver();
        char message[BUFFER_SIZE];
        snprintf(message, sizeof(message),"\nQuiz Disponibili\n+++++++++++++++++++++++++++++++++++++++\n1 - %s\n2 - %s\n+++++++++++++++++++++++++++++++++++++++\nLa tua scelta:",message1,message2);
        
        // Invio numero byte da inviare
        message_lenght = strlen(message);
        net_message_lenght = htonl(message_lenght); //conversione a network
        send(client->socket, &net_message_lenght,sizeof(net_message_lenght),0);

        // Invio messaggio
        send(client->socket, message, message_lenght, 0);
        return;
    }
    else if(client->state == sceltaquiz){
        if(strcmp(buffer,"1")==0){
            // Scelta del quiz 1
            client->type_quiz = 1;
            client->state = game;

            // Inserimento nella classifica del tema 1
            if(client->node1!=NULL){
                elimina_client_classifica(&head_classifica1,client->node1);
            }
            client->node1 = crea_nodo_classifica(client->name,&head_classifica1);
            
            outputserver();
            // Faccio la prima domanda del tema 1
            char message[BUFFER_SIZE];
            snprintf(message, sizeof(message),"\nQuiz - %s\n+++++++++++++++++++++++++++++++++++++++\n%s\n",message1,questions1[client->current_question]);
            
            // Invio numero byte da inviare
            message_lenght = strlen(message);
            net_message_lenght = htonl(message_lenght); //conversione a network
            send(client->socket, &net_message_lenght,sizeof(net_message_lenght),0);

            // Invio messaggio
            send(client->socket, message, message_lenght, 0);
            return;
        }
        if(strcmp(buffer,"2")==0){
            // Scelta del quiz 2
            client->type_quiz = 2;
            client->state = game;

            // Inserimento nella classifica del tema 2
            if(client->node2!=NULL){
                elimina_client_classifica(&head_classifica2,client->node2);
            }
            client->node2 = crea_nodo_classifica(client->name,&head_classifica2);

            outputserver();
            // Faccio la prima domanda del tema 2
            char message[BUFFER_SIZE];
            snprintf(message, sizeof(message),"\nQuiz - %s\n+++++++++++++++++++++++++++++++++++++++\n%s\n Risposta:",message2,questions2[client->current_question]);
            // Invio numero byte da inviare
            message_lenght = strlen(message);
            net_message_lenght = htonl(message_lenght); //conversione a network
            send(client->socket, &net_message_lenght,sizeof(net_message_lenght),0);

            // Invio messaggio
            send(client->socket, message, message_lenght, 0);
            return;
        }
        else if(strcmp(buffer,"endquiz")==0){
            endquiz(client,read_fds);
            return;
        }
        // Risposta non valida, ripeto la richiesta di scelta quiz
        char message[BUFFER_SIZE];
        snprintf(message, sizeof(message),"\nRisposta non valida!\nQuiz Disponibili\n+++++++++++++++++++++++++++++++++++++++\n1 - %s\n2 - %s\n+++++++++++++++++++++++++++++++++++++++\nLa tua scelta:",message1,message2);
        
        // Invio numero byte da inviare
        message_lenght = strlen(message);
        net_message_lenght = htonl(message_lenght); //conversione a network
        send(client->socket, &net_message_lenght,sizeof(net_message_lenght),0);

        // Invio messaggio
        send(client->socket, message, message_lenght, 0);
    }
    else if(client->state == game){
        // Caricamento delle domande e risposte del quiz scelto
        
        // Domande
        char questions[MAX_QUESTIONS][MAX_LENGTH] = {0};
        char (*p_questions)[MAX_LENGTH] = get_set(client->type_quiz,questions1,questions2);
        for(int i=0;i < MAX_QUESTIONS;i++){
            strcpy(questions[i],p_questions[i]);
        }

        // Risposte
        char answers[MAX_QUESTIONS][MAX_LENGTH] = {0};
        char (*p_answers)[MAX_LENGTH] = get_set(client->type_quiz,answers1,answers2);
        for(int i=0;i < MAX_QUESTIONS;i++){
            strcpy(answers[i],p_answers[i]);
        }

        int current_question = client->current_question;

        char questioncheck[22];

        // Controllo di eventuali risposte con comandi endquiz o show score
        if(strcmp(buffer,"show score")==0){
            showscore(client,questions);
            return;

        }
        else if(strcmp(buffer,"endquiz")==0){
            endquiz(client,read_fds);
            return;
        }else{
            if (check_answer(buffer,answers[current_question])) { // Controlla se la risposta è giusta
                client->score++;
                strcpy(questioncheck,"Risposta Corretta!\n");


                // Aggiornamento lista clients in modo che rimanga ordinata secondo lo score
                if(client->type_quiz==1){ // Quiz 1
                    if(client->node1!=NULL){
                        client->node1->score++;
                        riordina_classifica(&head_classifica1,client->node1);
                    }
                }
                else{ // Quiz 2
                    if(client->node2!=NULL){
                        client->node2->score++;
                        riordina_classifica(&head_classifica2,client->node2);
                    }
                }

            } else {
                strcpy(questioncheck,"Risposta Errata!\n");
            }
            client->current_question++;
        }
        outputserver();
        // Controllo se ha finito il quiz
        if (client->current_question < question_count) {
            snprintf(buffer, sizeof(buffer), "\n%s\nQuiz - %s\n+++++++++++++++++++++++++++++++++++++++\n%s\nRisposta:",questioncheck, typemessage(client->type_quiz),questions[client->current_question]);
            
            // Invio numero byte da inviare
            message_lenght = strlen(buffer);
            net_message_lenght = htonl(message_lenght); //conversione a network
            send(client->socket, &net_message_lenght,sizeof(net_message_lenght),0);

            // Invio messaggio
            send(client->socket, buffer, message_lenght, 0);
            return;
        } else {

            // Quiz terminato
            client->state = quiz_terminato;
            if(client->type_quiz==1){
                client->node1->terminated_quiz = 1;
            }
            else{
                client->node2->terminated_quiz = 1;
            }

            outputserver();
            snprintf(buffer, sizeof(buffer), "\n%s\nQuiz terminato! Grazie per aver giocato.\n\nPer tornare alla scelta del quiz scrivi 'restart'\nPer mostrare i punteggi scrivi 'show score'\nPer terminare il quiz scrivi 'endquiz'\nLa tua risposta:",questioncheck);
            
            // Invio numero byte da inviare
            message_lenght = strlen(buffer);
            net_message_lenght = htonl(message_lenght); //conversione a network
            send(client->socket, &net_message_lenght,sizeof(net_message_lenght),0);

            // Invio messaggio
            send(client->socket, buffer, message_lenght, 0);
            return;
        }
    }
    else if(client->state == quiz_terminato){
        
        if(strcmp(buffer,"show score")==0){
            //caricamento domande del tema scelto
            char questions[MAX_QUESTIONS][MAX_LENGTH] = {0};
            char (*p_questions)[MAX_LENGTH] = get_set(client->type_quiz,questions1,questions2);
            for(int i=0;i < MAX_QUESTIONS;i++){
                strcpy(questions[i],p_questions[i]);
            }

            showscore(client,questions);
            return;

        }
        else if(strcmp(buffer,"endquiz")==0){
            endquiz(client,read_fds);
            return;
        }
        else if(strcmp(buffer,"restart")==0){
            // Resetto le info attive del client poichè sta per iniziare un nuovo quiz
            client->state = sceltaquiz;
            client->current_question = 0;
            client->score = 0;
            outputserver();
            char message[BUFFER_SIZE];
            snprintf(message, sizeof(message),"\nQuiz Disponibili\n+++++++++++++++++++++++++++++++++++++++\n1 - %s\n2 - %s\n+++++++++++++++++++++++++++++++++++++++\nLa tua scelta:",message1,message2);
            
            // Invio numero byte da inviare
            message_lenght = strlen(message);
            net_message_lenght = htonl(message_lenght); //conversione a network
            send(client->socket, &net_message_lenght,sizeof(net_message_lenght),0);

            // Invio messaggio
            send(client->socket, message, message_lenght, 0);
            return;
        }
        // Risposta non valida, ripeto la richiesta 
        snprintf(buffer, sizeof(buffer), "\nRisposta non valida!\nQuiz terminato! Grazie per aver giocato.\n\nPer tornare alla scelta del quiz scrivi 'restart', per mostrare i punteggi scrivi 'show score', per terminare il quiz scrivi 'endquiz'\nLa tua risposta:");
        
        // Invio numero byte da inviare
        message_lenght = strlen(buffer);
        net_message_lenght = htonl(message_lenght); //conversione a network
        send(client->socket, &net_message_lenght,sizeof(net_message_lenght),0);

        // Invio messaggio
        send(client->socket, buffer, message_lenght, 0);
        return;
    }
}

// Funzione che carica domande e risposte nei vettori corrispondenti

int load_questions_and_answers(const char *question_file, char questions1[][MAX_LENGTH], char questions2[][MAX_LENGTH]) {
    // Apro i file
    FILE *qf = fopen(question_file, "r");
    if (!qf) {
        perror("Errore apertura file");
        return -1;
    }

    int count = 0;
    //Carico le prime 5 domande
    for (int i = 0; i < MAX_QUESTIONS; i++) {
        if (!fgets(questions1[i], MAX_LENGTH, qf)) break;
        questions1[i][strcspn(questions1[i], "\r\n")] = '\0'; // Rimuovi newline
        count++;
    }
    //Carico le seconde 5 domande
    for (int i = 0; i < MAX_QUESTIONS; i++) {
        if (!fgets(questions2[i], MAX_LENGTH, qf)) break;
        questions2[i][strcspn(questions2[i], "\r\n")] = '\0'; // Rimuovi newline
        count++;
    }

    // Chiudo i file
    fclose(qf);
    return count;
}

// Funzione che controlla se la risposta è corretta
int check_answer(const char *client_answer, const char *correct_answer) {
    return strcasecmp(client_answer, correct_answer) == 0; // Confronto case-insensitive
}

// Funzione che restituisce il set di domande/risposte corretto a seconda del tipo del quiz choice
char (*get_set(int choice, char questions1[MAX_QUESTIONS][MAX_LENGTH], char questions2[MAX_QUESTIONS][MAX_LENGTH]))[MAX_LENGTH]{
    if (choice == 1) {
        return questions1; // Ritorna il primo set
    } else if (choice == 2) {
        return questions2; // Ritorna il secondo set
    } else {
        return NULL; // Ritorna NULL se il parametro è invalido
    }
}

// Funzione chiamata ogni volta che va aggiornato il video del server che mostra i vari punteggi e i partecipanti
void outputserver(){

    bool vuoto = true;

    printf("Trivia Quiz\n+++++++++++++++++++++++++++++++++++++++\nTemi:\n1 - %s\n2 - %s\n+++++++++++++++++++++++++++++++++++++++\n \nPartecipanti(%d)\n",message1,message2,active_clients_registered);
    Client* temp = head_clients;
    while(temp!=NULL){
        if(temp->state == nickname) {
            temp = temp->next;
        }
        else{
            printf("- %s\n",temp->name);
            temp = temp->next;
            vuoto = false;
        }
    }
    if(vuoto==true){
        printf("--------\n");
    }

    vuoto = true;
    printf("\n\nPunteggio tema 1\n");
    Client_points* temp_class = head_classifica1;
    while(temp_class!=NULL){
        printf("- %s %d\n",temp_class->name,temp_class->score);
        temp_class = temp_class->next;
        vuoto = false;
    }
    if(vuoto==true){
        printf("--------\n");
    }

    vuoto = true;
    printf("\n\nPunteggio tema 2\n");
    temp_class = head_classifica2;
    while(temp_class!=NULL){
        printf("- %s %d\n",temp_class->name,temp_class->score);
        temp_class = temp_class->next;
        vuoto = false;
    }
    if(vuoto==true){
        printf("--------\n");
    }


    vuoto = true;
    printf("\n\nQuiz Tema 1 completato\n");
    temp_class = head_classifica1;
    while(temp_class!=NULL){
        if(temp_class->terminated_quiz==1){
            printf("- %s \n",temp_class->name);
            vuoto = false;
        }
        temp_class = temp_class->next;
    }
    if(vuoto==true){
            printf("--------\n");
    }

    vuoto = true;  
    printf("\n\nQuiz Tema 2 completato\n");
    temp_class = head_classifica2;
    while(temp_class!=NULL){
        if(temp_class->terminated_quiz==1){
            printf("- %s \n",temp_class->name);
            vuoto = false;
        }
        temp_class = temp_class->next;
    }
    if(vuoto==true){
        printf("--------\n");
    }  
    
    return;

}

// Funzione che manda al client che la chiama i punteggi e le classifiche
void showscore(Client* client,char questions[][MAX_LENGTH]){

    char message[BUFFER_SIZE];
    memset(message,0,sizeof(message));
    strncat(message,"\nTrivia Quiz\n+++++++++++++++++++++++++++++++++++++++\n\n",sizeof(message)-strlen(message)-1);

    char buffer_temp[200];
    memset(buffer_temp,0,sizeof(buffer_temp));
    snprintf(buffer_temp,sizeof(buffer_temp),"Partecipanti(%d)\n",active_clients_registered);
    strncat(message,buffer_temp,sizeof(message)-strlen(message)-1);

    Client* temp = head_clients;
    
    while(temp!=NULL){
        if(temp->state==nickname){
            temp = temp->next;
        }
        else{
            strncat(message,temp->name,sizeof(message)-strlen(message)-1);
            temp = temp->next;
            strncat(message,"\n",sizeof(message)-strlen(message)-1);
        }
    }

    strncat(message,"\nPunteggio Tema 1\n",sizeof(message)-strlen(message)-1);

    bool vuoto = true;
    memset(buffer_temp,0,sizeof(buffer_temp));
    Client_points* temp_class = head_classifica1;
    while(temp_class!=NULL){
        snprintf(buffer_temp,sizeof(buffer_temp),"- %s %d\n",temp_class->name,temp_class->score);
        strncat(message,buffer_temp,sizeof(message)-strlen(message)-1);
        temp_class = temp_class->next;
        vuoto = false;
    }
    if(vuoto==true){
        strncat(message,"--------\n",sizeof(message)-strlen(message)-1);
    }

    vuoto = true;
    memset(buffer_temp,0,sizeof(buffer_temp));
    temp_class = head_classifica2; 
    strncat(message,"\nPunteggio Tema 2\n",sizeof(message)-strlen(message)-1);
    while(temp_class!=NULL){
        snprintf(buffer_temp,sizeof(buffer_temp),"- %s %d\n",temp_class->name,temp_class->score);
        strncat(message,buffer_temp,sizeof(message)-strlen(message)-1);
        temp_class = temp_class->next;
        vuoto = false;
    }
    if(vuoto==true){
        strncat(message,"--------\n",sizeof(message)-strlen(message)-1);
    }

    vuoto = true;
    temp_class= head_classifica1;
    memset(buffer_temp,0,sizeof(buffer_temp));
    strncat(message,"\nQuiz Tema 1 completato\n",sizeof(message)-strlen(message)-1);

    while(temp_class!=NULL){
        if(temp_class->terminated_quiz==1){
            snprintf(buffer_temp,sizeof(buffer_temp),"- %s\n",temp_class->name);
            strncat(message,buffer_temp,sizeof(message)-strlen(message)-1);
            vuoto = false;
        }
        temp_class = temp_class->next;
    }
    if(vuoto==true){
        strncat(message,"--------\n",sizeof(message)-strlen(message)-1);
    }

    vuoto = true;
    memset(buffer_temp,0,sizeof(buffer_temp));
    temp_class= head_classifica2;

    strncat(message,"\nQuiz Tema 2 completato\n",sizeof(message)-strlen(message)-1);

    while(temp_class!=NULL){
        if(temp_class->terminated_quiz==1){
            snprintf(buffer_temp,sizeof(buffer_temp),"- %s\n",temp_class->name);
            strncat(message,buffer_temp,sizeof(message)-strlen(message)-1);
            vuoto = false;
        }
        temp_class = temp_class->next;
    }
    if(vuoto==true){
        strncat(message,"--------\n",sizeof(message)-strlen(message)-1);
    }
    strncat(message,"\n",sizeof(message)-strlen(message)-1);

    strncat(message,"\nRisultati mostrati!\n",sizeof(message)-strlen(message)-1);

    if(client->current_question>=0 && client->current_question < MAX_QUESTIONS){
        memset(buffer_temp,0,sizeof(buffer_temp));
        snprintf(buffer_temp, sizeof(buffer_temp), "\n\nQuiz - %s\n+++++++++++++++++++++++++++++++++++++++\n%s\nRisposta:",typemessage(client->type_quiz),questions[client->current_question]);
        strncat(message,buffer_temp,sizeof(message)-strlen(message)-1);
    }else{
        strncat(message,"\nInserisci restart o endquiz\nLa tua risposta:",sizeof(message)-strlen(message)-1);
    }

    // Invio numero byte da inviare
    int message_lenght = strlen(message);
    int net_message_lenght = htonl(message_lenght); // Conversione a network
    send(client->socket, &net_message_lenght,sizeof(net_message_lenght),0);

    // Invio messaggio
    send(client->socket, message, message_lenght, 0);

    return;
}

// Funzione che viene invocata se un client scrive endquiz come messaggio e va eliminato dai client connessi
void endquiz(Client* target,fd_set *read_fds){
    // Il client ha chiuso la connessione

    //setto il messagio da inviare al client con scritto 'endquiz'
    char message[BUFFER_SIZE];
    memset(message,0,sizeof(message));
    snprintf(message, sizeof(message), "endquiz");

    // Invio numero byte da inviare
    int message_lenght = strlen(message);
    int net_message_lenght = htonl(message_lenght); // Conversione a network
    send(target->socket, &net_message_lenght,sizeof(net_message_lenght),0);

    // Invio messaggio
    send(target->socket, message, message_lenght, 0);

    // Chiusura socket
    printf("\nDisconnessione: endquiz richiesto da %s (socket %d)\n\n", target->name, target->socket);
    close(target->socket);
    FD_CLR(target->socket, read_fds);

    // Eliminazione del nodo dalle eventuali classifiche in cui era
    if(target->node1!=NULL){
        elimina_client_classifica(&head_classifica1,target->node1);
    }
    if(target->node2!=NULL){
        elimina_client_classifica(&head_classifica2,target->node2);
    }

    // Eliminazione del nodo dalla lista di client
    elimina_client(target);

    active_clients--;
    active_clients_registered--;

    outputserver();

    return;
}

 // Funzione che crea un nuovo nodo client e lo aggiunge alla lista in fondo
Client* crea_nodo(int socket) { 
    Client* new_node = (Client*)malloc(sizeof(Client));
    if (new_node == NULL) {
        perror("Errore durante l'allocazione di memoria");
        exit(EXIT_FAILURE);
    }
    new_node->socket = socket;
    new_node->state = nickname;
    new_node->score=0;
    new_node->type_quiz = 0;
    new_node->current_question = 0;
    new_node->next = NULL;
    new_node->node1 = NULL;
    new_node->node2 = NULL;

    // Inserimento in coda alla lista
    if (head_clients == NULL) {
        head_clients = new_node;
    } else {
        Client* current = head_clients;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_node;
    }

    return new_node;
}

// Funzione che elimina il nodo client target dalla lista e dalla memoria
void elimina_client(Client* target) {
    if (head_clients == NULL || target == NULL) {
        return;
    }

    // Caso in cui il nodo da eliminare è la testa
    if (head_clients == target) {
        head_clients = head_clients->next;
        free(target);

        return;
    }

    // Cerca il nodo precedente al nodo target
    Client* prec = head_clients;
    while (prec->next != NULL && prec->next != target) {
        prec = prec->next;
    }

    if (prec->next == target) {
        prec->next = target->next;
        free(target);

    }
}


// GESTIONE CLASSIFICHE

// Funzione che crea il nodo della classifica
Client_points* crea_nodo_classifica(const char* name,Client_points** head) {
    Client_points* new_node = (Client_points*)malloc(sizeof(Client_points));
    if (new_node == NULL) {
        perror("Errore durante l'allocazione di memoria");
        exit(EXIT_FAILURE);
    }

    strcpy(new_node->name, name);
    new_node->score = 0;
    new_node->terminated_quiz = 0;
    new_node->prec = NULL;
    new_node->next = NULL;

    // Inserimento in coda
    if (*head == NULL) {
        *head = new_node;
    } else {
        Client_points* current = *head;
        while (current->next != NULL) {
            current = current->next;

        }
        new_node->prec = current;
        current->next = new_node;
    }

    return new_node;
}

// Funzione che riordina la lista della classifica passata come argomento
void riordina_classifica(Client_points** head, Client_points* target) {

   if (!head || !*head || !target) {
        return; // Lista vuota o nodo nullo
    }

    // Se il nodo è già in testa o è l'unico nella lista, non c'è nulla da fare
    if (target->prec == NULL && target == *head) {
        return;
    }

    // Controlla se il nodo è già nella posizione corretta
    if ((target->prec && target->prec->score >= target->score) || (!target->prec && target == *head)) {
        return; // Il nodo è già nella posizione corretta
    }

    // Rimuovi il nodo dalla posizione attuale
    if (target->next) {
        target->next->prec = target->prec;
    }
    if (target->prec) {
        target->prec->next = target->next;
    } else {
        *head = target->next; // Aggiorna la testa se il nodo era in cima
    }

    // Trova la nuova posizione spostandosi all'indietro finchè trova un nodo con uno score maggiore o finchè trova la testa
    Client_points* current = target->prec;
    while (current && current->score < target->score) {
        current = current->prec;
    }

    // Inserisci il nodo nella nuova posizione
    if (current) {
        target->next = current->next;
        target->prec = current;
        if (current->next) {
            current->next->prec = target;
        }
        current->next = target;
    } else {
        // Il nodo diventa la nuova testa
        target->next = *head;
        target->prec = NULL;
        if (*head) {
            (*head)->prec = target;
        }
        *head = target;
    }
}

// Funzione che elimina il nodo dalla classifica passata come argomento
void elimina_client_classifica(Client_points** head, Client_points* target) {
    if (!head || !*head || !target) {
        return; // Lista vuota o nodo nullo
    }

    // Aggiorna i puntatori del nodo successivo
    if (target->next) {
        target->next->prec = target->prec;
    }

    // Aggiorna i puntatori del nodo precedente
    if (target->prec) {
        target->prec->next = target->next;
    } else {
        // Se `target` è la testa, aggiorna la testa della lista
        *head = target->next;
    }

    // Libera la memoria del nodo eliminato
    free(target);
}

// Funzione che rimuove spazi in testa e in coda ad una stringa ( per il Nickname )
char* remove_spaces(char * stringa){
    char *end;

    // Rimuovo spazi iniziali
    while(isspace((unsigned char)*stringa)){
        stringa++;
    }

    // Se la stringa è vuota ritorno subito
    if(*stringa=='\0'){
        return stringa;
    }

    // Trovo la fine della stringa
    end = stringa + strlen(stringa)-1;
    while(end > stringa && isspace((unsigned char)*end)){
        end--;
    }

    // Aggiungo il terminatore nullo
    *(end +1) = '\0';

    return stringa;
}


// Handler che gestisce la chiusura dei socket corettamente in caso di chiusura da terminale del server quando era attivo
void handle_sigint(int sig){
    printf("\nChiusura da terminale del server!\n");
    Client* temp = head_clients;
    while(temp!=NULL){
        close(temp->socket);
        temp = temp->next;
    }
    printf("Ho chiuso i socket dei clients\n");
    close(server_socket);
    _exit(0);
}




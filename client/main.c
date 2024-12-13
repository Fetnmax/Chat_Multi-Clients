#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <termios.h>
#include <time.h>

#define PORT 8080
#define CLEAR_SCREEN "\033[2J"
#define CURSOR_HOME "\033[H"
#define SAVE_CURSOR "\033[s"
#define RESTORE_CURSOR "\033[u"
#define SEPARATOR_LINE 23
#define PROMPT_LINE 24
#define CURSOR_TO_INPUT "\033[24;1H"
#define CLEAR_LINE "\033[K"
#define MAX_MESSAGES 1000

typedef struct {
    int sock;
    char channel_name[50];
    char username[50];
} ThreadData;

char message_buffer[MAX_MESSAGES][1024];
int message_count = 0;
pthread_mutex_t message_lock = PTHREAD_MUTEX_INITIALIZER;

// Fonction pour configurer le terminal avec le nom du canal
void setup_terminal(char *channel_name) {
    printf(CLEAR_SCREEN);
    printf(CURSOR_HOME);
    printf("=== %s ===\n", channel_name);
    printf("\033[%d;0H=======================\n", SEPARATOR_LINE);
    printf("\033[%d;0H>", PROMPT_LINE);
    fflush(stdout);
}

// Fonction pour afficher les messages
void display_messages(void) {
    pthread_mutex_lock(&message_lock);
    // Effacer la zone des messages (lignes 2-22)
    for (int i = 2; i <= 22; i++) {
        printf("\033[%d;0H", i); // Aller à la ligne i
        printf(CLEAR_LINE);       // Effacer la ligne
    }
    // Afficher les messages
    int start = message_count > 20 ? message_count - 20 : 0;
    int line = 2;
    for (int i = start; i < message_count; i++) {
        printf("\033[%d;0H", line++); // Aller à la ligne suivante
        printf("%s", message_buffer[i]); // Pas de \n car déjà présent
    }
    // Ajouter la ligne de séparation et le prompt
    printf("\033[%d;0H=======================\n", SEPARATOR_LINE);
    printf("\033[%d;0H>", PROMPT_LINE); // Ligne de saisie
    fflush(stdout);
    pthread_mutex_unlock(&message_lock);
}

// Fonction pour le thread de réception
void *receive_messages(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    char recv_buffer[4096];
    char message[2048];
    int receiving_history = 1;
    int bytes_read;
    int message_start = 0;
    int recv_buffer_len = 0;

    while (1) {
        // Lire les données depuis le socket
        bytes_read = read(data->sock, recv_buffer + recv_buffer_len, sizeof(recv_buffer) - recv_buffer_len - 1);
        if (bytes_read <= 0) {
            // Déconnexion du serveur
            pthread_mutex_lock(&message_lock);
            strcpy(message_buffer[message_count++], "Déconnecté du serveur.\n");
            pthread_mutex_unlock(&message_lock);
            display_messages();
            exit(1);
        }

        recv_buffer_len += bytes_read;
        recv_buffer[recv_buffer_len] = '\0'; // Terminer la chaîne

        // Traiter les lignes complètes
        int i;
        for (i = 0; i < recv_buffer_len; i++) {
            if (recv_buffer[i] == '\n') {
                // Extraire une ligne complète
                int line_length = i - message_start + 1;
                strncpy(message, recv_buffer + message_start, line_length);
                message[line_length] = '\0';
                message_start = i + 1;

                // Vérifier la fin de l'historique
                if (receiving_history && strcmp(message, "<end_of_history>\n") == 0) {
                    receiving_history = 0;
                    continue;
                }

                // Ajouter le message au tampon
                pthread_mutex_lock(&message_lock);
                strcpy(message_buffer[message_count++], message);
                pthread_mutex_unlock(&message_lock);

                // Afficher les messages
                display_messages();
            }
        }

        // Garder le reste des données non traitées dans recv_buffer
        if (message_start < recv_buffer_len) {
            memmove(recv_buffer, recv_buffer + message_start, recv_buffer_len - message_start);
            recv_buffer_len -= message_start;
            message_start = 0;
        } else {
            recv_buffer_len = 0;
            message_start = 0;
        }
    }
    return NULL;
}

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char channel_name[50];
    char message[1024];
    char username[50];
    ThreadData thread_data;

    // Création du socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Erreur lors de la création du socket\n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("Adresse invalide ou non supportée\n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Échec de la connexion\n");
        return -1;
    }

    // Saisir le nom d'utilisateur
    printf("Entrez votre nom d'utilisateur : ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;

    // Saisir le nom du canal
    printf("Entrez le nom du canal : ");
    fgets(channel_name, sizeof(channel_name), stdin);
    channel_name[strcspn(channel_name, "\n")] = 0;

    // Préparer les messages à envoyer (chaque message se termine par \n)
    char send_buffer[1050];
    snprintf(send_buffer, sizeof(send_buffer), "%s\n%s\n", username, channel_name);

    // Envoyer le pseudonyme et le nom du canal au serveur
    send(sock, send_buffer, strlen(send_buffer), 0);

    // Configurer le terminal avec le nom du canal
    setup_terminal(channel_name);

    // Préparer les données pour le thread
    thread_data.sock = sock;
    strcpy(thread_data.channel_name, channel_name);
    strcpy(thread_data.username, username);

    // Créer le thread de réception
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receive_messages, &thread_data);

    // Boucle principale d'envoi de messages
    while (1) {
        // Aller à la zone de saisie
        printf(CURSOR_TO_INPUT);
        printf(CLEAR_LINE);
        printf(">");
        fflush(stdout);

        // Lire le message de l'utilisateur
        fgets(message, sizeof(message), stdin);
        message[strcspn(message, "\n")] = 0;

        if (strcmp(message, "exit") == 0) {
            break;
        }

        // Envoyer le message au serveur (ajout d'un \n)
        char message_to_send[1025];
        snprintf(message_to_send, sizeof(message_to_send), "%s\n", message);
        send(sock, message_to_send, strlen(message_to_send), 0);

        // Obtenir l'heure actuelle
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char time_str[9]; // HH:MM:SS
        strftime(time_str, sizeof(time_str), "%H:%M:%S", t);

        // Ajouter le message envoyé au tampon
        pthread_mutex_lock(&message_lock);
        char formatted_message[2048];
        snprintf(formatted_message, sizeof(formatted_message), "[%s][Vous]: %s\n", time_str, message);
        strcpy(message_buffer[message_count++], formatted_message);
        pthread_mutex_unlock(&message_lock);

        // Afficher les messages
        display_messages();
    }

    // Nettoyer
    close(sock);
    return 0;
}
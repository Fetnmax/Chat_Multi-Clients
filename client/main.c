#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <termios.h>
#include <time.h>
#include <sys/stat.h>

#define PORT 8080
#define CLEAR_SCREEN "\033[2J"
#define CURSOR_HOME "\033[H"
#define SEPARATOR_LINE 23
#define PROMPT_LINE 24
#define CURSOR_TO_INPUT "\033[24;1H"
#define CLEAR_LINE "\033[K"
#define MAX_MESSAGES 1000
#define CHUNK_SIZE 8192
#define DOWNLOADS_DIR "downloads"

// Couleurs
#define COLOR_RESET "\033[0m"
#define COLOR_SEND "\033[1;34m"
#define COLOR_RECEIVE "\033[1;32m"
#define COLOR_SYSTEM "\033[1;33m"

typedef struct {
    int sock;
    char channel_name[50];
    char username[50];
} ThreadData;

typedef struct {
    char content[2048];
    int is_file_transfer;
    int is_send;
} Message;

Message message_buffer[MAX_MESSAGES];
int message_count = 0;
pthread_mutex_t message_lock = PTHREAD_MUTEX_INITIALIZER;

// Configuration du terminal
void setup_terminal(const char *channel_name) {
    printf(CLEAR_SCREEN);
    printf(CURSOR_HOME);
    printf("=== %s ===\n", channel_name);
    printf("\033[%d;0H=======================\n", SEPARATOR_LINE);
    printf("\033[%d;0H>", PROMPT_LINE);
    fflush(stdout);
}

// Affiche la liste des messages
void display_messages(void) {
    pthread_mutex_lock(&message_lock);

    // Efface la zone des messages
    for (int i = 2; i <= 22; i++) {
        printf("\033[%d;0H%s", i, CLEAR_LINE);
    }

    int start = (message_count > 20) ? message_count - 20 : 0;
    int line = 2;
    for (int i = start; i < message_count; i++) {
        printf("\033[%d;0H", line++);
        if (message_buffer[i].is_file_transfer) {
            if (message_buffer[i].is_send) {
                printf(COLOR_SEND "%s" COLOR_RESET, message_buffer[i].content);
            } else {
                printf(COLOR_RECEIVE "%s" COLOR_RESET, message_buffer[i].content);
            }
        } else if (
            strncmp(message_buffer[i].content, "Erreur", 5) == 0 ||
            strncmp(message_buffer[i].content, "Fichier reçu", 12) == 0 ||
            strncmp(message_buffer[i].content, "Fichier envoyé", 14) == 0 ||
            strncmp(message_buffer[i].content, "Déconnecté", 10) == 0
        ) {
            printf(COLOR_SYSTEM "%s" COLOR_RESET, message_buffer[i].content);
        } else {
            printf("%s", message_buffer[i].content);
        }
    }

    printf("\033[%d;0H=======================\n", SEPARATOR_LINE);
    printf("\033[%d;0H>", PROMPT_LINE);
    fflush(stdout);
    pthread_mutex_unlock(&message_lock);
}

// Envoi d'un fichier
void send_file(int sock, const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        pthread_mutex_lock(&message_lock);
        snprintf(
            message_buffer[message_count].content,
            sizeof(message_buffer[message_count].content),
            "Erreur: Impossible d'ouvrir le fichier %s\n",
            filepath
        );
        message_buffer[message_count].is_file_transfer = 0;
        message_buffer[message_count].is_send = 0;
        message_count++;
        pthread_mutex_unlock(&message_lock);
        display_messages();
        return;
    }

    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *filename = strrchr(filepath, '/');
    filename = (filename) ? (filename + 1) : (char *)filepath;

    // Envoi de l'en-tête
    char header[1024];
    snprintf(header, sizeof(header), "/file\n%s\n%ld\n", filename, filesize);
    if (send(sock, header, strlen(header), 0) < 0) {
        pthread_mutex_lock(&message_lock);
        snprintf(
            message_buffer[message_count].content,
            sizeof(message_buffer[message_count].content),
            "Erreur: Envoi de l'en-tête du fichier a échoué.\n"
        );
        message_buffer[message_count].is_file_transfer = 1;
        message_buffer[message_count].is_send = 1;
        message_count++;
        pthread_mutex_unlock(&message_lock);
        display_messages();
        fclose(file);
        return;
    }

    // Envoi du contenu
    char buffer[CHUNK_SIZE];
    size_t bytes_read;
    int send_success = 1;
    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, file)) > 0) {
        if (send(sock, buffer, bytes_read, 0) < 0) {
            pthread_mutex_lock(&message_lock);
            snprintf(
                message_buffer[message_count].content,
                sizeof(message_buffer[message_count].content),
                "Erreur: Envoi du fichier a échoué.\n"
            );
            message_buffer[message_count].is_file_transfer = 1;
            message_buffer[message_count].is_send = 1;
            message_count++;
            pthread_mutex_unlock(&message_lock);
            display_messages();
            send_success = 0;
            break;
        }
    }
    fclose(file);

    // Message de confirmation si tout est envoyé
    if (send_success) {
        pthread_mutex_lock(&message_lock);
        snprintf(
            message_buffer[message_count].content,
            sizeof(message_buffer[message_count].content),
            "Fichier envoyé: %s\n",
            filename
        );
        message_buffer[message_count].is_file_transfer = 1;
        message_buffer[message_count].is_send = 1;
        message_count++;
        pthread_mutex_unlock(&message_lock);
        display_messages();
    }
}

// Réception d'un fichier
void receive_file(const char *filename, long filesize, int sock) {
    mkdir(DOWNLOADS_DIR, 0777);

    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s", DOWNLOADS_DIR, filename);

    FILE *file = fopen(filepath, "wb");
    if (!file) {
        pthread_mutex_lock(&message_lock);
        snprintf(
            message_buffer[message_count].content,
            sizeof(message_buffer[message_count].content),
            "Erreur: Impossible de créer le fichier %s\n",
            filepath
        );
        message_buffer[message_count].is_file_transfer = 0;
        message_buffer[message_count].is_send = 0;
        message_count++;
        pthread_mutex_unlock(&message_lock);
        display_messages();
        return;
    }

    char buffer[CHUNK_SIZE];
    long remaining = filesize;
    while (remaining > 0) {
        size_t to_read = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
        ssize_t rd = recv(sock, buffer, to_read, 0);
        if (rd <= 0) {
            pthread_mutex_lock(&message_lock);
            snprintf(
                message_buffer[message_count].content,
                sizeof(message_buffer[message_count].content),
                "Erreur: Réception du fichier interrompue.\n"
            );
            message_buffer[message_count].is_file_transfer = 1;
            message_buffer[message_count].is_send = 0;
            message_count++;
            pthread_mutex_unlock(&message_lock);
            display_messages();
            fclose(file);
            return;
        }
        fwrite(buffer, 1, rd, file);
        remaining -= rd;
    }
    fclose(file);

    pthread_mutex_lock(&message_lock);
    snprintf(
        message_buffer[message_count].content,
        sizeof(message_buffer[message_count].content),
        "Fichier reçu: %s/%s\n",
        DOWNLOADS_DIR,
        filename
    );
    message_buffer[message_count].is_file_transfer = 1;
    message_buffer[message_count].is_send = 0;
    message_count++;
    pthread_mutex_unlock(&message_lock);
    display_messages();
}

// Thread de réception
void *receive_messages(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    char buffer[CHUNK_SIZE];
    int receiving_history = 1;

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = recv(data->sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            pthread_mutex_lock(&message_lock);
            snprintf(
                message_buffer[message_count].content,
                sizeof(message_buffer[message_count].content),
                "Déconnecté du serveur.\n"
            );
            message_buffer[message_count].is_file_transfer = 0;
            message_buffer[message_count].is_send = 0;
            message_count++;
            pthread_mutex_unlock(&message_lock);
            display_messages();
            exit(1);
        }

        char *ptr = buffer;
        char *end = buffer + bytes_read;
        while (ptr < end) {
            if (!strncmp(ptr, "/file\n", 6)) {
                // En-tête de fichier
                char fn[256];
                long sz = 0;
                int header_consumed = 0;
                int sc = sscanf(ptr + 6, "%255s\n%ld\n%n", fn, &sz, &header_consumed);
                if (sc == 2) {
                    ptr += 6 + header_consumed;
                    receive_file(fn, sz, data->sock);
                } else {
                    // En-tête incomplète => on récupère plus tard
                    break;
                }
            } else {
                // Message texte
                char *newline = memchr(ptr, '\n', end - ptr);
                if (!newline) {
                    // Morceau de message incomplet
                    break;
                }
                size_t msg_len = (newline - ptr) + 1;
                if (msg_len > 2047) {
                    msg_len = 2047;
                }

                char msg[2048];
                strncpy(msg, ptr, msg_len);
                msg[msg_len] = '\0';

                // Fin de l'historique ?
                if (receiving_history && !strcmp(msg, "<end_of_history>\n")) {
                    receiving_history = 0;
                } else {
                    // Retirer le '\n' éventuel si on veut éviter les doubles sauts
                    size_t mlen = strlen(msg);
                    if (mlen > 0 && msg[mlen - 1] == '\n') {
                        msg[mlen - 1] = '\0';
                    }

                    pthread_mutex_lock(&message_lock);
                    snprintf(
                        message_buffer[message_count].content,
                        sizeof(message_buffer[message_count].content),
                        "%s\n",
                        msg
                    );
                    message_buffer[message_count].is_file_transfer = 0;
                    message_buffer[message_count].is_send = 0;
                    message_count++;
                    pthread_mutex_unlock(&message_lock);
                    display_messages();
                }
                ptr += msg_len;
            }
        }
    }
    return NULL;
}

int main() {
    int sock;
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

    // Récupération du pseudo
    printf("Entrez votre nom d'utilisateur : ");
    if (!fgets(username, sizeof(username), stdin)) {
        printf("Erreur de lecture du nom d'utilisateur.\n");
        close(sock);
        return -1;
    }
    username[strcspn(username, "\n")] = 0;

    // Récupération du nom de canal
    printf("Entrez le nom du canal : ");
    if (!fgets(channel_name, sizeof(channel_name), stdin)) {
        printf("Erreur de lecture du nom du canal.\n");
        close(sock);
        return -1;
    }
    channel_name[strcspn(channel_name, "\n")] = 0;

    // Envoi des infos initiales
    char send_buffer[1050];
    snprintf(send_buffer, sizeof(send_buffer), "%s\n%s\n", username, channel_name);
    if (send(sock, send_buffer, strlen(send_buffer), 0) < 0) {
        printf("Erreur: Envoi des informations initiales.\n");
        close(sock);
        return -1;
    }

    setup_terminal(channel_name);

    thread_data.sock = sock;
    strncpy(thread_data.channel_name, channel_name, sizeof(thread_data.channel_name) - 1);
    strncpy(thread_data.username, username, sizeof(thread_data.username) - 1);

    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receive_messages, &thread_data);

    // Boucle d'envoi
    while (1) {
        printf(CURSOR_TO_INPUT);
        printf(CLEAR_LINE);
        printf(">");
        fflush(stdout);

        if (!fgets(message, sizeof(message), stdin)) {
            break;
        }
        message[strcspn(message, "\n")] = 0;

        // Quitter
        if (!strcmp(message, "/exit")) {
            break;
        }
        // Envoi d'un fichier
        if (!strncmp(message, "/send ", 6)) {
            send_file(sock, message + 6);
            continue;
        }

        // Envoi d'un message texte
        char message_to_send[1050];
        snprintf(message_to_send, sizeof(message_to_send), "%s\n", message);
        if (send(sock, message_to_send, strlen(message_to_send), 0) < 0) {
            pthread_mutex_lock(&message_lock);
            snprintf(
                message_buffer[message_count].content,
                sizeof(message_buffer[message_count].content),
                "Erreur: Envoi du message a échoué.\n"
            );
            message_buffer[message_count].is_file_transfer = 0;
            message_buffer[message_count].is_send = 0;
            message_count++;
            pthread_mutex_unlock(&message_lock);
            display_messages();
            continue;
        }

        // Afficher localement le message envoyé
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char time_str[9];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", t);

        pthread_mutex_lock(&message_lock);
        snprintf(
            message_buffer[message_count].content,
            sizeof(message_buffer[message_count].content),
            "[%s][Vous]: %s\n",
            time_str,
            message
        );
        message_buffer[message_count].is_file_transfer = 0;
        message_buffer[message_count].is_send = 0;
        message_count++;
        pthread_mutex_unlock(&message_lock);
        display_messages();
    }

    close(sock);
    return 0;
}
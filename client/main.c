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
#define SAVE_CURSOR "\033[s"
#define RESTORE_CURSOR "\033[u"
#define SEPARATOR_LINE 23
#define PROMPT_LINE 24
#define CURSOR_TO_INPUT "\033[24;1H"
#define CLEAR_LINE "\033[K"
#define MAX_MESSAGES 1000
#define CHUNK_SIZE 8192
#define DOWNLOADS_DIR "downloads"

typedef struct {
    int sock;
    char channel_name[50];
    char username[50];
} ThreadData;

char message_buffer[MAX_MESSAGES][1024];
int message_count = 0;
pthread_mutex_t message_lock = PTHREAD_MUTEX_INITIALIZER;

// Configuration du terminal
void setup_terminal(char *channel_name) {
    printf(CLEAR_SCREEN);
    printf(CURSOR_HOME);
    printf("=== %s ===\n", channel_name);
    printf("\033[%d;0H=======================\n", SEPARATOR_LINE);
    printf("\033[%d;0H>", PROMPT_LINE);
    fflush(stdout);
}

// Affichage des messages
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
        printf("\033[%d;0H", line++);
        printf("%s", message_buffer[i]);
    }
    // Ajouter la ligne de séparation et le prompt
    printf("\033[%d;0H=======================\n", SEPARATOR_LINE);
    printf("\033[%d;0H>", PROMPT_LINE);
    fflush(stdout);
    pthread_mutex_unlock(&message_lock);
}

// Fonction pour envoyer un fichier
void send_file(int sock, const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        printf("Erreur: Impossible d'ouvrir le fichier %s\n", filepath);
        return;
    }

    // Obtenir la taille du fichier
    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Extraire le nom du fichier du chemin
    char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : (char*)filepath;

    // Envoyer en-tête du fichier
    char header[1024];
    snprintf(header, sizeof(header), "/file\n%s\n%ld\n", filename, filesize);
    if (send(sock, header, strlen(header), 0) < 0) {
        printf("Erreur: Envoi de l'en-tête du fichier a échoué.\n");
        fclose(file);
        return;
    }

    // Envoyer le contenu du fichier
    char buffer[CHUNK_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, file)) > 0) {
        if (send(sock, buffer, bytes_read, 0) < 0) {
            printf("Erreur: Envoi du fichier a échoué.\n");
            fclose(file);
            return;
        }
    }

    fclose(file);
    printf("Fichier envoyé: %s\n", filename);
}

// Fonction pour recevoir un fichier
void receive_file(const char *filename, long filesize, int sock) {
    // Créer le dossier downloads si nécessaire
    mkdir(DOWNLOADS_DIR, 0777);
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s", DOWNLOADS_DIR, filename);

    FILE *file = fopen(filepath, "wb");
    if (!file) {
        printf("Erreur: Impossible de créer le fichier %s\n", filepath);
        return;
    }

    char buffer[CHUNK_SIZE];
    long remaining = filesize;
    while (remaining > 0) {
        size_t to_read = remaining < CHUNK_SIZE ? remaining : CHUNK_SIZE;
        ssize_t bytes_read = read(sock, buffer, to_read);
        if (bytes_read <= 0) {
            printf("Erreur: Réception du fichier interrompue.\n");
            break;
        }
        fwrite(buffer, 1, bytes_read, file);
        remaining -= bytes_read;
    }

    fclose(file);
    printf("Fichier reçu: %s\n", filepath);
}

// Thread de réception
void *receive_messages(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    char buffer[CHUNK_SIZE];
    int receiving_history = 1;
    char partial_buffer[CHUNK_SIZE] = {0};
    int partial_len = 0;

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = recv(data->sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            pthread_mutex_lock(&message_lock);
            strcpy(message_buffer[message_count++], "Déconnecté du serveur.\n");
            pthread_mutex_unlock(&message_lock);
            display_messages();
            exit(1);
        }

        // Gérer les éventuelles données partielles
        char *ptr = buffer;
        while (bytes_read > 0) {
            // Si on attend un fichier
            if (strncmp(ptr, "/file\n", 6) == 0) {
                // Lire le nom du fichier et la taille
                char file_info[256];
                char filename[256];
                long filesize;
                sscanf(ptr + 6, "%s\n%ld\n", filename, &filesize);

                // Avancer le pointeur
                ptr += 6;
                // Trouver la fin de la ligne filename
                char *newline = strchr(ptr, '\n');
                if (!newline) break;
                ptr = newline + 1;

                // Trouver la fin de la taille
                newline = strchr(ptr, '\n');
                if (!newline) break;
                ptr = newline + 1;

                receive_file(filename, filesize, data->sock);
                bytes_read -= (newline - buffer) + 1;
            } else {
                // Traiter comme un message normal
                char *newline = strchr(ptr, '\n');
                if (newline) {
                    size_t msg_len = newline - ptr + 1;
                    char message[1024];
                    strncpy(message, ptr, msg_len);
                    message[msg_len] = '\0';

                    // Vérifier la fin de l'historique
                    if (receiving_history && strcmp(message, "<end_of_history>\n") == 0) {
                        receiving_history = 0;
                    } else {
                        pthread_mutex_lock(&message_lock);
                        strcpy(message_buffer[message_count++], message);
                        pthread_mutex_unlock(&message_lock);
                        display_messages();
                    }

                    ptr = newline + 1;
                    bytes_read -= msg_len;
                } else {
                    // Message partiel, stocker dans partial_buffer
                    memcpy(partial_buffer + partial_len, ptr, bytes_read);
                    partial_len += bytes_read;
                    partial_buffer[partial_len] = '\0';
                    ptr += bytes_read;
                    bytes_read = 0;
                }
            }
        }

        // Si on a un message partiel
        if (partial_len > 0) {
            printf("Message partiel reçu: %s\n", partial_buffer);
            partial_len = 0;
            memset(partial_buffer, 0, sizeof(partial_buffer));
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

    printf("Entrez votre nom d'utilisateur : ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;

    printf("Entrez le nom du canal : ");
    fgets(channel_name, sizeof(channel_name), stdin);
    channel_name[strcspn(channel_name, "\n")] = 0;

    // Préparer et envoyer le pseudonyme et le nom du canal
    char send_buffer[1050];
    snprintf(send_buffer, sizeof(send_buffer), "%s\n%s\n", username, channel_name);
    send(sock, send_buffer, strlen(send_buffer), 0);

    setup_terminal(channel_name);

    thread_data.sock = sock;
    strcpy(thread_data.channel_name, channel_name);
    strcpy(thread_data.username, username);

    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receive_messages, &thread_data);

    while (1) {
        printf(CURSOR_TO_INPUT);
        printf(CLEAR_LINE);
        printf(">");
        fflush(stdout);

        if (fgets(message, sizeof(message), stdin) == NULL) {
            break;
        }
        message[strcspn(message, "\n")] = 0;

        if (strcmp(message, "/exit") == 0) {
            break;
        }

        // Vérifier si c'est une commande d'envoi de fichier
        if (strncmp(message, "/send ", 6) == 0) {
            send_file(sock, message + 6);
            continue; // Ne pas ajouter la commande au tampon de messages
        }

        // Envoyer le message au serveur
        char message_to_send[1050];
        snprintf(message_to_send, sizeof(message_to_send), "%s\n", message);
        if (send(sock, message_to_send, strlen(message_to_send), 0) < 0) {
            printf("Erreur: Envoi du message a échoué.\n");
            continue;
        }

        // Ajouter le message au tampon local
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char time_str[9];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", t);

        pthread_mutex_lock(&message_lock);
        char formatted_message[2048];
        snprintf(formatted_message, sizeof(formatted_message), "[%s][Vous]: %s\n", time_str, message);
        strcpy(message_buffer[message_count++], formatted_message);
        pthread_mutex_unlock(&message_lock);

        display_messages();
    }

    close(sock);
    return 0;
}
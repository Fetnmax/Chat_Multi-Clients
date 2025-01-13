/**
 * @brief Serveur de chat en C avec support multi-canaux et transfert de fichiers
 * 
 * Fonctionnalités:
 * - Gestion multi-clients avec threads
 * - Support des canaux de discussion
 * - Historique des messages par canal
 * - Transfert de fichiers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>

// Configuration
#define PORT 8080
#define MAX_CONNECTIONS 100
#define MAX_CHANNELS 100
#define MAX_MESSAGES 1000
#define MAX_MESSAGE_LENGTH 1024
#define MAX_CLIENTS_PER_CHANNEL 100
#define CHUNK_SIZE 8192

// Structures de données
typedef struct {
    int socket;
    char username[50];
} Client;

typedef struct {
    char name[50];
    char messages[MAX_MESSAGES][MAX_MESSAGE_LENGTH];
    int message_count;
    Client clients[MAX_CLIENTS_PER_CHANNEL];
    int client_count;
} Channel;

// Variables globales
Channel channels[MAX_CHANNELS];
int channel_count = 0;
pthread_mutex_t channel_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Lit une ligne terminée par \n depuis le socket
 * @return Nombre d'octets lus ou -1 si erreur
 */
ssize_t read_line(int sock, char *buffer, size_t max_len) {
    ssize_t total = 0;
    while (total < max_len - 1) {
        char c;
        ssize_t bytes = read(sock, &c, 1);
        if (bytes <= 0) {
            return -1;
        }
        if (c == '\n') {
            break;
        }
        buffer[total++] = c;
    }
    buffer[total] = '\0';
    return total;
}

/**
 * Trouve ou crée un canal
 * @return Index du canal ou -1 si erreur
 */
int find_or_create_channel(const char *name) {
    pthread_mutex_lock(&channel_lock);

    // Recherche d'un canal existant
    for (int i = 0; i < channel_count; i++) {
        if (strcmp(channels[i].name, name) == 0) {
            pthread_mutex_unlock(&channel_lock);
            return i;
        }
    }

    // Création d'un nouveau canal
    if (channel_count < MAX_CHANNELS) {
        strcpy(channels[channel_count].name, name);
        channels[channel_count].message_count = 0;
        channels[channel_count].client_count = 0;
        channel_count++;
        pthread_mutex_unlock(&channel_lock);
        return channel_count - 1;
    }

    pthread_mutex_unlock(&channel_lock);
    return -1;
}

/**
 * Ajoute un client à un canal
 */
void add_client_to_channel(int channel_index, int client_socket, const char *username) {
    pthread_mutex_lock(&channel_lock);
    Channel *channel = &channels[channel_index];
    if (channel->client_count < MAX_CLIENTS_PER_CHANNEL) {
        channel->clients[channel->client_count].socket = client_socket;
        strcpy(channel->clients[channel->client_count].username, username);
        channel->client_count++;
    }
    pthread_mutex_unlock(&channel_lock);
}

/**
 * Retire un client d'un canal
 */
void remove_client_from_channel(int channel_index, int client_socket) {
    pthread_mutex_lock(&channel_lock);
    Channel *channel = &channels[channel_index];
    for (int i = 0; i < channel->client_count; i++) {
        if (channel->clients[i].socket == client_socket) {
            // Décalage des clients restants
            for (int j = i; j < channel->client_count - 1; j++) {
                channel->clients[j] = channel->clients[j + 1];
            }
            channel->client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&channel_lock);
}

/**
 * Ajoute un message à l'historique du canal
 */
void add_message_to_channel(int channel_index, const char *message) {
    pthread_mutex_lock(&channel_lock);
    Channel *channel = &channels[channel_index];

    if (channel->message_count < MAX_MESSAGES) {
        strncpy(channel->messages[channel->message_count], message, MAX_MESSAGE_LENGTH - 1);
        channel->messages[channel->message_count][MAX_MESSAGE_LENGTH - 1] = '\0';
        channel->message_count++;
    }

    // Sauvegarde dans le fichier d'historique
    char filename[100];
    snprintf(filename, sizeof(filename), "channel_%s.txt", channel->name);
    FILE *file = fopen(filename, "a");
    if (file != NULL) {
        fprintf(file, "%s", message);
        fclose(file);
    }

    pthread_mutex_unlock(&channel_lock);
}

/**
 * Transfère un fichier aux autres clients du canal
 */
void forward_file(int from_socket, const char *filename, long filesize, int channel_index) {
    pthread_mutex_lock(&channel_lock);
    Channel *channel = &channels[channel_index];

    // Envoi de l'en-tête aux autres clients
    char header[1024];
    snprintf(header, sizeof(header), "/file\n%s\n%ld\n", filename, filesize);
    for (int i = 0; i < channel->client_count; i++) {
        if (channel->clients[i].socket != from_socket) {
            send(channel->clients[i].socket, header, strlen(header), 0);
        }
    }

    // Transfert du contenu par morceaux
    char buffer[CHUNK_SIZE];
    long remaining = filesize;
    pthread_mutex_unlock(&channel_lock);

    while (remaining > 0) {
        ssize_t to_read = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
        ssize_t bytes_read = read(from_socket, buffer, to_read);
        if (bytes_read <= 0) {
            break;
        }

        pthread_mutex_lock(&channel_lock);
        for (int i = 0; i < channel->client_count; i++) {
            if (channel->clients[i].socket != from_socket) {
                send(channel->clients[i].socket, buffer, bytes_read, 0);
            }
        }
        pthread_mutex_unlock(&channel_lock);

        remaining -= bytes_read;
    }
}

/**
 * Thread gérant un client connecté
 */
void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);
    char buffer[CHUNK_SIZE] = {0};
    char username[50];
    int channel_index;

    // Lecture des informations initiales
    if (read_line(client_socket, username, sizeof(username)) <= 0) {
        close(client_socket);
        pthread_exit(NULL);
    }

    if (read_line(client_socket, buffer, sizeof(buffer)) <= 0) {
        close(client_socket);
        pthread_exit(NULL);
    }
    printf("Utilisateur %s demande de connexion au canal : %s\n", username, buffer);

    // Connexion au canal
    channel_index = find_or_create_channel(buffer);
    if (channel_index == -1) {
        char *error_message = "Erreur : Impossible de créer ou rejoindre le canal.\n";
        send(client_socket, error_message, strlen(error_message), 0);
        close(client_socket);
        pthread_exit(NULL);
    }
    add_client_to_channel(channel_index, client_socket, username);

    // Envoi de l'historique
    char filename[100];
    snprintf(filename, sizeof(filename), "channel_%s.txt", channels[channel_index].name);
    FILE *file = fopen(filename, "r");
    if (file != NULL) {
        while (fgets(buffer, sizeof(buffer), file)) {
            send(client_socket, buffer, strlen(buffer), 0);
        }
        fclose(file);
    }
    // Signale la fin de l'historique
    char *end_of_history = "<end_of_history>\n";
    send(client_socket, end_of_history, strlen(end_of_history), 0);

   // Boucle principale de réception
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            printf("Client %s déconnecté.\n", username);
            break;
        }
        buffer[bytes_read] = '\0';

        // Traitement des fichiers
        if (!strncmp(buffer, "/file\n", 6)) {
            char *ptr = buffer + 6;
            char recv_filename[256];
            long filesize;
            int scanned = sscanf(ptr, "%255s\n%ld\n", recv_filename, &filesize);
            if (scanned == 2) {
                forward_file(client_socket, recv_filename, filesize, channel_index);
            }
            continue;
        }

        // Traitement des messages texte
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }

        // Construction du message formaté
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char time_str[9];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", t);

        char full_message[2048];
        snprintf(full_message, sizeof(full_message), "[%s][%s]: %s\n", time_str, username, buffer);

        // Ajout à l'historique et diffusion
        add_message_to_channel(channel_index, full_message);

        pthread_mutex_lock(&channel_lock);
        Channel *channel = &channels[channel_index];
        for (int i = 0; i < channel->client_count; i++) {
            if (channel->clients[i].socket != client_socket) {
                send(channel->clients[i].socket, full_message, strlen(full_message), 0);
            }
        }
        pthread_mutex_unlock(&channel_lock);
    }

    remove_client_from_channel(channel_index, client_socket);
    close(client_socket);
    pthread_exit(NULL);
}

int main() {
    int server_fd, *client_socket;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    // Création du socket serveur
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Échec de la création du socket");
        exit(EXIT_FAILURE);
    }

    // Configuration de l'adresse
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Liaison du socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Échec du bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Mise en écoute
    if (listen(server_fd, MAX_CONNECTIONS) < 0) {
        perror("Échec de l'écoute");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Serveur en attente de connexions...\n");

    // Boucle d'acceptation des connexions
    while (1) {
        client_socket = malloc(sizeof(int));
        if ((*client_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
            perror("Échec de l'accept");
            free(client_socket);
            continue;
        }
        printf("Nouvelle connexion établie.\n");

        // Création d'un thread pour gérer le client
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, client_socket);
        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}

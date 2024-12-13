#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>

#define PORT 8080
#define MAX_CONNECTIONS 100
#define MAX_CHANNELS 100
#define MAX_MESSAGES 1000
#define MAX_MESSAGE_LENGTH 1024
#define MAX_CLIENTS_PER_CHANNEL 100
#define CHUNK_SIZE 8192

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

Channel channels[MAX_CHANNELS];
int channel_count = 0;
pthread_mutex_t channel_lock = PTHREAD_MUTEX_INITIALIZER;

// Fonction pour lire une ligne jusqu'à '\n'
ssize_t read_line(int sock, char *buffer, size_t max_len) {
    ssize_t total = 0;
    while (total < max_len - 1) {
        char c;
        ssize_t bytes = read(sock, &c, 1);
        if (bytes <= 0) {
            if (total == 0) return bytes;
            break;
        }
        if (c == '\n') break;
        buffer[total++] = c;
    }
    buffer[total] = '\0';
    return total;
}

// Fonction pour trouver ou créer un canal
int find_or_create_channel(const char *name) {
    pthread_mutex_lock(&channel_lock);
    for (int i = 0; i < channel_count; i++) {
        if (strcmp(channels[i].name, name) == 0) {
            pthread_mutex_unlock(&channel_lock);
            return i;
        }
    }
    if (channel_count < MAX_CHANNELS) {
        strcpy(channels[channel_count].name, name);
        channels[channel_count].message_count = 0;
        channels[channel_count].client_count = 0;
        pthread_mutex_unlock(&channel_lock);
        return channel_count++;
    }
    pthread_mutex_unlock(&channel_lock);
    return -1;
}

// Fonction pour ajouter un client au canal
void add_client_to_channel(int channel_index, int client_socket, char *username) {
    pthread_mutex_lock(&channel_lock);
    Channel *channel = &channels[channel_index];
    if (channel->client_count < MAX_CLIENTS_PER_CHANNEL) {
        channel->clients[channel->client_count].socket = client_socket;
        strcpy(channel->clients[channel->client_count].username, username);
        channel->client_count++;
    }
    pthread_mutex_unlock(&channel_lock);
}

// Fonction pour retirer un client du canal
void remove_client_from_channel(int channel_index, int client_socket) {
    pthread_mutex_lock(&channel_lock);
    Channel *channel = &channels[channel_index];
    for (int i = 0; i < channel->client_count; i++) {
        if (channel->clients[i].socket == client_socket) {
            for (int j = i; j < channel->client_count - 1; j++) {
                channel->clients[j] = channel->clients[j + 1];
            }
            channel->client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&channel_lock);
}

// Fonction pour ajouter un message au canal
void add_message_to_channel(int channel_index, const char *message) {
    pthread_mutex_lock(&channel_lock);
    Channel *channel = &channels[channel_index];

    if (channel->message_count < MAX_MESSAGES) {
        strcpy(channel->messages[channel->message_count++], message);
    }

    char filename[100];
    snprintf(filename, sizeof(filename), "channel_%s.txt", channel->name);
    FILE *file = fopen(filename, "a");
    if (file != NULL) {
        fprintf(file, "%s", message);
        fclose(file);
    }
    pthread_mutex_unlock(&channel_lock);
}

// Fonction pour transférer un fichier entre clients
void forward_file(int from_socket, const char *filename, long filesize, int channel_index) {
    pthread_mutex_lock(&channel_lock);
    Channel *channel = &channels[channel_index];

    // Envoyer l'en-tête du fichier à tous les clients sauf l'émetteur
    char header[1024];
    snprintf(header, sizeof(header), "/file\n%s\n%ld\n", filename, filesize);

    for (int i = 0; i < channel->client_count; i++) {
        if (channel->clients[i].socket != from_socket) {
            if (send(channel->clients[i].socket, header, strlen(header), 0) < 0) {
                printf("Erreur: Envoi de l'en-tête du fichier à %s a échoué.\n", channel->clients[i].username);
            }
        }
    }

    // Transférer le contenu du fichier
    char buffer[CHUNK_SIZE];
    long remaining = filesize;
    while (remaining > 0) {
        size_t to_read = remaining < CHUNK_SIZE ? remaining : CHUNK_SIZE;
        ssize_t bytes_read = read(from_socket, buffer, to_read);
        if (bytes_read <= 0) {
            printf("Erreur: Réception du fichier interrompue.\n");
            break;
        }

        for (int i = 0; i < channel->client_count; i++) {
            if (channel->clients[i].socket != from_socket) {
                if (send(channel->clients[i].socket, buffer, bytes_read, 0) < 0) {
                    printf("Erreur: Transfert du fichier à %s a échoué.\n", channel->clients[i].username);
                }
            }
        }
        remaining -= bytes_read;
    }

    pthread_mutex_unlock(&channel_lock);
}

// Fonction pour gérer chaque client
void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);
    char buffer[CHUNK_SIZE] = {0};
    int channel_index;
    char username[50];

    // Lire le pseudonyme
    ssize_t bytes = read_line(client_socket, username, sizeof(username));
    if (bytes <= 0) {
        close(client_socket);
        pthread_exit(NULL);
    }

    // Lire le nom du canal
    bytes = read_line(client_socket, buffer, sizeof(buffer));
    if (bytes <= 0) {
        close(client_socket);
        pthread_exit(NULL);
    }

    printf("Utilisateur %s demande de connexion au canal : %s\n", username, buffer);

    channel_index = find_or_create_channel(buffer);
    if (channel_index != -1) {
        add_client_to_channel(channel_index, client_socket, username);

        // Envoyer l'historique des messages
        char filename[100];
        snprintf(filename, sizeof(filename), "channel_%s.txt", channels[channel_index].name);
        FILE *file = fopen(filename, "r");
        if (file != NULL) {
            char line[1024];
            while (fgets(line, sizeof(line), file)) {
                send(client_socket, line, strlen(line), 0);
            }
            fclose(file);
        }

        // Indiquer la fin de l'historique
        char *end_of_history = "<end_of_history>\n";
        send(client_socket, end_of_history, strlen(end_of_history), 0);
    } else {
        char *error_message = "Erreur : Impossible de créer ou rejoindre le canal.\n";
        send(client_socket, error_message, strlen(error_message), 0);
        close(client_socket);
        pthread_exit(NULL);
    }

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            printf("Client %s déconnecté.\n", username);
            break;
        }
        buffer[bytes_read] = '\0';

        // Vérifier si c'est une commande de fichier
        if (strncmp(buffer, "/file\n", 6) == 0) {
            char filename[256];
            long filesize;
            sscanf(buffer + 6, "%s\n%ld\n", filename, &filesize);
            forward_file(client_socket, filename, filesize, channel_index);
            continue;
        }

        // Traiter comme un message normal
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char time_str[9];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", t);

        char full_message[2048];
        snprintf(full_message, sizeof(full_message), "[%s][%s]: %s\n", time_str, username, buffer);

        add_message_to_channel(channel_index, full_message);

        pthread_mutex_lock(&channel_lock);
        Channel *channel = &channels[channel_index];
        for (int i = 0; i < channel->client_count; i++) {
            if (channel->clients[i].socket != client_socket) {
                if (send(channel->clients[i].socket, full_message, strlen(full_message), 0) < 0) {
                    printf("Erreur: Envoi du message à %s a échoué.\n", channel->clients[i].username);
                }
            }
        }
        pthread_mutex_unlock(&channel_lock);
    }

    remove_client_from_channel(channel_index, client_socket);
    close(client_socket);
    pthread_exit(NULL);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Création du socket
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

    // Écoute des connexions entrantes
    if (listen(server_fd, MAX_CONNECTIONS) < 0) {
        perror("Échec de l'écoute");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Serveur en attente de connexions...\n");

    while (1) {
        int *client_socket = malloc(sizeof(int));
        if ((*client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            perror("Échec de l'acceptation");
            free(client_socket);
            continue;
        }

        printf("Nouvelle connexion établie.\n");

        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, client_socket);
        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}
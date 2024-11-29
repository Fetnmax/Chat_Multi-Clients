#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define MAX_CONNECTIONS 10
#define MAX_CHANNELS 100
#define MAX_MESSAGES 1000
#define MAX_MESSAGE_LENGTH 256

typedef struct Channel {
    char name[50];
    char messages[MAX_MESSAGES][MAX_MESSAGE_LENGTH];
    int message_count;
} Channel;

Channel channels[MAX_CHANNELS];
int channel_count = 0;
pthread_mutex_t channel_lock = PTHREAD_MUTEX_INITIALIZER;

// Fonction pour ajouter un message à un canal
void add_message_to_channel(int channel_index, const char *message) {
    pthread_mutex_lock(&channel_lock);
    Channel *channel = &channels[channel_index];
    if (channel->message_count < MAX_MESSAGES) {
        strncpy(channel->messages[channel->message_count], message, MAX_MESSAGE_LENGTH);
        channel->message_count++;
    } else {
        // Décalage des messages si le canal est plein
        for (int i = 1; i < MAX_MESSAGES; i++) {
            strncpy(channel->messages[i - 1], channel->messages[i], MAX_MESSAGE_LENGTH);
        }
        strncpy(channel->messages[MAX_MESSAGES - 1], message, MAX_MESSAGE_LENGTH);
    }
    pthread_mutex_unlock(&channel_lock);
}

// Fonction pour trouver ou créer un canal
int find_or_create_channel(const char *name) {
    pthread_mutex_lock(&channel_lock);
    for (int i = 0; i < channel_count; i++) {
        if (strcmp(channels[i].name, name) == 0) {
            pthread_mutex_unlock(&channel_lock);
            return i; // Canal existant
        }
    }
    // Créer un nouveau canal
    if (channel_count < MAX_CHANNELS) {
        strcpy(channels[channel_count].name, name);
        channels[channel_count].message_count = 0;
        pthread_mutex_unlock(&channel_lock);
        return channel_count++;
    }
    pthread_mutex_unlock(&channel_lock);
    return -1; // Pas de place pour de nouveaux canaux
}

// Fonction exécutée par chaque thread client
void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg); // Libérer l'allocation pour éviter une fuite
    char buffer[1024] = {0};

    // Recevoir le nom du canal du client
    read(client_socket, buffer, 1024);
    printf("Demande de connexion au canal : %s\n", buffer);

    // Trouver ou créer le canal
    int channel_index = find_or_create_channel(buffer);
    if (channel_index != -1) {
        char response[1024];
        snprintf(response, sizeof(response), "Connecté au canal : %s\n", channels[channel_index].name);
        send(client_socket, response, strlen(response), 0);
    } else {
        char *error_message = "Erreur : Impossible de créer ou rejoindre le canal.\n";
        send(client_socket, error_message, strlen(error_message), 0);
    }

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            printf("Client déconnecté.\n");
            break;
        }
        buffer[bytes_read] = '\0';
        printf("Message reçu : %s\n", buffer);

        // Ajouter le message au canal
        add_message_to_channel(channel_index, buffer);

        // Confirmer la réception au client
        char confirmation[] = "Message reçu et ajouté au canal.\n";
        send(client_socket, confirmation, strlen(confirmation), 0);
    }

    // Fermeture du socket
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

    // Écoute
    if (listen(server_fd, MAX_CONNECTIONS) < 0) {
        perror("Échec de l'écoute");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("Serveur en écoute sur le port %d...\n", PORT);

    // Accepter les connexions et créer un thread pour chaque client
    while ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) >= 0) {
        printf("Nouvelle connexion acceptée.\n");
        
        // Allouer dynamiquement le socket pour éviter les conflits entre threads
        int *client_socket = malloc(sizeof(int));
        *client_socket = new_socket;

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, client_socket) != 0) {
            perror("Erreur lors de la création du thread");
            close(new_socket);
            free(client_socket);
        } else {
            pthread_detach(thread_id); // Détacher le thread pour éviter de devoir le rejoindre
        }
    }

    if (new_socket < 0) {
        perror("Erreur lors de l'acceptation");
    }

    close(server_fd);
    return 0;
}
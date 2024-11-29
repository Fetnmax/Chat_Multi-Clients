#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

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

// Fonction pour trouver ou créer un canal
int find_or_create_channel(const char *name) {
    for (int i = 0; i < channel_count; i++) {
        if (strcmp(channels[i].name, name) == 0) {
            return i; // Canal existant
        }
    }
    // Créer un nouveau canal
    if (channel_count < MAX_CHANNELS) {
        strcpy(channels[channel_count].name, name);
        channels[channel_count].message_count = 0;
        return channel_count++;
    }
    return -1; // Pas de place pour de nouveaux canaux
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};
    
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
    
    // Accepter les connexions
    while ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) >= 0) {
        printf("Nouvelle connexion acceptée.\n");
        
        // Recevoir le nom du canal du client
        read(new_socket, buffer, 1024);
        printf("Demande de connexion au canal : %s\n", buffer);
        
        // Trouver ou créer le canal
        int channel_index = find_or_create_channel(buffer);
        if (channel_index != -1) {
            char response[1024];
            snprintf(response, sizeof(response), "Connecté au canal : %s\n", channels[channel_index].name);
            send(new_socket, response, strlen(response), 0);
        } else {
            char *error_message = "Erreur : Impossible de créer ou rejoindre le canal.\n";
            send(new_socket, error_message, strlen(error_message), 0);
        }
        
        close(new_socket);
    }
    
    if (new_socket < 0) {
        perror("Erreur lors de l'acceptation");
    }
    
    close(server_fd);
    return 0;
}
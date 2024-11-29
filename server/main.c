#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define MAX_CONNECTIONS 10

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
    
    // Accepter les connexions
    while ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) >= 0) {
        printf("Nouvelle connexion acceptée.\n");
        
        // Répondre au client
        char *welcome_message = "Bienvenue sur le serveur de chat !\n";
        send(new_socket, welcome_message, strlen(welcome_message), 0);
        
        // Fermeture de la connexion (temporaire, remplacé plus tard par une gestion multiclients)
        close(new_socket);
    }
    
    if (new_socket < 0) {
        perror("Erreur lors de l'acceptation");
    }
    
    close(server_fd);
    return 0;
}
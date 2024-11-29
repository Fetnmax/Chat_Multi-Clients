#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};
    char channel_name[50];
    char message[1024];

    // Création du socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Erreur lors de la création du socket\n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Conversion de l'adresse IP
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("Adresse invalide ou non supportée\n");
        return -1;
    }

    // Connexion au serveur
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Échec de la connexion\n");
        return -1;
    }

    // Demander au client le nom du canal
    printf("Entrez le nom du canal : ");
    fgets(channel_name, sizeof(channel_name), stdin);
    channel_name[strcspn(channel_name, "\n")] = 0; // Retirer le \n

    // Envoyer le nom du canal au serveur
    send(sock, channel_name, strlen(channel_name), 0);

    // Recevoir la réponse du serveur
    read(sock, buffer, sizeof(buffer));
    printf("Message du serveur : %s\n", buffer);

    // Boucle pour envoyer des messages
    while (1) {
        printf("Entrez un message (ou 'exit' pour quitter) : ");
        fgets(message, sizeof(message), stdin);
        message[strcspn(message, "\n")] = 0; // Retirer le \n

        if (strcmp(message, "exit") == 0) {
            printf("Déconnexion...\n");
            break;
        }

        // Envoyer le message au serveur
        send(sock, message, strlen(message), 0);

        // Recevoir la confirmation du serveur
        memset(buffer, 0, sizeof(buffer));
        read(sock, buffer, sizeof(buffer));
        printf("Serveur : %s\n", buffer);
    }

    // Fermeture du socket
    close(sock);
    return 0;
}
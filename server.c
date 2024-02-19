#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10
#define FILE_PATH "example.txt"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void *handle_client(void *arg) {
    int client_socket = *((int *)arg);
    char buffer[BUFFER_SIZE] = {0};

    // Read request from client
    recv(client_socket, buffer, BUFFER_SIZE, 0);
    printf("Received from client: %s\n", buffer);

    // Parse the request to get the HTTP method (GET or POST)
    char method[10];
    char path[BUFFER_SIZE];
    sscanf(buffer, "%s %s", method, path);
    printf("Method: %s, Path: %s\n", method, path);
    if (strcmp(method, "GET") == 0) {
        // Handle GET request
        // Open and read the file
        pthread_mutex_lock(&mutex); // Lock the file to prevent other clients from accessing it
        FILE *file = fopen(path, "r");
        if (file == NULL) { 
            perror("Error opening file");
            pthread_mutex_unlock(&mutex);
            close(client_socket);
            return NULL;
        }

        char file_buffer[BUFFER_SIZE];
        while (fgets(file_buffer, BUFFER_SIZE, file) != NULL) {
            send(client_socket, file_buffer, strlen(file_buffer), 0);
        }

        fclose(file);
        pthread_mutex_unlock(&mutex);
    } else if (strcmp(method, "POST") == 0) {
        // Handle POST request
        // Open file in append mode and write data from client
        pthread_mutex_lock(&mutex);
        FILE *file = fopen(path, "a");
        if (file == NULL) {
            perror("Error opening file");
            pthread_mutex_unlock(&mutex);
            close(client_socket);
            return NULL;
        }

        char *data = strstr(buffer, "\r\n\r\n") + 4; // Locate start of POST data
        fputs(data, file);

        fclose(file);
        pthread_mutex_unlock(&mutex);
    }

    // Close client connection
    close(client_socket);
    return NULL;
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Create server socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Error creating socket");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    int enable = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

    // Bind server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_socket, 5) < 0) {
        perror("Error listening");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port 8080...\n");

    while (1) {
        // Accept client connection
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("Error accepting connection");
            close(server_socket);
            exit(EXIT_FAILURE);
        }

        // Handle client request in a separate thread
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, &client_socket);
        pthread_detach(tid); 
    }

    close(server_socket);
    return 0;
}

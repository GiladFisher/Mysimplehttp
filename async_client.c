#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

// Structure to hold download information
typedef struct {
    char filename[256];
} DownloadInfo;

// Function to send request to server
void send_request(const char *request) {
    int client_socket;
    struct sockaddr_in server_addr;

    // Create socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(SERVER_PORT);

    // Connect to server
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error connecting to server");
        exit(EXIT_FAILURE);
    }

    // Send request to server
    if (send(client_socket, request, strlen(request), 0) < 0) {
        perror("Error sending request");
        exit(EXIT_FAILURE);
    }

    // Receive response from server
    char buffer[BUFFER_SIZE] = {0};
    while (recv(client_socket, buffer, BUFFER_SIZE, 0) > 0) {
        printf("%s", buffer);
        memset(buffer, 0, BUFFER_SIZE);
    }

    // Close socket
    close(client_socket);
}

// Function to download file
void *download_file(void *arg) {
    DownloadInfo *info = (DownloadInfo *)arg;
    printf("Downloading file: %s\n", info->filename);
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "GET %s HTTP/1.1\r\nHost: localhost\r\n\r\n", info->filename);
    int status = system(command);
    if (status != 0) {
        fprintf(stderr, "Error downloading file: %s\n", info->filename);
    }
    else{
        printf("Download complete: %s\n", info->filename);
    }
    free(info);
    return NULL;
}

int main() {
    // Request file list from server
    printf("Requesting file list...\n");
    send_request("GET /files.list HTTP/1.1\r\nHost: localhost\r\n\r\n");

    // Parse file list and initiate download for each file concurrently
    char *file_list = "file1.txt\nfile2.txt\nfile3.txt"; // Example file list received from server
    char *filename = strtok(file_list, "\n");
    while (filename != NULL) {
        // Create download information
        DownloadInfo *info = (DownloadInfo *)malloc(sizeof(DownloadInfo));
        strcpy(info->filename, filename);

        // Create thread for downloading file
        pthread_t tid;
        if (pthread_create(&tid, NULL, download_file, info) != 0) {
            fprintf(stderr, "Error creating thread for file: %s\n", filename);
            free(info);
        }

        // Move to next filename
        filename = strtok(NULL, "\n");
    }

    // Wait for all threads to finish
    pthread_exit(NULL);

    return 0;
}

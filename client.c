#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080

#define BUFFER_SIZE 1024

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

int main(int argc, char *argv[]) {
    if(argc < 3) {
        printf("GET Example: %s GET <path>\n", argv[0]);
        printf("POST Example: %s POST <path> <data path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    // Create socket
    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection Failed");
        exit(EXIT_FAILURE);
    }

    // Prepare request based on command line arguments:
    char buffer[BUFFER_SIZE] = {0};
    
    if (strcmp(argv[1], "GET") == 0) {
        snprintf(buffer, sizeof(buffer), "GET %s\r\n\r\n", argv[2]);

        send(sock, buffer, strlen(buffer), 0);
        printf("GET Request Sent: %s", buffer);
    }
    else if (strcmp(argv[1], "POST") == 0 && argc == 4) {
        // Create and open a unique temporary file for the base64 encoded data:
        char temp_file_template[] = "/tmp/tempfileXXXXXX";
        
        int temp_fd = mkstemp(temp_file_template);
        if (temp_fd == -1) {
            close(sock);
            perror("Error generating temp file");
            return NULL;
        }
        close(temp_fd);

        // Encode the file:
        char command[512];
        snprintf(command, sizeof(command), "base64 %s > %s", argv[2], temp_file_template);
        if (system(command) != 0) {
            close(sock);
            unlink(temp_file_template);
            perror("Error encoding file");
            return NULL;
        }

        FILE *encoded_file = fopen(temp_file_template, "r");
        if (encoded_file == NULL) { // Corrected variable name
            close(sock);
            unlink(temp_file_template);
            perror("Error opening encoded file");
            return NULL;
        }

        char buffer[BUFFER_SIZE-12]; // Example buffer size for file reading
        char request_buffer[BUFFER_SIZE]; // Buffer for formatted request
        int bytes_read;
        int first_packet = 1; // Flag to indicate the first packet

        bytes_read = fread(buffer, 1, sizeof(buffer) - 1, encoded_file)

        if (feof(encoded_file)) {
            // Handling the edge case where only one packet is needed, this means the file was empty or very small.
            snprintf(request_buffer, sizeof(request_buffer), "POST %s\r\n%s\r\n\r\n", argv[2], buffer);
            send(sock, request_buffer, strlen(request_buffer), 0);
        }
        else {
            // Format for the first packet, assuming there are multiple packets.
            snprintf(request_buffer, sizeof(request_buffer), "POST %s\r\n%s", argv[2], buffer);
            send(sock, request_buffer, strlen(request_buffer), 0);

            while ((bytes_read = fread(buffer, 1, sizeof(buffer) - 1, encoded_file)) > 0) {
                buffer[bytes_read] = '\0'; // Ensure the buffer is null-terminated

                if (feof(encoded_file)) {
                    // Format for the last packet
                    snprintf(request_buffer, sizeof(request_buffer), "%s\r\n\r\n", buffer);
                } else {
                    // Format for intermediate packets or the single packet that fits everything
                    strncpy(request_buffer, buffer, sizeof(request_buffer));
                }

                send(sock, request_buffer, strlen(request_buffer), 0);
            }
        }

        fclose(encoded_file); // Close the file when done
        unlink(temp_file_template); // Remove the temporary file
    }
    else {
        printf("Invalid command or insufficient arguments.\n");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Await and process the server's response
    int read_bytes = read(sock, buffer, sizeof(buffer) - 1);
    if (read_bytes > 0) {
        buffer[read_bytes] = '\0'; // Ensure null-termination
        printf("Server response:\n%s\n", buffer);

        // If it's a GET request, decode the Base64 content
        if (strcmp(argv[1], "GET") == 0) {
            // Find the start of the Base64 content
            char *base64_start = strstr(buffer, "\r\n") + 2;
            if (base64_start) {
                char decoded_content[BUFFER_SIZE] = {0};
                if (decode_base64(base64_start, decoded_content, sizeof(decoded_content))) {
                    printf("Decoded content:\n%s\n", decoded_content);
                } else {
                    fprintf(stderr, "Failed to decode content.\n");
                }
            }
        }
    } else {
        perror("Read error");
    }

    close(sock);

    return 0;
}
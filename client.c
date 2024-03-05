#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if(argc < 3) {
        printf("GET Example: %s GET <path>\n", argv[0]);
        printf("POST Example: %s POST <path on server> <input path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    // Create socket
    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    // Initialize server address structure
    struct sockaddr_in server_addr;
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection Failed");
        exit(EXIT_FAILURE);
    }

    // Prepare request based on command line arguments:
    char buffer[BUFFER_SIZE] = {0};
    
    if (strcmp(argv[1], "GET") == 0 && argc == 3) {
        snprintf(buffer, sizeof(buffer), "GET %s\r\n\r\n", argv[2]);

        send(sock, buffer, strlen(buffer), 0);
        usleep(1000);

        printf("Request Sent: %s", buffer);

        // Receive and process the server's response
        int read_bytes;
        char response_buffer[BUFFER_SIZE+1];

        char temp_file_name[] = "temp.txt";
        FILE *temp_file = fopen(temp_file_name, "w+");

        // Read response header
        read_bytes = read(sock, response_buffer, BUFFER_SIZE);
        if (read_bytes > 0) {
            response_buffer[read_bytes] = '\0'; // Ensure null-termination
            //printf("Server response:\n");

            // Check if it's a "200 OK" response
            if (strstr(response_buffer, "200 OK") != NULL) {
                int end_of_message = 0;

                while (!end_of_message) {
                    // Read data from the server
                    read_bytes = read(sock, response_buffer, BUFFER_SIZE);
                    response_buffer[read_bytes] = '\0'; // Ensure null-termination

                    // Write received data to the temporary file, excluding the initial "200 OK\r\n" and the "\r\n\r\n" at the end
                    if (strstr(response_buffer, "\r\n\r\n") != NULL) {
                        char *end = strstr(response_buffer, "\r\n\r\n");
                        fwrite(response_buffer, sizeof(char), (end - response_buffer), temp_file);
                        //printf("%s", response_buffer);

                        end_of_message = 1;
                    }
                    else {
                        fwrite(response_buffer, sizeof(char), read_bytes, temp_file);
                        //printf("%s", response_buffer);
                    }
                }

                fclose(temp_file);

                printf("File contents:\n");

                char command[512];
                snprintf(command, sizeof(command), "base64 --decode temp.txt");
                if (system(command) != 0) {
                    printf("Error decoding file.");
                    close(sock);
                    exit(EXIT_FAILURE);
                }
                printf("\n");

                if (remove(temp_file_name) != 0) {
                    perror("Error deleting file");
                    return 1;
                }
            } else {
                printf("Failed to receive message: %s\n", response_buffer);
            }
        } else {
            perror("Read error");
        }
    }
    else if (strcmp(argv[1], "POST") == 0 && argc == 4) {
        // check that the file exists:
        FILE *file = fopen(argv[3], "r");
        if (file == NULL) { 
            perror("Error opening file");
            close(sock);
            return NULL;
        }
        fclose(file);

        // Create and open a unique temporary file for the base64 encoded data:
        char temp_file_template[] = "/tmp/tempfileXXXXXX";
        
        int temp_fd = mkstemp(temp_file_template);
        if (temp_fd == -1) {
            perror("Error generating temp file");
            close(sock);
            return NULL;
        }
        close(temp_fd);

        // Encode the file:
        char command[512];
        snprintf(command, sizeof(command), "base64 %s > %s", argv[3], temp_file_template);
        if (system(command) != 0) {
            perror("Error encoding file");
            unlink(temp_file_template);
            close(sock);
            return NULL;
        }

        FILE *encoded_file = fopen(temp_file_template, "r");
        if (file == NULL) {
            perror("Error opening encoded file");
            unlink(temp_file_template);
            close(sock);
            return NULL;
        }

        // Send a POST request:
        char request_buffer[BUFFER_SIZE];
        snprintf(request_buffer, sizeof(request_buffer), "POST %s\r\n", argv[2]);
        send(sock, request_buffer, strlen(request_buffer), 0);
        usleep(1000);

        // Read the file and send its Base64-encoded content:
        int read;
        char encoded_buffer[BUFFER_SIZE];
        while ((read = fread(encoded_buffer, sizeof(char), BUFFER_SIZE, encoded_file)) > 0) {
            if (read < BUFFER_SIZE) {
                memcpy(encoded_buffer + read, "\r\n\r\n", 4);
                read += 4;
                send(sock, encoded_buffer, BUFFER_SIZE, 0);
            }
            else {
                send(sock, encoded_buffer, BUFFER_SIZE, 0);
                usleep(1000);
            }
        }

        fclose(encoded_file);
        unlink(temp_file_template);
    }
    else {
        printf("Invalid command or insufficient arguments.\n");
        close(sock);
        exit(EXIT_FAILURE);
    }

    close(sock);

    return 0;
}
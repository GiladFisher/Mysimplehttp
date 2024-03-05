#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>  // Include the poll header

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080

#define BUFFER_SIZE 1024

// Function to handle reading from the server socket and writing to the file
void handle_response(int sock) {
    int read_bytes;
    char response_buffer[BUFFER_SIZE + 1];

    // Create a temporary file for storing the downloaded content
    char temp_file_template[] = "/tmp/tempfileXXXXXX";
    int temp_fd = mkstemp(temp_file_template);
    if (temp_fd == -1) {
        perror("Error generating temp file");
        return NULL;
    }

    // Read response header
    read_bytes = read(sock, response_buffer, BUFFER_SIZE);
    if (read_bytes > 0) {
        response_buffer[read_bytes] = '\0'; // Ensure null-termination
        //printf("Server response:\n");

        // Check if it's a "200 OK" response
        if (strstr(response_buffer, "200 OK") != NULL) {
            int end_of_message = 0;

            // Handle case where we got one packet
            if (strstr(response_buffer, "\r\n\r\n") != NULL) {
                char *start = strstr(response_buffer, "\r\n") + 2;
                char *end = strstr(response_buffer, "\r\n\r\n");
                write(temp_fd, start, (end - start));

                end_of_message = 1;
            }

            while (!end_of_message) {
                // Read data from the server
                read_bytes = read(sock, response_buffer, BUFFER_SIZE);
                response_buffer[read_bytes] = '\0'; // Ensure null-termination

                // Write received data to the temporary file, excluding the initial "200 OK\r\n" and the "\r\n\r\n" at the end
                if (strstr(response_buffer, "\r\n\r\n") != NULL) {
                    char *end = strstr(response_buffer, "\r\n\r\n");
                    write(temp_fd, response_buffer, (end - response_buffer));
                    //printf("%s", response_buffer);

                    end_of_message = 1;
                }
                else {
                    write(temp_fd, response_buffer, read_bytes);
                    //printf("%s", response_buffer);
                }
            }
            close(temp_fd);

            printf("\nFile contents:\n");

            char command[512];
            snprintf(command, sizeof(command), "base64 --decode %s", temp_file_template);
            if (system(command) != 0) {
                printf("Error decoding file.");
                unlink(temp_file_template);
                exit(EXIT_FAILURE);
            }
            printf("\n");

            if (remove(temp_file_template) != 0) {
                perror("Error deleting file");
                unlink(temp_file_template);
                return 1;
            }
        } else {
            printf("Failed to receive message: %s\n", response_buffer);
        }
    } else {
        perror("Read error");
    }

    unlink(temp_file_template);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <file_list>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Open the file list
    FILE *file_list = fopen(argv[1], "r");
    if (file_list == NULL) {
        perror("Error opening file list");
        exit(EXIT_FAILURE);
    }

    char filename[BUFFER_SIZE];
    int num_fds = 0;
    struct pollfd *fds = malloc(sizeof(struct pollfd)); // Allocate memory for the pollfd structure

    while (fgets(filename, BUFFER_SIZE, file_list) != NULL) {
        // Remove trailing newline character
        strtok(filename, "\n");

        // Create socket
        int sock;
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("Socket creation error");
            continue;
        }

        // Enable TCP Keepalive on the socket
        int enable = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable)) < 0) {
            perror("setsockopt(SO_KEEPALIVE) failed");
            // Optionally handle this error; it's typically not fatal
        }

        // Initialize server address structure
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(SERVER_PORT);

        // Convert IP address to binary format
        if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
            perror("Invalid address/ Address not supported");
            close(sock);
            continue;
        }

        // Connect to the server
        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("Connection Failed");
            close(sock);
            continue;
        }

        // Send GET request for the current filename
        char request_buffer[BUFFER_SIZE];
        snprintf(request_buffer, BUFFER_SIZE, "GET %s\r\n\r\n", filename);
        send(sock, request_buffer, strlen(request_buffer), 0);

        // Add the socket to the pollfd structure
        fds = realloc(fds, (num_fds + 1) * sizeof(struct pollfd));
        fds[num_fds].fd = sock;
        fds[num_fds].events = POLLIN;
        num_fds++;
    }

    fclose(file_list);

    // Use poll to wait for data on multiple sockets
    while (num_fds > 0) {
        int ret = poll(fds, num_fds, -1);
        if (ret < 0) {
            perror("Poll error");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < num_fds; i++) {
            if (fds[i].revents & POLLIN) {
                handle_response(fds[i].fd);
                close(fds[i].fd); // Close the processed socket

                // Move the last socket in the array to the current position if not the same
                if (i != num_fds - 1) {
                    fds[i] = fds[num_fds - 1];
                }
                num_fds--; // Now decrement num_fds
                i--; // Decrement i to ensure the newly moved socket (if any) is processed
            }
        }
    }

    free(fds); // Free dynamically allocated memory
    return 0;
}

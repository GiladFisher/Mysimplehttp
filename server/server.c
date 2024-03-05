#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define BUFFER_SIZE 1024
#define MAX_QUEUE 10

#define SERVER_PORT 8080

// Structure to pass multiple arguments to the thread handling client requests
typedef struct {
    int client_socket;      // Socket descriptor for the client connection
    char *root_directory;   // The root directory from which files are served
} client_handle_args;


void *handle_client(void *arg) {
    client_handle_args *args = (client_handle_args *)arg;
    int client_socket = args->client_socket;
    char *root_directory = args->root_directory;

    char buffer[BUFFER_SIZE] = {0};

    // Receive the request from the client:
    recv(client_socket, buffer, BUFFER_SIZE, 0);
    printf("Received from client: %s", buffer);

    // Parse the request to get the HTTP method (GET or POST)
    char method[10];
    char path[BUFFER_SIZE];
    sscanf(buffer, "%s %s", method, path);

    // // Construct the full file path by concatenating the root directory and the path from the request:
    char full_path[BUFFER_SIZE];
    snprintf(full_path, sizeof(full_path), "%s%s", root_directory, path);

    //printf("Method: %s, Path: %s\n", method, full_path);
    
    if (strcmp(method, "GET") == 0) {
        // check that the file exists:
        FILE *file = fopen(full_path, "r");
        if (file == NULL) { 
            // Send a 404 response if the file cannot be found:
            const char *not_found_response = "404 FILE NOT FOUND\r\n\r\n";
            send(client_socket, not_found_response, strlen(not_found_response), 0);
            
            perror("Error opening file");
            return NULL;
        }
        fclose(file);

        // Create and open a unique temporary file for the base64 encoded data:
        char temp_file_template[] = "/tmp/tempfileXXXXXX";
        
        int temp_fd = mkstemp(temp_file_template);
        if (temp_fd == -1) {
            // Send a 500 response if a temporary file cannot be created:
            const char *internal_error_response = "500 INTERNAL ERROR\r\n\r\n";
            send(client_socket, internal_error_response, strlen(internal_error_response), 0);

            perror("Error generating temp file");
            return NULL;
        }
        close(temp_fd);

        // Encode the file:
        char command[512];
        snprintf(command, sizeof(command), "base64 %s > %s", full_path, temp_file_template);
        if (system(command) != 0) {
            // Send a 500 response if file cannot be encoded:
            const char *internal_error_response = "500 INTERNAL ERROR\r\n\r\n";
            send(client_socket, internal_error_response, strlen(internal_error_response), 0);

            perror("Error encoding file");
            unlink(temp_file_template);
            fclose(file);
            return NULL;
        }

        FILE *encoded_file = fopen(temp_file_template, "r");
        if (file == NULL) {
            // Send a 500 response if the file cannot be opened:
            const char *internal_error_response = "500 INTERNAL ERROR\r\n\r\n";
            send(client_socket, internal_error_response, strlen(internal_error_response), 0);

            perror("Error opening encoded file");
            unlink(temp_file_template);
            fclose(file);
            return NULL;
        }

        // Send a 200 OK response header before sending the file content:
        const char *ok_response = "200 OK\r\n";
        send(client_socket, ok_response, strlen(ok_response), 0);
        usleep(1000);

        // Read the file and send its Base64-encoded content:
        int read;
        char encoded_buffer[BUFFER_SIZE];
        while ((read = fread(encoded_buffer, sizeof(char), BUFFER_SIZE, encoded_file)) > 0) {
            if (read < BUFFER_SIZE) {
                memcpy(encoded_buffer + read, "\r\n\r\n", 4);
                read += 4;
                send(client_socket, encoded_buffer, BUFFER_SIZE, 0);
            }
            else {
                send(client_socket, encoded_buffer, BUFFER_SIZE, 0);
                usleep(1000);
            }
        }

        printf("File sent.\n");

        fclose(encoded_file);
        unlink(temp_file_template);
    }

    else if (strcmp(method, "POST") == 0) {
        // Lock the file:
        struct flock lock;
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK; // Exclusive lock
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0; // Lock the whole file

        // Added O_CREAT to create the file if it doesn't exist and 0666 for proper permissions
        int file_fd = open(full_path, O_RDWR | O_CREAT, 0666);
        if (file_fd < 0) {
            perror("Error opening/creating file");
            // Handle error, such as sending a 500 response and returning from the function
        }

        if (fcntl(file_fd, F_SETLK, &lock) == -1) {
            perror("Failed to lock the file");
            close(file_fd);
            exit(EXIT_FAILURE);
        }

        // Create and open a unique temporary file for the base64 encoded data:
        char temp_file_template[] = "/tmp/tempfileXXXXXX";
        
        int temp_fd = mkstemp(temp_file_template);
        if (temp_fd == -1) {
            // Send a 500 response if a temporary file cannot be created:
            const char *internal_error_response = "500 INTERNAL ERROR\r\n\r\n";
            send(client_socket, internal_error_response, strlen(internal_error_response), 0);

            perror("Error generating temp file");

            lock.l_type = F_UNLCK;
            if (fcntl(file_fd, F_SETLK, &lock) == -1) {
                perror("Failed to unlock the file");
            }
            close(file_fd);

            exit(EXIT_FAILURE);
        }
        close(temp_fd);

        FILE *temp_file = fopen(temp_file_template, "w+");

        int read_bytes;
        char response_buffer[BUFFER_SIZE+1];

        int end_of_message = 0;
        while (!end_of_message) {
            // Read data from the server
            read_bytes = recv(client_socket, response_buffer, BUFFER_SIZE, 0);
            response_buffer[read_bytes] = '\0'; // Ensure null-termination

            // Write received data to the temporary file, excluding the initial "200 OK\r\n" and the "\r\n\r\n" at the end
            if (strstr(response_buffer, "\r\n\r\n") != NULL) {
                char *end = strstr(response_buffer, "\r\n\r\n");
                fwrite(response_buffer, sizeof(char), (end - response_buffer), temp_file);
                //printf("%s\n", response_buffer);

                end_of_message = 1;
            }
            else {
                fwrite(response_buffer, sizeof(char), read_bytes, temp_file);
                //printf("%s", response_buffer);
            }
        }

        fclose(temp_file);

        char command[512];
        snprintf(command, sizeof(command), "base64 --decode %s > %s", temp_file_template, full_path);
        if (system(command) != 0) {
            printf("Error decoding file.");
            
            lock.l_type = F_UNLCK;
            if (fcntl(file_fd, F_SETLK, &lock) == -1) {
                perror("Failed to unlock the file");
            }
            close(file_fd);

            unlink(temp_fd);
            exit(EXIT_FAILURE);
        }

        // Unlock the file
        lock.l_type = F_UNLCK;
        if (fcntl(file_fd, F_SETLK, &lock) == -1) {
            perror("Failed to unlock the file");
        }
        close(file_fd);

        unlink(temp_file_template);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    // Ensure the server is started with a root directory argument:
    if (argc != 2) {
        printf("Usage: %s <root_directory>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *root_directory = argv[1];

    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Create and configure the server socket:
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Error creating socket");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Allow the socket to be rapidly reused during server restarts:
    int enable = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    // Enable TCP Keepalive on the socket
    enable = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));

    // Bind the server socket to a port:
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections:
    if (listen(server_socket, MAX_QUEUE) < 0) {
        perror("Error listening");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port 8080...\n");

    while (1) {
        // Accept incoming client connections and handle them in separate threads:

        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("Error accepting connection");
            close(server_socket);
            exit(EXIT_FAILURE);
        }

        client_handle_args *args = malloc(sizeof(client_handle_args));
        if (args == NULL) {
            perror("Failed to allocate memory for thread arguments");
            close(client_socket);
            continue; // Or handle error more gracefully
        }
        args->client_socket = client_socket;
        args->root_directory = root_directory;

        // Handle client request in a separate process
        pid_t pid = fork();
        if (pid == -1) {
            perror("Failed to create process");
            free(args);
            close(client_socket);
        } else if (pid == 0) {
            // Child process
            handle_client(args);
            free(args);
            close(client_socket);
            exit(EXIT_SUCCESS);
        } else {
            // Parent process
        }
    }

    close(server_socket);

    return 0;
}

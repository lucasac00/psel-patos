#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/limits.h>
#include <sys/sendfile.h>


#define PORT 8000
#define ROOT_DIR "./www"
#define BUFFER_SIZE 4096

// Auxiliary function to resolve paths safely
int is_path_safe(const char *root, const char *requested_path, char *resolved_path, size_t resolved_size) {
    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s%s", root, requested_path);

    // Normalize the path using realpath
    // I was curious abt this one so https://man7.org/linux/man-pages/man3/realpath.3.html
    if (!realpath(full_path, resolved_path)) {
        return 0;
    }

    // Make sure the resolved path starts with the root path
    char root_resolved[PATH_MAX];
    if (!realpath(root, root_resolved)) {
        return 0;
    }

    return strncmp(resolved_path, root_resolved, strlen(root_resolved)) == 0;
}


// Auxiliary function to get the MIME type based on file extension
// TODO: add more of them - https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/MIME_types/Common_types
const char* get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream"; // default binary

    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)  return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcmp(ext, ".txt") == 0) return "text/plain";
    if (strcmp(ext, ".pdf") == 0) return "application/pdf";

    return "application/octet-stream"; // fallback
}

void send_response(int sockfd, const char *status, const char *content_type, const char *body) {
    // Construct the HTTP response
    char response[BUFFER_SIZE];
    int length = snprintf(response, BUFFER_SIZE,
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n\r\n"
        "%s",
        status, content_type, strlen(body), body);
    
    send(sockfd, response, length, 0);
}

void serve_file(int sockfd, const char *path) {
    // Open the file in read-only mode
    int filefd = open(path, O_RDONLY);
    if (filefd == -1) {
        send_response(sockfd, "404 Not Found", "text/plain", "File not found");
        return;
    }

    struct stat stat_buf;
    fstat(filefd, &stat_buf);

    // Get the MIME type
    const char *mime_type = get_mime_type(path);
    
    // Send the headers
    char headers[BUFFER_SIZE];
    int headers_len = snprintf(headers, BUFFER_SIZE,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n\r\n",
        mime_type, stat_buf.st_size);
    
    send(sockfd, headers, headers_len, 0);
    
    // Send the file content
    off_t offset = 0;
    while (offset < stat_buf.st_size) {
        ssize_t sent = sendfile(sockfd, filefd, &offset, stat_buf.st_size - offset);
        if (sent == -1) break;
    }
    
    close(filefd);
}

void handle_request(int sockfd) {
    char buffer[BUFFER_SIZE];
    // recv() will block until data is received
    ssize_t bytes_read = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytes_read < 0) {
        perror("recv failed");
        return;
    }
    
    // Null-terminate the buffer to make it a valid string
    buffer[bytes_read] = '\0';
    
    // Req parsing
    char *method = strtok(buffer, " ");
    char *path = strtok(NULL, " ");
    
    if (!method || !path) {
        send_response(sockfd, "400 Bad Request", "text/plain", "Invalid request");
        return;
    }
    
    // Implementation w/out realpath and path checking
    // char full_path[256];
    // snprintf(full_path, sizeof(full_path), "%s%s", ROOT_DIR, path);

    // Get file path with path checking
    char resolved_path[PATH_MAX];
    if (!is_path_safe(ROOT_DIR, path, resolved_path, sizeof(resolved_path))) {
        send_response(sockfd, "403 Forbidden", "text/plain", "Access denied");
        return;
    }
    
    // Send file
    serve_file(sockfd, resolved_path);
}

int main() {
    // AF_INET is used for IPv4 addresses
    // SOCK_STREAM is used for TCP connections
    int s = socket(AF_INET, SOCK_STREAM, 0);
    // sockaddr_in structure used for storing ipv4 address information
    struct sockaddr_in addr;
    
    if (s < 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }
    
    // set the socket option to allow address reuse, getting rid of the “Address already in use.” message
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; // AF_INET for IPv4
    addr.sin_addr.s_addr = INADDR_ANY; // INADDR_ANY for all interfaces
    addr.sin_port = htons(PORT); // port number in network byte order

    // Now that we have a socket and address information, we can bind it, that is
    // associate the socket with a port in the local machine
    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Failed to bind socket");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    // Second parameter is 'backlog', the number of connections allowed on the queue
    // Keep it 10 for now, for simplicity
    if (listen(s, 10) < 0) {
        perror("Failed to listen on socket");
        exit(EXIT_FAILURE);
    }

    printf("Backend server running on port %d\n", PORT);
    
    while (1) {
        struct sockaddr_in client_addr;
        // socklen_t is an unsigned int, used as one of the function parameters of accept()
        socklen_t client_len = sizeof(client_addr);
        // accept() returns a new socket file descriptor for the incoming connection
        int client_s = accept(s, (struct sockaddr*)&client_addr, &client_len);

        if (client_s < 0) {
            perror("Failed to accept connection");
            continue; // don't exit the code, we want to keep listening!
        }
        
        printf("Request from %s\n", inet_ntoa(client_addr.sin_addr));
        
        if (!fork()) {
            close(s); // Close the listening socket in the child process
            handle_request(client_s);
            exit(EXIT_SUCCESS);
        }
        close(client_s); // Close the client socket in the parent process
    }
    
    // Close the listening socket
    close(s);
    return 0;
}
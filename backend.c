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
#include <dirent.h> // for directory operations (list files)

#define PORT 8000
#define ROOT_DIR "./www"
#define BUFFER_SIZE 4096

// Auxiliary function to resolve paths safely
int is_path_safe(const char *root, const char *requested_path, char *resolved_path) {
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

void send_response(int s, const char *status, const char *content_type, const char *body) {
    // Construct the HTTP response
    char response[BUFFER_SIZE];
    int length = snprintf(response, BUFFER_SIZE,
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n\r\n"
        "%s",
        status, content_type, strlen(body), body);
    
    send(s, response, length, 0);
}

void serve_file(int s, const char *path) {
    // Open the file in read-only mode
    int filefd = open(path, O_RDONLY);
    if (filefd == -1) {
        send_response(s, "404 Not Found", "text/plain", "File not found");
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
    
    send(s, headers, headers_len, 0);
    
    // Send the file content
    off_t offset = 0;
    while (offset < stat_buf.st_size) {
        ssize_t sent = sendfile(s, filefd, &offset, stat_buf.st_size - offset);
        if (sent == -1) break;
    }
    
    close(filefd);
}

// Auxiliary function to return a list of all files in ROOT_DIR
void list_files(int s) {
    // DIR data structure from dirent.h allows for directory operations
    DIR *dir;
    struct dirent *entry;
    char filelist[BUFFER_SIZE] = "";

    dir = opendir(ROOT_DIR);
    if (!dir) {
        send_response(s, "500 Internal Server Error", "text/plain", "Unable to open directory");
        return;
    }

    strcat(filelist, "File List:\n");
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." (current folder) and ".." (parent folder) 
        // which are included in the DIR file list for some reason
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            // Maybe too big? Idk if there's a better way to do this if there's massive filenames
            char line[512];
            snprintf(line, sizeof(line), "%s\n", entry->d_name);
            strcat(filelist, line);
        }
    }

    strcat(filelist, "\n");
    closedir(dir);
    send_response(s, "200 OK", "text/plain", filelist);
}

void handle_upload(int s, char *request) {
    // RAW REQUEST:
    // POST /upload HTTP/1.1
    // Host:
    // User-Agent:
    // Accept:
    // Content-Length:
    // Content-Type: multipart/form-data; boundary=------------------------75f587bf0f612a9e

    // --------------------------75f587bf0f612a9e
    // Content-Disposition: form-data; name="file"; filename="test.txt"
    // Content-Type: text/plain

    // Test

    // --------------------------75f587bf0f612a9e--

    // First, let's extract the boundary:
    // strstr() is used to find the first occurrence of a substring in a string
    // and returns a pointer to it.
    const char *boundary = strstr(request, "boundary=");
    if (!boundary) {
        send_response(s, "400 Bad Request", "text/plain", "No boundary found");
        return;
    }
    boundary += strlen("boundary=");
    const char *boundary_end = strstr(boundary, "\r\n\r\n");
    char boundary_delim[100];
    // Go to the end of the line and copy the boundary delimiter to boundary_delim
    snprintf(boundary_delim, boundary_end - boundary + 1, "%s", boundary);

    // Now, let's find the start of the body
    const char *body = strstr(request, "\r\n\r\n");
    if (!body) {
        send_response(s, "400 Bad Request", "text/plain", "Invalid multipart body");
        return;
    }
    body += 4; // skip past \r\n\r\n

    // And skip past the delimiter
    const char *part_start = strstr(body, boundary_delim);
    if (!part_start) {
        send_response(s, "400 Bad Request", "text/plain", "Boundary not found in body");
        return;
    }

    part_start += strlen(boundary_delim) + 2; // skip \r\n
    const char *headers_end = strstr(part_start, "\r\n\r\n");
    if (!headers_end) {
        send_response(s, "400 Bad Request", "text/plain", "Missing part headers");
        return;
    }

    // Now we can extract the filename
    // Look for filename in Content-Disposition
    const char *filename_start = strstr(part_start, "filename=\"");
    if (!filename_start) {
        send_response(s, "400 Bad Request", "text/plain", "Filename missing in upload");
        return;
    }
    filename_start += strlen("filename=\"");
    const char *filename_end = strchr(filename_start, '"');
    if (!filename_end) {
        send_response(s, "400 Bad Request", "text/plain", "Malformed filename");
        return;
    }

    // Saving the filename
    char filename[256];
    snprintf(filename, filename_end - filename_start + 1, "%s", filename_start);

    // Finding the end of content-type
    const char *content_type_start = strstr(part_start, "Content-Type: ");
    if (!content_type_start) {
        send_response(s, "400 Bad Request", "text/plain", "Content-Type missing in upload");
        return;
    }
    const char *content_type_end = strstr(part_start, "\r\n\r\n");
    if (!content_type_end) {
        send_response(s, "400 Bad Request", "text/plain", "Couldn't fint Content-Type end");
        return;
    }

    const char* file_content = content_type_end + 4;
    //printf("File content starts at: %s\n", file_content);
    // Finally, we're at the start of the actual file content.
    // Find the end of the part (boundary again)
    char boundary_search[128];
    snprintf(boundary_search, sizeof(boundary_search), "--%s--", boundary_delim);
    //printf("Boundary search: %s\n", boundary_search);
    const char *part_end = strstr(file_content, boundary_search);
    if (!part_end) {
        send_response(s, "400 Bad Request", "text/plain", "Could not find end of part");
        return;
    }
    //printf("File content ends at: %s\n", part_end);
    // Save file
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", ROOT_DIR, filename);
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        send_response(s, "500 Internal Server Error", "text/plain", "Could not write file");
        return;
    }

    fwrite(file_content, 1, part_end - file_content, fp);
    fclose(fp);

    send_response(s, "200 OK", "text/plain", "File uploaded successfully");
}

void handle_request(int s) {
    char buffer[BUFFER_SIZE];
    // recv() will block until data is received
    ssize_t bytes_read = recv(s, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytes_read < 0) {
        perror("recv failed");
        return;
    }
    // Null-terminate the buffer to make it a valid string
    buffer[bytes_read] = '\0';
    printf("Received request:\n%s\n", buffer);
    
    // Req parsing, HTTP method and path
    char buffer_copy[BUFFER_SIZE];
    strncpy(buffer_copy, buffer, sizeof(buffer_copy));
    
    // Parse method/path from the COPY
    char *method = strtok(buffer_copy, " ");
    char *path = strtok(NULL, " ");
    
    if (!method || !path) {
        send_response(s, "400 Bad Request", "text/plain", "Invalid request");
        return;
    }

    // Request for list of available files
    if (strcmp(path, "/list") == 0) {
        list_files(s);
        return;
    }

    // Request for uploading a file
    if (strcmp(method, "POST") == 0 && strcmp(path, "/upload") == 0) {
        printf("Upload request received\n");
        handle_upload(s, buffer);
        return;
    }
    
    // Implementation w/out realpath and path checking
    // char full_path[256];
    // snprintf(full_path, sizeof(full_path), "%s%s", ROOT_DIR, path);

    // Get file path with path checking
    char resolved_path[PATH_MAX];
    if (!is_path_safe(ROOT_DIR, path, resolved_path)) {
        send_response(s, "403 Forbidden", "text/plain", "Access denied");
        return;
    }
    
    // Send file
    serve_file(s, resolved_path);
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
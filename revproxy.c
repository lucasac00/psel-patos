#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <arpa/inet.h>


#define BACKEND_IP "127.0.0.1"
#define MYPORT 4950
#define BACKEND_PORT 8000
#define BUFFER_SIZE 4096

// Full-duplex function to handle data transfer in both directions
// This function uses select() to monitor both sockets for incoming data
// and forwards the data accordingly.
// https://en.wikipedia.org/wiki/Duplex_(telecommunications)
void forward(int client_sock, int backend_sock) {
    char buffer[BUFFER_SIZE];
    // Initialize the file descriptor set
    fd_set read_fds;
    // Set the maximum file descriptor for select()
    int max_fd = (client_sock > backend_sock) ? client_sock : backend_sock;
    ssize_t bytes_read, bytes_sent;

    while (1) {
        FD_ZERO(&read_fds);
        // Add the client and backend sockets to the set
        FD_SET(client_sock, &read_fds);
        FD_SET(backend_sock, &read_fds);

        // select() will block until one of the sockets is ready for reading
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select failed");
            break;
        }

        // Data from client -> backend
        if (FD_ISSET(client_sock, &read_fds)) {
            bytes_read = recv(client_sock, buffer, BUFFER_SIZE, 0);
            if (bytes_read <= 0) break;
            bytes_sent = send(backend_sock, buffer, bytes_read, 0);
            if (bytes_sent < 0) break;
        }

        // Data from backend -> client
        if (FD_ISSET(backend_sock, &read_fds)) {
            bytes_read = recv(backend_sock, buffer, BUFFER_SIZE, 0);
            if (bytes_read <= 0) break;
            bytes_sent = send(client_sock, buffer, bytes_read, 0);
            if (bytes_sent < 0) break;
        }
    }
}

// Function to handle the client socket connection
// This function will forward the client socket to the backend to be processed there
// and only then will the response be sent back to the client. This is done to follow
// the reverse proxy pattern.
void handle_connection(int client_s) {
    struct sockaddr_in backend_addr;
    int backend_s = socket(AF_INET, SOCK_STREAM, 0);
    
    if (backend_s < 0) {
        perror("Failed to create backend socket");
        exit(EXIT_FAILURE);
    }
    
    memset(&backend_addr, 0, sizeof(backend_addr));
    backend_addr.sin_family = AF_INET;
    backend_addr.sin_port = htons(BACKEND_PORT);
    
    if (inet_pton(AF_INET, BACKEND_IP, &backend_addr.sin_addr) <= 0){
        perror("Invalid backend address");
        exit(EXIT_FAILURE);
    }
    
    if (connect(backend_s, (struct sockaddr*)&backend_addr, sizeof(backend_addr)) < 0){
        perror("Backend connection failed");
        exit(EXIT_FAILURE);
    }
    // Function to forward data between client and backend
    forward(client_s, backend_s);
    
    close(backend_s);
    close(client_s);
}

int main() {
    // AF_INET is used for IPv4 addresses
    // SOCK_STREAM is used for TCP connections
    int s = socket(AF_INET, SOCK_STREAM, 0);
    // sockaddr_in structure used for storing ipv4 address information
    struct sockaddr_in proxy_addr;

    if (s < 0) {
        perror("Failed to create socket");
        exit(EXIT_FAILURE);
    }

    // set the socket option to allow address reuse, getting rid of the “Address already in use.” message
    // https://stackoverflow.com/questions/14388706/how-do-so-reuseaddr-and-so-reuseport-differ
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&proxy_addr, 0, sizeof(proxy_addr));
    proxy_addr.sin_family = AF_INET; // AF_INET for IPv4
    proxy_addr.sin_addr.s_addr = INADDR_ANY; // INADDR_ANY for all interfaces
    proxy_addr.sin_port = htons(MYPORT); // port number in network byte order

    // Now that we have a socket and address information, we can bind it, that is
    // associate the socket with a port in the local machine
    if (bind(s, (struct sockaddr*)&proxy_addr, sizeof(proxy_addr)) < 0) {
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

    printf("Reverse proxy server listening on port %d\n", MYPORT);

    // Finally, we can initialize the server loop
    while (1) {
        struct sockaddr_in client_addr;
        // socklen_t is an unsigned int, used as one of the function parameters of accept()
        // https://pubs.opengroup.org/onlinepubs/7908799/xns/syssocket.h.html
        socklen_t client_len = sizeof(client_addr);
        // accept() returns a new socket file descriptor for the incoming connection
        int client_s = accept(s, (struct sockaddr*)&client_addr, &client_len);

        if (client_s < 0) {
            perror("Failed to accept connection");
            continue; // don't exit the code, we want to keep listening!
        }

        printf("New connection from: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        if (!fork()) {
            close(s); // Close the listening socket in the child process
            handle_connection(client_s);
            exit(EXIT_SUCCESS);
        }

        close(client_s); // Close the client socket in the parent process
    }

    // Close the listening socket
    close(s);
    return 0;
}

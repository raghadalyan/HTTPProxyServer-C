#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "threadpool.h"

#define MAX_BUFFER_SIZE 15300
#define MAX_HOSTNAME_SIZE 256
#define FILTER_SIZE 200

struct CommandLine {
    in_port_t port;
    int pool_size;
    int max_requests;
    char* filter_file;
};

struct CommandLine command_line;

char filter[FILTER_SIZE][MAX_HOSTNAME_SIZE];
int filter_count ;

void read_filter_file() {
    filter_count = 0;
    FILE *fp = fopen(command_line.filter_file, "r");
    if (fp == NULL) {
        perror("Error opening filter file");
        exit(EXIT_FAILURE);
    }

    // Read each line from the filter file and store in the filter array
    char line[MAX_HOSTNAME_SIZE];
    while (fgets(line, sizeof(line), fp) != NULL) {
        // Remove newline character if present
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }
        if (len > 0 && line[len - 1] == '\r') {
            line[len - 1] = '\0';
        }

        strcpy(filter[filter_count++], line);
        if (filter_count >= FILTER_SIZE) {
            fprintf(stderr, "Filter list is full. Increase FILTER_SIZE.\n");
            break;
        }
    }

    fclose(fp);
}


int is_ip_in_subnet(const char *ip, const char *subnet) {
    struct in_addr ip_addr, subnet_addr, mask_addr;
    if (inet_pton(AF_INET, ip, &ip_addr) != 1 || inet_pton(AF_INET, subnet, &subnet_addr) != 1) {
        return 0; // Error
    }

    // Parse the subnet mask from the subnet string
    int mask_len;
    if (sscanf(subnet, "%d", &mask_len) != 1 || mask_len < 0 || mask_len > 32) {
        return 0; // Invalid subnet mask
    }
    mask_addr.s_addr = htonl(~((1 << (32 - mask_len)) - 1)); // Create mask from prefix length

    // Perform bitwise AND between IP and subnet mask to get network address
    if ((ip_addr.s_addr & mask_addr.s_addr) == (subnet_addr.s_addr & mask_addr.s_addr)) {
        return 1; // IP address matches subnet
    } else {
        return 0; // IP address does not match subnet
    }
}

int check_url_against_filter(const char *host) {

    // Check if the host directly matches an entry in the filter list
    for (int i = 0; i < filter_count; ++i) {

        int cmp = strcmp(host, filter[i]);
        if (cmp == 0) {
            printf("Host found in filter list\n");
            return 1; // Host found in filter list
        }
    }

    // Check if the host is an IP address and matches any subnet in the filter list
    struct in_addr ip_addr;
    if (inet_pton(AF_INET, host, &ip_addr) == 1) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, ip_str, INET_ADDRSTRLEN);
        printf("Checking IP address: %s\n", ip_str);
        for (int i = 0; i < filter_count; ++i) {
            if (is_ip_in_subnet(ip_str, filter[i])) {
                printf("IP address found in filter list\n");
                return 1; // IP address found in filter list
            }
        }
    }

    return 0; // Host or IP not found in filter
}

void send_not_supported_error(int *client_socket) {
    char current_time[50];
    time_t now = time(NULL);
    strftime(current_time, sizeof(current_time), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));

    char error_response[MAX_BUFFER_SIZE];
    snprintf(error_response, sizeof(error_response),
             "HTTP/1.1 501 Not Supported\r\n"
             "Server: webserver/1.0\r\n"
             "Date: %s\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %lu\r\n"
             "Connection: close\r\n"
             "\r\n"
             "<HTML><HEAD><TITLE>501 Not supported</TITLE></HEAD>\r\n"
             "<BODY><H4>501 Not supported</H4>\r\n"
             "Method is not supported.\r\n"
             "</BODY></HTML>\r\n",
             current_time, (unsigned long)strlen("Method is not supported.\r\n"));

    write(*client_socket, error_response, strlen(error_response));
    close(*client_socket);
}

void send_404_error_response(int *client_socket) {
    char error_response[] = "HTTP/1.1 404 Not Found\r\nServer: webserver/1.0\r\nDate: %s\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD><BODY><H4>404 Not Found</H4>File not found.</BODY></HTML>";
    char date[100];
    time_t now = time(NULL);
    strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));
    int content_length = snprintf(NULL, 0, error_response, date, 112);
    char response[content_length + 1];
    sprintf(response, error_response, date, 112);
    write(*client_socket, response, content_length);
}

void send_403_error_response(int *client_socket) {
    // Get the current time
    time_t now = time(NULL);
    char current_time[50];
    strftime(current_time, sizeof(current_time), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));

    // Define the error message
    const char *error_message = "<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\r\n"
                                "<BODY><H4>403 Forbidden</H4>\r\n"
                                "Access denied.\r\n"
                                "</BODY></HTML>\r\n";
    size_t content_length = strlen(error_message);

    // Prepare the error response with dynamic values
    char error_response[MAX_BUFFER_SIZE];
    snprintf(error_response, sizeof(error_response),
             "HTTP/1.1 403 Forbidden\r\n"
             "Server: webserver/1.0\r\n"
             "Date: %s\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             current_time, content_length, error_message);

    // Send the error response
    write(*client_socket, error_response, strlen(error_response));
    close(*client_socket);
}

void send_400_error_response(int client_socket, const char* message) {
    // Get the current time
    time_t now = time(NULL);
    char current_time[50];
    strftime(current_time, sizeof(current_time), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));

    // Calculate the length of the message
    size_t message_length = strlen(message);

    // Prepare the error response with dynamic values
    char error_response[MAX_BUFFER_SIZE];
    snprintf(error_response, sizeof(error_response),
             "HTTP/1.1 400 Bad Request\r\nServer: webserver/1.0\r\nDate: %s\r\nContent-Type: text/html\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\r\n<BODY><H4>400 Bad Request</H4>\r\n%s\r\n</BODY></HTML>\r\n",
             current_time, message_length, message);

    // Send the error response
    write(client_socket, error_response, strlen(error_response));
    close(client_socket);
}

void handle_request(int *client_socket) {

    // Buffer to store the incoming HTTP request
    char buffer[MAX_BUFFER_SIZE];

    // Read the HTTP request from the client socket
    ssize_t bytes_received = read(*client_socket, buffer, sizeof(buffer) - 1);

    if (bytes_received < 0) {
        perror("Error receiving data from client");
        close(*client_socket);
        return;
    } else if (bytes_received == 0) {
        // Connection closed by the client
        printf("Client closed the connection.\n");
        close(*client_socket);
        return;
    }

    // Null-terminate the received data to treat it as a string
    buffer[bytes_received] = '\0';

    // Extract the method, URL, and protocol from the HTTP request
    char method[10];    // To store the HTTP method
    char url[MAX_BUFFER_SIZE];   // To store the requested URL
    char protocol[20];  // To store the HTTP protocol/version

    // Attempt to parse the request line
    if (sscanf(buffer, "%9s %s %s", method, url, protocol) != 3) {
        // Send a 400 Bad Request response if parsing fails
        send_400_error_response(*client_socket, "Bad Request.");
        close(*client_socket);
        return;
    }

    // Check if the protocol is one of the HTTP versions
    if (strcasecmp(protocol, "HTTP/1.0") != 0 && strcasecmp(protocol, "HTTP/1.1") != 0) {
        // Send a 400 Bad Request response if the protocol is not supported
        send_400_error_response(*client_socket,"Bad Request.");
        return;
    }

    // Check if the method is GET method
    if (strcmp(method, "GET") != 0) {
        send_not_supported_error(client_socket);
        return;
    }

    // Parse the URL to extract host, port, and path
    char host[MAX_HOSTNAME_SIZE];  // To store the host name
    char path[MAX_BUFFER_SIZE];    // To store the path
    int port = 0;  // Default port if not specified

    // Check if the URL starts with "http://" and skip it if present
    char *url_start = strstr(url, "http://");
    if (url_start != NULL) {
        url_start += strlen("http://");
    } else {
        url_start = url;
    }

    // Attempt to parse host and port
    if (sscanf(url_start, "%[^/:]:%d", host, &port) != 2) {
        if (sscanf(url_start, "%[^/]", host) != 1) {
            fprintf(stderr, "Error extracting host from URL.\n");
            close(*client_socket);
            return;
        }
    }

    // Attempt to parse path
    char *path_start = strchr(url_start, '/');
    if (path_start != NULL) {
        strcpy(path, path_start);  // Copy the path if it exists
    } else {
        strcpy(path, "/index.html");  // Default to "/index.html" if no path is provided
    }

    // If port is not specified, default to 80
    if (port == 0) {
        port = 80;
    }

    read_filter_file();

    if (check_url_against_filter(host)) {
        // Send a 403 Forbidden error response
        send_403_error_response(client_socket);
            close(*client_socket);
            return;
        }

    // Create a socket to connect to the destination server
    int dest_socket;
    struct sockaddr_in dest_addr;

    // Resolve the destination host
    struct hostent *host_info = gethostbyname(host);
    if (host_info == NULL) {
        send_404_error_response( client_socket);
        close(*client_socket);
        return;
    }

    // Set up destination address struct
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr = *((struct in_addr *)host_info->h_addr);
    dest_addr.sin_port = htons(port );

    // Create a socket
    if ((dest_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("Error creating destination socket");
        close(*client_socket);
        return;
    }

    // Connect to the destination server
    if (connect(dest_socket, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
        perror("Error connecting to destination server");
        close(*client_socket);
        close(dest_socket);
        return;
    }

    char forward_req [MAX_BUFFER_SIZE];
    snprintf(forward_req, sizeof(forward_req), "GET /%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    send(dest_socket, forward_req, strlen(forward_req), 0);

    // Relay the server's response back to the client with modified headers
    char response[MAX_BUFFER_SIZE];
    ssize_t n;

    while ((n = recv(dest_socket, response, sizeof(response), 0)) > 0) {
        // Modify response headers to include Connection: close and remove Connection: keep-alive
        char* connection_keep_alive = strstr(response, "Connection: keep-alive");
        if (connection_keep_alive != NULL) {
            // Replace "Connection: keep-alive" with "Connection: close"
            memcpy(connection_keep_alive, "Connection: close", strlen("Connection: close"));
        } else {
            // If Connection: keep-alive header not found, add Connection: close header
            char* end_of_headers = strstr(response, "\r\n\r\n");
            if (end_of_headers != NULL) {
                // Insert Connection: close header after the last header line
                strcat(end_of_headers, "Connection: close\r\n");
            }
        }

        // Send the modified response back to the client
        ssize_t sent = send(*client_socket, response, n, 0);

        if (sent < 0) {
            perror("Error sending response to client");
            break;  // Break out of the loop on send error
        } else if (sent < n) {

            fprintf(stderr, "Partial send: %zd out of %zd bytes sent\n", sent, n);
            break;
        }
    }

    if (n < 0) {
        // Error receiving response from server
        perror("Error receiving response from destination server");
    }
    // Close the sockets
    close(*client_socket);
    close(dest_socket);
}

// Function to initialize the server socket
int initialize_server(in_port_t port) {
    int server_socket;
    struct sockaddr_in server_addr;

    // Create a socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("Error creating server socket");
        exit(EXIT_FAILURE);
    }

    // Set up server address struct
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    // Bind the socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding server socket");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_socket, SOMAXCONN) < 0) {
        perror("Error listening for connections");
        exit(EXIT_FAILURE);
    }

    return server_socket;
}

// Function to listen for incoming client connections
void listen_for_requests(int server_socket, threadpool* tp) {

    while (command_line.max_requests > 0) {

        int client_socket;
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(struct sockaddr_in);

        // Accept a new client connection
        if ((client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len)) < 0) {
            perror("Error accepting client connection");
            continue;
        }

        // Dispatch a task to the thread pool to handle the client connection
        dispatch(tp, (dispatch_fn)handle_request, (void*)&client_socket);

        usleep(100000); // Sleep for 0.1 seconds
        close (client_socket);

        command_line.max_requests--;
    }
}

int main(int argc, char* argv[]) {
    // Check command-line arguments
    if (argc != 5) {
        fprintf(stderr, "Usage: proxyServer <port> <pool-size> <max-number-of-request> <filter>\n");
        exit(EXIT_FAILURE);
    }

    // Parse command-line arguments and populate the command_line struct
    command_line.port = atoi(argv[1]);
    command_line.pool_size = atoi(argv[2]);
    command_line.max_requests = atoi(argv[3]);
    command_line.filter_file = argv[4];

    // Initialize your thread pool
    threadpool* tp = create_threadpool(command_line.pool_size);

    // Initialize your server socket
    int server_socket = initialize_server(command_line.port);

    // Listen for incoming client connections
    listen_for_requests(server_socket, tp);

    // Close the server socket
    close(server_socket);

    // Destroy the thread pool after serving the requests
    destroy_threadpool(tp);

    return 0;
}

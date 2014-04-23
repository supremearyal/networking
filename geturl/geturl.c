#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define HEADER_MAX_SIZE (1 << 13)

struct addrinfo *get_host_connection(struct addrinfo *results, int *socket_fd) {
    *socket_fd = -1;
    
    struct addrinfo *result = NULL;
    for (result = results; result != NULL; result = result->ai_next) {
        if ((*socket_fd = socket(result->ai_family, result->ai_socktype,
                                 result->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if(connect(*socket_fd, result->ai_addr, result->ai_addrlen) == -1) {
            close(*socket_fd);
            perror("client: connect");
            continue;
        }
        
        break;
    }

    if (result == NULL)
        fprintf(stderr, "client: failed to connect\n");

    return result;
}

char *get_request(char *url, char *host, int *request_length)
{
    char request_template[] = "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n";
    char *request = malloc(sizeof(request_template) +
                           strlen(url) +
                           strlen(host));
    *request_length = sprintf(request, request_template, url, host);
    return request;
}

int send_request(int socket_fd, char *request, int request_len) {
    int sent_length = 0;
    while (request_len > 0 &&
           (sent_length = send(socket_fd, request, request_len, 0)) != -1) {
        request += sent_length;
        request_len -= sent_length;
    }

    return sent_length;
}

int get_content_length(char *header)
{
    char content_length_field[] = "Content-Length: ";
    int content_length_field_length = sizeof(content_length_field) - 1;
    char *content_length_field_position = strstr(header,
                                                 content_length_field);
    if (content_length_field_position != NULL) {
        return atoi(content_length_field_position +
                    content_length_field_length);
    }

    return -1;
}

int write_response_body(int socket_fd) {
    int ret_val = 0;
    int total_header_bytes_written = 0;
    int total_body_bytes_written = 0;
    char header[HEADER_MAX_SIZE];
    char *body = NULL;
    int body_length = 0;
    int known_body_length = -1;

    while (1) {
        int bytes_written = recv(socket_fd,
                                 header + total_header_bytes_written,
                                 HEADER_MAX_SIZE - 1 - bytes_written, 0);
        if (bytes_written == 0) {
            fprintf(stderr, "connection terminated prematurely.");
            ret_val = -1;
            goto header_error;
        }

        if (bytes_written == -1) {
            perror("recv");
            ret_val = -1;
            goto header_error;
        }

        total_header_bytes_written += bytes_written;
        if (total_header_bytes_written == HEADER_MAX_SIZE - 1) {
            fprintf(stderr, "header too large.\n");
            ret_val = -1;
            goto header_error;
        }

        header[total_header_bytes_written] = '\0';
        char header_end[] = "\r\n\r\n";
        int header_end_length = sizeof(header_end) - 1;
        char *header_end_position = strstr(header +
                                           total_header_bytes_written -
                                           bytes_written,
                                           header_end);
        if (header_end_position != NULL) {
            *header_end_position = '\0';

            body_length = get_content_length(header);
            known_body_length = body_length;
            int fetched_body_length = header + total_header_bytes_written -
                header_end_position - header_end_length;

            if (body_length == -1)
                body_length = fetched_body_length * 2;

            body = malloc(body_length + 1);
            memcpy(body, header + total_header_bytes_written -
                   fetched_body_length, fetched_body_length);
            
            total_body_bytes_written += fetched_body_length;
            break;
        }
    }

    while (1) {
        int bytes_written = 0;
        bytes_written = recv(socket_fd,
                             body + total_body_bytes_written,
                             body_length - total_body_bytes_written, 0);

        if (bytes_written == 0) {
            if (known_body_length != -1 &&
                total_body_bytes_written == known_body_length) {
                break;
            } else if (known_body_length == -1) {
                body_length = total_body_bytes_written;
                break;
            } else {
                fprintf(stderr, "prematurely lost connection");
                ret_val = -1;
                goto body_error;
            }
        }

        if (bytes_written == -1) {
            perror("recv");
            ret_val = -1;
            goto body_error;
        }

        total_body_bytes_written += bytes_written;
        if (total_body_bytes_written == body_length) {
            if (known_body_length == -1) {
                body_length *= 2;
                body = realloc(body, body_length + 1);
            } else {
                break;
            }
        }
    }

    body[total_body_bytes_written] = '\0';
    fwrite(body, body_length, sizeof(char), stdout);

body_error:
    free(body);
header_error:
    return ret_val;
}

int main(int argc, char **argv)
{
    int ret_val = 0;
    
    char *host = NULL;
    char *address = NULL;
    char *url = NULL;
    char *port = NULL;

    if (argc < 2) {
        fprintf(stderr, "Usage: geturl <url> <port=80>\n");
        ret_val = 1;
        goto usage_error;
    } else {
        address = argv[1];

        char http_prefix[] = "http://";
        int http_prefix_length = sizeof(http_prefix) - 1;
        char *protocol = strstr(address, http_prefix);
        if (protocol != NULL) {
            address = protocol + http_prefix_length;
        }

        url = strchr(address, '/');
        if (url == NULL) {
            url = "/";
            host = strdup(address);
        } else {
            host = strndup(address, url - address);
        }

        if (argc > 2)
            port = argv[2];
        else
            port = "80";
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *results;
    int error;
    if ((error = getaddrinfo(host, port, &hints, &results)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        ret_val = 1;
        goto getaddrinfo_error;
    }

    struct addrinfo *host_address;
    int socket_fd;
    host_address = get_host_connection(results, &socket_fd);
    if (host_address == NULL) {
        ret_val = 1;
        goto connect_error;
    }

    int request_len;
    char *request = get_request(url, host, &request_len);
    if (send_request(socket_fd, request, request_len) == -1) {
        ret_val = 1;
        goto send_error;
    }

    if (write_response_body(socket_fd) == -1) {
        ret_val = 1;
        goto write_error;
    }

write_error:
send_error:
    close(socket_fd);
    free(request);
connect_error:
    freeaddrinfo(results);
getaddrinfo_error:
    free(host);
usage_error:
    return ret_val;
}

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <errno.h>
#include <pthread.h>
#include <regex.h>

#include "helps.h"
#include "fifo.h"
#include "lockers.h"

#define BUFFER_SIZE     4096
#define MAX_URI_SIZE    256
#define DEFAULT_THREADS 4

#define METHOD_REGEX       "([a-zA-Z]{1,8})"
#define URI_REGEX          "/([a-zA-Z0-9.-]{1,63})"
#define VERSION_REGEX      "(HTTP/[0-9].[0-9])"
#define HTTP_VERSION_REGEX "HTTP/1.1"
#define EMPTY_LINE_REGEX   "\r\n"
#define REQUEST_LINE_REGEX METHOD_REGEX " " URI_REGEX " " VERSION_REGEX EMPTY_LINE_REGEX

#define KEY_REGEX          "([a-zA-Z0-9.-]{1,128})"
#define VALUE_REGEX        "([ -~]{1,128})"
#define HEADER_FIELD_REGEX KEY_REGEX ": " VALUE_REGEX EMPTY_LINE_REGEX

#define MAX_HEADER_LENGTH 2048

typedef struct uri_node {
    char uri[MAX_URI_SIZE];
    rwlock_t *lock;
    struct uri_node *next;
} uri_node;

uri_node *uri_list = NULL;
pthread_mutex_t uri_list_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int connfd;
} http_request;

queue_t *request_queue;
pthread_t *kam_threads;
int num_threads = DEFAULT_THREADS;

void *kaam_bhari(void *arg);
void handle_connection(void *arg);
int process_request(const char *request, int connfd, int end_of_req, ssize_t bytes_read);
void log_request(const char *method, const char *uri, int status_code, const char *request_id);
int setup_server(int port, int threads);
int parse_http_request(
    const char *request, char *method, char *uri, char *version, char *request_id);
void jawab_de(
    int fd, int status_code, size_t body, const char *status_phrase, const char *content_type);
int get_command(int connfd, char *uri);
int put_command(
    int connfd, const char *request, const char *uri, int end_of_req, ssize_t bytes_read);
int parse_content_length(const char *headers);
rwlock_t *getthe_workers(const char *uri);
void start_them();
void cleanup();

int main(int argc, char **argv) {
    int opt, port;

    while ((opt = getopt(argc, argv, "t:")) != -1) {
        switch (opt) {
        case 't': num_threads = atoi(optarg); break;
        default: fprintf(stderr, "Usage: %s wrong way\n", argv[0]); exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "wrong\n");
        exit(EXIT_FAILURE);
    }

    port = atoi(argv[optind]);
    if (port < 1 || port > 65535) {
        fprintf(stderr, "Invalid Port\n");
        exit(EXIT_FAILURE);
    }

    setup_server(port, num_threads);
    cleanup();
    return 0;
}

void start_them() {
    uri_list = NULL;
}

void free_uri_locks() {
    uri_node *current = uri_list, *tmp;
    while (current != NULL) {
        tmp = current;
        current = current->next;
        rwlock_delete(&tmp->lock);
        free(tmp);
    }
}

rwlock_t *getthe_workers(const char *uri) {
    pthread_mutex_lock(&uri_list_mutex);
    for (uri_node *node = uri_list; node != NULL; node = node->next) {
        if (strcmp(node->uri, uri) == 0) {
            pthread_mutex_unlock(&uri_list_mutex);
            return node->lock;
        }
    }

    uri_node *new_node = malloc(sizeof(uri_node));
    strcpy(new_node->uri, uri);
    new_node->lock = rwlock_new(N_WAY, 1);
    new_node->next = uri_list;
    uri_list = new_node;

    pthread_mutex_unlock(&uri_list_mutex);
    return new_node->lock;
}

void cleanup() {
    free_uri_locks();
    free(kam_threads);
}

int setup_server(int port, int threads) {
    Listener_Socket sock;
    if (listener_init(&sock, port) != 0) {
        fprintf(stderr, "Listener failed\n");
        return -1;
    }

    start_them();
    request_queue = queue_new(threads * 2);
    kam_threads = malloc(threads * sizeof(pthread_t));

    for (int i = 0; i < threads; i++) {
        pthread_create(&kam_threads[i], NULL, kaam_bhari, NULL);
    }

    while (1) {
        int connfd = listener_accept(&sock);
        if (connfd < 0) {
            perror("Error accept connection");
            continue;
        }

        http_request *req = malloc(sizeof(http_request));
        req->connfd = connfd;
        queue_push(request_queue, req);
    }
    return 0;
}

void *kaam_bhari(void *arg) {
    (void) arg;
    while (1) {
        http_request *req;
        if (queue_pop(request_queue, (void **) &req)) {
            handle_connection(req);
            close(req->connfd);
            free(req);
        }
    }
    return NULL;
}

void handle_connection(void *arg) {
    http_request *req = (http_request *) arg;
    char buffer[BUFFER_SIZE] = { 0 };
    ssize_t bytes_read = read_until(req->connfd, buffer, BUFFER_SIZE - 1, "\r\n\r\n");

    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        int end_of_req = strstr(buffer, "\r\n\r\n") - buffer + 4;
        process_request(buffer, req->connfd, end_of_req, bytes_read);
    } else if (bytes_read == -1) {
        perror("Read error");
    }
}

int process_request(const char *request, int connfd, int end_of_req, ssize_t bytes_read) {
    char method[16], uri[MAX_URI_SIZE], version[16], request_id[256] = "0";
    int status_code;

    if (parse_http_request(request, method,

            uri, version, request_id)
        < 0) {
        jawab_de(connfd, 400, 12, "Bad Request", "PUT");
        log_request(method, uri, 400, request_id);

        return 1;
    }

    if (strcmp(version, "HTTP/1.1") != 0) {
        jawab_de(connfd, 505, 22, "Version Not Supported", method);
        log_request(method, uri, 505, request_id);

        return 1;
    }

    if (strcmp(method, "GET") == 0) {
        reader_lock(getthe_workers(uri));
        status_code = get_command(connfd, uri);
        reader_unlock(getthe_workers(uri));
    } else if (strcmp(method, "PUT") == 0) {
        writer_lock(getthe_workers(uri));
        status_code = put_command(connfd, request, uri, end_of_req, bytes_read);
        writer_unlock(getthe_workers(uri));
    } else {
        jawab_de(connfd, 501, 16, "Not Implemented", method);
        status_code = 501;
    }
    log_request(method, uri, status_code, request_id);

    return 0;
}

void log_request(const char *method, const char *uri, int status_code, const char *request_id) {
    // rwlock_t *lock = log_lock;
    // writer_lock(lock);
    fprintf(stderr, "%s,%s,%d,%s\n", method, uri, status_code, request_id);
    // writer_unlock(lock);
}

int parse_http_request(
    const char *request, char *method, char *uri, char *version, char *request_id) {
    regex_t req_line_regex, header_field_regex;
    regmatch_t pmatch[4];
    const char *line;

    if (regcomp(&req_line_regex, REQUEST_LINE_REGEX, REG_EXTENDED) != 0) {
        return -1;
    }
    if (regexec(&req_line_regex, request, 4, pmatch, 0) == 0) {
        strncpy(method, request + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so);
        method[pmatch[1].rm_eo - pmatch[1].rm_so] = '\0';
        strncpy(uri, request + pmatch[2].rm_so, pmatch[2].rm_eo - pmatch[2].rm_so);
        uri[pmatch[2].rm_eo - pmatch[2].rm_so] = '\0';
        strncpy(version, request + pmatch[3].rm_so, pmatch[3].rm_eo - pmatch[3].rm_so);
        version[pmatch[3].rm_eo - pmatch[3].rm_so] = '\0';
    } else {
        //fprintf(stderr, "Failed 1\n");
        regfree(&req_line_regex);
        return -1;
    }
    regfree(&req_line_regex);

    if (regcomp(&header_field_regex, HEADER_FIELD_REGEX, REG_EXTENDED) != 0) {
        return -1;
    }

    line = request + pmatch[0].rm_eo;

    regmatch_t matches[3];
    int ko = regexec(&header_field_regex, line, 3, matches, 0);

    while (ko == 0) {

        if (matches[0].rm_so != 0) {
            //fprintf(stderr, "HERE 1\n");
            regfree(&header_field_regex);
            return -1;
        }

        line += matches[0].rm_eo;

        ko = regexec(&header_field_regex, line, 3, matches, 0);
    }

    if (strncmp(line, "\r\n", 2) == 0) {
        regfree(&header_field_regex);
    } else {

        regfree(&header_field_regex);
        return -1;
    }

    const char *reqIdHeader = "Request-Id:";
    const char *foundHeader = strstr(request, reqIdHeader);
    if (foundHeader) {

        sscanf(foundHeader + strlen(reqIdHeader), "%255s", request_id);
    }
    return 0;
}

void jawab_de(
    int fd, int status_code, size_t body, const char *status_phrase, const char *content_type) {

    char header[1024];

    if (strcmp(content_type, "GET") == 0) {
        if (strcmp(status_phrase, "OK") == 0) {
            snprintf(header, sizeof(header), "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\n\r\n",
                status_code, status_phrase, body);
        } else {
            snprintf(header,
                sizeof

                (header),
                "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\n\r\n%s\n", status_code, status_phrase,
                body, status_phrase);
        }

    } else if (strcmp(content_type, "PUT") == 0) {
        snprintf(header, sizeof(header), "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\n\r\n%s\n",
            status_code, status_phrase, body, status_phrase);
    } else {
        snprintf(header, sizeof(header), "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\n\r\n%s\n",
            status_code, status_phrase, body, status_phrase);
    }

    write_n_bytes(fd, header, strlen(header));
}

int get_command(int connfd, char *uri) {
    struct stat statbuf;

    if (stat(uri, &statbuf) == -1) {

        if (errno == ENOENT) {
            jawab_de(connfd, 404, 10, "Not Found", "GET");
            return 404;
        } else {
            jawab_de(connfd, 500, 22, "Internal Server Error", "GET");
            return 500;
        }
    }

    if (S_ISDIR(statbuf.st_mode)) {
        jawab_de(connfd, 403, 10, "Forbidden", "GET");
        return 403;
    }

    int fd = open(uri, O_RDONLY);
    if (fd < 0) {
        jawab_de(connfd, 500, 22, "Internal Server Error", "GET");
        return 500;
    }

    jawab_de(connfd, 200, (size_t) statbuf.st_size, "OK", "GET");

    if (pass_n_bytes(fd, connfd, statbuf.st_size) == -1) {
        close(fd);
        jawab_de(connfd, 500, 22, "Internal Server Error", "GET");
        return 500;
    }

    close(fd);
    return 200;
}

int put_command(
    int connfd, const char *request, const char *uri, int end_of_req, ssize_t bytes_read) {
    //MAKE ANOTHER REGEX FOR The HEADER FIELD VALIDATION PLEASE

    int content_length = parse_content_length(request);
    if (content_length < 0) {
        jawab_de(connfd, 400, 12, "Bad Request", "PUT");
        return 400;
    }

    struct stat statbuf;
    stat(uri, &statbuf); /// CHANGE #2 asgn2 first was OK content length as 3

    if (S_ISDIR(statbuf.st_mode)) {
        jawab_de(connfd, 403, 10, "Forbidden", "PUT");
        return 403;
    }
    int file_exists = stat(uri, &statbuf) == 0;

    int fd = open(uri, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {

        jawab_de(connfd, 500, 22, "Internal Server Error", "PUT");
        return 403;
    }

    int initial_body_size = bytes_read - end_of_req;
    if (initial_body_size > 0) {
        char *initial_body_buffer = malloc(initial_body_size);
        memcpy(initial_body_buffer, request + end_of_req, initial_body_size);
        if (content_length < initial_body_size) {
            int one = write_n_bytes(fd, initial_body_buffer, content_length);
            if (one == -1) {
                free(initial_body_buffer);
                close(fd);
                jawab_de(connfd, 500, 22, "Internal Server Error", "PUT");
                return 500;
            }
            free(initial_body_buffer);
            close(fd);
            if (file_exists) {
                jawab_de(connfd, 200, 3, "OK", "PUT");
                return 200;
            } else {
                jawab_de(connfd, 201, 7, "Created", "PUT");
                return 201;
            }

            ;
        }
        int two = write_n_bytes(fd, initial_body_buffer, initial_body_size);
        if (two == -1) {
            free(initial_body_buffer);
            close(fd);
            jawab_de(connfd, 500, 22, "Internal Server Error", "PUT");
            return 500;
        }

        free(initial_body_buffer);
    }

    int remaining = content_length - initial_body_size;
    int three = pass_n_bytes(connfd, fd, remaining);
    if (three == -1) {
        close(fd);
        jawab_de(connfd, 500, 22, "Internal Server Error", "PUT");
        return 500;
    }

    close(fd);
    if (file_exists) {
        jawab_de(connfd, 200, 3, "OK", "PUT");
        return 200;
    } else {
        jawab_de(connfd, 201, 7, "Created", "PUT");
        return 201;
    }
}

int parse_content_length(const char *headers) {
    regex_t regex;
    regmatch_t pmatch[2];
    char pattern[] = "Content-Length: ([ -~]{1,128})\r\n";

    if (regcomp(&regex, pattern, REG_EXTENDED) != 0) {
        return -1; // Failed to compile regex
    }

    int content_length = -1;
    if (regexec(&regex, headers, 2, pmatch, 0) == 0) {
        char clength[16];
        strncpy(clength, headers + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so);
        clength[pmatch[1].rm_eo - pmatch[1].rm_so] = '\0';
        content_length = atoi(clength);
    }

    regfree(&regex);
    return content_length;
}

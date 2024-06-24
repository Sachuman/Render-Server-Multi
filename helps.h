#pragma once

#include <stdint.h>
#include <sys/types.h>


typedef struct {
    int fd;
} Listener_Socket;


int listener_init(Listener_Socket *sock, int port);


int listener_accept(Listener_Socket *sock);


ssize_t read_until(int fd, char buf[], size_t n, char *str);


ssize_t read_n_bytes(int in, char buf[], size_t n);


ssize_t write_n_bytes(int fd, char buf[], size_t n);

ssize_t pass_n_bytes(int src, int dst, size_t n);

#pragma once
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>  // close
#include <string.h>
#include <errno.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <arpa/inet.h>      // for inet_pton
#include <sys/time.h>       // for time_t


// - Functions - //

/* Connects to remote server over TCP */
int connect_to_server(const char *server_ip, unsigned int port, time_t send_timeout, time_t recv_timeout);

/* Send buffer to socket */
bool send_all(int socket, void *buffer, size_t length);

/* Send file to socket */
bool sendfile_all(int sockfd, int src_fd, size_t file_size);

#pragma once
#include <stdio.h>
#include <stdlib.h>         // for atoi and itoa
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>
#include <fcntl.h>          // for open
#include <getopt.h>         // getopt_long
#include "socket_utils.h"
#include "wait_for_exec.h"
#include "consts.h"


// - Consts - //

#define DEFAULT_PORT 8080

#define HEADER_BUF_SIZE 1024
#define SEND_TIMEOUT 300 // 5 minutes
#define RECV_TIMEOUT 120 // 2 minutes

#define DEFAULT_EXEC_BIN "/bin/enter"


// - Structs - //

/* Options */
static struct option long_options[] =
{
    {"port", required_argument, NULL, 'p'},
    {"exec", no_argument, NULL, 'e'},
    {"exec-extra-argument", required_argument, NULL, 'a'},
    {"exec-bin", required_argument, NULL, 'b'},
    {"exec-readdir-proc", no_argument, NULL, 'r'},
    {NULL, 0, NULL, 0}
    // TODO: add quiet mode (no prints to stdout / just error messages)
};

/* Configuration for current run, should mirror the long_options array */
typedef struct config
{
    const char * server_ip;
    unsigned int port;
    bool wait_for_exec;
    const char * exec_extra_arg;
    const char * exec_bin;
    bool exec_readdir_mode;
} config;


// - Funcs - //

/* Prints the help message */
void print_help(void);


/* Parses the arguments in argv to the conf parameter */
int parse_arguments(config * conf, int argc, char const *argv[]);


/* Send an HTTP POST header with application/octet-stream indicating a file is sent.                  *
 * If filename != NULL, it will be included in the Content-Disposition header in the "filename" field */
bool send_post_file_http_header(int sockfd, const char * server_ip, unsigned long long file_size, const char * filename);

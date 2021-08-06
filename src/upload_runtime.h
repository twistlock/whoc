#pragma once
#include <stdio.h>
#include <stdlib.h>         // for atoi, itoa, system
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>
#include <fcntl.h>          // for open
#include <getopt.h>         // getopt_long
#include "wait_for_exec.h"
#include "consts.h"


// - Consts - //

#define DEFAULT_PORT 8080

#define DEFAULT_EXEC_BIN "/bin/enter"

#define ORIGINAL_LD_PATH "/root/ld-linux-x86-64.so.2"
#define LD_PATH "/lib64/ld-linux-x86-64.so.2"


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

/* Sends the file at fd to the server by invoking curl */
int sendfile_curl(const char * server_ip, unsigned int port, int fd, const char * filename);

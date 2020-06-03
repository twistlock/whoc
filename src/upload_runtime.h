#pragma once
#include <stdio.h>
#include <stdlib.h>         // for atoi and itoa
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>       // for wait
#include <fcntl.h>          // for open
#include <getopt.h>         // getopt_long
#include <dirent.h>         // Defines DT_* constants
#include <ctype.h>          // isdigit
#include <sys/syscall.h>    // SYS_getdents64
#include "socket_utils.h"



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


// For getdents64()
struct linux_dirent64 {
   ino_t          d_ino;    /* 64-bit inode number */
   off_t          d_off;    /* 64-bit offset to next structure */
   unsigned short d_reclen; /* Size of this dirent */
   unsigned char  d_type;   /* File type */
   char           d_name[]; /* Filename (null-terminated) */
};


// - Consts - //


#define DEFAULT_PORT 8080

#define SEND_TIMEOUT 300 // 5 minutes
#define RECV_TIMEOUT 120 // 2 minutes

#define SMALL_BUF_SIZE 256
#define HEADER_BUF_SIZE 1024
#define LARGE_BUF_SIZE 16384
#define DEFAULT_EXEC_BIN "/bin/enter"

#define MAX_PID 9999  // we leave room for a 4 digit pid (since we're in a new pid ns, the pids should be rather small)



// - Funcs - //


/* Parses the arguments in argv to the conf parameter */
int parse_arguments(config * conf, int argc, char const *argv[]);


/* Send an HTTP POST header with application/octet-stream indicating a file is sent.                  *
 * If filename != NULL, it will be included in the Content-Disposition header in the "filename" field */
bool send_post_file_http_header(int sockfd, const char * server_ip, unsigned long long file_size, const char * filename);


/* Prints the help message */
void print_help(void);


/* Guess the next pid in our pid namespace by forking a child process and inspecting his pid */ 
pid_t guess_next_pid(void);


/* Creates an executable file at exec_bin_path containing "#!/proc/self/exe extra_arg" */
bool prepare_bin_for_exec(const char * exec_bin_path, const char * extra_arg);


/* Returns a file descriptor pointing to the runtime that exec-ed into the whoc container.         *
 * Guesses the next pid the ctr pid ns, an continuously tries to open('/proc/$guessed_rt_pid/exe') */
int catch_rt_guess_pid(const char * exec_bin_path);


/* Returns a file descriptor pointing to the runtime that execed into the whoc container.                         *                                           
 * Finds the runtime process by searching for new (i.e. not PID 1, us) numeric dentries in the '/proc' directory. *
 * Some limitations are documented in the function implementation                                                 */
int catch_rt_getdents_proc(const char * exec_bin_path);

#pragma one
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>          // for open
#include <dirent.h>         // Defines DT_* constants
#include <ctype.h>          // isdigit
#include <sys/syscall.h>    // SYS_getdents64
#include <sys/wait.h>       // for wait
#include <sys/stat.h>       // chmod
#include "consts.h"


#define DEFAULT_EXEC_BIN "/bin/enter"


// For getdents64()
struct linux_dirent64 {
   ino_t          d_ino;    /* 64-bit inode number */
   off_t          d_off;    /* 64-bit offset to next structure */
   unsigned short d_reclen; /* Size of this dirent */
   unsigned char  d_type;   /* File type */
   char           d_name[]; /* Filename (null-terminated) */
};


// - Funcs - //

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

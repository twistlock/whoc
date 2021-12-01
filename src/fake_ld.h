//
// Opens /proc/self/exe and then executes `upload_runtime`.
// Valid dynamic linker for Linux on x86_64. 
//

typedef unsigned int uint;

#define UPLOAD_RUNTIME_PATH "/upload_runtime"
#define MAX_ERR_STR_LEN 20 // 19 digits in maximum 64 bit errno (2**63 + 1)

#define __NR_write 1
#define __NR_open 2
#define __NR_execve 59
#define __NR_exit_group 231 // exit_group()
#define O_RDONLY 0
#define STDOUT 1

// libc-less syscalls
// Inlined to ensure no plt section
__attribute__((always_inline)) inline int my_open(char * path, int flags);
__attribute__((always_inline)) inline int my_execve(const char *pathname, char ** argv, char ** envp);
__attribute__((always_inline)) inline void my_exit(int exit_status);
__attribute__((always_inline)) inline int my_write(int fd, const void *buf);

// String utils 
__attribute__((always_inline)) inline uint my_strlen(const char *str); 
__attribute__((always_inline)) inline char * errno_to_str(char * str, uint size, uint error);
__attribute__((always_inline)) inline void log_err(char * err_msg, int errcode_neg);

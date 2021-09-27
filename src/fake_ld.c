//
// Opens /proc/self/exe and then executes '/upload_runtime'.
// Valid dynamic linker for Linux on x86_64. 
//

#define __NR_open 2
#define __NR_execve 59
#define __NR_exit 60
#define O_RDONLY 0
#define UPLOAD_RUNTIME_PATH "/upload_runtime"

// libc-less syscalls. 
// Inlined to ensure we won't need any relocation (no plt)
__attribute__((always_inline)) inline int my_open(char * path, int flags);
__attribute__((always_inline)) inline int my_execve(const char *pathname, char ** argv, char ** envp);
__attribute__((always_inline)) inline void my_exit(int exit_status);

// ELF entrypoint
void _start()
{  
    unsigned long argc;
    char **argv;

    // Get argc & argv from stack, frame after prologue is:
    // [rsp] --------> saved rbp
    // [rsp+0x8] ---> argc
    // [rsp+0x10] ---> argv[0]
    // [rsp+0x10+8n] > argv[argc-1] // n = argc-1
    // [rsp+0x18+8n] > envc
    // [rsp+0x20+8n] > envp[0]
    // ...
    asm ("mov 0x8(%%rbp), %0 ;"
         "lea 0x10(%%rbp), %1 ;"
         : "=r"(argc), "=r"(argv) ::);

    // execve expectes argv to end with NULL. This overwrites envc.
    argv[argc] = 0; 
    if (my_open("/proc/self/exe", O_RDONLY) > 0)
        my_exit(my_execve(UPLOAD_RUNTIME_PATH, argv, 0));

}


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
        : "0"(__NR_exit), "D"(exit_status)
        : "rcx", "r11", "memory"
    );
}

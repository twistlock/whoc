#include "wait_for_exec.h"


bool prepare_bin_for_exec(const char * exec_bin_path, const char * extra_arg)
{
    FILE * bin_fp;
    char shebang_buff[SMALL_BUF_SIZE];
    unsigned int extra_arg_len;
    int rc;

    if (extra_arg)
    {
        extra_arg_len = strlen(extra_arg);
        if (extra_arg_len > SMALL_BUF_SIZE - 20) // 20 to save room for '#!/proc/self/exe '
        {
            printf("[!] prepare_bin_for_exec: exec_extra_arg too long (is %u while max is %u)\n", extra_arg_len, SMALL_BUF_SIZE - 20);
            return false;
        }

        rc = snprintf(shebang_buff, SMALL_BUF_SIZE, "#!/proc/self/exe %s", extra_arg);
        if (rc < 0) 
        {
            printf("[!] prepare_bin_for_exec: snprintf(header) failed with '%s'\n", strerror(errno));
            return false;
        } // intentionally not checking if rc >= SMALL_BUF_SIZE, as verifying the strlen(extra_arg) should be enough
    }
    else
        strcpy(shebang_buff, "#!/proc/self/exe");

    // Open file
    bin_fp = fopen(exec_bin_path, "w");
    if (!bin_fp)
    {
        printf("[!] prepare_bin_for_exec: fopen(exec_bin_path) failed with '%s'\n", strerror(errno));
        return false;
    }

    // Write shebang line to file
    if (fputs(shebang_buff, bin_fp) == EOF)
    {
        printf("[!] prepare_bin_for_exec: fputs() failed with '%s'\n", strerror(errno));
        fclose(bin_fp);
        return false;
    }

    // chmod(exec_bin_path, 0777) 
    if (chmod(exec_bin_path, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH) < 0)
    {
        printf("[!] prepare_bin_for_exec: chmod(exec_bin_path) failed with '%s'\n", strerror(errno));
        fclose(bin_fp);
        return false;
    }

    fclose(bin_fp);
    return true;

}


pid_t guess_next_pid()
{
    pid_t child_pid;
    child_pid = fork();

    if(child_pid >= 0) // fork was successful
    {
        if (child_pid == 0) // child process
            exit(EXIT_SUCCESS);
        else // parent process
        {
            wait(NULL);
            return child_pid + 1;
        }
    }
    else // fork failed
    {
        printf("[!] guess_next_pid: fork() failed with '%s'\n",strerror(errno));
        return -1;
    }

}


int catch_rt_guess_pid(const char * exec_bin_path)
{
    char runtime_path[SMALL_BUF_SIZE];
    pid_t guessed_next_pid;
    int rc, runtime_fd = -1;

    // Try to guess as what pid the runtime process will pop up as in our pid ns
    guessed_next_pid = guess_next_pid();
    if (guessed_next_pid < 0)
        return -1; // error printed in guess_next_pid

    // Prepare /proc/$guessed_pid/exe
    rc = snprintf(runtime_path, SMALL_BUF_SIZE, "/proc/%d/exe", guessed_next_pid);
    if (rc < 0) 
    {
        printf("[!] catch_rt_guess_pid: snprintf(runtime_path, /proc/%%d/exe) failed with '%s'\n", strerror(errno));
        return -1;
    }
    if (rc >= SMALL_BUF_SIZE)
    {
        printf("[!] catch_rt_guess_pid: snprintf(runtime_path, /proc/%%d/exe) failed, not enough space in buffer (required:%d, bufsize:%d)", rc, SMALL_BUF_SIZE);
        return -1;
    }

    // Try to catch the runtime
    printf("[+] Waiting for the runtime to exec into container (run `<runtime> exec <whoc-ctr-name> %s`)\n", exec_bin_path);
    while (runtime_fd < 0)
        runtime_fd = open(runtime_path, O_RDONLY);

    printf("[+] Got runtime as /proc/%u/exe\n", guessed_next_pid);
    return runtime_fd;
}


int catch_rt_getdents_proc(const char * exec_bin_path)
{
    int dir_fd, nread, bpos, runtime_fd = -1;

    struct linux_dirent64 *d;
    char rt_pexe_path[SMALL_BUF_SIZE];
    char dir_buf[LARGE_BUF_SIZE];

    dir_fd = open("/proc", O_RDONLY | O_DIRECTORY);
    if (dir_fd < 0)
    {
        printf("[!] catch_rt_getdents_proc: open('/proc') failed with '%s'\n", strerror(errno));
        return -1;
    }

    // Try to catch the runtime
    printf("[+] Waiting for the runtime to exec into container (run `<runtime> exec <whoc-ctr-name> %s`)\n", exec_bin_path);
    printf("[+] Searching for the runtime process under '/proc'...\n");

    // Go over dentries at /proc
    while (1) 
    {
        nread = syscall(SYS_getdents64, dir_fd, dir_buf, LARGE_BUF_SIZE);
        if (nread == 0)
        {
            // Finished going over dir, start over
            lseek(dir_fd,  0, SEEK_SET);
            continue;
        }
        if (nread == -1)
        {
            printf("[!] catch_rt_getdents_proc: getdents64 failed with '%s'\n", strerror(errno));
            goto out; // runtime_fd here is < 0, error
        }

        for (bpos = 0; bpos < nread; bpos += d->d_reclen) 
        {
            d = (struct linux_dirent64 *) (dir_buf + bpos);

            // Check if the currect directory is a process directory (first char is numeric) and that it's not us (PID 1)
            // This won't work for runtimes like rkt and lxd, where we aren't pid 1, and where other services exist in the container that
            // may spawn processes at random times. This is a conscious choice as whoc is mostly helpful for gaining visibility into CSP offerings which
            // commonly give the docker-like/runc/crun experience, where you use a docker image and have complete control over the processes in your pid ns.
            // In the future, if it's required, a more robust solution can be used (though it'll probably be slower and miss short-lived processes).
            if (isdigit(d->d_name[0]) && (d->d_name[0] != '1'))                                                          
            {
                sprintf(rt_pexe_path, "/proc/%s/exe", d->d_name);  // we need to be fast, don't check errors
                runtime_fd = open(rt_pexe_path, O_RDONLY);
                if (runtime_fd > 0)
                {
                    printf("[+] Got runtime as %s\n", rt_pexe_path);
                    goto out;
                }
            }
        }
   }

out:
   close(dir_fd);
   return runtime_fd;
}

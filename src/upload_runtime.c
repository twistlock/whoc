#include "upload_runtime.h"


void print_help(void)
{
    char * help_format = \
"Usage: upload_runtime [options] <server_ip>\n\n"
"Options:\n"
" -p, --port                 Port of remote server, defaults to %d\n"
" -e, --exec                 Wait for exec mode for static container runtimes, wait until an exec to the container occurred\n"
" -b, --exec-bin             In exec mode, overrides the default binary created for the exec, default is %d\n"
" -a, --exec-extra-argument  In exec mode, pass an additional argument to the runtime so it won't exit quickly\n"
" -r, --exec-readdir-proc    In exec mode, instead of guessing the runtime pid (which gives whoc one shot of catching the runtime),\n"
"                            find the runtime by searching for new processes under '/proc'.\n";
    printf(help_format, DEFAULT_PORT, DEFAULT_EXEC_BIN);
}


int parse_arguments(config * conf, int argc, char const *argv[])
{
    int opt;
    while ((opt = getopt_long(argc, (char * const*)argv, "b:p:a:er", long_options, NULL)) != -1)
    {
        switch (opt) 
        {
            case 'p':
                conf->port = atoi(optarg);
                break;
            case 'e':
                conf->wait_for_exec = true;
                break;
            case 'a':
                conf->exec_extra_arg = optarg;
                break;
            case 'b':
                conf->exec_bin = optarg;
                break;
            case 'r':
                conf->exec_readdir_mode = true;
                break;
            default: /* '?' */
                print_help();
                return 1;
        }
    }
    if (optind >= argc) 
    {
        print_help();
        return 1;
    }
    conf->server_ip = argv[optind];
    return 0;

}


bool send_post_file_http_header(int sockfd, const char * server_ip, unsigned long long file_size, const char * filename)
{
    char header[HEADER_BUF_SIZE];
    char * header_format;
    int rc;

    if (filename != NULL)
    {
        header_format =
          "POST / HTTP/1.1\r\n"
          "Host: %s\r\n"
          "Content-Type: application/octet-stream\r\n"
          "Content-Length: %llu\r\n" 
          "Content-Disposition: attachment; filename=\"%s\"\r\n"
          "Connection: close\r\n"
          "\r\n";

        rc = snprintf(header, HEADER_BUF_SIZE, header_format, server_ip, file_size, filename);
    }
    else
    {
        header_format =
          "POST / HTTP/1.1\r\n"
          "Host: %s\r\n"
          "Content-Type: application/octet-stream\r\n"
          "Content-Length: %llu\r\n" 
          "Content-Disposition: attachment\r\n"
          "Connection: close\r\n"
          "\r\n";

        rc = snprintf(header, HEADER_BUF_SIZE, header_format, server_ip, file_size);
    }

    if (rc < 0) 
    {
        printf("[!] send_http_header: snprintf(header) failed with '%s'\n", strerror(errno));
        return false;
    }
    if (rc >= HEADER_BUF_SIZE)
    {
        printf("[!] send_http_header: snprintf(header) failed, not enough space in buffer (required:%d, bufsize:%d)", rc, HEADER_BUF_SIZE);
        return false;
    }

    return send_all(sockfd, (void *) header, (size_t) strlen(header));

}


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
        else //Parent process
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
    printf("[+] Waiting for the runtime to exec into container (do '$runtime exec whoc-ctr-name %s')\n", exec_bin_path);
    printf("[+] Searching for runtime process under '/proc'...\n");

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

            // Check that's it's a process dir (first char is numeric) and that it's not us (PID 1)
            // This won't work for runtimes like rkt and lxd, where we aren't pid 1, and where other services exist in the container 
            // that may spawn processes at random times. This is a conscious choice as whoc is mostly helpful for gaining visibility into CSP offerings
            // which mostly give the docker-like/runc/crun experience, where you use a docker image and have complete control over the processes in your pid ns.
            // In the future, if it's required, a more robust solution can be used (though it will definitely be slower and probably miss short-lived processes)
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
    printf("[+] Waiting for the runtime to exec into container... (do '$runtime exec whoc-ctr-name %s')\n", exec_bin_path);
    while (runtime_fd < 0)
        runtime_fd = open(runtime_path, O_RDONLY);

    printf("[+] Got runtime as /proc/%u/exe\n", guessed_next_pid);
    return runtime_fd;
}




int main(int argc, char const *argv[])
{
    int sockfd, runtime_fd = -1;
    struct stat file_info;
    char rt_hostpath_buf[SMALL_BUF_SIZE], link_buf[SMALL_BUF_SIZE];  
    char * rt_hostpath, *ctr_link_to_rt;
    int rc;

    /* Default configuration */
    config conf = {
        .server_ip = NULL, 
        .port = DEFAULT_PORT, 
        .wait_for_exec = false,
        .exec_extra_arg = NULL,
        .exec_bin = DEFAULT_EXEC_BIN,
        .exec_readdir_mode = false
    };
    if (parse_arguments(&conf, argc, argv) != 0)
        return 1;

    /* Get a file descriptor for the container runtime, and a link to the runtime's path on the host */
    if (!conf.wait_for_exec)
    {
        // Running as the dynamic linker of the runtime process, so the runtime should be accessible at /proc/self/exe
        ctr_link_to_rt = "/proc/self/exe";  
        runtime_fd = open(ctr_link_to_rt, O_RDONLY); 
        if (runtime_fd < 0)
        {
            printf("[!] main: open(\"/proc/self/exe\") failed with '%s'\n", strerror(errno));
            return 1;
        }
    }
    else
    {   
        // Running as a normal process in the container, waiting for the runtime to exec in
        printf("[+] Running in wait for exec mode; preparing '%s'\n", conf.exec_bin);

        // Create an executable at conf.exec_bin containing a shebang that points to the container runtime (#!/proc/self/exe)
        if (prepare_bin_for_exec(conf.exec_bin, conf.exec_extra_arg) == false)
            return 1;  // error printed in prepare_bin_for_exec()

        // Open fd to runtime
        if (!conf.exec_readdir_mode)
            runtime_fd = catch_rt_guess_pid(conf.exec_bin);
        else
            runtime_fd = catch_rt_getdents_proc(conf.exec_bin);
        if (runtime_fd < 0)
            return 1;

        // The runtime process may have already exited, so we'll try to read the runtime's path from the fd we opened for it
        // Prepare /proc/self/fd/$runtime_fd
        rc = snprintf(link_buf, SMALL_BUF_SIZE, "/proc/self/fd/%d", runtime_fd);
        if (rc < 0) 
        {
            printf("[!] main: snprintf(link_buf) failed with '%s'\n", strerror(errno));
            goto close_runtime_ret_1;
        }
        if (rc >= SMALL_BUF_SIZE)
        {
            printf("[!] main: snprintf(link_buf) failed, not enough space in buffer (required:%d, bufsize:%d)", rc, SMALL_BUF_SIZE);
            goto close_runtime_ret_1;
        }
        ctr_link_to_rt = link_buf;
    }

    /* Get container runtime size */
    if(fstat(runtime_fd, &file_info) != 0)
    {
        printf("[!] upload_file: fstat(fp) failed with '%s'\n", strerror(errno));
        goto close_runtime_ret_1;
    }
    /* Try to get the runtime's path on the host */
    rc = readlink(ctr_link_to_rt, rt_hostpath_buf, SMALL_BUF_SIZE);
    if (rc < 0)
    {
        printf("[!] main: readlink(ctr_link_to_rt) failed with '%s', continuing without the runtime's path\n", strerror(errno));
        rt_hostpath = NULL;
    }
    else
    {
        rt_hostpath_buf[rc] = 0;
        rt_hostpath = rt_hostpath_buf;
    }

    printf("[+] Uploading...\n");

    /* connect to server */
    sockfd = connect_to_server(conf.server_ip, conf.port, SEND_TIMEOUT, RECV_TIMEOUT);
    if (sockfd < 0)
        goto close_runtime_ret_1;

    /* send http POST request header */
    if (send_post_file_http_header(sockfd, conf.server_ip, (unsigned long long)file_info.st_size, rt_hostpath) == false)
        goto close_both_ret_1;

    /* send container runtime to server */
    if (sendfile_all(sockfd, runtime_fd, (size_t)file_info.st_size) == false)
        goto close_both_ret_1;

    printf("[+] Done\n");
    return 0;

close_both_ret_1:
    close(sockfd);
close_runtime_ret_1:
    close(runtime_fd); 
    return 1;
}

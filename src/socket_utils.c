#include "socket_utils.h"


int connect_to_server(const char *server_ip, unsigned int port, time_t send_timeout, time_t recv_timeout)
{
    int sockfd;
    struct sockaddr_in serv_addr; /* connector's address information */
    struct timeval timeout;      

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
    {
        printf("[!] connect_to_server: failed to create socket with '%s'\n", strerror(errno));
        return -1;
    }

    // Set send timeout to 5 minutes
    timeout.tv_sec = send_timeout; //300;
    timeout.tv_usec = 0;
    if (setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
    {
        printf("[!] connect_to_server: setsockopt(SO_RCVTIMEO) failed with '%s'\n", strerror(errno));
        close(sockfd);
        return -1; 
    } 
    // Set receive timeout to 2 minutes
    timeout.tv_sec = recv_timeout; //120;
    if (setsockopt (sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
    {
        printf("[!] connect_to_server: setsockopt(SO_SNDTIMEO) failed with '%s'\n", strerror(errno));
        close(sockfd);
        return -1; 
    } 
    // TODO: consider setsockopt TCP_CORK for better performance

    // Convert IPv4 and IPv6 addresses from text to binary form 
    if(inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0)  
    { 
    	if (errno == 0)
    		printf("[!] connect_to_server: inet_pton() failed, invalid address\n");
    	else
        	printf("[!] connect_to_server: inet_pton() failed with '%s'\n", strerror(errno));
        close(sockfd);
        return -1; 
    } 
    serv_addr.sin_family = AF_INET;      /* host byte order */
    serv_addr.sin_port = htons(port);    /* short, network byte order */
    bzero(&(serv_addr.sin_zero), 8);     /* zero the rest of the struct */

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1) 
    {
        printf("[!] connect_to_server: connect() failed with '%s'\n", strerror(errno));
    	close(sockfd);
        return -1; 
    }
    return sockfd;
}


bool send_all(int socket, void *buffer, size_t length)
{
    char *ptr = (char*) buffer;
    int i;
    while (length > 0)
    {
        i = send(socket, ptr, length, 0);
        if (i < 0)  // TODO: should this be less than 1?
        {
        	printf("[!] send_all: send failed with '%s'\n", strerror(errno));
        	return false;
        }
        ptr += i;
        length -= i;
    }
    return true;
}


bool sendfile_all(int sockfd, int src_fd, size_t file_size)
{
    off_t offset = 0;
    int rv;
    while (offset < file_size) 
    {
        rv = sendfile(sockfd, src_fd, &offset, file_size - offset);
        if (rv < 0) 
        {
        	printf("[!] send_all: sendfile failed with '%s'\n", strerror(errno));
            return false;
        }
        offset += rv;
    }
    return true;
}




#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <fcntl.h>
#define DATAFILE_NAME "/var/tmp/aesdsocketdata"
bool run_server;
int sockfd, new_fd;
FILE *fptr;
static void signal_handler(int signal_number)
{
    if ((signal_number == SIGINT) | (signal_number == SIGTERM))
    {
        syslog(LOG_INFO, "Caught signal, exiting\n");
        printf("received sigint/sigterm\n");
        shutdown(sockfd, SHUT_RDWR);
        run_server = false;
    }
}
int send_file(int socket_fd, FILE* file_fd)
{
    int rc;
    size_t size;
    
	char buffer[1024];
    // set file offset to begin
    rc = fseek(file_fd, 0, SEEK_SET);
    if (rc == -1)
    {
        syslog(LOG_ERR, "Error lseek: %s\n", strerror(errno));
        return -1;
    }
    while((size = fread(buffer, sizeof(char), 1024, file_fd)) > 0)
    {
        printf("send data: %.*s", (int)size, buffer);
        rc = send(socket_fd, buffer, size, 0);
        // handle not complete send?
    }
    return 0;
}
int file_append(FILE* fd, char *buffer, int size)
{
    int rc;
    // set file offset to end
    rc = fseek(fd, 0, SEEK_END);
    if (rc == -1)
    {
        syslog(LOG_ERR, "Error lseek: %s\n", strerror(errno));
        return -1;
    }
    // rc = write(fd, buffer, size);
    rc = fwrite(buffer, 1, size, fd);
    if (rc != size)
    {
        syslog(LOG_ERR, "Error writing to file: %s", strerror(errno));
        return -1;
    }
    return rc;
}
int start_daemon()
{
    pid_t pid = fork();
    if(pid > 0)
    {
        exit(0);
    }
    chdir("/");
    return 0;
}
int main(int argc, char* argv[])
{
    struct addrinfo hints;
    struct addrinfo *servinfo;
	struct sockaddr_storage their_addr;
    socklen_t addr_size;
    int ret;
	char buffer[1024];
    struct sigaction new_action;
    memset(&new_action,0,sizeof(struct sigaction));
	// setup syslog
	openlog(NULL, 0, LOG_USER);
	syslog(LOG_INFO, "Start logging");
    printf("Start aesd socket\n");

    // setup signal handler
    new_action.sa_handler=signal_handler;
    if(sigaction(SIGTERM, &new_action, NULL) != 0)
    {
        syslog(LOG_ERR, "Error setting up sigaction for SIGTERM");
        return -1;
    }
    if(sigaction(SIGINT, &new_action, NULL) != 0)
    {
        syslog(LOG_ERR, "Error setting up sigaction for SIGINT");
        return -1;
    }
    run_server = true;



    // open stream socket bound to port 9000
    sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if(sockfd == -1)
    {
        syslog(LOG_ERR, "Error opening socket: %s\n", strerror(errno));
        return -1;
    }
    // setup sockaddr
    memset(&hints, 0, sizeof hints);
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    ret = getaddrinfo(NULL, "9000", &hints, &servinfo);
    if (ret != 0)
    {
        syslog(LOG_ERR, "Error setting up addrinfo using getaddrinfo. Errorcode: %s, Errno: %s\n", 
            gai_strerror(ret), strerror(errno));
        return -1;
    }

    // bind to open socked using created sockaddr
    //ret = bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
    ret = bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
    if(ret == -1)
    {
        syslog(LOG_ERR, "Error binding socket: %s\n", strerror(errno));
        return -1;
    }
    // addrinfo is not required anymore after bind
    freeaddrinfo(servinfo);
    // start daemon mode if required
    if(argc == 2 && strcmp(argv[1], "-d") == 0)
    {
        start_daemon();
    }
	// listen
	ret = listen(sockfd, 20);
    if(ret == -1)
    {
        syslog(LOG_ERR, "Error listening to socket: %s\n", strerror(errno));
        return -1;
    }
    while(run_server)
    {
        // accept
        addr_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);
        if(new_fd == -1)
        {
            syslog(LOG_ERR, "Error accepting socket: %s\n", strerror(errno));
            break;
        }
        // logging address using getnameinfo
        char hoststr[NI_MAXHOST];
        char portstr[NI_MAXSERV];
        ret = getnameinfo((struct sockaddr *)&their_addr, addr_size, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);
        if(ret != 0)
        {
            return -1;
        }
        syslog(LOG_INFO, "Accepted connection from %s", hoststr);
        // write buffer to file
        // open file for writing
        fptr = fopen(DATAFILE_NAME, "a+");
        // check for opening error
        if(fptr == NULL)
        {
            syslog(LOG_ERR, "Error opening file var/tmp/aesdsocketdata: %s\n", strerror(errno));
            return 1;
        }
        while(1)
        {
            // receive data
            ret = recv(new_fd, &buffer, 1024, 0);
            // check if connection was closed
            if(ret == 0)
            {
                syslog(LOG_INFO, "Closed connection from %s", hoststr);
                break;
            }
            else if(ret < 0)
            {
                printf("recv error\n");
                syslog(LOG_ERR, "Recv error: %s", strerror(errno));
                break;
            }
            else if (ret > 0)
            {
                // received valid data, check in length if newline is found
                printf("received data\n");
                if(strchr(buffer, '\n'))
                {
                    // append line to file
                    file_append(fptr, buffer, ret);
                    printf("written data: %.*s", ret, buffer);
                    fsync(fileno(fptr));
                    // send all data from file
                    send_file(new_fd, fptr);
                }
                else if(ret == 1024)
                {
                    file_append(fptr, buffer, ret);
                }
            }
        }
    }
    // cleanup
    if (sockfd)
        close(sockfd);
    if (new_fd)
        close(new_fd);
    if (fptr)
        fclose(fptr);
    remove(DATAFILE_NAME);
    printf("Server closed\n");
	return 0;
}
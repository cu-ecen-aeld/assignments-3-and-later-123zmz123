#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#define PORT_NUM 9000
#define MAX_CLIENTS 5
#define BUFF_SIZE 1024
#define FILE_NAME "/var/tmp/aesdsocketdata"

static volatile int is_running = 1;
static volatile int file_fd = -1, server_fd = -1, client_fd = -1;

void sig_handler(int signal)
{
    if (file_fd >= 0)
        if (close(file_fd)) 
            syslog(LOG_ERR, "%s: %m", "Close the file");
    
    if (client_fd >= 0){
        if (shutdown(client_fd, SHUT_RDWR))
            syslog(LOG_ERR, "%s: %m", "Shutdown the connection");
        if (close(client_fd))
            syslog(LOG_ERR, "%s: %m","Close the client sockect");
    }


    if (server_fd >= 0)
        if (close(server_fd))
            syslog(LOG_ERR, "%s: %m", "close the server socket");
    
    if (unlink(FILE_NAME)) // delete the file by path
        syslog(LOG_ERR,"%s:%m","delete file error");
    

    if (signal == SIGINT || signal == SIGTERM){
        syslog(LOG_DEBUG,"%s", "caught signal exit!");
        is_running=0;
        exit(EXIT_SUCCESS); // exit by return 0
    }

    if (signal == -1){
        exit(EXIT_FAILURE);// exit with 1 means we encounter some issue
    }
}

void log_error(const char* message) {
    syslog(LOG_ERR, "%s: %m", message);
}

void log_done(const char* msg){
    syslog(LOG_INFO,"%s: %m",msg);
}

int main(int argc, char * argv[]){
    int optval=1;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFF_SIZE];

    openlog("aesdsocket", LOG_PID, LOG_USER);


    // Create server socket error
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) 
    
    {
        log_error("server socket create failed");
        return -1;
    }
    log_done("create server socket");



    // setup the server configurations
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT_NUM);


    if (setsockopt(server_fd,SOL_SOCKET,SO_REUSEADDR,&optval, sizeof(optval)) == -1){
        log_error("failed to set sockect options of server sockect");
        close(server_fd);
        return -1;
    }

    log_done("set socket opt done");

    if (bind(server_fd,(struct sockaddr*)&server_addr,sizeof(server_addr)) == -1){
        log_error("Bind server_addr to server socket failed");
        close(server_fd);
        return -1;
    }

    log_done("bind server done");

    if (argc>1){
        if (strcmp(argv[1], "-d") == 0){
            int pid = fork();
            if (pid == -1){
                log_error("fork is wrong");
                close(server_fd);
                return -1;
            }
            else if (pid != 0){
                close(server_fd);
                exit(EXIT_SUCCESS);
            }

            if (pid == 0){
                if (setsid() == -1){ // child process detatch from terminal
                    log_error("detatch from terminal failed");
                    close(server_fd);
                    return -1;
                }

                if ( chdir("/") == -1){
                    log_error("chir to root failed");
                    close(server_fd);
                    return -1;
                }

                for (int i=0; i<3; i++) // close stdin stdout stderr
                    close(i);
                open("/dev/null", O_RDWR);
                dup(0);
                //dup(0); // why not comment?
                
            }
        }
    }

     // Listen for incoming connections
    if (listen(server_fd, MAX_CLIENTS) == -1)
    {
        log_error("failed for server listening");
        close(server_fd);
        return -1;
    }

    log_done("listen success");

    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);

    long byts_read=0;
    while (is_running){
        if((client_fd = accept(server_fd,(struct sockaddr *)&client_addr, (socklen_t*)&addr_len)) < 0) {
            log_error("Accept failed");
            continue;
        }

        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        syslog(LOG_DEBUG, "Accept connection from %s", client_ip);


        // Read data from the client connection
        while ((byts_read = recv(client_fd, buffer, BUFF_SIZE, 0)) > 0) {
            if (byts_read == -1){
                log_error("recv");
                sig_handler(-1);
            }
             // Append the data to the file
            // Open if closed
            if (file_fd < 0) { 
                file_fd = open(FILE_NAME, O_CREAT | O_RDWR | O_APPEND, 0644);
                if (file_fd < 0) {
                    log_error("open /var/tmp/aesdsocketdata file is failed");
                    sig_handler(-1);
                }
            }

            write(file_fd, &buffer, byts_read);
            log_done("write done");
            if (buffer[byts_read-1] == '\n')
                break;
        }

        if (lseek(file_fd, (off_t) 0, SEEK_SET) != (off_t)  0){
            log_error("fail to seek the file starter");
            sig_handler(-1);
        }

        int byts_send;
        while ((byts_read = read(file_fd, &buffer, BUFF_SIZE)) > 0){
            while ((byts_send = send(client_fd, &buffer, byts_read, 0)) < byts_read){
                log_error("Fail send");
                sig_handler(-1);
            }
        }


        close(file_fd);
        file_fd = -1;
        close(client_fd);
        client_fd = -1;
        syslog(LOG_DEBUG, "Close connection from %s", client_ip); 
        closelog();       
    } 
    exit(EXIT_SUCCESS);
}
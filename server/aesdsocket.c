#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include "./queue.h"

#define PORT 9000
#define MAX_CLIENTS 5
#define BUF_SIZE 1024
#define FILENAME "/dev/aesdchar"
#define KEEPALIVE 10

static volatile int running = 1;
static volatile int wait_connection = 0;
static volatile int file_fd = -1, server_fd = -1;
sigset_t block_set;
pthread_mutex_t lock;

typedef struct client_thr_s client_thr_t;
struct client_thr_s{
    pthread_t thr_id;
    int client_fd;
    int state;
};

typedef struct clt_lst_s clt_lst_t;
struct clt_lst_s{
    client_thr_t *clt;
    SLIST_ENTRY(clt_lst_s) next;
};

SLIST_HEAD(slisthead, clt_lst_s) head;

/*****************************************************
*
* Service Functions
*
*****************************************************/
void cleanup_threads(void){

    clt_lst_t *node=NULL;
    clt_lst_t *tnode=NULL;

    SLIST_FOREACH_SAFE(node, &head, next, tnode) {
        if(node == NULL)
            break;
        if (node->clt->state == 1){
            if (pthread_join(node -> clt -> thr_id, NULL) != 0)
                syslog(LOG_ERR, "%s: %m", "Error join thread");

            SLIST_REMOVE(&head, node, clt_lst_s, next);
            free(node -> clt);
            free(node);
        }
    }

}

void exit_norm(void){
    // wait for ending threads
    while (!SLIST_EMPTY(&head))
        cleanup_threads();

    if (close(server_fd) == -1)
        syslog(LOG_ERR, "%s: %m", "Close server descriptor");

//    if (fsync(file_fd) < 0)
//        syslog(LOG_ERR, "%s: %m", "Error sync to disk before close");



    pthread_mutex_destroy(&lock);
    exit(EXIT_SUCCESS);
}

/*****************************************************
*
* Signal handlers
*
*****************************************************/

void exit_handler(int sig)
{
    running = 0;
    syslog(LOG_DEBUG,"%s", "Caught signal, exiting");
    if (wait_connection){
        exit_norm();
    }

}

void timer_handler(int sig){
    time_t current_time;
    struct tm *time_info;
    char time_str[80];

    memset(&current_time, 0, sizeof(time_t));
    time(&current_time);

    time_info = localtime(&current_time);

    strftime(time_str, sizeof(time_str), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", time_info);
    syslog(LOG_DEBUG,"%s",time_str);
    /* write to file */
    // pthread_mutex_lock(&lock); is unnecessary here. write is signal and thread safe due to POSIX.
    // and SIGALRM is blocked during packet processing
    // TODO check error and partial write
    write(file_fd, &time_str, strlen(time_str));
}

/*****************************************************
*)
* Socket functions
*
*****************************************************/

void *process_connection(void *thread_data){
    client_thr_t *data = (client_thr_t*) thread_data;
    data ->thr_id = pthread_self();

    int client_fd = data -> client_fd;

    // prepare buffer
    char buffer[BUF_SIZE];
    memset(&buffer, 0, BUF_SIZE);

    sigset_t old_set;
    sigemptyset(&old_set);

    // Read data from the client connection
    int packet = 0;
    ssize_t bytes_read=0;

    // Exit from loop to label in case error or closed connection
    do{
        while ( (bytes_read = recv(client_fd, buffer, BUF_SIZE, 0)) > 0) {
            // Lock mutex and block signals if new packet
            if (!packet){
                sigprocmask(SIG_BLOCK, &block_set, &old_set);
                pthread_mutex_lock(&lock);
                packet = 1;
            }

            // Open file for timestamps and data
            file_fd = open(FILENAME, O_CREAT | O_RDWR | O_APPEND | O_TRUNC | O_SYNC, 0644);
            if (file_fd < 0) {
                syslog(LOG_ERR, "%s: %m", "Failed to open data file");
                goto clean_thread;
            }

            // Append the data to the file
            // TODO check error and partial write
            write(file_fd, &buffer, bytes_read);

//            // ensure that data on disk
//            if (fsync(file_fd) < 0)
//                syslog(LOG_ERR, "%s: %m", "Error sync packet to disk");

            syslog(LOG_DEBUG,"received %ld bytes", bytes_read);
            syslog(LOG_DEBUG,"received %s", buffer);

            // if full packet received go to response (send)
            if (buffer[bytes_read-1] == '\n'){
                break;
            }
        }

        // Error read from socket
        if (bytes_read == -1){
            syslog(LOG_ERR, "%s: %m", "Error recv");
            goto clean_thread;
        }

        // connection closed by client
        if (bytes_read == 0){
            syslog(LOG_DEBUG,"%s", "Connection closed by client");
            goto clean_thread;
        }



        int bytes_send;
        off_t file_pos=0;

        while ((bytes_read = pread(file_fd, &buffer, BUF_SIZE, file_pos)) > 0){
            file_pos += bytes_read;
            // TODO check error and partial send
            if ((bytes_send = send(client_fd, &buffer, bytes_read, 0)) < bytes_read){
                syslog(LOG_ERR, "%s: %m", "Fail send");
                goto clean_thread;
            }
            syslog(LOG_DEBUG,"read  %ld bytes", bytes_read);
            syslog(LOG_DEBUG,"send %d bytes", bytes_send);
        }

        if (bytes_read == -1){
            syslog(LOG_ERR, "%s: %m", "Error read from file");
            goto clean_thread;
        }

        if (close(file_fd) == -1)
        syslog(LOG_ERR, "%s: %m", "Close file");

        memset(&buffer, 0, BUF_SIZE);
        pthread_mutex_unlock(&lock);
        sigprocmask(SIG_SETMASK, &old_set, NULL);
        packet = 0;
    }while(1);


    // unlock mutex and unblock signals
    clean_thread: if (packet){
        if(file_fd >=0)
            lseek(file_fd, (off_t) 0, SEEK_END); // try to seek to end of file here error check have no sense
        pthread_mutex_unlock(&lock);
        sigprocmask(SIG_SETMASK, &old_set, NULL);
        packet=0;

    }



    // close client socket
    if (close(client_fd) == -1)
        syslog(LOG_ERR, "%s: %m", "Error Close socket descriptor");

    // mark thread as finished
    data -> state = 1;

    //pthread_exit(NULL);
    return NULL;
}

void accept_connection(void){

    int client_fd = -1;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char client_ip[INET_ADDRSTRLEN];

     /* Accept a new client connection */

    wait_connection = 1;
    if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t*)&addr_len)) < 0) {
        syslog(LOG_ERR, "%s: %m", "Accept failed");
        return;
    }
    wait_connection = 0;

    // Log connection details to syslog
    if (!inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN)){
        syslog(LOG_ERR, "%s: %m", "Error convert address");
    }
    else
        syslog(LOG_DEBUG, "Accepted connection from %s:%d", client_ip,client_addr.sin_port);


    /* Create new thread */
    // Struct for data
    client_thr_t *data=NULL;
    data = malloc(sizeof(client_thr_t));
    if (data == NULL){
        syslog(LOG_ERR, "%s: %m", "Error allocate memory for thread data");
        return;
    }

    data -> thr_id = 0;
    data -> client_fd = client_fd;
    data -> state = 0;

    // create node for storing thread in list
    clt_lst_t *node=NULL;
    node = malloc(sizeof(clt_lst_t));
    if (node == NULL){
        syslog(LOG_ERR, "%s: %m", "Error allocate memory for list node");
        return;
    }
    node -> clt = data;

    // Create thread
    pthread_t thread; // for storing thr_id

    if (pthread_create( &thread, NULL, process_connection, (void*) data) !=0){
        syslog(LOG_ERR, "%s: %m", "Error create new thread");
    }

    /* add thread to list*/
    cleanup_threads();
    SLIST_INSERT_HEAD(&head, node, next);

}

/*****************************************************
*
* Main
*
*****************************************************/
int main(int argc, char * argv[]){
     // Open syslog with LOG_USER facility
    openlog(NULL, 0, LOG_USER);
    syslog(LOG_DEBUG,"%s","STARTED");

    // Create mutex
    if (pthread_mutex_init(&lock, NULL) !=0){
        syslog(LOG_ERR, "%s: %m", "Error initialize mutex");
        goto err;
    };

    // prepare set for blocking signals
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGALRM);
    sigaddset(&block_set, SIGINT);
    sigaddset(&block_set, SIGTERM);


     // Set up the signal handler using sigaction
    struct sigaction sa_int, sa_term,sa_alrm;//, sa_io;

    sa_int.sa_handler = exit_handler;
    sigemptyset(&sa_int.sa_mask);
    sigaddset(&sa_int.sa_mask, SIGALRM);
    sigaddset(&sa_int.sa_mask, SIGTERM);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);

    sa_term.sa_handler = exit_handler;
    sigemptyset(&sa_term.sa_mask);
    sigaddset(&sa_term.sa_mask, SIGALRM);
    sigaddset(&sa_term.sa_mask, SIGINT);
    sa_term.sa_flags = 0;
    sigaction(SIGTERM, &sa_term, NULL);

    sa_alrm.sa_handler = timer_handler;
    sigemptyset(&sa_alrm.sa_mask);
    sigaddset(&sa_alrm.sa_mask, SIGTERM);
    sigaddset(&sa_alrm.sa_mask, SIGINT);
    sa_alrm.sa_flags = 0;
    //sigaction(SIGALRM, &sa_alrm, NULL);

    /*
    *   Main socket section
    */

     // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM|SOCK_CLOEXEC, 0)) == -1){
        syslog(LOG_ERR, "%s: %m", "Failed to create socket");
        goto cleanup_thread;
    }


    struct sockaddr_in server_addr;

    // Set the server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);


     int  opt = 1;
    // Set socket options to reuse address and enable keepalive
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR|SO_KEEPALIVE, &opt, sizeof(opt)) == -1)
    {
        syslog(LOG_ERR, "%s: %m", "Failed to set socket options");
        goto cleanup_server;
    }

    // Bind socket to port
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        syslog(LOG_ERR, "%s: %m", "Bind failed");
        goto cleanup_server;
    }


    /* Check cmd line arguments. Run as daemon if necessary */
    if (argc>1){
        if (strcmp(argv[1], "-d") == 0){
            int pid = fork();
            // error fork
            if (pid == -1){
                syslog(LOG_ERR, "%s: %m", "fork");
                goto cleanup_server;
            }
            // parent
            else if (pid != 0){
                close(server_fd);
                exit(EXIT_SUCCESS);
            }

            // Daemon section
            if (pid == 0){

                if (setsid() == -1){
                    syslog(LOG_ERR, "%s: %m", "Error create new session");
                    goto cleanup_server;
                }

                if ( chdir("/") == -1){
                    syslog(LOG_ERR, "%s: %m", "Error chdir to /");
                    goto cleanup_server;
                }

                // redirect stdout stdin stderr
                for (int i=0; i<3; i++)
                    if (i != file_fd)
                        close(i);
                open("/dev/null", O_RDWR);
                dup(0);
                dup(0);

            }
        }
    }

    // Listen for incoming connections
    if (listen(server_fd, MAX_CLIENTS) == -1)
    {
        syslog(LOG_ERR, "%s: %m", "Failed to listen for incoming connections");
        goto cleanup_server;
    }


    /* Set timer*/
    /*struct itimerval timer;
    timer.it_interval.tv_sec = (time_t)KEEPALIVE;
    timer.it_interval.tv_usec = (suseconds_t)KEEPALIVE;
    timer.it_value.tv_sec = (time_t)10;
    timer.it_value.tv_usec = (suseconds_t)0;

    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        syslog(LOG_ERR, "%s: %m", "Error set timer");
        goto cleanup_server ;
    }*/

    /* Loop forever accepting incoming connections */
    // Init list

    SLIST_INIT(&head);

    while (running){
        accept_connection();
    }


     exit_norm();

    /* Error section */
    cleanup_server: if (close(server_fd) == -1)
            syslog(LOG_ERR, "%s: %m", "Close server descriptor");

    cleanup_thread: pthread_mutex_destroy(&lock);

    err: return -1;

}
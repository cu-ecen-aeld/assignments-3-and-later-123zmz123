#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    struct thread_data* args = (struct thread_data*) thread_param;
    if(!usleep(args->wait_obtain_ms*1000)) args->thread_complete_success=false;
    pthread_mutex_lock(args->mutex);
    usleep(args->wait_release_ms*1000);
    pthread_mutex_unlock(args->mutex);
    args->thread_complete_success=true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    int res;
    struct thread_data* args=malloc(sizeof(struct thread_data));

    if(args==NULL){
        perror("Error on create thread_data object");
        return 1;
    }

    args->mutex = mutex;
    args->wait_obtain_ms=wait_to_obtain_ms;
    args->wait_release_ms=wait_to_release_ms;

    res = pthread_create(thread,NULL,&threadfunc,(void*)args);
    if(res !=0){
        perror("thread creat error");
        args->thread_complete_success=false;
        return false;
    }



    
    return true;
}


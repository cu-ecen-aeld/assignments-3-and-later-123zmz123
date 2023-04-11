#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <stdint.h>

int file_write(char* filename, char* content)
{
    FILE *fptr;
    fptr = fopen(filename,"w");
    if(fptr == NULL) 
    {
        syslog(LOG_ERR, "file created failed the file name is %s, erro no %d: %s",filename,errno,strerror(errno));
        return 1;
    }

    fprintf(fptr,"%s",content);
    fclose(fptr);
    syslog(LOG_DEBUG|LOG_USER|LOG_INFO,"write %s to %s",content,filename);
    return 0;
}

int main(int argc, char* argv[]){
    if(argc!=3){
        syslog(LOG_ERR," error! argument length should set to 3");
        return 1;
    }
    char* filename = argv[1];
    char* content = argv[2];
    file_write(filename,content);
    return 0;

}
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc ,char* argv[]){
    int status=-1;
    pid_t pid=fork();
    printf("Hello World\n");
    if(pid==0){
        printf("Hello child\n");
        exit(0);
    }else{
        wait(&status);
        printf("Hello parent\n");
    }
    if(status ==0){
        printf("Pussy");
    }
    return 0;
}
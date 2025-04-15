#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <string.h>

int main(){
    char string[100];
    while(1){
        printf("Enter a reading input:");
        fgets(string,sizeof(string),stdin);
        pid_t pid=fork();
        if(pid==0){
            printf("%s",string);
            exit(0);
        }
        else{
            wait(NULL);
        }
    }
    return 0;
}
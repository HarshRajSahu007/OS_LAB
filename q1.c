#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/shm.h>

#define SHM_SIZE 4096
#define MAX_ELEMENT 1000

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

void merge(int arr[],int l ,int m ,int r);
void mergeSort(int arr[],int l,int r,int semid);
void semaphore_p(int semid);
void semaphore_v(int semid);

int main(){
    int shmid,semid;
    key_t key= ftok("shmfile",65);
    int *shared_array;
    int n=10;
    shmid=shmget(key,SHM_SIZE,IPC_CREAT | 0666);
    if(shmid<0){
        perror("shmget");
        exit(1);
    }
    shared_array= (int*)shmat(shmid,NULL,0);
    if(shared_array == (int*)-1){
        perror("shmat");
        exit(1);
    }
    semid=semget(key,1,IPC_CREAT|0666);
    if(semid<0){
        perror("semget");
        exit(1);
    }
    union semun sem_arg;
    sem_arg.val=1;
    if(semctl(semid,0,SETVAL,sem_arg)==-1){
        perror("semctl");
        exit(1);
    }
    for(int i=0;i<n;i++){
        shared_array[i]=rand()%100;
    }
    printf("Original array: ");
    for (int i = 0; i < n; i++) {
        printf("%d ", shared_array[i]);
    }
    printf("\n");
    pid_t pid=fork();
    if(pid<0){
        perror("fork");
        exit(1);
    }
    else if(pid==0){
        mergeSort(shared_array,n/2,n-1,semid);
        exit(0);
    }
    else{
        mergeSort(shared_array,0,n/2-1,semid);

        wait(NULL);

        semaphore_p(semid);
        merge(shared_array,0,n/2-1,n-1);
        semaphore_v(semid);
        printf("Sorted array :");
        for(int i=0;i<n;i++){
            printf("%d",shared_array[i]);
        }
        printf("\n");
        shmdt(shared_array);
        shmctl(shmid,IPC_RMID,NULL);
        semctl(semid,0,IPC_RMID);
    }
    return 0;
}

void merge(int arr[], int l, int m, int r) {
    int i, j, k;
    int n1 = m - l + 1;
    int n2 = r - m;
    

    int L[n1], R[n2];
    

    for (i = 0; i < n1; i++)
        L[i] = arr[l + i];
    for (j = 0; j < n2; j++)
        R[j] = arr[m + 1 + j];
    

    i = 0;
    j = 0;
    k = l;
    while (i < n1 && j < n2) {
        if (L[i] <= R[j]) {
            arr[k] = L[i];
            i++;
        } else {
            arr[k] = R[j];
            j++;
        }
        k++;
    }
    

    while (i < n1) {
        arr[k] = L[i];
        i++;
        k++;
    }
    

    while (j < n2) {
        arr[k] = R[j];
        j++;
        k++;
    }
}


void mergeSort(int arr[], int l, int r, int semid) {
    if (l < r) {
        int m = l + (r - l) / 2;
        

        mergeSort(arr, l, m, semid);
        mergeSort(arr, m + 1, r, semid);
        

        semaphore_p(semid);
        merge(arr, l, m, r);
        semaphore_v(semid);
    }
}
void semaphore_p(int semid){
    struct sembuf sb={0,-1,0};
    if(semop(semid,&sb,1)==-1){
        perror("semop P");
        exit(1);
    }

}


void semaphore_v(int semid){
    struct sembuf sb = {0,1,0};
    if(semop(semid,&sb,1)==-1){
        perror("semop V");
        exit(1);
    }
}
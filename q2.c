#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>

#define SHM_SIZE 4096
#define MAX_NODES 100
#define MAX_RESULTS 1000

// Structure for binary tree node in shared memory
typedef struct {
    int value;
    int left;  // Index of left child in the array
    int right; // Index of right child in the array
    bool processed;
    int result; // To store processing result
} Node;

// Structure for shared memory
typedef struct {
    Node nodes[MAX_NODES];
    int node_count;
    int result_count;
    int results[MAX_RESULTS];
} SharedMemory;

// Union for semaphore operations
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

// Function prototypes
void processNode(SharedMemory *shm, int node_idx, int semid);
void semaphore_p(int semid);
void semaphore_v(int semid);
int createBinaryTree(SharedMemory *shm);

int main() {
    int shmid, semid;
    key_t key = ftok("shmfile", 65);
    SharedMemory *shm;
    
    // Create shared memory segment
    shmid = shmget(key, sizeof(SharedMemory), IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget");
        exit(1);
    }
    
    // Attach shared memory segment
    shm = (SharedMemory*)shmat(shmid, NULL, 0);
    if (shm == (SharedMemory*)-1) {
        perror("shmat");
        exit(1);
    }
    
    // Create semaphore
    semid = semget(key, 1, IPC_CREAT | 0666);
    if (semid < 0) {
        perror("semget");
        exit(1);
    }
    
    // Initialize semaphore
    union semun sem_arg;
    sem_arg.val = 1;
    if (semctl(semid, 0, SETVAL, sem_arg) == -1) {
        perror("semctl");
        exit(1);
    }
    
    // Initialize shared memory
    memset(shm, 0, sizeof(SharedMemory));
    shm->result_count = 0;
    
    // Create a sample binary tree
    int root_idx = createBinaryTree(shm);
    
    printf("Binary Tree created with %d nodes, root index: %d\n", shm->node_count, root_idx);
    
    // Start traversal from the root
    processNode(shm, root_idx, semid);
    
    // Wait for all child processes to complete
    while (wait(NULL) > 0);
    
    // Print results
    printf("Results from tree traversal:\n");
    for (int i = 0; i < shm->result_count; i++) {
        printf("%d ", shm->results[i]);
    }
    printf("\n");
    
    // Detach and remove shared memory
    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);
    
    // Remove semaphore
    semctl(semid, 0, IPC_RMID);
    
    return 0;
}

// Create a sample binary tree in shared memory
int createBinaryTree(SharedMemory *shm) {
    shm->node_count = 0;
    
    // Create root
    int root_idx = shm->node_count++;
    shm->nodes[root_idx].value = 10;
    shm->nodes[root_idx].processed = false;
    
    // Add left child
    int left_idx = shm->node_count++;
    shm->nodes[left_idx].value = 5;
    shm->nodes[left_idx].processed = false;
    shm->nodes[root_idx].left = left_idx;
    
    // Add right child
    int right_idx = shm->node_count++;
    shm->nodes[right_idx].value = 15;
    shm->nodes[right_idx].processed = false;
    shm->nodes[root_idx].right = right_idx;
    
    // Add children to left node
    int left_left_idx = shm->node_count++;
    shm->nodes[left_left_idx].value = 3;
    shm->nodes[left_left_idx].processed = false;
    shm->nodes[left_idx].left = left_left_idx;
    
    int left_right_idx = shm->node_count++;
    shm->nodes[left_right_idx].value = 7;
    shm->nodes[left_right_idx].processed = false;
    shm->nodes[left_idx].right = left_right_idx;
    
    // Add children to right node
    int right_left_idx = shm->node_count++;
    shm->nodes[right_left_idx].value = 12;
    shm->nodes[right_left_idx].processed = false;
    shm->nodes[right_idx].left = right_left_idx;
    
    int right_right_idx = shm->node_count++;
    shm->nodes[right_right_idx].value = 20;
    shm->nodes[right_right_idx].processed = false;
    shm->nodes[right_idx].right = right_right_idx;
    
    // Initialize leaf nodes' children to -1
    shm->nodes[left_left_idx].left = shm->nodes[left_left_idx].right = -1;
    shm->nodes[left_right_idx].left = shm->nodes[left_right_idx].right = -1;
    shm->nodes[right_left_idx].left = shm->nodes[right_left_idx].right = -1;
    shm->nodes[right_right_idx].left = shm->nodes[right_right_idx].right = -1;
    
    return root_idx;
}

// Process a node and its children using separate processes
void processNode(SharedMemory *shm, int node_idx, int semid) {
    if (node_idx == -1) return;
    
    // Mark the node as processed
    semaphore_p(semid);
    shm->nodes[node_idx].processed = true;
    // Process the current node (e.g., square its value)
    shm->nodes[node_idx].result = shm->nodes[node_idx].value * shm->nodes[node_idx].value;
    // Add result to results array
    shm->results[shm->result_count++] = shm->nodes[node_idx].result;
    semaphore_v(semid);
    
    printf("Node %d with value %d processed, result: %d\n", 
           node_idx, shm->nodes[node_idx].value, shm->nodes[node_idx].result);
    
    // Process left child
    if (shm->nodes[node_idx].left != -1) {
        pid_t left_pid = fork();
        
        if (left_pid < 0) {
            perror("fork for left child");
            exit(1);
        } else if (left_pid == 0) {
            // Child process processes the left subtree
            processNode(shm, shm->nodes[node_idx].left, semid);
            exit(0);
        }
    }
    
    // Process right child
    if (shm->nodes[node_idx].right != -1) {
        pid_t right_pid = fork();
        
        if (right_pid < 0) {
            perror("fork for right child");
            exit(1);
        } else if (right_pid == 0) {
            // Child process processes the right subtree
            processNode(shm, shm->nodes[node_idx].right, semid);
            exit(0);
        }
    }
    
    // Wait for child processes to complete
    if (shm->nodes[node_idx].left != -1 || shm->nodes[node_idx].right != -1) {
        while (wait(NULL) > 0);
    }
}

// Semaphore wait (P) operation
void semaphore_p(int semid) {
    struct sembuf sb = {0, -1, 0};
    if (semop(semid, &sb, 1) == -1) {
        perror("semop P");
        exit(1);
    }
}


void semaphore_v(int semid) {
    struct sembuf sb = {0, 1, 0};
    if (semop(semid, &sb, 1) == -1) {
        perror("semop V");
        exit(1);
    }
}
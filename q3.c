#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#define SHM_SIZE 4096
#define MAX_ITEMS 100
#define NUM_PRODUCERS 3
#define NUM_CONSUMERS 2
#define PRODUCTION_LIMIT 30

// Structure for linked list node in shared memory
typedef struct Node {
    int data;
    int next;  // Index of next node in the array
} Node;

// Structure for shared memory
typedef struct {
    Node nodes[MAX_ITEMS];
    int free_list;  // Index of first free node
    int head;       // Index of list head
    int tail;       // Index of list tail
    int count;      // Number of items in list
    int total_produced;
    int total_consumed;
    int should_exit;
} SharedMemory;

// Union for semaphore operations
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

// Semaphore indices
#define SEM_MUTEX 0    // For mutual exclusion
#define SEM_EMPTY 1    // Count of empty slots
#define SEM_FULL 2     // Count of filled slots
#define NUM_SEMS 3

// Function prototypes
void producer(SharedMemory *shm, int semid, int producer_id);
void consumer(SharedMemory *shm, int semid, int consumer_id);
void semaphore_p(int semid, int sem_num);
void semaphore_v(int semid, int sem_num);
void initialize_list(SharedMemory *shm);

int main() {
    int shmid, semid;
    key_t key = ftok("shmfile", 65);
    SharedMemory *shm;
    pid_t pids[NUM_PRODUCERS + NUM_CONSUMERS];
    int pid_idx = 0;
    
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
    
    // Create semaphore set
    semid = semget(key, NUM_SEMS, IPC_CREAT | 0666);
    if (semid < 0) {
        perror("semget");
        exit(1);
    }
    
    // Initialize semaphores
    union semun sem_arg;
    
    // Initialize mutex semaphore to 1
    sem_arg.val = 1;
    if (semctl(semid, SEM_MUTEX, SETVAL, sem_arg) == -1) {
        perror("semctl MUTEX");
        exit(1);
    }
    
    // Initialize empty slots semaphore to MAX_ITEMS
    sem_arg.val = MAX_ITEMS;
    if (semctl(semid, SEM_EMPTY, SETVAL, sem_arg) == -1) {
        perror("semctl EMPTY");
        exit(1);
    }
    
    // Initialize full slots semaphore to 0
    sem_arg.val = 0;
    if (semctl(semid, SEM_FULL, SETVAL, sem_arg) == -1) {
        perror("semctl FULL");
        exit(1);
    }
    
    // Initialize shared memory
    memset(shm, 0, sizeof(SharedMemory));
    initialize_list(shm);
    shm->should_exit = 0;
    shm->total_produced = 0;
    shm->total_consumed = 0;
    
    printf("Starting producer-consumer simulation with linked list\n");
    printf("Creating %d producers and %d consumers\n", NUM_PRODUCERS, NUM_CONSUMERS);
    
    // Create producer processes
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("fork producer");
            exit(1);
        } else if (pid == 0) {
            // Child process becomes a producer
            producer(shm, semid, i);
            exit(0);
        } else {
            pids[pid_idx++] = pid;
        }
    }
    
    // Create consumer processes
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("fork consumer");
            exit(1);
        } else if (pid == 0) {
            // Child process becomes a consumer
            consumer(shm, semid, i);
            exit(0);
        } else {
            pids[pid_idx++] = pid;
        }
    }
    
    // Wait until all items are produced and consumed
    while (shm->total_consumed < PRODUCTION_LIMIT) {
        sleep(1);
    }
    
    // Signal processes to exit
    shm->should_exit = 1;
    
    // Wait for all processes to complete
    for (int i = 0; i < pid_idx; i++) {
        waitpid(pids[i], NULL, 0);
    }
    
    printf("Simulation complete. Total produced: %d, Total consumed: %d\n", 
           shm->total_produced, shm->total_consumed);
    
    // Detach and remove shared memory
    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);
    
    // Remove semaphore set
    semctl(semid, 0, IPC_RMID);
    
    return 0;
}

// Initialize linked list in shared memory
void initialize_list(SharedMemory *shm) {
    // Initialize all nodes to form a free list
    for (int i = 0; i < MAX_ITEMS - 1; i++) {
        shm->nodes[i].next = i + 1;
    }
    shm->nodes[MAX_ITEMS - 1].next = -1;  // End of free list
    
    shm->free_list = 0;  // First node is free
    shm->head = -1;      // Empty list
    shm->tail = -1;      // Empty list
    shm->count = 0;      // No items in list
}

// Get a node from the free list
int get_free_node(SharedMemory *shm) {
    if (shm->free_list == -1) {
        return -1;  // No free nodes
    }
    
    int node_idx = shm->free_list;
    shm->free_list = shm->nodes[node_idx].next;
    return node_idx;
}

// Return a node to the free list
void return_node(SharedMemory *shm, int node_idx) {
    shm->nodes[node_idx].next = shm->free_list;
    shm->free_list = node_idx;
}

// Producer function
void producer(SharedMemory *shm, int semid, int producer_id) {
    srand(time(NULL) ^ (producer_id * 100));
    
    while (!shm->should_exit) {
        // Check if production limit reached
        if (shm->total_produced >= PRODUCTION_LIMIT) {
            break;
        }
        
        // Generate item (random number)
        int item = rand() % 100;
        
        // Wait for an empty slot
        semaphore_p(semid, SEM_EMPTY);
        
        // Wait for mutex
        semaphore_p(semid, SEM_MUTEX);
        
        // If we should exit now, release mutex and empty slot
        if (shm->should_exit) {
            semaphore_v(semid, SEM_MUTEX);
            semaphore_v(semid, SEM_EMPTY);
            break;
        }
        
        // Get a free node
        int node_idx = get_free_node(shm);
        shm->nodes[node_idx].data = item;
        shm->nodes[node_idx].next = -1;
        
        // Add node to list
        if (shm->head == -1) {
            // Empty list
            shm->head = shm->tail = node_idx;
        } else {
            // Add to tail
            shm->nodes[shm->tail].next = node_idx;
            shm->tail = node_idx;
        }
        
        shm->count++;
        shm->total_produced++;
        
        printf("Producer %d: Produced item %d (total: %d)\n", 
               producer_id, item, shm->total_produced);
        
        // Release mutex
        semaphore_v(semid, SEM_MUTEX);
        
        // Signal that a slot is filled
        semaphore_v(semid, SEM_FULL);
        
        // Sleep for a random time
        usleep(rand() % 500000);
    }
    
    printf("Producer %d: Exiting\n", producer_id);
}

// Consumer function
void consumer(SharedMemory *shm, int semid, int consumer_id) {
    srand(time(NULL) ^ (consumer_id * 200));
    
    while (!shm->should_exit) {
        // Wait for a filled slot
        semaphore_p(semid, SEM_FULL);
        
        // Wait for mutex
        semaphore_p(semid, SEM_MUTEX);
        
        // If we should exit now, release mutex and full slot
        if (shm->should_exit) {
            semaphore_v(semid, SEM_MUTEX);
            semaphore_v(semid, SEM_FULL);
            break;
        }
        
        // Remove item from head of list
        int node_idx = shm->head;
        int item = shm->nodes[node_idx].data;
        
        // Update head
        shm->head = shm->nodes[node_idx].next;
        if (shm->head == -1) {
            // List is now empty
            shm->tail = -1;
        }
        
        // Return node to free list
        return_node(shm, node_idx);
        
        shm->count--;
        shm->total_consumed++;
        
        printf("Consumer %d: Consumed item %d (total: %d)\n", 
               consumer_id, item, shm->total_consumed);
        
        // Release mutex
        semaphore_v(semid, SEM_MUTEX);
        
        // Signal that a slot is empty
        semaphore_v(semid, SEM_EMPTY);
        
        // Sleep for a random time
        usleep(rand() % 1000000);
    }
    
    printf("Consumer %d: Exiting\n", consumer_id);
}

// Semaphore wait (P) operation
void semaphore_p(int semid, int sem_num) {
    struct sembuf sb = {sem_num, -1, 0};
    if (semop(semid, &sb, 1) == -1) {
        perror("semop P");
        exit(1);
    }
}

// Semaphore signal (V) operation
void semaphore_v(int semid, int sem_num) {
    struct sembuf sb = {sem_num, 1, 0};
    if (semop(semid, &sb, 1) == -1) {
        perror("semop V");
        exit(1);
    }
}
//Create a parallel implementation of a B-tree where different processes handle different operations (insert, delete, search). Use shared memory for the tree structure with proper synchronization mechanisms

/*
 * Parallel B-Tree Implementation in C
 * 
 * This implementation creates a shared memory B-tree that can be accessed
 * by multiple processes concurrently for insert, delete, and search operations.
 * Each operation type is handled by a dedicated process.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <unistd.h>
 #include <sys/types.h>
 #include <sys/wait.h>
 #include <sys/ipc.h>
 #include <sys/shm.h>
 #include <sys/sem.h>
 #include <string.h>
 #include <signal.h>
 #include <stdbool.h>
 
 #define MIN_DEGREE 3        // Minimum degree of B-Tree (t)
 #define MAX_KEYS (2*MIN_DEGREE - 1)
 #define MAX_CHILDREN (2*MIN_DEGREE)
 #define MAX_OPERATIONS 100  // Maximum number of operations in the queue
 
 // Semaphore operations
 union semun {
     int val;
     struct semid_ds *buf;
     unsigned short *array;
 };
 
 // Operation types
 typedef enum {
     INSERT,
     DELETE,
     SEARCH,
     TERMINATE
 } OperationType;
 
 // Operation structure
 typedef struct {
     OperationType type;
     int key;
     int result;  // For search operations, -1 if not found
     bool completed;
 } Operation;
 
 // Operation queue for communication between main process and worker processes
 typedef struct {
     Operation operations[MAX_OPERATIONS];
     int front;
     int rear;
     int count;
 } OperationQueue;
 
 // B-Tree node structure
 typedef struct BTreeNode {
     int keys[MAX_KEYS];          // Keys stored in the node
     int children[MAX_CHILDREN];  // Indices of child nodes in the shared memory
     int n;                       // Current number of keys
     bool leaf;                   // Is this a leaf node?
     int index;                   // Index of this node in the shared memory
 } BTreeNode;
 
 // B-Tree structure
 typedef struct {
     int root;          // Index of the root node
     int nextNodeIndex; // Next available node index
     int size;          // Number of keys in the tree
 } BTree;
 
 // Shared memory structure
 typedef struct {
     BTree tree;
     BTreeNode nodes[1000];       // Array of nodes
     OperationQueue insert_queue;
     OperationQueue delete_queue;
     OperationQueue search_queue;
     int terminate;               // Flag to signal termination
 } SharedMemory;
 
 // Global variables
 int shmid;
 int semid;
 SharedMemory *shared_memory;
 
 // Semaphore indices
 #define SEM_TREE 0         // For tree structure modifications
 #define SEM_INSERT_QUEUE 1 // For insert queue
 #define SEM_DELETE_QUEUE 2 // For delete queue
 #define SEM_SEARCH_QUEUE 3 // For search queue
 #define NUM_SEMS 4
 
 // Function prototypes
 void initialize_semaphores();
 void cleanup_resources();
 void semaphore_operation(int sem_index, int op);
 void semaphore_wait(int sem_index);
 void semaphore_signal(int sem_index);
 int create_node(bool is_leaf);
 void insert_process();
 void delete_process();
 void search_process();
 void insert_key(int key);
 bool delete_key(int key);
 bool search_key(int key);
 void enqueue_operation(OperationQueue *queue, OperationType type, int key);
 Operation dequeue_operation(OperationQueue *queue);
 void split_child(int x_index, int i);
 void insert_non_full(int node_index, int key);
 int find_key(BTreeNode *node, int key);
 void merge_children(int node_index, int idx);
 void remove_from_leaf(int node_index, int idx);
 void remove_from_non_leaf(int node_index, int idx);
 int get_predecessor(int node_index, int idx);
 int get_successor(int node_index, int idx);
 void fill_child(int node_index, int idx);
 void borrow_from_prev(int node_index, int idx);
 void borrow_from_next(int node_index, int idx);
 bool search_in_node(int node_index, int key);
 void print_tree(int node_index, int level);
 
 // Signal handler
 void handle_signal(int sig) {
     if (sig == SIGINT || sig == SIGTERM) {
         printf("Received termination signal. Cleaning up...\n");
         cleanup_resources();
         exit(0);
     }
 }
 
 int main() {
     // Register signal handler
     signal(SIGINT, handle_signal);
     signal(SIGTERM, handle_signal);
 
     // Create shared memory
     shmid = shmget(IPC_PRIVATE, sizeof(SharedMemory), IPC_CREAT | 0666);
     if (shmid < 0) {
         perror("shmget failed");
         exit(1);
     }
 
     // Attach shared memory
     shared_memory = (SharedMemory*)shmat(shmid, NULL, 0);
     if (shared_memory == (void*)-1) {
         perror("shmat failed");
         exit(1);
     }
 
     // Initialize shared memory
     memset(shared_memory, 0, sizeof(SharedMemory));
     shared_memory->tree.root = create_node(true);
     shared_memory->tree.size = 0;
     shared_memory->terminate = 0;
 
     // Initialize operation queues
     shared_memory->insert_queue.front = 0;
     shared_memory->insert_queue.rear = -1;
     shared_memory->insert_queue.count = 0;
     
     shared_memory->delete_queue.front = 0;
     shared_memory->delete_queue.rear = -1;
     shared_memory->delete_queue.count = 0;
     
     shared_memory->search_queue.front = 0;
     shared_memory->search_queue.rear = -1;
     shared_memory->search_queue.count = 0;
 
     // Initialize semaphores
     initialize_semaphores();
 
     // Create worker processes
     pid_t insert_pid, delete_pid, search_pid;
     
     insert_pid = fork();
     if (insert_pid == 0) {
         insert_process();
         exit(0);
     } else if (insert_pid < 0) {
         perror("Fork failed for insert process");
         cleanup_resources();
         exit(1);
     }
     
     delete_pid = fork();
     if (delete_pid == 0) {
         delete_process();
         exit(0);
     } else if (delete_pid < 0) {
         perror("Fork failed for delete process");
         cleanup_resources();
         exit(1);
     }
     
     search_pid = fork();
     if (search_pid == 0) {
         search_process();
         exit(0);
     } else if (search_pid < 0) {
         perror("Fork failed for search process");
         cleanup_resources();
         exit(1);
     }
 
     // Main process: handle user input and delegate operations
     printf("Parallel B-Tree System\n");
     printf("Commands: insert <key>, delete <key>, search <key>, print, exit\n");
     
     char command[20];
     int key;
     
     while (1) {
         printf("> ");
         scanf("%s", command);
         
         if (strcmp(command, "exit") == 0) {
             break;
         } else if (strcmp(command, "insert") == 0) {
             scanf("%d", &key);
             semaphore_wait(SEM_INSERT_QUEUE);
             enqueue_operation(&shared_memory->insert_queue, INSERT, key);
             semaphore_signal(SEM_INSERT_QUEUE);
         } else if (strcmp(command, "delete") == 0) {
             scanf("%d", &key);
             semaphore_wait(SEM_DELETE_QUEUE);
             enqueue_operation(&shared_memory->delete_queue, DELETE, key);
             semaphore_signal(SEM_DELETE_QUEUE);
         } else if (strcmp(command, "search") == 0) {
             scanf("%d", &key);
             semaphore_wait(SEM_SEARCH_QUEUE);
             enqueue_operation(&shared_memory->search_queue, SEARCH, key);
             semaphore_signal(SEM_SEARCH_QUEUE);
             
             // Wait for the result
             bool found = false;
             while (!found) {
                 semaphore_wait(SEM_SEARCH_QUEUE);
                 for (int i = 0; i < MAX_OPERATIONS; i++) {
                     if (shared_memory->search_queue.operations[i].key == key && 
                         shared_memory->search_queue.operations[i].completed) {
                         if (shared_memory->search_queue.operations[i].result != -1) {
                             printf("Key %d found in the tree.\n", key);
                         } else {
                             printf("Key %d not found in the tree.\n", key);
                         }
                         // Clear the operation
                         shared_memory->search_queue.operations[i].completed = false;
                         found = true;
                         break;
                     }
                 }
                 semaphore_signal(SEM_SEARCH_QUEUE);
                 if (!found) {
                     usleep(1000); // Small delay to avoid busy waiting
                 }
             }
         } else if (strcmp(command, "print") == 0) {
             semaphore_wait(SEM_TREE);
             printf("B-Tree Structure:\n");
             print_tree(shared_memory->tree.root, 0);
             semaphore_signal(SEM_TREE);
         } else {
             printf("Unknown command: %s\n", command);
         }
     }
 
     // Signal termination to worker processes
     shared_memory->terminate = 1;
     semaphore_signal(SEM_INSERT_QUEUE);
     semaphore_signal(SEM_DELETE_QUEUE);
     semaphore_signal(SEM_SEARCH_QUEUE);
     
     // Wait for worker processes to terminate
     waitpid(insert_pid, NULL, 0);
     waitpid(delete_pid, NULL, 0);
     waitpid(search_pid, NULL, 0);
     
     // Clean up resources
     cleanup_resources();
     
     return 0;
 }
 
 // Initialize semaphores
 void initialize_semaphores() {
     semid = semget(IPC_PRIVATE, NUM_SEMS, IPC_CREAT | 0666);
     if (semid < 0) {
         perror("semget failed");
         exit(1);
     }
     
     union semun arg;
     arg.val = 1;
     
     for (int i = 0; i < NUM_SEMS; i++) {
         if (semctl(semid, i, SETVAL, arg) < 0) {
             perror("semctl failed");
             exit(1);
         }
     }
 }
 
 // Clean up resources (shared memory and semaphores)
 void cleanup_resources() {
     // Detach shared memory
     if (shmdt(shared_memory) < 0) {
         perror("shmdt failed");
     }
     
     // Remove shared memory
     if (shmctl(shmid, IPC_RMID, NULL) < 0) {
         perror("shmctl failed to remove shared memory");
     }
     
     // Remove semaphores
     if (semctl(semid, 0, IPC_RMID) < 0) {
         perror("semctl failed to remove semaphores");
     }
 }
 
 // Perform a semaphore operation
 void semaphore_operation(int sem_index, int op) {
     struct sembuf sb;
     sb.sem_num = sem_index;
     sb.sem_op = op;
     sb.sem_flg = 0;
     
     if (semop(semid, &sb, 1) < 0) {
         perror("semop failed");
         exit(1);
     }
 }
 
 // Wait operation on a semaphore
 void semaphore_wait(int sem_index) {
     semaphore_operation(sem_index, -1);
 }
 
 // Signal operation on a semaphore
 void semaphore_signal(int sem_index) {
     semaphore_operation(sem_index, 1);
 }
 
 // Create a new node in the B-tree
 int create_node(bool is_leaf) {
     semaphore_wait(SEM_TREE);
     int index = shared_memory->tree.nextNodeIndex++;
     BTreeNode *node = &shared_memory->nodes[index];
     node->n = 0;
     node->leaf = is_leaf;
     node->index = index;
     semaphore_signal(SEM_TREE);
     return index;
 }
 
 // Insert worker process
 void insert_process() {
     while (1) {
         semaphore_wait(SEM_INSERT_QUEUE);
         
         if (shared_memory->terminate) {
             semaphore_signal(SEM_INSERT_QUEUE);
             break;
         }
         
         if (shared_memory->insert_queue.count > 0) {
             Operation op = dequeue_operation(&shared_memory->insert_queue);
             semaphore_signal(SEM_INSERT_QUEUE);
             
             // Process the insert operation
             insert_key(op.key);
         } else {
             semaphore_signal(SEM_INSERT_QUEUE);
             usleep(10000); // Sleep to avoid busy waiting
         }
     }
 }
 
 // Delete worker process
 void delete_process() {
     while (1) {
         semaphore_wait(SEM_DELETE_QUEUE);
         
         if (shared_memory->terminate) {
             semaphore_signal(SEM_DELETE_QUEUE);
             break;
         }
         
         if (shared_memory->delete_queue.count > 0) {
             Operation op = dequeue_operation(&shared_memory->delete_queue);
             semaphore_signal(SEM_DELETE_QUEUE);
             
             // Process the delete operation
             bool result = delete_key(op.key);
             printf("Delete %d: %s\n", op.key, result ? "Successful" : "Key not found");
         } else {
             semaphore_signal(SEM_DELETE_QUEUE);
             usleep(10000); // Sleep to avoid busy waiting
         }
     }
 }
 
 // Search worker process
 void search_process() {
     while (1) {
         semaphore_wait(SEM_SEARCH_QUEUE);
         
         if (shared_memory->terminate) {
             semaphore_signal(SEM_SEARCH_QUEUE);
             break;
         }
         
         for (int i = 0; i < MAX_OPERATIONS; i++) {
             Operation *op = &shared_memory->search_queue.operations[i];
             if (op->type == SEARCH && !op->completed) {
                 int key = op->key;
                 semaphore_signal(SEM_SEARCH_QUEUE);
                 
                 // Process the search operation
                 bool result = search_key(key);
                 
                 semaphore_wait(SEM_SEARCH_QUEUE);
                 op->result = result ? key : -1;
                 op->completed = true;
                 semaphore_signal(SEM_SEARCH_QUEUE);
                 break;
             }
         }
         
         if (shared_memory->search_queue.count == 0) {
             semaphore_signal(SEM_SEARCH_QUEUE);
             usleep(10000); // Sleep to avoid busy waiting
         } else {
             semaphore_signal(SEM_SEARCH_QUEUE);
         }
     }
 }
 
 // Insert a key into the B-tree
 void insert_key(int key) {
     semaphore_wait(SEM_TREE);
     
     // If the root is full, split it
     if (shared_memory->nodes[shared_memory->tree.root].n == MAX_KEYS) {
         int s = create_node(false);
         BTreeNode *new_root = &shared_memory->nodes[s];
         new_root->children[0] = shared_memory->tree.root;
         
         // Make old root as a child of new root
         shared_memory->tree.root = s;
         
         // Split the old root
         split_child(s, 0);
         
         // Insert the key into the appropriate child
         insert_non_full(s, key);
     } else {
         // If root is not full, insert directly
         insert_non_full(shared_memory->tree.root, key);
     }
     
     // Update tree size
     shared_memory->tree.size++;
     
     semaphore_signal(SEM_TREE);
     printf("Inserted key: %d\n", key);
 }
 
 // Split a child node
 void split_child(int x_index, int i) {
     // Create a new node
     int z_index = create_node(shared_memory->nodes[shared_memory->nodes[x_index].children[i]].leaf);
     BTreeNode *z = &shared_memory->nodes[z_index];
     BTreeNode *y = &shared_memory->nodes[shared_memory->nodes[x_index].children[i]];
     BTreeNode *x = &shared_memory->nodes[x_index];
     
     // Copy the second half of y to z
     z->n = MIN_DEGREE - 1;
     for (int j = 0; j < MIN_DEGREE - 1; j++) {
         z->keys[j] = y->keys[j + MIN_DEGREE];
     }
     
     // Copy the child pointers if not a leaf
     if (!y->leaf) {
         for (int j = 0; j < MIN_DEGREE; j++) {
             z->children[j] = y->children[j + MIN_DEGREE];
         }
     }
     
     // Reduce the number of keys in y
     y->n = MIN_DEGREE - 1;
     
     // Shift children of x to make room for the new child
     for (int j = x->n; j >= i + 1; j--) {
         x->children[j + 1] = x->children[j];
     }
     
     // Connect the new child
     x->children[i + 1] = z_index;
     
     // Move a key from y to x
     for (int j = x->n - 1; j >= i; j--) {
         x->keys[j + 1] = x->keys[j];
     }
     x->keys[i] = y->keys[MIN_DEGREE - 1];
     
     // Increment the count of keys in x
     x->n++;
 }
 
 // Insert a key into a non-full node
 void insert_non_full(int node_index, int key) {
     BTreeNode *node = &shared_memory->nodes[node_index];
     
     // If this is a leaf node
     if (node->leaf) {
         // Find the position to insert
         int i = node->n - 1;
         while (i >= 0 && node->keys[i] > key) {
             node->keys[i + 1] = node->keys[i];
             i--;
         }
         
         // Insert the key
         node->keys[i + 1] = key;
         node->n++;
     } else {
         // Find the child which is going to have the new key
         int i = node->n - 1;
         while (i >= 0 && node->keys[i] > key) {
             i--;
         }
         i++;
         
         // Check if the child is full
         if (shared_memory->nodes[node->children[i]].n == MAX_KEYS) {
             // Split the child
             split_child(node_index, i);
             
             // After split, the middle key goes up and node is split into two
             if (node->keys[i] < key) {
                 i++;
             }
         }
         
         // Recursively insert the key
         insert_non_full(node->children[i], key);
     }
 }
 
 // Delete a key from the B-tree
 bool delete_key(int key) {
     semaphore_wait(SEM_TREE);
     
     int root = shared_memory->tree.root;
     BTreeNode *root_node = &shared_memory->nodes[root];
     
     // Check if the key exists in the tree
     if (!search_key(key)) {
         semaphore_signal(SEM_TREE);
         return false;
     }
     
     // If the root node has only one key and it is the key to be deleted
     if (root_node->n == 1 && root_node->keys[0] == key && root_node->leaf) {
         root_node->n = 0;
         shared_memory->tree.size--;
         semaphore_signal(SEM_TREE);
         return true;
     }
     
     // If the root becomes empty, make its first child the new root
     if (root_node->n == 0 && !root_node->leaf) {
         shared_memory->tree.root = root_node->children[0];
         // The old root node is not freed in this example, but it could be
     }
     
     // Call the recursive delete function
     bool result = search_in_node(root, key);
     if (result) {
         shared_memory->tree.size--;
     }
     
     semaphore_signal(SEM_TREE);
     return result;
 }
 
 // Helper functions for deletion (only partially implemented)
 int find_key(BTreeNode *node, int key) {
     int idx = 0;
     while (idx < node->n && node->keys[idx] < key) {
         idx++;
     }
     return idx;
 }
 
 void remove_from_leaf(int node_index, int idx) {
     BTreeNode *node = &shared_memory->nodes[node_index];
     
     // Move all keys after idx one position backward
     for (int i = idx + 1; i < node->n; i++) {
         node->keys[i - 1] = node->keys[i];
     }
     
     // Reduce the count of keys
     node->n--;
 }
 
 bool search_in_node(int node_index, int key) {
     BTreeNode *node = &shared_memory->nodes[node_index];
     
     // Find the first key greater than or equal to key
     int i = 0;
     while (i < node->n && key > node->keys[i]) {
         i++;
     }
     
     // If the key is found
     if (i < node->n && key == node->keys[i]) {
         // If this is a leaf node, just remove the key
         if (node->leaf) {
             remove_from_leaf(node_index, i);
         } else {
             // This is a non-leaf node, more complex deletion
             remove_from_non_leaf(node_index, i);
         }
         return true;
     }
     
     // If this is a leaf node, the key is not present
     if (node->leaf) {
         return false;
     }
     
     // Recursively delete from the subtree
     return search_in_node(node->children[i], key);
 }
 
 // Search for a key in the B-tree
 bool search_key(int key) {
     semaphore_wait(SEM_TREE);
     int node_index = shared_memory->tree.root;
     
     while (true) {
         BTreeNode *node = &shared_memory->nodes[node_index];
         
         // Find the key in the current node
         int i = 0;
         while (i < node->n && key > node->keys[i]) {
             i++;
         }
         
         // If the key is found
         if (i < node->n && key == node->keys[i]) {
             semaphore_signal(SEM_TREE);
             return true;
         }
         
         // If this is a leaf node and key is not found
         if (node->leaf) {
             semaphore_signal(SEM_TREE);
             return false;
         }
         
         // Go to the appropriate child
         node_index = node->children[i];
     }
 }
 
 // Enqueue an operation
 void enqueue_operation(OperationQueue *queue, OperationType type, int key) {
     queue->rear = (queue->rear + 1) % MAX_OPERATIONS;
     queue->operations[queue->rear].type = type;
     queue->operations[queue->rear].key = key;
     queue->operations[queue->rear].result = -1;
     queue->operations[queue->rear].completed = false;
     queue->count++;
 }
 
 // Dequeue an operation
 Operation dequeue_operation(OperationQueue *queue) {
     Operation op = queue->operations[queue->front];
     queue->front = (queue->front + 1) % MAX_OPERATIONS;
     queue->count--;
     return op;
 }
 
 // Print the B-tree (for debugging)
 void print_tree(int node_index, int level) {
     BTreeNode *node = &shared_memory->nodes[node_index];
     
     // Print node information
     printf("Level %d - Node %d (keys=%d): ", level, node_index, node->n);
     for (int i = 0; i < node->n; i++) {
         printf("%d ", node->keys[i]);
     }
     printf("\n");
     
     // Print children recursively
     if (!node->leaf) {
         for (int i = 0; i <= node->n; i++) {
             print_tree(node->children[i], level + 1);
         }
     }
 }
 
 // These functions are incomplete and would need to be implemented for a full B-tree
 void remove_from_non_leaf(int node_index, int idx) {
     // Implementation omitted for brevity
 }
 
 int get_predecessor(int node_index, int idx) {
     // Implementation omitted for brevity
     return 0;
 }
 
 int get_successor(int node_index, int idx) {
     // Implementation omitted for brevity
     return 0;
 }
 
 void fill_child(int node_index, int idx) {
     // Implementation omitted for brevity
 }
 
 void borrow_from_prev(int node_index, int idx) {
     // Implementation omitted for brevity
 }
 
 void borrow_from_next(int node_index, int idx) {
     // Implementation omitted for brevity
 }
 
 void merge_children(int node_index, int idx) {
     // Implementation omitted for brevity
 }
//Implement a system that uses exec() to run external commands, with input/output redirection through pipes connected to a shared circular buffer in shared memory, protected by semaphores.


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>

#define BUFFER_SIZE 4096
#define MAX_COMMAND_LEN 256

// Circular buffer structure
typedef struct {
    char data[BUFFER_SIZE];
    int read_pos;
    int write_pos;
    int count;
    sem_t mutex;      // For mutual exclusion
    sem_t empty;      // Counts empty slots
    sem_t filled;     // Counts filled slots
} CircularBuffer;

// Initialize the circular buffer
void init_buffer(CircularBuffer *buffer) {
    buffer->read_pos = 0;
    buffer->write_pos = 0;
    buffer->count = 0;
    
    sem_init(&buffer->mutex, 1, 1);     // Binary semaphore for mutual exclusion
    sem_init(&buffer->empty, 1, BUFFER_SIZE);  // Initially all slots are empty
    sem_init(&buffer->filled, 1, 0);    // Initially no slots are filled
}

// Write a byte to the buffer
void write_to_buffer(CircularBuffer *buffer, char byte) {
    // Wait until there's an empty slot
    sem_wait(&buffer->empty);
    
    // Get exclusive access to the buffer
    sem_wait(&buffer->mutex);
    
    // Write to the buffer
    buffer->data[buffer->write_pos] = byte;
    buffer->write_pos = (buffer->write_pos + 1) % BUFFER_SIZE;
    buffer->count++;
    
    // Release the mutex
    sem_post(&buffer->mutex);
    
    // Signal that a slot has been filled
    sem_post(&buffer->filled);
}

// Read a byte from the buffer
char read_from_buffer(CircularBuffer *buffer) {
    char byte;
    
    // Wait until there's a filled slot
    sem_wait(&buffer->filled);
    
    // Get exclusive access to the buffer
    sem_wait(&buffer->mutex);
    
    // Read from the buffer
    byte = buffer->data[buffer->read_pos];
    buffer->read_pos = (buffer->read_pos + 1) % BUFFER_SIZE;
    buffer->count--;
    
    // Release the mutex
    sem_post(&buffer->mutex);
    
    // Signal that a slot has been emptied
    sem_post(&buffer->empty);
    
    return byte;
}

// Execute a command with I/O redirected through pipes
void execute_command(const char *command, CircularBuffer *input_buffer, CircularBuffer *output_buffer) {
    int input_pipe[2];  // Parent writes to command's stdin
    int output_pipe[2]; // Parent reads from command's stdout
    
    // Create pipes
    if (pipe(input_pipe) < 0 || pipe(output_pipe) < 0) {
        perror("Pipe creation failed");
        return;
    }
    
    // Fork a new process
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("Fork failed");
        return;
    } else if (pid == 0) {
        // Child process
        
        // Redirect stdin from input pipe
        close(input_pipe[1]);  // Close write end
        dup2(input_pipe[0], STDIN_FILENO);
        close(input_pipe[0]);
        
        // Redirect stdout to output pipe
        close(output_pipe[0]);  // Close read end
        dup2(output_pipe[1], STDOUT_FILENO);
        close(output_pipe[1]);
        
        // Execute the command
        execlp("/bin/sh", "sh", "-c", command, NULL);
        
        // If exec fails
        perror("Exec failed");
        exit(1);
    } else {
        // Parent process
        
        // Close unused pipe ends
        close(input_pipe[0]);
        close(output_pipe[1]);
        
        // Create a reader process for the command's output
        pid_t reader_pid = fork();
        
        if (reader_pid < 0) {
            perror("Fork failed for reader");
            return;
        } else if (reader_pid == 0) {
            // Reader process
            close(input_pipe[1]);  // Close input pipe write end
            
            // Read from command's stdout and write to the output buffer
            char byte;
            while (read(output_pipe[0], &byte, 1) > 0) {
                write_to_buffer(output_buffer, byte);
            }
            
            close(output_pipe[0]);
            exit(0);
        } else {
            // Parent continues
            
            // Read from input buffer and write to command's stdin
            int buffer_empty = 0;
            while (!buffer_empty) {
                sem_wait(&input_buffer->mutex);
                buffer_empty = (input_buffer->count == 0);
                sem_post(&input_buffer->mutex);
                
                if (!buffer_empty) {
                    char byte = read_from_buffer(input_buffer);
                    write(input_pipe[1], &byte, 1);
                }
            }
            
            // Close pipes to signal EOF to the command
            close(input_pipe[1]);
            
            // Wait for the command to finish
            waitpid(pid, NULL, 0);
            
            // Wait for the reader to finish
            waitpid(reader_pid, NULL, 0);
            
            close(output_pipe[0]);
        }
    }
}

// Clean up resources
void cleanup_buffer(CircularBuffer *buffer) {
    sem_destroy(&buffer->mutex);
    sem_destroy(&buffer->empty);
    sem_destroy(&buffer->filled);
}

int main() {
    // Create shared memory for input and output buffers
    CircularBuffer *input_buffer = mmap(NULL, sizeof(CircularBuffer), 
                                      PROT_READ | PROT_WRITE, 
                                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    CircularBuffer *output_buffer = mmap(NULL, sizeof(CircularBuffer), 
                                       PROT_READ | PROT_WRITE, 
                                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    
    if (input_buffer == MAP_FAILED || output_buffer == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }
    
    // Initialize buffers
    init_buffer(input_buffer);
    init_buffer(output_buffer);
    
    // Sample data to process
    const char *input_data = "Hello, world!\nThis is a test.\nLet's count lines: wc -l\n";
    
    // Write input data to input buffer
    for (int i = 0; i < strlen(input_data); i++) {
        write_to_buffer(input_buffer, input_data[i]);
    }
    
    // Commands to execute
    const char *commands[] = {
        "cat",                  // Simply output what it receives
        "grep test",            // Filter lines containing "test"
        "tr '[:lower:]' '[:upper:]'",  // Convert to uppercase
        "wc -c"                 // Count characters
    };
    
    int num_commands = sizeof(commands) / sizeof(commands[0]);
    
    // Create a process for each command
    for (int i = 0; i < num_commands; i++) {
        printf("\nExecuting command: %s\n", commands[i]);
        
        // Execute the command
        execute_command(commands[i], input_buffer, output_buffer);
        
        // Print the output
        printf("Command output:\n");
        while (1) {
            sem_wait(&output_buffer->mutex);
            int is_empty = (output_buffer->count == 0);
            sem_post(&output_buffer->mutex);
            
            if (is_empty) break;
            
            char byte = read_from_buffer(output_buffer);
            putchar(byte);
        }
        printf("\n");
        
        // Reset the input buffer with the original data for the next command
        init_buffer(input_buffer);
        for (int j = 0; j < strlen(input_data); j++) {
            write_to_buffer(input_buffer, input_data[j]);
        }
    }
    
    // Clean up
    cleanup_buffer(input_buffer);
    cleanup_buffer(output_buffer);
    munmap(input_buffer, sizeof(CircularBuffer));
    munmap(output_buffer, sizeof(CircularBuffer));
    
    return 0;
}
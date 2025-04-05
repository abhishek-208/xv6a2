#include "types.h"
#include "user.h"
#include "stat.h"

#define CPU_ITERATIONS 100000000
#define IO_ITERATIONS 5

// CPU-bound process
void cpu_work() {
    printf(1, "CPU-bound process (PID %d) started\n\n", getpid());
    for(volatile int i = 0; i < CPU_ITERATIONS; i++);
    printf(1, "CPU-bound process (PID %d) finished\n\n", getpid());
}

// I/O-bound process
void io_work() {
    printf(1, "I/O-bound process (PID %d) started\n\n", getpid());
    for(int i = 0; i < IO_ITERATIONS; i++) {
        sleep(10);  // Simulate I/O wait
        printf(1, "I/O-bound process (PID %d) completed I/O cycle %d\n\n", getpid(), i+1);
    }
    printf(1, "I/O-bound process (PID %d) finished\n\n", getpid());
}

// Mixed workload process
void mixed_work() {
    printf(1, "Mixed process (PID %d) started\n\n", getpid());
    for(int i = 0; i < IO_ITERATIONS; i++) {
        // Do some CPU work
        for(volatile int j = 0; j < CPU_ITERATIONS/10; j++);
        // Then I/O
        sleep(5);
        printf(1, "Mixed process (PID %d) completed cycle %d\n\n", getpid(), i+1);
    }
    printf(1, "Mixed process (PID %d) finished\n\n", getpid());
}

int main() {
    int pid;
    
    printf(1, "\nStarting priority scheduler test...\n\n");

    // Create 3 child processes with different workloads
    for(int i = 0; i < 3; i++) {
        pid = fork();
        if(pid < 0) {
            printf(1, "Fork failed!\n");
            exit();
        }
        
        if(pid == 0) { // Child
            switch(i) {
                case 0: cpu_work(); break;
                case 1: io_work(); break;
                case 2: mixed_work(); break;
            }
            exit();
        }
    }

    // Parent waits for all children
    for(int i = 0; i < 3; i++) {
        wait();
    }

    printf(1, "\nPriority test completed!\n\n");
    exit();
}
#include "types.h"
#include "user.h"
#include "stat.h"

#define CPU_ITERATIONS 100000000
#define IO_ITERATIONS 5

// CPU-bound process
void cpu_work() {
    printf(1, "CPU-bound process (PID %d) started\n", getpid());
    for (volatile int i = 0; i < CPU_ITERATIONS; i++);
    printf(1, "CPU-bound process (PID %d) finished\n", getpid());
}

// I/O-bound process
void io_work() {
    printf(1, "I/O-bound process (PID %d) started\n", getpid());
    for (int i = 0; i < IO_ITERATIONS; i++) {
        sleep(10);  // Simulate I/O wait
        printf(1, "I/O-bound process (PID %d) completed I/O cycle %d\n", getpid(), i+1);
    }
    printf(1, "I/O-bound process (PID %d) finished\n", getpid());
}

// Mixed workload process
void mixed_work() {
    printf(1, "Mixed process (PID %d) started\n", getpid());
    for (int i = 0; i < IO_ITERATIONS; i++) {
        for (volatile int j = 0; j < CPU_ITERATIONS / 10; j++); // CPU work
        sleep(5); // Simulate I/O wait
        printf(1, "Mixed process (PID %d) completed cycle %d\n", getpid(), i+1);
    }
    printf(1, "Mixed process (PID %d) finished\n", getpid());
}

int main() {
    int pid[3];

    printf(1, "\nStarting priority scheduler test with custom fork...\n");

    for (int i = 0; i < 3; i++) {
        pid[i] = custom_fork(1, -1); // Start later
        if (pid[i] < 0) {
            printf(1, "Custom fork failed for process %d!\n", i);
            exit();
        }

        if (pid[i] == 0) { // Child process
            switch (i) {
                case 0: cpu_work(); break;
                case 1: io_work(); break;
                case 2: mixed_work(); break;
            }
            exit();
        }
    }

    // Start all delayed processes
    scheduler_start();

    // Parent waits for all children
    for (int i = 0; i < 3; i++) {
        wait();
    }

    printf(1, "\nPriority test completed!\n");
    exit();
}

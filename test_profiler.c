#include "types.h"
#include "user.h"

int main() {
    printf(1, "Testing scheduler profiler with custom fork...\n");

    int pid = custom_fork(1, 10); // Start later, run indefinitely
    if (pid < 0) {
        printf(1, "Custom fork failed!\n");
        exit();
    }

    if (pid == 0) {
        profile_test(); // Function to profile execution
        exit();
    }

    // Start all delayed processes
    scheduler_start();

    // Parent waits for the child process
    wait();

    printf(1, "Profiler test completed.\n");
    exit();
}

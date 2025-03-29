#include "types.h"
#include "stat.h"
#include "user.h"

int main() {
    if (fork() == 0) {
        printf(1, "Child process running...\n");
        sleep(10); // Simulate work
        printf(1, "Child process exiting...\n");
        exit();
    }

    wait(); // Parent should clean up the child
    printf(1, "Parent process exiting...\n");
    exit();
}

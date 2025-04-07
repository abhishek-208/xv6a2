#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_PROCS 3

void itoa(int n, char* s) {
    int i = 0, sign = n;
    if (n == 0) {
        s[i++] = '0';
        s[i] = '\0';
        return;
    }
    if (n < 0) n = -n;
    while (n > 0) {
        s[i++] = n % 10 + '0';
        n /= 10;
    }
    if (sign < 0) s[i++] = '-';
    s[i] = '\0';

    // Reverse the string
    for (int j = 0, k = i - 1; j < k; j++, k--) {
        char t = s[j];
        s[j] = s[k];
        s[k] = t;
    }
}

void write_msg(const char* label, int i, int pid) {
    char buf[128];
    char idx_buf[10], pid_buf[10];
    int p = 0;

    itoa(i, idx_buf);
    itoa(pid, pid_buf);

    const char* prefix = "Child ";
    for (int j = 0; prefix[j]; j++) buf[p++] = prefix[j];
    for (int j = 0; idx_buf[j]; j++) buf[p++] = idx_buf[j];
    const char* mid = " (PID: ";
    for (int j = 0; mid[j]; j++) buf[p++] = mid[j];
    for (int j = 0; pid_buf[j]; j++) buf[p++] = pid_buf[j];
    const char* suffix = ") ";
    for (int j = 0; suffix[j]; j++) buf[p++] = suffix[j];
    for (int j = 0; label[j]; j++) buf[p++] = label[j];
    buf[p++] = '\n';

    write(1, buf, p);
}

int main() {
    int pids[NUM_PROCS];

    printf(1, "All child processes created with start_later flag set.\n");

    for (int i = 0; i < NUM_PROCS; i++) {
        int pid = custom_fork(1, -1);  

        if (pid < 0) {
            printf(1, "Failed to fork process %d\n", i);
            exit();
        } else if (pid == 0) {
            // Child process
            sleep(2);  // Let the message appear after scheduler_start()
            write_msg("started but should not run yet.", i, getpid());
            sleep(5);  // Simulate some work
            write_msg("exiting.", i, getpid());
            exit();
        }

        // Parent process
        pids[i] = pid;
    }

    sleep(10);  // Give time to ensure all children are blocked

    printf(1, "Calling sys_scheduler_start() to allow execution.\n");
    scheduler_start();

    for (int i = 0; i < NUM_PROCS; i++) {
        int waited_pid = wait();
        if (waited_pid < 0) {
            printf(1, "Child %d (PID: %d) failed during wait()\n", i, pids[i]);
        }
    }

    printf(1, "All child processes completed.\n\n");
    sleep(50);  // Let final stats print out
    exit();
}

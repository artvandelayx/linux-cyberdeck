#include "linux_supervisor.h"
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <android/log.h>

#define LOG_TAG "LinuxCyberdeck-Monitor"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static struct {
    pid_t* pids;
    int count;
    int capacity;
    pthread_mutex_t mutex;
} g_process_registry = {0};

void process_monitor_register(pid_t pid) {
    pthread_mutex_lock(&g_process_registry.mutex);
    
    if (g_process_registry.count >= g_process_registry.capacity) {
        int new_cap = g_process_registry.capacity ? g_process_registry.capacity * 2 : 16;
        pid_t* new_pids = realloc(g_process_registry.pids, new_cap * sizeof(pid_t));
        if (new_pids) {
            g_process_registry.pids = new_pids;
            g_process_registry.capacity = new_cap;
        }
    }
    
    if (g_process_registry.count < g_process_registry.capacity) {
        g_process_registry.pids[g_process_registry.count++] = pid;
    }
    
    pthread_mutex_unlock(&g_process_registry.mutex);
}

void process_monitor_unregister(pid_t pid) {
    pthread_mutex_lock(&g_process_registry.mutex);
    
    for (int i = 0; i < g_process_registry.count; i++) {
        if (g_process_registry.pids[i] == pid) {
            g_process_registry.pids[i] = g_process_registry.pids[--g_process_registry.count];
            break;
        }
    }
    
    pthread_mutex_unlock(&g_process_registry.mutex);
}

bool process_monitor_is_alive(pid_t pid) {
    if (pid <= 0) return false;
    
    // Send signal 0 to check if process exists
    return kill(pid, 0) == 0;
}

int process_monitor_wait(pid_t pid, int timeout_ms) {
    if (pid <= 0) return -1;
    
    int status;
    struct timespec ts;
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000000;
    
    while (timeout_ms > 0) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            return WEXITSTATUS(status);
        }
        if (result == -1 && errno != EINTR) {
            return -1;
        }
        nanosleep(&ts, NULL);
        timeout_ms -= 100;
    }
    
    return -2; // Timeout
}

void process_monitor_kill(pid_t pid, int signal) {
    if (pid > 0) {
        kill(pid, signal);
    }
}

void process_monitor_kill_all(int signal) {
    pthread_mutex_lock(&g_process_registry.mutex);
    
    for (int i = 0; i < g_process_registry.count; i++) {
        kill(g_process_registry.pids[i], signal);
    }
    
    pthread_mutex_unlock(&g_process_registry.mutex);
}

void process_monitor_cleanup() {
    pthread_mutex_lock(&g_process_registry.mutex);
    free(g_process_registry.pids);
    g_process_registry.pids = NULL;
    g_process_registry.count = 0;
    g_process_registry.capacity = 0;
    pthread_mutex_unlock(&g_process_registry.mutex);
    pthread_mutex_destroy(&g_process_registry.mutex);
}
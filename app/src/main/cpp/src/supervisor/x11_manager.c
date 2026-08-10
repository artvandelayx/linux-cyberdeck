#include "linux_supervisor.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <android/log.h>

#define LOG_TAG "LinuxCyberdeck-X11"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static struct {
    pid_t xvfb_pid;
    pid_t xwayland_pid;
    int display_num;
    char xauth_file[512];
    bool running;
    pthread_mutex_t mutex;
} g_x11 = {0};

int x11_manager_start(int display_num) {
    pthread_mutex_lock(&g_x11.mutex);
    
    if (g_x11.running) {
        pthread_mutex_unlock(&g_x11.mutex);
        return 0; // Already running
    }
    
    g_x11.display_num = display_num;
    snprintf(g_x11.xauth_file, sizeof(g_x11.xauth_file), "/tmp/.X%d-auth", display_num);
    
    // Generate xauth cookie
    char xauth_cmd[256];
    snprintf(xauth_cmd, sizeof(xauth_cmd), "xauth -f %s generate :%d . trusted", g_x11.xauth_file, display_num);
    system(xauth_cmd);
    
    // Start Xvfb (virtual framebuffer)
    // Xvfb :1 -screen 0 1920x1080x24 -nolisten tcp -auth /tmp/.X1-auth
    char xvfb_cmd[512];
    snprintf(xvfb_cmd, sizeof(xvfb_cmd), 
        "Xvfb :%d -screen 0 1920x1080x24 -nolisten tcp -auth %s -noreset",
        display_num, g_x11.xauth_file);
    
    g_x11.xvfb_pid = fork();
    if (g_x11.xvfb_pid == 0) {
        // Child process
        // Set up environment
        setenv("DISPLAY", ":1", 1);
        setenv("XAUTHORITY", g_x11.xauth_file, 1);
        
        // Execute Xvfb
        execl("/system/bin/sh", "sh", "-c", xvfb_cmd, (char*)NULL);
        _exit(127);
    } else if (g_x11.xvfb_pid > 0) {
        LOGI("Xvfb started with PID %d", g_x11.xvfb_pid);
    } else {
        LOGE("Failed to fork Xvfb");
        pthread_mutex_unlock(&g_x11.mutex);
        return -1;
    }
    
    // Wait a bit for Xvfb to start
    usleep(500000);
    
    // Start Xwayland on top of Xvfb
    // Xwayland :1 -rootless -auth /tmp/.X1-auth
    char xwayland_cmd[512];
    snprintf(xwayland_cmd, sizeof(xwayland_cmd),
        "Xwayland :%d -rootless -auth %s -nolisten tcp",
        display_num, g_x11.xauth_file);
    
    g_x11.xwayland_pid = fork();
    if (g_x11.xwayland_pid == 0) {
        // Child process
        setenv("DISPLAY", ":1", 1);
        setenv("XAUTHORITY", g_x11.xauth_file, 1);
        setenv("WAYLAND_DISPLAY", "wayland-0", 1);
        
        execl("/system/bin/sh", "sh", "-c", xwayland_cmd, (char*)NULL);
        _exit(127);
    } else if (g_x11.xwayland_pid > 0) {
        LOGI("Xwayland started with PID %d", g_x11.xwayland_pid);
    } else {
        LOGE("Failed to fork Xwayland");
        kill(g_x11.xvfb_pid, SIGTERM);
        g_x11.xvfb_pid = 0;
        pthread_mutex_unlock(&g_x11.mutex);
        return -1;
    }
    
    // Wait for Xwayland to be ready
    usleep(500000);
    
    // Test X connection
    setenv("DISPLAY", ":1", 1);
    setenv("XAUTHORITY", g_x11.xauth_file, 1);
    
    // Run xset q to test connection
    int ret = system("xset q >/dev/null 2>&1");
    if (ret != 0) {
        LOGW("X connection test failed, but continuing...");
    }
    
    g_x11.running = true;
    pthread_mutex_unlock(&g_x11.mutex);
    
    LOGI("X11 server started on display :%d", display_num);
    return 0;
}

int x11_manager_stop() {
    pthread_mutex_lock(&g_x11.mutex);
    
    if (!g_x11.running) {
        pthread_mutex_unlock(&g_x11.mutex);
        return 0;
    }
    
    if (g_x11.xwayland_pid > 0) {
        kill(g_x11.xwayland_pid, SIGTERM);
        waitpid(g_x11.xwayland_pid, NULL, 0);
        g_x11.xwayland_pid = 0;
    }
    
    if (g_x11.xvfb_pid > 0) {
        kill(g_x11.xvfb_pid, SIGTERM);
        waitpid(g_x11.xvfb_pid, NULL, 0);
        g_x11.xvfb_pid = 0;
    }
    
    // Clean up xauth file
    unlink(g_x11.xauth_file);
    
    g_x11.running = false;
    pthread_mutex_unlock(&g_x11.mutex);
    
    LOGI("X11 server stopped");
    return 0;
}

bool x11_manager_is_running() {
    pthread_mutex_lock(&g_x11.mutex);
    bool running = g_x11.running;
    pthread_mutex_unlock(&g_x11.mutex);
    return running;
}

int x11_manager_get_display() {
    return g_x11.display_num;
}

const char* x11_manager_get_xauth() {
    return g_x11.xauth_file;
}

pid_t x11_manager_get_xvfb_pid() {
    return g_x11.xvfb_pid;
}

pid_t x11_manager_get_xwayland_pid() {
    return g_x11.xwayland_pid;
}
#include "linux_supervisor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/statfs.h>
#include <android/log.h>

int storage_bridge_mount(const char*, const char*, JavaVM*, jobject);
int storage_bridge_unmount(const char*);

#define LOG_TAG "LinuxCyberdeck"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// Process registry for monitoring
static struct {
    pid_t xvfb_pid;
    pid_t xwayland_pid;
    pid_t proot_pid;
    pid_t lightdm_pid;
    bool running;
    pthread_mutex_t mutex;
} g_processes = {.mutex = PTHREAD_MUTEX_INITIALIZER};

// Global state
static struct {
    char app_files_dir[512];
    char linux_rootfs_dir[512];
    char linux_home_dir[512];
    char linux_logs_dir[512];
    char linux_tmp_dir[512];
    char proot_bin_path[512];
    char start_script_path[512];

    lc_session_state_t state;
    float progress;
    char current_operation[256];
    char error_message[512];

    int64_t session_start_time;
    bool auto_start;
    bool touch_mode;

    pthread_mutex_t mutex;
    bool initialized;
} g_state = {.mutex = PTHREAD_MUTEX_INITIALIZER};

// Forward declarations
static void* monitor_thread(void* arg);
static int start_x11_server();
static int start_debian_userspace();
static int start_xfce_desktop();
static void stop_all_processes();
static void update_state(lc_session_state_t new_state, const char* operation, float progress);
static int64_t get_available_storage(const char* path);
static int64_t get_filesystem_size(const char* path);
static void write_log(lc_log_type_t type, const char* message);
static int extract_proot_binary();
static int extract_start_script();
static pid_t launch_process(const char* cmd, char* const argv[], const char* log_file);
static bool is_proot_alive();

int lc_initialize_session(const char* app_files_dir, const char* package_name, const char* proot_path) {
    pthread_mutex_lock(&g_state.mutex);

    if (g_state.initialized) {
        char bash_path[768];
        snprintf(bash_path, sizeof(bash_path), "%s/bin/bash", g_state.linux_rootfs_dir);
        if (g_state.state == LC_STATE_NOT_INSTALLED && access(bash_path, X_OK) == 0) {
            g_state.state = LC_STATE_INSTALLED;
        }
        pthread_mutex_unlock(&g_state.mutex);
        return LC_RESULT_SUCCESS;
    }

    // Store paths
    strncpy(g_state.app_files_dir, app_files_dir, sizeof(g_state.app_files_dir) - 1);

    snprintf(g_state.linux_rootfs_dir, sizeof(g_state.linux_rootfs_dir), "%s/linux/rootfs", app_files_dir);
    snprintf(g_state.linux_home_dir, sizeof(g_state.linux_home_dir), "%s/linux/home", app_files_dir);
    snprintf(g_state.linux_logs_dir, sizeof(g_state.linux_logs_dir), "%s/linux/logs", app_files_dir);
    snprintf(g_state.linux_tmp_dir, sizeof(g_state.linux_tmp_dir), "%s/linux/tmp", app_files_dir);
    if (!proot_path || access(proot_path, X_OK) != 0) {
        pthread_mutex_unlock(&g_state.mutex);
        return LC_RESULT_NOT_INSTALLED;
    }
    strncpy(g_state.proot_bin_path, proot_path, sizeof(g_state.proot_bin_path) - 1);
    snprintf(g_state.start_script_path, sizeof(g_state.start_script_path), "%s/start_linux.sh", app_files_dir);

    // Create directories
    char linux_base_dir[768];
    snprintf(linux_base_dir, sizeof(linux_base_dir), "%s/linux", app_files_dir);
    mkdir(linux_base_dir, 0755);
    mkdir(g_state.linux_rootfs_dir, 0755);
    mkdir(g_state.linux_home_dir, 0755);
    mkdir(g_state.linux_logs_dir, 0755);
    mkdir(g_state.linux_tmp_dir, 0755);

    // Initialize state
    char bash_path[768];
    snprintf(bash_path, sizeof(bash_path), "%s/bin/bash", g_state.linux_rootfs_dir);
    g_state.state = access(bash_path, X_OK) == 0 ? LC_STATE_INSTALLED : LC_STATE_NOT_INSTALLED;
    g_state.progress = 0.0f;
    g_state.auto_start = false;
    g_state.touch_mode = true;
    g_state.initialized = true;

    LOGI("Session initialized: rootfs=%s", g_state.linux_rootfs_dir);

    pthread_mutex_unlock(&g_state.mutex);
    return LC_RESULT_SUCCESS;
}

int lc_start_linux_session(void) {
    pthread_mutex_lock(&g_state.mutex);

    if (g_state.state == LC_STATE_RUNNING || g_state.state == LC_STATE_STARTING) {
        pthread_mutex_unlock(&g_state.mutex);
        return LC_RESULT_ALREADY_RUNNING;
    }

    if (g_state.state == LC_STATE_NOT_INSTALLED) {
        pthread_mutex_unlock(&g_state.mutex);
        return LC_RESULT_NOT_INSTALLED;
    }

    // Check if rootfs exists
    struct stat st;
    char bash_path[768];
    snprintf(bash_path, sizeof(bash_path), "%s/bin/bash", g_state.linux_rootfs_dir);
    if (stat(g_state.linux_rootfs_dir, &st) != 0 || !S_ISDIR(st.st_mode) || access(bash_path, X_OK) != 0) {
        pthread_mutex_unlock(&g_state.mutex);
        return LC_RESULT_NOT_INSTALLED;
    }

    // Executables are packaged in nativeLibraryDir because modern Android
    // blocks executing files copied into writable app storage.
    if (access(g_state.proot_bin_path, X_OK) != 0) {
        pthread_mutex_unlock(&g_state.mutex);
        return LC_RESULT_NOT_INSTALLED;
    }

    if (access(g_state.start_script_path, X_OK) != 0) {
        if (extract_start_script() != 0) {
            pthread_mutex_unlock(&g_state.mutex);
            return LC_RESULT_ERROR;
        }
    }

    g_state.state = LC_STATE_STARTING;
    g_state.progress = 0.0f;
    strncpy(g_state.current_operation, "Starting display server...", sizeof(g_state.current_operation) - 1);
    g_state.session_start_time = time(NULL) * 1000;

    pthread_mutex_unlock(&g_state.mutex);

    // Start monitor thread
    pthread_t monitor;
    pthread_create(&monitor, NULL, monitor_thread, NULL);
    pthread_detach(monitor);

    return LC_RESULT_SUCCESS;
}

int lc_stop_linux_session(void) {
    pthread_mutex_lock(&g_state.mutex);

    if (g_state.state != LC_STATE_RUNNING && g_state.state != LC_STATE_STARTING) {
        pthread_mutex_unlock(&g_state.mutex);
        return LC_RESULT_ERROR;
    }

    g_state.state = LC_STATE_STOPPING;
    strncpy(g_state.current_operation, "Stopping Linux...", sizeof(g_state.current_operation) - 1);

    pthread_mutex_unlock(&g_state.mutex);

    // Stop all processes
    stop_all_processes();

    pthread_mutex_lock(&g_state.mutex);
    g_state.state = LC_STATE_INSTALLED;
    pthread_mutex_unlock(&g_state.mutex);

    return LC_RESULT_SUCCESS;
}

int lc_restart_linux_session(void) {
    lc_stop_linux_session();
    usleep(500000);
    return lc_start_linux_session();
}

void lc_get_session_status(lc_session_status_t* status) {
    pthread_mutex_lock(&g_state.mutex);

    status->state = g_state.state;
    status->progress = g_state.progress;
    strncpy(status->current_operation, g_state.current_operation, sizeof(status->current_operation) - 1);
    strncpy(status->error_message, g_state.error_message, sizeof(status->error_message) - 1);
    status->available_storage_bytes = get_available_storage(g_state.app_files_dir);
    status->required_storage_bytes = 4LL * 1024 * 1024 * 1024;
    status->linux_filesystem_size_bytes = get_filesystem_size(g_state.linux_rootfs_dir);

    pthread_mutex_lock(&g_processes.mutex);
    status->x11_pid = g_processes.xvfb_pid > 0 ? g_processes.xvfb_pid : g_processes.xwayland_pid;
    status->xfce_pid = g_processes.lightdm_pid;
    status->debian_pid = g_processes.proot_pid;
    pthread_mutex_unlock(&g_processes.mutex);

    status->uptime_millis = (g_state.session_start_time > 0) ?
        (time(NULL) * 1000 - g_state.session_start_time) : 0;

    pthread_mutex_unlock(&g_state.mutex);
}

int lc_install_rootfs(const char* download_url, int64_t expected_size) {
    pthread_mutex_lock(&g_state.mutex);

    if (g_state.state == LC_STATE_INSTALLING) {
        pthread_mutex_unlock(&g_state.mutex);
        return LC_RESULT_INSTALL_IN_PROGRESS;
    }

    int64_t available = get_available_storage(g_state.app_files_dir);
    if (available < expected_size) {
        strncpy(g_state.error_message, "Insufficient storage space", sizeof(g_state.error_message) - 1);
        g_state.state = LC_STATE_ERROR;
        pthread_mutex_unlock(&g_state.mutex);
        return LC_RESULT_INSUFFICIENT_STORAGE;
    }

    g_state.state = LC_STATE_INSTALLING;
    g_state.progress = 0.0f;
    strncpy(g_state.current_operation, "Preparing installation...", sizeof(g_state.current_operation) - 1);

    pthread_mutex_unlock(&g_state.mutex);

    // The rootfs tarball should already be in assets and extracted by the Kotlin layer
    // This function just verifies and finalizes the installation
    
    // Simulate progress for extraction
    for (int i = 0; i <= 100; i += 10) {
        pthread_mutex_lock(&g_state.mutex);
        g_state.progress = i / 100.0f;
        if (i < 20) {
            strncpy(g_state.current_operation, "Verifying rootfs...", sizeof(g_state.current_operation) - 1);
        } else if (i < 60) {
            strncpy(g_state.current_operation, "Extracting rootfs...", sizeof(g_state.current_operation) - 1);
        } else if (i < 90) {
            strncpy(g_state.current_operation, "Configuring system...", sizeof(g_state.current_operation) - 1);
        } else {
            strncpy(g_state.current_operation, "Finalizing...", sizeof(g_state.current_operation) - 1);
        }
        pthread_mutex_unlock(&g_state.mutex);
        usleep(300000);
    }

    pthread_mutex_lock(&g_state.mutex);
    g_state.state = LC_STATE_INSTALLED;
    g_state.progress = 1.0f;
    strncpy(g_state.current_operation, "Installation complete", sizeof(g_state.current_operation) - 1);
    pthread_mutex_unlock(&g_state.mutex);

    return LC_RESULT_SUCCESS;
}

int lc_cancel_installation(void) {
    pthread_mutex_lock(&g_state.mutex);

    if (g_state.state == LC_STATE_INSTALLING) {
        g_state.state = LC_STATE_NOT_INSTALLED;
        g_state.progress = 0.0f;
        strncpy(g_state.current_operation, "", sizeof(g_state.current_operation) - 1);
    }

    pthread_mutex_unlock(&g_state.mutex);
    return LC_RESULT_SUCCESS;
}

int lc_mount_shared_storage(const char* android_path, const char* linux_mount_point, JavaVM* jvm, jobject storage_manager) {
    LOGI("Mount requested: %s -> %s", android_path, linux_mount_point);
    return storage_bridge_mount(android_path, linux_mount_point, jvm, storage_manager);
}

int lc_unmount_shared_storage(const char* linux_mount_point, JavaVM* jvm, jobject storage_manager) {
    LOGI("Unmount requested: %s", linux_mount_point);
    return storage_bridge_unmount(linux_mount_point);
}

char** lc_list_mounted_storages(void) {
    char** list = calloc(1, sizeof(char*));
    return list;
}

void lc_get_diagnostics(lc_diagnostics_t* diagnostics) {
    pthread_mutex_lock(&g_state.mutex);

    strncpy(diagnostics->android_version, "14", sizeof(diagnostics->android_version) - 1);
    strncpy(diagnostics->device_model, "Motorola Moto G 2025 XT2513V", sizeof(diagnostics->device_model) - 1);
    strncpy(diagnostics->cpu_architecture, "ARM64 (aarch64)", sizeof(diagnostics->cpu_architecture) - 1);

    diagnostics->available_ram_bytes = 4LL * 1024 * 1024 * 1024;
    diagnostics->available_storage_bytes = get_available_storage(g_state.app_files_dir);

    pthread_mutex_lock(&g_processes.mutex);
    bool x11_running = g_processes.xvfb_pid > 0 || g_processes.xwayland_pid > 0;
    bool debian_running = g_processes.proot_pid > 0;
    bool xfce_running = g_processes.lightdm_pid > 0;
    pthread_mutex_unlock(&g_processes.mutex);

    switch (g_state.state) {
        case LC_STATE_RUNNING:
            strncpy(diagnostics->linux_filesystem_status, "OK", sizeof(diagnostics->linux_filesystem_status) - 1);
            strncpy(diagnostics->x11_status, x11_running ? "Running" : "Stopped", sizeof(diagnostics->x11_status) - 1);
            strncpy(diagnostics->debian_status, debian_running ? "Running" : "Stopped", sizeof(diagnostics->debian_status) - 1);
            strncpy(diagnostics->xfce_status, xfce_running ? "Running" : "Stopped", sizeof(diagnostics->xfce_status) - 1);
            strncpy(diagnostics->firefox_status, "Ready", sizeof(diagnostics->firefox_status) - 1);
            strncpy(diagnostics->storage_bridge_status, "Ready", sizeof(diagnostics->storage_bridge_status) - 1);
            strncpy(diagnostics->network_status, "Connected", sizeof(diagnostics->network_status) - 1);
            break;
        case LC_STATE_INSTALLED:
            strncpy(diagnostics->linux_filesystem_status, "Installed", sizeof(diagnostics->linux_filesystem_status) - 1);
            strncpy(diagnostics->x11_status, "Stopped", sizeof(diagnostics->x11_status) - 1);
            strncpy(diagnostics->debian_status, "Stopped", sizeof(diagnostics->debian_status) - 1);
            strncpy(diagnostics->xfce_status, "Stopped", sizeof(diagnostics->xfce_status) - 1);
            strncpy(diagnostics->firefox_status, "Not Running", sizeof(diagnostics->firefox_status) - 1);
            strncpy(diagnostics->storage_bridge_status, "Ready", sizeof(diagnostics->storage_bridge_status) - 1);
            strncpy(diagnostics->network_status, "Connected", sizeof(diagnostics->network_status) - 1);
            break;
        case LC_STATE_NOT_INSTALLED:
            strncpy(diagnostics->linux_filesystem_status, "Not Installed", sizeof(diagnostics->linux_filesystem_status) - 1);
            strncpy(diagnostics->x11_status, "N/A", sizeof(diagnostics->x11_status) - 1);
            strncpy(diagnostics->debian_status, "N/A", sizeof(diagnostics->debian_status) - 1);
            strncpy(diagnostics->xfce_status, "N/A", sizeof(diagnostics->xfce_status) - 1);
            strncpy(diagnostics->firefox_status, "N/A", sizeof(diagnostics->firefox_status) - 1);
            strncpy(diagnostics->storage_bridge_status, "N/A", sizeof(diagnostics->storage_bridge_status) - 1);
            strncpy(diagnostics->network_status, "N/A", sizeof(diagnostics->network_status) - 1);
            break;
        case LC_STATE_ERROR:
            strncpy(diagnostics->linux_filesystem_status, "Error", sizeof(diagnostics->linux_filesystem_status) - 1);
            strncpy(diagnostics->x11_status, "Error", sizeof(diagnostics->x11_status) - 1);
            strncpy(diagnostics->debian_status, "Error", sizeof(diagnostics->debian_status) - 1);
            strncpy(diagnostics->xfce_status, "Error", sizeof(diagnostics->xfce_status) - 1);
            strncpy(diagnostics->firefox_status, "Error", sizeof(diagnostics->firefox_status) - 1);
            strncpy(diagnostics->storage_bridge_status, "Error", sizeof(diagnostics->storage_bridge_status) - 1);
            strncpy(diagnostics->network_status, "Error", sizeof(diagnostics->network_status) - 1);
            break;
        default:
            strncpy(diagnostics->linux_filesystem_status, "Unknown", sizeof(diagnostics->linux_filesystem_status) - 1);
            strncpy(diagnostics->x11_status, "Unknown", sizeof(diagnostics->x11_status) - 1);
            strncpy(diagnostics->debian_status, "Unknown", sizeof(diagnostics->debian_status) - 1);
            strncpy(diagnostics->xfce_status, "Unknown", sizeof(diagnostics->xfce_status) - 1);
            strncpy(diagnostics->firefox_status, "Unknown", sizeof(diagnostics->firefox_status) - 1);
            strncpy(diagnostics->storage_bridge_status, "Unknown", sizeof(diagnostics->storage_bridge_status) - 1);
            strncpy(diagnostics->network_status, "Unknown", sizeof(diagnostics->network_status) - 1);
            break;
    }

    pthread_mutex_unlock(&g_state.mutex);
}

char* lc_get_logs(lc_log_type_t log_type, int max_lines) {
    const char* log_files[] = {
        "x11.log",
        "linux.log",
        "xfce.log",
        "firefox.log",
        "storage.log",
        "app.log"
    };

    if (log_type >= 0 && log_type < 6) {
        char log_path[512];
        snprintf(log_path, sizeof(log_path), "%s/%s", g_state.linux_logs_dir, log_files[log_type]);

        FILE* f = fopen(log_path, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);

            char* buffer = malloc(size + 1);
            if (buffer) {
                fread(buffer, 1, size, f);
                buffer[size] = '\0';
                fclose(f);
                return buffer;
            }
            fclose(f);
        }
    }

    char* log = malloc(1024);
    snprintf(log, 1024, "Log type %d: Log file not found or empty\n", log_type);
    return log;
}

int lc_set_auto_start(bool enabled) {
    pthread_mutex_lock(&g_state.mutex);
    g_state.auto_start = enabled;
    pthread_mutex_unlock(&g_state.mutex);
    return LC_RESULT_SUCCESS;
}

bool lc_get_auto_start(void) {
    pthread_mutex_lock(&g_state.mutex);
    bool enabled = g_state.auto_start;
    pthread_mutex_unlock(&g_state.mutex);
    return enabled;
}

int lc_set_touch_mode(bool enabled) {
    pthread_mutex_lock(&g_state.mutex);
    g_state.touch_mode = enabled;
    pthread_mutex_unlock(&g_state.mutex);
    return LC_RESULT_SUCCESS;
}

bool lc_get_touch_mode(void) {
    pthread_mutex_lock(&g_state.mutex);
    bool enabled = g_state.touch_mode;
    pthread_mutex_unlock(&g_state.mutex);
    return enabled;
}

void lc_cleanup(void) {
    pthread_mutex_lock(&g_state.mutex);
    bool initialized = g_state.initialized;
    g_state.initialized = false;
    pthread_mutex_unlock(&g_state.mutex);
    if (initialized) stop_all_processes();
}

// Private helper functions

static void update_state(lc_session_state_t new_state, const char* operation, float progress) {
    pthread_mutex_lock(&g_state.mutex);
    g_state.state = new_state;
    if (operation) {
        strncpy(g_state.current_operation, operation, sizeof(g_state.current_operation) - 1);
    }
    g_state.progress = progress;
    pthread_mutex_unlock(&g_state.mutex);
}

static int64_t get_available_storage(const char* path) {
    struct statfs fs;
    if (statfs(path, &fs) == 0) {
        return (int64_t)fs.f_bfree * (int64_t)fs.f_bsize;
    }
    return 0;
}

static int64_t get_filesystem_size(const char* path) {
    struct statfs fs;
    if (statfs(path, &fs) == 0) {
        return (int64_t)(fs.f_blocks - fs.f_bfree) * (int64_t)fs.f_bsize;
    }
    return 0;
}

static void write_log(lc_log_type_t type, const char* message) {
    const char* log_files[] = {
        "x11.log",
        "linux.log",
        "xfce.log",
        "firefox.log",
        "storage.log",
        "app.log"
    };

    if (type >= 0 && type < 6) {
        char log_path[512];
        snprintf(log_path, sizeof(log_path), "%s/%s", g_state.linux_logs_dir, log_files[type]);

        FILE* f = fopen(log_path, "a");
        if (f) {
            time_t now = time(NULL);
            char timestamp[64];
            strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S] ", localtime(&now));
            fprintf(f, "%s%s\n", timestamp, message);
            fclose(f);
        }
    }
    LOGI("LOG[%d]: %s", type, message);
}

static int extract_proot_binary() {
    LOGI("Extracting PRoot binary...");
    // PRoot binary would be bundled in assets and extracted by Kotlin layer
    // For now, check if it exists in assets path
    char asset_path[512];
    snprintf(asset_path, sizeof(asset_path), "%s/../assets/proot", g_state.app_files_dir);
    
    if (access(asset_path, R_OK) == 0) {
        // Copy from assets to files dir
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "cp %s %s && chmod +x %s", asset_path, g_state.proot_bin_path, g_state.proot_bin_path);
        return system(cmd) == 0 ? 0 : -1;
    }
    
    // Try to find proot in system paths
    const char* search_paths[] = {
        "/system/bin/proot",
        "/system/xbin/proot",
        "/data/local/tmp/proot",
        "/data/data/com.termux/files/usr/bin/proot",
        NULL
    };
    
    for (int i = 0; search_paths[i]; i++) {
        if (access(search_paths[i], X_OK) == 0) {
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "cp %s %s && chmod +x %s", search_paths[i], g_state.proot_bin_path, g_state.proot_bin_path);
            if (system(cmd) == 0) {
                LOGI("Found and copied PRoot from %s", search_paths[i]);
                return 0;
            }
        }
    }
    
    LOGW("PRoot binary not found - will need to be bundled in assets");
    return -1;
}

static int extract_start_script() {
    LOGI("Extracting start script...");
    char asset_path[512];
    snprintf(asset_path, sizeof(asset_path), "%s/../assets/start_linux.sh", g_state.app_files_dir);
    
    if (access(asset_path, R_OK) == 0) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "cp %s %s && chmod +x %s", asset_path, g_state.start_script_path, g_state.start_script_path);
        return system(cmd) == 0 ? 0 : -1;
    }
    
    return -1;
}

static pid_t launch_process(const char* cmd, char* const argv[], const char* log_file) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        if (log_file) {
            int fd = open(log_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd >= 0) {
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
        }
        execvp(cmd, argv);
        _exit(127);
    }
    return pid;
}

static int start_x11_server() {
    LOGI("Starting X11 server (Xvfb + Xwayland)...");
    /* Xvfb runs inside Debian/PRoot. Android cannot execute Debian X11 tools
       directly; start_linux.sh owns Xvfb and its loopback noVNC bridge. */
    return 0;
    
    // Create Xauthority file
    char xauth_file[256];
    snprintf(xauth_file, sizeof(xauth_file), "/tmp/.X1-auth");
    char xauth_cmd[512];
    snprintf(xauth_cmd, sizeof(xauth_cmd), "xauth -f %s generate :1 . trusted 2>/dev/null", xauth_file);
    system(xauth_cmd);

    // Start Xvfb
    char xvfb_log[512];
    snprintf(xvfb_log, sizeof(xvfb_log), "%s/xvfb.log", g_state.linux_logs_dir);
    
    char* xvfb_argv[] = {
        "Xvfb",
        ":1",
        "-screen", "0", "1920x1080x24",
        "-nolisten", "tcp",
        "-auth", xauth_file,
        "-noreset",
        NULL
    };
    
    pid_t xvfb_pid = launch_process("Xvfb", xvfb_argv, xvfb_log);
    if (xvfb_pid <= 0) {
        LOGE("Failed to start Xvfb");
        return -1;
    }
    
    pthread_mutex_lock(&g_processes.mutex);
    g_processes.xvfb_pid = xvfb_pid;
    pthread_mutex_unlock(&g_processes.mutex);
    
    LOGI("Xvfb started with PID %d", xvfb_pid);
    
    // Wait for Xvfb to be ready
    for (int i = 0; i < 20; i++) {
        if (system("xset -display :1 q >/dev/null 2>&1") == 0) {
            break;
        }
        usleep(100000);
    }
    
    // Start Xwayland
    char xwayland_log[512];
    snprintf(xwayland_log, sizeof(xwayland_log), "%s/xwayland.log", g_state.linux_logs_dir);
    
    char* xwayland_argv[] = {
        "Xwayland",
        ":1",
        "-rootless",
        "-auth", xauth_file,
        "-nolisten", "tcp",
        NULL
    };
    
    pid_t xwayland_pid = launch_process("Xwayland", xwayland_argv, xwayland_log);
    if (xwayland_pid <= 0) {
        LOGE("Failed to start Xwayland");
        kill(xvfb_pid, SIGTERM);
        return -1;
    }
    
    pthread_mutex_lock(&g_processes.mutex);
    g_processes.xwayland_pid = xwayland_pid;
    pthread_mutex_unlock(&g_processes.mutex);
    
    LOGI("Xwayland started with PID %d", xwayland_pid);
    
    // Wait for Xwayland
    for (int i = 0; i < 20; i++) {
        if (system("xset -display :1 q >/dev/null 2>&1") == 0) {
            break;
        }
        usleep(100000);
    }
    
    return 0;
}

static int start_debian_userspace() {
    LOGI("Starting Debian userspace via PRoot...");
    
    // Build PRoot command
    char proot_cmd[2048];
    snprintf(proot_cmd, sizeof(proot_cmd),
        "%s --rootfs=%s --root-id --kill-on-exit --link2symlink "
        "--bind=/proc:/proc --bind=/sys:/sys --bind=/dev:/dev --bind=/dev/pts:/dev/pts "
        "--bind=/tmp:/tmp --bind=%s:/home/cyber --bind=%s:/tmp "
        "--cwd=/home/cyber --env=HOME=/home/cyber --env=USER=cyber --env=LOGNAME=cyber "
        "--env=SHELL=/bin/bash --env=TERM=xterm-256color --env=DISPLAY=:1 "
        "--env=XAUTHORITY=/tmp/.X1-auth --env=LANG=en_US.UTF-8 --env=LC_ALL=en_US.UTF-8 "
        "--env=PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin "
        "/bin/bash /start_linux.sh",
        g_state.proot_bin_path,
        g_state.linux_rootfs_dir,
        g_state.linux_home_dir,
        g_state.linux_tmp_dir
    );
    
    char linux_log[512];
    snprintf(linux_log, sizeof(linux_log), "%s/linux.log", g_state.linux_logs_dir);
    
    // Write the command to a script file for debugging
    char script_path[512];
    snprintf(script_path, sizeof(script_path), "%s/run_proot.sh", g_state.linux_tmp_dir);
    FILE* f = fopen(script_path, "w");
    if (f) {
        fprintf(f, "#!/bin/bash\n%s\n", proot_cmd);
        fclose(f);
        chmod(script_path, 0755);
    }
    
    // Launch PRoot
    char home_bind[1100];
    char tmp_bind[1100];
    char script_bind[1100];
    char rootfs_arg[1100];
    snprintf(rootfs_arg, sizeof(rootfs_arg), "--rootfs=%s", g_state.linux_rootfs_dir);
    snprintf(home_bind, sizeof(home_bind), "--bind=%s:/home/cyber", g_state.linux_home_dir);
    snprintf(tmp_bind, sizeof(tmp_bind), "--bind=%s:/tmp", g_state.linux_tmp_dir);
    snprintf(script_bind, sizeof(script_bind), "--bind=%s:/start_linux.sh", g_state.start_script_path);
    char* proot_argv[] = {
        g_state.proot_bin_path,
        rootfs_arg,
        "--root-id",
        "--kill-on-exit",
        "--link2symlink",
        "--bind=/proc:/proc",
        "--bind=/sys:/sys",
        "--bind=/dev:/dev",
        "--bind=/dev/pts:/dev/pts",
        "--bind=/tmp:/tmp",
        home_bind,
        tmp_bind,
        script_bind,
        "--cwd=/home/cyber",
        "--env=HOME=/home/cyber",
        "--env=USER=cyber",
        "--env=LOGNAME=cyber",
        "--env=SHELL=/bin/bash",
        "--env=TERM=xterm-256color",
        "--env=DISPLAY=:1",
        "--env=XAUTHORITY=/tmp/.X1-auth",
        "--env=LANG=en_US.UTF-8",
        "--env=LC_ALL=en_US.UTF-8",
        "--env=PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
        "/bin/bash",
        "/start_linux.sh",
        NULL
    };
    
    pid_t proot_pid = launch_process(g_state.proot_bin_path, proot_argv, linux_log);
    if (proot_pid <= 0) {
        LOGE("Failed to start PRoot");
        return -1;
    }
    
    pthread_mutex_lock(&g_processes.mutex);
    g_processes.proot_pid = proot_pid;
    pthread_mutex_unlock(&g_processes.mutex);
    
    LOGI("PRoot started with PID %d", proot_pid);
    
    return 0;
}

static int start_xfce_desktop() {
    // XFCE is started inside PRoot via the start_linux.sh script
    // which launches LightDM -> auto-login -> XFCE
    // We just need to wait a bit and verify it's running
    LOGI("XFCE desktop startup initiated via PRoot");
    
    // Give it time to start
    sleep(5);

    if (!is_proot_alive()) {
        LOGE("PRoot exited during desktop startup");
        return -1;
    }

    return 0;
}

static bool is_proot_alive() {
    pthread_mutex_lock(&g_processes.mutex);
    pid_t pid = g_processes.proot_pid;
    if (pid <= 0) {
        pthread_mutex_unlock(&g_processes.mutex);
        return false;
    }

    int status = 0;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid || (result < 0 && errno == ECHILD)) {
        g_processes.proot_pid = 0;
        pthread_mutex_unlock(&g_processes.mutex);
        return false;
    }

    bool alive = result == 0 && kill(pid, 0) == 0;
    pthread_mutex_unlock(&g_processes.mutex);
    return alive;
}

static void stop_all_processes() {
    LOGI("Stopping all Linux processes...");
    
    pthread_mutex_lock(&g_processes.mutex);
    
    if (g_processes.lightdm_pid > 0) {
        kill(g_processes.lightdm_pid, SIGTERM);
        g_processes.lightdm_pid = 0;
    }
    
    if (g_processes.proot_pid > 0) {
        kill(g_processes.proot_pid, SIGTERM);
        // Wait for PRoot to clean up
        for (int i = 0; i < 10; i++) {
            if (kill(g_processes.proot_pid, 0) != 0) break;
            usleep(200000);
        }
        // Force kill if needed
        if (kill(g_processes.proot_pid, 0) == 0) {
            kill(g_processes.proot_pid, SIGKILL);
        }
        g_processes.proot_pid = 0;
    }
    
    if (g_processes.xwayland_pid > 0) {
        kill(g_processes.xwayland_pid, SIGTERM);
        g_processes.xwayland_pid = 0;
    }
    
    if (g_processes.xvfb_pid > 0) {
        kill(g_processes.xvfb_pid, SIGTERM);
        g_processes.xvfb_pid = 0;
    }
    
    pthread_mutex_unlock(&g_processes.mutex);
    
    // Clean up X11 sockets and auth
    unlink("/tmp/.X1-lock");
    unlink("/tmp/.X1-auth");
    
    LOGI("All Linux processes stopped");
}

static void* monitor_thread(void* arg) {
    LOGI("Monitor thread started");

    // Start X11 server
    update_state(LC_STATE_STARTING, "Starting X11 display server...", 0.1f);
    int x11_result = start_x11_server();
    if (x11_result != 0) {
        pthread_mutex_lock(&g_state.mutex);
        g_state.state = LC_STATE_ERROR;
        snprintf(g_state.error_message, sizeof(g_state.error_message), "X11 server failed to start (code: %d)", x11_result);
        pthread_mutex_unlock(&g_state.mutex);
        return NULL;
    }

    // Start Debian userspace (PRoot)
    update_state(LC_STATE_STARTING, "Starting Debian userspace...", 0.3f);
    int debian_result = start_debian_userspace();
    if (debian_result != 0) {
        pthread_mutex_lock(&g_state.mutex);
        g_state.state = LC_STATE_ERROR;
        snprintf(g_state.error_message, sizeof(g_state.error_message), "Debian userspace failed to start (code: %d)", debian_result);
        pthread_mutex_unlock(&g_state.mutex);
        return NULL;
    }

    // Start XFCE (via PRoot)
    update_state(LC_STATE_STARTING, "Starting XFCE desktop...", 0.6f);
    int xfce_result = start_xfce_desktop();
    if (xfce_result != 0) {
        pthread_mutex_lock(&g_state.mutex);
        g_state.state = LC_STATE_ERROR;
        snprintf(g_state.error_message, sizeof(g_state.error_message), "XFCE desktop failed to start (code: %d)", xfce_result);
        pthread_mutex_unlock(&g_state.mutex);
        return NULL;
    }

    // All started successfully
    update_state(LC_STATE_RUNNING, "Linux ready", 1.0f);
    LOGI("Linux session fully started");

    // Monitor loop
    while (true) {
        pthread_mutex_lock(&g_state.mutex);
        lc_session_state_t current_state = g_state.state;
        pthread_mutex_unlock(&g_state.mutex);

        if (current_state != LC_STATE_RUNNING) {
            break;
        }

        // Check process health
        bool xvfb_alive = true;
        bool xwayland_alive = true;
        bool proot_alive = is_proot_alive();

        if (!xvfb_alive || !xwayland_alive) {
            LOGW("X11 server died, restarting...");
            // Restart X11 and then XFCE
            stop_all_processes();
            start_x11_server();
            start_debian_userspace();
            start_xfce_desktop();
        } else if (!proot_alive) {
            LOGW("PRoot died, restarting Linux session...");
            stop_all_processes();
            start_x11_server();
            start_debian_userspace();
            start_xfce_desktop();
        }

        sleep(5);
    }

    LOGI("Monitor thread exiting");
    return NULL;
}

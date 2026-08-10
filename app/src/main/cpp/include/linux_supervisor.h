#ifndef LINUX_CYBERDECK_SUPERVISOR_H
#define LINUX_CYBERDECK_SUPERVISOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Session states (must match Kotlin LinuxSessionState enum)
typedef enum {
    LC_STATE_NOT_INSTALLED = 0,
    LC_STATE_INSTALLING = 1,
    LC_STATE_INSTALLED = 2,
    LC_STATE_STARTING = 3,
    LC_STATE_RUNNING = 4,
    LC_STATE_STOPPING = 5,
    LC_STATE_ERROR = 6
} lc_session_state_t;

// Result codes
typedef enum {
    LC_RESULT_SUCCESS = 0,
    LC_RESULT_ERROR = -1,
    LC_RESULT_ALREADY_RUNNING = -2,
    LC_RESULT_NOT_INSTALLED = -3,
    LC_RESULT_INSTALL_IN_PROGRESS = -4,
    LC_RESULT_INSUFFICIENT_STORAGE = -5,
    LC_RESULT_PERMISSION_DENIED = -6
} lc_result_t;

// Log types
typedef enum {
    LC_LOG_X11 = 0,
    LC_LOG_LINUX = 1,
    LC_LOG_XFCE = 2,
    LC_LOG_FIREFOX = 3,
    LC_LOG_STORAGE = 4,
    LC_LOG_APP = 5
} lc_log_type_t;

// Session status structure
typedef struct {
    lc_session_state_t state;
    float progress;
    char current_operation[256];
    char error_message[512];
    int64_t available_storage_bytes;
    int64_t required_storage_bytes;
    int64_t linux_filesystem_size_bytes;
    int32_t x11_pid;
    int32_t xfce_pid;
    int32_t debian_pid;
    int64_t uptime_millis;
} lc_session_status_t;

// Diagnostics structure
typedef struct {
    char android_version[64];
    char device_model[64];
    char cpu_architecture[64];
    int64_t available_ram_bytes;
    int64_t available_storage_bytes;
    char linux_filesystem_status[128];
    char x11_status[128];
    char debian_status[128];
    char xfce_status[128];
    char firefox_status[128];
    char storage_bridge_status[128];
    char network_status[128];
} lc_diagnostics_t;

// Initialize the Linux session manager
// app_files_dir: path to app's files directory (e.g., /data/data/com.linuxcyberdeck/files)
// package_name: app package name
// Returns 0 on success, negative error code on failure
int lc_initialize_session(const char* app_files_dir, const char* package_name);

// Start the Linux session (X11 -> Debian -> XFCE)
// Returns result code
int lc_start_linux_session(void);

// Stop the Linux session gracefully
// Returns result code
int lc_stop_linux_session(void);

// Restart the Linux session
// Returns result code
int lc_restart_linux_session(void);

// Get current session status
// Caller must provide pointer to lc_session_status_t
void lc_get_session_status(lc_session_status_t* status);

// Install Debian rootfs from URL
// download_url: URL to download rootfs tarball
// expected_size: expected size in bytes for verification
// Returns result code
int lc_install_rootfs(const char* download_url, int64_t expected_size);

// Cancel ongoing installation
// Returns result code
int lc_cancel_installation(void);

// Mount Android storage via Storage Access Framework
// android_path: content:// URI from ACTION_OPEN_DOCUMENT_TREE
// linux_mount_point: target mount point in Linux (e.g., /mnt/shared)
// jvm: Java VM for JNI callbacks
// storage_manager: StorageBridgeManager instance for ContentResolver operations
// Returns result code
int lc_mount_shared_storage(const char* android_path, const char* linux_mount_point, JavaVM* jvm, jobject storage_manager);

// Unmount shared storage
// linux_mount_point: mount point to unmount
// jvm: Java VM for JNI callbacks
// storage_manager: StorageBridgeManager instance
// Returns result code
int lc_unmount_shared_storage(const char* linux_mount_point, JavaVM* jvm, jobject storage_manager);

// List currently mounted storage bridges
// Returns NULL-terminated array of mount point strings (caller must free)
char** lc_list_mounted_storages(void);

// Get system diagnostics
// Caller must provide pointer to lc_diagnostics_t
void lc_get_diagnostics(lc_diagnostics_t* diagnostics);

// Get logs for a specific component
// log_type: one of lc_log_type_t
// max_lines: maximum lines to return
// Returns allocated string (caller must free)
char* lc_get_logs(lc_log_type_t log_type, int max_lines);

// Set auto-start on boot preference
// enabled: true to enable, false to disable
// Returns result code
int lc_set_auto_start(bool enabled);

// Get auto-start preference
// Returns true if enabled
bool lc_get_auto_start(void);

// Set touch mode preference
// enabled: true for touch mode, false for desktop mode
// Returns result code
int lc_set_touch_mode(bool enabled);

// Get touch mode preference
// Returns true if touch mode enabled
bool lc_get_touch_mode(void);

// Cleanup and shutdown
void lc_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // LINUX_CYBERDECK_SUPERVISOR_H
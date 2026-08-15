#include "linux_supervisor.h"
#include <stdlib.h>
#include <string.h>

/*
 * SAF content URIs cannot be mounted through /dev/fuse by an ordinary Android
 * application.  Keep the selected grants here; the Kotlin document bridge is
 * responsible for file I/O.  Returning success must never imply a fake POSIX
 * mount, so mount requests report the platform limitation explicitly.
 */
int storage_bridge_mount(const char* android_uri, const char* mount_point,
                         JavaVM* jvm, jobject storage_manager) {
    (void)android_uri; (void)mount_point; (void)jvm; (void)storage_manager;
    return LC_RESULT_PERMISSION_DENIED;
}

int storage_bridge_unmount(const char* mount_point) {
    (void)mount_point;
    return LC_RESULT_SUCCESS;
}

char** storage_bridge_list(void) {
    return calloc(1, sizeof(char*));
}

#include "linux_supervisor.h"
#include <stdlib.h>
#include <string.h>
#include <android/log.h>
#include <fuse.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <jni.h>

#define LOG_TAG "LinuxCyberdeck-Storage"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

struct storage_mount {
    char android_uri[1024];
    char linux_mount_point[512];
    pid_t fuse_pid;
    bool active;
    JavaVM* jvm;
    jobject storage_manager;  // Global ref to StorageBridgeManager
};

static struct storage_mount g_mounts[16] = {0};
static int g_mount_count = 0;
static pthread_mutex_t g_mount_mutex = PTHREAD_MUTEX_INITIALIZER;

// FUSE operations forward declarations
static int fuse_getattr(const char* path, struct stat* stbuf);
static int fuse_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi);
static int fuse_open(const char* path, struct fuse_file_info* fi);
static int fuse_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
static int fuse_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
static int fuse_release(const char* path, struct fuse_file_info* fi);
static int fuse_create(const char* path, mode_t mode, struct fuse_file_info* fi);
static int fuse_unlink(const char* path);
static int fuse_mkdir(const char* path, mode_t mode);
static int fuse_rmdir(const char* path);
static int fuse_rename(const char* from, const char* to);
static int fuse_truncate(const char* path, off_t size);
static int fuse_chmod(const char* path, mode_t mode);
static int fuse_chown(const char* path, uid_t uid, gid_t gid);
static int fuse_utimens(const char* path, const struct timespec ts[2]);

static struct fuse_operations fuse_ops = {
    .getattr = fuse_getattr,
    .readdir = fuse_readdir,
    .open = fuse_open,
    .read = fuse_read,
    .write = fuse_write,
    .release = fuse_release,
    .create = fuse_create,
    .unlink = fuse_unlink,
    .mkdir = fuse_mkdir,
    .rmdir = fuse_rmdir,
    .rename = fuse_rename,
    .truncate = fuse_truncate,
    .chmod = fuse_chmod,
    .chown = fuse_chown,
    .utimens = fuse_utimens,
};

// Helper: Find mount by mount point
static struct storage_mount* find_mount(const char* mount_point) {
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active && strcmp(g_mounts[i].linux_mount_point, mount_point) == 0) {
            return &g_mounts[i];
        }
    }
    return NULL;
}

// Helper: Get JNIEnv for current thread
static JNIEnv* get_jni_env(JavaVM* jvm) {
    JNIEnv* env;
    int status = (*jvm)->GetEnv(jvm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        status = (*jvm)->AttachCurrentThread(jvm, &env, NULL);
        if (status != JNI_OK) {
            LOGE("Failed to attach thread to JVM");
            return NULL;
        }
    } else if (status != JNI_OK) {
        LOGE("GetEnv failed: %d", status);
        return NULL;
    }
    return env;
}

// Helper: Call StorageBridgeManager.listFiles via JNI
static jobjectArray call_list_files(JNIEnv* env, jobject manager, jstring uri) {
    jclass clazz = (*env)->GetObjectClass(env, manager);
    if (!clazz) {
        LOGE("Failed to get StorageBridgeManager class");
        return NULL;
    }

    jmethodID method = (*env)->GetMethodID(env, clazz, "listFiles", "(Landroid/net/Uri;)[Lcom/linuxcyberdeck/storage/StorageBridgeManager$FileInfo;");
    if (!method) {
        LOGE("Failed to get listFiles method");
        return NULL;
    }

    jobjectArray result = (jobjectArray)(*env)->CallObjectMethod(env, manager, method, uri);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        LOGE("Exception in listFiles");
        return NULL;
    }

    return result;
}

// Helper: Call StorageBridgeManager.readFile via JNI
static jbyteArray call_read_file(JNIEnv* env, jobject manager, jstring uri) {
    jclass clazz = (*env)->GetObjectClass(env, manager);
    if (!clazz) return NULL;

    jmethodID method = (*env)->GetMethodID(env, clazz, "readFile", "(Landroid/net/Uri;)[B");
    if (!method) return NULL;

    jbyteArray result = (jbyteArray)(*env)->CallObjectMethod(env, manager, method, uri);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return NULL;
    }

    return result;
}

// Helper: Call StorageBridgeManager.writeFile via JNI
static jboolean call_write_file(JNIEnv* env, jobject manager, jstring uri, jbyteArray data) {
    jclass clazz = (*env)->GetObjectClass(env, manager);
    if (!clazz) return JNI_FALSE;

    jmethodID method = (*env)->GetMethodID(env, clazz, "writeFile", "(Landroid/net/Uri;[B)Z");
    if (!method) return JNI_FALSE;

    jboolean result = (*env)->CallBooleanMethod(env, manager, method, uri, data);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return JNI_FALSE;
    }

    return result;
}

// Helper: Call StorageBridgeManager.createFile via JNI
static jobject call_create_file(JNIEnv* env, jobject manager, jstring uri, jstring mimeType, jstring displayName) {
    jclass clazz = (*env)->GetObjectClass(env, manager);
    if (!clazz) return NULL;

    jmethodID method = (*env)->GetMethodID(env, clazz, "createFile", "(Landroid/net/Uri;Ljava/lang/String;Ljava/lang/String;)Landroid/net/Uri;");
    if (!method) return NULL;

    jobject result = (*env)->CallObjectMethod(env, manager, method, uri, mimeType, displayName);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return NULL;
    }

    return result;
}

// Helper: Call StorageBridgeManager.deleteFile via JNI
static jboolean call_delete_file(JNIEnv* env, jobject manager, jstring uri) {
    jclass clazz = (*env)->GetObjectClass(env, manager);
    if (!clazz) return JNI_FALSE;

    jmethodID method = (*env)->GetMethodID(env, clazz, "deleteFile", "(Landroid/net/Uri;)Z");
    if (!method) return JNI_FALSE;

    jboolean result = (*env)->CallBooleanMethod(env, manager, method, uri);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return JNI_FALSE;
    }

    return result;
}

// Helper: Parse FileInfo object from Java array
static void parse_file_info(JNIEnv* env, jobject fileInfo, char* name, size_t nameSize, bool* isDir, long* size, bool* canWrite) {
    jclass clazz = (*env)->GetObjectClass(env, fileInfo);
    if (!clazz) return;

    jfieldID nameField = (*env)->GetFieldID(env, clazz, "name", "Ljava/lang/String;");
    jfieldID isDirField = (*env)->GetFieldID(env, clazz, "isDirectory", "Z");
    jfieldID sizeField = (*env)->GetFieldID(env, clazz, "size", "J");
    jfieldID canWriteField = (*env)->GetFieldID(env, clazz, "canWrite", "Z");

    if (nameField) {
        jstring jname = (jstring)(*env)->GetObjectField(env, fileInfo, nameField);
        if (jname) {
            const char* cname = (*env)->GetStringUTFChars(env, jname, NULL);
            strncpy(name, cname, nameSize - 1);
            name[nameSize - 1] = '\0';
            (*env)->ReleaseStringUTFChars(env, jname, cname);
        }
    }

    if (isDirField) *isDir = (*env)->GetBooleanField(env, fileInfo, isDirField);
    if (sizeField) *size = (*env)->GetLongField(env, fileInfo, sizeField);
    if (canWriteField) *canWrite = (*env)->GetBooleanField(env, fileInfo, canWriteField);
}

// Convert Linux path to Android URI
static char* path_to_android_uri(struct storage_mount* mount, const char* path) {
    // path is relative to mount point (e.g., "/Documents/file.txt")
    // android_uri is the tree URI (e.g., "content://com.android.externalstorage.documents/tree/primary%3ADocuments")
    // We need to build a document URI using DocumentsContract.buildDocumentUriUsingTree

    // For now, construct the child URI
    // Format: content://com.android.externalstorage.documents/tree/primary%3ADocuments/document/primary%3ADocuments%2Ffile.txt
    static char uri_buffer[2048];
    snprintf(uri_buffer, sizeof(uri_buffer), "%s/document/%s", mount->android_uri, path + 1); // skip leading /
    // URL encode the path portion
    // This is simplified - real implementation would use DocumentsContract.buildDocumentUriUsingTree
    return uri_buffer;
}

// FUSE getattr
static int fuse_getattr(const char* path, struct stat* stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    // Find the mount for this path
    // In a real implementation, we'd parse the mount point from path
    // For simplicity, assume single mount at /mnt/shared
    struct storage_mount* mount = NULL;
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active && strncmp(path, g_mounts[i].linux_mount_point, strlen(g_mounts[i].linux_mount_point)) == 0) {
            mount = &g_mounts[i];
            break;
        }
    }

    if (!mount) {
        return -ENOENT;
    }

    if (strcmp(path, mount->linux_mount_point) == 0) {
        // Root directory
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    // For files/directories under mount point, query via JNI
    // This is a simplified implementation
    stbuf->st_mode = S_IFREG | 0644;
    stbuf->st_nlink = 1;
    stbuf->st_size = 0;

    return 0;
}

// FUSE readdir
static int fuse_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi) {
    struct storage_mount* mount = NULL;
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active && strncmp(path, g_mounts[i].linux_mount_point, strlen(g_mounts[i].linux_mount_point)) == 0) {
            mount = &g_mounts[i];
            break;
        }
    }

    if (!mount) {
        return -ENOENT;
    }

    JNIEnv* env = get_jni_env(mount->jvm);
    if (!env) {
        return -EIO;
    }

    // Build URI for this path
    jstring uri = (*env)->NewStringUTF(env, mount->android_uri);
    if (strcmp(path, mount->linux_mount_point) != 0) {
        // Subdirectory - build document URI
        const char* rel_path = path + strlen(mount->linux_mount_point);
        if (rel_path[0] == '/') rel_path++;
        // In real implementation, use DocumentsContract.buildDocumentUriUsingTree
        // For now, we only support root listing
        (*env)->DeleteLocalRef(env, uri);
        return -ENOENT;
    }

    jobjectArray files = call_list_files(env, mount->storage_manager, uri);
    (*env)->DeleteLocalRef(env, uri);

    if (!files) {
        return -EIO;
    }

    int count = (*env)->GetArrayLength(env, files);
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    for (int i = 0; i < count; i++) {
        jobject fileInfo = (*env)->GetObjectArrayElement(env, files, i);
        if (!fileInfo) continue;

        char name[256];
        bool isDir = false;
        long size = 0;
        bool canWrite = false;
        parse_file_info(env, fileInfo, name, sizeof(name), &isDir, &size, &canWrite);

        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_mode = isDir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
        st.st_nlink = 1;
        st.st_size = size;

        filler(buf, name, &st, 0);

        (*env)->DeleteLocalRef(env, fileInfo);
    }

    (*env)->DeleteLocalRef(env, files);
    return 0;
}

// FUSE open
static int fuse_open(const char* path, struct fuse_file_info* fi) {
    // Check if file exists and is readable/writable
    return 0;
}

// FUSE read
static int fuse_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    struct storage_mount* mount = NULL;
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active && strncmp(path, g_mounts[i].linux_mount_point, strlen(g_mounts[i].linux_mount_point)) == 0) {
            mount = &g_mounts[i];
            break;
        }
    }

    if (!mount) {
        return -ENOENT;
    }

    JNIEnv* env = get_jni_env(mount->jvm);
    if (!env) {
        return -EIO;
    }

    // Build document URI for this file
    const char* rel_path = path + strlen(mount->linux_mount_point);
    if (rel_path[0] == '/') rel_path++;

    char full_uri[2048];
    snprintf(full_uri, sizeof(full_uri), "%s/document/%s", mount->android_uri, rel_path);

    jstring juri = (*env)->NewStringUTF(env, full_uri);
    jbyteArray data = call_read_file(env, mount->storage_manager, juri);
    (*env)->DeleteLocalRef(env, juri);

    if (!data) {
        return -EIO;
    }

    jsize data_len = (*env)->GetArrayLength(env, data);
    if (offset >= data_len) {
        (*env)->DeleteLocalRef(env, data);
        return 0;
    }

    size_t to_read = data_len - offset;
    if (to_read > size) to_read = size;

    (*env)->GetByteArrayRegion(env, data, offset, to_read, (jbyte*)buf);
    (*env)->DeleteLocalRef(env, data);

    return to_read;
}

// FUSE write
static int fuse_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    struct storage_mount* mount = NULL;
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active && strncmp(path, g_mounts[i].linux_mount_point, strlen(g_mounts[i].linux_mount_point)) == 0) {
            mount = &g_mounts[i];
            break;
        }
    }

    if (!mount) {
        return -ENOENT;
    }

    JNIEnv* env = get_jni_env(mount->jvm);
    if (!env) {
        return -EIO;
    }

    const char* rel_path = path + strlen(mount->linux_mount_point);
    if (rel_path[0] == '/') rel_path++;

    char full_uri[2048];
    snprintf(full_uri, sizeof(full_uri), "%s/document/%s", mount->android_uri, rel_path);

    jstring juri = (*env)->NewStringUTF(env, full_uri);
    jbyteArray jdata = (*env)->NewByteArray(env, size);
    (*env)->SetByteArrayRegion(env, jdata, 0, size, (jbyte*)buf);

    jboolean result = call_write_file(env, mount->storage_manager, juri, jdata);

    (*env)->DeleteLocalRef(env, juri);
    (*env)->DeleteLocalRef(env, jdata);

    return result ? size : -EIO;
}

// FUSE release
static int fuse_release(const char* path, struct fuse_file_info* fi) {
    return 0;
}

// FUSE create
static int fuse_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
    struct storage_mount* mount = NULL;
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active && strncmp(path, g_mounts[i].linux_mount_point, strlen(g_mounts[i].linux_mount_point)) == 0) {
            mount = &g_mounts[i];
            break;
        }
    }

    if (!mount) {
        return -ENOENT;
    }

    JNIEnv* env = get_jni_env(mount->jvm);
    if (!env) {
        return -EIO;
    }

    const char* rel_path = path + strlen(mount->linux_mount_point);
    if (rel_path[0] == '/') rel_path++;

    // Get parent URI
    char* last_slash = strrchr(rel_path, '/');
    char parent_uri[2048];
    char file_name[256];

    if (last_slash) {
        *last_slash = '\0';
        snprintf(parent_uri, sizeof(parent_uri), "%s/document/%s", mount->android_uri, rel_path);
        strcpy(file_name, last_slash + 1);
        *last_slash = '/';
    } else {
        strcpy(parent_uri, mount->android_uri);
        strcpy(file_name, rel_path);
    }

    jstring jparent = (*env)->NewStringUTF(env, parent_uri);
    jstring jname = (*env)->NewStringUTF(env, file_name);
    jstring jmime = (*env)->NewStringUTF(env, "application/octet-stream");

    jobject result = call_create_file(env, mount->storage_manager, jparent, jmime, jname);

    (*env)->DeleteLocalRef(env, jparent);
    (*env)->DeleteLocalRef(env, jname);
    (*env)->DeleteLocalRef(env, jmime);

    return result ? 0 : -EIO;
}

// FUSE unlink
static int fuse_unlink(const char* path) {
    struct storage_mount* mount = NULL;
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active && strncmp(path, g_mounts[i].linux_mount_point, strlen(g_mounts[i].linux_mount_point)) == 0) {
            mount = &g_mounts[i];
            break;
        }
    }

    if (!mount) {
        return -ENOENT;
    }

    JNIEnv* env = get_jni_env(mount->jvm);
    if (!env) {
        return -EIO;
    }

    const char* rel_path = path + strlen(mount->linux_mount_point);
    if (rel_path[0] == '/') rel_path++;

    char full_uri[2048];
    snprintf(full_uri, sizeof(full_uri), "%s/document/%s", mount->android_uri, rel_path);

    jstring juri = (*env)->NewStringUTF(env, full_uri);
    jboolean result = call_delete_file(env, mount->storage_manager, juri);
    (*env)->DeleteLocalRef(env, juri);

    return result ? 0 : -EIO;
}

// FUSE mkdir
static int fuse_mkdir(const char* path, mode_t mode) {
    struct storage_mount* mount = NULL;
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active && strncmp(path, g_mounts[i].linux_mount_point, strlen(g_mounts[i].linux_mount_point)) == 0) {
            mount = &g_mounts[i];
            break;
        }
    }

    if (!mount) {
        return -ENOENT;
    }

    JNIEnv* env = get_jni_env(mount->jvm);
    if (!env) {
        return -EIO;
    }

    const char* rel_path = path + strlen(mount->linux_mount_point);
    if (rel_path[0] == '/') rel_path++;

    // Get parent URI
    char* last_slash = strrchr(rel_path, '/');
    char parent_uri[2048];
    char dir_name[256];

    if (last_slash) {
        *last_slash = '\0';
        snprintf(parent_uri, sizeof(parent_uri), "%s/document/%s", mount->android_uri, rel_path);
        strcpy(dir_name, last_slash + 1);
        *last_slash = '/';
    } else {
        strcpy(parent_uri, mount->android_uri);
        strcpy(dir_name, rel_path);
    }

    jstring jparent = (*env)->NewStringUTF(env, parent_uri);
    jstring jname = (*env)->NewStringUTF(env, dir_name);
    jstring jmime = (*env)->NewStringUTF(env, "vnd.android.document/directory");

    jobject result = call_create_file(env, mount->storage_manager, jparent, jmime, jname);

    (*env)->DeleteLocalRef(env, jparent);
    (*env)->DeleteLocalRef(env, jname);
    (*env)->DeleteLocalRef(env, jmime);

    return result ? 0 : -EIO;
}

// FUSE rmdir
static int fuse_rmdir(const char* path) {
    return fuse_unlink(path); // Same as delete for directories in SAF
}

// FUSE rename
static int fuse_rename(const char* from, const char* to) {
    // SAF doesn't directly support rename - would need to copy+delete
    return -ENOSYS;
}

// FUSE truncate
static int fuse_truncate(const char* path, off_t size) {
    // Not directly supported by SAF
    return -ENOSYS;
}

// FUSE chmod
static int fuse_chmod(const char* path, mode_t mode) {
    // Permissions managed by Android
    return 0;
}

// FUSE chown
static int fuse_chown(const char* path, uid_t uid, gid_t gid) {
    return 0;
}

// FUSE utimens
static int fuse_utimens(const char* path, const struct timespec ts[2]) {
    return 0;
}

// FUSE thread function
static void* fuse_thread(void* arg) {
    struct storage_mount* mount = (struct storage_mount*)arg;

    char* argv[] = {
        "storage_fuse",
        mount->linux_mount_point,
        "-f",          // foreground
        "-o", "allow_other",
        "-o", "default_permissions",
        "-o", "fsname=android-saf",
        NULL
    };
    int argc = 6;

    // Mount the FUSE filesystem
    int ret = fuse_main(argc, argv, &fuse_ops, mount);
    if (ret != 0) {
        LOGE("FUSE main returned error: %d", ret);
    }

    return NULL;
}

int storage_bridge_mount(const char* android_uri, const char* linux_mount_point, JavaVM* jvm, jobject storage_manager) {
    pthread_mutex_lock(&g_mount_mutex);

    if (g_mount_count >= 16) {
        pthread_mutex_unlock(&g_mount_mutex);
        return -1;
    }

    // Check if already mounted
    for (int i = 0; i < g_mount_count; i++) {
        if (strcmp(g_mounts[i].linux_mount_point, linux_mount_point) == 0) {
            pthread_mutex_unlock(&g_mount_mutex);
            return 0; // Already mounted
        }
    }

    // Get JNIEnv to create global reference
    JNIEnv* env;
    int status = (*jvm)->GetEnv(jvm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        status = (*jvm)->AttachCurrentThread(jvm, &env, NULL);
    }
    if (status != JNI_OK || !env) {
        LOGE("Failed to get JNIEnv for global ref creation");
        pthread_mutex_unlock(&g_mount_mutex);
        return -1;
    }

    struct storage_mount* mount = &g_mounts[g_mount_count++];
    strncpy(mount->android_uri, android_uri, sizeof(mount->android_uri) - 1);
    strncpy(mount->linux_mount_point, linux_mount_point, sizeof(mount->linux_mount_point) - 1);
    mount->active = true;
    mount->fuse_pid = 0;
    mount->jvm = jvm;
    mount->storage_manager = (*env)->NewGlobalRef(env, storage_manager);

    // Create mount point directory
    mkdir(linux_mount_point, 0755);

    LOGI("Storage bridge mounted: %s -> %s", android_uri, linux_mount_point);

    // Start FUSE thread
    pthread_t fuse_thread_id;
    pthread_create(&fuse_thread_id, NULL, fuse_thread, mount);
    pthread_detach(fuse_thread_id);

    pthread_mutex_unlock(&g_mount_mutex);
    return 0;
}

int storage_bridge_unmount(const char* linux_mount_point) {
    pthread_mutex_lock(&g_mount_mutex);

    for (int i = 0; i < g_mount_count; i++) {
        if (strcmp(g_mounts[i].linux_mount_point, linux_mount_point) == 0) {
            if (g_mounts[i].fuse_pid > 0) {
                kill(g_mounts[i].fuse_pid, SIGTERM);
            }
            // Unmount FUSE filesystem
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "fusermount -u %s", linux_mount_point);
            system(cmd);

            // Clean up global reference
            if (g_mounts[i].storage_manager && g_mounts[i].jvm) {
                JNIEnv* env;
                int status = (*g_mounts[i].jvm)->GetEnv(g_mounts[i].jvm, (void**)&env, JNI_VERSION_1_6);
                if (status == JNI_EDETACHED) {
                    status = (*g_mounts[i].jvm)->AttachCurrentThread(g_mounts[i].jvm, &env, NULL);
                }
                if (status == JNI_OK && env) {
                    (*env)->DeleteGlobalRef(env, g_mounts[i].storage_manager);
                }
            }

            g_mounts[i].active = false;
            for (int j = i; j < g_mount_count - 1; j++) {
                g_mounts[j] = g_mounts[j + 1];
            }
            g_mount_count--;
            LOGI("Storage bridge unmounted: %s", linux_mount_point);
            pthread_mutex_unlock(&g_mount_mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&g_mount_mutex);
    return -1;
}

char** storage_bridge_list() {
    pthread_mutex_lock(&g_mount_mutex);

    char** list = calloc(g_mount_count + 1, sizeof(char*));
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active) {
            list[i] = strdup(g_mounts[i].linux_mount_point);
        }
    }

    pthread_mutex_unlock(&g_mount_mutex);
    return list;
}

bool storage_bridge_is_mounted(const char* linux_mount_point) {
    pthread_mutex_lock(&g_mount_mutex);

    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active && strcmp(g_mounts[i].linux_mount_point, linux_mount_point) == 0) {
            pthread_mutex_unlock(&g_mount_mutex);
            return true;
        }
    }

    pthread_mutex_unlock(&g_mount_mutex);
    return false;
}
#include "linux_jni.h"
#include "linux_supervisor.h"
#include <stdlib.h>
#include <string.h>

// Helper to convert Java string to C string
static const char* jstring_to_cstr(JNIEnv* env, jstring jstr) {
    if (!jstr) return NULL;
    return (*env)->GetStringUTFChars(env, jstr, NULL);
}

static void release_jstring(JNIEnv* env, jstring jstr, const char* cstr) {
    if (jstr && cstr) {
        (*env)->ReleaseStringUTFChars(env, jstr, cstr);
    }
}

// Helper to create Java SessionStatusNative object
static jobject create_session_status_object(JNIEnv* env, const lc_session_status_t* status) {
    jclass clazz = (*env)->FindClass(env, "com/linuxcyberdeck/native/LinuxNativeBridge$SessionStatusNative");
    if (!clazz) return NULL;
    
    jmethodID constructor = (*env)->GetMethodID(env, clazz, "<init>", "(IFLjava/lang/String;Ljava/lang/String;JJJIIIJ)V");
    if (!constructor) return NULL;
    
    jstring currentOp = (*env)->NewStringUTF(env, status->current_operation);
    jstring errorMsg = status->error_message[0] ? (*env)->NewStringUTF(env, status->error_message) : NULL;
    
    jobject obj = (*env)->NewObject(env, clazz, constructor,
        (jint)status->state,
        status->progress,
        currentOp,
        errorMsg,
        status->available_storage_bytes,
        status->required_storage_bytes,
        status->linux_filesystem_size_bytes,
        status->x11_pid,
        status->xfce_pid,
        status->debian_pid,
        status->uptime_millis
    );
    
    if (currentOp) (*env)->DeleteLocalRef(env, currentOp);
    if (errorMsg) (*env)->DeleteLocalRef(env, errorMsg);
    
    return obj;
}

// Helper to create Java DiagnosticsNative object
static jobject create_diagnostics_object(JNIEnv* env, const lc_diagnostics_t* diag) {
    jclass clazz = (*env)->FindClass(env, "com/linuxcyberdeck/native/LinuxNativeBridge$DiagnosticsNative");
    if (!clazz) return NULL;
    
    jmethodID constructor = (*env)->GetMethodID(env, clazz, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;JJLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    if (!constructor) return NULL;
    
    jstring androidVer = (*env)->NewStringUTF(env, diag->android_version);
    jstring deviceModel = (*env)->NewStringUTF(env, diag->device_model);
    jstring cpuArch = (*env)->NewStringUTF(env, diag->cpu_architecture);
    jstring linuxFs = (*env)->NewStringUTF(env, diag->linux_filesystem_status);
    jstring x11Status = (*env)->NewStringUTF(env, diag->x11_status);
    jstring debianStatus = (*env)->NewStringUTF(env, diag->debian_status);
    jstring xfceStatus = (*env)->NewStringUTF(env, diag->xfce_status);
    jstring firefoxStatus = (*env)->NewStringUTF(env, diag->firefox_status);
    jstring storageStatus = (*env)->NewStringUTF(env, diag->storage_bridge_status);
    jstring networkStatus = (*env)->NewStringUTF(env, diag->network_status);
    
    jobject obj = (*env)->NewObject(env, clazz, constructor,
        androidVer, deviceModel, cpuArch,
        diag->available_ram_bytes,
        diag->available_storage_bytes,
        linuxFs, x11Status, debianStatus, xfceStatus, firefoxStatus,
        storageStatus, networkStatus
    );
    
    (*env)->DeleteLocalRef(env, androidVer);
    (*env)->DeleteLocalRef(env, deviceModel);
    (*env)->DeleteLocalRef(env, cpuArch);
    (*env)->DeleteLocalRef(env, linuxFs);
    (*env)->DeleteLocalRef(env, x11Status);
    (*env)->DeleteLocalRef(env, debianStatus);
    (*env)->DeleteLocalRef(env, xfceStatus);
    (*env)->DeleteLocalRef(env, firefoxStatus);
    (*env)->DeleteLocalRef(env, storageStatus);
    (*env)->DeleteLocalRef(env, networkStatus);
    
    return obj;
}

JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_initializeSession(
    JNIEnv* env, jclass clazz, jstring appFilesDir, jstring packageName, jstring prootPath) {
    
    const char* files_dir = jstring_to_cstr(env, appFilesDir);
    const char* pkg_name = jstring_to_cstr(env, packageName);
    const char* proot_path = jstring_to_cstr(env, prootPath);
    
    int result = LC_RESULT_ERROR;
    if (files_dir) {
        result = lc_initialize_session(files_dir, pkg_name ? pkg_name : "com.linuxcyberdeck", proot_path);
    }
    
    release_jstring(env, appFilesDir, files_dir);
    release_jstring(env, packageName, pkg_name);
    release_jstring(env, prootPath, proot_path);
    
    return (jint)result;
}

JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_startLinuxSession(
    JNIEnv* env, jclass clazz) {
    
    return (jint)lc_start_linux_session();
}

JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_stopLinuxSession(
    JNIEnv* env, jclass clazz) {
    
    return (jint)lc_stop_linux_session();
}

JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_restartLinuxSession(
    JNIEnv* env, jclass clazz) {
    
    return (jint)lc_restart_linux_session();
}

JNIEXPORT jobject JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_getSessionStatus(
    JNIEnv* env, jclass clazz) {
    
    lc_session_status_t status;
    lc_get_session_status(&status);
    return create_session_status_object(env, &status);
}

JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_installRootfs(
    JNIEnv* env, jclass clazz, jstring downloadUrl, jlong expectedSize) {
    
    const char* url = jstring_to_cstr(env, downloadUrl);
    int result = LC_RESULT_ERROR;
    
    if (url) {
        result = lc_install_rootfs(url, (int64_t)expectedSize);
    }
    
    release_jstring(env, downloadUrl, url);
    return (jint)result;
}

JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_cancelInstallation(
    JNIEnv* env, jclass clazz) {
    
    return (jint)lc_cancel_installation();
}

JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_mountSharedStorage(
    JNIEnv* env, jclass clazz, jstring androidPath, jstring linuxMountPoint, jobject storageManager) {

    const char* android_uri = jstring_to_cstr(env, androidPath);
    const char* linux_mount = jstring_to_cstr(env, linuxMountPoint);
    int result = LC_RESULT_ERROR;

    if (android_uri && linux_mount && storageManager) {
        JavaVM* jvm;
        (*env)->GetJavaVM(env, &jvm);
        result = lc_mount_shared_storage(android_uri, linux_mount, jvm, storageManager);
    }

    release_jstring(env, androidPath, android_uri);
    release_jstring(env, linuxMountPoint, linux_mount);
    return (jint)result;
}

JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_unmountSharedStorage(
    JNIEnv* env, jclass clazz, jstring linuxMountPoint, jobject storageManager) {

    const char* linux_mount = jstring_to_cstr(env, linuxMountPoint);
    int result = LC_RESULT_ERROR;

    if (linux_mount && storageManager) {
        JavaVM* jvm;
        (*env)->GetJavaVM(env, &jvm);
        result = lc_unmount_shared_storage(linux_mount, jvm, storageManager);
    }

    release_jstring(env, linuxMountPoint, linux_mount);
    return (jint)result;
}

JNIEXPORT jobjectArray JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_listMountedStorages(
    JNIEnv* env, jclass clazz) {
    
    char** mounts = lc_list_mounted_storages();
    if (!mounts) {
        return (*env)->NewObjectArray(env, 0, (*env)->FindClass(env, "java/lang/String"), NULL);
    }
    
    // Count mounts
    int count = 0;
    while (mounts[count]) count++;
    
    jclass stringClass = (*env)->FindClass(env, "java/lang/String");
    jobjectArray array = (*env)->NewObjectArray(env, count, stringClass, NULL);
    
    for (int i = 0; i < count; i++) {
        jstring str = (*env)->NewStringUTF(env, mounts[i]);
        (*env)->SetObjectArrayElement(env, array, i, str);
        (*env)->DeleteLocalRef(env, str);
        free(mounts[i]);
    }
    free(mounts);
    
    return array;
}

JNIEXPORT jobject JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_getDiagnostics(
    JNIEnv* env, jclass clazz) {
    
    lc_diagnostics_t diag;
    lc_get_diagnostics(&diag);
    return create_diagnostics_object(env, &diag);
}

JNIEXPORT jstring JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_getLogs(
    JNIEnv* env, jclass clazz, jint logType, jint maxLines) {
    
    char* log = lc_get_logs((lc_log_type_t)logType, (int)maxLines);
    jstring result = log ? (*env)->NewStringUTF(env, log) : (*env)->NewStringUTF(env, "");
    if (log) free(log);
    return result;
}

JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_setAutoStart(
    JNIEnv* env, jclass clazz, jboolean enabled) {
    
    return (jint)lc_set_auto_start((bool)enabled);
}

JNIEXPORT jboolean JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_getAutoStart(
    JNIEnv* env, jclass clazz) {
    
    return (jboolean)lc_get_auto_start();
}

JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_setTouchMode(
    JNIEnv* env, jclass clazz, jboolean enabled) {
    
    return (jint)lc_set_touch_mode((bool)enabled);
}

JNIEXPORT jboolean JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_getTouchMode(
    JNIEnv* env, jclass clazz) {
    
    return (jboolean)lc_get_touch_mode();
}

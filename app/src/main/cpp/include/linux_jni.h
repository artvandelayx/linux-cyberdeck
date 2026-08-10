#ifndef LINUX_CYBERDECK_JNI_H
#define LINUX_CYBERDECK_JNI_H

#include <jni.h>
#include "linux_supervisor.h"

#ifdef __cplusplus
extern "C" {
#endif

// JNI bridge functions called from Kotlin

// Initialize session
JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_initializeSession(
    JNIEnv* env, jclass clazz, jstring appFilesDir, jstring packageName);

// Start Linux session
JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_startLinuxSession(
    JNIEnv* env, jclass clazz);

// Stop Linux session
JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_stopLinuxSession(
    JNIEnv* env, jclass clazz);

// Restart Linux session
JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_restartLinuxSession(
    JNIEnv* env, jclass clazz);

// Get session status
JNIEXPORT jobject JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_getSessionStatus(
    JNIEnv* env, jclass clazz);

// Install rootfs
JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_installRootfs(
    JNIEnv* env, jclass clazz, jstring downloadUrl, jlong expectedSize);

// Cancel installation
JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_cancelInstallation(
    JNIEnv* env, jclass clazz);

// Mount shared storage
JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_mountSharedStorage(
    JNIEnv* env, jclass clazz, jstring androidPath, jstring linuxMountPoint);

// Unmount shared storage
JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_unmountSharedStorage(
    JNIEnv* env, jclass clazz, jstring linuxMountPoint);

// List mounted storages
JNIEXPORT jobjectArray JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_listMountedStorages(
    JNIEnv* env, jclass clazz);

// Get diagnostics
JNIEXPORT jobject JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_getDiagnostics(
    JNIEnv* env, jclass clazz);

// Get logs
JNIEXPORT jstring JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_getLogs(
    JNIEnv* env, jclass clazz, jint logType, jint maxLines);

// Set auto start
JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_setAutoStart(
    JNIEnv* env, jclass clazz, jboolean enabled);

// Get auto start
JNIEXPORT jboolean JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_getAutoStart(
    JNIEnv* env, jclass clazz);

// Set touch mode
JNIEXPORT jint JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_setTouchMode(
    JNIEnv* env, jclass clazz, jboolean enabled);

// Get touch mode
JNIEXPORT jboolean JNICALL
Java_com_linuxcyberdeck_native_LinuxNativeBridge_getTouchMode(
    JNIEnv* env, jclass clazz);

#ifdef __cplusplus
}
#endif

#endif // LINUX_CYBERDECK_JNI_H
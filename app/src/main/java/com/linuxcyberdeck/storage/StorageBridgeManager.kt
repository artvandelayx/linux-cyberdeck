package com.linuxcyberdeck.storage

import android.app.Activity
import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.database.Cursor
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.DocumentsContract
import android.provider.MediaStore
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.linuxcyberdeck.native.LinuxNativeBridge
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.io.OutputStream
import timber.log.Timber

// Make the class accessible from JNI
@Suppress("UNUSED_PARAMETER")
class StorageBridgeManager(private val context: Context) : ViewModel() {

    private val _mountStatus = MutableLiveData<MountStatus>()
    val mountStatus: LiveData<MountStatus> = _mountStatus

    private val _availableStorages = MutableLiveData<List<StorageOption>>()
    val availableStorages: LiveData<List<StorageOption>> = _availableStorages

    private val _selectedUri = MutableLiveData<Uri?>()
    val selectedUri: LiveData<Uri?> = _selectedUri

    data class MountStatus(
        val isMounted: Boolean = false,
        val mountPoint: String = "",
        val androidPath: String = "",
        val errorMessage: String? = null
    )

    data class StorageOption(
        val id: String,
        val displayName: String,
        val description: String,
        val isAvailable: Boolean,
        val uri: Uri? = null
    )

    companion object {
        const val MOUNT_POINT_SHARED = "/mnt/shared"
        const val MOUNT_POINT_SDCARD = "/mnt/sdcard"
        const val REQUEST_CODE_STORAGE = 1001
    }

    fun requestSharedStorageAccess(activity: Activity, launcher: ActivityResultLauncher<Intent>) {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
        // Suggest starting at root of shared storage
        val primaryVolume = Environment.getExternalStorageDirectory().absolutePath
        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, Uri.parse("file://$primaryVolume"))
        launcher.launch(intent)
    }

    fun requestSdCardAccess(activity: Activity, launcher: ActivityResultLauncher<Intent>) {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
        // Try to suggest SD card if available
        val secondaryVolumes = context.getExternalFilesDirs(null)
        if (secondaryVolumes.size > 1) {
            val sdCard = secondaryVolumes[1]
            if (sdCard != null) {
                val sdPath = sdCard.absolutePath
                val sdRoot = sdPath.substringBefore("/Android/data")
                intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, Uri.parse("file://$sdRoot"))
            }
        }
        launcher.launch(intent)
    }

    fun handleStorageResult(uri: Uri?) {
        uri?.let { treeUri ->
            // Take persistable permission
            takePersistablePermission(treeUri)
            _selectedUri.value = treeUri

            // Mount via native bridge
            mountSharedStorage(treeUri, MOUNT_POINT_SHARED)
        }
    }

    private fun takePersistablePermission(uri: Uri) {
        val contentResolver = context.contentResolver
        val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION

        try {
            contentResolver.takePersistableUriPermission(uri, flags)
            Timber.i("Persistable permission granted for: $uri")
        } catch (e: SecurityException) {
            Timber.w(e, "Failed to take persistable permission")
        }
    }

    fun mountSharedStorage(treeUri: Uri, linuxMountPoint: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val androidPath = treeUri.toString()

                // Create mount point directory in Linux (via native bridge)
                val result = LinuxNativeBridge.mountSharedStorage(androidPath, linuxMountPoint, this@StorageBridgeManager)

                if (result == LinuxNativeBridge.RESULT_SUCCESS) {
                    _mountStatus.postValue(MountStatus(
                        isMounted = true,
                        mountPoint = linuxMountPoint,
                        androidPath = androidPath
                    ))
                    refreshMounts()
                } else {
                    _mountStatus.postValue(MountStatus(
                        isMounted = false,
                        errorMessage = "Failed to mount storage (code: $result)"
                    ))
                }
            } catch (e: Exception) {
                Timber.e(e, "Failed to mount shared storage")
                _mountStatus.postValue(MountStatus(
                    isMounted = false,
                    errorMessage = "Exception: ${e.message}"
                ))
            }
        }
    }

    fun unmountSharedStorage(mountPoint: String) {
        viewModelScope.launch(Dispatchers.IO) {
            val result = LinuxNativeBridge.unmountSharedStorage(mountPoint, this@StorageBridgeManager)
            if (result == LinuxNativeBridge.RESULT_SUCCESS) {
                _mountStatus.postValue(MountStatus())
                refreshMounts()
            }
        }
    }

    fun refreshMounts() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val mounts = LinuxNativeBridge.listMountedStorages()
                // Convert to StorageOption list
                val options = mounts.map { path ->
                    StorageOption(
                        id = path,
                        displayName = getDisplayNameForPath(path),
                        description = "Mounted at $path",
                        isAvailable = true
                    )
                }.toList()
                _availableStorages.postValue(options)
            } catch (e: Exception) {
                Timber.e(e, "Failed to list mounted storages")
            }
        }
    }

    private fun getDisplayNameForPath(path: String): String {
        return when (path) {
            MOUNT_POINT_SHARED -> "Internal Shared Storage"
            MOUNT_POINT_SDCARD -> "SD Card"
            else -> "Storage ($path)"
        }
    }

    // ContentResolver-based file operations for FUSE bridge
    fun listFiles(uri: Uri): List<FileInfo> {
        val contentResolver = context.contentResolver
        val result = mutableListOf<FileInfo>()

        try {
            val cursor = contentResolver.query(
                uri,
                arrayOf(
                    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_MIME_TYPE,
                    DocumentsContract.Document.COLUMN_SIZE,
                    DocumentsContract.Document.COLUMN_FLAGS
                ),
                null,
                null,
                null
            )

            cursor?.use { c ->
                while (c.moveToNext()) {
                    val id = c.getString(c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DOCUMENT_ID))
                    val name = c.getString(c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DISPLAY_NAME))
                    val mimeType = c.getString(c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_MIME_TYPE))
                    val size = c.getLong(c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_SIZE))
                    val flags = c.getInt(c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_FLAGS))

                    val isDirectory = DocumentsContract.Document.MIME_TYPE_DIR == mimeType
                    val canWrite = (flags and DocumentsContract.Document.FLAG_SUPPORTS_WRITE) != 0

                    val childUri = DocumentsContract.buildDocumentUriUsingTree(uri, id)
                    result.add(FileInfo(childUri, name, isDirectory, size, canWrite))
                }
            }
        } catch (e: Exception) {
            Timber.e(e, "Failed to list files")
        }

        return result
    }

    fun readFile(uri: Uri): ByteArray? {
        return try {
            context.contentResolver.openInputStream(uri)?.readBytes()
        } catch (e: Exception) {
            Timber.e(e, "Failed to read file: $uri")
            null
        }
    }

    fun writeFile(uri: Uri, data: ByteArray): Boolean {
        return try {
            context.contentResolver.openOutputStream(uri)?.use { output ->
                output.write(data)
            }
            true
        } catch (e: Exception) {
            Timber.e(e, "Failed to write file: $uri")
            false
        }
    }

    fun createFile(uri: Uri, mimeType: String, displayName: String): Uri? {
        return try {
            val contentValues = android.content.ContentValues().apply {
                put(DocumentsContract.Document.COLUMN_MIME_TYPE, mimeType)
                put(DocumentsContract.Document.COLUMN_DISPLAY_NAME, displayName)
            }
            context.contentResolver.insert(uri, contentValues)
        } catch (e: Exception) {
            Timber.e(e, "Failed to create file")
            null
        }
    }

    fun createDirectory(uri: Uri, displayName: String): Uri? {
        return createFile(uri, DocumentsContract.Document.MIME_TYPE_DIR, displayName)
    }

    fun deleteFile(uri: Uri): Boolean {
        return try {
            context.contentResolver.delete(uri, null, null) > 0
        } catch (e: Exception) {
            Timber.e(e, "Failed to delete file: $uri")
            false
        }
    }

    data class FileInfo(
        val uri: Uri,
        val name: String,
        val isDirectory: Boolean,
        val size: Long,
        val canWrite: Boolean
    )
}

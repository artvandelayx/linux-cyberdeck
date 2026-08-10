package com.linuxcyberdeck.installer

import android.content.Context
import android.content.res.AssetManager
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.linuxcyberdeck.LinuxCyberdeckApplication
import com.linuxcyberdeck.session.LinuxSessionState
import com.linuxcyberdeck.session.LinuxSessionStatus
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.util.zip.GZIPInputStream
import timber.log.Timber

class LinuxInstaller(private val context: Context) : ViewModel() {

    private val _installStatus = MutableLiveData<InstallStatus>()
    val installStatus: LiveData<InstallStatus> = _installStatus

    data class InstallStatus(
        val state: LinuxSessionState = LinuxSessionState.NOT_INSTALLED,
        val progress: Float = 0f,
        val currentOperation: String = "",
        val errorMessage: String? = null,
        val bytesProcessed: Long = 0,
        val totalBytes: Long = 0
    )

    // Asset file names
    private val ROOTFS_TARBALL = "rootfs-arm64.tar.gz"
    private val PROOT_BINARY = "proot"
    private val START_SCRIPT = "start_linux.sh"

    fun installRootfs() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                _installStatus.postValue(InstallStatus(
                    state = LinuxSessionState.INSTALLING,
                    progress = 0f,
                    currentOperation = "Preparing installation..."
                ))

                val filesDir = context.filesDir
                val linuxDir = File(filesDir, "linux")
                val rootfsDir = File(linuxDir, "rootfs")
                val assets = context.assets

                // Check if rootfs already exists and is valid
                if (rootfsDir.exists() && isRootfsValid(rootfsDir)) {
                    Timber.i("Rootfs already installed and valid")
                    _installStatus.postValue(InstallStatus(
                        state = LinuxSessionState.INSTALLED,
                        progress = 1f,
                        currentOperation = "Already installed"
                    ))
                    return@launch
                }

                // Extract PRoot binary first
                _installStatus.postValue(InstallStatus(
                    state = LinuxSessionState.INSTALLING,
                    progress = 0.05f,
                    currentOperation = "Extracting PRoot..."
                ))
                extractAsset(PROOT_BINARY, File(filesDir, PROOT_BINARY), true)

                // Extract start script
                _installStatus.postValue(InstallStatus(
                    state = LinuxSessionState.INSTALLING,
                    progress = 0.1f,
                    currentOperation = "Extracting startup script..."
                ))
                extractAsset(START_SCRIPT, File(filesDir, START_SCRIPT), true)

                // Extract rootfs tarball
                _installStatus.postValue(InstallStatus(
                    state = LinuxSessionState.INSTALLING,
                    progress = 0.15f,
                    currentOperation = "Extracting rootfs (this may take several minutes)..."
                ))

                val tarballFile = File(filesDir, ROOTFS_TARBALL)
                
                // Copy tarball from assets to files dir (streaming to avoid memory issues)
                copyAssetToFile(assets, ROOTFS_TARBALL, tarballFile) { bytesCopied, totalBytes ->
                    _installStatus.postValue(InstallStatus(
                        state = LinuxSessionState.INSTALLING,
                        progress = 0.15f + (0.7f * bytesCopied / totalBytes.toFloat()),
                        currentOperation = "Copying rootfs... ${formatBytes(bytesCopied)}/${formatBytes(totalBytes)}",
                        bytesProcessed = bytesCopied,
                        totalBytes = totalBytes
                    ))
                }

                // Extract tarball
                _installStatus.postValue(InstallStatus(
                    state = LinuxSessionState.INSTALLING,
                    progress = 0.85f,
                    currentOperation = "Extracting rootfs archive..."
                ))
                extractTarGz(tarballFile, rootfsDir) { filesExtracted, totalFiles ->
                    _installStatus.postValue(InstallStatus(
                        state = LinuxSessionState.INSTALLING,
                        progress = 0.85f + (0.1f * filesExtracted / totalFiles.toFloat()),
                        currentOperation = "Extracting files... $filesExtracted/$totalFiles"
                    ))
                }

                // Clean up tarball
                tarballFile.delete()

                // Verify installation
                _installStatus.postValue(InstallStatus(
                    state = LinuxSessionState.INSTALLING,
                    progress = 0.95f,
                    currentOperation = "Verifying installation..."
                ))

                if (!isRootfsValid(rootfsDir)) {
                    throw IOException("Rootfs verification failed")
                }

                _installStatus.postValue(InstallStatus(
                    state = LinuxSessionState.INSTALLED,
                    progress = 1f,
                    currentOperation = "Installation complete"
                ))

                Timber.i("Rootfs installation complete")

            } catch (e: Exception) {
                Timber.e(e, "Installation failed")
                _installStatus.postValue(InstallStatus(
                    state = LinuxSessionState.ERROR,
                    progress = 0f,
                    currentOperation = "",
                    errorMessage = "Installation failed: ${e.message}"
                ))
            }
        }
    }

    fun cancelInstallation() {
        // Note: Cancellation would require more complex implementation
        // For now, just reset status
        _installStatus.postValue(InstallStatus())
    }

    private fun extractAsset(assetName: String, targetFile: File, makeExecutable: Boolean) {
        val assets = context.assets
        assets.open(assetName).use { input ->
            FileOutputStream(targetFile).use { output ->
                input.copyTo(output)
            }
        }
        if (makeExecutable) {
            targetFile.setExecutable(true, false)
        }
    }

    private fun copyAssetToFile(
        assets: AssetManager,
        assetName: String,
        targetFile: File,
        progressCallback: (Long, Long) -> Unit
    ) {
        val input = assets.open(assetName)
        val totalBytes = input.available().toLong()
        var bytesCopied = 0L
        val buffer = ByteArray(8192)

        try {
            FileOutputStream(targetFile).use { output ->
                while (true) {
                    val read = input.read(buffer)
                    if (read == -1) break
                    output.write(buffer, 0, read)
                    bytesCopied += read
                    if (totalBytes > 0) {
                        progressCallback(bytesCopied, totalBytes)
                    }
                }
            }
        } finally {
            input.close()
        }
    }

    private fun extractTarGz(
        tarballFile: File,
        targetDir: File,
        progressCallback: (Int, Int) -> Unit
    ) {
        targetDir.mkdirs()

        GZIPInputStream(tarballFile.inputStream()).use { gzis ->
            // Simple tar extraction (GNU tar format)
            val buffer = ByteArray(512)
            var filesExtracted = 0
            var totalFiles = estimateTarEntries(gzis)
            
            // Reset stream for actual extraction
            gzis.close()
            GZIPInputStream(tarballFile.inputStream()).use { gzis2 ->
                while (true) {
                    val header = ByteArray(512)
                    val read = gzis2.read(header)
                    if (read != 512) break
                    
                    val name = String(header, 0, 100).trim { it <= ' ' }
                    if (name.isEmpty()) break
                    
                    val sizeStr = String(header, 124, 12).trim()
                    val size = sizeStr.toLongOrNull() ?: 0L
                    val typeFlag = header[156].toChar()
                    
                    val targetFile = File(targetDir, name)
                    
                    when (typeFlag) {
                        '0', '\0' -> { // Regular file
                            targetFile.parentFile.mkdirs()
                            FileOutputStream(targetFile).use { out ->
                                var remaining = size
                                val buf = ByteArray(8192)
                                while (remaining > 0) {
                                    val toRead = minOf(buf.size, remaining.toInt())
                                    val read = gzis2.read(buf, 0, toRead)
                                    if (read <= 0) break
                                    out.write(buf, 0, read)
                                    remaining -= read
                                }
                            }
                            filesExtracted++
                            if (totalFiles > 0) progressCallback(filesExtracted, totalFiles)
                        }
                        '5' -> { // Directory
                            targetFile.mkdirs()
                        }
                        '2' -> { // Symlink
                            val linkName = String(header, 157, 100).trim { it <= ' ' }
                            targetFile.parentFile.mkdirs()
                            try {
                                // Note: symlink creation may need special handling on Android
                            } catch (e: Exception) {
                                Timber.w(e, "Failed to create symlink: $name -> $linkName")
                            }
                        }
                        else -> {
                            // Skip unknown types, but consume data
                            var remaining = size
                            val skipBuf = ByteArray(8192)
                            while (remaining > 0) {
                                val toRead = minOf(skipBuf.size, remaining.toInt())
                                val read = gzis2.read(skipBuf, 0, toRead)
                                if (read <= 0) break
                                remaining -= read
                            }
                        }
                    }
                    
                    // Pad to 512-byte boundary
                    val padding = (512 - (size % 512)) % 512
                    if (padding > 0) {
                        gzis2.skip(padding.toLong())
                    }
                }
            }
        }
    }

    private fun estimateTarEntries(gzis: GZIPInputStream): Int {
        // Quick pass to count entries (returns 0 if not feasible)
        return 0 // Skip estimation for now
    }

    private fun isRootfsValid(rootfsDir: File): Boolean {
        // Check for essential directories and files
        val essentialPaths = listOf(
            "bin/bash",
            "etc/passwd",
            "etc/debian_version",
            "usr/bin/xfce4-session",
            "usr/bin/firefox-esr"
        )
        
        return essentialPaths.all { rootfsDir.resolve(it).exists() }
    }

    private fun formatBytes(bytes: Long): String {
        return when {
            bytes >= 1024L * 1024 * 1024 -> String.format("%.1f GB", bytes / (1024.0 * 1024 * 1024))
            bytes >= 1024L * 1024 -> String.format("%.1f MB", bytes / (1024.0 * 1024))
            bytes >= 1024L -> String.format("%.1f KB", bytes / 1024.0)
            else -> "$bytes B"
        }
    }
}
package com.linuxcyberdeck.installer

import android.content.Context
import android.content.res.AssetManager
import android.system.Os
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.linuxcyberdeck.LinuxCyberdeckApplication
import com.linuxcyberdeck.session.LinuxSessionState
import com.linuxcyberdeck.session.LinuxSessionStatus
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.util.zip.GZIPInputStream
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream
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
    private val START_SCRIPT = "start_linux.sh"
    private var installJob: Job? = null

    fun installRootfs() {
        installJob = viewModelScope.launch(Dispatchers.IO) {
            try {
                _installStatus.postValue(InstallStatus(
                    state = LinuxSessionState.INSTALLING,
                    progress = 0f,
                    currentOperation = "Preparing installation..."
                ))

                val filesDir = context.filesDir
                val linuxDir = File(filesDir, "linux")
                val rootfsDir = File(linuxDir, "rootfs")
                val stagingDir = File(linuxDir, "rootfs.installing")
                val assets = context.assets
                val requiredFreeBytes = 6L * 1024 * 1024 * 1024
                if (filesDir.usableSpace < requiredFreeBytes) {
                    throw IOException(
                        "Insufficient storage: ${formatBytes(filesDir.usableSpace)} free; " +
                            "${formatBytes(requiredFreeBytes)} required"
                    )
                }

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
                stagingDir.deleteRecursively()
                extractTarGz(tarballFile, stagingDir) { filesExtracted, totalFiles ->
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

                if (!isRootfsValid(stagingDir)) {
                    throw IOException("Rootfs verification failed")
                }
                rootfsDir.deleteRecursively()
                if (!stagingDir.renameTo(rootfsDir)) {
                    throw IOException("Could not activate the installed rootfs")
                }

                _installStatus.postValue(InstallStatus(
                    state = LinuxSessionState.INSTALLED,
                    progress = 1f,
                    currentOperation = "Installation complete"
                ))

                Timber.i("Rootfs installation complete")

            } catch (_: CancellationException) {
                _installStatus.postValue(InstallStatus())
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
        installJob?.cancel()
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
        val targetRoot = targetDir.canonicalFile.apply { mkdirs() }
        var extracted = 0
        GZIPInputStream(tarballFile.inputStream().buffered()).use { gzip ->
            TarArchiveInputStream(gzip).use { tar ->
                while (true) {
                    val entry = tar.nextTarEntry ?: break
                    val relativeName = entry.name.removePrefix("rootfs-arm64/").removePrefix("./")
                    if (relativeName.isEmpty()) continue
                    val target = File(targetRoot, relativeName).canonicalFile
                    if (!target.path.startsWith(targetRoot.path + File.separator)) {
                        throw IOException("Unsafe path in rootfs archive: ${entry.name}")
                    }
                    when {
                        entry.isDirectory -> target.mkdirs()
                        entry.isSymbolicLink -> {
                            target.parentFile?.mkdirs()
                            if (target.exists()) target.delete()
                            Os.symlink(entry.linkName, target.path)
                        }
                        entry.isLink -> {
                            val linkName = entry.linkName.removePrefix("rootfs-arm64/").removePrefix("./")
                            val source = File(targetRoot, linkName).canonicalFile
                            if (!source.path.startsWith(targetRoot.path + File.separator)) {
                                throw IOException("Unsafe hard link in rootfs archive: ${entry.name}")
                            }
                            target.parentFile?.mkdirs()
                            Os.link(source.path, target.path)
                        }
                        entry.isFile -> {
                            target.parentFile?.mkdirs()
                            FileOutputStream(target).use { output -> tar.copyTo(output, 64 * 1024) }
                        }
                    }
                    if (!entry.isSymbolicLink && (entry.isFile || entry.isDirectory)) {
                        Os.chmod(target.path, entry.mode)
                    }
                    extracted++
                    if (extracted % 250 == 0) progressCallback(extracted, extracted + 1)
                }
            }
        }
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

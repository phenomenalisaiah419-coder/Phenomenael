package com.example.bedrockai

import android.content.Context
import android.net.Uri
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.util.zip.ZipInputStream

/**
 * Imports the minimum secret material needed for the authenticated Bedrock
 * connection. The directory is app-private; do not import these files to
 * external/shared storage. The archive may contain a parent directory, but
 * archive paths never become output paths.
 */
object BedrockAiSecureLogin {
    private val limits = mapOf(
        "login_payload.bin" to 1024 * 1024,
        "ecdsa_p384.pem" to 32 * 1024,
        "profile.json" to 64 * 1024
    )

    @Throws(IOException::class)
    fun importCacheArchive(context: Context, archiveUri: Uri): File {
        val root = File(context.noBackupFilesDir, "bedrock_ai")
        val staging = File(context.noBackupFilesDir, "bedrock_ai_import_staging")
        val backup = File(context.noBackupFilesDir, "bedrock_ai_import_backup")
        deleteRecursively(staging)
        deleteRecursively(backup)
        if (!staging.mkdirs()) throw IOException("Cannot create private cache staging directory")
        staging.setReadable(true, true)
        staging.setWritable(true, true)
        // Directories require the owner execute/search bit to create and read children.
        staging.setExecutable(true, true)

        val seen = mutableSetOf<String>()
        try {
            val input = context.contentResolver.openInputStream(archiveUri)
                ?: throw IOException("Cannot open selected login-cache archive")
            input.use { raw ->
                ZipInputStream(raw).use { zip ->
                    while (true) {
                        val entry = zip.nextEntry ?: break
                        try {
                            if (entry.isDirectory) continue
                            val archiveName = entry.name.replace('\\', '/')
                            if (archiveName.startsWith('/') || archiveName.split('/').any { it == ".." || it == "." }) {
                                throw IOException("Unsafe path in login-cache archive")
                            }
                            val basename = archiveName.substringAfterLast('/')
                            val cap = limits[basename] ?: continue // Ignore refresh/device tokens; native login does not need them.
                            if (!seen.add(basename)) throw IOException("Duplicate $basename in login-cache archive")
                            val output = File(staging, basename)
                            var total = 0
                            FileOutputStream(output).use { fileOut ->
                                val buffer = ByteArray(8192)
                                while (true) {
                                    val count = zip.read(buffer)
                                    if (count < 0) break
                                    total += count
                                    if (total > cap) throw IOException("$basename exceeds its size limit")
                                    fileOut.write(buffer, 0, count)
                                }
                            }
                            output.setReadable(true, true)
                            output.setWritable(true, true)
                            output.setExecutable(false, false)
                            if (total == 0) throw IOException("$basename is empty")
                        } finally {
                            zip.closeEntry()
                        }
                    }
                }
            }
            if (!seen.containsAll(limits.keys)) {
                throw IOException("Archive must contain login_payload.bin, ecdsa_p384.pem and profile.json")
            }
            val hadPrevious = root.exists()
            if (hadPrevious && !root.renameTo(backup)) {
                throw IOException("Cannot stage the previous private login cache for replacement")
            }
            if (!staging.renameTo(root)) {
                if (hadPrevious) backup.renameTo(root)
                throw IOException("Cannot finalize private login cache")
            }
            root.setReadable(true, true)
            root.setWritable(true, true)
            // Keep the directory traversable by this app's UID; child key files stay non-executable.
            root.setExecutable(true, true)
            deleteRecursively(backup)
            return root
        } catch (failure: Exception) {
            deleteRecursively(staging)
            if (backup.exists() && !root.exists()) backup.renameTo(root)
            if (failure is IOException) throw failure
            throw IOException("Login-cache import failed", failure)
        }
    }

    fun connect(
        handle: Long,
        cryptoProviderHandle: Long,
        context: Context,
        host: String,
        port: Int
    ): Boolean {
        val cache = File(context.noBackupFilesDir, "bedrock_ai")
        if (!cache.isDirectory) return false
        return BedrockAiNative.nativeConnectSecureRakNet(
            handle, host, port, cache.absolutePath, cryptoProviderHandle
        )
    }

    fun lastError(handle: Long): String = BedrockAiNative.nativeLastError(handle)

    private fun deleteRecursively(file: File): Boolean {
        if (!file.exists()) return true
        if (file.isDirectory) {
            val children = file.listFiles() ?: return false
            for (child in children) if (!deleteRecursively(child)) return false
        }
        return file.delete()
    }
}

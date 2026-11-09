package com.example.bedrockai

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/** Bridges Android network callbacks to the native transport recovery coordinator. */
class BedrockAiNetworkMonitor(
    context: Context,
    private val nativeHandle: Long
) {
    private val connectivity = context.getSystemService(ConnectivityManager::class.java)
        ?: error("ConnectivityManager is unavailable")
    private val generation = AtomicLong(0L)
    private val currentNetwork = AtomicReference<Network?>(null)
    @Volatile private var registered = false

    private val callback = object : ConnectivityManager.NetworkCallback() {
        override fun onAvailable(network: Network) {
            currentNetwork.set(network)
            val next = generation.incrementAndGet()
            BedrockAiNative.nativeNetworkAvailable(nativeHandle, next)
        }

        override fun onLost(network: Network) {
            if (currentNetwork.compareAndSet(network, null)) {
                BedrockAiNative.nativeNetworkLost(nativeHandle)
            }
        }
    }

    fun start() {
        if (registered) return
        connectivity.registerDefaultNetworkCallback(callback)
        registered = true
    }

    fun stop() {
        if (!registered) return
        connectivity.unregisterNetworkCallback(callback)
        registered = false
        currentNetwork.set(null)
    }
}

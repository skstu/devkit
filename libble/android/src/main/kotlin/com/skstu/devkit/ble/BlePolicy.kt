package com.skstu.devkit.ble

/** Ephemeral hints select a retry address, never revoke a live route. */
internal class NearbyBleHints {
    private val peers = mutableMapOf<List<Byte>, String>()
    fun clear() = peers.clear()
    fun observe(hint: ByteArray, peer: String, isProbe: Boolean = false, isReady: (String) -> Boolean): String? {
        if (hint.size != 8 || isProbe) return null
        val key = hint.toList()
        val old = peers[key]
        if (old != null && old != peer && isReady(old)) return null
        if (old == null && peers.size >= 64) return null
        peers[key] = peer
        return old?.takeIf { it != peer }
    }
}

/** Cancellation also invalidates an already-queued callback. */
internal class NearbyBleDeadline(
    private val post: (Runnable, Long) -> Unit,
    private val remove: (Runnable) -> Unit,
) {
    private var pending: Runnable? = null
    fun cancel() { pending?.let(remove); pending = null }
    fun arm(delayMs: Long, expired: () -> Unit) {
        cancel()
        val task = object : Runnable {
            override fun run() {
                if (pending !== this) return
                pending = null
                expired()
            }
        }
        pending = task
        post(task, delayMs)
    }
}

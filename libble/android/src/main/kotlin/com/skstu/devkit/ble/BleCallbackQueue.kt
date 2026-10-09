package com.skstu.devkit.ble

// Admission happens on Binder/native callback threads, before Handler posting.
internal class BleCallbackQueue {
    private val gate = Any()
    private var count = 0
    private var bytes = 0
    private var failed = false
    fun reset() = synchronized(gate) { failed = false }
    fun submit(size: Int, enqueue: (Runnable) -> Boolean, action: () -> Unit): Boolean {
        synchronized(gate) {
            if (failed || size < 0 || size > 262144 || count >= 256 || bytes > 262144 - size) {
                failed = true
                return false
            }
            count++; bytes += size
        }
        val task = Runnable { try { action() } finally { release(size) } }
        val accepted = try { enqueue(task) } catch (_: Exception) { false }
        if (!accepted) {
            release(size)
            synchronized(gate) { failed = true }
        }
        return accepted
    }
    private fun release(size: Int) = synchronized(gate) { count--; bytes -= size }
}

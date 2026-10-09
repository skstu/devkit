package com.skstu.devkit.ble
fun main() {
    val queue = BleCallbackQueue()
    val tasks = java.util.concurrent.ConcurrentLinkedQueue<Runnable>()
    var delivered = 0
    val enqueue: (Runnable) -> Boolean = { tasks.add(it); true }
    repeat(256) { check(queue.submit(1, enqueue) { delivered++ }) }
    check(!queue.submit(1, enqueue) { error("overflow was admitted") })
    tasks.remove().run()
    check(!queue.submit(1, enqueue) {}) // failure remains sticky until stop/restart
    queue.reset()
    check(queue.submit(1, enqueue) { delivered++ })
    while(tasks.isNotEmpty())tasks.remove().run()
    check(delivered == 257)
    queue.reset()
    repeat(4) { check(queue.submit(65536, enqueue) {}) }
    check(!queue.submit(1, enqueue) {})
    // Reset must not forget closures still queued on the main Handler.
    queue.reset()
    check(!queue.submit(1, enqueue) {})
    while(tasks.isNotEmpty())tasks.remove().run()
    queue.reset()
    check(!queue.submit(1, { false }) {})
    queue.reset()
    check(queue.submit(1, enqueue) {})
    tasks.remove().run()
    val threads = List(8) { Thread { repeat(256) { queue.submit(512, enqueue) {} } } }
    threads.forEach { it.start() }; threads.forEach { it.join() }
    check(tasks.size == 256)
    while(tasks.isNotEmpty())tasks.remove().run()
    println("actual Android callback queue: count/byte saturation, sticky failure, restart accounting and failed Handler admission passed")
}

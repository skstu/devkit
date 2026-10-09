package com.skstu.devkit.ble

import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.os.SystemClock
import java.util.UUID
import java.security.SecureRandom

/** Generic GATT byte transport. The consumer owns permissions and authentication. */
@Suppress("DEPRECATION")
class BleEngine(private val context: Context, private var handle: Long,
    service: String, receive: String, notify: String, private val useHints: Boolean) {
    private val serviceId = UUID.fromString(service)
    private val rxId = UUID.fromString(receive)
    private val txId = UUID.fromString(notify)
    private val cccdId = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    private var readInterval=50L
    fun setReadInterval(interval: Int): Int = onMain {if(mode!="off") -2 else {readInterval=interval.toLong();0}}
    private var abiGeneration = 0L
    private var connectTimeout=20000L
    private var sendTimeout=25000L
    private var retryDelay=3000L
    private var candidateTtl=30000L
    private var maximumLinks=4
    private var maximumCandidates=64
    fun setOptions(values: IntArray): Int = onMain {
        if(values.size!=6 || mode!="off") return@onMain -2
        connectTimeout=values[0].toLong();sendTimeout=values[1].toLong();retryDelay=values[2].toLong();candidateTtl=values[3].toLong()
        maximumLinks=values[4];maximumCandidates=values[5];0
    }
    private class Reply(private var completion: ((String) -> Unit)?) {
        fun success(value: Any?) { val fn = completion; completion = null; fn?.invoke("") }
        fun error(code: String, message: String, details: Any?) { val fn=completion; completion=null; fn?.invoke(code) }
    }
    private class Call(val method: String, private val args: Map<String, Any>) {
        @Suppress("UNCHECKED_CAST") fun <T> argument(key: String): T? = args[key] as? T
    }
    private class Link(val device: BluetoothDevice, val initiator: Boolean) {
        val id = UUID.randomUUID().toString()
        var gatt: BluetoothGatt? = null
        var write: BluetoothGattCharacteristic? = null
        var notify: BluetoothGattCharacteristic? = null
        val early=java.util.ArrayDeque<ByteArray>()
        var earlyBytes=0
        var readDue=false
        var reading=false
        var polling=false
        var helloWriting=false
        val session=ByteArray(8).also { java.security.SecureRandom().nextBytes(it) }
        var readTask: Runnable?=null
        var ready = false
        var mtu = 20
        var pending = ByteArray(0)
        var offset = 0
        var inFlight = 0
        var completion: Reply? = null
        var deadline: Runnable? = null
        var subscriptionDeadline: NearbyBleDeadline? = null
        var probe = ""
    }
    private val main = Handler(Looper.getMainLooper())
    private val callbackQueue = BleCallbackQueue()
    private val callbackOverflow = java.util.concurrent.atomic.AtomicBoolean(false)
    private val manager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val adapter get() = manager.adapter
    private var mode = "off"
    private var generation = 0
    private var server: BluetoothGattServer? = null
    private var tx: BluetoothGattCharacteristic? = null
    private var scanner: ScanCallback? = null
    private var advertiser: AdvertiseCallback? = null
    private val seen=mutableMapOf<String,Long>()
    private var expiryTask: Runnable?=null
    private val peers = linkedMapOf<String, BluetoothDevice>()
    // Android may rotate its private address when advertising restarts. This
    // process-only hint allows rediscovery; it is never a trusted identity.
    private val advertisementHint = ByteArray(8).also { SecureRandom().nextBytes(it) }
    private val hintedPeers = NearbyBleHints()
    private val links = linkedMapOf<String, Link>()
    private val wanted = mutableSetOf<String>()
    private val retry = mutableMapOf<String, NearbyBleDeadline>()
    private val closing = mutableMapOf<BluetoothGatt, NearbyBleDeadline>()
    private var serverSending: String? = null

    private external fun nativeEvent(handle: Long, generation: Long, type: Int, request: Long, status: Int,
        link: String, peer: String, probe: String, detail: String, data: ByteArray, rssi: Int, initiator: Boolean)
    private fun emit(value: Map<String, Any>) {
        if(handle == 0L) return
        val types=mapOf("state" to 1,"candidate" to 2,"candidateGone" to 3,"link" to 4,"disconnected" to 5,
            "data" to 6,"probeEnded" to 7,"diagnostic" to 10)
        val type=types[value["type"]] ?: return
        nativeEvent(handle,abiGeneration,type,0L,0,value["link"] as? String ?: "",value["peer"] as? String ?: "",
            value["probe"] as? String ?: value["token"] as? String ?: "",value["state"] as? String ?: value["event"] as? String ?: "",
            value["data"] as? ByteArray ?: ByteArray(0),value["rssi"] as? Int ?: 0,value["initiator"] as? Boolean ?: false)
    }
    private fun onMain(action: () -> Int): Int {
        if(Looper.myLooper() == Looper.getMainLooper()) return action()
        val latch=java.util.concurrent.CountDownLatch(1)
        val pending=java.util.concurrent.atomic.AtomicBoolean(true)
        var result=-4
        main.post { if(pending.compareAndSet(true,false)) { try { result=action() } finally { latch.countDown() } } }
        if(!latch.await(5,java.util.concurrent.TimeUnit.SECONDS)) { pending.set(false); return -4 }
        return result
    }
    fun command(op: Int, epoch: Long, value: String, extra: String, bytes: ByteArray, request: Long): Int = onMain {
        abiGeneration=epoch
        val methods=arrayOf("start","start","stop","connect","probe","cancelProbe","adoptProbe","disconnect","resetLink","send","recover")
        if(op !in methods.indices) return@onMain -1
        val args=mutableMapOf<String,Any>()
        when(op){
            0 -> args["mode"]="scan"
            1 -> args["mode"]="advertise"
            3 -> args["peer"]=value
            4 -> {args["peer"]=value;args["token"]=extra}
            5 -> args["token"]=value
            6 -> {args["token"]=value;args["oldPeer"]=extra}
            7,8 -> args["link"]=value
            9 -> {args["link"]=value;args["data"]=bytes.copyOf()}
        }
        var submitting=true; var status=0
        val reply=Reply { error ->
            val code=if(error.isEmpty())0 else if(error=="invalid_operation")-1 else if(error=="unsupported")-7
                else if(error=="peer_unavailable"||error=="link_unavailable")-2 else -4
            if(submitting && code!=0)status=code
            else if(handle!=0L && abiGeneration==epoch && (op==9 || code!=0))
                nativeEvent(handle,epoch,if(op==9)8 else 9,request,code,value,"","",error,ByteArray(0),0,false)
        }
        onCall(Call(methods[op],args),reply);submitting=false;status
    }
    fun close(): Int = onMain {handle=0L;stop();0}
    private fun state(value: String) = emit(mapOf("type" to "state", "state" to value))
    private fun discoveryFailure(stage: String, code: Int) {
        // Retain the platform reason locally, without names, addresses or payloads.
        emit(mapOf("type" to "diagnostic", "event" to "native-discovery-failed",
            "stage" to stage, "code" to code))
        state("unavailable")
    }
    private fun post(epoch: Int, size: Int = 0, action: () -> Unit) {
        val accepted = callbackQueue.submit(size, { main.post(it) }) {
            if (epoch == generation && mode != "off") {
                try {action()} catch(_: SecurityException){stop();state("permission_denied")}
            }
        }
        if (!accepted && callbackOverflow.compareAndSet(false,true)) {
            // Reserved control turn: a full data queue cannot starve shutdown.
            main.postAtFrontOfQueue {
                try {
                    if (epoch == generation && mode != "off") {
                        val abiEpoch = abiGeneration
                        stop()
                        if(handle!=0L) nativeEvent(handle,abiEpoch,9,0,-6,"","","",
                            "native-callback-overflow",ByteArray(0),0,false)
                    }
                } finally { callbackOverflow.set(false) }
            }
        }
    }
    private fun onCall(call: Call, result: Reply) {
        try {
            when (call.method) {
                "start" -> {
                    val selected = call.argument<String>("mode")
                    require(selected == "scan" || selected == "advertise")
                    stop()
                    if (adapter?.isEnabled != true) { result.error("bluetooth_off", "bluetooth_off", null); return }
                    mode = selected!!
                    if (mode == "scan") startScan() else startAdvertising()
                    result.success(null)
                }
                "stop" -> { stop(); result.success(null) }
                "connect" -> {
                    val peer = call.argument<String>("peer") ?: ""
                    require(mode == "scan" && peers.containsKey(peer) && (wanted.size < maximumLinks || peer in wanted))
                    wanted.add(peer); connect(peer); result.success(null)
                }
                "probe" -> {
                    val peer = call.argument<String>("peer") ?: ""
                    val token = call.argument<String>("token") ?: ""
                    require(mode == "scan" && peer in peers && peer !in wanted &&
                        token.length in 1..64 && links.size + closing.size < maximumLinks &&
                        links.values.none { it.device.address == peer || it.probe.isNotEmpty() })
                    connect(peer, token); result.success(null)
                }
                "cancelProbe", "adoptProbe" -> {
                    val token = call.argument<String>("token") ?: ""
                    val link = links.values.firstOrNull { token.isNotEmpty() && it.probe == token }
                    if (call.method == "cancelProbe") {
                        if (link != null) drop(link)
                    } else {
                        require(link != null && link.ready)
                        val old = call.argument<String>("oldPeer") ?: ""
                        require(links.values.none { it !== link && it.device.address == old && it.ready })
                        require(wanted.size < maximumLinks || old in wanted || link.device.address in wanted)
                        wanted.remove(old); retry.remove(old)?.cancel()
                        links.values.filter { it !== link && it.device.address == old }.toList().forEach { drop(it) }
                        wanted.add(link.device.address); link.probe = ""
                    }
                    result.success(null)
                }
                "disconnect", "resetLink" -> {
                    links[call.argument<String>("link")]?.let {
                        if (call.method == "disconnect") {
                            wanted.remove(it.device.address); retry.remove(it.device.address)?.cancel()
                        }
                        drop(it)
                        if (call.method == "resetLink") schedule(it.device.address)
                    }
                    result.success(null)
                }
                "recover" -> {
                    if (mode == "scan" && adapter?.isEnabled == true) {
                        scanner?.let { adapter.bluetoothLeScanner.stopScan(it) }; startScan()
                        wanted.toList().forEach { connect(it) }
                    }
                    result.success(null)
                }
                "send" -> {
                    val link = links[call.argument<String>("link")]
                    val data = call.argument<ByteArray>("data")
                    if (link == null || !link.ready || link.completion != null || data == null || data.size !in 1..65560) {
                        result.error("link_unavailable", "link_unavailable", null); return
                    }
                    link.pending = data; link.offset = 0; link.completion = result
                    link.deadline = Runnable { if (links[link.id] === link && link.completion != null) {
                        emit(mapOf("type" to "diagnostic", "event" to "send-timeout offset=${link.offset} size=${link.pending.size} flight=${link.inFlight} reading=${link.reading}"))
                        drop(link); schedule(link.device.address)
                    } }
                    main.postDelayed(link.deadline!!, sendTimeout)
                    drain(link)
                }
                else -> result.error("invalid_operation","invalid_operation",null)
            }
        } catch (_: SecurityException) {
            stop(); result.error("permission_denied", "permission_denied", null)
        } catch (_: Exception) {
            if (call.method == "start") stop()
            result.error("unavailable", "unavailable", null)
        }
    }
    private fun stop() {
        mode = "off"; generation++; callbackQueue.reset()
        wanted.clear(); retry.values.forEach { it.cancel() }; retry.clear()
        runCatching { scanner?.let { adapter?.bluetoothLeScanner?.stopScan(it) } }; scanner = null
        runCatching { advertiser?.let { adapter?.bluetoothLeAdvertiser?.stopAdvertising(it) } }; advertiser = null
        links.values.toList().forEach { drop(it) }
        closing.keys.toList().forEach { finishClose(it) }
        runCatching { server?.close() }; server = null; tx = null; serverSending = null; peers.clear();seen.clear();expiryTask?.let {main.removeCallbacks(it)};expiryTask=null; hintedPeers.clear()
        state("off")
    }
    private fun expire(epoch: Int, delay: Long=readInterval){
        val task=Runnable {
            if(epoch==generation && mode=="scan"){
                val now=SystemClock.elapsedRealtime()
                val expired=seen.filter { (peer,time)->now-time>candidateTtl && peer !in wanted && links.values.none {it.device.address==peer}}.keys.toList()
                for(peer in expired){seen.remove(peer);peers.remove(peer);emit(mapOf("type" to "candidateGone","peer" to peer))}
                expire(epoch)
            }
        }
        expiryTask=task;main.postDelayed(task,minOf(1000L,candidateTtl))
    }
    private fun startScan() {
        val epoch = generation
        expire(epoch)
        val callback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) = post(epoch) {
                val peer = result.device.address
                val hint = result.scanRecord?.getServiceData(ParcelUuid(serviceId))
                if (hint?.size == 8) {
                    val probing = links.values.any { it.device.address == peer && it.probe.isNotEmpty() }
                    val old = if(useHints) hintedPeers.observe(hint, peer, isProbe = probing) { previous ->
                        links.values.any { it.device.address == previous && it.ready }
                    }
                    else null
                    if (old != null) {
                        val reconnect = wanted.remove(old)
                        retry.remove(old)?.cancel()
                        links.values.filter { it.device.address == old }.toList().forEach { drop(it) }
                        peers.remove(old)
                        emit(mapOf("type" to "candidateGone", "peer" to old))
                        if (reconnect) wanted.add(peer)
                    }
                }
                if (peers.size >= maximumCandidates && peer !in peers) return@post
                peers[peer] = result.device;seen[peer]=SystemClock.elapsedRealtime()
                emit(mapOf("type" to "candidate", "peer" to peer, "rssi" to result.rssi))
                if (peer in wanted) connect(peer)
            }
            override fun onScanFailed(errorCode: Int) = post(epoch) { discoveryFailure("scan", errorCode) }
        }
        scanner = callback
        adapter.bluetoothLeScanner.startScan(listOf(ScanFilter.Builder().setServiceUuid(ParcelUuid(serviceId)).build()),
            ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), callback)
        state("scanning")
    }
    private fun startAdvertising() {
        if(!adapter.isMultipleAdvertisementSupported || adapter.bluetoothLeAdvertiser == null) { state("unsupported");return }
        val epoch = generation
        val service = BluetoothGattService(serviceId, BluetoothGattService.SERVICE_TYPE_PRIMARY)
        service.addCharacteristic(BluetoothGattCharacteristic(rxId, BluetoothGattCharacteristic.PROPERTY_WRITE,
            BluetoothGattCharacteristic.PERMISSION_WRITE))
        tx = BluetoothGattCharacteristic(txId, BluetoothGattCharacteristic.PROPERTY_NOTIFY, 0).also {
            it.addDescriptor(BluetoothGattDescriptor(cccdId, BluetoothGattDescriptor.PERMISSION_READ or BluetoothGattDescriptor.PERMISSION_WRITE))
            service.addCharacteristic(it)
        }
        server = manager.openGattServer(context, serverCallbacks(epoch))
        check(server?.addService(service) == true)
    }
    private fun advertise(epoch: Int) {
        val callback = object : AdvertiseCallback() {
            override fun onStartSuccess(settingsInEffect: AdvertiseSettings?) = post(epoch) { state("advertising") }
            override fun onStartFailure(errorCode: Int) = post(epoch) { discoveryFailure("advertise", errorCode) }
        }
        advertiser = callback
        adapter.bluetoothLeAdvertiser.startAdvertising(
            AdvertiseSettings.Builder().setConnectable(true).setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_BALANCED).setTimeout(0).build(),
            AdvertiseData.Builder().addServiceUuid(ParcelUuid(serviceId)).setIncludeDeviceName(false).build(),
            AdvertiseData.Builder().apply {if(useHints)addServiceData(ParcelUuid(serviceId),advertisementHint)}.build(), callback)
    }
    private fun connect(peer: String, probe: String = "") {
        // Scan callbacks and recover() must respect the same backoff as the
        // timer; reconnecting immediately can keep the old CCCD subscription.
        if (mode != "scan" || peer in retry || (probe.isEmpty() && peer !in wanted) || links.size + closing.size >= maximumLinks ||
            links.values.any { it.device.address == peer } || adapter?.isEnabled != true) return
        val device = peers[peer] ?: return
        val link = Link(device, true); link.probe = probe; links[link.id] = link
        link.gatt = device.connectGatt(context, false, clientCallbacks(generation), BluetoothDevice.TRANSPORT_LE)
        link.deadline = Runnable { if (links[link.id] === link && !link.ready) { drop(link); schedule(peer) } }
        main.postDelayed(link.deadline!!, connectTimeout)
    }
    private fun schedule(peer: String) {
        if (mode != "scan" || peer !in wanted || peer in retry) return
        val task = NearbyBleDeadline({ callback, delay -> main.postDelayed(callback, delay) },
            { callback -> main.removeCallbacks(callback) })
        retry[peer] = task
        task.arm(retryDelay) {
            if (retry[peer] === task) { retry.remove(peer); connect(peer) }
        }
    }
    private fun ready(link: Link) {
        if (link.ready) return
        link.subscriptionDeadline?.cancel(); link.subscriptionDeadline = null
        link.ready = true; link.deadline?.let { main.removeCallbacks(it) }; link.deadline = null
        emit(mapOf("type" to "link", "link" to link.id, "peer" to link.device.address, "initiator" to link.initiator, "probe" to link.probe))
        while(link.early.isNotEmpty())emit(mapOf("type" to "data","link" to link.id,"data" to link.early.removeFirst()))
        link.earlyBytes=0
    }
    private fun drop(link: Link) {
        if (links.remove(link.id) == null) return
        link.readTask?.let {main.removeCallbacks(it)};link.readTask=null
        link.subscriptionDeadline?.cancel(); link.subscriptionDeadline = null
        link.deadline?.let { main.removeCallbacks(it) }; link.deadline = null
        val completion = link.completion; link.completion = null; link.pending = ByteArray(0)
        if (link.probe.isNotEmpty()) {
            wanted.remove(link.device.address); retry.remove(link.device.address)?.cancel()
        }
        closeGatt(link)
        if (!link.initiator) runCatching { server?.cancelConnection(link.device) }
        if (serverSending == link.id) serverSending = null
        if (link.ready) emit(mapOf("type" to "disconnected", "link" to link.id))
        if (link.probe.isNotEmpty()) emit(mapOf("type" to "probeEnded", "token" to link.probe))
        completion?.error("link_closed", "link_closed", null)
    }
    private fun finishClose(gatt: BluetoothGatt) {
        closing.remove(gatt)?.cancel()
        runCatching { gatt.disconnect() }
        runCatching { gatt.close() }
    }
    private fun closeGatt(link: Link) {
        val gatt = link.gatt ?: return
        val notify = link.notify
        val descriptor = notify?.getDescriptor(cccdId)
        if (mode != "scan" || descriptor == null) { finishClose(gatt); return }
        // Android can retain a physical ACL while a GATT client is replaced.
        // Explicitly unsubscribe so a peer that rejected this old subscription
        // can accept the next one without rebuilding its other live routes.
        val deadline = NearbyBleDeadline({ task, delay -> main.postDelayed(task, delay) },
            { task -> main.removeCallbacks(task) })
        closing[gatt] = deadline
        deadline.arm(1500) { finishClose(gatt) }
        val accepted = runCatching {
            gatt.setCharacteristicNotification(notify, false)
            if (Build.VERSION.SDK_INT >= 33) gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE) == 0
            else { descriptor.value = BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE; gatt.writeDescriptor(descriptor) }
        }.getOrDefault(false)
        if (!accepted) finishClose(gatt)
    }
    private fun drain(link: Link) {
        if (link.completion == null || link.inFlight != 0) return
        if (link.offset == link.pending.size) {
            link.deadline?.let { main.removeCallbacks(it) }; link.deadline = null
            link.pending = ByteArray(0)
            val completion = link.completion; link.completion = null; completion?.success(null); return
        }
        if(link.reading)return
        if(link.polling && link.readDue){
            link.readDue=false;link.reading=true
            if(link.gatt?.readCharacteristic(link.notify)!=true){drop(link);schedule(link.device.address)}
            return
        }
        if (!link.initiator && serverSending != null) return
        val count = minOf(if(link.polling)11 else link.mtu, 512, link.pending.size - link.offset)
        val payload = link.pending.copyOfRange(link.offset, link.offset + count)
        val bytes = if(link.polling) byteArrayOf(3)+link.session+payload else payload
        link.inFlight = count
        val accepted = if (link.initiator) {
            val characteristic = link.write ?: return drop(link)
            if (Build.VERSION.SDK_INT >= 33) link.gatt?.writeCharacteristic(characteristic, bytes, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == 0
            else { characteristic.value = bytes; characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT; link.gatt?.writeCharacteristic(characteristic) == true }
        } else {
            serverSending = link.id
            val characteristic = tx ?: return drop(link)
            if (Build.VERSION.SDK_INT >= 33) server?.notifyCharacteristicChanged(link.device, characteristic, false, bytes) == 0
            else { characteristic.value = bytes; server?.notifyCharacteristicChanged(link.device, characteristic, false) == true }
        }
        if (!accepted) { emit(mapOf("type" to "diagnostic", "event" to "gatt-write-not-accepted")); drop(link); schedule(link.device.address) }
    }
    private fun sent(link: Link, status: Int) {
        if (status != BluetoothGatt.GATT_SUCCESS) { emit(mapOf("type" to "diagnostic", "event" to "gatt-write-status:$status")); drop(link); schedule(link.device.address); return }
        link.offset += link.inFlight; link.inFlight = 0
        if (!link.initiator) serverSending = null
        drain(link)
        if(link.polling && !link.reading && link.inFlight==0) {
            val gatt=link.gatt;val characteristic=link.notify
            if(gatt!=null && characteristic!=null)scheduleRead(link,gatt,characteristic,generation)
        }
        if (!link.initiator) links.values.toList().filter { !it.initiator }.forEach { drain(it) }
    }
    private fun scheduleRead(link: Link, gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, epoch: Int, delay: Long=readInterval){
        val task=Runnable {
            if(epoch==generation && links[link.id]===link && link.ready){
                if(link.inFlight!=0 || link.reading){link.readDue=true;return@Runnable}
                link.reading=true
                if(!gatt.readCharacteristic(characteristic)){emit(mapOf("type" to "diagnostic", "event" to "gatt-read-not-accepted"));drop(link);schedule(link.device.address)}
            }
        }
        link.readTask?.let {main.removeCallbacks(it)};link.readTask=task;main.postDelayed(task,delay)
    }
    private fun clientCallbacks(epoch: Int) = object : BluetoothGattCallback() {
        private fun link(gatt: BluetoothGatt) = links.values.firstOrNull { it.gatt === gatt }
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) = post(epoch) {
            if (gatt in closing) {
                if (newState == BluetoothProfile.STATE_DISCONNECTED) finishClose(gatt)
                return@post
            }
            val link = link(gatt) ?: return@post
            emit(mapOf("type" to "diagnostic", "event" to "gatt-connection-state status=$status state=$newState"))
            if (status != 0 || newState == BluetoothProfile.STATE_DISCONNECTED) { drop(link); schedule(link.device.address) }
            else if (newState == BluetoothProfile.STATE_CONNECTED && !gatt.requestMtu(185)) gatt.discoverServices()
        }
        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) = post(epoch) {
            val link = link(gatt) ?: return@post
            if (status == 0) link.mtu = (mtu - 3).coerceIn(20, 512)
            if (!link.ready) gatt.discoverServices()
        }
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) = post(epoch) {
            val link = link(gatt) ?: return@post
            val service = gatt.getService(serviceId)
            val write = service?.getCharacteristic(rxId)
            val notify = service?.getCharacteristic(txId)
            val descriptor = notify?.getDescriptor(cccdId)
            if(status!=0 || write==null || notify==null || write.properties and BluetoothGattCharacteristic.PROPERTY_WRITE==0){drop(link);schedule(link.device.address);return@post}
            link.write=write;link.notify=notify
            link.polling=notify.properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY==0 && notify.properties and BluetoothGattCharacteristic.PROPERTY_READ!=0
            if(link.polling){
                val hello=byteArrayOf(2)+link.session
                link.helloWriting=true
                val accepted=if(Build.VERSION.SDK_INT>=33)gatt.writeCharacteristic(write,hello,BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)==0
                    else {write.value=hello;write.writeType=BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT;gatt.writeCharacteristic(write)}
                if(!accepted){drop(link);schedule(link.device.address)}
                return@post
            }
            if(descriptor==null || !gatt.setCharacteristicNotification(notify,true)){drop(link);schedule(link.device.address);return@post}
            val ok = if (Build.VERSION.SDK_INT >= 33) gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == 0
                else { descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE; gatt.writeDescriptor(descriptor) }
            if (!ok) { drop(link); schedule(link.device.address) }
        }
        private fun read(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int, value: ByteArray) = post(epoch, value.size) {
            val link=link(gatt) ?: return@post
            if(!link.polling || characteristic.uuid!=txId)return@post
            if(status!=0){emit(mapOf("type" to "diagnostic", "event" to "gatt-read-status:$status"));drop(link);schedule(link.device.address);return@post}
            link.reading=false;link.readDue=false
            if(value.size !in 9..512 || value[0].toInt() !in 0..1 || (value[0].toInt()==0 && value.size!=9) || (value[0].toInt()==1 && value.size<=9)){drop(link);schedule(link.device.address);return@post}
            if(!value.copyOfRange(1,9).contentEquals(link.session)){
                emit(mapOf("type" to "diagnostic","event" to "stale-read-epoch"))
                // Retry a stale initial reply without ever exposing its bytes.
                if(!link.ready){link.reading=true;if(!gatt.readCharacteristic(characteristic)){drop(link);schedule(link.device.address)}}
                else scheduleRead(link,gatt,characteristic,epoch)
                return@post
            }
            val payload=value.copyOfRange(9,value.size)
            if(status!=0 || value.size>512){drop(link);schedule(link.device.address);return@post}
            if(!link.ready)ready(link)
            if(payload.isNotEmpty())emit(mapOf("type" to "data","link" to link.id,"data" to payload))
            drain(link)
            scheduleRead(link,gatt,characteristic,epoch,if(payload.isEmpty())readInterval else 0L)
        }
        override fun onCharacteristicRead(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            if(Build.VERSION.SDK_INT<33)read(gatt,characteristic,status,characteristic.value?.copyOf() ?: ByteArray(0))
        }
        override fun onCharacteristicRead(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray, status: Int) {read(gatt,characteristic,status,value.copyOf())}
        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) = post(epoch) {
            if (gatt in closing) { finishClose(gatt); return@post }
            val link = link(gatt) ?: return@post
            if (descriptor.uuid == cccdId && status == 0) ready(link)
            else { drop(link); schedule(link.device.address) }
        }
        override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) = post(epoch) {
            val link=link(gatt) ?: return@post
            if(characteristic.uuid!=rxId)return@post
            if(link.helloWriting){
                link.helloWriting=false
                if(status!=0){drop(link);schedule(link.device.address);return@post}
                link.reading=true
                if(!gatt.readCharacteristic(link.notify)){drop(link);schedule(link.device.address)}
            } else if(link.inFlight!=0)sent(link,status)
        }
        private fun data(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, bytes: ByteArray) = post(epoch, bytes.size) {
            val link = link(gatt) ?: return@post
            if(characteristic.uuid!=txId)return@post
            if(bytes.isEmpty() || bytes.size>512){drop(link);schedule(link.device.address);return@post}
            if(link.ready)emit(mapOf("type" to "data","link" to link.id,"data" to bytes))
            else if(link.early.size>=128 || link.earlyBytes+bytes.size>4096){drop(link);schedule(link.device.address)}
            else {link.early.addLast(bytes);link.earlyBytes+=bytes.size}
        }
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            if (Build.VERSION.SDK_INT < 33) data(gatt, characteristic, characteristic.value.copyOf())
        }
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
            data(gatt, characteristic, value.copyOf())
        }
    }
    private fun serverCallbacks(epoch: Int) = object : BluetoothGattServerCallback() {
        private fun link(device: BluetoothDevice) = links.values.firstOrNull { !it.initiator && it.device.address == device.address }
        override fun onServiceAdded(status: Int, service: BluetoothGattService) = post(epoch) {
            if (status == 0) advertise(epoch) else discoveryFailure("gatt-service", status)
        }
        override fun onConnectionStateChange(device: BluetoothDevice, status: Int, newState: Int) = post(epoch) {
            if (newState == BluetoothProfile.STATE_DISCONNECTED) link(device)?.let { drop(it) }
            else if (newState == BluetoothProfile.STATE_CONNECTED && link(device) == null) {
                if (links.size >= maximumLinks) server?.cancelConnection(device)
                else {
                    val link = Link(device, false); links[link.id] = link
                    link.subscriptionDeadline = NearbyBleDeadline(
                        { task, delay -> main.postDelayed(task, delay) },
                        { task -> main.removeCallbacks(task) },
                    ).also { deadline ->
                        deadline.arm(connectTimeout) {
                            if (epoch == generation && links[link.id] === link && !link.ready)
                                drop(link)
                        }
                    }
                }
            }
        }
        override fun onMtuChanged(device: BluetoothDevice, mtu: Int) = post(epoch) { link(device)?.mtu = (mtu - 3).coerceIn(20, 512) }
        override fun onDescriptorWriteRequest(device: BluetoothDevice, requestId: Int, descriptor: BluetoothGattDescriptor,
            preparedWrite: Boolean, responseNeeded: Boolean, offset: Int, value: ByteArray) = post(epoch, value.size) {
            val link = link(device)
            val valid = link != null && !preparedWrite && offset == 0 && descriptor.uuid == cccdId &&
                value.contentEquals(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
            if (responseNeeded) server?.sendResponse(device, requestId, if (valid) 0 else BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED, offset, null)
            if (valid) ready(link!!) else link?.let { drop(it) }
        }
        override fun onCharacteristicWriteRequest(device: BluetoothDevice, requestId: Int, characteristic: BluetoothGattCharacteristic,
            preparedWrite: Boolean, responseNeeded: Boolean, offset: Int, value: ByteArray) = post(epoch, value.size) {
            val link = link(device)
            val valid = link?.ready == true && !preparedWrite && offset == 0 && characteristic.uuid == rxId && value.size in 1..512
            if (responseNeeded) server?.sendResponse(device, requestId, if (valid) 0 else BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED, offset, null)
            if (valid) emit(mapOf("type" to "data", "link" to link!!.id, "data" to value.copyOf()))
        }
        override fun onNotificationSent(device: BluetoothDevice, status: Int) = post(epoch) { link(device)?.let { sent(it, status) } }
    }
}

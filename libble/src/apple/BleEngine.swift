import Foundation
import CoreBluetooth

// Own the association as well as the move decision: rejecting a hint must not
// silently replace its remembered peer while a GATT route is live.
struct BleHints {
  private var peers: [Data: UUID] = [:]
  mutating func removeAll() { peers.removeAll() }
  mutating func observe(_ hint: Data, peer: UUID, isProbe: Bool = false, isReady: (UUID) -> Bool) -> UUID? {
    guard hint.count == 8, !isProbe else { return nil }
    let old = peers[hint]
    if let old = old, old != peer, isReady(old) { return nil }
    guard old != nil || peers.count < 64 else { return nil }
    peers[hint] = peer
    return old != peer ? old : nil
  }
}

// One implementation for iOS and macOS. All state and GATT callbacks are on
// the main queue. The consumer owns authentication; this engine owns bytes.
final class BleEngine: NSObject,
  CBCentralManagerDelegate, CBPeripheralManagerDelegate, CBPeripheralDelegate {
  var readInterval: Double = 0.05
  var options = BleOptions()
  private let service: CBUUID
  private let rx: CBUUID
  private let tx: CBUUID
  private final class Link {
    let id = UUID().uuidString
    let peer: UUID
    var peripheral: CBPeripheral?
    var central: CBCentral?
    var write: CBCharacteristic?
    var notify: CBCharacteristic?
    var ready = false
    var probe = ""
    var reading = false, readDue = false
    var readCount = 0, writeCount = 0, writeIssued = 0
    var polling = false, helloWriting = false
    let session = BleReadSession.newEpoch()
    var readTask: DispatchWorkItem?
    var pending = Data()
    var offset = 0
    var inFlight = 0
    var completion: BleCompletion?
    var deadline: DispatchWorkItem?
    init(peer: UUID) { self.peer = peer }
  }
  private var manager: CBCentralManager?
  private var advertiser: CBPeripheralManager?
  private var characteristic: CBMutableCharacteristic?
  private var sink: BleSink?
  private var mode = "off"
  private var published = false
  private var publishing = false
  private var peers: [UUID: CBPeripheral] = [:]
  private var seen: [UUID: Double] = [:]
  private var expiryTask: DispatchWorkItem?
  private var hintedPeers = BleHints()
  private var links: [String: Link] = [:]
  private var wanted = Set<UUID>()
  private var retries: [UUID: DispatchWorkItem] = [:]
  private var retryTokens: [UUID: UUID] = [:]
  private var rejectedSubscribers = Set<UUID>()

  #if LIBBLE_TESTING
  // State-only tests use the actual bridge paths without constructing a radio
  // manager, opening host event channels or touching any device's product data.
  private convenience override init() { self.init(service: "00000000-0000-0000-0000-000000000001", rx: "00000000-0000-0000-0000-000000000002", tx: "00000000-0000-0000-0000-000000000003", useHints: true, sink: { _ in }) }
  static func regressionChecks() -> [String: Bool] {
    var checks: [String: Bool] = [:]
    for ready in [false, true] {
      let bridge = BleEngine()
      bridge.mode = "scan"
      let old = UUID(), peer = UUID(), hint = Data(repeating: 7, count: 8)
      bridge.observeHint(hint, peer: old); bridge.wanted.insert(old)
      let link = Link(peer: peer); link.probe = "unverified"; link.ready = ready
      bridge.links[link.id] = link
      bridge.observeHint(hint, peer: peer)
      checks["probe hint preserves association / ready=\(ready)"] =
        bridge.wanted.contains(old) && !bridge.wanted.contains(peer) &&
        bridge.hintedPeers.observe(hint, peer: UUID(), isReady: { _ in false }) == old
      // Contain already-polluted retry state as well as preventing new moves.
      bridge.wanted.insert(peer); bridge.schedule(peer)
      let pending = bridge.retries[peer]
      bridge.handle(BleCommand(method: "cancelProbe", arguments: ["token": link.probe])) { _ in }
      bridge.schedule(peer) // A late failure callback must not revive the probe.
      checks["probe cancel removes wanted and queued retry / ready=\(ready)"] =
        bridge.links[link.id] == nil && !bridge.wanted.contains(peer) &&
        bridge.retries[peer] == nil && pending?.isCancelled == true
      bridge.wanted.insert(old); bridge.schedule(old)
      checks["ordinary connection retains retry / ready=\(ready)"] = bridge.retries[old] != nil
      bridge.stop()
      checks["stop clears retries / ready=\(ready)"] = bridge.retries.isEmpty && bridge.wanted.isEmpty
    }
    for ready in [false, true] {
      for retry in [false, true] {
        let bridge = BleEngine()
        let failed = Link(peer: UUID()), other = Link(peer: UUID())
        failed.ready = true; other.ready = ready
        bridge.links[failed.id] = failed; bridge.links[other.id] = other
        bridge.disconnectLink(failed, retry: retry)
        checks["inbound isolation / other ready=\(ready) retry=\(retry)"] =
          bridge.links[failed.id] == nil && bridge.links[other.id] === other &&
          bridge.rejectedSubscribers.contains(failed.peer) &&
          !bridge.rejectedSubscribers.contains(other.peer)
        bridge.stop()
      }
    }
    let bridge = BleEngine()
    let old = Link(peer: UUID()), probe = Link(peer: UUID())
    old.ready = true; probe.ready = true; probe.probe = "once"
    bridge.links[old.id] = old; bridge.links[probe.id] = probe
    var rejected = false
    bridge.handle(BleCommand(method: "adoptProbe", arguments: ["token":"once", "oldPeer":old.peer.uuidString])) { rejected = $0 != nil }
    checks["probe cannot replace a ready old link"] = rejected && probe.probe == "once" && bridge.links.count == 2
    bridge.mode = "scan"; bridge.wanted.insert(old.peer)
    bridge.handle(BleCommand(method: "recover", arguments: [:])) { _ in }
    checks["scan refresh preserves ready links and unverified probe intent"] =
      bridge.links[old.id] === old && bridge.links[probe.id] === probe &&
      bridge.wanted.contains(old.peer) && !bridge.wanted.contains(probe.peer) && probe.probe == "once"
    let finalWrite = Link(peer: UUID()); finalWrite.pending = Data([1]); finalWrite.offset = 1
    finalWrite.reading = true; finalWrite.readDue = true; finalWrite.polling = true
    var finalCompletions = 0
    finalWrite.completion = { _ in finalCompletions += 1 }
    bridge.drain(finalWrite); bridge.drain(finalWrite)
    checks["final write completes once despite pending read"] = finalCompletions == 1 && finalWrite.completion == nil
    var completed = 0
    old.completion = { _ in completed += 1 }; old.pending = Data([9])
    var busy = false
    bridge.handle(BleCommand(method: "send", arguments: ["link":old.id, "data":BleBytes(bytes:Data([1,2]))])) { busy = $0?.code == "send_busy" }
    checks["busy send preserves accepted bytes"] = busy && old.pending == Data([9]) && completed == 0
    bridge.drop(old); bridge.drop(old)
    checks["close completes accepted send exactly once"] = completed == 1
    bridge.stop()
    return checks
  }
  #endif

  // Main-thread API, matching the original Apple bridge callback ordering.
  private let useHints: Bool
  init(service: String, rx: String, tx: String, useHints: Bool, sink: @escaping BleSink) {
    self.service = CBUUID(string: service); self.rx = CBUUID(string: rx); self.tx = CBUUID(string: tx)
    self.useHints = useHints; self.sink = sink
    super.init()
  }
  func shutdown() {
    stop()
    manager?.delegate = nil; advertiser?.delegate = nil
    manager = nil; advertiser = nil; sink = nil
  }
  private func emit(_ value: [String: Any]) { sink?(value) }
  private func state(_ value: String) { emit(["type": "state", "state": value]) }
  private func error(_ code: String) -> BleFailure { BleFailure(code: code, message: code, details: nil) }

  func handle(_ call: BleCommand, _ result: @escaping BleCompletion) {
    let args = call.arguments
    switch call.method {
    case "start":
      guard let selected = args["mode"] as? String, ["scan", "advertise"].contains(selected) else {
        result(error("invalid_mode")); return
      }
      stop(); mode = selected
      // Lazy construction: no permission prompt before explicit user action.
      if manager == nil { manager = CBCentralManager(delegate: self, queue: .main) }
      if advertiser == nil { advertiser = CBPeripheralManager(delegate: self, queue: .main) }
      updateMode(); result(nil)
    case "stop": stop(); result(nil)
    case "connect":
      guard mode == "scan", let id = args["peer"] as? String, let uuid = UUID(uuidString: id),
            peers[uuid] != nil, wanted.count < options.maximumLinks || wanted.contains(uuid) else {
        result(error("peer_unavailable")); return
      }
      wanted.insert(uuid); connect(uuid); result(nil)
    case "probe":
      guard mode == "scan", let id = args["peer"] as? String, let uuid = UUID(uuidString: id),
            let token = args["token"] as? String, !token.isEmpty, token.count <= 64,
            peers[uuid] != nil, !wanted.contains(uuid), links.count < options.maximumLinks,
            !links.values.contains(where: { $0.peer == uuid || !$0.probe.isEmpty }) else {
        result(error("peer_unavailable")); return
      }
      connect(uuid, probe: token); result(nil)
    case "cancelProbe", "adoptProbe":
      let token = args["token"] as? String ?? ""
      let link = links.values.first { !token.isEmpty && $0.probe == token }
      if call.method == "cancelProbe" {
        if let link = link { drop(link) }
      } else {
        guard let link = link, link.ready else { result(error("link_unavailable")); return }
        let old = (args["oldPeer"] as? String).flatMap(UUID.init(uuidString:))
        guard !links.values.contains(where: { $0 !== link && $0.peer == old && $0.ready }) else {
          result(error("link_unavailable")); return
        }
        guard wanted.count < options.maximumLinks || old.map({ wanted.contains($0) }) == true || wanted.contains(link.peer) else {
          result(error("peer_unavailable")); return
        }
        if let old = old {
          wanted.remove(old); cancelRetry(old)
          for other in Array(links.values) where other !== link && other.peer == old { drop(other) }
        }
        wanted.insert(link.peer); link.probe = ""
      }
      result(nil)
    case "disconnect", "resetLink":
      if let id = args["link"] as? String, let link = links[id] {
        disconnectLink(link, retry: call.method == "resetLink")
      }
      result(nil)
    case "recover":
      // CoreBluetooth suppresses duplicate advertisements by default. An
      // explicit refresh must produce fresh observations without dropping GATT.
      if mode == "scan" { manager?.stopScan() }
      updateMode(); result(nil)
    case "send":
      guard let id = args["link"] as? String, let link = links[id], link.ready,
            let bytes = args["data"] as? BleBytes,
            (1...65560).contains(bytes.data.count) else {
        result(error("link_unavailable")); return
      }
      guard link.completion == nil else { result(error("send_busy")); return }
      link.pending = bytes.data; link.offset = 0; link.completion = result
      let deadline = DispatchWorkItem { [weak self, weak link] in
        guard let self = self, let link = link, link.completion != nil else { return }
        self.emit(["type": "diagnostic", "event": "send-timeout offset=\(link.offset) size=\(link.pending.count) flight=\(link.inFlight) reading=\(link.reading) due=\(link.readDue)"])
        self.drop(link); self.schedule(link.peer)
      }
      link.deadline = deadline
      DispatchQueue.main.asyncAfter(deadline: .now() + options.sendTimeout, execute: deadline)
      drain(link)
    default: result(error("invalid_operation"))
    }
  }
  private func updateMode() {
    guard mode != "off" else { return }
    let current = mode == "scan" ? manager?.state : advertiser?.state
    switch current {
    case .poweredOn:
      if mode == "scan" {
        manager?.scanForPeripherals(withServices: [service], options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
        expireCandidates()
        state("scanning")
        for id in wanted { connect(id) }
      } else if published {
        if advertiser?.isAdvertising != true {
          advertiser?.startAdvertising([CBAdvertisementDataServiceUUIDsKey: [service]])
        }
      } else if !publishing {
        publishing = true
        let publishedService = CBMutableService(type: service, primary: true)
        let receive = CBMutableCharacteristic(type: rx, properties: [.write], value: nil, permissions: [.writeable])
        let transmit = CBMutableCharacteristic(type: tx, properties: [.notify], value: nil, permissions: [])
        characteristic = transmit; publishedService.characteristics = [receive, transmit]
        advertiser?.add(publishedService)
      }
    case .unauthorized: state("permission_denied")
    case .unsupported: state("unsupported")
    case .poweredOff: state("bluetooth_off")
    default: state("starting")
    }
  }
  private func stop() {
    mode = "off"; wanted.removeAll()
    retries.values.forEach { $0.cancel() }; retries.removeAll()
    retryTokens.removeAll(); rejectedSubscribers.removeAll()
    manager?.stopScan(); advertiser?.stopAdvertising()
    for link in Array(links.values) { drop(link) }
    advertiser?.removeAllServices(); published = false; publishing = false
    characteristic = nil
    for peer in peers.values { peer.delegate = nil }
    peers.removeAll(); seen.removeAll(); expiryTask?.cancel(); expiryTask = nil; hintedPeers.removeAll()
    manager?.delegate = nil; advertiser?.delegate = nil
    manager = nil; advertiser = nil
    state("off")
  }
  private func connect(_ id: UUID, probe: String = "") {
    guard mode == "scan", manager?.state == .poweredOn, let peer = peers[id],
          retries[id] == nil,
          !probe.isEmpty || wanted.contains(id),
          links.count < options.maximumLinks, !links.values.contains(where: { $0.peer == id }) else { return }
    let link = Link(peer: id); link.probe = probe; link.peripheral = peer; links[link.id] = link
    peer.delegate = self; manager?.connect(peer, options: nil)
    let deadline = DispatchWorkItem { [weak self, weak link] in
      guard let self = self, let link = link, !link.ready, self.links[link.id] != nil else { return }
      self.drop(link); self.schedule(id)
    }
    link.deadline = deadline
    DispatchQueue.main.asyncAfter(deadline: .now() + options.connectTimeout, execute: deadline)
  }
  private func schedule(_ peer: UUID) {
    guard mode == "scan", wanted.contains(peer), retries[peer] == nil else { return }
    let token = UUID()
    retryTokens[peer] = token
    let task = DispatchWorkItem { [weak self] in
      guard let self = self, self.retryTokens[peer] == token else { return }
      self.retryTokens.removeValue(forKey: peer)
      self.retries.removeValue(forKey: peer); self.connect(peer)
    }
    retries[peer] = task; DispatchQueue.main.asyncAfter(deadline: .now() + options.retryDelay, execute: task)
  }
  private func cancelRetry(_ peer: UUID) {
    retryTokens.removeValue(forKey: peer)
    retries.removeValue(forKey: peer)?.cancel()
  }
  private func ready(_ link: Link, initiator: Bool) {
    guard !link.ready else { return }
    link.ready = true; link.deadline?.cancel(); link.deadline = nil
    emit(["type": "link", "link": link.id, "peer": link.peer.uuidString, "initiator": initiator, "probe": link.probe])
  }
  private func drop(_ link: Link) {
    guard links.removeValue(forKey: link.id) != nil else { return }
    link.readTask?.cancel(); link.readTask = nil
    link.deadline?.cancel(); link.deadline = nil
    let completion = link.completion; link.completion = nil; link.pending.removeAll()
    if !link.probe.isEmpty { wanted.remove(link.peer); cancelRetry(link.peer) }
    if let peripheral = link.peripheral { manager?.cancelPeripheralConnection(peripheral) }
    if link.ready { emit(["type": "disconnected", "link": link.id]) }
    if !link.probe.isEmpty { emit(["type": "probeEnded", "token": link.probe]) }
    completion?(error("link_closed"))
  }
  private func disconnectLink(_ link: Link, retry: Bool) {
    if !retry { wanted.remove(link.peer); cancelRetry(link.peer) }
    // CoreBluetooth cannot cancel an individual inbound central. Keep other
    // subscribers alive and reject this one until it unsubscribes.
    let republish = retry && link.peripheral == nil && links.count == 1
    emit(["type": "diagnostic", "event": "native-reset", "peer": link.peer.uuidString,
          "otherLinks": links.count - 1, "republish": republish])
    if link.peripheral == nil && !republish { rejectedSubscribers.insert(link.peer) }
    drop(link)
    if retry {
      if link.peripheral != nil { schedule(link.peer) }
      else if republish {
        advertiser?.stopAdvertising(); advertiser?.removeAllServices()
        rejectedSubscribers.removeAll()
        published = false; publishing = false; updateMode()
      }
    }
  }
  private func drain(_ link: Link) {
    guard link.completion != nil, link.inFlight == 0 else { return }
    // The final write response completes this send even if a poll is pending.
    // Waiting for an unrelated read can hold the next ACK/data batch forever.
    if link.offset == link.pending.count {
      link.pending.removeAll(); link.deadline?.cancel(); link.deadline = nil
      let completion = link.completion; link.completion = nil; completion?(nil); return
    }
    guard !link.reading else { return }
    if link.polling && link.readDue {
      link.readDue = false
      // Give CoreBluetooth's write delegate turn time to finish before the
      // next ATT procedure. The task is link-fenced and shares the idle pump.
      pollRead(link, delay: readInterval); return
    }
    while link.offset < link.pending.count {
      if let peer = link.peripheral, let write = link.write {
        // withResponse may report the 512-byte long-write limit. Bound by
        // the ATT-sized write limit as well; peers do not accept prepare writes.
        let limit = min(512, peer.maximumWriteValueLength(for: .withResponse),
                        peer.maximumWriteValueLength(for: .withoutResponse))
        let count = min(limit - (link.polling ? BleReadSession.header : 0),
                        link.pending.count - link.offset)
        guard count > 0 else { drop(link); return }
        link.inFlight = count
        #if LIBBLE_RADIO_TRACE
        link.writeIssued += 1
        emit(["type":"diagnostic", "event":"write-start n=\(link.writeIssued) bytes=\(count) offset=\(link.offset) us=\(Int64(Date().timeIntervalSince1970 * 1_000_000))"])
        #endif
        let payload = link.pending.subdata(in: link.offset..<(link.offset + count))
        peer.writeValue(link.polling ? BleReadSession.packet(3, link.session, payload) : payload,
                        for: write, type: .withResponse)
        return
      }
      guard let central = link.central, let tx = characteristic else { drop(link); return }
      let count = min(central.maximumUpdateValueLength, 512, link.pending.count - link.offset)
      guard count > 0 else { drop(link); return }
      if advertiser?.updateValue(link.pending.subdata(in: link.offset..<(link.offset + count)),
                                 for: tx, onSubscribedCentrals: [central]) != true { return }
      link.offset += count
    }
    link.pending.removeAll(); link.deadline?.cancel(); link.deadline = nil
    let completion = link.completion; link.completion = nil; completion?(nil)
  }
  func centralManagerDidUpdateState(_ central: CBCentralManager) {
    guard central === manager else { return }
    if central.state != .poweredOn {
      for link in Array(links.values) where link.peripheral != nil { drop(link) }
    }
    updateMode()
  }
  func peripheralManagerDidUpdateState(_ peripheral: CBPeripheralManager) {
    guard peripheral === advertiser else { return }
    if peripheral.state != .poweredOn {
      published = false; publishing = false
      rejectedSubscribers.removeAll()
      for link in Array(links.values) where link.central != nil { drop(link) }
    }
    updateMode()
  }
  func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                      advertisementData: [String: Any], rssi RSSI: NSNumber) {
    guard central === manager else { return }
    guard mode == "scan" else { return }
    // Android's address can rotate on an advertising restart. This ephemeral
    // hint only selects where to retry Noise; it cannot grant device trust.
    if let data = advertisementData[CBAdvertisementDataServiceDataKey] as? [CBUUID: Data],
       let hint = data[service], hint.count == 8 {
      observeHint(hint, peer: peripheral.identifier)
    }
    guard peers.count < options.maximumCandidates || peers[peripheral.identifier] != nil else { return }
    peers[peripheral.identifier] = peripheral; seen[peripheral.identifier] = ProcessInfo.processInfo.systemUptime
    emit(["type": "candidate", "peer": peripheral.identifier.uuidString, "rssi": RSSI.intValue])
    if wanted.contains(peripheral.identifier) { connect(peripheral.identifier) }
  }
  private func expireCandidates() {
    expiryTask?.cancel()
    let task = DispatchWorkItem { [weak self] in
      guard let self = self, self.mode == "scan" else { return }
      let now = ProcessInfo.processInfo.systemUptime, active = Set(self.links.values.map { $0.peer })
      for (peer, time) in self.seen where now-time > self.options.candidateTtl && !self.wanted.contains(peer) && !active.contains(peer) {
        self.seen.removeValue(forKey: peer); self.peers.removeValue(forKey: peer)
        self.emit(["type": "candidateGone", "peer": peer.uuidString])
      }
      self.expireCandidates()
    }
    expiryTask = task; DispatchQueue.main.asyncAfter(deadline: .now()+min(1, options.candidateTtl), execute: task)
  }
  private func observeHint(_ hint: Data, peer: UUID) {
    guard useHints else { return }
    let activePeers = Set(links.values.filter { $0.ready }.map { $0.peer })
    let probing = links.values.contains { $0.peer == peer && !$0.probe.isEmpty }
    if let old = hintedPeers.observe(hint, peer: peer,
                                    isProbe: probing,
                                    isReady: { activePeers.contains($0) }) {
      let reconnect = wanted.remove(old) != nil
      cancelRetry(old)
      for link in Array(links.values) where link.peer == old { drop(link) }
      peers.removeValue(forKey: old)
      emit(["type": "candidateGone", "peer": old.uuidString])
      if reconnect { wanted.insert(peer) }
    }
  }
  func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
    guard central === manager else { return }
    guard mode == "scan", links.values.contains(where: { $0.peripheral === peripheral }) else {
      central.cancelPeripheralConnection(peripheral); return
    }
    peripheral.discoverServices([service])
  }
  func peripheral(_ peripheral: CBPeripheral, didModifyServices invalidatedServices: [CBService]) {
    if invalidatedServices.contains(where: { $0.uuid == self.service }) { disconnected(peripheral) }
  }
  func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
    guard central === manager else { return }
    disconnected(peripheral)
  }
  func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
    guard central === manager else { return }
    disconnected(peripheral)
  }
  private func disconnected(_ peer: CBPeripheral) {
    let current = links.values.filter { $0.peripheral === peer }
    guard !current.isEmpty else { return }
    for link in current { drop(link) }
    schedule(peer.identifier)
  }
  func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
    guard mode == "scan", links.values.contains(where: { $0.peripheral === peripheral }) else { return }
    guard error == nil, let service = peripheral.services?.first(where: { $0.uuid == self.service }) else {
      disconnected(peripheral); return
    }
    peripheral.discoverCharacteristics([rx, tx], for: service)
  }
  func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
    guard let link = links.values.first(where: { $0.peripheral === peripheral }), error == nil,
          let rx = service.characteristics?.first(where: { $0.uuid == self.rx && $0.properties.contains(.write) }),
          let tx = service.characteristics?.first(where: { $0.uuid == self.tx && ($0.properties.contains(.notify) || $0.properties.contains(.read)) }) else {
      disconnected(peripheral); return
    }
    link.write = rx; link.notify = tx
    link.polling = !tx.properties.contains(.notify) && tx.properties.contains(.read)
    if link.polling {
      link.helloWriting = true
      peripheral.writeValue(BleReadSession.packet(2, link.session), for: rx, type: .withResponse)
    } else { peripheral.setNotifyValue(true, for: tx) }
  }
  func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
    guard let link = links.values.first(where: { $0.peripheral === peripheral }) else { return }
    if error != nil || !characteristic.isNotifying { drop(link); schedule(link.peer); return }
    ready(link, initiator: true)
  }
  private func readTrace(_ link: Link) {
    #if LIBBLE_RADIO_TRACE
    link.readCount += 1
    emit(["type":"diagnostic", "event":"read-start n=\(link.readCount) offset=\(link.offset)"])
    #endif
  }
  private func pollRead(_ link: Link, delay: Double) {
    guard links[link.id] === link, link.polling, let peer = link.peripheral, let characteristic = link.notify else { return }
    let task = DispatchWorkItem { [weak self, weak link] in
      guard let self = self, let link = link, self.links[link.id] === link else { return }
      if link.inFlight != 0 || link.reading { link.readDue = true; return }
      link.readDue = false; link.reading = true; readTrace(link); peer.readValue(for: characteristic)
    }
    link.readTask?.cancel(); link.readTask = task
    DispatchQueue.main.asyncAfter(deadline: .now()+delay, execute: task)
  }
  func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
    guard characteristic.uuid == self.tx,
      let link = links.values.first(where: { $0.peripheral === peripheral }) else { return }
    guard error == nil, let data = characteristic.value, data.count <= 512 else {
      let reason = error as NSError?
      emit(["type": "diagnostic", "event": "gatt-read-failed:\(reason?.domain ?? "value"):\(reason?.code ?? -1)"])
      drop(link); schedule(link.peer); return
    }
    var payload = data
    if link.polling {
      link.reading = false; link.readDue = false
      guard let decoded = BleReadSession.decode(data, link.session) else {
        // A late reply cannot enter a newly created logical byte stream.
        guard data.count >= BleReadSession.header, data.count <= 512,
              let flag = data.first, flag <= 1,
              (flag == 0 ? data.count == BleReadSession.header : data.count > BleReadSession.header) else {
          drop(link); schedule(link.peer); return
        }
        emit(["type":"diagnostic", "event":"stale-read-epoch"])
        pollRead(link, delay: readInterval); return
      }
      payload = decoded
      #if LIBBLE_RADIO_TRACE
      emit(["type":"diagnostic", "event":"read-end n=\(link.readCount) bytes=\(data.count)"])
      #endif
    }
    if link.polling && !link.ready { ready(link, initiator: true) }
    if link.ready && !payload.isEmpty { emit(["type": "data", "link": link.id, "data": BleBytes(bytes: payload)]) }
    if link.polling { drain(link); pollRead(link, delay: readInterval) }
  }
  func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
    guard characteristic.uuid == rx,
          let link = links.values.first(where: { $0.peripheral === peripheral }),
          link.helloWriting || link.inFlight > 0 else { return }
    if let error = error as NSError? {
      emit(["type": "diagnostic", "event": "gatt-write-failed:\(error.domain):\(error.code)"])
      drop(link); schedule(link.peer); return
    }
    if link.helloWriting {
      link.helloWriting = false
      guard let tx = link.notify else { drop(link); return }
      link.reading = true; readTrace(link); peripheral.readValue(for: tx)
      return
    }
    #if LIBBLE_RADIO_TRACE
    link.writeCount += 1
    emit(["type":"diagnostic", "event":"write-end n=\(link.writeCount) issued=\(link.writeIssued) bytes=\(link.inFlight) us=\(Int64(Date().timeIntervalSince1970 * 1_000_000))"])
    #endif
    link.offset += link.inFlight; link.inFlight = 0; drain(link)
    // A poll deferred by the final write still needs an idle continuation.
    if link.polling && !link.reading && link.inFlight == 0 { pollRead(link, delay: readInterval) }
  }
  func peripheralManager(_ peripheral: CBPeripheralManager, didAdd service: CBService, error: Error?) {
    guard peripheral === advertiser else { return }
    guard mode == "advertise", service.characteristics?.contains(where: { $0 === characteristic }) == true else { return }
    publishing = false
    if error != nil { state("unavailable"); return }
    published = true; updateMode()
  }
  func peripheralManagerDidStartAdvertising(_ peripheral: CBPeripheralManager, error: Error?) {
    guard peripheral === advertiser else { return }
    if mode == "advertise" { state(error == nil ? "advertising" : "unavailable") }
  }
  func peripheralManager(_ peripheral: CBPeripheralManager, central: CBCentral, didSubscribeTo characteristic: CBCharacteristic) {
    guard peripheral === advertiser else { return }
    emit(["type": "diagnostic", "event": "native-subscribed", "peer": central.identifier.uuidString,
          "rejected": rejectedSubscribers.contains(central.identifier)])
    guard mode == "advertise", characteristic.uuid == self.tx, links.count < options.maximumLinks,
          !rejectedSubscribers.contains(central.identifier),
          !links.values.contains(where: { $0.peer == central.identifier }) else { return }
    let link = Link(peer: central.identifier); link.central = central; links[link.id] = link
    ready(link, initiator: false)
  }
  func peripheralManager(_ peripheral: CBPeripheralManager, central: CBCentral, didUnsubscribeFrom characteristic: CBCharacteristic) {
    guard peripheral === advertiser else { return }
    emit(["type": "diagnostic", "event": "native-unsubscribed", "peer": central.identifier.uuidString])
    rejectedSubscribers.remove(central.identifier)
    for link in Array(links.values) where link.peer == central.identifier { drop(link) }
  }
  func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveWrite requests: [CBATTRequest]) {
    guard peripheral === advertiser else { return }
    guard let first = requests.first else { return }
    for request in requests {
      guard request.characteristic.uuid == self.rx, request.offset == 0,
            !rejectedSubscribers.contains(request.central.identifier),
            let data = request.value, !data.isEmpty, data.count <= 512,
            links.values.contains(where: { $0.peer == request.central.identifier && $0.ready }) else {
        peripheral.respond(to: first, withResult: .unlikelyError); return
      }
    }
    for request in requests {
      if let link = links.values.first(where: { $0.peer == request.central.identifier }) {
        emit(["type": "data", "link": link.id, "data": BleBytes(bytes: request.value!)])
      }
    }
    peripheral.respond(to: first, withResult: .success)
  }
  func peripheralManagerIsReady(toUpdateSubscribers peripheral: CBPeripheralManager) {
    guard peripheral === advertiser else { return }
    for link in Array(links.values) where link.central != nil { drain(link) }
  }
}

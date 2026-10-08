import Foundation

// Radio and callbacks preserve the main-queue ordering of the extracted bridge.
// This bounded mailbox separates foreign callbacks from CoreBluetooth delegates.
final class BleContext {
  let service: String, rx: String, tx: String
  let hints: Bool
  let wake: dkble_wake_fn?
  let user: UnsafeMutableRawPointer?
  var engine: BleEngine!
  var events: [[String: Any]] = []
  var bytes = 0
  var generation: UInt64 = 0
  var dispatching = false, notifying = false, overflow = false, stopped = true
  init(_ config: dkble_config, _ uuids: [String]) {
    service = uuids[0]; rx = uuids[1]; tx = uuids[2]
    hints = config.flags & DKBLE_HINT_REBIND != 0
    wake = config.wake; user = config.user
    engine = BleEngine(service: service, rx: rx, tx: tx, useHints: hints) { [weak self] in self?.enqueue($0) }
  }
  func signal() {
    guard !notifying else { return }
    notifying = true; wake?(user); notifying = false
  }
  func enqueue(_ value: [String: Any]) {
    guard !overflow else { return }
    let size = (value["data"] as? BleBytes)?.data.count ?? 0
    if value["type"] as? String == "candidate",
       let index = events.firstIndex(where: { $0["type"] as? String == "candidate" && $0["peer"] as? String == value["peer"] as? String }) {
      var event = value; event["generation"] = generation; events[index] = event; return
    }
    if events.count >= 256 || bytes + size > 262144 {
      overflow = true; stopped = true; events.removeAll(); bytes = 0
      engine.handle(BleCommand(method: "stop", arguments: [:])) { _ in }
      events.append(["type": "overflow", "generation": generation]); signal(); return
    }
    let empty = events.isEmpty
    var event = value; event["generation"] = generation
    events.append(event); bytes += size
    if empty { signal() }
  }
  func invoke(_ method: String, _ args: [String: Any] = [:]) -> Int32 {
    var result: Int32 = 0
    engine.handle(BleCommand(method: method, arguments: args)) { failure in result = failureCode(failure) }
    return result
  }
  func stop() {
    // Suppress old send completions and queued data, then publish only off.
    overflow = true; stopped = true; engine.handle(BleCommand(method: "stop", arguments: [:])) { _ in }
    events.removeAll(); bytes = 0; generation &+= 1; overflow = false
    enqueue(["type": "state", "state": "off"])
  }
}
private func failureCode(_ failure: BleFailure?) -> Int32 {
  guard let failure = failure else { return 0 }
  switch failure.code {
  case "invalid_mode", "invalid_operation": return -1
  case "send_busy": return -3
  case "peer_unavailable", "link_unavailable": return -2
  default: return -4
  }
}
private func text(_ p: UnsafePointer<CChar>?, limit: Int = 64) -> String? {
  guard let p = p, strnlen(p, limit + 1) <= limit else { return nil }
  return String(validatingCString: p)
}
private func uuid(_ p: UnsafePointer<CChar>?) -> String? {
  guard let s = text(p, limit: 36), s.utf8.count == 36, let id = UUID(uuidString: s) else { return nil }
  return id.uuidString
}
private func access(_ raw: OpaquePointer?, _ body: (BleContext) -> Int32) -> Int32 {
  guard Thread.isMainThread else { return -5 }
  guard let raw = raw else { return -1 }
  let c = Unmanaged<BleContext>.fromOpaque(UnsafeRawPointer(raw)).takeUnretainedValue()
  guard !c.notifying else { return -3 }
  return body(c)
}
@_cdecl("dkble_create")
public func abiCreate(_ config: UnsafePointer<dkble_config>?, _ output: UnsafeMutablePointer<OpaquePointer?>?) -> Int32 {
  guard let output = output else { return -1 }; output.pointee = nil
  guard Thread.isMainThread else { return -5 }
  guard let config = config, config.pointee.struct_size == MemoryLayout<dkble_config>.size,
        config.pointee.abi_version == 1, config.pointee.flags & ~UInt32(DKBLE_HINT_REBIND) == 0,
        config.pointee.reserved == 0,
        let service = uuid(config.pointee.service_uuid), let rx = uuid(config.pointee.receive_uuid),
        let tx = uuid(config.pointee.notify_uuid), Set([service, rx, tx]).count == 3 else { return -1 }
  output.pointee = OpaquePointer(Unmanaged.passRetained(BleContext(config.pointee, [service, rx, tx])).toOpaque())
  return 0
}
@_cdecl("dkble_destroy")
public func abiDestroy(_ raw: OpaquePointer?) -> Int32 {
  access(raw) { c in
    guard !c.dispatching else { return -3 }
    c.overflow = true; c.engine.shutdown()
    Unmanaged<BleContext>.fromOpaque(UnsafeRawPointer(raw!)).release(); return 0
  }
}
@_cdecl("dkble_start")
public func abiStart(_ raw: OpaquePointer?, _ mode: UInt32) -> Int32 {
  access(raw) { c in
    guard mode == 1 || mode == 2 else { return -1 }
    c.stop(); c.events.removeAll(); c.bytes = 0; c.stopped = false
    return c.invoke("start", ["mode": mode == 1 ? "scan" : "advertise"])
  }
}
@_cdecl("dkble_stop")
public func abiStop(_ raw: OpaquePointer?) -> Int32 { access(raw) { $0.stop(); return 0 } }
@_cdecl("dkble_connect")
public func abiConnect(_ raw: OpaquePointer?, _ peer: UnsafePointer<CChar>?) -> Int32 {
  access(raw) { c in guard let peer = uuid(peer) else { return -1 }; guard !c.stopped else { return -2 }; return c.invoke("connect", ["peer": peer]) }
}
@_cdecl("dkble_probe")
public func abiProbe(_ raw: OpaquePointer?, _ peer: UnsafePointer<CChar>?, _ token: UnsafePointer<CChar>?) -> Int32 {
  access(raw) { c in
    guard let peer = uuid(peer), let token = text(token), !token.isEmpty else { return -1 }
    guard !c.stopped else { return -2 }; return c.invoke("probe", ["peer": peer, "token": token])
  }
}
@_cdecl("dkble_cancel_probe")
public func abiCancelProbe(_ raw: OpaquePointer?, _ token: UnsafePointer<CChar>?) -> Int32 {
  access(raw) { c in guard let token = text(token), !token.isEmpty else { return -1 }; return c.invoke("cancelProbe", ["token": token]) }
}
@_cdecl("dkble_adopt_probe")
public func abiAdoptProbe(_ raw: OpaquePointer?, _ token: UnsafePointer<CChar>?, _ old: UnsafePointer<CChar>?) -> Int32 {
  access(raw) { c in
    guard let token = text(token), !token.isEmpty else { return -1 }
    var args: [String: Any] = ["token": token]
    if old != nil { guard let peer = uuid(old) else { return -1 }; args["oldPeer"] = peer }
    guard !c.stopped else { return -2 }; return c.invoke("adoptProbe", args)
  }
}
@_cdecl("dkble_disconnect")
public func abiDisconnect(_ raw: OpaquePointer?, _ link: UnsafePointer<CChar>?, _ retry: UInt32) -> Int32 {
  access(raw) { c in
    guard let link = uuid(link), retry <= 1 else { return -1 }
    return c.invoke(retry == 0 ? "disconnect" : "resetLink", ["link": link])
  }
}
@_cdecl("dkble_send")
public func abiSend(_ raw: OpaquePointer?, _ link: UnsafePointer<CChar>?, _ data: UnsafePointer<UInt8>?, _ size: Int, _ request: UInt64) -> Int32 {
  access(raw) { c in
    guard let link = uuid(link), let data = data, size > 0, size <= 65560 else { return -1 }
    guard !c.stopped else { return -2 }
    let generation = c.generation
    var submitting = true, result: Int32 = 0
    c.engine.handle(BleCommand(method: "send", arguments: ["link": link, "data": BleBytes(bytes: Data(bytes: data, count: size))])) { [weak c] failure in
      if submitting && failure != nil { result = failureCode(failure); return }
      guard let c = c, c.generation == generation, !c.stopped else { return }
      c.enqueue(["type": "send", "link": link, "request": request, "status": failureCode(failure)])
    }
    submitting = false; return result
  }
}
@_cdecl("dkble_recover")
public func abiRecover(_ raw: OpaquePointer?) -> Int32 {
  access(raw) { c in guard !c.stopped else { return -2 }; return c.invoke("recover") }
}
private func deliver(_ value: [String: Any], _ callback: dkble_event_fn, _ user: UnsafeMutableRawPointer?) {
  let names: [String: UInt32] = ["state":1, "candidate":2, "candidateGone":3, "link":4, "disconnected":5,
                               "data":6, "probeEnded":7, "send":8, "overflow":9, "diagnostic":10]
  var event = dkble_event()
  event.struct_size = UInt32(MemoryLayout<dkble_event>.size); event.type = names[value["type"] as? String ?? ""] ?? 9
  event.generation = value["generation"] as? UInt64 ?? 0
  event.request_id = value["request"] as? UInt64 ?? 0
  event.status = event.type == 9 ? -6 : value["status"] as? Int32 ?? 0
  event.rssi = Int32(value["rssi"] as? Int ?? 0); event.initiator = value["initiator"] as? Bool == true ? 1 : 0
  let data = (value["data"] as? BleBytes)?.data ?? Data()
  (value["link"] as? String ?? "").withCString { link in
    (value["peer"] as? String ?? "").withCString { peer in
      (value["probe"] as? String ?? value["token"] as? String ?? "").withCString { probe in
        (value["state"] as? String ?? value["event"] as? String ?? "").withCString { detail in
          data.withUnsafeBytes { buffer in
            event.link = link; event.peer = peer; event.probe = probe; event.detail = detail
            event.data = buffer.bindMemory(to: UInt8.self).baseAddress; event.size = buffer.count
            callback(user, &event)
          }
        }
      }
    }
  }
}
@_cdecl("dkble_dispatch")
public func abiDispatch(_ raw: OpaquePointer?, _ maximum: UInt32, _ callback: dkble_event_fn?, _ user: UnsafeMutableRawPointer?) -> Int32 {
  access(raw) { c in
    guard let callback = callback, maximum > 0, maximum <= 256 else { return -1 }
    guard !c.dispatching else { return -3 }
    c.dispatching = true; defer { c.dispatching = false }
    let count = min(Int(maximum), c.events.count), generation = c.generation
    var delivered: Int32 = 0
    for _ in 0..<count {
      if c.generation != generation || c.events.isEmpty { break }
      let event = c.events.removeFirst(); c.bytes -= (event["data"] as? BleBytes)?.data.count ?? 0
      deliver(event, callback, user); delivered += 1
    }
    if !c.events.isEmpty { c.signal() }
    return delivered
  }
}

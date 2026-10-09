import Foundation
@main struct StateTest {
  static func main() {
    let epoch = BleReadSession.newEpoch(), next = BleReadSession.newEpoch()
    let bytes = Data([0,1,255])
    precondition(epoch.count == 8 && epoch != next)
    precondition(BleReadSession.decode(BleReadSession.packet(1, epoch, bytes), epoch) == bytes)
    precondition(BleReadSession.decode(BleReadSession.packet(1, epoch, bytes), next) == nil)
    precondition(BleReadSession.decode(BleReadSession.packet(0, epoch), epoch) == Data())
    precondition(BleReadSession.decode(Data([0]), epoch) == nil)
    precondition(BleReadSession.decode(BleReadSession.packet(2, epoch), epoch) == nil)
    let checks = BleEngine.regressionChecks()
    for key in checks.keys.sorted() { precondition(checks[key] == true, key) }
    // Test the actual mailbox with synthetic bytes, never a radio manager.
    var config = dkble_config(); config.struct_size = UInt32(MemoryLayout<dkble_config>.size); config.abi_version = 1
    let c = BleContext(config, ["00000000-0000-0000-0000-000000000001", "00000000-0000-0000-0000-000000000002", "00000000-0000-0000-0000-000000000003"])
    c.stopped = false
    for n in 0..<1000 { c.enqueue(["type":"candidate", "peer":"same", "rssi":n]) }
    precondition(c.events.count == 1)
    c.events.removeAll()
    for _ in 0..<257 { c.enqueue(["type":"data", "data":BleBytes(bytes:Data(repeating:1,count:512))]) }
    precondition(c.overflow && c.stopped && c.events.count == 1 && c.bytes == 0)
    precondition(c.events.first?["type"] as? String == "overflow")
    let previous = c.generation
    c.stop(); precondition(!c.overflow && c.generation != previous && c.events.count == 1)
    c.engine.shutdown()
    print("\(checks.count) extracted retry/probe/isolation checks and bounded mailbox passed (radio untouched)")
  }
}

import Foundation
// Transport epoch only; authentication and message ACKs belong to the consumer.
enum BleReadSession {
  static let header = 9
  static func newEpoch() -> Data {
    withUnsafeBytes(of: UUID().uuid) { Data($0.prefix(8)) }
  }
  static func packet(_ kind: UInt8, _ epoch: Data, _ payload: Data = Data()) -> Data {
    Data([kind]) + epoch + payload
  }
  static func decode(_ data: Data, _ epoch: Data) -> Data? {
    guard data.count >= header, data.count <= 512,
          let kind = data.first, kind <= 1,
          (kind == 0 ? data.count == header : data.count > header),
          data.dropFirst().prefix(8) == epoch else { return nil }
    return Data(data.dropFirst(header))
  }
}

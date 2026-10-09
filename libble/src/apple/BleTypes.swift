import Foundation
struct BleCommand { let method: String; let arguments: [String: Any] }
struct BleFailure { let code: String; let message: String; let details: String? }
struct BleBytes { let data: Data; init(bytes: Data) { data = bytes } }
typealias BleCompletion = (BleFailure?) -> Void
typealias BleSink = ([String: Any]) -> Void

struct BleOptions {
  var connectTimeout: Double = 20, sendTimeout: Double = 25, retryDelay: Double = 3
  var candidateTtl: Double = 30
  var maximumLinks = 4, maximumCandidates = 64
}

import Foundation

// Bounded protobuf wire reader shared by GTFS-realtime and MVT. All offsets and lengths are checked;
// unknown fields are skipped without treating malformed protobuf as an empty feed.
struct Proto {
  var bytes: [UInt8]
  var offset = 0
  struct Field {
    let number: Int
    let integer: UInt64?
    let payload: [UInt8]?
    let fixed: UInt32?
    var fixed64: UInt64? = nil
  }
  mutating func varint() throws -> UInt64 {
    var value: UInt64 = 0
    for shift in stride(from: 0, through: 63, by: 7) {
      guard offset < bytes.count else { throw FeedError.malformed }
      let byte = bytes[offset]
      offset += 1
      if shift == 63 && byte > 1 { throw FeedError.malformed }
      value |= UInt64(byte & 0x7f) << shift
      if byte & 0x80 == 0 { return value }
    }
    throw FeedError.malformed
  }
  mutating func fields() throws -> [Field] {
    var result: [Field] = []
    while offset < bytes.count {
      let tag = try varint()
      guard tag >> 3 > 0, result.count < 100_000 else { throw FeedError.malformed }
      let number = Int(tag >> 3)
      switch tag & 7 {
      case 0: result.append(Field(number: number, integer: try varint(), payload: nil, fixed: nil))
      case 2:
        let length = try varint()
        guard length <= UInt64(bytes.count - offset) else { throw FeedError.malformed }
        let end = offset + Int(length)
        result.append(
          Field(number: number, integer: nil, payload: Array(bytes[offset..<end]), fixed: nil))
        offset = end
      case 5:
        guard offset + 4 <= bytes.count else { throw FeedError.malformed }
        let value = (0..<4).reduce(UInt32(0)) { $0 | UInt32(bytes[offset + $1]) << (8 * $1) }
        offset += 4
        result.append(Field(number: number, integer: nil, payload: nil, fixed: value))
      case 1:
        guard offset + 8 <= bytes.count else { throw FeedError.malformed }
        let value = (0..<8).reduce(UInt64(0)) { $0 | UInt64(bytes[offset + $1]) << (8 * $1) }
        offset += 8
        result.append(Field(number: number, integer: nil, payload: nil, fixed: nil, fixed64: value))
      default: throw FeedError.malformed
      }
    }
    return result
  }
  static func read(_ bytes: [UInt8]) throws -> [Field] {
    var reader = Proto(bytes: bytes)
    return try reader.fields()
  }
}

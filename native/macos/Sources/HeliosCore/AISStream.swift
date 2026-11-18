import Foundation

public enum AISParser {
  public static func record(_ data: Data) throws -> GeoRecord? {
    guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
      throw FeedError.malformed
    }
    if root["error"] != nil || root["Error"] != nil {
      throw FeedError.coverage(
        "AISStream rejected the subscription. Check the API key in Provider Settings.")
    }
    guard let type = root["MessageType"] as? String,
      ["PositionReport", "StandardClassBPositionReport", "ExtendedClassBPositionReport"].contains(
        type), let body = (root["Message"] as? [String: Any])?[type] as? [String: Any],
      let meta = root["MetaData"] as? [String: Any]
    else { return nil }
    guard let mmsi = Parsers.number(meta["MMSI"] ?? body["UserID"]), mmsi.rounded() == mmsi,
      (10000...999_999_999).contains(mmsi), let lat = Parsers.number(body["Latitude"]),
      let lon = Parsers.number(body["Longitude"]),
      let coordinate = Coordinate(latitude: lat, longitude: lon)
    else { return nil }
    let id = String(Int(mmsi))
    let rawTime = meta["time_utc"] as? String
    let time = rawTime.flatMap { text -> Date? in
      if let date = ProviderParsers.date(text) { return date }
      let formatter = DateFormatter()
      formatter.locale = Locale(identifier: "en_US_POSIX")
      formatter.timeZone = TimeZone(secondsFromGMT: 0)
      formatter.dateFormat = "yyyy-MM-dd HH:mm:ss.SSSSSSSSS ZZZZ 'UTC'"
      return formatter.date(from: text)
    }
    var details = [
      "MMSI": id, "Position basis": "AIS-reported position; reception coverage is incomplete",
    ]
    if let speed = Parsers.number(body["Sog"]), (0..<102.3).contains(speed) {
      details["Speed"] = "\(speed) kn"
    }
    if let course = Parsers.number(body["Cog"]), (0..<360).contains(course) {
      details["Course"] = "\(course)°"
    }
    return GeoRecord(
      id: "ais:\(id)", feed: .vessels, label: SourceText.clean(meta["ShipName"]) ?? "Vessel \(id)",
      coordinate: coordinate, altitude: nil, observedAt: time, source: "AISStream", details: details
    )
  }
}
public actor AISStream {
  private let session = URLSession(configuration: .ephemeral)
  private var socket: URLSessionWebSocketTask?
  private var reader: Task<Void, Never>?
  private var currentKey = ""
  private var records: [String: (Date, GeoRecord)] = [:]
  private var lastMessage: Date?
  private var issue: String?
  private var connectionStarted: Date?
  private var generation = UUID()
  public init() {}
  public func snapshot(key: String) throws -> Snapshot {
    guard !key.isEmpty, key.count <= 256 else {
      throw FeedError.coverage(
        "Add an AISStream API key in Provider Settings to receive real vessel positions.")
    }
    if currentKey != key || reader == nil {
      stop()
      currentKey = key
      issue = nil
      lastMessage = nil
      records = [:]
      connectionStarted = Date()
      let generation = self.generation
      reader = Task { [weak self] in await self?.read(key: key, generation: generation) }
    }
    records = records.filter { Date().timeIntervalSince($0.value.0) < 900 }
    if let issue { throw FeedError.coverage(issue) }
    if let time = lastMessage ?? connectionStarted, Date().timeIntervalSince(time) > 120 {
      throw FeedError.coverage(
        "No AIS reports received for more than two minutes. Retained positions are stale.")
    }
    return Snapshot(
      records: records.values.map(\.1), rejected: 0, source: "AISStream",
      notices: [
        lastMessage == nil
          ? "Connecting; awaiting actual AIS reports."
          : "Received fixes retained for up to 15 minutes. Reception gaps are not interpolated."
      ], retrievedAt: lastMessage)
  }
  private func read(key: String, generation: UUID) async {
    let socket = session.webSocketTask(with: URL(string: "wss://stream.aisstream.io/v0/stream")!)
    socket.maximumMessageSize = 256 * 1024
    self.socket = socket
    socket.resume()
    defer { socket.cancel(with: .goingAway, reason: nil) }
    do {
      let data = try JSONSerialization.data(withJSONObject: [
        "APIKey": key, "BoundingBoxes": [[[-90, -180], [90, 180]]],
        "FilterMessageTypes": [
          "PositionReport", "StandardClassBPositionReport", "ExtendedClassBPositionReport",
        ],
      ])
      try await socket.send(.data(data))
      while !Task.isCancelled {
        let message = try await socket.receive()
        guard !Task.isCancelled, self.generation == generation else { return }
        let data: Data
        switch message {
        case .data(let value): data = value
        case .string(let value): data = Data(value.utf8)
        @unknown default: continue
        }
        if let record = try AISParser.record(data) {
          lastMessage = Date()
          records[record.id] = (Date(), record)
          if records.count > 5000, let oldest = records.min(by: { $0.value.0 < $1.value.0 })?.key {
            records.removeValue(forKey: oldest)
          }
        }
      }
    } catch {
      if !Task.isCancelled, self.generation == generation {
        issue =
          "AIS connection unavailable. Check your key or disable and enable the source to reconnect."
      }
    }
  }
  public func stop() {
    generation = UUID()
    connectionStarted = nil
    reader?.cancel()
    reader = nil
    socket?.cancel(with: .goingAway, reason: nil)
    socket = nil
    currentKey = ""
  }
  public func shutdown() {
    stop()
    session.invalidateAndCancel()
  }
}

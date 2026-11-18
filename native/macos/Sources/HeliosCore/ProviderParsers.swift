import Foundation

public enum ProviderParsers {
  public static func date(_ value: Any?) -> Date? {
    guard let value = value as? String else { return nil }
    let formatter = ISO8601DateFormatter()
    if let date = formatter.date(from: value) { return date }
    formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
    return formatter.date(from: value)
  }
  public static func fires(_ data: Data, now: Date = Date()) throws -> Snapshot {
    guard let text = String(data: data, encoding: .utf8) else { throw FeedError.malformed }
    let lines = text.split(whereSeparator: \.isNewline)
    guard let header = lines.first else { throw FeedError.malformed }
    let columns = header.split(separator: ",", omittingEmptySubsequences: false).map {
      $0.trimmingCharacters(in: .whitespaces).lowercased()
    }
    guard Set(columns).count == columns.count,
      ["latitude", "longitude", "acq_date", "acq_time", "confidence", "frp"].allSatisfy(
        columns.contains)
    else { throw FeedError.malformed }
    let formatter = DateFormatter()
    formatter.locale = Locale(identifier: "en_US_POSIX")
    formatter.timeZone = TimeZone(secondsFromGMT: 0)
    formatter.dateFormat = "yyyy-MM-dd HHmm"
    formatter.isLenient = false
    var rows: [GeoRecord] = []
    var seen = Set<String>()
    var rejected = 0
    for line in lines.dropFirst() {
      let cells = line.split(separator: ",", omittingEmptySubsequences: false).map {
        $0.trimmingCharacters(in: .whitespaces)
      }
      guard cells.count == columns.count else {
        rejected += 1
        continue
      }
      let values = Dictionary(uniqueKeysWithValues: zip(columns, cells))
      let rawTime = values["acq_time"] ?? ""
      guard let lat = Parsers.number(values["latitude"]),
        let lon = Parsers.number(values["longitude"]),
        let coordinate = Coordinate(latitude: lat, longitude: lon), !rawTime.isEmpty,
        rawTime.count <= 4, rawTime.allSatisfy({ $0.isASCII && $0.isNumber }),
        let date = formatter.date(
          from:
            "\(values["acq_date"] ?? "") \(String(repeating: "0", count: 4-rawTime.count))\(rawTime)"
        )
      else {
        rejected += 1
        continue
      }
      guard date >= now.addingTimeInterval(-86400), date <= now.addingTimeInterval(7200) else {
        continue
      }
      let id = "firms:\(lat):\(lon):\(date.timeIntervalSince1970):\(values["satellite"] ?? "")"
      guard seen.insert(id).inserted else {
        rejected += 1
        continue
      }
      var details = [
        "Confidence": values["confidence"] ?? "Unknown",
        "Instrument": values["instrument"] ?? "Unknown",
        "Satellite": values["satellite"] ?? "Unknown",
        "Evidence": "Satellite thermal detection; not a confirmed fire perimeter",
        "Source URL": "https://firms.modaps.eosdis.nasa.gov/",
      ]
      if let frp = Parsers.number(values["frp"]), frp >= 0 {
        details["Radiative power"] = "\(frp) MW"
      } else {
        details["Radiative power"] = "Unknown"
      }
      rows.append(
        .init(
          id: id, feed: .fires, label: "Thermal detection · \(values["satellite"] ?? "VIIRS")",
          coordinate: coordinate, altitude: nil, observedAt: date, source: "NASA FIRMS",
          details: details))
    }
    return Snapshot(records: rows, rejected: rejected, source: "NASA FIRMS", retrievedAt: now)
  }
  public static func sentinel(_ data: Data, center: Coordinate, kind: String) throws -> Snapshot {
    guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
      let rows = root["value"] as? [[String: Any]]
    else { throw FeedError.malformed }
    var records: [GeoRecord] = []
    var seen = Set<String>()
    var rejected = 0
    for row in rows.prefix(10) {
      guard let id = row["Id"] as? String, UUID(uuidString: id) != nil, seen.insert(id).inserted,
        let name = SourceText.clean(row["Name"], limit: 200)
      else {
        rejected += 1
        continue
      }
      let paths = ReferenceLayers.geometry(row["GeoFootprint"])
      let time = date((row["ContentDate"] as? [String: Any])?["Start"])
      var details = [
        "Product ID": id, "Collection": kind,
        "Evidence": "Catalog metadata only; product pixels have not been decoded",
        "Source URL":
          "https://browser.dataspace.copernicus.eu/?lat=\(center.latitude)&lng=\(center.longitude)&zoom=12&datasetId=\(kind == "SENTINEL-1" ? "S1_AWS_IW_VVVH" : "S2_L2A_CDAS")",
        "Location method": "Search point; footprint shown when supplied",
      ]
      for attribute in row["Attributes"] as? [[String: Any]] ?? [] {
        if (attribute["Name"] as? String)?.lowercased() == "cloudcover",
          let cloud = Parsers.number(attribute["Value"]), (0...100).contains(cloud)
        {
          details["Cloud cover"] = "\(cloud)%"
        }
      }
      records.append(
        .init(
          id: "sentinel:\(id)", feed: .sentinel, label: name, coordinate: center, altitude: nil,
          observedAt: time, source: "Copernicus Data Space", details: details,
          footprint: paths.first ?? [], paths: paths))
    }
    return Snapshot(
      records: records, rejected: rejected, source: "Copernicus Data Space",
      notices: [
        "Newest catalog products intersecting the view center. Metadata and footprints are not a raster overlay or SAR decoder."
      ], retrievedAt: Date())
  }
  public static func launches(_ data: Data) throws -> Snapshot {
    guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
      let rows = root["results"] as? [[String: Any]]
    else { throw FeedError.malformed }
    var records: [GeoRecord] = []
    var seen = Set<String>()
    var rejected = 0
    for row in rows {
      let pad = row["pad"] as? [String: Any] ?? [:]
      let location = pad["location"] as? [String: Any] ?? [:]
      guard let id = row["id"] as? String, seen.insert(id).inserted,
        let lat = Parsers.number(pad["latitude"] ?? location["latitude"]),
        let lon = Parsers.number(pad["longitude"] ?? location["longitude"]),
        let coordinate = Coordinate(latitude: lat, longitude: lon),
        let name = SourceText.clean(row["name"])
      else {
        rejected += 1
        continue
      }
      let mission = row["mission"] as? [String: Any] ?? [:]
      let status = row["status"] as? [String: Any] ?? [:]
      records.append(
        .init(
          id: "launch:\(id)", feed: .launches, label: name, coordinate: coordinate, altitude: nil,
          observedAt: date(row["net"]), source: "Launch Library 2",
          details: [
            "Status": SourceText.clean(status["name"]) ?? "Unknown",
            "Launch pad": SourceText.clean(pad["name"]) ?? "Unknown",
            "Mission": SourceText.clean(mission["description"], limit: 1500) ?? "Unknown",
            "Position basis": "Launch-site coordinate, not the rocket's position",
            "Source URL": "https://thespacedevs.com/llapi",
          ]))
    }
    return Snapshot(
      records: records, rejected: rejected, source: "Launch Library 2",
      notices: [
        "Previous 30 days. Times are scheduled launch times; orbit matching is not inferred."
      ], retrievedAt: Date())
  }
  public static func cameras(_ data: Data, provider: CameraProvider) throws -> Snapshot {
    let root = try JSONSerialization.jsonObject(with: data)
    let rows: [[String: Any]]
    if provider == .finland {
      guard let features = (root as? [String: Any])?["features"] as? [[String: Any]] else {
        throw FeedError.malformed
      }
      rows = features
    } else {
      guard let values = root as? [[String: Any]] else { throw FeedError.malformed }
      rows = values
    }
    var records: [GeoRecord] = []
    var seen = Set<String>()
    var rejected = 0
    for row in rows {
      let properties = row["properties"] as? [String: Any] ?? row
      let geo = ReferenceLayers.geometry(row["geometry"]).first?.first
      let coordinate =
        geo
        ?? {
          guard let lat = Parsers.number(row["lat"]), let lon = Parsers.number(row["lon"]) else {
            return nil as Coordinate?
          }
          return Coordinate(latitude: lat, longitude: lon)
        }()
      guard let coordinate,
        let rawID = (row["id"] ?? properties["id"]).map({ String(describing: $0) })
      else {
        rejected += 1
        continue
      }
      let name = SourceText.clean(row["commonName"] ?? properties["name"]) ?? rawID
      let presets: [(String, String, String?)]
      if provider == .london {
        let extra = row["additionalProperties"] as? [[String: Any]] ?? []
        func value(_ key: String) -> String? {
          extra.first { $0["key"] as? String == key }?["value"] as? String
        }
        guard value("available") != "false", let frame = value("imageUrl") else { continue }
        presets = [(rawID, frame, value("videoUrl"))]
      } else {
        guard properties["collectionStatus"] as? String == "GATHERING" else { continue }
        presets = (properties["presets"] as? [[String: Any]] ?? []).compactMap { preset in
          guard preset["inCollection"] as? Bool == true,
            let id = preset["id"] as? String, id.count == 8, id.first == "C",
            id.hasPrefix(rawID), id.dropFirst().allSatisfy({ $0.isASCII && $0.isNumber })
          else { return nil }
          return (id, "https://weathercam.digitraffic.fi/\(id).jpg", nil)
        }
      }
      for (id, frame, video) in presets {
        guard CameraProvider.admit(frame) != nil, seen.insert(id).inserted else {
          rejected += 1
          continue
        }
        var details = [
          "Frame URL": frame,
          "Evidence": "Public operator camera; frame acquisition time may be unknown",
          "Source URL": provider.sourceURL,
          "Data license": provider == .finland
            ? "Fintraffic / Digitraffic · CC BY 4.0" : "TfL Transport Data Service terms",
        ]
        if let video, CameraProvider.admit(video) != nil { details["Video URL"] = video }
        records.append(
          .init(
            id: "camera:\(provider.rawValue):\(id)", feed: .cameras, label: name,
            coordinate: coordinate, altitude: nil, observedAt: nil, source: provider.name,
            details: details))
      }
    }
    return Snapshot(
      records: Array(records.prefix(2000)), rejected: rejected, source: provider.name,
      notices: records.count > 2000 ? ["Catalog capped at 2,000 cameras."] : [], retrievedAt: Date()
    )
  }
}
public enum CameraProvider: String, CaseIterable, Codable, Sendable {
  case london, finland
  public var name: String { self == .london ? "TfL London" : "Fintraffic Finland" }
  public var center: Coordinate {
    self == .london
      ? Coordinate(latitude: 51.5074, longitude: -0.1278)!
      : Coordinate(latitude: 60.1699, longitude: 24.9384)!
  }
  public var catalogURL: URL {
    URL(
      string: self == .london
        ? "https://api.tfl.gov.uk/Place/Type/JamCam"
        : "https://tie.digitraffic.fi/api/weathercam/v1/stations")!
  }
  public var sourceURL: String {
    self == .london
      ? "https://tfl.gov.uk/corporate/terms-and-conditions/transport-data-service"
      : "https://www.digitraffic.fi/en/road-traffic/"
  }
  public static func admit(_ raw: String) -> URL? {
    guard let url = URL(string: raw), url.scheme == "https", url.user == nil, url.password == nil,
      url.port == nil, url.fragment == nil,
      (url.host == "weathercam.digitraffic.fi" && url.path.hasSuffix(".jpg"))
        || (url.host == "s3-eu-west-1.amazonaws.com" && url.path.hasPrefix("/jamcams.tfl.gov.uk/")
          && (url.path.hasSuffix(".jpg") || url.path.hasSuffix(".mp4")))
    else { return nil }
    return url
  }
}

import Foundation

/// Existing public raster imagery choices; dates identify the requested daily product, not every pixel's acquisition time.
public enum DailyImagery: String, CaseIterable, Codable, Sendable {
  case off, trueColor, terra, nightLights, thermal
  public var name: String {
    switch self {
    case .off: "Off"
    case .trueColor: "VIIRS true colour"
    case .terra: "Terra true colour"
    case .nightLights: "Night lights"
    case .thermal: "Thermal anomalies"
    }
  }
  public var resolution: String {
    switch self {
    case .trueColor, .terra: "~250 m · neighbourhood scale"
    case .nightLights: "~500 m · city lights"
    case .thermal: "~1 km · thermal detections"
    case .off: ""
    }
  }
  public static func date(at now: Date = Date()) -> String {
    let formatter = DateFormatter()
    formatter.locale = Locale(identifier: "en_US_POSIX")
    formatter.timeZone = TimeZone(secondsFromGMT: 0)
    formatter.dateFormat = "yyyy-MM-dd"
    return formatter.string(from: now.addingTimeInterval(-86_400))
  }
  public func url(tile: TileID, date: String) -> URL? {
    guard self != .off, tile.zoom <= 9, date.count == 10,
      date.enumerated().allSatisfy({
        [4, 7].contains($0.offset) ? $0.element == "-" : $0.element.isASCII && $0.element.isNumber
      })
    else { return nil }
    let layer: String
    let format: String
    switch self {
    case .off: return nil
    case .trueColor:
      layer = "VIIRS_SNPP_CorrectedReflectance_TrueColor"
      format = "jpg"
    case .terra:
      layer = "MODIS_Terra_CorrectedReflectance_TrueColor"
      format = "jpg"
    case .nightLights:
      layer = "VIIRS_SNPP_DayNightBand_ENCC"
      format = "png"
    case .thermal:
      layer = "MODIS_Terra_Thermal_Anomalies_All"
      format = "png"
    }
    return URL(
      string:
        "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/\(layer)/default/\(date)/GoogleMapsCompatible_Level9/\(tile.zoom)/\(tile.y)/\(tile.x).\(format)"
    )
  }
}
public struct RasterKey: Hashable, Sendable {
  public let basemap: Basemap
  public let tile: TileID
  public let imagery: DailyImagery
  public let date: String
  public init(
    tile: TileID, imagery: DailyImagery = .off, date: String = "", basemap: Basemap = .satellite
  ) {
    self.basemap = basemap
    self.tile = tile
    self.imagery = imagery
    self.date = date
  }
  public var url: URL? { imagery == .off ? basemap.url(tile) : imagery.url(tile: tile, date: date) }
}

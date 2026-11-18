import Foundation

/// Bounded, validated local documents. File URLs are supplied by the native application.
public enum AnnotationDocument {
  public static func load(from url: URL) throws -> [LocalAnnotation] {
    guard FileManager.default.fileExists(atPath: url.path) else { return [] }
    let data = try readBounded(url)
    let values = try JSONDecoder().decode([LocalAnnotation].self, from: data)
    guard values.count <= 500, values.allSatisfy(\.isValid),
      Set(values.map(\.id)).count == values.count
    else { throw FeedError.malformed }
    return values
  }
  public static func save(_ values: [LocalAnnotation], to url: URL) throws {
    guard values.count <= 500, values.allSatisfy(\.isValid),
      Set(values.map(\.id)).count == values.count
    else { throw FeedError.malformed }
    if FileManager.default.fileExists(atPath: url.path) {
      do { _ = try load(from: url) } catch {
        throw FeedError.coverage(
          "The existing annotation file is unreadable and has been preserved. Back it up or recover it before replacing saved annotations."
        )
      }
    }
    try FileManager.default.createDirectory(
      at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    let data = try JSONEncoder().encode(values)
    guard data.count <= 2 * 1024 * 1024 else { throw FeedError.oversized }
    try data.write(to: url, options: .atomic)
  }
  public static func readBounded(_ url: URL) throws -> Data {
    let handle = try FileHandle(forReadingFrom: url)
    defer { try? handle.close() }
    let data = try handle.read(upToCount: 2 * 1024 * 1024 + 1) ?? Data()
    guard data.count <= 2 * 1024 * 1024 else { throw FeedError.oversized }
    return data
  }
}

import Foundation

public enum SourceText {
  public static func clean(_ value: Any?, limit: Int = 200) -> String? {
    guard let text = value as? String else { return nil }
    let cleaned = text.unicodeScalars.filter { !CharacterSet.controlCharacters.contains($0) }.map(
      String.init
    ).joined()
      .split(whereSeparator: { $0.isWhitespace }).joined(separator: " ")
    return cleaned.isEmpty ? nil : String(cleaned.prefix(limit))
  }
}

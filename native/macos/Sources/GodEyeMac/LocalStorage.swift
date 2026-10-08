import AppKit
import GodEyeCore
import LocalAuthentication
import Security
import UniformTypeIdentifiers

@MainActor enum ProviderKeys {
  static let service = "io.godeye.native.providers"
  static func read(_ name: String) -> String {
    let context = LAContext()
    context.interactionNotAllowed = true
    var item: CFTypeRef?
    let status = SecItemCopyMatching(
      [
        kSecClass: kSecClassGenericPassword, kSecAttrService: service, kSecAttrAccount: name,
        kSecReturnData: true, kSecMatchLimit: kSecMatchLimitOne,
        kSecUseAuthenticationContext: context,
      ] as CFDictionary, &item)
    guard status == errSecSuccess, let data = item as? Data else { return "" }
    return String(data: data, encoding: .utf8) ?? ""
  }
  static func save(_ value: String, name: String) throws {
    let value = value.trimmingCharacters(in: .whitespacesAndNewlines)
    guard value.count <= 256 else { throw FeedError.coverage("Provider key is too long.") }
    let query =
      [kSecClass: kSecClassGenericPassword, kSecAttrService: service, kSecAttrAccount: name]
      as CFDictionary
    if value.isEmpty {
      let result = SecItemDelete(query)
      guard result == errSecSuccess || result == errSecItemNotFound else { throw keyError(result) }
      return
    }
    let attributes = [kSecValueData: Data(value.utf8)] as CFDictionary
    var status = SecItemUpdate(query, attributes)
    if status == errSecItemNotFound {
      status = SecItemAdd(
        [
          kSecClass: kSecClassGenericPassword, kSecAttrService: service, kSecAttrAccount: name,
          kSecValueData: Data(value.utf8),
          kSecAttrAccessible: kSecAttrAccessibleWhenUnlockedThisDeviceOnly,
        ] as CFDictionary, nil)
    }
    guard status == errSecSuccess else { throw keyError(status) }
  }
  private static func keyError(_ code: OSStatus) -> FeedError {
    .coverage("Keychain could not save the provider credential (\(code)).")
  }
}
@MainActor enum AnnotationStorage {
  static var url: URL {
    SatelliteStorage.root.appendingPathComponent("annotations.json")
  }
  static func load() throws -> [LocalAnnotation] { try AnnotationDocument.load(from: url) }
  static func save(_ values: [LocalAnnotation]) throws {
    try AnnotationDocument.save(values, to: url)
  }
  static func readBounded(_ url: URL) throws -> Data { try AnnotationDocument.readBounded(url) }
}

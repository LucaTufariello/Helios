#include "Credentials.h"
#import <Foundation/Foundation.h>
#import <LocalAuthentication/LocalAuthentication.h>
#import <Security/Security.h>

namespace helios {
CredentialResult macCredential(const QString &account, const QString *value, bool remove) {
  @autoreleasepool {
    const auto name = account.toUtf8();
    NSString *identity = [[NSString alloc] initWithBytes:name.constData()
                                                  length:name.size()
                                                encoding:NSUTF8StringEncoding];
    NSDictionary *query = @{
      (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
      (__bridge id)kSecAttrService : @"io.helios.native.providers",
      (__bridge id)kSecAttrAccount : identity
    };
    OSStatus status = errSecSuccess;
    CredentialResult result{true, false, {}, {}, "macOS Keychain"};
    if (remove) {
      status = SecItemDelete((__bridge CFDictionaryRef)query);
    } else if (value) {
      const auto secret = value->toUtf8();
      NSData *data = [NSData dataWithBytes:secret.constData() length:secret.size()];
      status = SecItemUpdate((__bridge CFDictionaryRef)query,
                             (__bridge CFDictionaryRef) @{(__bridge id)kSecValueData : data});
      if (status == errSecItemNotFound) {
        NSMutableDictionary *item = [query mutableCopy];
        item[(__bridge id)kSecValueData] = data;
        item[(__bridge id)kSecAttrAccessible] =
            (__bridge id)kSecAttrAccessibleWhenUnlockedThisDeviceOnly;
        status = SecItemAdd((__bridge CFDictionaryRef)item, nullptr);
      }
    } else {
      LAContext *context = [LAContext new];
      context.interactionNotAllowed = YES;
      NSMutableDictionary *item = [query mutableCopy];
      item[(__bridge id)kSecReturnData] = @YES;
      item[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
      item[(__bridge id)kSecUseAuthenticationContext] = context;
      CFTypeRef data = nullptr;
      status = SecItemCopyMatching((__bridge CFDictionaryRef)item, &data);
      if (status == errSecSuccess && data) {
        NSData *bytes = CFBridgingRelease(data);
        result.found = true;
        result.value = QString::fromUtf8(static_cast<const char *>(bytes.bytes), bytes.length);
      } else if (data)
        CFRelease(data);
    }
    if (status != errSecSuccess && status != errSecItemNotFound) {
      result.ok = false;
      result.found = false;
      result.value.clear();
      result.error = QString("macOS Keychain could not access the credential (%1). Unlock the "
                             "keychain or review application access.")
                         .arg(status);
    }
    return result;
  }
}
} // namespace helios

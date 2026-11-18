#pragma once
#include <QString>
namespace helios {
struct CredentialResult {
  bool ok = false, found = false;
  QString value, error, backend;
};
class Credentials {
public:
  // Call from background work: an OS keyring can need user interaction.
  static CredentialResult read(const QString &account);
  static CredentialResult save(const QString &account, const QString &value);
  static CredentialResult erase(const QString &account);
  static bool validAccount(const QString &account);
};
} // namespace helios

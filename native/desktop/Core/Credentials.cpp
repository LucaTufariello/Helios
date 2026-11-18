#if defined(HELIOS_HAVE_LIBSECRET)
#include <libsecret/secret.h>
#endif
#include "Credentials.h"
#include <QLibrary>
#include <QRegularExpression>
#include <algorithm>
#include <cstring>

namespace helios {
#if defined(Q_OS_MACOS)
CredentialResult macCredential(const QString &, const QString *, bool);
#endif
namespace {
CredentialResult unavailable() {
  return {false,
          false,
          {},
          "The operating-system secret store is unavailable or locked. Configure or unlock the "
          "desktop keyring; no plaintext fallback is used.",
          "Unavailable"};
}
#if defined(HELIOS_HAVE_LIBSECRET)
CredentialResult linuxCredential(const QString &account, const QString *value, bool remove) {
  QLibrary library("secret-1", 0);
  library.setLoadHints(QLibrary::PreventUnloadHint);
  if (!library.load())
    return unavailable();
  auto lookup = reinterpret_cast<decltype(&secret_password_lookup_sync)>(
      library.resolve("secret_password_lookup_sync"));
  auto store = reinterpret_cast<decltype(&secret_password_store_sync)>(
      library.resolve("secret_password_store_sync"));
  auto clear = reinterpret_cast<decltype(&secret_password_clear_sync)>(
      library.resolve("secret_password_clear_sync"));
  auto release =
      reinterpret_cast<decltype(&secret_password_free)>(library.resolve("secret_password_free"));
  auto errorFree = reinterpret_cast<decltype(&g_error_free)>(library.resolve("g_error_free"));
  if (!lookup || !store || !clear || !release || !errorFree)
    return unavailable();
  SecretSchema schema{};
  schema.name = "io.helios.native.providers";
  schema.flags = SECRET_SCHEMA_NONE;
  schema.attributes[0] = {"service", SECRET_SCHEMA_ATTRIBUTE_STRING};
  schema.attributes[1] = {"account", SECRET_SCHEMA_ATTRIBUTE_STRING};
  const auto name = account.toUtf8();
  constexpr auto service = "io.helios.native.providers";
  GError *error = nullptr;
  CredentialResult result{true, false, {}, {}, "Linux Secret Service"};
  if (remove) {
    clear(&schema, nullptr, &error, "service", service, "account", name.constData(),
          static_cast<const char *>(nullptr));
  } else if (value) {
    const auto secret = value->toUtf8();
    result.ok = store(&schema, SECRET_COLLECTION_DEFAULT, "Helios provider credential",
                      secret.constData(), nullptr, &error, "service", service, "account",
                      name.constData(), static_cast<const char *>(nullptr));
  } else {
    char *secret = lookup(&schema, nullptr, &error, "service", service, "account", name.constData(),
                          static_cast<const char *>(nullptr));
    if (secret) {
      result.found = true;
      const auto length=strnlen(secret,1025);
      if (length>1024) { result.ok=false; result.found=false; result.error="Saved provider key exceeds the supported limit; re-enter it."; }
      else result.value = QString::fromUtf8(secret,qsizetype(length));
      release(secret);
    }
  }
  if (error || !result.ok) {
    if (error)
      errorFree(error);
    result.ok = false;
    result.found = false;
    result.value.clear();
    result.error = "Linux Secret Service could not access the credential. Unlock/configure the "
                   "desktop keyring; no plaintext fallback is used.";
  }
  return result;
}
#endif
CredentialResult operate(const QString &account, const QString *value, bool remove) {
  if (!Credentials::validAccount(account))
    return {false, false, {}, "Invalid provider credential identifier.", "Unavailable"};
#if defined(Q_OS_MACOS)
  return macCredential(account, value, remove);
#elif defined(HELIOS_HAVE_LIBSECRET)
  return linuxCredential(account, value, remove);
#else
  Q_UNUSED(value);
  Q_UNUSED(remove);
  return unavailable();
#endif
}
} // namespace
bool Credentials::validAccount(const QString &account) {
  return QRegularExpression("^[A-Za-z0-9_-]{1,80}$").match(account).hasMatch();
}
CredentialResult Credentials::read(const QString &account) {
  return operate(account, nullptr, false);
}
CredentialResult Credentials::save(const QString &account, const QString &input) {
  const auto value = input.trimmed();
  if (value.size() > 256 || std::any_of(value.begin(), value.end(), [](QChar c) {
        return c.unicode() < 32 || c.unicode() == 127;
      }))
    return {false,
            false,
            {},
            "Provider keys must be at most 256 characters without control characters.",
            "Unavailable"};
  return value.isEmpty() ? erase(account) : operate(account, &value, false);
}
CredentialResult Credentials::erase(const QString &account) {
  return operate(account, nullptr, true);
}
} // namespace helios

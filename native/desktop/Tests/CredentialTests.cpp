#include "Core/Credentials.h"
#include <QTest>
#include <QUuid>
class CredentialTests : public QObject {
  Q_OBJECT
private slots:
  void admissionDoesNotReachTheSecretStore() {
    QVERIFY(!helios::Credentials::read("../escape").ok);
    QVERIFY(!helios::Credentials::save("firms", QString(257, 'x')).ok);
    QVERIFY(!helios::Credentials::save("ais", "header\r\ninjection").ok);
    QVERIFY(!helios::Credentials::erase("").ok);
    QVERIFY(helios::Credentials::validAccount("firms"));
  }
  void realPlatformStoreRoundTrip() {
    if (qEnvironmentVariable("HELIOS_SECRET_CHECK") != "1")
      QSKIP("Opt-in generated credential in the actual OS secret store.");
    const auto account = "qa-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    struct Cleanup {
      QString account;
      ~Cleanup() {
        helios::Credentials::erase(account);
      }
    } cleanup{account};
    const auto saved = helios::Credentials::save(account, "GENERATED_QA_NOT_A_REAL_PROVIDER_KEY");
    QVERIFY2(saved.ok, qPrintable(saved.error));
    const auto read = helios::Credentials::read(account);
    QVERIFY2(read.ok, qPrintable(read.error));
    QVERIFY(read.found);
    QCOMPARE(read.value, QString("GENERATED_QA_NOT_A_REAL_PROVIDER_KEY"));
    QVERIFY(helios::Credentials::erase(account).ok);
    const auto removed = helios::Credentials::read(account);
    QVERIFY(removed.ok);
    QVERIFY(!removed.found);
    QVERIFY(removed.value.isEmpty());
  }
};
QTEST_GUILESS_MAIN(CredentialTests)
#include "CredentialTests.moc"

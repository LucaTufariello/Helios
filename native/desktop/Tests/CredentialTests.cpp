#include "Core/Credentials.h"
#include <QTest>
#include <QUuid>
class CredentialTests : public QObject {
  Q_OBJECT
private slots:
  void admissionDoesNotReachTheSecretStore() {
    QVERIFY(!godeye::Credentials::read("../escape").ok);
    QVERIFY(!godeye::Credentials::save("firms", QString(257, 'x')).ok);
    QVERIFY(!godeye::Credentials::save("ais", "header\r\ninjection").ok);
    QVERIFY(!godeye::Credentials::erase("").ok);
    QVERIFY(godeye::Credentials::validAccount("tomtom"));
  }
  void realPlatformStoreRoundTrip() {
    if (qEnvironmentVariable("GODEYE_SECRET_CHECK") != "1")
      QSKIP("Opt-in generated credential in the actual OS secret store.");
    const auto account = "qa-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    struct Cleanup {
      QString account;
      ~Cleanup() {
        godeye::Credentials::erase(account);
      }
    } cleanup{account};
    const auto saved = godeye::Credentials::save(account, "GENERATED_QA_NOT_A_REAL_PROVIDER_KEY");
    QVERIFY2(saved.ok, qPrintable(saved.error));
    const auto read = godeye::Credentials::read(account);
    QVERIFY2(read.ok, qPrintable(read.error));
    QVERIFY(read.found);
    QCOMPARE(read.value, QString("GENERATED_QA_NOT_A_REAL_PROVIDER_KEY"));
    QVERIFY(godeye::Credentials::erase(account).ok);
    const auto removed = godeye::Credentials::read(account);
    QVERIFY(removed.ok);
    QVERIFY(!removed.found);
    QVERIFY(removed.value.isEmpty());
  }
};
QTEST_GUILESS_MAIN(CredentialTests)
#include "CredentialTests.moc"

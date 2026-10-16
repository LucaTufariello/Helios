#include "Core/Infrastructure.h"
#include "Core/LibraryStorage.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
using namespace godeye;
class InfrastructureTests : public QObject {
  Q_OBJECT
  QJsonObject result() {
    QFile file(QString(GODEYE_FIXTURE_DIR) + "/change-review-result.json");
    if (!file.open(QIODevice::ReadOnly))
      qFatal("Missing reference fixture");
    return QJsonDocument::fromJson(file.readAll()).object();
  }
private slots:
  void trashRequiresOwnedIdentityHashAndLiveLease() {
    QTemporaryDir root;
    const auto id = QString("11111111-2222-3333-4444-555555555555");
    const auto job = root.filePath(id);
    QVERIFY(QDir().mkpath(job + "/product"));
    const auto record =
        QJsonDocument(QJsonObject{{"version", 1}, {"id", id}, {"state", "completed"}}).toJson();
    QFile run(job + "/run.json");
    QVERIFY(run.open(QIODevice::WriteOnly));
    run.write(record);
    run.close();
    QFile result(job + "/product/result.json");
    QVERIFY(result.open(QIODevice::WriteOnly));
    const QByteArray bytes = "{\"state\":\"completed\"}";
    result.write(bytes);
    result.close();
    QFile lock(job + "/.run.lock");
    QVERIFY(lock.open(QIODevice::WriteOnly));
    lock.close();
    ProductLayer product;
    product.jobID = id;
    product.manifest = result.fileName();
    product.manifestHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    bool called = false;
    auto refuse = [&](const QString &, QString *) {
      called = true;
      return false;
    };
    auto changed = product;
    changed.manifestHash = QByteArray(32, 'x');
    QVERIFY_EXCEPTION_THROWN(trashOwnedProduct(root.path(), changed, refuse), std::runtime_error);
    QVERIFY(!called);
    const int fd = ::open(QFile::encodeName(lock.fileName()).constData(), O_RDWR);
    QVERIFY(fd >= 0);
    QCOMPARE(::flock(fd, LOCK_EX | LOCK_NB), 0);
    QVERIFY_EXCEPTION_THROWN(trashOwnedProduct(root.path(), product, refuse), std::runtime_error);
    QVERIFY(!called);
    ::close(fd);
    QVERIFY_EXCEPTION_THROWN(trashOwnedProduct(root.path(), product, refuse), std::runtime_error);
    QVERIFY(called);
    QVERIFY(QFileInfo::exists(result.fileName()));
    auto imported = product;
    imported.manifest = root.filePath("external-result.json");
    QVERIFY_EXCEPTION_THROWN(trashOwnedProduct(root.path(), imported, refuse), std::runtime_error);
    const auto moved = root.filePath("generated-trash");
    const auto destination =
        trashOwnedProduct(root.path(), product, [&](const QString &source, QString *target) {
          *target = moved;
          return QDir().rename(source, moved);
        });
    QCOMPARE(destination, moved);
    QVERIFY(QFileInfo::exists(moved + "/product/result.json"));
    QVERIFY(!QFileInfo::exists(job));
  }
  void referenceReviewAndCompatibility() {
    auto r = result();
    validateChangeReview(r);
    const auto e = r["evidence"].toArray();
    auto a = e[0].toObject(), b = e[1].toObject();
    for (auto *record : {&a, &b}) {
      (*record)["version"] = 1;
      (*record)["state"] = "completed";
      (*record)["outputs"] = QJsonArray{QJsonObject{{"id", "VV"}, {"scientific", "VV-sigma0.tif"}}};
    }
    QVERIFY(changeCompatibility(a, b, "VV").isEmpty());
    QVERIFY(!changeCompatibility(b, a, "VV").isEmpty());
    QVERIFY(!changeCompatibility(a, b, "VH").isEmpty());
    auto metadata = b["metadata"].toObject();
    metadata["relativeOrbit"] = 67;
    b["metadata"] = metadata;
    QVERIFY(!changeCompatibility(a, b, "VV").isEmpty());
    r["height"] = 1000000;
    QVERIFY_EXCEPTION_THROWN(validateChangeReview(r), std::runtime_error);
    r = result();
    auto candidates = r["candidates"].toArray();
    candidates.append(candidates[0]);
    r["candidates"] = candidates;
    QVERIFY_EXCEPTION_THROWN(validateChangeReview(r), std::runtime_error);
  }
  void sitesPreserveExtensionsBackupsAndWriterConflicts() {
    QTemporaryDir root;
    const auto path = root.filePath("sites.json");
    SiteLibrary sites(path);
    sites.load();
    const auto id = sites.add("Generated site", "Infrastructure", {9, 45, 9.01, 45.01});
    auto values = sites.sites();
    auto site = values[0].toObject();
    site["futureExtension"] = QJsonObject{{"kept", true}};
    values[0] = site;
    sites.save(values);
    SiteLibrary other(path);
    other.load();
    sites.rename(id, "Renamed", "Industrial site");
    QVERIFY(sites.sites()[0].toObject()["futureExtension"].toObject()["kept"].toBool());
    QFile backup(path + ".backup");
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(backup.readAll()).array(), values);
    QVERIFY_EXCEPTION_THROWN(other.remove(id), std::runtime_error);
    sites.remove(id);
    QVERIFY(sites.sites().isEmpty());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("CORRUPT");
    file.close();
    QVERIFY_EXCEPTION_THROWN(sites.load(), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(sites.add("New", "Infrastructure", {9, 45, 9.01, 45.01}),
                             std::runtime_error);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("CORRUPT"));
  }
  void reviewNotesRoundTripUnknownFieldsAndInvalidReferences() {
    QTemporaryDir root;
    const auto id = QString("11111111-2222-3333-4444-555555555555");
    ChangeReview review{id, root.path(), result()};
    ChangeNotes notes(review);
    notes.load();
    notes.set("1", "Follow up", "Needs corroboration", "https://example.test/analyst-citation");
    ChangeNotes restored(review);
    restored.load();
    QCOMPARE(restored.document(), notes.document());
    QVERIFY_EXCEPTION_THROWN(restored.set("499", "Follow up", "Unknown candidate", ""),
                             std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(restored.set("1", "CONFIRMED EVENT", "Invalid interpretation", ""),
                             std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(restored.set("1", "Uncertain", QString(4001, 'x'), ""),
                             std::runtime_error);
    QCOMPARE(restored.document(), notes.document());
  }
};
QTEST_GUILESS_MAIN(InfrastructureTests)
#include "InfrastructureTests.moc"

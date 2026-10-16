#include "UI/CameraViewer.h"
#include "UI/DomainWindow.h"
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>
using namespace godeye;
class DomainUITests : public QObject {
  Q_OBJECT
private slots:
  void savedSnapshotInspectorIsFrozenAndUsesOriginalSourceTime() {
    QTemporaryDir profile;
    DomainWindow window(profile.path(), nullptr, false);
    DomainSnapshot live;
    DomainRecord r;
    r.id = "fixture:same-id";
    r.feed = "flights";
    r.label = "Live fixture";
    r.source = "Live provider fixture";
    r.coordinate = {5, 6};
    live.records.append(r);
    window.setAnnotations(live);
    QJsonObject saved{
        {"id", r.id},
        {"feed", "flights"},
        {"label", "Saved fixture"},
        {"source", "Original provider fixture"},
        {"coordinate", QJsonObject{{"latitude", 1}, {"longitude", 2}}},
        {"observedAt", 0.0},
        {"altitude", QJsonValue()},
        {"details", QJsonObject{{"Speed", "12 kt"}, {"Evidence", "Stored source observation"}}},
        {"footprint", QJsonArray{}},
        {"paths", QJsonArray{}}};
    QSignalSpy selected(&window, &DomainWindow::recordSelected),
        feeds(&window, &DomainWindow::feedsChanged);
    window.showSavedRecord(saved);
    auto *dialog = window.findChild<QDialog *>("savedRecordInspector");
    QVERIFY(dialog);
    QVERIFY(QTest::qWaitForWindowExposed(dialog));
    const auto snapshotQA = qEnvironmentVariable("GODEYE_DOMAIN_QA");
    if (!snapshotQA.isEmpty()) {
      QVERIFY(QDir().mkpath(snapshotQA));
      QVERIFY(dialog->grab().save(snapshotQA + "/generated-saved-snapshot-ui.png"));
    }
    QCOMPARE(dialog->windowTitle(), "Saved mission snapshot");
    auto *time = dialog->findChild<QLabel *>("snapshotObservedAt");
    QVERIFY(time);
    QVERIFY(time->text().contains("2001-01-01T00:00:00.000Z"));
    auto *details = dialog->findChild<QTextEdit *>("snapshotDetails");
    auto *json = dialog->findChild<QTextEdit *>("snapshotJSON");
    QVERIFY(details && json);
    QVERIFY(details->isReadOnly() && json->isReadOnly());
    QVERIFY(details->toPlainText().contains("12 kt"));
    QVERIFY(json->toPlainText().contains("Original provider fixture"));
    QCOMPARE(selected.size(), 0);
    QCOMPARE(feeds.size(), 0);
    QVERIFY(!window.network().busy());
    live.records[0].label = "Changed live fixture";
    window.setAnnotations(live);
    QCOMPARE(dialog->findChild<QLabel *>("snapshotTitle")->text(), "Saved fixture");
    QVERIFY(json->toPlainText().contains("Original provider fixture"));
    saved["observedAt"] = "2026-10-15T12:34:56Z";
    window.showSavedRecord(saved);
    dialog = window.findChild<QDialog *>("savedRecordInspector");
    QVERIFY(dialog->findChild<QLabel *>("snapshotObservedAt")
                ->text()
                .contains("2026-10-15T12:34:56.000Z"));
    saved["observedAt"] = QJsonValue();
    QJsonObject largeDetails;
    for (int i = 0; i < 100; ++i)
      largeDetails[QString("Detail %1").arg(i)] = QString(1000, 'x');
    saved["details"] = largeDetails;
    window.showSavedRecord(saved);
    dialog = window.findChild<QDialog *>("savedRecordInspector");
    QVERIFY(dialog->findChild<QLabel *>("snapshotObservedAt")->text().contains("Unknown"));
    QVERIFY(dialog->findChild<QTextEdit *>("snapshotDetails")->toPlainText().contains("truncated"));
    QVERIFY(dialog->findChild<QTextEdit *>("snapshotJSON")->toPlainText().size() < 66000);
    dialog->close();
  }
  void boundedDecoderFrameParser() {
    QImage frame;
    QByteArray bytes = "P6\n1 1\n255\n";
    bytes.append(char(10));
    bytes.append(char(20));
    bytes.append(char(30));
    auto parsed = CameraViewer::takeFrame(bytes, frame);
    QVERIFY(parsed && *parsed);
    QCOMPARE(frame.size(), QSize(1, 1));
    QCOMPARE(frame.pixelColor(0, 0), QColor(10, 20, 30));
    QVERIFY(bytes.isEmpty());
    bytes = "P6\n1281 1\n255\n";
    parsed = CameraViewer::takeFrame(bytes, frame);
    QVERIFY(parsed && !*parsed);
    bytes = "P6\n1 1\n255\n";
    QVERIFY(!CameraViewer::takeFrame(bytes, frame));
    bytes = "P3\n1 1\n255\n";
    parsed = CameraViewer::takeFrame(bytes, frame);
    QVERIFY(parsed && !*parsed);
  }
  void offlineRestoresEnabledFlagsWithoutStartingRemoteWork() {
    QTemporaryDir profile;
    QSettings settings(profile.path() + "/sources.ini", QSettings::IniFormat);
    for (const auto &feed : domainFeeds())
      settings.setValue("enabled/" + feed.id, true);
    settings.sync();
    DomainWindow window(profile.path(), nullptr, false);
    QVERIFY(!window.network().networking());
    window.restoreFollowing("flights:pending-fixture");
    QCOMPARE(window.followedID(), "flights:pending-fixture");
    window.restoreFollowing(QString(201, 'x'));
    QVERIFY(window.followedID().isEmpty());
    window.restoreFollowing({});
    QVERIFY(window.followedID().isEmpty());
    for (const auto &feed : domainFeeds())
      if (!QStringList{"annotations", "datacenters", "dams", "cables"}.contains(feed.id)) {
        QVERIFY(window.network().enabled(feed.id));
        QVERIFY(!window.network().busy(feed.id));
        QVERIFY(window.network().snapshot(feed.id).error.contains("Offline mode"));
      }
    auto config = window.network().configuration();
    config.satelliteGroup = "gps-ops";
    config.cameraProvider = "finland";
    config.transitID = "hsl-helsinki";
    config.sentinelKind = "SENTINEL-1";
    window.setConfiguration(config);
    QSet<QString> displayed;
    for (auto *combo : window.findChildren<QComboBox *>())
      displayed.insert(combo->currentData().isValid() ? combo->currentData().toString()
                                                      : combo->currentText());
    for (const auto &value : QStringList{"gps-ops", "finland", "hsl-helsinki", "SENTINEL-1"})
      QVERIFY(displayed.contains(value));
  }
  void suppliedRecordSelectionEvidenceAndRetainedSettings() {
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    {
      DomainWindow window(profile.path());
      window.show();
      DomainSnapshot snapshot;
      snapshot.source = "User-authored test fixture";
      DomainRecord r;
      r.id = "annotation:generated-fixture";
      r.feed = "annotations";
      r.label = "Generated source-selection fixture";
      r.coordinate = {0, 0};
      r.source = snapshot.source;
      r.details = {{"Evidence", "User-authored fixture"}};
      snapshot.records.append(r);
      window.setAnnotations(snapshot);
      window.network().setEnabled("annotations", true);
      window.selectRecord(r.id);
      QSignalSpy selected(&window, &DomainWindow::recordSelected),
          evidence(&window, &DomainWindow::evidenceRequested);
      window.selectRecord(r.id);
      QVERIFY(!selected.isEmpty());
      for (auto *button : window.findChildren<QPushButton *>())
        if (button->text() == "Add evidence…")
          button->click();
      QCOMPARE(evidence.size(), 1);
      const auto saved = evidence[0][0].toJsonObject();
      QCOMPARE(saved["id"].toString(), r.id);
      QVERIFY(!saved.contains("raw"));
      QVERIFY(saved["observedAt"].isNull());
      auto *feeds = window.findChild<QListWidget *>("domainFeeds");
      QVERIFY(feeds);
      int visible = 0;
      window.setSection("Marine");
      for (int i = 0; i < feeds->count(); ++i)
        visible += !feeds->item(i)->isHidden();
      QCOMPARE(visible, 2);
      // Persist through the actual checkbox handler; provider credentials never enter this file.
      window.setSection("Ground");
      for (int i = 0; i < feeds->count(); ++i)
        if (feeds->item(i)->data(Qt::UserRole) == "annotations")
          feeds->item(i)->setCheckState(Qt::Unchecked);
    }
    QSettings settings(profile.path() + "/sources.ini", QSettings::IniFormat);
    QVERIFY(!settings.value("enabled/annotations", true).toBool());
  }
  void realOperatorStillAndNativeClip() {
    if (qEnvironmentVariable("GODEYE_LIVE_CHECKS") != "1")
      QSKIP("Set GODEYE_LIVE_CHECKS=1 for actual public camera media checks.");
    const QString qa = qEnvironmentVariable("GODEYE_DOMAIN_QA");
    QVERIFY2(!qa.isEmpty(), "Set GODEYE_DOMAIN_QA to retain actual camera evidence");
    QVERIFY(QDir().mkpath(qa));
    QTemporaryDir profile;
    DomainWindow window(profile.path());
    window.show();
    window.network().setEnabled("cameras", true);
    window.selectFeed("cameras");
    QTRY_VERIFY_WITH_TIMEOUT(!window.network().busy("cameras"), 45000);
    const auto snapshot = window.network().snapshot("cameras");
    QVERIFY2(snapshot.ok(), qPrintable(snapshot.error));
    QVERIFY(!snapshot.records.isEmpty());
    std::optional<DomainRecord> record;
    for (const auto &r : snapshot.records)
      if (!r.details["Video URL"].toString().isEmpty()) {
        record = r;
        break;
      }
    QVERIFY(record);
    window.selectRecord(record->id);
    auto *tabs = window.findChild<QTabWidget *>();
    QVERIFY(tabs);
    tabs->setCurrentIndex(2);
    auto *still = window.findChild<QPushButton *>("domainCameraStill");
    auto *image = window.findChild<QLabel *>("domainCameraImage");
    QVERIFY(still);
    QVERIFY(image);
    still->click();
    QTRY_VERIFY_WITH_TIMEOUT(!image->pixmap().isNull(), 35000);
    QVERIFY(window.grab().save(qa + "/camera-source-ui.png"));
    QVERIFY(image->pixmap().save(qa + "/operator-still.png"));
    auto *clip = new CameraViewer(*record);
    QPointer<CameraViewer> guard(clip);
    clip->show();
    auto *video = clip->findChild<QLabel *>("cameraClipImage");
    QVERIFY(video);
    QSignalSpy frames(clip, &CameraViewer::frameDecoded);
    auto *load = clip->findChild<QPushButton *>("cameraClipLoad");
    QVERIFY(load);
    load->click();
    QTRY_VERIFY_WITH_TIMEOUT(!clip->decodedFrame().isNull(), 45000);
    const auto frame = clip->decodedFrame();
    QVERIFY(frame.width() > 0);
    QVERIFY(frame.height() > 0);
    QVERIFY(!frames.isEmpty());
    QVERIFY(frame.save(qa + "/operator-clip-frame.png"));
    QVERIFY(clip->grab().save(qa + "/camera-clip-ui.png"));
    const auto owned = clip->ownedClipPath();
    QVERIFY(!owned.isEmpty());
    QVERIFY(QFile::exists(owned));
    auto *pause = clip->findChild<QPushButton *>("cameraClipPause");
    QVERIFY(pause);
    pause->click();
    QCOMPARE(pause->text(), "Resume");
    pause->click();
    QCOMPARE(pause->text(), "Pause");
    auto *seek = clip->findChild<QDoubleSpinBox *>("cameraClipSeekSeconds");
    auto *seekButton = clip->findChild<QPushButton *>("cameraClipSeek");
    QVERIFY(seek);
    QVERIFY(seekButton);
    const auto before = frames.size();
    seek->setValue(.5);
    seekButton->click();
    QTRY_VERIFY_WITH_TIMEOUT(frames.size() > before, 10000);
    QCOMPARE(clip->ownedClipPath(), owned);
    QFile report(qa + "/camera-media.json");
    QVERIFY(report.open(QIODevice::WriteOnly));
    report.write(
        QJsonDocument(
            QJsonObject{{"timeUTC", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                        {"recordID", record->id},
                        {"source", record->source},
                        {"frameURL", record->details["Frame URL"]},
                        {"clipURL", record->details["Video URL"]},
                        {"decodedWidth", frame.width()},
                        {"decodedHeight", frame.height()},
                        {"nativeMediaBackend", "Owned FFmpeg process / Qt Widgets"},
                        {"acquisitionTime", QJsonValue()},
                        {"limitation", "Actual provider media may be delayed. Retrieval UTC is "
                                       "distinct from acquisition time."}})
            .toJson());
    report.close();
    clip->close();
    QTRY_VERIFY(guard.isNull());
    QTRY_VERIFY(!QFile::exists(owned));
  }
};
QTEST_MAIN(DomainUITests)
#include "DomainUITests.moc"

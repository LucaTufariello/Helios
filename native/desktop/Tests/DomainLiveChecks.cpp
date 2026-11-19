#include "Core/DomainNetwork.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <cstdio>
using namespace helios;
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().size() < 2 || qEnvironmentVariable("HELIOS_LIVE_CHECKS") != "1") {
    std::fprintf(stderr, "Explicit opt-in required: HELIOS_LIVE_CHECKS=1 DomainLiveChecks "
                         "OUTPUT.json [feed IDs...]\n");
    return 2;
  }
  auto feeds = app.arguments().mid(2);
  const bool finland = feeds.removeAll("--finland") > 0;
  if (feeds.isEmpty())
    feeds = {"earthquakes", "military", "disasters", "installations", "energy", "launches",
             "transit",     "cameras",  "sentinel",  "datacenters",   "dams",   "cables"};
  DomainNetwork network;
  if (finland) {
    auto config = network.configuration();
    config.cameraProvider = "finland";
    network.setConfiguration(config);
  }
  network.setView({9.17, 45.45, 9.21, 45.48});
  QElapsedTimer elapsed;
  elapsed.start();
  QEventLoop loop;
  QSet<QString> completed;
  QObject::connect(&network, &DomainNetwork::changed, &loop, [&](const QString &feed) {
    if (feeds.contains(feed) && !network.busy(feed) && network.enabled(feed)) {
      completed.insert(feed);
      const auto snapshot = network.snapshot(feed);
      std::fprintf(stderr, "%s: %lld records, %s\n", qPrintable(feed),
                   static_cast<long long>(snapshot.records.size()),
                   qPrintable(snapshot.error.isEmpty() ? QString("received") : snapshot.error));
      if (completed.size() == feeds.size())
        loop.quit();
    }
  });
  for (const auto &feed : feeds) {
    if (!domainFeed(feed) || QStringList{"fires", "vessels"}.contains(feed)) {
      std::fprintf(stderr, "Unsupported or credentialed live-check feed: %s\n", qPrintable(feed));
      return 2;
    }
    network.setEnabled(feed, true);
  }
  QTimer::singleShot(150000, &loop, &QEventLoop::quit);
  loop.exec();
  QJsonArray results;
  int successful = 0;
  for (const auto &feed : feeds) {
    const auto snapshot = network.snapshot(feed);
    QJsonArray ids;
    for (const auto &r : snapshot.records.mid(0, 10))
      ids.append(QJsonObject{
          {"id", r.id},
          {"label", r.label},
          {"source", r.source},
          {"observedAt", r.observedAt.isValid()
                             ? QJsonValue(r.observedAt.toUTC().toString(Qt::ISODateWithMs))
                             : QJsonValue()}});
    const bool passed = completed.contains(feed) && snapshot.ok();
    successful += passed;
    results.append(QJsonObject{
        {"feed", feed},
        {"completed", completed.contains(feed)},
        {"ok", passed},
        {"count", snapshot.records.size()},
        {"rejected", snapshot.rejected},
        {"retrievedAt", snapshot.retrievedAt.isValid()
                            ? QJsonValue(snapshot.retrievedAt.toUTC().toString(Qt::ISODateWithMs))
                            : QJsonValue()},
        {"error", snapshot.error},
        {"notice", snapshot.notices.join("\n")},
        {"records", ids}});
  }
  network.cancel();
  QSaveFile output(app.arguments()[1]);
  const auto bytes =
      QJsonDocument(
          QJsonObject{{"timeUTC", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                      {"elapsedMilliseconds", elapsed.elapsed()},
                      {"sources", results},
                      {"successful", successful},
                      {"total", feeds.size()},
                      {"limitation",
                       "Keyless source transport checks only; no AIS/FIRMS "
                       "credentials, no scientific interpretation or GUI acceptance."}})
          .toJson(QJsonDocument::Indented);
  if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
    return 2;
  return successful == feeds.size() ? 0 : 1;
}

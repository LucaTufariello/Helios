#pragma once
#include "DomainFeeds.h"
#include "DomainOrbit.h"
#include "DomainSocket.h"
#include <QFutureWatcher>
#include <QTimer>
#include <atomic>
#include <memory>
namespace helios {
struct DomainHttpResult {
  QByteArray bytes;
  QString error;
  int status = 0;
};
// Runs on a worker thread. Redirects stay on HTTPS; credentials and forms never redirect.
DomainHttpResult domainFetch(const QUrl &url, qsizetype maximum,
                             const std::shared_ptr<std::atomic_bool> &cancel,
                             const QMap<QByteArray, QByteArray> &headers = {},
                             const QByteArray &form = {}, bool redirects = true);
class DomainNetwork : public QObject {
  Q_OBJECT
public:
  struct TrafficCache;
  explicit DomainNetwork(QObject *parent = nullptr, bool networking = true);
  ~DomainNetwork() override;
  void setView(Bounds bounds);
  void setConfiguration(DomainConfiguration configuration);
  const DomainConfiguration &configuration() const {
    return configuration_;
  }
  void setEnabled(const QString &feed, bool enabled);
  bool enabled(const QString &feed) const;
  bool networking() const {
    return networking_;
  }
  void refresh(const QString &feed = {});
  void cancel(const QString &feed = {});
  bool busy(const QString &feed = {}) const;
  DomainSnapshot snapshot(const QString &feed) const;
  QVector<DomainRecord> records() const;
  void setAnnotations(DomainSnapshot snapshot);
  bool orbit(const QString &record, QVector<Coordinate> &points, QVector<double> &altitudes,
             QDateTime at = QDateTime::currentDateTimeUtc()) const;
signals:
  void changed(QString feed);

private:
  struct Entry {
    bool enabled = false, queued = false;
    quint64 generation = 0;
    QDateTime attempted;
    DomainSnapshot snapshot;
    QFutureWatcher<DomainSnapshot> *watcher = nullptr;
    std::shared_ptr<std::atomic_bool> cancel;
  };
  QMap<QString, Entry> entries_;
  QMap<QString, DomainOrbit> catalogs_;
  std::shared_ptr<TrafficCache> traffic_;
  DomainSocket ais_;
  QTimer timer_;
  Bounds view_{8.9, 45.2, 9.5, 45.7};
  DomainConfiguration configuration_;
  int active_ = 0;
  bool networking_ = true;
  void pump();
  void start(const QString &feed);
};
} // namespace helios

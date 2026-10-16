#pragma once
#include "RadarCatalog.h"
#include <QFutureWatcher>
#include <atomic>
#include <memory>
namespace godeye {
struct DownloadResult {
  QString file, error;
};
class RadarDownload : public QObject {
  Q_OBJECT
public:
  using NetworkFactory = std::function<std::unique_ptr<QNetworkAccessManager>()>;
  static constexpr qint64 maximumBytes = 40ll * 1024 * 1024 * 1024;
  explicit RadarDownload(QObject *parent = nullptr, NetworkFactory factory = {});
  ~RadarDownload() override;
  bool start(const RadarScene &scene, const RadarQuery &query, const QString &token,
             const QString &library, qint64 maximum = maximumBytes);
  void cancel();
  bool busy() const {
    return busy_;
  }
  static bool trusted(const QUrl &url);
  static bool validArchive(const QString &path);
  static QString savedArchive(const QString &directory, const RadarScene &scene);
signals:
  void changed();
  void progress(qint64 received, qint64 expected);
  void finished(QString archive, QString error);

private:
  struct Control {
    std::atomic_bool cancelled{false};
  };
  std::shared_ptr<Control> control_;
  QFutureWatcher<DownloadResult> watcher_;
  NetworkFactory factory_;
  bool busy_ = false;
};
} // namespace godeye

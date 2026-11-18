#pragma once
#include "Geography.h"
#include <QDate>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QUrl>

class QNetworkReply;
namespace helios {
struct OpticalQuery {
  Bounds region;
  QDate start, end;
  double maximumCloud = 20;
  QString source = "sentinel2";
  bool valid() const;
  QUrl url() const;
};
class OpticalCatalog : public QObject {
  Q_OBJECT
public:
  explicit OpticalCatalog(QObject *parent = nullptr);
  void search(const OpticalQuery &query);
  void cancel();
  bool busy() const {
    return busy_;
  }
  static QVector<QJsonObject> parse(const QByteArray &bytes, const QString &source);
  static bool validSentinelAsset(const QString &url, const QString &sceneID);
  static QString cbersAsset(const QString &sceneID, const QString &band);
signals:
  void changed();
  void results(QVector<QJsonObject> scenes);
  void failed(QString error);

private:
  QNetworkAccessManager network_;
  QPointer<QNetworkReply> reply_;
  quint64 generation_ = 0;
  bool busy_ = false;
};
} // namespace helios

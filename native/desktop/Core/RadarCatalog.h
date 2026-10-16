#pragma once
#include "Geography.h"
#include "HttpRequest.h"
#include <QDate>
#include <QDateTime>
#include <QMap>
#include <functional>
namespace godeye {
struct RadarQuery {
  Bounds region;
  QDate start, end;
  QString polarization = "VV + VH", mode = "IW";
  bool valid() const;
  QUrl url(int skip = 0) const;
  QJsonObject json() const;
};
struct RadarScene {
  QString id, name, acquisition, orbit = "Unknown orbit";
  qint64 contentLength = 0;
  bool online = false;
  std::optional<int> relativeOrbit;
  QVector<QVector<Coordinate>> footprint;
  QJsonObject json() const;
  QString archiveFilename() const;
  QUrl downloadURL() const;
  static bool supportedName(const QString &name);
};
struct RadarPage {
  QVector<RadarScene> scenes;
  bool hasMore = false;
  int rejected = 0;
};
class RadarCatalog : public QObject {
  Q_OBJECT
public:
  explicit RadarCatalog(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
  void search(const RadarQuery &query, int skip = 0);
  void cancel();
  bool busy() const {
    return request_.busy() || parsing_;
  }
  static RadarPage parse(const QByteArray &bytes);
signals:
  void changed();
  void results(godeye::RadarPage page);
  void failed(QString error);

private:
  HttpRequest request_;
  std::optional<RadarQuery> query_;
  bool parsing_ = false;
  quint64 generation_ = 0;
};
class CopernicusAccount : public QObject {
  Q_OBJECT
public:
  explicit CopernicusAccount(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
  void signIn(const QString &username, const QString &password, const QString &totp = {});
  void signOut();
  void access(std::function<void(QString, QString)> completion);
  bool busy() const {
    return request_.busy();
  }
  bool signedIn() const {
    return !access_.isEmpty();
  }
signals:
  void changed();
  void failed(QString error);

private:
  HttpRequest request_;
  QString access_, refresh_;
  QDateTime expires_;
  std::function<void(QString, QString)> pending_;
  void exchange(const QMap<QString, QString> &form);
};
} // namespace godeye
Q_DECLARE_METATYPE(godeye::RadarPage)

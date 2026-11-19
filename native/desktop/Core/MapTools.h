#pragma once
#include "Geography.h"
#include "HttpRequest.h"
#include "JsonFile.h"
#include <QDateTime>
#include <QJsonArray>
#include <QUrl>
#include <memory>
namespace helios {
struct PlaceMatch {
  QString label;
  Coordinate coordinate;
};
QUrl placeSearchURL(const QString &query);
QVector<PlaceMatch> parsePlaces(const QByteArray &bytes);
// One cancellable search per instance; parsing is bounded and runs outside the
// UI thread.
class MapLookup : public QObject {
  Q_OBJECT
public:
  explicit MapLookup(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
  ~MapLookup() override;
  bool search(const QString &query);
  void cancel();
  bool busy() const {
    return busy_ || parsing_;
  }
  QString error() const {
    return error_;
  }
  QVector<PlaceMatch> places() const {
    return places_;
  }
signals:
  void changed();
  void completed(bool success);

private:
  HttpRequest request_;
  quint64 generation_ = 0;
  bool busy_ = false, parsing_ = false;
  QString error_;
  QVector<PlaceMatch> places_;
  bool start(const QUrl &url);
};

void validateAnnotations(const QJsonArray &annotations);
QJsonObject newAnnotation(const QString &title, const QVector<Coordinate> &points, bool closed);
class AnnotationLibrary {
public:
  explicit AnnotationLibrary(QString file);
  void load();
  QJsonArray values() const {
    return values_;
  }
  QString filename() const {
    return file_.path();
  }
  void replace(const QJsonArray &values);
  void add(const QJsonObject &value);
  void update(const QString &id, const QString &title, const QVector<Coordinate> &points,
              bool closed);
  void remove(const QString &id);

private:
  JsonFile file_;
  QJsonArray values_;
};
QJsonObject defaultWorkspace();
// Validates known legacy v1 fields while preserving unknown fields for local I/O.
QJsonObject validatedWorkspace(QJsonObject workspace);
QJsonObject loadWorkspace(const QString &filename);
void saveWorkspace(const QString &filename, const QJsonObject &workspace);
QUrl cameraShareURL(const Camera &camera);
std::optional<Camera> cameraFromShareURL(const QUrl &url);
QUrl workspaceShareURL(const QJsonObject &workspace);
std::optional<QJsonObject> workspaceFromShareURL(const QUrl &url);
struct SolarState {
  QDateTime date;
  Coordinate subsolar;
  explicit SolarState(QDateTime utc = QDateTime::currentDateTimeUtc());
  double elevation(Coordinate point) const;
  QVector<Coordinate> terminator() const;
};
} // namespace helios

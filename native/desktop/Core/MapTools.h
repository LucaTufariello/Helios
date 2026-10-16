#pragma once
#include "Geography.h"
#include "HttpRequest.h"
#include "JsonFile.h"
#include <QDateTime>
#include <QJsonArray>
#include <QUrl>
#include <memory>
namespace godeye {
struct PlaceMatch {
  QString label;
  Coordinate coordinate;
};
struct RouteStep {
  QString instruction;
  double distance = 0;
  Coordinate coordinate;
};
struct NativeRoute {
  QVector<Coordinate> points;
  QVector<RouteStep> steps;
  double distance = 0, duration = 0;
};
QUrl placeSearchURL(const QString &query);
QVector<PlaceMatch> parsePlaces(const QByteArray &bytes);
QUrl routeURL(Coordinate start, Coordinate end, const QString &mode);
NativeRoute parseRoute(const QByteArray &bytes);
// One cancellable operation per instance; create two instances for independent
// search and routing. Parsing is bounded and runs outside the UI thread.
class MapLookup : public QObject {
  Q_OBJECT
public:
  explicit MapLookup(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
  ~MapLookup() override;
  bool search(const QString &query);
  bool route(Coordinate start, Coordinate end, const QString &mode);
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
  NativeRoute result() const {
    return route_;
  }
signals:
  void changed();
  void completed(bool success);

private:
  HttpRequest request_;
  quint64 generation_ = 0;
  bool busy_ = false, routing_ = false, parsing_ = false;
  QString error_;
  QVector<PlaceMatch> places_;
  NativeRoute route_;
  bool start(const QUrl &url, bool routing);
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
// Validates known Swift v1 fields while preserving unknown fields for local I/O.
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
} // namespace godeye

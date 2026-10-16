#pragma once
#include <QJsonDocument>
#include <QString>
#include <functional>
namespace godeye {
// Small user documents: validated load, conflict detection, last-good backup
// and atomic publication. Call load before editing an existing document.
class JsonFile {
public:
  using Validator = std::function<void(const QJsonDocument &)>;
  JsonFile(QString path, qsizetype maximum, Validator validator);
  QJsonDocument load(const QJsonDocument &empty = {});
  void save(const QJsonDocument &document);
  QString path() const {
    return path_;
  }
  static QByteArray read(const QString &path, qsizetype maximum);

private:
  QString path_;
  qsizetype maximum_;
  Validator validator_;
  QByteArray hash_;
  bool loaded_ = false, existed_ = false;
};
} // namespace godeye

#pragma once
#include "JsonFile.h"
#include "Products.h"
#include <QJsonArray>
namespace helios {
void validateSites(const QJsonDocument &document);
void validateChangeReview(const QJsonObject &result);
QString changeCompatibility(const QJsonObject &before, const QJsonObject &after,
                            const QString &polarization);
class SiteLibrary {
public:
  explicit SiteLibrary(const QString &path);
  void load();
  void save(QJsonArray sites);
  QJsonArray sites() const {
    return sites_;
  }
  QString add(const QString &name, const QString &kind, Bounds bounds);
  void rename(const QString &id, const QString &name, const QString &kind);
  void remove(const QString &id);

private:
  JsonFile file_;
  QJsonArray sites_;
};
struct ChangeReview {
  QString id, directory;
  QJsonObject result;
  static ChangeReview read(const QString &manifest);
  ProductLayer layer(const QString &view) const;
};
class ChangeNotes {
public:
  explicit ChangeNotes(const ChangeReview &review);
  void load();
  void set(const QString &candidate, const QString &decision, const QString &note,
           const QString &reference);
  QJsonObject document() const {
    return notes_;
  }

private:
  JsonFile file_;
  QJsonObject notes_;
  QString id_;
};
} // namespace helios

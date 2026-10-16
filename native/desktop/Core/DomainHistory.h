#pragma once
#include "DomainFeeds.h"
namespace godeye {
struct DomainTrackFix {
  Coordinate coordinate;
  std::optional<double> altitude;
  QDateTime time;
};
class DomainHistory {
public:
  void ingest(const QVector<DomainRecord> &records,
              QDateTime now = QDateTime::currentDateTimeUtc());
  QVector<DomainTrackFix> fixes(const QString &id) const {
    return tracks_.value(id);
  }
  std::optional<DomainRecord> delayed(const DomainRecord &record, QDateTime time) const;
  qsizetype count() const {
    return tracks_.size();
  }

private:
  QMap<QString, QVector<DomainTrackFix>> tracks_;
};
} // namespace godeye

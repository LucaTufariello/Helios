#include "DomainHistory.h"
#include <algorithm>
#include <cmath>
namespace godeye {
void DomainHistory::ingest(const QVector<DomainRecord> &records, QDateTime now) {
  for (const auto &record : records) {
    if (!QStringList{"flights", "military", "vessels", "transit"}.contains(record.feed) ||
        !record.observedAt.isValid() || record.observedAt > now.addSecs(120) ||
        record.observedAt < now.addSecs(-3600) || !record.coordinate.valid() ||
        (record.altitude && !std::isfinite(*record.altitude)))
      continue;
    auto &fixes = tracks_[record.id];
    if (!fixes.isEmpty() && fixes.back().time >= record.observedAt)
      continue;
    fixes.append({record.coordinate, record.altitude, record.observedAt});
    while (!fixes.isEmpty() && (fixes.size() > 240 || fixes.front().time.secsTo(now) > 3600))
      fixes.removeFirst();
  }
  for (auto i = tracks_.begin(); i != tracks_.end();) {
    if (i->isEmpty() || i->back().time.secsTo(now) > 3600)
      i = tracks_.erase(i);
    else
      ++i;
  }
  if (tracks_.size() > 5000) {
    QVector<std::pair<QDateTime, QString>> ages;
    ages.reserve(tracks_.size());
    for (auto i = tracks_.begin(); i != tracks_.end(); ++i)
      ages.append({i->back().time, i.key()});
    std::sort(ages.begin(), ages.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    const auto remove = tracks_.size() - 5000;
    for (qsizetype i = 0; i < remove; ++i)
      tracks_.remove(ages[i].second);
  }
}
std::optional<DomainRecord> DomainHistory::delayed(const DomainRecord &record, QDateTime at) const {
  const auto entries = tracks_.constFind(record.id);
  if (entries == tracks_.cend() || !at.isValid())
    return {};
  for (auto i = entries->crbegin(); i != entries->crend(); ++i)
    if (i->time <= at) {
      auto delayed = record;
      delayed.coordinate = i->coordinate;
      delayed.altitude = i->altitude;
      delayed.observedAt = i->time;
      delayed.paths.clear();
      delayed.footprint.clear();
      delayed.details["Playback"] = "Retained observed fix; no interpolation or extrapolation";
      delayed.details["Metadata basis"] =
          "Latest source metadata; displayed position and time come from the retained fix";
      delayed.raw = {{"id", record.id},
                     {"coordinate", i->coordinate.json()},
                     {"observedAt", i->time.toUTC().toString(Qt::ISODateWithMs)},
                     {"positionBasis", "Retained observed fix; no interpolation or extrapolation"}};
      delayed.raw["altitude"] = i->altitude ? QJsonValue(*i->altitude) : QJsonValue();
      return delayed;
    }
  return {};
}
} // namespace godeye

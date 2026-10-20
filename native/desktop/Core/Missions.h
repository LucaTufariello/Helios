#pragma once
#include "Geography.h"
#include "Investigations.h"
#include <QJsonArray>
#include <QMap>
#include <QObject>
#include <functional>

namespace godeye {
double foundationNow();
QString dateLabel(const QJsonValue &date);
QString newID();
void validateMissions(const QJsonArray &plans);

// Keep the original JSON objects, including extension keys we do not yet edit.
// Changes are validated before publishing; saving is atomic and conflict checked.
class Missions : public QObject {
  Q_OBJECT
public:
  explicit Missions(QObject *parent = nullptr);
  QJsonArray plans() const {
    return plans_;
  }
  QJsonObject selected() const;
  QString selectedID() const {
    return selectedID_;
  }
  QString filename() const {
    return filename_;
  }
  bool dirty() const {
    return dirty_;
  }
  void load(const QString &filename);
  void save(const QString &filename);
  void select(const QString &id);
  QString create(const QString &name);
  void removeMission(const QString &id);
  void editEntry(const QString &collection, const QString &id, const QJsonObject &fields);
  void setComparison(const QString &baseline, const QString &later, double blend);
  void captureAcquisitions(const QMap<QString, QJsonObject> &snapshots);
  QString addMonitor(const QJsonObject &fields);
  void commitMonitor(const QString &missionID, const QString &monitorID,
                     const QJsonObject &checkpoint, const QString &event = {});
  void addObservationProposal(const QString &missionID, const QJsonObject &observation);
  void setField(const QString &field, const QString &value);
  void setBrief(const QString &name, const QString &objective, const QString &notes);
  void addNote(const QString &title, const QString &body, const QString &reference = {},
               const QString &evidenceID = {}, const QString &areaID = {});
  QString addTask(const QString &title, const QString &areaID = {},
                  const QString &observationID = {});
  void setTaskDone(const QString &id, bool done);
  QString addArea(const QString &name, const QString &purpose, Bounds bounds);
  QString importDossierSite(const QString &missionID, const QJsonObject &site);
  QString linkDossierEvidence(const QString &missionID, const QString &areaID, QJsonObject item);
  void setDossierEvidence(const QString &missionID, const QString &areaID,
                          const QString &evidenceID, bool linked);
  void setDossierBaseline(const QString &missionID, const QString &areaID,
                          const QString &evidenceID, bool pinned);
  void configureMonitoring(const QString &missionID, const QString &areaID, QJsonObject profile);
  QString upsertMonitoringEvent(const QString &missionID, QJsonObject event);
  void reviewMonitoringEvent(const QString &eventID, const QString &state,
                             const QString &rationale);
  QString addMonitoringTask(const QString &eventID, const QString &title);
  void removeMonitoringEvent(const QString &eventID);
  void addView(const QString &title, const Camera &camera);
  void addEvidence(QJsonObject item);
  void removeEntry(const QString &collection, const QString &id);
  void setObservationReview(const QString &id, const QString &state, const QString &note);
  QString addObservation(const QString &title, const QString &category, Coordinate coordinate,
                         const QString &evidenceID = {}, const QString &areaID = {});
  void setInvestigationOverview(const QString &missionID, const QJsonObject &expected,
                                const QJsonObject &fields);
  QString adoptAsset(const QString &missionID, const Asset &asset, const QString &areaID = {});
  QString adoptRelationship(const QString &missionID, const AssetRelationship &relationship);
  QString retainCitation(const QString &missionID, QJsonObject citation);
  void approveAssociation(const QString &missionID, const AssociationSuggestion &suggestion);
  QString saveFinding(const QString &missionID, Finding finding, int expectedRevision,
                      const QJsonArray &newCitations = {});
  QString draftFinding(const QString &missionID, const QJsonObject &origin);
  QString addFindingTask(const QString &missionID, const QString &findingID, const QString &title);
signals:
  void changed();

private:
  QJsonArray plans_;
  QString selectedID_, filename_;
  QByteArray originalHash_;
  bool dirty_ = false;
  void updateMission(const QString &missionID, const std::function<void(QJsonObject &)> &mutation,
                     bool persist);
  void update(const std::function<void(QJsonObject &)> &mutation);
  static QJsonObject brief(QJsonObject plan);
  static void activity(QJsonObject &brief, const QString &message);
};
} // namespace godeye

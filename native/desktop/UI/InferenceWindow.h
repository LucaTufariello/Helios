#pragma once
#include "Core/Inference.h"
#include "Core/WorkerJob.h"
#include <QFutureWatcher>
#include <QMainWindow>
#include <QSet>
#include <memory>
class QLabel;
class QLineEdit;
class QPushButton;
class QDoubleSpinBox;
class QListWidget;
namespace godeye {
class InferenceWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit InferenceWindow(const QString &profile, QWidget *parent = nullptr);
  bool busy() const;
  WorkerJob *job() const {
    return job_.get();
  }
  // Call with the currently linked optical evidence. Changing it while work runs
  // leaves the owned job running but prevents acceptance into another mission.
  void setEvidence(const ProductLayer &layer, const QString &evidenceID, const QString &missionID);
  void invalidateEvidence();
  void cancel();
  void markProposalPending(const QString &id);
  void resolveProposal(const QString &id, bool saved, const QString &message = {});
signals:
  void proposalAccepted(QString missionID, QJsonObject observation);

private:
  QString profile_, evidenceID_, missionID_, runEvidenceID_, runMissionID_;
  QString runPreview_;
  QByteArray runManifestHash_;
  QSet<QString> pending_;
  ProductLayer layer_;
  QLineEdit *python_, *contract_;
  QDoubleSpinBox *threshold_;
  QLabel *source_, *status_, *preview_, *previewCaption_;
  QPushButton *analyze_, *cancel_, *accept_;
  QListWidget *proposals_;
  std::unique_ptr<WorkerJob> job_;
  QFutureWatcher<QImage> previewLoad_;
  QImage previewImage_;
  void start();
  void refresh();
};
} // namespace godeye

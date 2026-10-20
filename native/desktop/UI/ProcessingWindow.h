#pragma once
#include "Core/OpticalCatalog.h"
#include "Core/ScientificQueue.h"
#include "Core/WorkerJob.h"
#include "Graphics/MapWidget.h"
#include "RadarPanel.h"
#include <QDialog>
#include <QSettings>
class QComboBox;
class QCheckBox;
class QDateEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
namespace godeye {
class ProcessingWindow : public QDialog {
  Q_OBJECT
public:
  ProcessingWindow(const QString &profile, MapWidget &map, QWidget *parent = nullptr,
                   bool networking = true);
  ~ProcessingWindow() override;
  bool busy() const;
  void setEngines(const QString &python, const QString &gpt = {});
  bool setQuery(const OpticalQuery &query);
  bool setRadarQuery(const RadarQuery &query);
  RadarPanel *radarPanel() const {
    return radarPanel_;
  }
  bool startOptical(const QJsonObject &scene, Bounds region, const QString &mode = "pan");
  bool startPrisma(const QString &path, const QString &mode, double wavelength,
                   std::optional<Bounds> region = {});
  bool startRadar(const QString &path, const QJsonObject &options, bool inspect = false);
  bool startReview(const QJsonObject &site, const QString &before, const QString &after,
                   const QJsonObject &options);
  WorkerJob *job() const {
    return job_;
  }
  ScientificQueue &scientificQueue() {
    return *queue_;
  }
  bool submitMonitoring(WorkerRequest request, const QString &token);
  void cancel();
signals:
  void productReady(QString manifest);
  void reviewReady(QString manifest);

private:
  QString profile_;
  bool networking_ = true;
  MapWidget &map_;
  QSettings settings_;
  OpticalCatalog catalog_;
  RadarPanel *radarPanel_ = nullptr;
  QPointer<WorkerJob> job_;
  ScientificQueue *queue_;
  QProcess preflight_;
  quint64 preflightGeneration_ = 0;
  QByteArray preflightOutput_, preflightErrors_;
  QVector<QJsonObject> scenes_;
  QComboBox *source_, *mode_;
  QDoubleSpinBox *west_, *south_, *east_, *north_, *cloud_, *wavelength_, *spacing_;
  QDateEdit *start_, *end_;
  QCheckBox *clip_, *vv_, *vh_, *orbit_, *speckle_;
  QLineEdit *python_, *gpt_, *prisma_, *radar_;
  QListWidget *acquisitions_, *history_;
  QLabel *sourceInfo_, *status_, *engineStatus_;
  QPlainTextEdit *details_;
  QProgressBar *progress_;
  QPushButton *cancel_;
  QVector<QPushButton *> startButtons_;
  Bounds region() const;
  void setRegion(Bounds region);
  bool launch(WorkerRequest request);
  void refresh();
  void reloadJobs();
  void checkPython();
};
} // namespace godeye

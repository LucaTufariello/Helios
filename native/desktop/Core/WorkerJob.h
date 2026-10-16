#pragma once
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <optional>

namespace godeye {
struct WorkerRequest {
  QString python, worker, operation, input, gpt;
  QJsonObject options;
  bool optical = false;
  QJsonObject document;
  QString host;
};

// One owned process, one new job directory, bounded JSON-lines and diagnostics.
// A live advisory lock, inherited by Python, establishes recovery ownership;
// persisted PIDs are never used to signal or establish a worker's liveness.
class WorkerJob : public QObject {
  Q_OBJECT
public:
  explicit WorkerJob(const QString &jobsDirectory, QObject *parent = nullptr);
  ~WorkerJob() override;
  bool start(const WorkerRequest &request);
  void cancel();
  bool running() const {
    return process_.state() != QProcess::NotRunning;
  }
  QString directory() const {
    return directory_;
  }
  QString resultFile() const {
    return directory_ +
           (record_["operation"] == "inspect" ? "/inspection.json" : "/product/result.json");
  }
  QJsonObject record() const {
    return record_;
  }
  QString error() const {
    return error_;
  }
  static QString workerPath(bool optical);
  static QString hostPath();
  static QJsonObject readRecord(const QString &file);
  static bool recover(const QString &directory, QString *error = nullptr);
signals:
  void changed();
  void finished(bool success);

private:
  QString root_, directory_, error_;
  QProcess process_;
  QTimer stopTimer_, logTimer_;
  QElapsedTimer elapsed_;
  QJsonObject record_;
  QByteArray pending_, log_;
  int lock_ = -1;
  bool cancelled_ = false, optical_ = false, protocolFailed_ = false, finished_ = false;
  void receive();
  void diagnose(const QByteArray &bytes);
  void fail(const QString &message);
  void complete(int code, QProcess::ExitStatus status);
  bool persist();
  void releaseLock();
  void cleanupIncomplete();
};
} // namespace godeye

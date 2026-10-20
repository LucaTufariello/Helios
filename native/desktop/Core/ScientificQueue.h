#pragma once
#include "WorkerJob.h"
#include <QPointer>
#include <QVector>
namespace godeye {
class ScientificQueue : public QObject {
  Q_OBJECT
public:
  ScientificQueue(QString root, QObject *parent = nullptr);
  ~ScientificQueue() override;
  bool submit(WorkerRequest request, const QString &token, bool automatic);
  void cancel(const QString &token);
  void cancelAll();
  bool contains(const QString &token) const;
  bool busy() const;
  int pendingCount() const {
    return pending_.size();
  }
  WorkerJob *activeJob() const {
    return active_;
  }
signals:
  void started(QString token, WorkerJob *job, bool automatic);
  void finished(QString token, QString directory, bool success, bool automatic, QString error);
  void changed();

private:
  struct Entry {
    WorkerRequest request;
    QString token;
    bool automatic;
  };
  QString root_;
  QVector<Entry> pending_;
  QPointer<WorkerJob> active_;
  Entry current_;
  bool completing_ = false;
  void drain();
};
} // namespace godeye

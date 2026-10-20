#include "ScientificQueue.h"
#include <QTimer>
namespace godeye {
ScientificQueue::ScientificQueue(QString root, QObject *parent)
    : QObject(parent), root_(std::move(root)) {}
ScientificQueue::~ScientificQueue() {
  if (active_) {
    disconnect(active_, nullptr, this, nullptr);
    active_->blockSignals(true);
    delete active_;
  }
}
bool ScientificQueue::contains(const QString &token) const {
  if (active_ && active_->running() && current_.token == token)
    return true;
  for (const auto &p : pending_)
    if (p.token == token)
      return true;
  return false;
}
bool ScientificQueue::busy() const {
  return completing_ || (active_ && active_->running()) || !pending_.isEmpty();
}
bool ScientificQueue::submit(WorkerRequest request, const QString &token, bool automatic) {
  if (token.isEmpty() || contains(token) || pending_.size() >= 20)
    return false;
  Entry e{std::move(request), token, automatic};
  if (automatic)
    pending_.append(e);
  else {
    pending_.prepend(e);
    if (active_ && active_->running() && current_.automatic)
      active_->cancel();
  }
  emit changed();
  drain();
  return true;
}
void ScientificQueue::drain() {
  if (completing_ || (active_ && active_->running()) || pending_.isEmpty())
    return;
  if (active_)
    active_->deleteLater();
  current_ = pending_.takeFirst();
  auto *job = new WorkerJob(root_, this);
  active_ = job;
  connect(job, &WorkerJob::changed, this, &ScientificQueue::changed);
  connect(job, &WorkerJob::finished, this, [this, job](bool success) {
    completing_ = true;
    emit finished(current_.token, job->directory(), success, current_.automatic, job->error());
    completing_ = false;
    emit changed();
    QTimer::singleShot(0, this, &ScientificQueue::drain);
  });
  if (job->start(current_.request))
    emit started(current_.token, job, current_.automatic);
  else {
    completing_ = true;
    emit finished(current_.token, job->directory(), false, current_.automatic, job->error());
    completing_ = false;
    emit changed();
    QTimer::singleShot(0, this, &ScientificQueue::drain);
  }
}
void ScientificQueue::cancel(const QString &token) {
  for (qsizetype i = pending_.size(); i-- > 0;)
    if (pending_[i].token == token) {
      const auto e = pending_.takeAt(i);
      emit finished(e.token, {}, false, e.automatic, "Queued analysis cancelled");
    }
  if (active_ && active_->running() && current_.token == token)
    active_->cancel();
  emit changed();
}
void ScientificQueue::cancelAll() {
  const auto pending = pending_;
  pending_.clear();
  for (const auto &e : pending)
    emit finished(e.token, {}, false, e.automatic, "Queued work cancelled");
  if (active_ && active_->running())
    active_->cancel();
  emit changed();
}
} // namespace godeye

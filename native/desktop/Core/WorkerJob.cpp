#include "WorkerJob.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QUuid>
#include <cmath>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <unistd.h>

namespace godeye {
namespace {
constexpr qsizetype maximumLine = 128 * 1024, maximumLog = 1024 * 1024;
QString now() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}
bool atomicWrite(const QString &path, const QByteArray &bytes) {
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
int acquire(const QString &directory) {
  const auto path = QFile::encodeName(directory + "/.run.lock");
  int fd = ::open(path.constData(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) != 0) {
    ::close(fd);
    fd = -1;
  }
  return fd;
}
bool active(const QString &state) {
  return state == "queued" || state == "validating" || state == "processing" ||
         state == "exporting";
}
bool transition(const QString &from, const QString &to) {
  if (from == to || to == "failed")
    return true;
  if (from == "queued")
    return to == "validating";
  if (from == "validating")
    return to == "processing" || to == "completed";
  if (from == "processing")
    return to == "exporting";
  if (from == "exporting")
    return to == "completed";
  return false;
}
} // namespace
WorkerJob::WorkerJob(const QString &jobsDirectory, QObject *parent)
    : QObject(parent), root_(jobsDirectory) {
  connect(&process_, &QProcess::readyReadStandardOutput, this, &WorkerJob::receive);
  connect(&process_, &QProcess::readyReadStandardError, this,
          [this] { diagnose(process_.readAllStandardError()); });
  connect(&process_, &QProcess::finished, this, &WorkerJob::complete);
  connect(&process_, &QProcess::started, this, [this] {
    if (cancelled_)
      cancel();
  });
  connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
      fail("The Python worker could not start. Check the selected executable and permissions.");
      complete(-1, QProcess::CrashExit);
    }
  });
  stopTimer_.setSingleShot(true);
  logTimer_.setSingleShot(true);
  logTimer_.setInterval(250);
  connect(&logTimer_, &QTimer::timeout, this,
          [this] { atomicWrite(directory_ + "/worker.log", log_); });
  connect(&stopTimer_, &QTimer::timeout, this, [this] {
    // Signal only the new session led by this live QProcess. The bundled worker
    // keeps GPT in this session, so forced cancellation also stops descendants.
    if (running() && process_.processId() > 0)
      ::kill(-pid_t(process_.processId()), SIGKILL);
  });
}
WorkerJob::~WorkerJob() {
  // QProcess can emit while its destructor waits; its callbacks must never
  // outlive the record and buffers destroyed before that member.
  disconnect(&process_, nullptr, this, nullptr);
  stopTimer_.stop();
  logTimer_.stop();
  if (process_.state() == QProcess::Starting && !process_.waitForStarted(3000)) {
    process_.kill();
    process_.waitForFinished(2000);
  }
  if (running()) {
    cancel();
    if (!process_.waitForFinished(optical_ ? 4000 : 15000)) {
      if (process_.processId() > 0)
        ::kill(-pid_t(process_.processId()), SIGKILL);
      process_.waitForFinished(2000);
    }
  }
  releaseLock();
}
QString WorkerJob::workerPath(bool optical) {
  const auto app = QCoreApplication::applicationDirPath();
#if defined(Q_OS_MACOS)
  const QString root = QDir(app).absoluteFilePath("../Resources/workers");
#else
  const QString root = QDir(app).absoluteFilePath("../share/godeye/workers");
#endif
  return root + (optical ? "/Optical/sentinel2.py" : "/SAR/sentinel1.py");
}
QString WorkerJob::hostPath() {
  return QCoreApplication::applicationDirPath() + "/GodEyeWorkerHost";
}
bool WorkerJob::start(const WorkerRequest &request) {
  if (running() || !directory_.isEmpty())
    return false;
  const QFileInfo python(request.python), worker(request.worker), input(request.input);
  const QFileInfo host(request.host.isEmpty() ? hostPath() : request.host);
  if (!host.isFile() || !host.isExecutable()) {
    error_ = "A required processing component is missing. Rebuild or reinstall the desktop app.";
    emit changed();
    return false;
  }
  const auto document = QJsonDocument(request.document).toJson();
  if (!python.isFile() || !python.isExecutable() || !worker.isFile() ||
      (request.document.isEmpty() && !input.exists()) || document.size() > 1024 * 1024 ||
      (!request.document.isEmpty() && request.operation != "render" &&
       request.operation != "compare") ||
      !QStringList{"inspect", "process", "compare", "render"}.contains(request.operation) ||
      (request.operation == "process" &&
       (!QFileInfo(request.gpt).isFile() || !QFileInfo(request.gpt).isExecutable()))) {
    error_ = "Select an executable Python, the bundled worker and an existing input. Raw GRD "
             "processing also needs executable SNAP GPT.";
    emit changed();
    return false;
  }
  if (!QDir().mkpath(root_) || QFileInfo(root_).isSymLink()) {
    error_ = "The job library cannot be created safely.";
    emit changed();
    return false;
  }
  directory_ =
      QDir(root_).canonicalPath() + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!QDir().mkdir(directory_) || (lock_ = acquire(directory_)) < 0) {
    error_ = "A new job directory cannot be locked.";
    emit changed();
    return false;
  }
  QFile::setPermissions(directory_,
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
  optical_ = request.optical;
  record_ = {
      {"version", 1},
      {"id", QFileInfo(directory_).fileName()},
      {"state", "queued"},
      {"stage", "Queued for local processing"},
      {"createdAt", now()},
      {"operation", request.operation},
      {"pythonExecutable", python.absoluteFilePath()},
      {"inputName", request.document.isEmpty() ? input.fileName()
                    : request.operation == "compare"
                        ? request.document["site"].toObject()["name"].toString()
                        : request.document["scene"].toObject()["id"].toString(
                              QFileInfo(request.document["originalInput"].toString()).fileName())},
      {"optical", optical_},
      {"progress", QJsonValue::Null}};
  if (!persist()) {
    fail("The job record could not be saved atomically.");
    releaseLock();
    return false;
  }
  const auto output =
      request.operation == "inspect" ? directory_ + "/inspection.json" : directory_ + "/product";
  const auto inputPath =
      request.document.isEmpty() ? input.absoluteFilePath() : directory_ + "/request.json";
  if (!request.document.isEmpty() && !atomicWrite(inputPath, document)) {
    fail("The processing request could not be saved atomically.");
    releaseLock();
    return false;
  }
  QStringList arguments{
      "-I",       "-B",  worker.absoluteFilePath(), request.operation, "--input", inputPath,
      "--output", output};
  if (request.operation == "process")
    arguments << "--gpt" << request.gpt << "--options"
              << QString::fromUtf8(QJsonDocument(request.options).toJson(QJsonDocument::Compact));
  process_.setProgram(host.absoluteFilePath());
  process_.setArguments(QStringList{optical_ ? "3" : "12", python.absoluteFilePath()} + arguments);
  process_.setWorkingDirectory(directory_);
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert("PYTHONUNBUFFERED", "1");
  environment.insert("GODEYE_WORKER_OWN_SESSION", "1");
  process_.setProcessEnvironment(environment);
  process_.setUnixProcessParameters(QProcess::UnixProcessFlag::ResetSignalHandlers |
                                    QProcess::UnixProcessFlag::CreateNewSession);
  const int inheritedLock = lock_;
  process_.setChildProcessModifier([inheritedLock] { fcntl(inheritedLock, F_SETFD, 0); });
  elapsed_.start();
  process_.start();
  emit changed();
  return true;
}
void WorkerJob::diagnose(const QByteArray &bytes) {
  log_.append(bytes.right(maximumLog));
  if (log_.size() > maximumLog)
    log_.remove(0, log_.size() - maximumLog);
  if (!logTimer_.isActive())
    logTimer_.start();
}
void WorkerJob::receive() {
  pending_.append(process_.read(64 * 1024));
  if (process_.bytesAvailable() > 0)
    QTimer::singleShot(0, this, &WorkerJob::receive);
  while (!pending_.isEmpty()) {
    const auto newline = pending_.indexOf('\n');
    if ((newline < 0 && pending_.size() > maximumLine) || newline > maximumLine) {
      pending_.clear();
      protocolFailed_ = true;
      fail("Worker status exceeded the 128 KiB line limit.");
      cancel();
      return;
    }
    if (newline < 0)
      return;
    const auto line = pending_.first(newline);
    pending_.remove(0, newline + 1);
    diagnose(line + '\n');
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(line, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
      continue;
    const auto event = doc.object();
    const auto state = event["state"].toString(), stage = event["stage"].toString();
    const auto progress = event["progress"];
    if (!QStringList{"queued", "validating", "processing", "exporting", "completed", "failed"}
             .contains(state) ||
        !transition(record_["state"].toString(), state) || stage.isEmpty() || stage.size() > 2000 ||
        !(progress.isNull() || progress.isUndefined() ||
          (progress.isDouble() && std::isfinite(progress.toDouble()) && progress.toDouble() >= 0 &&
           progress.toDouble() <= 1)))
      continue;
    record_["state"] = state;
    record_["stage"] = stage;
    record_["progress"] = progress.isUndefined() ? QJsonValue::Null : progress;
    record_["elapsedSeconds"] = elapsed_.elapsed() / 1000.0;
    if (state == "failed")
      error_ = stage.left(600);
    if (!persist()) {
      protocolFailed_ = true;
      fail("Job progress could not be saved; processing was stopped.");
      cancel();
    }
    emit changed();
  }
}
bool WorkerJob::persist() {
  record_["updatedAt"] = now();
  return atomicWrite(directory_ + "/run.json", QJsonDocument(record_).toJson());
}
void WorkerJob::fail(const QString &message) {
  error_ = message;
  record_["state"] = "failed";
  record_["stage"] = message;
  record_["progress"] = QJsonValue::Null;
  if (!directory_.isEmpty())
    persist();
  emit changed();
}
void WorkerJob::cancel() {
  cancelled_ = true;
  if (running()) {
    record_["stage"] = "Cancellation requested; waiting for worker cleanup";
    record_["progress"] = QJsonValue::Null;
    persist();
    // During Starting, a forked Qt child has not exec'd the owned host yet.
    // Defer SIGTERM until started() so inherited application handlers cannot run.
    if (process_.state() == QProcess::Running) {
      process_.terminate();
      stopTimer_.start(optical_ ? 3000 : 12000);
    }
    emit changed();
  }
}
void WorkerJob::complete(int code, QProcess::ExitStatus status) {
  if (finished_)
    return;
  finished_ = true;
  stopTimer_.stop();
  receive();
  while (process_.bytesAvailable() > 0)
    receive();
  diagnose(process_.readAllStandardError());
  const auto result = readRecord(resultFile());
  const bool resultValid =
      !result.isEmpty() && (record_["operation"] == "inspect" || result["state"] == "completed");
  const bool success = !cancelled_ && !protocolFailed_ && status == QProcess::NormalExit &&
                       code == 0 && record_["state"] == "completed" && resultValid;
  if (!success)
    fail(protocolFailed_    ? error_
         : cancelled_       ? "Processing cancelled; inspect retained diagnostics before retrying."
         : error_.isEmpty() ? "Worker exited without a completed result. Check its diagnostic log."
                            : error_);
  record_["exitCode"] = code;
  if (!success && status == QProcess::CrashExit && !cancelled_)
    fail("Processing host stopped unexpectedly; worker files were preserved for recovery.");
  if (!success && (cancelled_ || status == QProcess::NormalExit))
    cleanupIncomplete();
  record_["elapsedSeconds"] = elapsed_.isValid() ? elapsed_.elapsed() / 1000.0 : 0;
  persist();
  logTimer_.stop();
  atomicWrite(directory_ + "/worker.log", log_);
  releaseLock();
  emit changed();
  emit finished(success);
}
void WorkerJob::cleanupIncomplete() {
  const auto product = directory_ + "/product";
  if (QFileInfo(product).isSymLink() || QDir(product).canonicalPath() != product)
    return;
  auto result = readRecord(product + "/result.json");
  // A completed atomic worker result remains valid even if cancellation raced
  // process shutdown. Never turn it into a partial-output cleanup target.
  if (result["state"] == "completed")
    return;
  QStringList names{"preview.png", "true-color.tif", "source-values.tif", "selected.tmp.tif",
                    "result.tmp"};
  if (record_["operation"] == "compare")
    names << "Before.png" << "After.png" << "Change.png"
          << "delta-db.tif" << "candidate-mask.tif" << "change-display.tif"
          << "corrected-linear-power.tif" << "quality.json";
  for (const auto &band : {"VV", "VH", "Composite"})
    for (const auto &suffix : {"-preview.png", "-display.tif", "-sigma0.tif", "-db.tif"})
      names.append(QString(band) + suffix);
  for (const auto &name : names)
    QFile::remove(product + "/" + name);
  const auto temporary = QDir(product).entryInfoList(
      {".sar-work-*"}, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
  for (const auto &entry : temporary)
    if (entry.isSymLink() || entry.isFile())
      QFile::remove(entry.filePath());
    else if (entry.isDir())
      QDir(entry.filePath()).removeRecursively();
  if (!result.isEmpty()) {
    result["state"] = "failed";
    result["error"] = error_;
    if (result.contains("outputs"))
      result["outputs"] = QJsonArray{};
    atomicWrite(product + "/result.json", QJsonDocument(result).toJson());
  }
}
void WorkerJob::releaseLock() {
  if (lock_ >= 0) {
    ::close(lock_);
    lock_ = -1;
  }
}
QJsonObject WorkerJob::readRecord(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
    return {};
  QJsonParseError parse;
  const auto doc = QJsonDocument::fromJson(file.read(1024 * 1024 + 1), &parse);
  return parse.error == QJsonParseError::NoError && doc.isObject() ? doc.object() : QJsonObject{};
}
bool WorkerJob::recover(const QString &directory, QString *error) {
  const QFileInfo info(directory);
  if (info.isSymLink() || !info.isDir() || QUuid(info.fileName()).isNull())
    return false;
  auto record = readRecord(directory + "/run.json");
  if (record["version"] != 1 || record["id"] != info.fileName() ||
      !active(record["state"].toString()))
    return false;
  const int lock = acquire(directory);
  if (lock < 0) {
    if (error)
      *error = "A worker still owns this job; its files were preserved.";
    return false;
  }
  // Recovery preserves all worker data. Retrying always creates a new directory;
  // no saved PID or guessed process name authorizes deletion of an old product.
  record["state"] = "failed";
  record["stage"] =
      "Processing was interrupted. Retained files require inspection; retry in a new job.";
  record["progress"] = QJsonValue::Null;
  record["updatedAt"] = now();
  const bool saved = atomicWrite(directory + "/run.json", QJsonDocument(record).toJson());
  ::close(lock);
  if (!saved && error)
    *error = "Interrupted job could not be marked failed; original files were preserved.";
  return saved;
}
} // namespace godeye

#include "CameraViewer.h"
#include "Core/DomainNetwork.h"
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <csignal>
namespace godeye {
CameraViewer::Clip::~Clip() {
  if (!path.isEmpty())
    QFile::remove(path);
}
QString CameraViewer::decoderPath() {
  const auto bundled = QCoreApplication::applicationDirPath() + "/ffmpeg";
  if (QFileInfo(bundled).isExecutable())
    return bundled;
#ifdef Q_OS_MACOS
  for (const QString path : {"/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg"})
    if (QFileInfo(path).isExecutable())
      return path;
#endif
  return QStandardPaths::findExecutable("ffmpeg");
}
QString CameraViewer::ownedClipPath() const {
  return clip_ ? clip_->path : QString{};
}
CameraViewer::CameraViewer(DomainRecord record, QWidget *parent)
    : QDialog(parent), record_(std::move(record)), video_(new QLabel), status_(new QLabel),
      load_(new QPushButton("Retrieve and play operator clip")), stop_(new QPushButton("Stop")) {
  setAttribute(Qt::WA_DeleteOnClose);
  setObjectName("cameraViewer");
  setWindowFlag(Qt::Window, true);
  setWindowTitle(record_.label);
  resize(700, 520);
  load_->setObjectName("cameraClipLoad");
  status_->setObjectName("cameraClipStatus");
  auto *layout = new QVBoxLayout(this);
  video_->setAlignment(Qt::AlignCenter);
  video_->setObjectName("cameraClipImage");
  video_->setMinimumSize(320, 240);
  layout->addWidget(video_, 1);
  status_->setWordWrap(true);
  layout->addWidget(status_);
  auto *buttons = new QHBoxLayout;
  buttons->addWidget(load_);
  buttons->addWidget(stop_);
  pause_ = new QPushButton("Pause");
  pause_->setObjectName("cameraClipPause");
  pause_->setEnabled(false);
  buttons->addWidget(pause_);
  seek_ = new QDoubleSpinBox;
  seek_->setRange(0, 600);
  seek_->setDecimals(1);
  seek_->setSuffix(" s");
  seek_->setObjectName("cameraClipSeekSeconds");
  buttons->addWidget(seek_);
  seekButton_ = new QPushButton("Seek");
  seekButton_->setObjectName("cameraClipSeek");
  seekButton_->setEnabled(false);
  buttons->addWidget(seekButton_);
  position_ = new QLabel("Clip position unavailable");
  layout->addWidget(position_);
  buttons->addStretch();
  layout->addLayout(buttons);
  auto *credit = new QLabel(
      record_.source +
      " · actual operator clip · acquisition time unknown; media may be delayed. At most 24 MiB is "
      "retrieved. Display is bounded to 1280×720 and 12 frames/s; no audio.");
  credit->setWordWrap(true);
  layout->addWidget(credit);
  connect(load_, &QPushButton::clicked, this, &CameraViewer::load);
  connect(stop_, &QPushButton::clicked, this, &CameraViewer::stop);
  connect(pause_, &QPushButton::clicked, this, [this] {
#ifdef Q_OS_UNIX
    if (decoder_.state() != QProcess::Running)
      return;
    const auto pid = decoder_.processId();
    if (pid > 1 && ::kill(pid_t(pid), paused_ ? SIGCONT : SIGSTOP) == 0) {
      paused_ = !paused_;
      pause_->setText(paused_ ? "Resume" : "Pause");
    }
#endif
  });
  connect(seekButton_, &QPushButton::clicked, this, [this] {
    if (!clip_)
      return;
    seekStart_ = seek_->value();
    ++generation_;
    paused_ = false;
    pause_->setText("Pause");
    if (decoder_.state() != QProcess::NotRunning) {
      restart_ = true;
      decoder_.kill();
    } else
      decode();
  });
  connect(&decoder_, &QProcess::readyReadStandardOutput, this, &CameraViewer::readFrames);
  connect(&decoder_, &QProcess::readyReadStandardError, this,
          [this] { decoder_.readAllStandardError(); });
  connect(&decoder_, &QProcess::started, this, [this] {
    if (decoderGeneration_ != generation_)
      decoder_.kill();
  });
  connect(&decoder_, &QProcess::errorOccurred, this, [this] {
    if (decoderGeneration_ == generation_)
      status_->setText("The owned FFmpeg decoder could not start or stopped unexpectedly. Install "
                       "FFmpeg or use a package containing it.");
  });
  connect(&decoder_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus state) {
    if (restart_) {
      restart_ = false;
      decode();
      return;
    }
    readFrames();
    paused_ = false;
    pause_->setText("Pause");
    pause_->setEnabled(false);
    if (decoderGeneration_ == generation_) {
      if (state != QProcess::NormalExit || code != 0 || decoded_.isNull())
        status_->setText("The installed FFmpeg decoder could not decode this bounded operator "
                         "clip. No replacement frames were generated.");
      else
        status_->setText(status_->text() + " · clip ended");
    }
  });
  status_->setText(decoderPath().isEmpty()
                       ? "FFmpeg is unavailable. Camera stills work independently; install FFmpeg "
                         "or use a package containing it for native clip playback."
                       : "No clip loaded. Retrieve an actual operator clip to inspect it.");
}
CameraViewer::~CameraViewer() {
  stop();
  if (watcher_) {
    disconnect(watcher_, nullptr, this, nullptr);
    watcher_->waitForFinished();
  }
  if (decoder_.state() != QProcess::NotRunning) {
    decoder_.kill();
    decoder_.waitForFinished(3000);
  }
  clip_.reset();
}
void CameraViewer::closeEvent(QCloseEvent *event) {
  stop();
  QDialog::closeEvent(event);
}
void CameraViewer::stop() {
  ++generation_;
  if (cancel_)
    cancel_->store(true);
  if (decoder_.state() != QProcess::NotRunning)
    decoder_.kill();
  restart_ = false;
  paused_ = false;
  seekStart_ = 0;
  decodedFrames_ = 0;
  pause_->setText("Pause");
  pause_->setEnabled(false);
  seekButton_->setEnabled(false);
  position_->setText("Clip position unavailable");
  frameBuffer_.clear();
  decoded_ = {};
  video_->clear();
  clip_.reset();
  status_->setText("Stopped. No camera acquisition time has been inferred.");
  load_->setEnabled(!watcher_);
}
void CameraViewer::load() {
  if (watcher_ || decoder_.state() != QProcess::NotRunning)
    return;
  stop();
  if (decoderPath().isEmpty()) {
    status_->setText("FFmpeg is unavailable. Camera stills work independently; install FFmpeg or "
                     "use a package containing it.");
    return;
  }
  const auto url = DomainParsers::cameraURL(record_.details["Video URL"].toString());
  if (url.isEmpty() || !url.path().endsWith(".mp4")) {
    status_->setText("No supported operator clip URL.");
    return;
  }
  const auto generation = ++generation_;
  auto canceled = std::make_shared<std::atomic_bool>(false);
  cancel_ = canceled;
  load_->setEnabled(false);
  status_->setText("Retrieving the actual operator clip…");
  auto *watcher = new QFutureWatcher<std::shared_ptr<Clip>>(this);
  watcher_ = watcher;
  connect(watcher, &QFutureWatcher<std::shared_ptr<Clip>>::finished, this,
          [this, watcher, generation] {
            auto result = watcher->result();
            watcher->deleteLater();
            watcher_ = nullptr;
            cancel_.reset();
            load_->setEnabled(true);
            if (generation != generation_)
              return;
            if (!result->error.isEmpty()) {
              status_->setText(result->error);
              return;
            }
            clip_ = result;
            seekButton_->setEnabled(true);
            status_->setText(
                "Retrieved UTC: " + result->retrievedAt.toUTC().toString(Qt::ISODateWithMs) +
                " · acquisition time unknown");
            decode();
          });
  watcher->setFuture(QtConcurrent::run([url, canceled] {
    auto clip = std::make_shared<Clip>();
    auto data = domainFetch(url, 24 * 1024 * 1024, canceled, {}, {}, false);
    if (!data.error.isEmpty()) {
      clip->error = data.error;
      return clip;
    }
    if (canceled->load()) {
      clip->error = "Canceled.";
      return clip;
    }
    if (data.bytes.size() < 12 || data.bytes.mid(4, 4) != "ftyp") {
      clip->error = "Operator response is outside the supported MP4 clip contract.";
      return clip;
    }
    QTemporaryFile file(QDir::tempPath() + "/godeye-camera-XXXXXX.mp4");
    if (!file.open()) {
      clip->error = "Could not create an owned temporary clip file.";
      return clip;
    }
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (file.write(data.bytes) != data.bytes.size() || !file.flush() || canceled->load()) {
      clip->error = "Clip retrieval was canceled or its temporary file could not be written.";
      return clip;
    }
    file.setAutoRemove(false);
    clip->path = file.fileName();
    clip->retrievedAt = QDateTime::currentDateTimeUtc();
    return clip;
  }));
}
void CameraViewer::decode() {
  if (!clip_)
    return;
  decoderGeneration_ = generation_;
  decodedFrames_ = 0;
  pause_->setEnabled(true);
  frameBuffer_.clear();
  decoded_ = {};
  // Decode only our bounded local file. MOV external data references remain disabled.
  decoder_.setProgram(decoderPath());
  decoder_.setArguments(
      {"-nostdin",
       "-hide_banner",
       "-loglevel",
       "error",
       "-max_alloc",
       "67108864",
       "-threads",
       "2",
       "-filter_threads",
       "2",
       "-protocol_whitelist",
       "file,pipe",
       "-enable_drefs",
       "0",
       "-use_absolute_path",
       "0",
       "-max_pixels",
       "16777216",
       "-re",
       "-ss",
       QString::number(seekStart_, 'f', 3),
       "-i",
       clip_->path,
       "-map",
       "0:v:0",
       "-an",
       "-sn",
       "-dn",
       "-vf",
       "scale=w=min(1280\\,iw):h=min(720\\,ih):force_original_aspect_ratio=decrease,fps=12",
       "-threads",
       "2",
       "-c:v",
       "ppm",
       "-f",
       "image2pipe",
       "pipe:1"});
  decoder_.setProcessChannelMode(QProcess::SeparateChannels);
  decoder_.start();
  const auto generation = generation_;
  QTimer::singleShot(600000, this, [this, generation] {
    if (generation == generation_ && decoder_.state() != QProcess::NotRunning) {
      decoder_.kill();
      status_->setText("Clip decoding reached the ten-minute duration limit.");
    }
  });
}
std::optional<bool> CameraViewer::takeFrame(QByteArray &buffer, QImage &frame) {
  if (buffer.size() < 3)
    return {};
  if (!buffer.startsWith("P6\n"))
    return false;
  const auto end = buffer.indexOf("\n255\n");
  if (end < 0)
    return buffer.size() > 128 ? std::optional<bool>(false) : std::nullopt;
  const auto header = buffer.left(end + 5);
  static const QRegularExpression format("^P6\\n([0-9]{1,4}) ([0-9]{1,4})\\n255\\n$");
  const auto match = format.match(QString::fromLatin1(header));
  if (!match.hasMatch())
    return false;
  const int width = match.captured(1).toInt(), height = match.captured(2).toInt();
  if (width < 1 || height < 1 || width > 1280 || height > 720)
    return false;
  const qsizetype length = qsizetype(width) * height * 3;
  if (buffer.size() - header.size() < length)
    return {};
  frame = QImage(reinterpret_cast<const uchar *>(buffer.constData() + header.size()), width, height,
                 width * 3, QImage::Format_RGB888)
              .copy();
  buffer.remove(0, header.size() + length);
  return !frame.isNull();
}
void CameraViewer::readFrames() {
  if (decoderGeneration_ != generation_) {
    decoder_.readAllStandardOutput();
    return;
  }
  frameBuffer_ += decoder_.read(128 * 1024);
  if (frameBuffer_.size() > 8 * 1024 * 1024) {
    decoder_.kill();
    status_->setText("Decoder output exceeded its bounded frame buffer.");
    return;
  }
  int consumed = 0;
  for (int i = 0; i < 2; ++i) {
    QImage frame;
    auto parsed = takeFrame(frameBuffer_, frame);
    if (!parsed)
      break;
    if (!*parsed) {
      decoder_.kill();
      status_->setText("Decoder returned an unsupported or oversized frame.");
      return;
    }
    ++consumed;
    decoded_ = frame;
    ++decodedFrames_;
    position_->setText(
        "Clip playback offset: " + QString::number(seekStart_ + decodedFrames_ / 12.0, 'f', 1) +
        " s · source acquisition time unknown");
    video_->setPixmap(QPixmap::fromImage(frame).scaled(video_->size(), Qt::KeepAspectRatio,
                                                       Qt::SmoothTransformation));
    emit frameDecoded(frame);
  }
  if (decoder_.bytesAvailable() > 0 || (consumed == 2 && !frameBuffer_.isEmpty()))
    QTimer::singleShot(1, this, &CameraViewer::readFrames);
}
} // namespace godeye

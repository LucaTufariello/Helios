#pragma once
#include "Core/DomainFeeds.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QImage>
#include <QProcess>
#include <atomic>
#include <memory>
class QLabel;
class QPushButton;
class QDoubleSpinBox;
namespace godeye {
class CameraViewer : public QDialog {
  Q_OBJECT
public:
  explicit CameraViewer(DomainRecord record, QWidget *parent = nullptr);
  ~CameraViewer() override;
  QImage decodedFrame() const {
    return decoded_;
  }
  QString ownedClipPath() const;
  static QString decoderPath();
  static std::optional<bool> takeFrame(QByteArray &buffer, QImage &frame);
signals:
  void frameDecoded(QImage frame);

private:
  struct Clip {
    QString path, error;
    QDateTime retrievedAt;
    ~Clip();
  };
  DomainRecord record_;
  QProcess decoder_;
  QByteArray frameBuffer_;
  QImage decoded_;
  QLabel *video_;
  QLabel *status_, *position_;
  QPushButton *load_, *stop_, *pause_, *seekButton_;
  QDoubleSpinBox *seek_;
  QFutureWatcher<std::shared_ptr<Clip>> *watcher_ = nullptr;
  std::shared_ptr<std::atomic_bool> cancel_;
  std::shared_ptr<Clip> clip_;
  quint64 generation_ = 0;
  quint64 decoderGeneration_ = 0;
  quint64 decodedFrames_ = 0;
  bool paused_ = false, restart_ = false;
  double seekStart_ = 0;
  void load();
  void stop();
  void decode();
  void readFrames();
  void closeEvent(QCloseEvent *event) override;
};
} // namespace godeye

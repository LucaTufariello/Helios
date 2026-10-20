#include "InferenceWindow.h"
#include "WorkspaceStyle.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QtConcurrent>
namespace godeye {
InferenceWindow::InferenceWindow(const QString &profile, QWidget *parent)
    : QMainWindow(parent), profile_(profile) {
  setWindowTitle("Local object assistance");
  resize(1000, 680);
  auto *body = new QWidget(this);
  auto *columns = new QHBoxLayout(body);
  auto *controls = new QWidget;
  auto *layout = new QVBoxLayout(controls);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(12);
  design::heading(layout, "ANALYST ASSISTANCE", "Local image assistance");
  columns->addWidget(controls, 3);
  auto *review = new QWidget;
  auto *reviewLayout = new QVBoxLayout(review);
  reviewLayout->setContentsMargins(0, 0, 0, 0);
  review->setMinimumWidth(220);
  columns->addWidget(review, 2);
  auto *disclosure =
      new QLabel("Analyze a linked optical PNG preview with an explicit local ONNX model contract. "
                 "CPU analysis runs locally. Scores are model scores; source resolution and "
                 "preview accuracy still apply. "
                 "No aerial accuracy, construction or damage capability has been validated.");
  disclosure->setWordWrap(true);
  disclosure->setProperty("role", "secondary");
  layout->addWidget(disclosure);
  source_ = new QLabel("Select linked optical evidence from Mission Control.");
  source_->setWordWrap(true);
  layout->addWidget(source_);
  auto *form = new QFormLayout;
  const auto row = [this](QLineEdit *edit, bool executable) {
    auto *container = new QWidget;
    auto *box = new QHBoxLayout(container);
    box->setContentsMargins(0, 0, 0, 0);
    auto *choose = new QPushButton("Choose…");
    box->addWidget(edit);
    box->addWidget(choose);
    connect(choose, &QPushButton::clicked, this, [this, edit, executable] {
      const auto path = QFileDialog::getOpenFileName(
          this, executable ? "Select inference Python" : "Select local model contract",
          edit->text(), executable ? QString{} : "Model contract (*.json)");
      if (!path.isEmpty())
        edit->setText(path);
    });
    return container;
  };
  python_ = new QLineEdit(QSettings(profile_ + "/inference.ini", QSettings::IniFormat)
                              .value("inference/python")
                              .toString());
  contract_ = new QLineEdit(QSettings(profile_ + "/inference.ini", QSettings::IniFormat)
                                .value("inference/contract")
                                .toString());
  python_->setObjectName("inferencePython");
  contract_->setObjectName("inferenceContract");
  threshold_ = new QDoubleSpinBox;
  threshold_->setRange(0, 1);
  threshold_->setSingleStep(0.05);
  threshold_->setValue(0.5);
  threshold_->setObjectName("inferenceThreshold");
  form->addRow("Inference Python", row(python_, true));
  form->addRow("Model contract", row(contract_, false));
  form->addRow("Minimum model score", threshold_);
  layout->addLayout(form);
  auto *actions = new QHBoxLayout;
  analyze_ = new QPushButton("Analyze preview");
  cancel_ = new QPushButton("Cancel analysis");
  analyze_->setObjectName("inferenceAnalyze");
  cancel_->setObjectName("inferenceCancel");
  actions->addWidget(analyze_);
  actions->addWidget(cancel_);
  actions->addStretch();
  layout->addLayout(actions);
  status_ = new QLabel("Choose an inference Python with the optional dependencies and a model "
                       "contract. No model is downloaded automatically.");
  status_->setWordWrap(true);
  status_->setObjectName("inferenceStatus");
  layout->addWidget(status_);
  proposals_ = new QListWidget;
  proposals_->setSelectionMode(QAbstractItemView::SingleSelection);
  proposals_->setObjectName("inferenceProposals");
  layout->addWidget(proposals_);
  previewCaption_ = new QLabel("Analyzed preview · red outline marks the selected model proposal");
  previewCaption_->setWordWrap(true);
  reviewLayout->addWidget(previewCaption_);
  preview_ = new QLabel("The analyzed preview and selected box will appear here.");
  preview_->setMinimumHeight(180);
  preview_->setMaximumHeight(230);
  preview_->setAlignment(Qt::AlignCenter);
  preview_->setObjectName("inferencePreview");
  reviewLayout->addWidget(preview_);
  reviewLayout->addStretch();
  accept_ = new QPushButton("Add as an unreviewed observation");
  accept_->setObjectName("inferenceAccept");
  layout->addWidget(accept_);
  setCentralWidget(body);
  connect(analyze_, &QPushButton::clicked, this, &InferenceWindow::start);
  connect(cancel_, &QPushButton::clicked, this, &InferenceWindow::cancel);
  connect(proposals_, &QListWidget::itemSelectionChanged, this, &InferenceWindow::refresh);
  connect(&previewLoad_, &QFutureWatcher<QImage>::finished, this, [this] {
    previewImage_ = previewLoad_.result();
    if (previewImage_.isNull())
      status_->setText("The analyzed preview is unavailable or changed. Retry analysis before "
                       "accepting a proposal.");
    refresh();
  });
  connect(accept_, &QPushButton::clicked, this, [this] {
    if (missionID_ != runMissionID_ || evidenceID_ != runEvidenceID_ || busy() ||
        layer_.preview != runPreview_ || layer_.manifestHash != runManifestHash_)
      return;
    for (auto *item : proposals_->selectedItems()) {
      auto proposal = item->data(Qt::UserRole).toJsonObject();
      if (proposal.isEmpty())
        continue;
      markProposalPending(proposal["id"].toString());
      emit proposalAccepted(runMissionID_, proposal);
    }
    refresh();
  });
  refresh();
}
bool InferenceWindow::busy() const {
  return !pending_.isEmpty() || previewLoad_.isRunning() || (job_ && job_->running());
}
void InferenceWindow::setEvidence(const ProductLayer &layer, const QString &evidenceID,
                                  const QString &missionID) {
  layer_ = layer;
  evidenceID_ = evidenceID;
  missionID_ = missionID;
  source_->setText(QString("Current linked evidence: %1 · %2 × %3 preview pixels · %4")
                       .arg(layer.label)
                       .arg(layer.pixelSize.width())
                       .arg(layer.pixelSize.height())
                       .arg(layer.resolution > 0
                                ? QString("%1 m source spacing").arg(layer.resolution)
                                : "source spacing unknown"));
  refresh();
}
void InferenceWindow::invalidateEvidence() {
  evidenceID_.clear();
  missionID_.clear();
  source_->setText("Select linked optical evidence from Mission Control.");
  refresh();
}
void InferenceWindow::cancel() {
  if (job_)
    job_->cancel();
}
void InferenceWindow::markProposalPending(const QString &id) {
  if (!id.isEmpty()) {
    pending_.insert(id);
    status_->setText("Checking the source preview and mission link before adding this proposal…");
    refresh();
  }
}
void InferenceWindow::resolveProposal(const QString &id, bool saved, const QString &message) {
  pending_.remove(id);
  for (int i = 0; i < proposals_->count(); ++i) {
    auto *item = proposals_->item(i);
    if (item->data(Qt::UserRole).toJsonObject()["id"] != id)
      continue;
    if (saved) {
      item->setData(Qt::UserRole, QVariant{});
      item->setText(item->text() + " · added as an unreviewed observation");
      item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    } else {
      status_->setText(
          message.isEmpty()
              ? "The proposal could not be added. Its source link must still exist in this mission."
              : message);
    }
  }
  refresh();
}
void InferenceWindow::start() {
  if (busy())
    return;
  const auto request =
      inferenceRequest(layer_, contract_->text(), evidenceID_, missionID_, threshold_->value());
  if (request.isEmpty()) {
    status_->setText("Select valid linked optical evidence before analyzing its preview.");
    return;
  }
  proposals_->clear();
  previewImage_ = {};
  preview_->setPixmap({});
  preview_->setText("Waiting for the analyzed source preview…");
  previewCaption_->setText("Analyzed preview: " + layer_.label +
                           " · Red outline: selected model proposal");
  runEvidenceID_ = evidenceID_;
  runMissionID_ = missionID_;
  runPreview_ = layer_.preview;
  runManifestHash_ = layer_.manifestHash;
  QSettings(profile_ + "/inference.ini", QSettings::IniFormat)
      .setValue("inference/python", python_->text());
  QSettings(profile_ + "/inference.ini", QSettings::IniFormat)
      .setValue("inference/contract", contract_->text());
  job_ = std::make_unique<WorkerJob>(profile_ + "/inference/jobs");
  connect(job_.get(), &WorkerJob::changed, this, [this] {
    status_->setText(job_->error().isEmpty() ? job_->record()["stage"].toString() : job_->error());
    refresh();
  });
  connect(job_.get(), &WorkerJob::finished, this, [this](bool success) {
    if (success) {
      QString error;
      const auto result = WorkerJob::readRecord(job_->resultFile());
      const auto values = inferenceProposals(result, runMissionID_, runEvidenceID_, &error);
      if (!error.isEmpty()) {
        status_->setText(error);
      } else {
        for (const auto &value : values) {
          const auto proposal = value.toObject();
          auto *item = new QListWidgetItem(
              QString("%1 · model score %2 · %3, %4")
                  .arg(proposal["title"].toString())
                  .arg(proposal["modelScore"].toDouble(), 0, 'f', 3)
                  .arg(proposal["coordinate"].toObject()["latitude"].toDouble(), 0, 'f', 6)
                  .arg(proposal["coordinate"].toObject()["longitude"].toDouble(), 0, 'f', 6),
              proposals_);
          item->setData(Qt::UserRole, proposal);
        }
        status_->setText(QString("CPU analysis finished: %1 proposals. Review against the source "
                                 "image before adding. Provenance: %2")
                             .arg(values.size())
                             .arg(job_->resultFile()));
        const auto previewPath = job_->directory() + "/product/preview.png";
        const auto expectedHash = result["sourceSHA256"].toString().toLatin1();
        previewLoad_.setFuture(QtConcurrent::run([previewPath, expectedHash] {
          QFile input(previewPath);
          if (!input.open(QIODevice::ReadOnly) || input.size() > 32 * 1024 * 1024)
            return QImage{};
          auto bytes = input.read(32 * 1024 * 1024 + 1);
          if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != expectedHash)
            return QImage{};
          QBuffer buffer(&bytes);
          buffer.open(QIODevice::ReadOnly);
          QImageReader reader(&buffer, "PNG");
          const auto size = reader.size();
          if (size.width() <= 0 || size.height() <= 0 || size.width() > 2048 ||
              size.height() > 2048)
            return QImage{};
          return reader.read();
        }));
      }
    }
    refresh();
  });
  WorkerRequest worker;
  worker.python = python_->text();
  worker.worker = inferenceWorkerPath();
  worker.operation = "render";
  worker.optical = true;
  worker.document = request;
  if (!job_->start(worker))
    status_->setText(job_->error());
  refresh();
}
void InferenceWindow::refresh() {
  const bool active = busy();
  analyze_->setEnabled(!active && !evidenceID_.isEmpty() && !missionID_.isEmpty());
  cancel_->setEnabled(job_ && job_->running());
  python_->setEnabled(!active);
  contract_->setEnabled(!active);
  threshold_->setEnabled(!active);
  accept_->setEnabled(!active && !proposals_->selectedItems().isEmpty() && !missionID_.isEmpty() &&
                      !previewImage_.isNull() && missionID_ == runMissionID_ &&
                      evidenceID_ == runEvidenceID_ && layer_.preview == runPreview_ &&
                      layer_.manifestHash == runManifestHash_);
  if (!previewImage_.isNull()) {
    auto image =
        previewImage_.scaled(preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!proposals_->selectedItems().isEmpty()) {
      auto *selected = proposals_->selectedItems().first();
      const auto box = selected->data(Qt::UserRole).toJsonObject()["imageBox"].toArray();
      if (box.size() == 4) {
        QPainter painter(&image);
        painter.setPen(QPen(QColor(255, 50, 50), 2));
        painter.drawRect(QRectF(box[0].toDouble() * image.width(),
                                box[1].toDouble() * image.height(),
                                (box[2].toDouble() - box[0].toDouble()) * image.width(),
                                (box[3].toDouble() - box[1].toDouble()) * image.height()));
      }
    }
    preview_->setPixmap(QPixmap::fromImage(image));
  }
}
} // namespace godeye

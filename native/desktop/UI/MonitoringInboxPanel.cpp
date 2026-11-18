#include "MonitoringInboxPanel.h"
#include "Core/Timeline.h"
#include "WorkspaceStyle.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtConcurrent>
namespace helios {
namespace {
QString qualityText(const QJsonObject &result) {
  const auto q = result["quality"].toObject();
  QString text =
      "Registration: " + q["state"].toString("Unavailable") + "\n" + q["reason"].toString() + "\n";
  if (q.contains("maximumVerificationResidualPixels"))
    text += QString("Maximum independent residual: %1 pixels\n")
                .arg(q["maximumVerificationResidualPixels"].toDouble(), 0, 'f', 3);
  const auto translation = q["translationPixels"].toArray();
  if (translation.size() == 2)
    text += QString("Applied row / column translation: %1 / %2 pixels\n")
                .arg(translation[0].toDouble(), 0, 'f', 3)
                .arg(translation[1].toDouble(), 0, 'f', 3);
  text += "Absolute geolocation: unknown · Absolute radiometric accuracy: unknown\n";
  if (result.contains("commonValidFraction"))
    text += QString("Common valid coverage: %1%\n")
                .arg(100 * result["commonValidFraction"].toDouble(), 0, 'f', 1);
  text += "\nSTABLE CONTROLS\n";
  for (const auto v : q["controls"].toArray()) {
    const auto c = v.toObject();
    text += c["role"].toString() + " · " + c["name"].toString() + " · " + c["state"].toString();
    if (c.contains("residualMagnitudePixels"))
      text += QString(" · residual %1 px · correlation %2")
                  .arg(c["residualMagnitudePixels"].toDouble(), 0, 'f', 3)
                  .arg(c["afterCorrelation"].toDouble(), 0, 'f', 3);
    if (c.contains("stableMedianDeltaDb"))
      text += QString("\nStable median Δ %1 dB · MAD %2 dB")
                  .arg(c["stableMedianDeltaDb"].toDouble(), 0, 'f', 3)
                  .arg(c["stableMADDb"].toDouble(), 0, 'f', 3);
    text += "\n" + c["reason"].toString() + "\n";
  }
  text += "\nRelative checks use analyst-selected stable controls. Their thresholds are "
          "provisional; passing does not identify the cause of a radar change.";
  return text;
}
} // namespace

MonitoringInboxPanel::MonitoringInboxPanel(Missions &missions, MonitoringRunner &runner,
                                           QWidget *parent)
    : QWidget(parent), missions_(missions), runner_(runner) {
  setObjectName("monitoringInbox");
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  auto *scroll = new QScrollArea;
  scroll->setObjectName("monitoringInboxScroll");
  scroll->setWidgetResizable(true);
  auto *content = new QWidget;
  auto *layout = new QVBoxLayout(content);
  scroll->setWidget(content);
  outer->addWidget(scroll);
  auto *help = design::label("Acquisition discovery, quality checks and measured radar change "
                             "remain separate events. Review outcomes are analyst judgments.",
                             "secondary");
  help->setWordWrap(true);
  layout->addWidget(help);
  search_ = new QLineEdit;
  search_->setPlaceholderText("Find site, source or review…");
  search_->setClearButtonEnabled(true);
  search_->setObjectName("inboxSearch");
  layout->addWidget(search_);
  auto *filters = new QHBoxLayout;
  kind_ = new QComboBox;
  kind_->addItems({"All event types", "Analysis", "Acquisition"});
  state_ = new QComboBox;
  state_->addItems(
      {"Active reviews", "All reviews", "New", "Investigating", "Closed", "Dismissed", "Snoozed"});
  filters->addWidget(kind_);
  filters->addWidget(state_);
  layout->addLayout(filters);
  list_ = new QListWidget;
  list_->setObjectName("monitoringInboxList");
  list_->setWordWrap(true);
  list_->setMinimumHeight(180);
  layout->addWidget(list_, 1);
  summary_ = design::label({}, "secondary");
  summary_->setWordWrap(true);
  layout->addWidget(summary_);
  auto *buttons = new QGridLayout;
  layout->addLayout(buttons);
  auto action = [&](QString text, QString name, std::function<void()> fn) {
    auto *b = new QPushButton(text);
    b->setObjectName(name);
    const auto n = buttons->count();
    buttons->addWidget(b, n / 2, n % 2);
    connect(b, &QPushButton::clicked, this, [this, fn] { run(fn); });
    return b;
  };
  action("Draft finding from entry", "inboxDraftFinding", [this] {
    const auto e=selected(); if(e.isEmpty()) return;
    emit findingRequested(QJsonObject{{"kind","Monitoring entry"},{"sourceID",e["id"]},
      {"label",e["title"].toString(e["reason"].toString("Monitoring entry"))},{"record",e},
      {"areaID",e["areaID"]},{"manifest",runner_.reportFile(e)},
      {"qualityLimitations","Monitoring event is a candidate for analyst investigation. Registration, coverage and processing limitations in the original entry remain applicable."}});
  });
  action("Review entry…", "inboxReview", [this] { review(); });
  open_ = action("Open measured review", "inboxOpenReview", [this] {
    const auto e = selected();
    if (e["status"] == "Ready")
      emit reviewRequested(runner_.reportFile(e));
  });
  action("Open dossier", "inboxOpenDossier", [this] {
    const auto e = selected();
    if (e["areaID"].isString())
      emit dossierRequested(e["areaID"].toString());
  });
  action("Create follow-up…", "inboxFollowUp", [this] {
    const auto mission = missions_.selectedID(), id = selected()["id"].toString();
    if (id.isEmpty())
      return;
    bool ok = false;
    const auto title = QInputDialog::getText(this, "Monitoring follow-up", "Next action",
                                             QLineEdit::Normal, "Review monitoring evidence", &ok);
    if (ok) {
      if (mission != missions_.selectedID() || selected()["id"] != id)
        throw std::runtime_error("The review context changed.");
      missions_.addMonitoringTask(id, title);
    }
  });
  action("Run local analysis", "inboxRunLocal", [this] {
    const auto e = selected();
    if (e["areaID"].isString())
      runner_.runNow(e["areaID"].toString());
  });
  cancel_ = action("Cancel analysis", "inboxCancel", [this] {
    const auto e = selected();
    runner_.cancelEntry(missions_.selectedID(), e["key"].toString());
  });
  action("Remove resolved entry…", "inboxRemove", [this] {
    const auto mission = missions_.selectedID(), id = selected()["id"].toString();
    if (id.isEmpty())
      return;
    if (QMessageBox::question(this, "Remove resolved inbox entry",
                              "Remove this resolved entry? Its job files remain available.",
                              QMessageBox::Yes | QMessageBox::Cancel) == QMessageBox::Yes) {
      if (mission != missions_.selectedID())
        throw std::runtime_error("Mission changed.");
      missions_.removeMonitoringEvent(id);
    }
  });
  detail_ = new QPlainTextEdit;
  detail_->setObjectName("monitoringReport");
  detail_->setReadOnly(true);
  detail_->setMinimumHeight(150);
  layout->addWidget(detail_);
  raw_ = new QPlainTextEdit;
  raw_->setReadOnly(true);
  raw_->setMaximumHeight(180);
  design::disclosure(layout, raw_, "Retained analysis JSON");
  status_ = design::label({}, "secondary");
  status_->setWordWrap(true);
  layout->addWidget(status_);
  connect(&reader_, &QFutureWatcherBase::finished, this, [this] {
    reading_ = false;
    const auto r = reader_.result();
    if (readMission_ == missions_.selectedID() && readID_ == selectedID_) {
      detail_->setPlainText(r.isEmpty() ? "Detailed report is unavailable on this machine. "
                                          "Portable summaries remain in the inbox."
                                        : qualityText(r));
      raw_->setPlainText(r.isEmpty() ? QString{} : QString::fromUtf8(QJsonDocument(r).toJson()));
      if (r.isEmpty())
        open_->setEnabled(false);
    }
    readNext();
  });
  connect(list_, &QListWidget::currentRowChanged, this, [this] { inspect(); });
  connect(search_, &QLineEdit::textChanged, this, [this] { refresh(); });
  for (auto *c : {kind_, state_})
    connect(c, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
  connect(&missions_, &Missions::changed, this, &MonitoringInboxPanel::refresh);
  connect(&runner_, &MonitoringRunner::changed, this,
          [this] { status_->setText(runner_.status()); });
  refresh();
}
QJsonObject MonitoringInboxPanel::selected() const {
  if (!list_->currentItem())
    return {};
  const auto id = list_->currentItem()->data(Qt::UserRole);
  for (const auto v : missions_.selected()["brief"].toObject()["monitoringInbox"].toArray())
    if (v.toObject()["id"].toString() == id.toString())
      return v.toObject();
  return {};
}
void MonitoringInboxPanel::refresh() {
  const auto previous = missions_.selectedID() == missionID_ && list_->currentItem()
                            ? list_->currentItem()->data(Qt::UserRole).toString()
                            : QString{};
  missionID_ = missions_.selectedID();
  QSignalBlocker block(list_);
  list_->clear();
  for (const auto v : missions_.selected()["brief"].toObject()["monitoringInbox"].toArray()) {
    const auto e = v.toObject();
    if (!areaID_.isEmpty() && e["areaID"] != areaID_)
      continue;
    if (kind_->currentIndex() > 0 && e["kind"] != kind_->currentText())
      continue;
    const auto review = e["reviewState"].toString();
    if (state_->currentIndex() == 0 &&
        (review == "Closed" || review == "Dismissed" ||
         (review == "Snoozed" && missionTime(e["snoozedUntil"]) > QDateTime::currentDateTimeUtc())))
      continue;
    if (state_->currentIndex() > 1 && review != state_->currentText())
      continue;
    const auto caption = e["areaName"].toString() + " · " + e["status"].toString() + " · " +
                         review + "\n" + e["title"].toString() + "\n" + e["sourceID"].toString();
    if (!caption.contains(search_->text(), Qt::CaseInsensitive))
      continue;
    auto *item = new QListWidgetItem(caption, list_);
    item->setData(Qt::UserRole, e["id"]);
    if (e["id"] == previous)
      list_->setCurrentItem(item);
  }
  if (!list_->currentItem() && list_->count())
    list_->setCurrentRow(0);
  block.unblock();
  inspect();
}
void MonitoringInboxPanel::inspect() {
  const auto e = selected();
  pendingFile_.clear();
  raw_->clear();
  selectedID_ = e["id"].toString();
  open_->setEnabled(e["status"] == "Ready");
  cancel_->setEnabled(e["status"] == "Queued" || e["status"] == "Running");
  summary_->setText(e.isEmpty()
                        ? "No matching inbox entries. Configure a site watch or run local analysis "
                          "from its dossier."
                        : e["reason"].toString() + "\nSource time: " + dateLabel(e["sourceTime"]) +
                              " · Recorded: " + dateLabel(e["createdAt"]) + "\n" +
                              e["rationale"].toString());
  if (e.contains("candidateCount"))
    summary_->setText(summary_->text() +
                      QString("\n%1 candidate(s) · residual %2 pixels · %3% common valid coverage")
                          .arg(e["candidateCount"].toInt())
                          .arg(e["maximumResidualPixels"].toDouble(), 0, 'f', 3)
                          .arg(100 * e["commonValidFraction"].toDouble(), 0, 'f', 1));
  if (e["signature"].isString() &&
      e["signature"].toString() !=
          monitoringSignature(monitoringArea(missions_.selected(), e["areaID"].toString())))
    summary_->setText(summary_->text() +
                      "\nHistorical configuration: current watch settings differ from this run.");
  detail_->setPlainText(e.isEmpty() ? QString{} : QString::fromUtf8(QJsonDocument(e).toJson()));
  const auto file = runner_.reportFile(e), mission = missions_.selectedID(), id = selectedID_;
  if (file.isEmpty())
    return;
  pendingFile_ = file;
  pendingMission_ = mission;
  pendingID_ = id;
  readNext();
}
void MonitoringInboxPanel::readNext() {
  if (reading_ || pendingFile_.isEmpty())
    return;
  const auto file = pendingFile_;
  pendingFile_.clear();
  readMission_ = pendingMission_;
  readID_ = pendingID_;
  reading_ = true;
  reader_.setFuture(QtConcurrent::run([file] { return WorkerJob::readRecord(file); }));
}

void MonitoringInboxPanel::review() {
  const auto e = selected();
  if (e.isEmpty())
    return;
  const auto mission = missions_.selectedID(), id = e["id"].toString();
  QDialog dialog(this);
  dialog.setObjectName("monitoringReviewEditor");
  dialog.setWindowTitle("Review monitoring entry");
  auto *layout = new QVBoxLayout(&dialog);
  auto *state = new QComboBox;
  state->setObjectName("inboxReviewState");
  state->addItems({"New", "Investigating", "Closed", "Dismissed", "Snoozed"});
  state->setCurrentText(e["reviewState"].toString());
  layout->addWidget(state);
  auto *note = new QPlainTextEdit(e["rationale"].toString());
  note->setObjectName("inboxReviewRationale");
  note->setPlaceholderText("Rationale, corroborating sources and unresolved questions. Snooze "
                           "hides this entry for 24 hours.");
  layout->addWidget(note);
  auto *error = design::label({}, "secondary");
  error->setWordWrap(true);
  layout->addWidget(error);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    try {
      if (mission != missions_.selectedID() || selected()["id"] != id)
        throw std::runtime_error("The selected review context changed.");
      missions_.reviewMonitoringEvent(id, state->currentText(), note->toPlainText());
      dialog.accept();
    } catch (const std::exception &ex) {
      error->setText(QString::fromUtf8(ex.what()));
    }
  });
  dialog.exec();
}
void MonitoringInboxPanel::run(const std::function<void()> &fn) {
  try {
    fn();
  } catch (const std::exception &e) {
    status_->setText(QString::fromUtf8(e.what()));
  }
}
} // namespace helios

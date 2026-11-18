#include "InvestigationWorkbench.h"
#include "MissionEditor.h"
#include "WorkspaceStyle.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QException>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent>
#include <cmath>
#include <stdexcept>
namespace helios {
namespace {
void require(bool b, const char *s) {
  if (!b)
    throw std::runtime_error(s);
}
QJsonObject row(const QJsonArray &rows, const QString &id) {
  for (auto v : rows)
    if (v.toObject()["id"] == id)
      return v.toObject();
  return {};
}
QString chosen(QListWidget *list) {
  return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString{};
}
void add(QListWidget *list, QString label, QString id) {
  auto *item = new QListWidgetItem(label, list);
  item->setData(Qt::UserRole, id);
}
void restore(QListWidget *list, const QString &id) {
  for (int n = 0; n < list->count(); ++n)
    if (list->item(n)->data(Qt::UserRole) == id) {
      list->setCurrentRow(n);
      return;
    }
  if (list->count())
    list->setCurrentRow(0);
}
QPushButton *button(QBoxLayout *layout, const QString &text, QObject *context,
                    const std::function<void()> &action, const QString &name = {}) {
  auto *b = new QPushButton(text);
  b->setObjectName(name);
  layout->addWidget(b);
  QObject::connect(b, &QPushButton::clicked, context, action);
  return b;
}
QComboBox *boundedCombo() {
  auto *combo = new QComboBox;
  combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  combo->setMinimumContentsLength(18);
  combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  return combo;
}
QLabel *notice(const QString &text) {
  auto *l = design::label(text, "secondary");
  l->setWordWrap(true);
  l->setTextFormat(Qt::PlainText);
  return l;
}
QPlainTextEdit *paragraph(QFormLayout *form, const QString &label, const QString &value) {
  auto *e = new QPlainTextEdit(value);
  e->setMaximumHeight(110);
  form->addRow(label, e);
  return e;
}
QLineEdit *line(QFormLayout *form, const QString &label, const QString &value, int max = 240) {
  auto *e = new QLineEdit(value);
  e->setMaxLength(max);
  form->addRow(label, e);
  return e;
}
QDialogButtonBox *saveButtons(QVBoxLayout *layout, QDialog &dialog,
                              const std::function<void()> &save) {
  auto *b = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  layout->addWidget(b);
  QObject::connect(b, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(b, &QDialogButtonBox::accepted, &dialog, [&dialog, save] {
    try {
      save();
      dialog.accept();
    } catch (const std::exception &e) {
      QMessageBox::warning(&dialog, "Draft retained", QString::fromUtf8(e.what()));
    }
  });
  return b;
}
QWidget *scrollForm(QVBoxLayout *layout, QFormLayout *&form) {
  auto *content = new QWidget;
  form = new QFormLayout(content);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  auto *scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setWidget(content);
  layout->addWidget(scroll, 1);
  return content;
}
QJsonObject manualCitation(const QString &label, const QString &reference) {
  auto c = citationForOrigin(
      QJsonObject{{"label", label},
                  {"sourceID", reference},
                  {"kind", "Analyst supplied citation"},
                  {"qualityLimitations", "Analyst supplied reference; source accuracy and "
                                         "infrastructure interpretation require review."}});
  c["reference"] = reference;
  return c;
}
QString exceptionMessage(std::exception_ptr error) {
  if (!error)
    return "Unknown briefing error.";
  try {
    std::rethrow_exception(error);
  } catch (const QUnhandledException &e) {
    return exceptionMessage(e.exception());
  } catch (const std::exception &e) {
    return QString::fromUtf8(e.what());
  } catch (...) {
    return "The local operation could not be completed.";
  }
}
class BriefingDialog : public QDialog {
public:
  using QDialog::QDialog;
  std::function<bool()> busy;
  void reject() override {
    if (!busy || !busy())
      QDialog::reject();
  }

protected:
  void closeEvent(QCloseEvent *e) override {
    if (busy && busy())
      e->ignore();
    else
      QDialog::closeEvent(e);
  }
};
class GraphView : public QGraphicsView {
public:
  using QGraphicsView::QGraphicsView;
  void fitContents() {
    autoFit_ = true;
    scheduleFit();
  }

protected:
  void showEvent(QShowEvent *e) override {
    QGraphicsView::showEvent(e);
    if (autoFit_)
      scheduleFit();
  }
  void resizeEvent(QResizeEvent *e) override {
    QGraphicsView::resizeEvent(e);
    if (autoFit_)
      scheduleFit();
  }
  void wheelEvent(QWheelEvent *e) override {
    const auto factor = e->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
    const auto next = transform().m11() * factor;
    if (next >= 0.12 && next <= 4) {
      autoFit_ = false;
      scale(factor, factor);
    }
    e->accept();
  }

private:
  bool autoFit_ = true, fitQueued_ = false;
  void scheduleFit() {
    if (fitQueued_)
      return;
    fitQueued_ = true;
    QTimer::singleShot(0, this, [this] {
      fitQueued_ = false;
      if (!autoFit_ || !isVisible() || !scene() || scene()->items().isEmpty())
        return;
      // Hidden stacked pages have provisional viewport sizes. Fit only after
      // the visible layout has settled, and keep small neighborhoods readable.
      fitInView(scene()->itemsBoundingRect().adjusted(-30, -30, 30, 30), Qt::KeepAspectRatio);
      if (transform().m11() > 1.5)
        scale(1.5 / transform().m11(), 1.5 / transform().m11());
    });
  }
};
} // namespace
InvestigationWorkbench::InvestigationWorkbench(const QString &profile, Missions &missions,
                                               QWidget *parent)
    : QObject(parent), missions_(missions), registry_(profile + "/Registry/assets.json", this) {
  QDir().mkpath(profile + "/Registry");
  overview_ = new QWidget(parent);
  assetsPage_ = new QWidget(parent);
  connectionsPage_ = new QWidget(parent);
  findingsPage_ = new QWidget(parent);
  overview_->setObjectName("investigationOverview");
  assetsPage_->setObjectName("assetRegistry");
  connectionsPage_->setObjectName("assetConnections");
  findingsPage_->setObjectName("investigationFindings");
  auto *o = new QVBoxLayout(overview_);
  design::heading(o, "CONNECTED INVESTIGATIONS", "Case completion",
                  "Define the question in Overview, retain dated source records, review findings "
                  "and prepare a briefing.");
  caseSummary_ = new QTextBrowser;
  caseSummary_->setMaximumHeight(230);
  caseSummary_->setMinimumHeight(150);
  o->addWidget(caseSummary_);
  button(
      o, "Edit investigation state and conclusions…", this,
      [this] { run([this] { editOverview(); }); }, "editInvestigation");
  snapshots_ = new QListWidget;
  snapshots_->setObjectName("caseAssetSnapshots");
  snapshots_->setWordWrap(true);
  o->addWidget(notice(
      "Adopted asset and relationship versions - registry changes never replace these records."));
  o->addWidget(snapshots_, 1);
  button(
      o, "Preview registry import from this case…", this, [this] { run([this] { importCase(); }); },
      "importCaseAssets");
  button(
      o, "Export briefing…", this, [this] { run([this] { briefing(); }); },
      "exportInvestigationBriefing");
  auto *a = new QVBoxLayout(assetsPage_);
  design::heading(a, "PROFILE ASSET REGISTRY", "Shared infrastructure assets",
                  "Register a source or area explicitly. Link the same asset to multiple missions "
                  "while retaining each case's adopted versions.");
  registryStatus_ = notice({});
  a->addWidget(registryStatus_);
  assetSearch_ = new QLineEdit;
  assetSearch_->setPlaceholderText("Search name, alias, category or stable ID…");
  a->addWidget(assetSearch_);
  auto *split = new QSplitter;
  assets_ = new QListWidget;
  assets_->setObjectName("registryAssets");
  assets_->setWordWrap(true);
  relationships_ = new QListWidget;
  relationships_->setObjectName("registryRelationships");
  relationships_->setWordWrap(true);
  split->addWidget(assets_);
  split->addWidget(relationships_);
  a->addWidget(split, 1);
  auto *assetActions = new QHBoxLayout;
  a->addLayout(assetActions);
  button(
      assetActions, "Register source / area…", this, [this] { run([this] { editAsset(true); }); },
      "registerAsset");
  button(assetActions, "Edit asset…", this, [this] { run([this] { editAsset(false); }); });
  button(
      assetActions, "Link / adopt selected asset…", this,
      [this] {
        run([this] {
          const auto mid = missions_.selectedID(), aid = chosen(assets_);
          const auto source = registry_.asset(aid);
          require(!source.isEmpty(), "Select a registry asset.");
          QDialog dialog(assetsPage_);
          dialog.setWindowTitle("Link asset / adopt current version");
          auto *l = new QVBoxLayout(&dialog);
          l->addWidget(notice("Append an immutable case snapshot of " + source["name"].toString() +
                              " revision " + QString::number(source["revision"].toInt()) +
                              ". Earlier snapshots and finding decisions remain intact."));
          auto *areas = boundedCombo();
          areas->addItem("Case-wide (no area)", QString{});
          for (auto v : missions_.selected()["brief"].toObject()["areas"].toArray())
            areas->addItem(v.toObject()["name"].toString(), v.toObject()["id"].toString());
          l->addWidget(areas);
          saveButtons(l, dialog, [&] {
            require(registry_.asset(aid) == source,
                    "The registry asset changed. Reopen this preview.");
            missions_.adoptAsset(mid, Asset::read(source), areas->currentData().toString());
          });
          dialog.exec();
        });
      },
      "adoptAsset");
  button(assetActions, "Archive asset", this, [this] {
    run([this] {
      auto a = Asset::read(registry_.asset(chosen(assets_)));
      registry_.archive(a.id, false, a.revision);
    });
  });
  auto *relActions = new QHBoxLayout;
  a->addLayout(relActions);
  button(
      relActions, "Add relationship…", this, [this] { run([this] { editRelationship(true); }); },
      "addRelationship");
  button(relActions, "Edit relationship…", this,
         [this] { run([this] { editRelationship(false); }); });
  button(relActions, "Adopt relationship", this, [this] {
    run([this] {
      auto r = AssetRelationship::read(registry_.relationship(chosen(relationships_)));
      missions_.adoptRelationship(missions_.selectedID(), r);
    });
  });
  button(relActions, "Archive relationship", this, [this] {
    run([this] {
      auto r = AssetRelationship::read(registry_.relationship(chosen(relationships_)));
      registry_.archive(r.id, true, r.revision);
    });
  });
  button(relActions, "Reload registry", this, [this] {
    run([this] {
      registry_.load();
      registryReady_ = true;
      refresh();
    });
  });
  a->addWidget(notice("Suggested evidence associations - review the reason before approval. "
                      "Proximity does not establish a dependency."));
  suggestions_ = new QListWidget;
  suggestions_->setObjectName("assetAssociationSuggestions");
  suggestions_->setWordWrap(true);
  suggestions_->setMaximumHeight(170);
  a->addWidget(suggestions_);
  button(
      a, "Approve selected association", this,
      [this] {
        run([this] {
          const int n = suggestions_->currentRow();
          require(n >= 0 && n < suggested_.size(), "Select a suggestion.");
          missions_.approveAssociation(missions_.selectedID(), suggested_[n]);
        });
      },
      "approveAssetAssociation");
  auto *c = new QVBoxLayout(connectionsPage_);
  design::heading(c, "ASSET CONNECTIONS", "Infrastructure context",
                  "Select a node to connect map location, area dossier, dated evidence and "
                  "findings. Wheel to zoom; drag the background to pan.");
  auto *controls = new QHBoxLayout;
  c->addLayout(controls);
  graphSource_ = boundedCombo();
  graphSource_->setObjectName("connectionsSource");
  graphSource_->addItems({"Case snapshots", "Current registry"});
  controls->addWidget(graphSource_);
  graphSearch_ = new QLineEdit;
  graphSearch_->setPlaceholderText("Find asset / alias…");
  controls->addWidget(graphSearch_, 1);
  graphRoot_ = boundedCombo();
  graphRoot_->setMinimumWidth(180);
  controls->addWidget(graphRoot_);
  graphDepth_ = new QSpinBox;
  graphDepth_->setRange(1, 10);
  graphDepth_->setPrefix("Hops ");
  controls->addWidget(graphDepth_);
  button(controls, "Expand", this,
         [this] { graphDepth_->setValue(std::min(10, graphDepth_->value() + 1)); });
  button(controls, "Fit", this, [this] {
    static_cast<GraphView *>(graph_)->fitContents();
  });
  graphStatus_ = notice({});
  graphStatus_->setObjectName("connectionsStatus");
  c->addWidget(graphStatus_);
  auto *graphSplit = new QSplitter;
  scene_ = new QGraphicsScene(this);
  graph_ = new GraphView(scene_);
  graph_->setObjectName("connectionsGraph");
  graph_->setRenderHint(QPainter::Antialiasing);
  graph_->setDragMode(QGraphicsView::ScrollHandDrag);
  graph_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  graph_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  graph_->setMinimumHeight(330);
  graphSplit->addWidget(graph_);
  graphDetail_ = new QTextBrowser;
  graphDetail_->setObjectName("connectionsDetail");
  graphDetail_->setOpenLinks(false);
  graphDetail_->setOpenExternalLinks(false);
  connect(graphDetail_, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
    run([&] {
      if (url.scheme() == "helios-finding") {
        restore(findings_, url.path());
        emit findingsRequested();
      } else if (url.scheme() == "helios-evidence") {
        const auto citation =
            row(investigation(missions_.selected())["citations"].toArray(), url.path());
        require(citation["evidence"].isObject(),
                "This source snapshot is no longer in the selected case.");
        emit evidenceRequested(citation["evidence"].toObject());
      }
    });
  });
  graphDetail_->setMinimumWidth(280);
  graphSplit->addWidget(graphDetail_);
  graphSplit->setStretchFactor(0, 3);
  c->addWidget(graphSplit, 1);
  auto *links = new QHBoxLayout;
  c->addLayout(links);
  button(links, "Focus asset on map", this, [this] {
    run([this] {
      require(!scene_->selectedItems().isEmpty(), "Select a graph asset.");
      const auto id = scene_->selectedItems().first()->data(0).toString();
      const auto a = Asset::read(row(shownGraph_.nodes, id));
      if (a.boundary)
        emit focusRequested(*a.boundary);
      else {
        const auto p = *a.location;
        emit focusRequested(
            Bounds{p.longitude - .01, p.latitude - .01, p.longitude + .01, p.latitude + .01});
      }
    });
  });
  button(links, "Open linked dossier", this, [this] {
    run([this] {
      require(!scene_->selectedItems().isEmpty(), "Select a graph asset.");
      const auto aid = scene_->selectedItems().first()->data(0).toString();
      QString area;
      for (auto v : investigation(missions_.selected())["snapshots"].toArray())
        if (v.toObject()["asset"].toObject()["id"] == aid && v.toObject()["areaID"].isString())
          area = v.toObject()["areaID"].toString();
      require(!row(missions_.selected()["brief"].toObject()["areas"].toArray(), area).isEmpty(),
              "No live area is linked to this asset. Historical snapshots remain available.");
      emit dossierRequested(area);
    });
  });
  button(links, "Open Findings", this, [this] { emit findingsRequested(); });
  auto *f = new QVBoxLayout(findingsPage_);
  design::heading(
      f, "ANALYST FINDINGS", "Claims and evidence",
      "Supported requires a supporting citation and rationale. Changing a reviewed claim or its "
      "citations resets it to Unreviewed and retains the earlier decision.");
  auto *findingSplit = new QSplitter;
  findings_ = new QListWidget;
  findings_->setObjectName("findingsList");
  findings_->setWordWrap(true);
  findingSplit->addWidget(findings_);
  findingDetail_ = new QTextBrowser;
  findingSplit->addWidget(findingDetail_);
  f->addWidget(findingSplit, 1);
  findingStatus_ = notice({});
  f->addWidget(findingStatus_);
  auto *findingActions = new QHBoxLayout;
  f->addLayout(findingActions);
  button(
      findingActions, "New finding…", this, [this] { run([this] { editFinding(true); }); },
      "newFinding");
  button(
      findingActions, "Edit / review…", this, [this] { run([this] { editFinding(false); }); },
      "reviewFinding");
  button(findingActions, "Draft from observation…", this, [this] {
    run([this] {
      const auto mid = missions_.selectedID();
      const auto rows = missions_.selected()["brief"].toObject()["observations"].toArray();
      QStringList labels;
      for (auto v : rows)
        labels << v.toObject()["title"].toString() + " / " + v.toObject()["id"].toString();
      bool ok = false;
      auto pick = QInputDialog::getItem(findingsPage_, "Draft finding", "Observation", labels, 0,
                                        false, &ok);
      if (ok) {
        require(mid == missions_.selectedID(), "Mission changed.");
        auto o = rows[labels.indexOf(pick)].toObject();
        promote({{"kind", "Observation"},
                 {"sourceID", o["id"]},
                 {"label", o["title"]},
                 {"record", o},
                 {"areaID", o["areaID"]},
                 {"sourceDate", o["observedAt"]},
                 {"qualityLimitations",
                  "Original observation and any model scores retained. An observation or model "
                  "proposal requires independent analyst review."}});
      }
    });
  });
  button(
      findingActions, "Create follow-up…", this,
      [this] {
        run([this] {
          const auto mid = missions_.selectedID(), fid = chosen(findings_);
          require(!fid.isEmpty(), "Select a finding.");
          bool ok = false;
          const auto title = QInputDialog::getText(findingsPage_, "Finding follow-up", "Action",
                                                   QLineEdit::Normal, {}, &ok);
          if (ok) {
            const auto id = missions_.addFindingTask(mid, fid, title);
            editMissionEntry(findingsPage_, missions_, "tasks", id);
          }
        });
      },
      "findingFollowUp");
  button(findingActions, "Export briefing…", this, [this] { run([this] { briefing(); }); });
  connect(assetSearch_, &QLineEdit::textChanged, this, &InvestigationWorkbench::refresh);
  connect(graphSearch_, &QLineEdit::textChanged, this, &InvestigationWorkbench::refresh);
  connect(graphSource_, &QComboBox::currentIndexChanged, this, &InvestigationWorkbench::refresh);
  connect(graphRoot_, &QComboBox::currentIndexChanged, this, &InvestigationWorkbench::renderGraph);
  connect(graphDepth_, &QSpinBox::valueChanged, this, &InvestigationWorkbench::renderGraph);
  connect(scene_, &QGraphicsScene::selectionChanged, this, &InvestigationWorkbench::graphSelection);
  connect(findings_, &QListWidget::currentRowChanged, this,
          &InvestigationWorkbench::inspectFinding);
  connect(&missions_, &Missions::changed, this, &InvestigationWorkbench::refresh);
  connect(&registry_, &AssetRegistry::changed, this, &InvestigationWorkbench::refresh);
  try {
    registry_.load();
    registryReady_ = true;
  } catch (const std::exception &e) {
    registryStatus_->setText(
        "Registry unavailable: " + QString::fromUtf8(e.what()) +
        ". Case snapshots remain usable; the original registry has been preserved.");
  }
  refresh();
}
void InvestigationWorkbench::run(const std::function<void()> &action) {
  try {
    action();
  } catch (...) {
    QMessageBox::warning(qobject_cast<QWidget *>(parent()), "Investigation",
                         exceptionMessage(std::current_exception()));
  }
}
void InvestigationWorkbench::refresh() {
  if (refreshing_)
    return;
  refreshing_ = true;
  const auto p = missions_.selected(), i = investigation(p);
  const auto currentAsset = chosen(assets_), currentSnapshot = chosen(snapshots_),
             currentRel = chosen(relationships_), currentFinding = chosen(findings_);
  const auto root = graphRoot_->currentData().toString();
  caseSummary_->setPlainText(
      p.isEmpty() ? "Create or select a mission to begin."
                  : QString("%1 · %2 findings · %3 retained versions · %4 outstanding "
                            "items\nCompletion: %5\nConclusion: %6\nUnresolved: %7")
                        .arg(i["status"].toString())
                        .arg(i["findings"].toArray().size())
                        .arg(i["snapshots"].toArray().size())
                        .arg(outstandingInvestigationWork(p).size())
                        .arg(i["completionCriteria"].toString(), i["conclusion"].toString(),
                             i["unresolvedQuestions"].toString()));
  assets_->clear();
  relationships_->clear();
  snapshots_->clear();
  findings_->clear();
  suggestions_->clear();
  QSignalBlocker block(graphRoot_);
  graphRoot_->clear();
  for (auto v : registry_.assets()) {
    const auto a = v.toObject();
    const auto hay = a["name"].toString() + " " + a["category"].toString() + " " +
                     a["aliases"].toVariant().toStringList().join(" ") + " " + a["id"].toString();
    if (hay.contains(assetSearch_->text(), Qt::CaseInsensitive))
      add(assets_,
          a["name"].toString() + " / " + a["category"].toString() + " / r" +
              QString::number(a["revision"].toInt()) +
              (a["archived"].toBool() ? " / Archived" : ""),
          a["id"].toString());
  }
  for (auto v : registry_.relationships()) {
    const auto r = v.toObject();
    add(relationships_,
        registry_.asset(r["fromAssetID"].toString())["name"].toString() + " → " +
            r["kind"].toString() + " → " +
            registry_.asset(r["toAssetID"].toString())["name"].toString() + " / " +
            r["reviewState"].toString() + (r["archived"].toBool() ? " / Archived" : ""),
        r["id"].toString());
  }
  int updates = 0;
  QMap<QString, QString> latestVersions;
  for (auto v : i["snapshots"].toArray()) {
    const auto s = v.toObject();
    const auto key = s.contains("asset") ? "asset" : "relationship";
    latestVersions[QString(key) + s[key].toObject()["id"].toString()] = s["id"].toString();
  }
  QMap<QString, QJsonObject> caseAssets;
  for (auto v : i["snapshots"].toArray()) {
    const auto s = v.toObject();
    const bool isAsset = s.contains("asset");
    const auto a = s[isAsset ? "asset" : "relationship"].toObject();
    const auto live =
        isAsset ? registry_.asset(a["id"].toString()) : registry_.relationship(a["id"].toString());
    const bool latest =
        latestVersions[QString(isAsset ? "asset" : "relationship") + a["id"].toString()] ==
        s["id"].toString();
    const bool updated = latest && !live.isEmpty() && live != a;
    if (updated)
      ++updates;
    add(snapshots_,
        (isAsset ? a["name"].toString() : a["kind"].toString() + " relationship") + " / r" +
            QString::number(a["revision"].toInt()) + " / " + dateLabel(s["capturedAt"]) +
            (updated  ? " / Registry update available; adopt explicitly"
             : latest ? ""
                      : " / Retained historical version"),
        s["id"].toString());
    if (isAsset)
      caseAssets[a["id"].toString()] = a;
  }
  if (registryReady_)
    registryStatus_->setText(QString("%1 assets · %2 relationships · %3 historical snapshots "
                                     "differ from the registry. Adopt a current version explicitly "
                                     "to update this case. Archived records remain available.")
                                 .arg(registry_.assets().size())
                                 .arg(registry_.relationships().size())
                                 .arg(updates));
  QJsonArray nodes = graphSource_->currentIndex() == 1 ? registry_.assets() : QJsonArray{};
  if (graphSource_->currentIndex() == 0)
    for (auto a : caseAssets)
      nodes.append(a);
  for (auto v : nodes) {
    const auto a = v.toObject();
    const auto hay = a["name"].toString() + " " +
                     a["aliases"].toVariant().toStringList().join(" ") + " " + a["id"].toString();
    if (hay.contains(graphSearch_->text(), Qt::CaseInsensitive))
      graphRoot_->addItem(a["name"].toString() + (a["archived"].toBool() ? " / Archived" : ""),
                          a["id"].toString());
  }
  graphRoot_->setCurrentIndex(std::max(0, graphRoot_->findData(root)));
  for (auto v : i["findings"].toArray()) {
    const auto f = v.toObject();
    add(findings_, f["state"].toString() + " · " + f["claim"].toString(), f["id"].toString());
  }
  suggested_ = suggestAssociations(p);
  for (const auto &s : suggested_) {
    const auto snap = row(i["snapshots"].toArray(), s.assetSnapshotID);
    add(suggestions_,
        snap["asset"].toObject()["name"].toString() + " ↔ " + s.evidence["title"].toString() +
            "\n" + s.reason,
        s.assetSnapshotID);
  }
  restore(assets_, currentAsset);
  restore(snapshots_, currentSnapshot);
  restore(relationships_, currentRel);
  restore(findings_, currentFinding);
  refreshing_ = false;
  renderGraph();
  inspectFinding();
}
void InvestigationWorkbench::editOverview() {
  const auto mid = missions_.selectedID();
  require(!mid.isEmpty(), "Select a mission.");
  const auto original = investigation(missions_.selected());
  QDialog d(overview_);
  d.setObjectName("investigationOverviewEditor");
  d.setWindowTitle("Investigation state and conclusions");
  d.resize(720, 720);
  auto *l = new QVBoxLayout(&d);
  QFormLayout *form;
  scrollForm(l, form);
  auto *state = boundedCombo();
  state->addItems({"Open", "In review", "Closed"});
  state->setCurrentText(original["status"].toString());
  form->addRow("Case state", state);
  auto *criteria =
           paragraph(form, "Completion criteria", original["completionCriteria"].toString()),
       *conclusion = paragraph(form, "Conclusion", original["conclusion"].toString()),
       *questions =
           paragraph(form, "Unresolved questions", original["unresolvedQuestions"].toString()),
       *closure = paragraph(form, "Rationale for closing\nwith outstanding work",
                            original["closureRationale"].toString());
  saveButtons(l, d, [&] {
    missions_.setInvestigationOverview(mid, original,
                                       {{"status", state->currentText()},
                                        {"completionCriteria", criteria->toPlainText()},
                                        {"conclusion", conclusion->toPlainText()},
                                        {"unresolvedQuestions", questions->toPlainText()},
                                        {"closureRationale", closure->toPlainText()}});
  });
  d.exec();
}
void InvestigationWorkbench::editAsset(bool create) {
  require(registryReady_, "Reload or repair the registry before editing.");
  const auto mid = missions_.selectedID();
  Asset a;
  if (!create)
    a = Asset::read(registry_.asset(chosen(assets_)));
  const int expected = create ? 0 : a.revision;
  QDialog d(assetsPage_);
  d.setObjectName("assetEditor");
  d.setWindowTitle(create ? "Register asset" : "Edit registry asset");
  d.resize(720, 760);
  auto *l = new QVBoxLayout(&d);
  QFormLayout *form;
  scrollForm(l, form);
  auto *source = boundedCombo();
  QList<QJsonObject> sources;
  source->addItem("Manual location and citation");
  sources << QJsonObject{};
  for (auto v : missions_.selected()["brief"].toObject()["areas"].toArray()) {
    source->addItem("Area: " + v.toObject()["name"].toString());
    sources << QJsonObject{{"area", v}};
  }
  for (auto v : missions_.selected()["items"].toArray()) {
    const auto e = v.toObject();
    if (e["kind"] == "site" || e["kind"] == "record") {
      source->addItem("Snapshot: " + e["title"].toString());
      sources << QJsonObject{{"evidence", e}};
    }
  }
  form->addRow("Register from", source);
  source->setVisible(create);
  auto *name = line(form, "Name", a.name),
       *aliases = line(form, "Aliases (semicolon separated)", a.aliases.join("; "), 4000),
       *category = line(form, "Category",
                        a.category.isEmpty() ? "Civilian infrastructure" : a.category, 120);
  auto spin = [&](const QString &label, double value, double low, double high) {
    auto *s = new QDoubleSpinBox;
    s->setDecimals(6);
    s->setRange(low, high);
    s->setValue(value);
    form->addRow(label, s);
    return s;
  };
  const auto p = a.location.value_or(a.boundary ? a.boundary->center() : Coordinate{0, 0});
  auto *lat = spin("Latitude", p.latitude, -90, 90),
       *lon = spin("Longitude", p.longitude, -180, 180);
  auto *useBoundary = new QCheckBox("Use geographic boundary");
  useBoundary->setChecked(a.boundary.has_value());
  form->addRow(useBoundary);
  const auto b = a.boundary.value_or(Bounds{-0.01, -0.01, 0.01, 0.01});
  auto *west = spin("West", b.west, -180, 180), *south = spin("South", b.south, -85, 85),
       *east = spin("East", b.east, -180, 180), *north = spin("North", b.north, -85, 85);
  auto *provider = line(form, "Identifier provider",
                        a.identifiers.isEmpty() ? QString{} : a.identifiers.begin().key(), 512);
  auto *providerID =
      line(form, "Provider-qualified ID",
           a.identifiers.isEmpty() ? QString{} : a.identifiers.begin().value().toString(), 1024);
  auto *reference = line(form, "Additional source citation", {}, 4096);
  auto *archived = new QCheckBox("Archived");
  archived->setChecked(a.archived);
  form->addRow(archived);
  auto *existing = new QPlainTextEdit(QJsonDocument(a.citations).toJson(QJsonDocument::Indented));
  existing->setReadOnly(true);
  existing->setMaximumHeight(100);
  form->addRow("Retained source citations", existing);
  connect(source, &QComboBox::currentIndexChanged, &d, [&](int n) {
    const auto s = sources[n], e = s["evidence"].toObject(), area = s["area"].toObject(),
               r = e["record"].toObject();
    if (!area.isEmpty()) {
      name->setText(area["name"].toString());
      if (auto region = Bounds::read(area["region"])) {
        useBoundary->setChecked(true);
        west->setValue(region->west);
        south->setValue(region->south);
        east->setValue(region->east);
        north->setValue(region->north);
        lat->setValue(region->center().latitude);
        lon->setValue(region->center().longitude);
      }
    } else if (!e.isEmpty()) {
      name->setText(e["title"].toString());
      if (e["kind"] == "site") {
        const auto region = Bounds::read(e["site"].toObject()["region"]);
        if (region) {
          useBoundary->setChecked(true);
          west->setValue(region->west);
          south->setValue(region->south);
          east->setValue(region->east);
          north->setValue(region->north);
        }
      } else if (auto point = Coordinate::read(r["coordinate"].toObject())) {
        useBoundary->setChecked(false);
        lat->setValue(point->latitude);
        lon->setValue(point->longitude);
        provider->setText(r["source"].toString());
        providerID->setText(r["id"].toString());
      }
    }
  });
  saveButtons(l, d, [&] {
    require(mid == missions_.selectedID(),
            "The mission changed. Your registration draft is retained.");
    a.name = name->text();
    a.aliases = aliases->text().split(';', Qt::SkipEmptyParts);
    for (auto &alias : a.aliases)
      alias = alias.trimmed();
    a.category = category->text();
    a.location = Coordinate{lat->value(), lon->value()};
    a.boundary = useBoundary->isChecked()
                     ? std::optional<Bounds>(
                           Bounds{west->value(), south->value(), east->value(), north->value()})
                     : std::nullopt;
    if (!provider->text().trimmed().isEmpty()) {
      require(!providerID->text().trimmed().isEmpty(), "Supply the provider-qualified ID.");
      a.identifiers[provider->text().trimmed()] = providerID->text().trimmed();
    }
    const auto src = sources[source->currentIndex()];
    if (create && src["evidence"].isObject())
      a.citations.append(citationForEvidence(src["evidence"].toObject()));
    if (create && src["area"].isObject())
      a.citations.append(
          manualCitation(src["area"].toObject()["name"].toString(),
                         "mission:" + mid + "/area:" + src["area"].toObject()["id"].toString()));
    if (!reference->text().trimmed().isEmpty())
      a.citations.append(manualCitation(a.name, reference->text().trimmed()));
    a.archived = archived->isChecked();
    registry_.saveAsset(a, expected);
  });
  d.exec();
}
void InvestigationWorkbench::editRelationship(bool create) {
  require(registryReady_, "Reload or repair the registry before editing.");
  AssetRelationship r;
  if (!create)
    r = AssetRelationship::read(registry_.relationship(chosen(relationships_)));
  const int expected = create ? 0 : r.revision;
  QDialog d(assetsPage_);
  d.setObjectName("relationshipEditor");
  d.setWindowTitle("Evidence-backed relationship");
  d.resize(700, 680);
  auto *l = new QVBoxLayout(&d);
  l->addWidget(notice(
      "Part of and Depends on are directed from the first asset to the second. Proximity never "
      "establishes a dependency. Reviewed relationships require citations and rationale."));
  QFormLayout *form;
  scrollForm(l, form);
  auto *from = boundedCombo(), *to = boundedCombo();
  for (auto v : registry_.assets()) {
    const auto a = v.toObject();
    from->addItem(a["name"].toString(), a["id"].toString());
    to->addItem(a["name"].toString(), a["id"].toString());
  }
  from->setCurrentIndex(std::max(0, from->findData(r.fromAssetID)));
  to->setCurrentIndex(std::max(0, to->findData(r.toAssetID)));
  form->addRow("From asset", from);
  form->addRow("To asset", to);
  auto *kind = boundedCombo();
  kind->addItems({"Part of", "Connected to", "Depends on"});
  kind->setCurrentText(r.kind);
  form->addRow("Relationship", kind);
  auto *review = boundedCombo();
  review->addItems({"Unreviewed", "Supported", "Uncertain", "Dismissed"});
  review->setCurrentText(r.reviewState);
  form->addRow("Analyst review", review);
  auto *rationale = paragraph(form, "Rationale", r.rationale);
  auto *start = line(form, "Valid from (YYYY-MM-DD)", r.validFrom, 10),
       *end = line(form, "Valid through (YYYY-MM-DD)", r.validThrough, 10),
       *reference = line(form, "Add explicit evidence citation", {}, 4096);
  auto *cites = new QPlainTextEdit(QJsonDocument(r.citations).toJson(QJsonDocument::Indented));
  cites->setReadOnly(true);
  form->addRow("Retained citations", cites);
  saveButtons(l, d, [&] {
    r.fromAssetID = from->currentData().toString();
    r.toAssetID = to->currentData().toString();
    r.kind = kind->currentText();
    r.reviewState = review->currentText();
    r.rationale = rationale->toPlainText();
    r.validFrom = start->text();
    r.validThrough = end->text();
    if (!reference->text().trimmed().isEmpty())
      r.citations.append(manualCitation(r.kind, reference->text()));
    registry_.saveRelationship(r, expected);
  });
  d.exec();
}
void InvestigationWorkbench::importCase() {
  require(registryReady_, "Reload or repair the registry before importing.");
  const auto plan = missions_.selected();
  const auto preview = registry_.importPreview(plan);
  require(!preview.isEmpty(), "This case has no asset snapshots to import.");
  QDialog d(overview_);
  d.setWindowTitle("Registry import conflict preview");
  d.resize(800, 620);
  auto *l = new QVBoxLayout(&d);
  l->addWidget(notice("Approve the exact preview below to import these identities. Conflicting "
                      "case versions replace current registry values as a new revision. Existing "
                      "case snapshots remain unchanged."));
  auto *view = new QPlainTextEdit(QJsonDocument(preview).toJson(QJsonDocument::Indented));
  view->setReadOnly(true);
  l->addWidget(view, 1);
  saveButtons(l, d, [&] {
    require(missions_.selected() == plan, "The case changed. Preview the import again.");
    registry_.importSnapshots(plan, preview);
  });
  d.exec();
}
void InvestigationWorkbench::renderGraph() {
  if (refreshing_)
    return;
  const auto i = investigation(missions_.selected());
  QJsonArray assets, relationships;
  if (graphSource_->currentIndex() == 1) {
    assets = registry_.assets();
    relationships = registry_.relationships();
  } else {
    QMap<QString, QJsonObject> latestAssets, latestRelationships;
    for (auto v : i["snapshots"].toArray())
      for (auto key : {"asset", "relationship"}) {
        const auto a = v.toObject()[key].toObject();
        if (a.isEmpty())
          continue;
        auto &rows = QString(key) == "asset" ? latestAssets : latestRelationships;
        rows[a["id"].toString()] = a;
      }
    for (auto a : latestAssets)
      assets.append(a);
    for (auto r : latestRelationships)
      relationships.append(r);
  }
  const auto root = graphRoot_->currentData().toString();
  shownGraph_ = assetNeighborhood(assets, relationships, root, graphDepth_->value());
  QSignalBlocker block(scene_);
  scene_->clear();
  QMap<QString, QPointF> points;
  const int count = shownGraph_.nodes.size();
  int n = 0;
  for (auto v : shownGraph_.nodes) {
    const auto a = v.toObject();
    QPointF point;
    if (count <= 14) {
      if (a["id"] == root)
        point = {0, 0};
      else {
        const double angle = 2 * 3.141592653589793 * n / std::max(1, count - 1);
        point = {std::cos(angle) * 320, std::sin(angle) * 260};
        ++n;
      }
    } else {
      point = {double(n % 10) * 210, double(n / 10) * 130};
      ++n;
    }
    points[a["id"].toString()] = point;
  }
  for (auto v : shownGraph_.edges) {
    const auto e = v.toObject();
    const auto p = points[e["fromAssetID"].toString()], q = points[e["toAssetID"].toString()];
    const QColor color = e["kind"] == "Depends on" ? QColor("#e6ae5e") : QColor("#537b91");
    auto *edge = scene_->addLine(QLineF(p, q), QPen(color, 1.5));
    edge->setZValue(-2);
    edge->setToolTip(e["kind"].toString() + " / " + e["reviewState"].toString() + "\n" +
                     e["rationale"].toString());
    // Arrow head preserves direction for Part of / Depends on.
    if (e["kind"] != "Connected to") {
      QLineF segment(p, q);
      const auto mid = segment.pointAt(.65);
      const auto angle = std::atan2(q.y() - p.y(), q.x() - p.x());
      QPolygonF arrow;
      arrow << mid << mid - QPointF(std::cos(angle - .5) * 12, std::sin(angle - .5) * 12)
            << mid - QPointF(std::cos(angle + .5) * 12, std::sin(angle + .5) * 12);
      scene_->addPolygon(arrow, QPen(color), QBrush(color))->setZValue(-1);
    }
    if (count <= 20) {
      auto *label = scene_->addText(e["kind"].toString());
      label->setDefaultTextColor(color);
      label->setPos((p + q) / 2 - QPointF(label->boundingRect().width() / 2, -4));
      label->setZValue(-1);
    }
  }
  for (auto v : shownGraph_.nodes) {
    const auto a = v.toObject();
    auto *node = scene_->addRect(QRectF(-85, -32, 170, 64),
                                 QPen(a["id"] == root ? QColor("#7bdef3") : QColor("#55778c"), 2),
                                 QBrush(QColor("#172e40")));
    node->setPos(points[a["id"].toString()]);
    node->setFlag(QGraphicsItem::ItemIsSelectable);
    node->setData(0, a["id"].toString());
    node->setToolTip(a["name"].toString() + "\n" + a["id"].toString() + " / revision " +
                     QString::number(a["revision"].toInt()));
    auto *text = new QGraphicsTextItem(a["name"].toString().left(90), node);
    text->setTextWidth(160);
    text->setPos(-80, -28);
    text->setDefaultTextColor(QColor("#e4f0f6"));
    text->setAcceptedMouseButtons(Qt::NoButton);
    if (a["id"] == root)
      node->setSelected(true);
  }
  graphStatus_->setText(
      QString("%1 nodes / 100 · %2 edges / 200 · %3 hop(s)%4")
          .arg(shownGraph_.nodes.size())
          .arg(shownGraph_.edges.size())
          .arg(graphDepth_->value())
          .arg(shownGraph_.truncated
                   ? " · TRUNCATED: neighborhood limit reached. Search another root or reduce hops."
                   : " · Neighborhood only; use search and Expand to explore."));
  static_cast<GraphView *>(graph_)->fitContents();
  graphSelection();
}
void InvestigationWorkbench::graphSelection() {
  if (scene_->selectedItems().isEmpty()) {
    graphDetail_->setPlainText("Select an asset to inspect its context.");
    return;
  }
  const auto aid = scene_->selectedItems().first()->data(0).toString();
  const auto a = row(shownGraph_.nodes, aid);
  QString text = a["name"].toString() + "\n" + a["category"].toString() + "\n" + aid +
                 " / revision " + QString::number(a["revision"].toInt()) + "\n\n";
  QSet<QString> snapshotIDs;
  const auto i = investigation(missions_.selected());
  for (auto v : i["snapshots"].toArray())
    if (v.toObject()["asset"].toObject()["id"] == aid)
      snapshotIDs.insert(v.toObject()["id"].toString());
  text += "RELATIONSHIPS\n";
  for (auto v : shownGraph_.edges) {
    const auto r = v.toObject();
    if (r["fromAssetID"] == aid || r["toAssetID"] == aid)
      text += r["kind"].toString() + " / " + r["reviewState"].toString() + "\n" +
              r["rationale"].toString() + "\n";
  }
  text += "\nFINDINGS\n";
  for (auto v : i["findings"].toArray()) {
    const auto f = v.toObject();
    bool linked = false;
    for (auto ref : f["assetSnapshotIDs"].toArray())
      linked = linked || snapshotIDs.contains(ref.toString());
    if (linked)
      text += f["state"].toString() + ": " + f["claim"].toString() + "\n";
  }
  text += "\nAPPROVED EVIDENCE\n";
  for (auto v : i["associations"].toArray()) {
    const auto r = v.toObject();
    if (snapshotIDs.contains(r["assetSnapshotID"].toString())) {
      const auto c = row(i["citations"].toArray(), r["citationID"].toString());
      text += c["label"].toString() + "\n" + r["reason"].toString() +
              "\nSource: " + dateLabel(c["sourceDate"]) + "\n";
    }
  }
  QString links;
  for (auto v : i["findings"].toArray()) {
    const auto f = v.toObject();
    bool related = false;
    for (auto sid : f["assetSnapshotIDs"].toArray())
      related = related || snapshotIDs.contains(sid.toString());
    if (related)
      links += "<p><a href='helios-finding:" + f["id"].toString() +
               "'>Open finding: " + f["claim"].toString().toHtmlEscaped() + "</a></p>";
  }
  for (auto v : i["associations"].toArray()) {
    const auto association = v.toObject();
    if (!snapshotIDs.contains(association["assetSnapshotID"].toString()))
      continue;
    const auto citation = row(i["citations"].toArray(), association["citationID"].toString());
    if (citation["evidence"].isObject())
      links += "<p><a href='helios-evidence:" + citation["id"].toString() +
               "'>Open retained evidence: " + citation["label"].toString().toHtmlEscaped() +
               "</a></p>";
  }
  graphDetail_->setHtml("<p>" + text.toHtmlEscaped().replace('\n', "<br/>") + "</p>" + links);
}
void InvestigationWorkbench::inspectFinding() {
  const auto f = row(investigation(missions_.selected())["findings"].toArray(), chosen(findings_));
  if (f.isEmpty()) {
    findingDetail_->clear();
    return;
  }
  const auto i = investigation(missions_.selected());
  QString text = f["claim"].toString() + "\n\n" + f["state"].toString() +
                 " / analyst judgment\nTime context: " + f["timeContext"].toString() +
                 "\n\nRationale\n" + f["rationale"].toString() + "\n\nAlternative explanations\n" +
                 f["alternatives"].toString() + "\n\nCitations\n";
  for (auto v : f["citations"].toArray()) {
    const auto c = row(i["citations"].toArray(), v.toObject()["citationID"].toString());
    text += v.toObject()["role"].toString() + ": " + c["label"].toString() + "\nSource " +
            c["provider"].toString() + " / " + c["sourceID"].toString() +
            "\nAcquisition/source date " + dateLabel(c["sourceDate"]) + "\nRecorded " +
            dateLabel(c["capturedAt"]) + "\n" + c["qualityLimitations"].toString() + "\n\n";
  }
  text += "Retained earlier decisions\n";
  for (auto v : f["revisions"].toArray())
    text += v.toObject()["state"].toString() + " / revision " +
            QString::number(v.toObject()["revision"].toInt()) + "\n" +
            v.toObject()["claim"].toString() + "\n" + v.toObject()["rationale"].toString() + "\n";
  findingDetail_->setPlainText(text);
  findingStatus_->setText(
      "Revision " + QString::number(f["revision"].toInt()) + " · " +
      QString::number(f["revisions"].toArray().size()) +
      " retained prior reviews · Source files may be unavailable; snapshots remain retained.");
}
void InvestigationWorkbench::promote(QJsonObject origin) {
  run([&] {
    const auto fid = missions_.draftFinding(missions_.selectedID(), origin);
    restore(findings_, fid);
    emit findingsRequested();
  });
}
void InvestigationWorkbench::editFinding(bool create) {
  const auto mid = missions_.selectedID();
  require(!mid.isEmpty(), "Select a mission.");
  const auto plan = missions_.selected(), i = investigation(plan);
  Finding finding;
  if (!create) {
    const auto f = row(i["findings"].toArray(), chosen(findings_));
    require(!f.isEmpty(), "Select a finding.");
    finding = Finding::read(f);
  }
  const int expected = create ? 0 : finding.revision;
  QDialog d(findingsPage_);
  d.setObjectName("findingEditor");
  d.setWindowTitle("Finding claim and analyst review");
  d.resize(900, 850);
  auto *l = new QVBoxLayout(&d);
  l->addWidget(notice(
      "Edits to a reviewed claim, time context, asset links or citations return it to Unreviewed. "
      "Save those edits, then review the resulting version. Prior decisions remain in the case."));
  QFormLayout *form;
  scrollForm(l, form);
  auto *claim = paragraph(form, "Claim", finding.claim);
  claim->setObjectName("findingClaim");
  auto *time = paragraph(form, "Time context", finding.timeContext);
  auto *alternatives = paragraph(form, "Alternatives / uncertainty", finding.alternatives);
  auto *rationale = paragraph(form, "Review rationale", finding.rationale);
  rationale->setObjectName("findingRationale");
  auto *state = boundedCombo();
  state->setObjectName("findingState");
  state->addItems({"Unreviewed", "Supported", "Uncertain", "Dismissed"});
  state->setCurrentText(finding.state);
  form->addRow("Analyst judgment", state);
  auto *assetLinks = new QListWidget;
  assetLinks->setMaximumHeight(150);
  for (auto v : i["snapshots"].toArray()) {
    const auto s = v.toObject(), a = s["asset"].toObject();
    if (a.isEmpty())
      continue;
    auto *item =
        new QListWidgetItem(a["name"].toString() + " / r" + QString::number(a["revision"].toInt()) +
                                " / " + dateLabel(s["capturedAt"]),
                            assetLinks);
    item->setData(Qt::UserRole, s["id"].toString());
    item->setCheckState(finding.assetSnapshotIDs.contains(s["id"].toString()) ? Qt::Checked
                                                                              : Qt::Unchecked);
  }
  form->addRow("Retained asset versions", assetLinks);
  auto *areas = new QListWidget;
  areas->setMaximumHeight(110);
  for (auto v : plan["brief"].toObject()["areas"].toArray()) {
    auto *item = new QListWidgetItem(v.toObject()["name"].toString(), areas);
    item->setData(Qt::UserRole, v.toObject()["id"].toString());
    item->setCheckState(finding.areaIDs.contains(v.toObject()["id"].toString()) ? Qt::Checked
                                                                                : Qt::Unchecked);
  }
  form->addRow("Relevant areas", areas);
  auto *evidence = boundedCombo();
  evidence->addItem("Retain a mission evidence snapshot…", QString{});
  for (auto v : plan["items"].toArray())
    evidence->addItem(v.toObject()["title"].toString(), v.toObject()["id"].toString());
  form->addRow("Additional evidence", evidence);
  auto *table = new QTableWidget(0, 2);
  table->setObjectName("findingCitations");
  table->setHorizontalHeaderLabels({"Retained citation", "Role"});
  table->setColumnWidth(1, 180);
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table->setMinimumHeight(240);
  table->setMaximumHeight(340);
  form->addRow("Evidence roles", table);
  QJsonArray available = i["citations"].toArray();
  auto append = [&](const QJsonObject &c, QString role) {
    const int n = table->rowCount();
    table->insertRow(n);
    auto *item = new QTableWidgetItem(c["label"].toString() + " / " + c["sourceID"].toString());
    item->setData(Qt::UserRole, c["id"].toString());
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setToolTip(c["qualityLimitations"].toString());
    table->setItem(n, 0, item);
    auto *roles = boundedCombo();
    roles->addItems({"Not used", "Supporting", "Contradicting", "Context"});
    roles->setCurrentText(role);
    table->setCellWidget(n, 1, roles);
  };
  for (auto v : available) {
    QString role = "Not used";
    for (auto ref : finding.citations)
      if (ref.toObject()["citationID"] == v.toObject()["id"])
        role = ref.toObject()["role"].toString();
    append(v.toObject(), role);
  }
  connect(evidence, &QComboBox::activated, &d, [&](int n) {
    if (n <= 0)
      return;
    const auto e = row(plan["items"].toArray(), evidence->itemData(n).toString());
    if (e.isEmpty())
      return;
    const auto c = citationForEvidence(e);
    available.append(c);
    append(c, "Context");
  });
  auto *manualLabel = line(form, "New citation label", {}, 1024),
       *manualSource = line(form, "Source URL / document reference", {}, 4096);
  auto *manualLimit = paragraph(form, "Source quality limitations", {});
  saveButtons(l, d, [&] {
    require(mid == missions_.selectedID(),
            "The selected mission changed. Your finding draft is retained.");
    // Retain all new citations and the finding in one mutation, avoiding partial saves.
    finding.claim = claim->toPlainText();
    finding.timeContext = time->toPlainText();
    finding.alternatives = alternatives->toPlainText();
    finding.rationale = rationale->toPlainText();
    finding.state = state->currentText();
    finding.assetSnapshotIDs.clear();
    for (int n = 0; n < assetLinks->count(); ++n)
      if (assetLinks->item(n)->checkState() == Qt::Checked)
        finding.assetSnapshotIDs << assetLinks->item(n)->data(Qt::UserRole).toString();
    QStringList liveAreas;
    for (int n = 0; n < areas->count(); ++n)
      liveAreas << areas->item(n)->data(Qt::UserRole).toString();
    QStringList retained;
    for (const auto &id : finding.areaIDs)
      if (!liveAreas.contains(id))
        retained << id;
    finding.areaIDs = retained;
    for (int n = 0; n < areas->count(); ++n)
      if (areas->item(n)->checkState() == Qt::Checked)
        finding.areaIDs << areas->item(n)->data(Qt::UserRole).toString();
    QJsonArray newCitations;
    finding.citations = {};
    for (int n = 0; n < table->rowCount(); ++n) {
      const auto role = qobject_cast<QComboBox *>(table->cellWidget(n, 1))->currentText();
      if (role == "Not used")
        continue;
      const auto cid = table->item(n, 0)->data(Qt::UserRole).toString();
      finding.citations.append(QJsonObject{{"citationID", cid}, {"role", role}});
      if (row(i["citations"].toArray(), cid).isEmpty())
        newCitations.append(row(available, cid));
    }
    if (!manualSource->text().trimmed().isEmpty()) {
      auto c = manualCitation(manualLabel->text(), manualSource->text());
      c["qualityLimitations"] = manualLimit->toPlainText();
      newCitations.append(c);
      finding.citations.append(QJsonObject{{"citationID", c["id"]}, {"role", "Context"}});
    }
    const auto fid = missions_.saveFinding(mid, finding, expected, newCitations);
    restore(findings_, fid);
  });
  d.exec();
}
void InvestigationWorkbench::briefing() {
  const auto plan = missions_.selected();
  require(!plan.isEmpty(), "Select a mission.");
  const auto rows = investigation(plan)["findings"].toArray();
  require(!rows.isEmpty(), "Create a finding before preparing a briefing.");
  BriefingDialog d(overview_);
  d.setObjectName("briefingPreview");
  d.setWindowTitle("Select findings and preview frozen briefing");
  d.resize(1000, 850);
  auto *l = new QVBoxLayout(&d);
  l->addWidget(notice("Select findings explicitly. Prepare freezes source records and available "
                      "previews. Export publishes a new PDF and JSON folder together. It does not "
                      "change review states or contact data providers."));
  auto *list = new QListWidget;
  list->setObjectName("briefingFindingSelection");
  list->setMaximumHeight(180);
  for (auto v : rows) {
    auto *item = new QListWidgetItem(
        v.toObject()["state"].toString() + " / " + v.toObject()["claim"].toString(), list);
    item->setData(Qt::UserRole, v.toObject()["id"].toString());
    item->setCheckState(Qt::Unchecked);
  }
  l->addWidget(list);
  auto *captureNotice = notice("Map context: not captured");
  l->addWidget(captureNotice);
  QImage map;
  QJsonObject capture;
  auto *actions = new QHBoxLayout;
  l->addLayout(actions);
  auto *preview = new QTextBrowser;
  preview->setObjectName("briefingDocumentPreview");
  preview->setOpenExternalLinks(false);
  l->addWidget(preview, 1);
  auto *status = notice({});
  l->addWidget(status);
  std::optional<FrozenBriefing> frozen;
  auto *captureButton = button(actions, "Capture current map context", &d, [&] {
    if (captureProvider_) {
      const auto result = captureProvider_();
      map = result.first;
      capture = result.second;
      captureNotice->setText("Captured " + dateLabel(capture["capturedAt"]) + " / " +
                             capture["attribution"].toString());
      frozen.reset();
      preview->clear();
    }
  });
  captureButton->setEnabled(bool(captureProvider_));
  QFutureWatcher<FrozenBriefing> prepare;
  QFutureWatcher<QString> write;
  d.busy = [&] { return prepare.isRunning() || write.isRunning(); };
  auto *prepareButton = button(actions, "Prepare preview", &d, [&] {
    run([&] {
      require(!prepare.isRunning() && !write.isRunning(),
              "A briefing operation is already running.");
      QStringList selected;
      for (int n = 0; n < list->count(); ++n)
        if (list->item(n)->checkState() == Qt::Checked)
          selected << list->item(n)->data(Qt::UserRole).toString();
      require(!selected.isEmpty(), "Select at least one finding.");
      status->setText("Freezing case and checking local previews…");
      frozen.reset();
      prepare.setFuture(QtConcurrent::run([plan, selected, library = library_, map, capture] {
        return prepareBriefing(plan, selected, library, map, capture);
      }));
    });
  });
  auto *exportButton = button(actions, "Export frozen PDF + manifest…", &d, [&] {
    run([&] {
      require(frozen.has_value(), "Prepare the preview first.");
      const auto parent =
          QFileDialog::getExistingDirectory(&d, "Choose parent folder for new briefing");
      if (parent.isEmpty())
        return;
      const auto value = *frozen;
      status->setText("Writing native PDF and evidence manifest…");
      write.setFuture(QtConcurrent::run([value, parent] { return exportBriefing(value, parent); }));
    });
  });
  prepareButton->setObjectName("briefingPrepare");
  exportButton->setObjectName("briefingExport");
  exportButton->setEnabled(false);
  connect(captureButton, &QPushButton::clicked, &d,
          [exportButton] { exportButton->setEnabled(false); });
  connect(&prepare, &QFutureWatcher<FrozenBriefing>::finished, &d, [&] {
    run([&] {
      frozen = prepare.result();
      preview->setHtml(frozen->html);
      for (auto it = frozen->images.begin(); it != frozen->images.end(); ++it)
        preview->document()->addResource(QTextDocument::ImageResource, QUrl(it.key()), it.value());
      preview->document()->markContentsDirty(0, preview->document()->characterCount());
      exportButton->setEnabled(true);
      status->setText("Frozen preview ready. Source availability is recorded in the briefing. "
                      "Current mission edits will not alter this preview.");
    });
  });
  connect(&write, &QFutureWatcher<QString>::finished, &d, [&] {
    run([&] {
      const auto path = write.result();
      status->setText("Exported PDF and manifest together: " + path);
    });
  });
  connect(list, &QListWidget::itemChanged, &d, [&] {
    frozen.reset();
    preview->clear();
    exportButton->setEnabled(false);
  });
  auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
  l->addWidget(close);
  connect(close, &QDialogButtonBox::rejected, &d, [&] {
    if (!prepare.isRunning() && !write.isRunning())
      d.reject();
    else
      status->setText("Wait for the local briefing operation to finish before closing.");
  });
  connect(&prepare, &QFutureWatcher<FrozenBriefing>::started, &d, [&] {
    list->setEnabled(false);
    exportButton->setEnabled(false);
    prepareButton->setEnabled(false);
    captureButton->setEnabled(false);
  });
  connect(&prepare, &QFutureWatcher<FrozenBriefing>::finished, &d, [&] {
    list->setEnabled(true);
    prepareButton->setEnabled(true);
    captureButton->setEnabled(bool(captureProvider_));
  });
  connect(&write, &QFutureWatcher<QString>::started, &d, [&] {
    list->setEnabled(false);
    captureButton->setEnabled(false);
    exportButton->setEnabled(false);
    prepareButton->setEnabled(false);
  });
  connect(&write, &QFutureWatcher<QString>::finished, &d, [&] {
    list->setEnabled(true);
    captureButton->setEnabled(bool(captureProvider_));
    exportButton->setEnabled(frozen.has_value());
    prepareButton->setEnabled(true);
  });
  d.exec();
  d.busy = {};
  try {
    prepare.waitForFinished();
    write.waitForFinished();
  } catch (...) {
  }
}
} // namespace helios

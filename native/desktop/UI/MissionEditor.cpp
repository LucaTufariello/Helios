#include "MissionEditor.h"
#include "Core/Timeline.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTimeZone>
#include <QVBoxLayout>
namespace godeye {
bool editMissionEntry(QWidget *parent, Missions &missions, const QString &collection,
                      const QString &id, const std::function<void()> &validateContext) {
  const auto missionID = missions.selectedID();
  const auto plan = missions.selected(), brief = plan["brief"].toObject();
  const auto values = collection == "items" ? plan["items"].toArray() : brief[collection].toArray();
  QJsonObject original;
  for (const auto value : values)
    if (value.toObject()["id"] == id)
      original = value.toObject();
  if (original.isEmpty())
    return false;
  const QJsonObject baseline = original;
  QDialog dialog(parent);
  dialog.setObjectName("missionEntryEditor");
  dialog.setWindowTitle("Edit mission " + collection + " entry");
  dialog.resize(640, 580);
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout;
  layout->addLayout(form);
  QMap<QString, std::function<QJsonValue()>> fields;
  auto line = [&](const QString &key, const QString &label, int maximum) {
    auto *field = new QLineEdit(original[key].toString());
    field->setObjectName("entry_" + key);
    field->setMaxLength(maximum);
    form->addRow(label, field);
    fields[key] = [field] { return field->text(); };
  };
  auto text = [&](const QString &key, const QString &label) {
    auto *field = new QPlainTextEdit(original[key].toString());
    field->setObjectName("entry_" + key);
    field->setMaximumHeight(140);
    form->addRow(label, field);
    fields[key] = [field] { return field->toPlainText(); };
  };
  auto link = [&](const QString &key, const QString &label, const QJsonArray &choices,
                  const QString &title) {
    auto *field = new QComboBox;
    field->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    field->setMinimumContentsLength(24);
    field->addItem("None", QString{});
    for (const auto value : choices) {
      const auto o = value.toObject();
      field->addItem(o[title].toString().simplified().left(160), o["id"].toString());
      field->setItemData(field->count() - 1, o[title].toString(), Qt::ToolTipRole);
    }
    field->setCurrentIndex(std::max(0, field->findData(original[key].toString())));
    form->addRow(label, field);
    fields[key] = [field]() -> QJsonValue {
      const auto id = field->currentData().toString();
      return id.isEmpty() ? QJsonValue::Null : QJsonValue(id);
    };
  };
  auto date = [&](const QString &key, const QString &label) {
    auto *enabled = new QCheckBox(label);
    enabled->setObjectName("entry_" + key + "Enabled");
    enabled->setChecked(original.contains(key) && !original[key].isNull());
    auto *field = new QDateTimeEdit;
    field->setObjectName("entry_" + key);
    field->setTimeZone(QTimeZone::UTC);
    field->setDisplayFormat("yyyy-MM-dd HH:mm:ss 'UTC'");
    field->setCalendarPopup(true);
    auto time = missionTime(original[key]);
    field->setDateTime(time.isValid() ? time : QDateTime::currentDateTimeUtc());
    field->setEnabled(enabled->isChecked());
    QObject::connect(enabled, &QCheckBox::toggled, field, &QWidget::setEnabled);
    form->addRow(enabled, field);
    fields[key] = [enabled, field]() -> QJsonValue {
      return enabled->isChecked()
                 ? QJsonValue(field->dateTime().toMSecsSinceEpoch() / 1000.0 - 978307200.0)
                 : QJsonValue::Null;
    };
  };
  auto spin = [&](const QString &label, double value, double low, double high) {
    auto *field = new QDoubleSpinBox;
    field->setRange(low, high);
    field->setDecimals(6);
    field->setValue(value);
    form->addRow(label, field);
    return field;
  };
  if (collection == "areas") {
    line("name", "Name", 160);
    text("purpose", "Purpose");
    const auto bounds = Bounds::read(original["region"]);
    if (!bounds)
      return false;
    auto *west = spin("West longitude", bounds->west, -180, 180),
         *east = spin("East longitude", bounds->east, -180, 180),
         *south = spin("South latitude", bounds->south, -85, 85),
         *north = spin("North latitude", bounds->north, -85, 85);
    fields["region"] = [=] {
      return Bounds{west->value(), south->value(), east->value(), north->value()}.json();
    };
  } else {
    line("title", "Title", collection == "items" ? 240 : 240);
    if (collection == "notes") {
      text("body", "Note body");
      line("reference", "Citation / reference", 2000);
      link("evidenceID", "Evidence", plan["items"].toArray(), "title");
    }
    if (collection == "tasks") {
      date("due", "Due date known");
      line("owner", "Local owner label", 160);
      link("findingID", "Finding origin", brief["investigation"].toObject()["findings"].toArray(),
           "claim");
      auto *dependencies = new QListWidget;
      dependencies->setObjectName("taskDependencies");
      for (auto v : brief["tasks"].toArray()) {
        const auto t = v.toObject();
        if (t["id"] == id)
          continue;
        auto *item = new QListWidgetItem(t["title"].toString() +
                                             (t["done"].toBool() ? " (done)" : " (unfinished)"),
                                         dependencies);
        item->setData(Qt::UserRole, t["id"]);
        item->setCheckState(original["dependencyIDs"].toArray().contains(t["id"]) ? Qt::Checked
                                                                                  : Qt::Unchecked);
      }
      form->addRow("Prerequisite tasks", dependencies);
      fields["dependencyIDs"] = [dependencies] {
        QJsonArray refs;
        for (int n = 0; n < dependencies->count(); ++n)
          if (dependencies->item(n)->checkState() == Qt::Checked)
            refs.append(dependencies->item(n)->data(Qt::UserRole).toString());
        return refs;
      };
      link("observationID", "Follow-up observation", brief["observations"].toArray(), "title");
    }
    if (collection == "observations") {
      line("category", "Category", 120);
      date("observedAt", "Observation time known");
      link("evidenceID", "Evidence", plan["items"].toArray(), "title");
      if (!original.contains("modelName")) {
        const auto coordinate = Coordinate::read(original["coordinate"].toObject());
        if (coordinate) {
          auto *lat = spin("Latitude", coordinate->latitude, -90, 90),
               *lon = spin("Longitude", coordinate->longitude, -180, 180);
          fields["coordinate"] = [=] { return Coordinate{lat->value(), lon->value()}.json(); };
        }
      }
    }
    if (collection != "items")
      link("areaID", "Area", brief["areas"].toArray(), "name");
  }
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    try {
      if (missions.selectedID() != missionID)
        throw std::runtime_error(
            "The selected mission changed. Close this editor and choose the entry again.");
      if (validateContext)
        validateContext();
      const auto currentPlan = missions.selected();
      const auto currentRows = collection == "items"
                                   ? currentPlan["items"].toArray()
                                   : currentPlan["brief"].toObject()[collection].toArray();
      QJsonObject current;
      for (auto value : currentRows)
        if (value.toObject()["id"] == id)
          current = value.toObject();
      if (current != baseline)
        throw std::runtime_error("This entry changed while the editor was open. Your draft has "
                                 "been retained; reopen the current entry before saving.");
      QJsonObject edits;
      for (auto i = fields.begin(); i != fields.end(); ++i)
        edits[i.key()] = i.value()();
      missions.editEntry(collection, id, edits);
      dialog.accept();
    } catch (const std::exception &e) {
      QMessageBox::warning(&dialog, "Edit entry", QString::fromUtf8(e.what()));
    }
  });
  return dialog.exec() == QDialog::Accepted;
}
} // namespace godeye

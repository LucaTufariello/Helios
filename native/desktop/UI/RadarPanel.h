#pragma once
#include "Core/RadarCatalog.h"
#include "Core/RadarDownload.h"
#include "Graphics/MapWidget.h"
#include <QWidget>
#include <functional>
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QProgressBar;
namespace godeye {
class RadarPanel : public QWidget {
  Q_OBJECT
public:
  RadarPanel(const QString &profile, MapWidget &map, std::function<Bounds()> region,
             QWidget *parent = nullptr, bool networking = true);
  bool busy() const;
  void cancel();
  bool setQuery(const RadarQuery &query);
  QVector<RadarScene> scenes() const {
    return scenes_;
  }
signals:
  void changed();
  void status(QString message);
  void inspect(QString path);

private:
  MapWidget &map_;
  bool networking_ = true;
  std::function<Bounds()> region_;
  RadarCatalog catalog_;
  CopernicusAccount account_;
  RadarDownload download_;
  QVector<RadarScene> scenes_;
  std::optional<RadarQuery> query_;
  int skip_ = 0;
  bool hasMore_ = false;
  QDateEdit *start_, *end_;
  QComboBox *polarization_, *mode_;
  QLineEdit *username_, *password_, *totp_, *library_;
  QLabel *accountStatus_, *selection_;
  QListWidget *results_;
  QPushButton *search_, *next_, *retrieve_, *signIn_, *signOut_;
  QProgressBar *progress_;
  void search(bool next);
  void refresh();
  void selected();
};
} // namespace godeye

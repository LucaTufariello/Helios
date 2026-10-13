#pragma once
#include "Core/Products.h"
#include <QDialog>
#include <QSettings>
class QListWidget;
class QLabel;
class QPlainTextEdit;
class QPushButton;
namespace godeye {
class SatelliteWindow : public QDialog {
  Q_OBJECT
public:
  SatelliteWindow(const QString &profile, QWidget *parent = nullptr);
  void importManifest(const QString &filename);
  void showProduct(const QString &id, bool compare = false);
  QVector<ProductLayer> displayed() const {
    return displayed_;
  }
  QVector<ProductLayer> library() const {
    return library_;
  }
  bool busy() const {
    return busy_;
  }
signals:
  void layersChanged(QVector<godeye::ProductLayer> layers, bool focus);
  void linkRequested(godeye::ProductLayer product);
  void blendChanged(double value);
  void pixelGridChanged(bool grid);

private:
  QSettings settings_;
  QVector<ProductLayer> library_, displayed_;
  QStringList pendingImports_;
  QListWidget *list_;
  QLabel *summary_, *thumbnail_, *status_;
  QPlainTextEdit *metadata_;
  QPushButton *scientific_, *visualization_, *png_, *cancel_;
  bool busy_ = false;
  std::shared_ptr<std::atomic_bool> cancelled_;
  void selected();
  void pumpImports();
  void exportSelected(const QString &kind);
  void remember();
};
} // namespace godeye

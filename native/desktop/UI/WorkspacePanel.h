#pragma once
#include "WorkspaceStyle.h"
#include <QFrame>
#include <QPointer>
#include <QVariantMap>
class QLabel;
class QToolButton;
namespace godeye {
// Bounded native palettes above the map; uncovered map input stays available.
class WorkspacePanel : public QFrame {
  Q_OBJECT
public:
  WorkspacePanel(const QString &id, const QString &title, design::Symbol symbol, QWidget *content,
                 QWidget *surface, QSize preferred, bool right = false);
  void reveal();
  void constrain();
  void resetLayout();
  void setCollapsed(bool collapsed);
  bool collapsed() const {
    return collapsed_;
  }
  QString panelID() const {
    return id_;
  }
  QVariantMap layoutState() const;
  void restoreLayout(const QVariantMap &state);
signals:
  void visibilityChanged(bool visible);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  QString id_;
  QWidget *header_, *content_, *body_, *grip_;
  QToolButton *collapse_;
  QSize preferred_, expanded_;
  bool changing_ = false;
  bool right_, positioned_ = false, collapsed_ = false, dragging_ = false, resizing_ = false;
  QPoint press_;
  QRect start_;
};
class WorkspaceSurface : public QWidget {
  Q_OBJECT
public:
  explicit WorkspaceSurface(QWidget *map, QWidget *parent = nullptr);
  void setCommands(QWidget *search, QWidget *actions);
  void addPanel(WorkspacePanel *panel);
  void hidePanels();
  void resetPanels();

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  QWidget *map_, *search_ = nullptr, *actions_ = nullptr;
  QList<QPointer<WorkspacePanel>> panels_;
  void arrange();
};
} // namespace godeye

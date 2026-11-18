#include "WorkspacePanel.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
namespace helios {
WorkspacePanel::WorkspacePanel(const QString &id, const QString &title, design::Symbol symbol,
                               QWidget *content, QWidget *surface, QSize preferred, bool right)
    : QFrame(surface), id_(id), content_(content), preferred_(preferred), expanded_(preferred),
      right_(right) {
  setObjectName("workspacePanel-" + id);
  setAccessibleName(title + " panel");
  setProperty("role", "panel");
  setAttribute(Qt::WA_StyledBackground);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(1, 1, 1, 1);
  layout->setSpacing(0);
  layout->setSizeConstraint(QLayout::SetNoConstraint);
  header_ = new QWidget;
  header_->setObjectName("panelDragHandle");
  header_->setCursor(Qt::OpenHandCursor);
  header_->setFixedHeight(42);
  header_->installEventFilter(this);
  auto *bar = new QHBoxLayout(header_);
  bar->setContentsMargins(12, 0, 8, 0);
  bar->setSpacing(8);
  auto *glyph = new QLabel;
  glyph->setPixmap(design::icon(symbol).pixmap(18, 18));
  glyph->setAttribute(Qt::WA_TransparentForMouseEvents);
  bar->addWidget(glyph);
  auto *caption = design::label(title, "panelTitle");
  auto font = caption->font();
  font.setWeight(QFont::DemiBold);
  caption->setFont(font);
  caption->setAttribute(Qt::WA_TransparentForMouseEvents);
  bar->addWidget(caption, 1);
  auto tool = [&](design::Symbol s, const QString &name) {
    auto *b = new QToolButton;
    b->setIcon(design::icon(s));
    b->setToolTip(name);
    b->setAccessibleName(name);
    b->setProperty("role", "quiet");
    b->setFixedSize(27, 28);
    bar->addWidget(b);
    return b;
  };
  auto *more = tool(design::Symbol::More, "Panel layout");
  auto *menu = new QMenu(more);
  connect(menu->addAction("Reset size and position"), &QAction::triggered, this,
          &WorkspacePanel::resetLayout);
  more->setMenu(menu);
  more->setPopupMode(QToolButton::InstantPopup);
  collapse_ = tool(design::Symbol::Minus, "Collapse " + title);
  collapse_->setObjectName("panelCollapse");
  connect(collapse_, &QToolButton::clicked, this, [this] { setCollapsed(!collapsed_); });
  auto *close = tool(design::Symbol::Close, "Close " + title);
  close->setObjectName("panelClose");
  connect(close, &QToolButton::clicked, this, &QWidget::hide);
  layout->addWidget(header_);
  content_->setParent(this, Qt::Widget);
  content_->setMinimumSize(0, 0);
  if (content_->layout())
    content_->layout()->setSizeConstraint(QLayout::SetMinimumSize);
  auto *scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(content_);
  body_ = scroll;
  layout->addWidget(body_, 1);
  content_->installEventFilter(this);
  for (auto *child : content_->findChildren<QWidget *>())
    child->installEventFilter(this);
  auto *foot = new QWidget;
  foot->setFixedHeight(12);
  auto *footer = new QHBoxLayout(foot);
  footer->setContentsMargins(0, 0, 3, 0);
  footer->addStretch();
  grip_ = new QLabel("◢");
  grip_->setFixedSize(15, 12);
  grip_->setCursor(Qt::SizeFDiagCursor);
  grip_->setAccessibleName("Resize " + title);
  grip_->setToolTip("Drag to resize panel");
  grip_->installEventFilter(this);
  footer->addWidget(grip_);
  layout->addWidget(foot);
  content_->show();
  hide();
}
void WorkspacePanel::reveal() {
  if (collapsed_)
    setCollapsed(false);
  changing_ = true;
  content_->show();
  changing_ = false;
  show();
  raise();
}
void WorkspacePanel::resetLayout() {
  positioned_ = false;
  expanded_ = preferred_;
  setCollapsed(false);
  resize(preferred_);
  constrain();
}
void WorkspacePanel::constrain() {
  if (!parentWidget())
    return;
  const int w = std::max(200, parentWidget()->width() - 24);
  const int h = std::max(44, parentWidget()->height() - 88);
  if (!positioned_) {
    resize(std::min(preferred_.width(), w), collapsed_ ? 44 : std::min(preferred_.height(), h));
    move(right_ ? parentWidget()->width() - width() - 12 : 12, 72);
    positioned_ = true;
  }
  resize(std::min(width(), w), collapsed_ ? 44 : std::min(height(), h));
  move(std::clamp(x(), 12, std::max(12, parentWidget()->width() - width() - 12)),
       std::clamp(y(), 72, std::max(72, parentWidget()->height() - height() - 12)));
}
void WorkspacePanel::setCollapsed(bool collapsed) {
  if (collapsed_ == collapsed)
    return;
  collapsed_ = collapsed;
  if (collapsed)
    expanded_ = size();
  changing_ = true;
  body_->setVisible(!collapsed);
  changing_ = false;
  // Only the header participates in the collapsed geometry.
  layout()->itemAt(2)->widget()->setVisible(!collapsed);
  collapse_->setIcon(design::icon(collapsed ? design::Symbol::Plus : design::Symbol::Minus));
  collapse_->setToolTip(collapsed ? "Expand panel" : "Collapse panel");
  collapse_->setAccessibleName(collapsed ? "Expand panel" : "Collapse panel");
  resize(collapsed ? QSize(width(), 44) : expanded_);
  constrain();
}
QVariantMap WorkspacePanel::layoutState() const {
  const auto size = collapsed_ ? expanded_ : this->size();
  return {{"x", x()}, {"y", y()}, {"width", size.width()}, {"height", size.height()}};
}
void WorkspacePanel::restoreLayout(const QVariantMap &s) {
  if (!s.contains("width"))
    return;
  resize(std::clamp(s.value("width").toInt(), 280, 1600),
         std::clamp(s.value("height").toInt(), 280, 1200));
  move(s.value("x", 12).toInt(), s.value("y", 72).toInt());
  positioned_ = true;
  expanded_ = size();
}
bool WorkspacePanel::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::MouseButtonPress)
    raise();
  if (watched == content_ && event->type() == QEvent::ShowToParent && !collapsed_ && !changing_) {
    show();
    raise();
  }
  if (watched == content_ && event->type() == QEvent::HideToParent && !changing_)
    hide();
  if (watched != header_ && watched != grip_)
    return QFrame::eventFilter(watched, event);
  if (event->type() == QEvent::MouseButtonDblClick && watched == header_) {
    setCollapsed(!collapsed_);
    return true;
  }
  if (event->type() == QEvent::MouseButtonPress) {
    const auto *m = static_cast<QMouseEvent *>(event);
    if (m->button() != Qt::LeftButton)
      return false;
    dragging_ = watched == header_;
    resizing_ = watched == grip_;
    press_ = m->globalPosition().toPoint();
    start_ = geometry();
    raise();
    return true;
  }
  if (event->type() == QEvent::MouseMove && (dragging_ || resizing_)) {
    const auto delta = static_cast<QMouseEvent *>(event)->globalPosition().toPoint() - press_;
    if (dragging_)
      move(start_.topLeft() + delta);
    else
      resize(std::max(320, start_.width() + delta.x()), std::max(280, start_.height() + delta.y()));
    constrain();
    return true;
  }
  if (event->type() == QEvent::MouseButtonRelease) {
    dragging_ = resizing_ = false;
    return true;
  }
  return QFrame::eventFilter(watched, event);
}
void WorkspacePanel::showEvent(QShowEvent *event) {
  constrain();
  QFrame::showEvent(event);
  emit visibilityChanged(true);
}
void WorkspacePanel::hideEvent(QHideEvent *event) {
  QFrame::hideEvent(event);
  emit visibilityChanged(false);
}
WorkspaceSurface::WorkspaceSurface(QWidget *map, QWidget *parent) : QWidget(parent), map_(map) {
  setObjectName("mapWorkspace");
  map_->setParent(this);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(map_);
}
void WorkspaceSurface::setCommands(QWidget *search, QWidget *actions) {
  search_ = search;
  actions_ = actions;
  search->setParent(this);
  actions->setParent(this);
  arrange();
}
void WorkspaceSurface::addPanel(WorkspacePanel *panel) {
  panels_.append(panel);
}
void WorkspaceSurface::hidePanels() {
  for (const auto &panel : panels_)
    if (panel)
      panel->hide();
  map_->setFocus();
}
void WorkspaceSurface::resetPanels() {
  for (const auto &panel : panels_)
    if (panel)
      panel->resetLayout();
}
void WorkspaceSurface::arrange() {
  if (search_ && actions_) {
    const int actionWidth = actions_->sizeHint().width();
    const int searchWidth = std::min(320, std::max(180, width() - actionWidth - 48));
    search_->setGeometry(16, 14, searchWidth, 44);
    actions_->setGeometry(width() - actionWidth - 16, 14, actionWidth, 44);
  }
  for (const auto &panel : panels_)
    if (panel && panel->isVisible())
      panel->constrain();
}
void WorkspaceSurface::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  arrange();
}
} // namespace helios

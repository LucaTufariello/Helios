#include "WorkspaceStyle.h"
#include <QApplication>
#include <QFontDatabase>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>
#include <QToolButton>
#include <QVBoxLayout>
namespace helios::design {
void apply(QApplication &app) {
  app.setStyle(QStyleFactory::create("Fusion"));
  QFont font = app.font();
  const auto families = QFontDatabase::families();
  for (const auto &family : {"Inter", "Noto Sans", "DejaVu Sans"})
    if (families.contains(QString::fromLatin1(family))) {
      font.setFamily(QString::fromLatin1(family));
      break;
    }
  font.setPixelSize(13);
  app.setFont(font);
  QPalette p;
  p.setColor(QPalette::Window, QColor("#111a26"));
  p.setColor(QPalette::WindowText, QColor("#e1e8f0"));
  p.setColor(QPalette::Base, QColor("#0d1520"));
  p.setColor(QPalette::AlternateBase, QColor("#162231"));
  p.setColor(QPalette::Text, QColor("#e1e8f0"));
  p.setColor(QPalette::Button, QColor("#1c2a3b"));
  p.setColor(QPalette::ButtonText, QColor("#dce5ee"));
  p.setColor(QPalette::Highlight, QColor("#234b60"));
  p.setColor(QPalette::HighlightedText, QColor("#effcff"));
  p.setColor(QPalette::Link, QColor("#75cddd"));
  p.setColor(QPalette::PlaceholderText, QColor("#8b9bad"));
  p.setColor(QPalette::ToolTipBase, QColor("#243246"));
  p.setColor(QPalette::ToolTipText, QColor("#eef4fa"));
  p.setColor(QPalette::Disabled, QPalette::Text, QColor("#7d8b9c"));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#7d8b9c"));
  p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#7d8b9c"));
  app.setPalette(p);
  app.setWindowIcon(icon(Symbol::Globe));
  app.setStyleSheet(QString::fromUtf8(R"QSS(
QMainWindow, QDialog { background: #111a26; }
QWidget[role="canvas"] { background: #0a111b; }
QWidget[role="header"], QWidget[role="panel"], QWidget[role="sidebar"] { background: #111c2a; }
QWidget[role="header"] { border-bottom: 1px solid #2a394b; }
QWidget[role="panel"] { border: 1px solid #34465a; border-radius: 9px; }
QLabel { background: transparent; }
QLabel[role="title"] { color: #eff5fb; font-size: 22px; font-weight: 600; }
QLabel[role="brand"] { color: #eff5fb; font-size: 18px; font-weight: 600; letter-spacing: 1px; }
QLabel[role="kicker"] { color: #78c6d7; font-size: 10px; font-weight: 600; letter-spacing: 1.5px; }
QLabel[role="secondary"] { color: #a0b0c2; font-size: 12px; }
QLabel[role="mono"] { color: #b6c8da; font-family: "DejaVu Sans Mono", monospace; font-size: 11px; }
QLabel[role="notice"] { color: #e8c68a; background: #2c281e; padding: 8px 12px; border-radius: 4px; }
QLabel[role="badge"] { color: #9cdeca; background: #1b3435; border: 1px solid #2e5350; border-radius: 4px; padding: 4px 8px; font-size: 10px; font-weight: 600; letter-spacing: 1px; }
QPushButton, QToolButton { background: #1a293a; color: #dce5ee; border: 1px solid #34465b; border-radius: 5px; min-height: 28px; padding: 2px 11px; }
QPushButton[role="summaryCard"] { min-height: 102px; padding: 0; text-align: left; background: #162535; }
QPushButton:hover, QToolButton:hover { background: #24394c; border-color: #65879b; }
QPushButton:pressed, QToolButton:pressed { background: #284c60; }
QPushButton:focus, QToolButton:focus { border: 1px solid #84d4e2; }
QPushButton:disabled, QToolButton:disabled { background: #15202d; color: #7d8b9c; border-color: #293747; }
QPushButton[role="primary"], QToolButton[role="primary"] { color: #081b24; background: #8bd1db; border-color: #8bd1db; font-weight: 600; }
QPushButton[role="primary"]:hover, QToolButton[role="primary"]:hover { background: #b1e6ec; }
QPushButton[role="primary"]:disabled { color: #8197a5; background: #243c49; border-color: #344957; }
QToolButton[role="nav"] { background: transparent; border: 1px solid transparent; color: #aebed0; padding: 5px 12px; font-weight: 500; }
QToolButton[role="nav"]:hover { background: #1b2c3e; color: #eff7fc; }
QToolButton[role="nav"]:checked { background: #203d4e; border-color: #366074; color: #a8e3ea; }
QToolButton[role="quiet"] { background: transparent; border-color: transparent; padding: 2px 6px; }
QToolButton[role="quiet"]:hover, QToolButton[role="quiet"]:checked { background: #284053; color: #a8e3ea; }
QLineEdit, QTextEdit, QPlainTextEdit, QAbstractSpinBox, QComboBox { background: #0c1622; color: #e1e8f0; border: 1px solid #34465b; border-radius: 5px; padding: 6px 8px; selection-background-color: #2d566e; }
QLineEdit#workspaceSearch { border: none; background: transparent; padding: 4px 0; }
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QAbstractSpinBox:focus, QComboBox:focus { border-color: #7fc5d5; }
QLineEdit:disabled, QAbstractSpinBox:disabled, QComboBox:disabled { color: #7d8b9c; background: #141e2b; }
QComboBox { padding-right: 24px; min-height: 19px; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: #192a3b; color: #edf5fb; selection-background-color: #2c5267; border: 1px solid #456074; }
QListView, QTreeView, QTableView { background: #0d1723; alternate-background-color: #142131; border: 1px solid #2d3e51; border-radius: 5px; outline: 0; }
QListView::item { padding: 9px 8px; border-bottom: 1px solid #1c2b3a; }
QListView::item:hover { background: #1a2e40; }
QListView::item:selected { background: #23495c; color: #edfcff; border-bottom-color: #376679; }
QListWidget[role="navigation"] { background: transparent; border: none; }
QListWidget[role="navigation"]::item { padding: 11px 12px; margin: 2px 0; border: 1px solid transparent; border-radius: 5px; }
QListWidget[role="navigation"]::item:selected { background: #203d4e; border-color: #366074; color: #a8e3ea; }
QHeaderView::section { background: #1c2c3e; color: #b9c9da; padding: 8px; border: none; border-right: 1px solid #34465b; }
QGroupBox { background: #14202e; border: 1px solid #2d4053; border-radius: 7px; margin-top: 16px; padding: 18px 12px 12px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; color: #bdd0df; }
QTabWidget::pane { border: 1px solid #2d4053; border-radius: 5px; background: #111c2a; top: -1px; }
QTabBar::tab { background: #13202e; color: #a3b5c7; padding: 9px 12px; border: 1px solid transparent; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { background: #1a2d3e; color: #b0e8f0; border-bottom-color: #80c9d5; }
QTabBar::tab:hover { background: #20374a; }
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: #111c29; width: 9px; margin: 2px; }
QScrollBar:horizontal { background: #111c29; height: 9px; margin: 2px; }
QScrollBar::handle { background: #3e536a; border-radius: 3px; min-height: 24px; min-width: 24px; }
QScrollBar::handle:hover { background: #627f97; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QCheckBox { spacing: 8px; padding: 3px 0; }
QSlider::groove:horizontal { height: 4px; background: #34475a; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #7ac8d5; border-radius: 2px; }
QSlider::handle:horizontal { width: 12px; margin: -5px 0; border-radius: 6px; background: #c6e6ec; }
QSplitter::handle { background: #233549; width: 4px; height: 4px; }
QToolButton::menu-indicator { image: none; }
QMenuBar, QStatusBar, QToolBar { background: #111c2a; color: #aabdd0; border: none; }
QMenuBar::item { padding: 5px 10px; }
QMenuBar::item:selected, QMenu::item:selected { background: #28475b; color: #e8faff; }
QMenu { background: #172638; color: #dce8f2; border: 1px solid #41586e; padding: 5px; }
QMenu::item { padding: 8px 24px; border-radius: 3px; }
QMenu::separator { height: 1px; background: #34485b; margin: 5px 8px; }
QToolTip { background: #243246; color: #eef4fa; border: 1px solid #527086; padding: 6px; }
QStatusBar { border-top: 1px solid #2a394b; font-size: 11px; }
QStatusBar::item { border: none; }
QProgressBar { border: 1px solid #34465b; border-radius: 4px; background: #0d1723; text-align: center; min-height: 16px; }
QProgressBar::chunk { background: #3e899d; border-radius: 3px; }
)QSS"));
}
QLabel *label(const QString &text, const char *role, QWidget *parent) {
  auto *l = new QLabel(text, parent);
  l->setProperty("role", role);
  l->setTextFormat(Qt::PlainText);
  return l;
}
void heading(QVBoxLayout *layout, const QString &eyebrow, const QString &title,
             const QString &description) {
  auto *block = new QWidget;
  auto *column = new QVBoxLayout(block);
  column->setContentsMargins(0, 0, 0, 8);
  column->setSpacing(5);
  column->addWidget(label(eyebrow.toUpper(), "kicker"));
  column->addWidget(label(title, "title"));
  if (!description.isEmpty()) {
    auto *subtitle = label(description, "secondary");
    subtitle->setWordWrap(true);
    column->addWidget(subtitle);
  }
  layout->addWidget(block);
}
void disclosure(QVBoxLayout *layout, QWidget *content, const QString &title) {
  auto *button = new QToolButton;
  button->setText(title);
  button->setCheckable(true);
  button->setArrowType(Qt::RightArrow);
  button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  button->setProperty("role", "quiet");
  button->setAccessibleName(title);
  button->setToolTip("Show or hide " + title.toLower());
  layout->addWidget(button, 0, Qt::AlignLeft);
  layout->addWidget(content);
  content->hide();
  QObject::connect(button, &QToolButton::toggled, content, [button, content](bool open) {
    button->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    content->setVisible(open);
  });
}
QIcon icon(Symbol symbol) {
  QIcon result;
  for (int scale : {1, 2, 3}) {
    QPixmap pixmap(20 * scale, 20 * scale);
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#a4ccdb"), 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    auto line = [&](qreal x, qreal y, qreal a, qreal b) {
      p.drawLine(QPointF(x, y), QPointF(a, b));
    };
    switch (symbol) {
    case Symbol::Globe:
      p.drawEllipse(QRectF(2, 2, 16, 16));
      p.drawEllipse(QRectF(6, 2, 8, 16));
      line(2, 10, 18, 10);
      break;
    case Symbol::Satellite:
      p.save();
      p.translate(10, 10);
      p.rotate(-40);
      p.drawRect(QRectF(-3, -3, 6, 6));
      p.drawRect(QRectF(-8, -2, 4, 4));
      p.drawRect(QRectF(4, -2, 4, 4));
      line(0, 3, 0, 7);
      p.restore();
      break;
    case Symbol::Marine:
      line(3, 8, 17, 8);
      line(3, 8, 6, 14);
      line(17, 8, 14, 14);
      line(10, 3, 10, 8);
      line(7, 5, 13, 5);
      p.drawArc(QRectF(1, 12, 6, 5), 180 * 16, 180 * 16);
      p.drawArc(QRectF(7, 12, 6, 5), 180 * 16, 180 * 16);
      p.drawArc(QRectF(13, 12, 6, 5), 180 * 16, 180 * 16);
      break;
    case Symbol::Aircraft: {
      QPainterPath path;
      path.moveTo(10, 2);
      path.lineTo(12, 8);
      path.lineTo(18, 11);
      path.lineTo(18, 13);
      path.lineTo(12, 11);
      path.lineTo(12, 15);
      path.lineTo(14, 17);
      path.lineTo(6, 17);
      path.lineTo(8, 15);
      path.lineTo(8, 11);
      path.lineTo(2, 13);
      path.lineTo(2, 11);
      path.lineTo(8, 8);
      path.closeSubpath();
      p.drawPath(path);
      break;
    }
    case Symbol::Ground:
      p.drawRect(QRectF(3, 7, 6, 11));
      p.drawRect(QRectF(9, 3, 8, 15));
      line(12, 7, 14, 7);
      line(12, 11, 14, 11);
      line(12, 15, 14, 15);
      line(5, 11, 6, 11);
      line(5, 15, 6, 15);
      break;
    case Symbol::Layers: {
      QPolygonF poly{{2, 6}, {10, 2}, {18, 6}, {10, 10}};
      p.drawPolygon(poly);
      line(2, 10, 10, 14);
      line(10, 14, 18, 10);
      line(2, 14, 10, 18);
      line(10, 18, 18, 14);
      break;
    }
    case Symbol::Inspect:
      p.drawEllipse(QRectF(4, 4, 12, 12));
      line(10, 1, 10, 6);
      line(10, 14, 10, 19);
      line(1, 10, 6, 10);
      line(14, 10, 19, 10);
      break;
    case Symbol::Search:
      p.drawEllipse(QRectF(3, 3, 10, 10));
      line(12, 12, 17, 17);
      break;
    case Symbol::Data:
      line(6, 4, 3, 4);
      line(3, 4, 3, 8);
      line(3, 8, 1, 10);
      line(1, 10, 3, 12);
      line(3, 12, 3, 16);
      line(3, 16, 6, 16);
      line(14, 4, 17, 4);
      line(17, 4, 17, 8);
      line(17, 8, 19, 10);
      line(19, 10, 17, 12);
      line(17, 12, 17, 16);
      line(17, 16, 14, 16);
      break;
    case Symbol::Tools:
      line(3, 5, 17, 5);
      line(3, 10, 17, 10);
      line(3, 15, 17, 15);
      p.drawEllipse(QRectF(5, 3, 4, 4));
      p.drawEllipse(QRectF(12, 8, 4, 4));
      p.drawEllipse(QRectF(7, 13, 4, 4));
      break;
    case Symbol::Mission:
      p.drawRoundedRect(QRectF(3, 5, 14, 13), 2, 2);
      p.drawRect(QRectF(7, 2, 6, 5));
      line(7, 11, 13, 11);
      line(7, 14, 11, 14);
      break;
    case Symbol::Plus:
      line(4, 10, 16, 10);
      line(10, 4, 10, 16);
      break;
    case Symbol::Minus:
      line(4, 10, 16, 10);
      break;
    case Symbol::North:
      p.drawPolygon(QPolygonF{{10, 2}, {16, 17}, {10, 13}, {4, 17}});
      break;
    case Symbol::Close:
      line(5, 5, 15, 15);
      line(15, 5, 5, 15);
      break;
    case Symbol::More:
      p.setBrush(QColor("#a4ccdb"));
      for (int x : {4, 10, 16})
        p.drawEllipse(QPointF(x, 10), .9, .9);
      break;
    case Symbol::Folder:
      p.drawRoundedRect(QRectF(2, 6, 16, 11), 2, 2);
      line(3, 6, 3, 3);
      line(3, 3, 8, 3);
      line(8, 3, 11, 6);
      break;
    case Symbol::Save:
      p.drawRect(QRectF(3, 2, 14, 16));
      p.drawRect(QRectF(6, 2, 8, 5));
      p.drawRect(QRectF(6, 12, 8, 6));
      break;
    }
    p.end();
    result.addPixmap(pixmap);
  }
  return result;
}
} // namespace helios::design

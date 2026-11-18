#include "UI/WorkspacePanel.h"
#include "UI/WorkspaceStyle.h"
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QVBoxLayout>
using namespace helios;
class WorkspaceUITests : public QObject {
  Q_OBJECT
private slots:
  void mapRemainsAvailableOutsidePalette() {
    auto *map = new QPushButton("Map input");
    map->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    WorkspaceSurface surface(map);
    surface.resize(1080, 700);
    auto *content = new QDialog;
    auto *layout = new QVBoxLayout(content);
    layout->addWidget(new QLineEdit);
    WorkspacePanel panel("test", "Analysis", design::Symbol::Inspect, content, &surface,
                         {400, 500});
    surface.addPanel(&panel);
    surface.show();
    panel.reveal();
    QCoreApplication::processEvents();
    QSignalSpy clicked(map, &QPushButton::clicked);
    QTest::mouseClick(map, Qt::LeftButton, {}, QPoint(900, 500));
    QCOMPARE(clicked.size(), 1);
    QVERIFY(!content->isWindow());
    QVERIFY(panel.isVisible());
    QTest::mouseClick(panel.findChild<QToolButton *>("panelClose"), Qt::LeftButton);
    QVERIFY(!panel.isVisible());
    panel.reveal();
    QVERIFY(panel.isVisible());
    content->reject();
    QVERIFY(!panel.isVisible());
    panel.reveal();
    QVERIFY(content->isVisible());
  }
  void collapseAndResetPreserveVisibilityAndDrafts() {
    WorkspaceSurface surface(new QWidget);
    surface.resize(1280, 820);
    auto *content = new QDialog;
    auto *layout = new QVBoxLayout(content);
    auto *entry = new QLineEdit;
    layout->addWidget(entry);
    entry->setText("Unsaved analyst note");
    WorkspacePanel panel("test", "Analysis", design::Symbol::Inspect, content, &surface,
                         {420, 500});
    surface.addPanel(&panel);
    surface.show();
    panel.reveal();
    const auto size = panel.size();
    panel.setCollapsed(true);
    QCOMPARE(panel.height(), 44);
    panel.setCollapsed(false);
    QCOMPARE(panel.size(), size);
    QCOMPARE(entry->text(), QString("Unsaved analyst note"));
    panel.setCollapsed(true);
    panel.hide();
    surface.resetPanels();
    QVERIFY(!panel.isVisible());
    panel.reveal();
    QCOMPARE(entry->text(), QString("Unsaved analyst note"));
  }
  void palettesStayInsideResizedMapAndRestoreSafely() {
    WorkspaceSurface surface(new QWidget);
    surface.resize(1440, 960);
    WorkspacePanel panel("test", "Analysis", design::Symbol::Inspect, new QWidget, &surface,
                         {680, 660}, true);
    surface.addPanel(&panel);
    surface.show();
    panel.reveal();
    panel.restoreLayout({{"x", 5000}, {"y", 5000}, {"width", 5000}, {"height", 5000}});
    panel.constrain();
    surface.resize(1080, 700);
    QCoreApplication::processEvents();
    QVERIFY(panel.x() >= 12);
    QVERIFY(panel.y() >= 72);
    QVERIFY(panel.geometry().right() < surface.width());
    QVERIFY(panel.geometry().bottom() < surface.height());
    const auto state = panel.layoutState();
    QVERIFY(state.value("width").toInt() <= 1056);
    panel.resetLayout();
    QCOMPARE(panel.width(), 680);
    QCOMPARE(panel.height(), 612);
  }
  void smallMapScrollsControlsWithoutCompressingThem() {
    WorkspaceSurface surface(new QWidget);
    surface.resize(1080, 420);
    auto *content = new QWidget;
    auto *form = new QVBoxLayout(content);
    QList<QPushButton *> buttons;
    for (int i = 0; i < 12; ++i) {
      auto *button = new QPushButton(QString("Control %1").arg(i));
      form->addWidget(button);
      buttons.append(button);
    }
    WorkspacePanel panel("test", "Analysis", design::Symbol::Inspect, content, &surface,
                         {400, 600});
    surface.addPanel(&panel);
    surface.show();
    panel.reveal();
    QCoreApplication::processEvents();
    auto *scroll = panel.findChild<QScrollArea *>();
    QVERIFY(scroll);
    QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
    for (auto *button : buttons)
      QVERIFY(button->height() >= 28);
    scroll->ensureWidgetVisible(buttons.last());
    QVERIFY(scroll->verticalScrollBar()->value() > 0);
  }
  void headerDragIsBounded() {
    WorkspaceSurface surface(new QWidget);
    surface.resize(1080, 700);
    WorkspacePanel panel("test", "Analysis", design::Symbol::Inspect, new QWidget, &surface,
                         {400, 400});
    surface.addPanel(&panel);
    surface.show();
    panel.reveal();
    auto *header = panel.findChild<QWidget *>("panelDragHandle");
    QVERIFY(header);
    QTest::mousePress(header, Qt::LeftButton, {}, QPoint(80, 20));
    QTest::mouseMove(header, QPoint(-200, -200));
    QTest::mouseRelease(header, Qt::LeftButton, {}, QPoint(-200, -200));
    QVERIFY(panel.x() >= 12);
    QVERIFY(panel.y() >= 72);
  }
};
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  design::apply(app);
  WorkspaceUITests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "WorkspaceUITests.moc"

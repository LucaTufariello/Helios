#include "GodEyeScene.h"
#include "SceneFixture.h"
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <memory>

using Scene = std::unique_ptr<GEScene, decltype(&GE_scene_destroy)>;
class SceneTests : public QObject {
  Q_OBJECT
  Scene open(const QString &path) {
    return Scene(GE_scene_create(QUrl::fromLocalFile(path).toEncoded().constData()),
                 GE_scene_destroy);
  }
  int load(GEScene *scene, bool expectFailure = false) {
    QElapsedTimer timer;
    timer.start();
    int count = 0;
    while (timer.elapsed() < 5000) {
      count = int(GE_scene_update(scene, {6378737, 0, 0}, {-1, 0, 0}, {0, 0, 1}, 800, 600));
      if (expectFailure ? GE_scene_failures(scene) > 0 : count > 0)
        break;
      QTest::qWait(10);
    }
    return count;
  }
private slots:
  void geometryMaterialPickingAndRelease() {
    QTemporaryDir directory;
    auto scene = open(godeye::qa::sceneFixture(directory.path()));
    QVERIFY(scene);
    QCOMPARE(load(scene.get()), 1);
    GEMesh mesh{};
    QVERIFY(GE_scene_mesh(scene.get(), 0, &mesh));
    QCOMPARE(mesh.vertexCount, 3u);
    QCOMPARE(mesh.indexCount, 3u);
    QCOMPARE(mesh.origin.x, 6378237.0);
    QCOMPARE(mesh.red, 0.2f);
    QCOMPARE(GE_scene_failures(scene.get()), 0u);
    QCOMPARE(GE_scene_progress(scene.get()), 100.f);
    GEPoint hit{};
    QVERIFY(GE_scene_pick(scene.get(), {6378737, 0, 0}, {-1, 0, 0}, &hit));
    QCOMPARE(hit.x, 6378237.0);
    QVERIFY(QString::fromUtf8(GE_scene_credits(scene.get()))
                .contains("GodEye automated test geometry"));
    GE_scene_rendered_meshes(scene.get(), nullptr, 0);
    QVERIFY(!GE_scene_pick(scene.get(), {6378737, 0, 0}, {-1, 0, 0}, &hit));
    GE_scene_rendered_meshes(scene.get(), &mesh.id, 1);
    QVERIFY(GE_scene_pick(scene.get(), {6378737, 0, 0}, {-1, 0, 0}, &hit));
    QElapsedTimer timer;
    timer.start();
    scene.reset();
    QVERIFY(timer.elapsed() < 2000);
  }
  void rejectedContent_data() {
    QTest::addColumn<QString>("content");
    QTest::addColumn<int>("mode");
    QTest::newRow("missing") << QString("missing.glb") << 4;
    QTest::newRow("directory-escape") << QString("../roof.glb") << 4;
    QTest::newRow("unsupported-lines") << QString("roof.glb") << 1;
    QTest::newRow("external-origin") << QString("https://example.com/roof.glb") << 4;
  }
  void rejectedContent() {
    QFETCH(QString, content);
    QFETCH(int, mode);
    QTemporaryDir directory;
    auto scene = open(godeye::qa::sceneFixture(directory.path(), content, mode));
    QVERIFY(scene);
    QCOMPARE(load(scene.get(), true), 0);
    QVERIFY(GE_scene_failures(scene.get()) > 0);
  }
  void transparency_data() {
    QTest::addColumn<QString>("mode");
    QTest::addColumn<bool>("textured");
    for (const auto &mode : {"OPAQUE", "MASK", "BLEND"})
      for (bool textured : {false, true})
        QTest::newRow(qPrintable(QString(mode) + (textured ? "-texture" : "-factor")))
            << QString(mode) << textured;
  }
  void transparency() {
    QFETCH(QString, mode);
    QFETCH(bool, textured);
    QTemporaryDir directory;
    auto scene = open(godeye::qa::sceneFixture(directory.path(), "roof.glb", 4, mode, 0, textured));
    QVERIFY(scene);
    QCOMPARE(load(scene.get()), 1);
    GEPoint hit{};
    QCOMPARE(bool(GE_scene_pick(scene.get(), {6378737, 0, 0}, {-1, 0, 0}, &hit)), mode == "OPAQUE");
    GEMesh mesh{};
    QVERIFY(GE_scene_mesh(scene.get(), 0, &mesh));
    QCOMPARE(bool(mesh.pixels), textured);
    if (textured) {
      QCOMPARE(mesh.width, 4u);
      QCOMPARE(mesh.wrapT, 33648);
    }
  }
  void cancellationAndAdmission() {
    QTemporaryDir directory;
    const auto path = godeye::qa::sceneFixture(directory.path());
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 10; ++i) {
      auto scene = open(path);
      QVERIFY(scene);
    }
    QVERIFY(timer.elapsed() < 2000);
    QVERIFY(!GE_scene_create(nullptr));
    QVERIFY(!GE_scene_create("http://example.com/tileset.json"));
    QVERIFY(!GE_scene_create("https://user:password@example.com/tileset.json"));
    QVERIFY(!GE_scene_create("file://remote/tileset.json"));
  }
};
QTEST_GUILESS_MAIN(SceneTests)
#include "SceneTests.moc"

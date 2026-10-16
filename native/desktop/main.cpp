#include "Core/Monitors.h"
#include "UI/MainWindow.h"
#include "UI/MapToolsWindow.h"
#include "Validation.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileOpenEvent>
#include <QStandardPaths>
#include <QTimer>

namespace {
class GodEyeOpenLink : public QObject {
public:
  explicit GodEyeOpenLink(godeye::MainWindow &window) : window_(window) {}
  bool eventFilter(QObject *, QEvent *event) override {
    if (event->type() != QEvent::FileOpen)
      return false;
    const auto url = static_cast<QFileOpenEvent *>(event)->url();
    if (url.scheme() != "godeye")
      return false;
    if (!window_.mapToolsWindow().openLink(url))
      qWarning("The GodEye workspace link is invalid or outside its limits.");
    window_.show();
    window_.raise();
    return true;
  }

private:
  godeye::MainWindow &window_;
};
} // namespace
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("GodEye");
  QCoreApplication::setApplicationName("GodEyeDesktop");
  QCoreApplication::setApplicationVersion("0.1");
  QCommandLineParser parser;
  parser.setApplicationDescription("Native GodEye cross-platform desktop preview");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption({"profile", "Separate desktop working-library directory.", "directory"});
  parser.addOption({"offline", "Disable provider and map network requests."});
  parser.addOption({"validate", "Run isolated native window/GPU integration checks."});
  parser.addOption(
      {"validate-live", "Run an opt-in public map/terrain check with a disposable profile."});
  parser.addOption({"qa-output", "Validation output directory.", "directory"});
  parser.addOption({"raster-python",
                    "Raster Python executable (also enables generated worker validation).",
                    "file"});
  parser.addOption(
      {"validate-optical",
       "Opt-in public Sentinel-2 search, pixel retrieval, library and export validation."});
  parser.addOption(
      {"validate-radar-search",
       "Opt-in public Sentinel-1 catalogue and footprint validation; no archive download."});
  parser.addOption(
      {"mission-library", "Open a mission library with automatic monitor checks paused.", "file"});
  parser.addOption({"product", "Import a completed satellite product manifest.", "file"});
  parser.addOption({"tileset",
                    "Load an HTTPS or local 3D Tiles tileset (also supported in validation).",
                    "url-or-file"});
  parser.addPositionalArgument("url", "Optional godeye://view or godeye://workspace link.",
                               "[url]");
  parser.process(app);
  if (parser.isSet("validate") || parser.isSet("validate-live") ||
      parser.isSet("validate-optical") || parser.isSet("validate-radar-search"))
    return godeye::runValidation(app, parser.value("qa-output"), parser.isSet("validate-live"),
                                 parser.value("product"), parser.value("tileset"),
                                 parser.value("raster-python"), parser.isSet("validate-optical"),
                                 parser.isSet("validate-radar-search"));
  const QString profile = parser.isSet("profile")
                              ? QDir(parser.value("profile")).absolutePath()
                              : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  godeye::MainWindow window(profile, !parser.isSet("offline"));
  if (parser.isSet("raster-python"))
    window.processingWindow().setEngines(parser.value("raster-python"));
  if (parser.isSet("mission-library")) {
    window.monitorRunner().stop();
    try {
      window.missions().load(parser.value("mission-library"));
    } catch (const std::exception &e) {
      qWarning("Mission library: %s", e.what());
    }
  }
  if (parser.isSet("product"))
    window.satelliteWindow().importManifest(parser.value("product"));
  GodEyeOpenLink links(window);
  app.installEventFilter(&links);
  const auto urls = parser.positionalArguments();
  if (urls.size() > 1)
    qWarning("Only one GodEye view/workspace link can be opened at startup.");
  else if (urls.size() == 1 && !window.mapToolsWindow().openLink(QUrl(urls.first())))
    qWarning("The GodEye view/workspace link is invalid or outside its limits.");
  window.show();
  if (parser.isSet("tileset") &&
      !window.map().loadTileset(QUrl::fromUserInput(parser.value("tileset"))))
    qWarning("3D Tiles: choose a local tileset.json or an HTTPS URL without credentials");
  return app.exec();
}

#include "UI/MainWindow.h"
#include "Validation.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>

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
  parser.addOption({"offline", "Disable map network requests."});
  parser.addOption({"validate", "Run isolated native window/GPU integration checks."});
  parser.addOption(
      {"validate-live", "Run an opt-in public map/terrain check with a disposable profile."});
  parser.addOption({"qa-output", "Validation output directory.", "directory"});
  parser.addOption({"mission-library",
                    "Open an existing mission library without writing it automatically.", "file"});
  parser.addOption({"product", "Import a completed satellite product manifest.", "file"});
  parser.process(app);
  if (parser.isSet("validate") || parser.isSet("validate-live"))
    return godeye::runValidation(app, parser.value("qa-output"), parser.isSet("validate-live"),
                                 parser.value("product"));
  const QString profile = parser.isSet("profile")
                              ? QDir(parser.value("profile")).absolutePath()
                              : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  godeye::MainWindow window(profile, !parser.isSet("offline"));
  if (parser.isSet("mission-library")) {
    try {
      window.missions().load(parser.value("mission-library"));
    } catch (const std::exception &e) {
      qWarning("Mission library: %s", e.what());
    }
  }
  if (parser.isSet("product"))
    window.satelliteWindow().importManifest(parser.value("product"));
  window.show();
  return app.exec();
}

#pragma once
#include <QString>
class QApplication;
namespace helios {
int runValidation(QApplication &application, const QString &output, bool live = false,
                  const QString &product = {}, const QString &tileset = {},
                  const QString &python = {}, bool retrieveOptical = false,
                  bool searchRadar = false);
}

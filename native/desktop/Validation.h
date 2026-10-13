#pragma once
#include <QString>
class QApplication;
namespace godeye {
int runValidation(QApplication &application, const QString &output, bool live = false,
                  const QString &product = {});
}

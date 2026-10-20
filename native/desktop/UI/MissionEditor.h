#pragma once
#include "Core/Missions.h"
class QWidget;
namespace godeye {
bool editMissionEntry(QWidget *parent, Missions &missions, const QString &collection,
                      const QString &id, const std::function<void()> &validateContext = {});
}

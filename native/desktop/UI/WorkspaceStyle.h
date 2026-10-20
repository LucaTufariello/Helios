#pragma once
#include <QIcon>
#include <QString>
class QApplication;
class QLabel;
class QVBoxLayout;
class QWidget;
namespace godeye::design {
enum class Symbol {
  Globe,
  Satellite,
  Marine,
  Aircraft,
  Ground,
  Layers,
  Inspect,
  Data,
  Tools,
  Mission,
  Search,
  Plus,
  Minus,
  North,
  Close,
  More,
  Folder,
  Save
};
void apply(QApplication &app);
QIcon icon(Symbol symbol);
QLabel *label(const QString &text, const char *role, QWidget *parent = nullptr);
void heading(QVBoxLayout *layout, const QString &eyebrow, const QString &title,
             const QString &description = {});
void disclosure(QVBoxLayout *layout, QWidget *content, const QString &title);
} // namespace godeye::design

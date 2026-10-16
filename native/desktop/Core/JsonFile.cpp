#include "JsonFile.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <stdexcept>
namespace godeye {
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
QByteArray digest(const QByteArray &bytes) {
  return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
void write(const QString &path, const QByteArray &bytes) {
  require(!QFileInfo(path).isSymLink(),
          "The destination is a symbolic link; choose a regular document.");
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
          "The document could not be saved atomically; the previous copy was preserved.");
}
QJsonDocument decode(const QByteArray &bytes) {
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(bytes, &error);
  require(error.error == QJsonParseError::NoError && !document.isNull(),
          "The saved document is unreadable and has been preserved.");
  return document;
}
} // namespace
JsonFile::JsonFile(QString path, qsizetype maximum, Validator validator)
    : path_(QFileInfo(path).absoluteFilePath()), maximum_(maximum),
      validator_(std::move(validator)) {
  require(maximum > 0 && maximum <= 8 * 1024 * 1024, "Invalid document size limit.");
}
QByteArray JsonFile::read(const QString &path, qsizetype maximum) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly) && file.size() <= maximum,
          "The document is missing, unreadable or exceeds its size limit.");
  const auto bytes = file.read(maximum + 1);
  require(bytes.size() <= maximum && file.error() == QFileDevice::NoError,
          "The document could not be read safely.");
  return bytes;
}
QJsonDocument JsonFile::load(const QJsonDocument &empty) {
  if (!QFileInfo::exists(path_)) {
    validator_(empty);
    hash_.clear();
    existed_ = false;
    loaded_ = true;
    return empty;
  }
  const auto bytes = read(path_, maximum_);
  const auto document = decode(bytes);
  validator_(document);
  hash_ = digest(bytes);
  existed_ = true;
  loaded_ = true;
  return document;
}
void JsonFile::save(const QJsonDocument &document) {
  validator_(document);
  const auto bytes = document.toJson(QJsonDocument::Indented);
  require(bytes.size() <= maximum_, "The document exceeds its supported size limit.");
  require(loaded_, "Open the document before editing it.");
  require(QDir().mkpath(QFileInfo(path_).absolutePath()),
          "The destination directory could not be created.");
  QLockFile lock(path_ + ".lock");
  require(lock.tryLock(0), "Another process is saving this document.");
  if (QFileInfo::exists(path_)) {
    const auto previous = read(path_, maximum_);
    validator_(decode(previous));
    require(existed_ && digest(previous) == hash_,
            "The document changed on disk. Reopen it or save a separate copy.");
    write(path_ + ".backup", previous);
  } else
    require(!existed_, "The original document was removed; save a separate copy.");
  write(path_, bytes);
  hash_ = digest(bytes);
  existed_ = true;
}
} // namespace godeye

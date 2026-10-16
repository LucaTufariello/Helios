#include "LibraryStorage.h"
#include "JsonFile.h"
#include "WorkerJob.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <unistd.h>
namespace godeye {
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
struct Lease {
  int fd = -1;
  ~Lease() {
    if (fd >= 0)
      ::close(fd);
  }
};
} // namespace
QString trashOwnedProduct(const QString &jobsRoot, const ProductLayer &product, TrashMove move) {
  const QFileInfo root(jobsRoot);
  const QUuid uuid(product.jobID);
  require(root.isDir() && !root.isSymLink() && !uuid.isNull(),
          "Choose a product generated in this desktop profile before moving its job to Trash.");
  const auto directory =
      QDir(root.canonicalFilePath()).filePath(uuid.toString(QUuid::WithoutBraces));
  const QFileInfo folder(directory), manifest(product.manifest);
  const auto expected = directory + "/product/result.json";
  require(folder.isDir() && !folder.isSymLink() && folder.canonicalFilePath() == directory &&
              manifest.isFile() && !manifest.isSymLink() &&
              manifest.canonicalFilePath() == expected &&
              !QFileInfo(directory + "/run.json").isSymLink(),
          "This is an imported or moved product. Remove its library reference or manage its source "
          "folder explicitly.");
  const auto record = WorkerJob::readRecord(directory + "/run.json");
  require(
      record["version"] == 1 && QUuid(record["id"].toString()) == uuid &&
          QStringList{"completed", "failed"}.contains(record["state"].toString()),
      "The selected job is active or its ownership record is invalid; its files were preserved.");
  Lease lease;
  lease.fd = ::open(QFile::encodeName(directory + "/.run.lock").constData(),
                    O_RDWR | O_CLOEXEC | O_NOFOLLOW);
  require(lease.fd >= 0 && ::flock(lease.fd, LOCK_EX | LOCK_NB) == 0,
          "A worker still owns this job; its files were preserved.");
  const auto bytes = JsonFile::read(expected, 8 * 1024 * 1024);
  require(!product.manifestHash.isEmpty() &&
              QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) == product.manifestHash,
          "The product changed on disk. Reimport and inspect it before moving the job to Trash.");
  QString trashed;
  if (!move)
    move = [](const QString &path, QString *result) { return QFile::moveToTrash(path, result); };
  require(move(directory, &trashed), "The job could not be moved to the operating system Trash. No "
                                     "permanent-delete fallback was used.");
  return trashed;
}
} // namespace godeye

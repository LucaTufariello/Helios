#include "Products.h"
#include "Missions.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>
#include <stdexcept>

namespace godeye {
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
QImage previewImage(const QString &filename, bool decode, QSize &pixelSize) {
  QFile file(filename);
  require(file.open(QIODevice::ReadOnly) && file.size() <= 32 * 1024 * 1024,
          "The preview is missing or exceeds 32 MB.");
  QByteArray data = file.read(32 * 1024 * 1024 + 1);
  QBuffer buffer(&data);
  buffer.open(QIODevice::ReadOnly);
  QImageReader reader(&buffer);
  require(reader.format() == "png" || reader.format() == "jpeg",
          "Only PNG/JPEG display previews are supported.");
  const auto size = reader.size();
  require(size.width() > 0 && size.height() > 0 && size.width() <= 2048 && size.height() <= 2048,
          "The preview dimensions are invalid or exceed 2048 pixels per side.");
  pixelSize = size;
  if (!decode)
    return {};
  auto image = reader.read();
  require(!image.isNull(), "The preview image could not be decoded.");
  return image.convertToFormat(QImage::Format_RGBA8888);
}
} // namespace
QString safeProductFile(const QString &directory, const QString &name, bool required) {
  require(!name.isEmpty() && name != "." && name != ".." && !name.contains('/') &&
              !name.contains('\\') && !name.contains(':') && !name.contains(QChar(0)),
          "An output filename is unsafe.");
  const QFileInfo root(directory), file(QDir(directory).filePath(name));
  if (!file.exists() && !required)
    return {};
  require(file.exists() && file.isFile() &&
              QFileInfo(file.canonicalFilePath()).absolutePath() == root.canonicalFilePath(),
          "An output file is missing or resolves outside its product folder.");
  return file.canonicalFilePath();
}
QVector<ProductLayer> readProducts(const QString &manifest, bool decodeImages) {
  QFile file(manifest);
  require(file.open(QIODevice::ReadOnly) && file.size() <= 8 * 1024 * 1024,
          "The product manifest is missing or exceeds 8 MB.");
  const auto bytes = file.read(8 * 1024 * 1024 + 1);
  QJsonParseError error{};
  const auto document = QJsonDocument::fromJson(bytes, &error);
  require(error.error == QJsonParseError::NoError && document.isObject(),
          "The product manifest is not valid JSON.");
  const auto root = document.object();
  require(root["version"].toInt() == 1 && root["state"].toString() == "completed",
          "Only completed version-1 satellite products can be displayed.");
  const QFileInfo info(manifest);
  const QString directory = info.absolutePath();
  QString jobID = QFileInfo(directory).fileName();
  if (QUuid(jobID).isNull())
    jobID = QUuid::createUuidV5(QUuid("AD031149-730B-4BCA-9916-3AFB651D60DC"),
                                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256))
                .toString(QUuid::WithoutBraces)
                .toUpper();
  QVector<ProductLayer> result;
  auto append = [&](QJsonObject o, bool optical) {
    const auto b = Bounds::read(o["bounds"]);
    require(b.has_value(), "The product has invalid or unsupported geographic bounds.");
    const auto width = o["width"], height = o["height"];
    require(width.isDouble() && height.isDouble() && width.toInt(-1) == width.toDouble() &&
                height.toInt(-1) == height.toDouble() && width.toInt() > 0 && height.toInt() > 0,
            "The product raster dimensions are invalid.");
    ProductLayer p;
    p.jobID = jobID;
    p.kind = optical ? "optical" : "radar";
    const auto mode = root["mode"].toString();
    p.band = optical ? (mode == "pan"        ? "PAN"
                        : mode == "spectral" ? "Spectral"
                                             : "RGB")
                     : o["id"].toString();
    require(optical || QStringList{"VV", "VH", "Composite"}.contains(p.band),
            "The radar output band is unsupported.");
    p.id = p.kind + ":" + jobID + ":" + p.band;
    p.bounds = *b;
    p.metadata = root;
    p.manifest = info.canonicalFilePath();
    p.manifestHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    p.preview = safeProductFile(directory, o["preview"].toString());
    p.image = previewImage(p.preview, decodeImages, p.pixelSize);
    p.rasterSize = QSize(width.toInt(), height.toInt());
    require(!optical || p.pixelSize == p.rasterSize,
            "The optical preview dimensions do not match its manifest.");
    // SAR output width/height describe the full scientific grid. Its PNG is a
    // separately bounded preview; do not confuse the two resolutions or reject
    // a valid large GRD result because the display was intentionally downsampled.
    const auto geoName = o[optical ? "geotiff" : "visualization"].toString();
    if (!geoName.isEmpty())
      p.geotiff = safeProductFile(directory, geoName, false);
    if (!o["scientific"].toString().isEmpty())
      p.scientific = safeProductFile(directory, o["scientific"].toString(), false);
    if (optical) {
      require(root["crs"].toString() == "EPSG:4326",
              "This optical preview CRS is unsupported; geographic bounds will not be guessed.");
      const auto scene = root["scene"].toObject();
      p.sourceID = scene["id"].toString();
      p.acquisition = scene["acquiredAt"].toString();
      require(!scene["platform"].toString().isEmpty() && scene["platform"].toString().size() <= 120,
              "The optical sensor/platform identifier is missing or invalid.");
      p.label = scene["platform"].toString() + " · " + p.band + " · " + dateLabel(p.acquisition);
      p.resolution = root["sourceResolutionMetres"].toArray().first().toDouble();
    } else {
      const auto metadata = root["metadata"].toObject();
      p.sourceID = metadata["inputName"].toString();
      p.acquisition = metadata["acquisitionStart"].toString();
      require(!metadata["satellite"].toString().isEmpty() &&
                  metadata["satellite"].toString().size() <= 120,
              "The radar satellite identifier is missing or invalid.");
      p.label = metadata["satellite"].toString() + " · " + p.band + " radar backscatter · " +
                dateLabel(p.acquisition);
      p.resolution = root["options"].toObject()["pixelSpacing"].toDouble();
    }
    require(!p.sourceID.isEmpty() && p.sourceID.size() <= 512,
            "The source product identifier is missing or invalid.");
    require(!p.acquisition.isEmpty() &&
                QDateTime::fromString(p.acquisition, Qt::ISODateWithMs).isValid(),
            "The acquisition time is missing or invalid.");
    for (const auto &existing : result)
      require(existing.id != p.id, "The product contains duplicate outputs.");
    result.append(p);
  };
  if (root["scene"].isObject())
    append(root, true);
  else {
    require(root["outputs"].isArray() && root["outputs"].toArray().size() <= 3 &&
                root["metadata"].isObject(),
            "The SAR product outputs or metadata are invalid.");
    // The existing SAR worker exports visualization bounds in EPSG:4326. Reject
    // an explicit conflicting CRS rather than pretending another CRS matches.
    require(!root.contains("crs") || root["crs"].toString() == "EPSG:4326",
            "The SAR display CRS is unsupported.");
    for (const auto output : root["outputs"].toArray()) {
      require(output.isObject(), "A radar output is invalid.");
      append(output.toObject(), false);
    }
  }
  require(!result.isEmpty(), "This product has no viewable outputs.");
  return result;
}
QJsonObject ProductLayer::missionEvidence() const {
  QJsonArray b{bounds.west, bounds.south, bounds.east, bounds.north};
  QJsonObject a{{"sourceID", sourceID},
                {"sensor", kind == "radar" ? metadata["metadata"].toObject()["satellite"].toString()
                                           : metadata["scene"].toObject()["platform"].toString()},
                {"bounds", b},
                {"displayBand", band}};
  const auto time = QDateTime::fromString(acquisition, Qt::ISODateWithMs);
  if (time.isValid())
    a["acquiredAt"] = time.toMSecsSinceEpoch() / 1000.0 - 978307200.0;
  if (resolution > 0)
    a["resolutionMetres"] = resolution;
  return {{"id", newID()},
          {"kind", kind},
          {"referenceID", jobID},
          {"title", label},
          {"savedAt", foundationNow()},
          {"acquisition", a}};
}
void exportProductFile(const QString &source, const QString &destination,
                       std::shared_ptr<std::atomic_bool> cancelled) {
  require(QFileInfo(source).canonicalFilePath() != QFileInfo(destination).canonicalFilePath() ||
              !QFileInfo::exists(destination),
          "Choose a destination different from the source file.");
  QFile input(source);
  QSaveFile output(destination);
  output.setDirectWriteFallback(false);
  require(input.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly),
          "The source or export destination could not be opened.");
  while (!input.atEnd()) {
    require(!cancelled || !cancelled->load(),
            "Export cancelled. The destination was not replaced.");
    const auto block = input.read(1024 * 1024);
    require(input.error() == QFileDevice::NoError && !block.isEmpty() &&
                output.write(block) == block.size(),
            "Export failed while copying the product.");
  }
  require(!cancelled || !cancelled->load(), "Export cancelled. The destination was not replaced.");
  require(output.commit(), "The exported product could not be committed atomically.");
}
} // namespace godeye

#pragma once
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <bit>
#include <stdexcept>

namespace godeye::qa {
// Isolated generated fixture matching the Swift scene regression geometry.
inline QString sceneFixture(const QString &directory, const QString &content = "roof.glb",
                            int mode = 4, const QString &alphaMode = "OPAQUE", double alpha = 1,
                            bool textured = false) {
  QDir().mkpath(directory);
  QByteArray binary;
  const auto integer = [](QByteArray &bytes, quint32 value) {
    const auto little = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char *>(&little), 4);
  };
  for (float value : {-50.f, -50.f, 0.f, 50.f, -50.f, 0.f, 0.f, 50.f, 0.f})
    integer(binary, std::bit_cast<quint32>(value));
  QJsonArray views{QJsonObject{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}}};
  QJsonArray accessors{QJsonObject{{"bufferView", 0},
                                   {"componentType", 5126},
                                   {"count", 3},
                                   {"type", "VEC3"},
                                   {"min", QJsonArray{-50, -50, 0}},
                                   {"max", QJsonArray{50, 50, 0}}}};
  QJsonObject attributes{{"POSITION", 0}};
  QJsonObject pbr{{"baseColorFactor", QJsonArray{0.2, 0.6, 0.9, alpha}}};
  QJsonObject model{
      {"asset", QJsonObject{{"version", "2.0"}, {"copyright", "GodEye automated test geometry"}}}};
  if (textured) {
    for (float value : {0.f, 0.f, 1.f, 0.f, 0.5f, 1.f})
      integer(binary, std::bit_cast<quint32>(value));
    views.append(QJsonObject{{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 24}});
    accessors.append(
        QJsonObject{{"bufferView", 1}, {"componentType", 5126}, {"count", 3}, {"type", "VEC2"}});
    attributes["TEXCOORD_0"] = 1;
    QImage image(4, 4, QImage::Format_RGBA8888);
    image.fill(QColor(255, 255, 255, alpha == 0 ? 0 : 255));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
      throw std::runtime_error("Scene QA texture encoding failed");
    views.append(
        QJsonObject{{"buffer", 0}, {"byteOffset", binary.size()}, {"byteLength", png.size()}});
    binary.append(png);
    pbr["baseColorTexture"] = QJsonObject{{"index", 0}};
    model["images"] = QJsonArray{QJsonObject{{"bufferView", 2}, {"mimeType", "image/png"}}};
    model["textures"] = QJsonArray{QJsonObject{{"source", 0}, {"sampler", 0}}};
    model["samplers"] = QJsonArray{
        QJsonObject{{"wrapS", 33071}, {"wrapT", 33648}, {"magFilter", 9728}, {"minFilter", 9987}}};
  }
  const auto binaryLength = binary.size();
  while (binary.size() % 4)
    binary.append('\0');
  model["buffers"] = QJsonArray{QJsonObject{{"byteLength", binaryLength}}};
  model["bufferViews"] = views;
  model["accessors"] = accessors;
  model["materials"] = QJsonArray{QJsonObject{{"doubleSided", true},
                                              {"alphaMode", alphaMode},
                                              {"alphaCutoff", 0.5},
                                              {"pbrMetallicRoughness", pbr}}};
  model["meshes"] = QJsonArray{QJsonObject{
      {"primitives",
       QJsonArray{QJsonObject{{"attributes", attributes}, {"material", 0}, {"mode", mode}}}}}};
  model["nodes"] = QJsonArray{QJsonObject{{"mesh", 0}}};
  model["scenes"] = QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}};
  model["scene"] = 0;
  QByteArray json = QJsonDocument(model).toJson(QJsonDocument::Compact);
  while (json.size() % 4)
    json.append(' ');
  QByteArray glb;
  for (quint32 value : {0x46546c67u, 2u, quint32(28 + json.size() + binary.size()),
                        quint32(json.size()), 0x4e4f534au})
    integer(glb, value);
  glb.append(json);
  integer(glb, quint32(binary.size()));
  integer(glb, 0x004e4942);
  glb.append(binary);
  QFile mesh(directory + "/roof.glb");
  if (!mesh.open(QIODevice::WriteOnly) || mesh.write(glb) != glb.size())
    throw std::runtime_error("Scene QA GLB write failed");
  mesh.close();
  QJsonObject tileset{
      {"asset", QJsonObject{{"version", "1.1"}, {"gltfUpAxis", "Z"}}},
      {"geometricError", 0},
      {"root",
       QJsonObject{{"boundingVolume",
                    QJsonObject{{"box", QJsonArray{0, 0, 0, 100, 0, 0, 0, 100, 0, 0, 0, 50}}}},
                   {"geometricError", 0},
                   {"refine", "REPLACE"},
                   {"transform", QJsonArray{0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 6378237, 0, 0, 1}},
                   {"content", QJsonObject{{"uri", content}}}}}};
  QFile file(directory + "/tileset.json");
  const auto bytes = QJsonDocument(tileset).toJson();
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
    throw std::runtime_error("Scene QA tileset write failed");
  return file.fileName();
}
} // namespace godeye::qa

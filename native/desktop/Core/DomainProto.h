#pragma once
#include <QByteArray>
#include <QVector>
#include <optional>
namespace godeye {
struct DomainProtoField {
  quint64 number = 0;
  QByteArray payload;
  std::optional<quint64> integer, fixed64;
  std::optional<quint32> fixed;
};
namespace DomainProto {
std::optional<QVector<DomainProtoField>> read(const QByteArray &bytes);
std::optional<QVector<quint64>> packed(const QByteArray &bytes);
const DomainProtoField *find(const QVector<DomainProtoField> &fields, quint64 number);
} // namespace DomainProto
} // namespace godeye

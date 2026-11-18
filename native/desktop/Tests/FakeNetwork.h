#pragma once
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQueue>
#include <QTimer>
#include <cstring>

namespace helios::qa {
struct ReplySpec {
  QByteArray body;
  int status = 200;
  qint64 length = -1;
  QUrl redirect;
  bool hold = false;
};
class FakeReply final : public QNetworkReply {
  QByteArray body_;
  qint64 offset_ = 0;

public:
  FakeReply(const QNetworkRequest &request, QNetworkAccessManager::Operation operation,
            ReplySpec spec, QObject *parent)
      : QNetworkReply(parent), body_(std::move(spec.body)) {
    setRequest(request);
    setUrl(request.url());
    setOperation(operation);
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, spec.status);
    setHeader(QNetworkRequest::ContentLengthHeader, spec.length >= 0 ? spec.length : body_.size());
    if (!spec.redirect.isEmpty())
      setAttribute(QNetworkRequest::RedirectionTargetAttribute, spec.redirect);
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    if (!spec.hold)
      QTimer::singleShot(0, this, [this] {
        emit metaDataChanged();
        if (isFinished())
          return;
        if (!body_.isEmpty())
          emit readyRead();
        if (isFinished())
          return;
        setFinished(true);
        emit finished();
      });
  }
  bool isSequential() const override {
    return true;
  }
  qint64 bytesAvailable() const override {
    return body_.size() - offset_ + QNetworkReply::bytesAvailable();
  }
  void abort() override {
    if (isFinished())
      return;
    setError(QNetworkReply::OperationCanceledError, "Generated network cancellation");
    setFinished(true);
    emit finished();
  }

protected:
  qint64 readData(char *data, qint64 maximum) override {
    const qint64 count = std::min(maximum, qint64(body_.size() - offset_));
    if (count <= 0)
      return -1;
    std::memcpy(data, body_.constData() + offset_, size_t(count));
    offset_ += count;
    return count;
  }
};
class FakeNetwork : public QNetworkAccessManager {
public:
  QQueue<ReplySpec> responses;
  QVector<QNetworkRequest> requests;
  QVector<QByteArray> bodies;

protected:
  QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                               QIODevice *outgoing) override {
    requests.append(request);
    bodies.append(outgoing ? outgoing->readAll() : QByteArray{});
    return new FakeReply(request, operation,
                         responses.isEmpty() ? ReplySpec{{}, 500} : responses.dequeue(), this);
  }
};
} // namespace helios::qa

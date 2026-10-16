#pragma once
#include <QNetworkAccessManager>
#include <QPointer>
#include <QUrl>
class QNetworkReply;
namespace godeye {
class HttpRequest : public QObject {
  Q_OBJECT
public:
  explicit HttpRequest(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
  ~HttpRequest() override;
  void start(const QUrl &url, qsizetype maximumBytes, const QByteArray &form = {});
  void cancel();
  bool busy() const {
    return busy_;
  }
signals:
  void changed();
  void completed(int status, QByteArray bytes, QString error);

private:
  QNetworkAccessManager *network_;
  QPointer<QNetworkReply> reply_;
  quint64 generation_ = 0;
  bool busy_ = false;
};
} // namespace godeye

#include "Briefing.h"
#include "JsonFile.h"
#include "Missions.h"
#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QSet>
#include <QTextDocument>
#include <QTextOption>
#include <cmath>
#include <stdexcept>
namespace godeye {
namespace {
void require(bool b, const char *s) {
  if (!b)
    throw std::runtime_error(s);
}
QString h(QString s) {
  return s.toHtmlEscaped().replace('\n', "<br/>");
}
QString digest(const QByteArray &b) {
  return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
QString jsonHash(const QJsonObject &o) {
  return digest(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
QString imageHash(const QImage &image) {
  QByteArray b;
  QBuffer buffer(&b);
  buffer.open(QIODevice::WriteOnly);
  require(image.save(&buffer, "PNG"), "Cannot encode captured image.");
  return digest(b);
}
QString imageTag(const QString &key, const QImage &image, int width, int height) {
  const auto size = image.size().scaled(width, height, Qt::KeepAspectRatio);
  return "<p align='center' style='line-height:100%;margin-top:0;margin-bottom:0'><img src='" +
         key + "' width='" + QString::number(size.width()) + "' height='" +
         QString::number(size.height()) + "'/></p>";
}
QString paragraphs(const QString &s) {
  return "<p>" + h(s.isEmpty() ? "Not recorded" : s) + "</p>";
}
} // namespace
FrozenBriefing prepareBriefing(const QJsonObject &plan, const QStringList &selected,
                               const QVector<ProductLayer> &library, const QImage &map,
                               const QJsonObject &capture) {
  validateMissions(QJsonArray{plan});
  require(!selected.isEmpty(), "Select at least one finding for the briefing.");
  FrozenBriefing out;
  const auto i = investigation(plan);
  QJsonArray findings;
  QSet<QString> wanted(selected.begin(), selected.end());
  require(wanted.size() == selected.size(), "Duplicate selected finding.");
  for (auto v : i["findings"].toArray())
    if (wanted.remove(v.toObject()["id"].toString()))
      findings.append(v);
  require(wanted.isEmpty(), "A selected finding no longer exists.");
  QJsonObject overview = i;
  for (auto key : {"findings", "snapshots", "citations", "associations"})
    overview.remove(key);
  out.manifest = {{"schema", "org.godeye.investigation-briefing"},
                  {"version", 1},
                  {"exportID", newID()},
                  {"frozenAt", foundationNow()},
                  {"case", QJsonObject{{"id", plan["id"]},
                                       {"name", plan["name"]},
                                       {"question", plan["objective"]},
                                       {"investigation", overview}}},
                  {"caseDocumentSHA256", jsonHash(plan)},
                  {"selectedFindingIDs", QJsonArray::fromStringList(selected)},
                  {"snapshots", i["snapshots"]},
                  {"citations", i["citations"]},
                  {"associations", i["associations"]},
                  {"findings", findings},
                  {"tasks", plan["brief"].toObject()["tasks"]},
                  {"areas", plan["brief"].toObject()["areas"]},
                  {"limitations", "Local single-user analyst record. Review states are judgments; "
                                  "owner labels are not authenticated identities. Decision history "
                                  "is not a tamper-proof audit. Raster files remain external."}};
  QString html = "<html><head><style>body{font-family:'Noto "
                 "Sans',sans-serif;color:#172b3c;font-size:10pt;}h1{font-size:25pt;color:#13354a;}"
                 "h2{font-size:16pt;color:#155268;margin-top:22pt;}h3{font-size:12pt;color:#155268;"
                 "margin-top:16pt;}p{line-height:145%;} .meta{color:#506576;font-size:9pt;} "
                 ".warning{color:#8d350d;} .label{font-weight:bold;}li{margin-bottom:6pt;} "
                 "table{border-collapse:collapse;}td{padding:6pt;vertical-align:top;}img{margin:"
                 "8pt 0;}</style></head><body>";
  html += "<p class='meta'>GODEYE / INVESTIGATION BRIEFING / LOCAL ANALYST RECORD</p><h1>" +
          h(plan["name"].toString()) + "</h1>";
  html += "<p class='meta'>Frozen " + h(dateLabel(out.manifest["frozenAt"])) + "<br/>Case " +
          h(plan["id"].toString()) + "<br/>Status: " + h(i["status"].toString()) + "</p>";
  html += "<h2>Case question</h2>" + paragraphs(plan["objective"].toString());
  for (auto key : {"completionCriteria", "conclusion", "unresolvedQuestions", "closureRationale"}) {
    const QMap<QString, QString> labels{{"completionCriteria", "Completion criteria"},
                                        {"conclusion", "Case conclusion"},
                                        {"unresolvedQuestions", "Unresolved questions"},
                                        {"closureRationale", "Closure rationale"}};
    html += "<h3>" + labels[key] + "</h3>" + paragraphs(i[key].toString());
  }
  html += "<p class='warning'>" + h(out.manifest["limitations"].toString()) + "</p>";
  if (!map.isNull()) {
    require(!capture["capturedAt"].isUndefined() && !capture["attribution"].toString().isEmpty(),
            "Map capture needs capture time and attribution.");
    const auto scaled = map.scaled(1600, 1200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    out.images["godeye:map"] = scaled;
    auto meta = capture;
    meta["sha256"] = imageHash(scaled);
    meta["meaning"] = "Map context captured from display; not an acquisition date or a "
                      "georeferenced measurement.";
    out.manifest["mapCapture"] = meta;
    html += "<h2 style='page-break-before:always'>Explicitly captured map context</h2>" +
            imageTag("godeye:map", scaled, 620, 500) + "<p class='meta'>Captured " +
            h(dateLabel(capture["capturedAt"])) + ". " + h(capture["attribution"].toString()) +
            "<br/>Display capture; acquisition dates are listed per source.</p>";
  }
  html += "<h2>Assets and relationships</h2>";
  for (auto v : i["snapshots"].toArray()) {
    const auto s = v.toObject(), a = s["asset"].toObject(), r = s["relationship"].toObject();
    if (!a.isEmpty()) {
      html += "<h3>" + h(a["name"].toString()) + " (revision " +
              QString::number(a["revision"].toInt()) + ")</h3><p>" + h(a["category"].toString()) +
              (a["archived"].toBool() ? " / ARCHIVED" : "") +
              "<br/>Aliases: " + h(a["aliases"].toVariant().toStringList().join(", ")) + "</p>";
      if (auto b = Bounds::read(a["boundary"]))
        html += "<p class='meta'>Boundary W/S/E/N: " +
                QString("%1 / %2 / %3 / %4")
                    .arg(b->west, 0, 'f', 6)
                    .arg(b->south, 0, 'f', 6)
                    .arg(b->east, 0, 'f', 6)
                    .arg(b->north, 0, 'f', 6) +
                "</p>";
      else if (auto p = Coordinate::read(a["location"].toObject()))
        html += "<p class='meta'>Location: " + QString::number(p->latitude, 'f', 6) + ", " +
                QString::number(p->longitude, 'f', 6) + "</p>";
      html += "<p class='meta'>Asset " + h(a["id"].toString()) + "<br/>Snapshot " +
              h(s["id"].toString()) + " / adopted " + h(dateLabel(s["capturedAt"])) + "</p>";
      for (auto cv : a["citations"].toArray()) {
        auto c = cv.toObject();
        html += "<p class='meta'>Asset source: " + h(c["label"].toString()) + " / " +
                h(c["provider"].toString()) + " / " + h(c["sourceID"].toString()) + " / " +
                h(c["reference"].toString()) + "</p>";
      }
    } else {
      html += "<h3>Relationship: " + h(r["kind"].toString()) + "</h3><p>" +
              h(r["fromAssetID"].toString()) + " &rarr; " + h(r["toAssetID"].toString()) +
              "<br/>Review: " + h(r["reviewState"].toString()) + " / revision " +
              QString::number(r["revision"].toInt()) +
              (r["archived"].toBool() ? " / ARCHIVED" : "") +
              "<br/>Validity: " + h(r["validFrom"].toString()) + " through " +
              h(r["validThrough"].toString()) + "</p>" + paragraphs(r["rationale"].toString());
      for (auto cv : r["citations"].toArray()) {
        auto c = cv.toObject();
        html += "<p class='meta'>Relationship source: " + h(c["label"].toString()) + " / " +
                h(c["sourceID"].toString()) + " / " + h(c["reference"].toString()) +
                " / Source date " + h(dateLabel(c["sourceDate"])) + "<br/>" +
                h(c["qualityLimitations"].toString()) + "</p>";
      }
    }
  }
  QMap<QString, QJsonObject> cites;
  for (auto v : i["citations"].toArray())
    cites[v.toObject()["id"].toString()] = v.toObject();
  html += "<h2 style='page-break-before:always'>Selected findings</h2>";
  int index = 0;
  for (auto v : findings) {
    const auto f = v.toObject();
    html += "<h3>" + QString::number(++index) + ". " + h(f["state"].toString()) +
            " / analyst judgment</h3>" + paragraphs(f["claim"].toString()) +
            "<p class='meta'>Finding " + h(f["id"].toString()) + " / revision " +
            QString::number(f["revision"].toInt()) +
            "<br/>Time context: " + h(f["timeContext"].toString()) +
            "</p><p><b>Review rationale</b></p>" + paragraphs(f["rationale"].toString()) +
            "<p><b>Alternative explanations and uncertainty</b></p>" +
            paragraphs(f["alternatives"].toString());
    html += "<p class='meta'>Asset snapshot references: " +
            h(f["assetSnapshotIDs"].toVariant().toStringList().join(", ")) +
            "<br/>Area references: " + h(f["areaIDs"].toVariant().toStringList().join(", ")) +
            "</p>";
    for (auto av : f["areaSnapshots"].toArray()) {
      const auto area = av.toObject();
      html += "<p class='meta'>Retained area: " + h(area["name"].toString()) + " / " +
              h(area["purpose"].toString()) + "<br/>" + h(area["id"].toString()) + "</p>";
    }
    for (auto cv : f["citations"].toArray()) {
      const auto link = cv.toObject(), c = cites[link["citationID"].toString()];
      html +=
          "<p><b>" + h(link["role"].toString()) + ": " + h(c["label"].toString()) + "</b><br/>" +
          h(c["provider"].toString()) + " / " + h(c["sourceID"].toString()) +
          "<br/>Source date: " + h(dateLabel(c["sourceDate"])) + " / recorded " +
          h(dateLabel(c["capturedAt"])) + "<br/>" + h(c["reference"].toString()) +
          "</p><p class='meta'>" + h(c["qualityLimitations"].toString()) +
          "<br/>Recorded fingerprint: " +
          h(c["fingerprint"].toString().isEmpty() ? "Not recorded" : c["fingerprint"].toString()) +
          "</p>";
    }
    html += "<p class='meta'>Retained prior review decisions: " +
            QString::number(f["revisions"].toArray().size()) +
            " (full versions in the evidence manifest).</p>";
  }
  html += "<h2 style='page-break-before:always'>Evidence availability and previews</h2>";
  QJsonArray availability;
  qint64 previewBytes = 0;
  for (const auto &image : out.images)
    previewBytes += image.sizeInBytes();
  for (auto v : i["citations"].toArray()) {
    const auto c = v.toObject(), e = c["evidence"].toObject();
    QJsonObject state{{"citationID", c["id"]},
                      {"sourceID", c["sourceID"]},
                      {"recordedFingerprint", c["fingerprint"]},
                      {"availability", "Retained source snapshot; no local raster expected"}};
    html += "<h3>" + h(c["label"].toString()) + "</h3>";
    const bool raster = e["kind"] == "optical" || e["kind"] == "radar";
    QString manifestPath;
    bool preview = false;
    for (const auto &layer : library)
      if (layer.jobID == e["referenceID"].toString() && layer.kind == e["kind"].toString()) {
        manifestPath = layer.manifest;
        break;
      }
    if (raster) {
      state["availability"] = "UNAVAILABLE: local product not found";
      if (!manifestPath.isEmpty())
        try {
          const auto bytes = JsonFile::read(manifestPath, 8 * 1024 * 1024);
          state["localManifest"] = manifestPath;
          state["observedManifestSHA256"] = digest(bytes);
          const auto recorded = c["fingerprint"].toString();
          if (!recorded.isEmpty() && recorded != digest(bytes))
            state["availability"] =
                "UNAVAILABLE: local manifest differs from the recorded fingerprint";
          else
            for (const auto &p : readProducts(manifestPath, false))
              if (p.kind == e["kind"].toString()) {
                const auto key = "godeye:evidence/" + c["id"].toString();
                state["previewPath"] = p.preview;
                state["geotiffPath"] = p.geotiff;
                state["scientificPath"] = p.scientific;
                if (out.images.size() >= 24 || previewBytes >= 64 * 1024 * 1024) {
                  state["availability"] = "Preview omitted: bounded export budget (24 images / 64 "
                                          "MB decoded). External raster remains referenced.";
                  break;
                }
                const auto bytes = JsonFile::read(p.preview, 32 * 1024 * 1024);
                QBuffer buffer;
                buffer.setData(bytes);
                buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer);
                const auto size = reader.size();
                require(size.isValid() && size.width() <= 2048 && size.height() <= 2048,
                        "Preview dimensions changed or exceed bounds.");
                reader.setScaledSize(size.scaled(1200, 1200, Qt::KeepAspectRatio));
                const auto image = reader.read();
                require(!image.isNull(), "Preview could not be decoded.");
                if (previewBytes + image.sizeInBytes() > 64 * 1024 * 1024) {
                  state["availability"] = "Preview omitted: bounded export budget (24 images / 64 "
                                          "MB decoded). External raster remains referenced.";
                  break;
                }
                previewBytes += image.sizeInBytes();
                out.images[key] = image;
                state["previewSHA256"] = imageHash(image);
                state["previewPath"] = p.preview;
                state["geotiffPath"] = p.geotiff;
                state["scientificPath"] = p.scientific;
                state["availability"] = p.geotiff.isEmpty() && p.scientific.isEmpty()
                                            ? "Preview available; scientific raster unavailable"
                                            : "Preview and external raster available";
                html += imageTag(key, image, 560, 420) + "<p class='meta'>Display preview / " +
                        h(p.sensor) + " / source " + h(p.sourceID) + " / acquisition " +
                        h(p.acquisition) + ". Source attribution: " + h(c["provider"].toString()) +
                        ".</p>";
                preview = true;
                break;
              }
        } catch (const std::exception &ex) {
          state["availability"] = "UNAVAILABLE: " + QString::fromUtf8(ex.what());
        }
    } else if ((QFileInfo(c["reference"].toString()).isAbsolute() ||
                !c["origin"].toObject()["manifest"].toString().isEmpty()) &&
               c["origin"].isObject()) {
      const auto path = c["reference"].toString();
      if (!QFileInfo::exists(path))
        state["availability"] =
            "UNAVAILABLE: original local report; retained origin snapshot remains available";
      else
        state["availability"] = "Original local report available; retained source snapshot used";
    }
    html += "<p class='" + QString(raster && !preview ? "warning" : "meta") + "'>" +
            h(state["availability"].toString()) + "</p>";
    availability.append(state);
  }
  out.manifest["evidenceAvailability"] = availability;
  html += "<h2>Outstanding follow-up</h2>";
  bool any = false;
  for (auto v : plan["brief"].toObject()["tasks"].toArray()) {
    const auto t = v.toObject();
    if (t["done"].toBool())
      continue;
    any = true;
    html += "<p><b>" + h(t["title"].toString()) +
            "</b><br/>Owner label: " + h(t["owner"].toString()) + " / due " +
            h(dateLabel(t["due"])) + "<br/>Finding origin: " + h(t["findingID"].toString()) +
            "<br/>Prerequisite task IDs: " +
            h(t["dependencyIDs"].toVariant().toStringList().join(", ")) + "</p>";
  }
  if (!any)
    html += "<p>No unfinished tasks.</p>";
  html += "<h2>Reproduction record</h2><p class='meta'>Export " +
          h(out.manifest["exportID"].toString()) +
          "<br/>Case document SHA-256: " + h(out.manifest["caseDocumentSHA256"].toString()) +
          "<br/>The manifest records selected finding versions, retained review revisions, source "
          "identities, recorded fingerprints, derivation references and availability at freeze "
          "time. Raster files remain external. Regenerating this PDF does not query providers or "
          "change review states.</p></body></html>";
  out.manifest["rendering"] = QJsonObject{
      {"templateVersion", 1}, {"pageSize", "A4"}, {"htmlSHA256", digest(html.toUtf8())}};
  QSet<QString> selectedIDs(selected.begin(), selected.end());
  QJsonArray unselectedOrigins;
  for (auto v : plan["brief"].toObject()["tasks"].toArray()) {
    const auto fid = v.toObject()["findingID"].toString();
    if (!fid.isEmpty() && !selectedIDs.contains(fid) && !unselectedOrigins.contains(fid))
      unselectedOrigins.append(fid);
  }
  out.manifest["unselectedTaskFindingIDs"] = unselectedOrigins;
  out.html = html;
  out.manifest["contentSHA256"] = jsonHash(out.manifest);
  validateBriefingManifest(out.manifest);
  return out;
}
void validateBriefingManifest(const QJsonObject &m) {
  require(m["schema"] == "org.godeye.investigation-briefing" && m["version"] == 1,
          "Invalid briefing manifest schema.");
  auto content = m;
  content.remove("contentSHA256");
  content.remove("pdfSHA256");
  require(m["contentSHA256"] == jsonHash(content),
          "Briefing manifest content fingerprint mismatch.");
  require(QJsonDocument(m).toJson(QJsonDocument::Indented).size() <= 16 * 1024 * 1024,
          "Briefing manifest exceeds 16 MB.");
  QSet<QString> citations, snapshots, findings;
  for (auto v : m["citations"].toArray())
    citations.insert(v.toObject()["id"].toString());
  for (auto v : m["snapshots"].toArray())
    snapshots.insert(v.toObject()["id"].toString());
  for (auto v : m["findings"].toArray()) {
    const auto f = v.toObject();
    findings.insert(f["id"].toString());
    for (auto ref : f["assetSnapshotIDs"].toArray())
      require(snapshots.contains(ref.toString()), "Manifest finding asset reference missing.");
    auto checkCites = [&](const QJsonArray &refs) {
      for (auto ref : refs)
        require(citations.contains(ref.toObject()["citationID"].toString()),
                "Manifest finding citation missing.");
    };
    checkCites(f["citations"].toArray());
    for (auto old : f["revisions"].toArray())
      checkCites(old.toObject()["citations"].toArray());
  }
  require(findings.size() == m["selectedFindingIDs"].toArray().size(),
          "Manifest selection mismatch.");
  for (auto v : m["selectedFindingIDs"].toArray())
    require(findings.contains(v.toString()), "Manifest selected finding missing.");
  QSet<QString> assets, tasks;
  for (auto v : m["snapshots"].toArray())
    if (v.toObject()["asset"].isObject())
      assets.insert(v.toObject()["asset"].toObject()["id"].toString());
  for (auto v : m["snapshots"].toArray())
    if (v.toObject()["relationship"].isObject()) {
      const auto r = v.toObject()["relationship"].toObject();
      require(assets.contains(r["fromAssetID"].toString()) &&
                  assets.contains(r["toAssetID"].toString()),
              "Manifest relationship endpoint missing.");
    }
  for (auto v : m["associations"].toArray()) {
    const auto a = v.toObject();
    require(snapshots.contains(a["assetSnapshotID"].toString()) &&
                citations.contains(a["citationID"].toString()),
            "Manifest association reference missing.");
  }
  for (auto v : m["tasks"].toArray())
    tasks.insert(v.toObject()["id"].toString());
  for (auto v : m["tasks"].toArray()) {
    const auto t = v.toObject();
    for (auto dep : t["dependencyIDs"].toArray())
      require(tasks.contains(dep.toString()), "Manifest task prerequisite missing.");
    const auto fid = t["findingID"].toString();
    require(fid.isEmpty() || findings.contains(fid) ||
                m["unselectedTaskFindingIDs"].toArray().contains(fid),
            "Manifest task finding origin missing.");
  }
  for (auto v : m["evidenceAvailability"].toArray())
    require(citations.contains(v.toObject()["citationID"].toString()),
            "Manifest availability reference missing.");
}
QString exportBriefing(const FrozenBriefing &briefing, const QString &parent) {
  validateBriefingManifest(briefing.manifest);
  require(briefing.manifest["rendering"].toObject()["htmlSHA256"] == digest(briefing.html.toUtf8()),
          "Frozen briefing HTML changed after preparation.");
  require(QFileInfo(parent).isDir(), "Choose an existing export directory.");
  const auto suffix = newID().left(8);
  const auto name =
      "briefing-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss") + "-" + suffix;
  QDir dir(parent);
  const auto temporary = ".godeye-briefing-" + newID();
  require(dir.mkdir(temporary), "Cannot create briefing staging directory.");
  QDir stage(dir.filePath(temporary));
  try {
    {
      QPdfWriter writer(stage.filePath("briefing.pdf"));
      writer.setPageSize(QPageSize(QPageSize::A4));
      writer.setResolution(96);
      writer.setPageMargins(QMarginsF(16, 18, 16, 18));
      writer.setTitle(briefing.manifest["case"].toObject()["name"].toString());
      writer.setCreator("GodEye / native Qt briefing");
      QTextDocument doc;
      doc.setDefaultFont(QFont("Noto Sans", 10));
      QTextOption option;
      option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
      doc.setDefaultTextOption(option);
      doc.setHtml(briefing.html);
      for (auto it = briefing.images.begin(); it != briefing.images.end(); ++it)
        doc.addResource(QTextDocument::ImageResource, QUrl(it.key()), it.value());
      const qreal bodyHeight = writer.height() - 48;
      doc.setPageSize(QSizeF(writer.width(), bodyHeight));
      require(doc.size().height() / bodyHeight <= 400,
              "Briefing exceeds the 400-page export limit. Select fewer findings or shorten source "
              "text.");
      QPainter painter;
      require(painter.begin(&writer), "Cannot start PDF writer.");
      const int pages = std::max(1, int(std::ceil(doc.size().height() / bodyHeight)));
      for (int n = 0; n < pages; ++n) {
        if (n > 0)
          require(writer.newPage(), "Cannot add PDF page.");
        painter.setPen(QColor("#436579"));
        painter.setFont(QFont("Noto Sans", 8));
        painter.drawText(QRectF(0, 0, writer.width(), 18), Qt::AlignLeft,
                         "GODEYE  /  " +
                             briefing.manifest["case"].toObject()["name"].toString().left(90));
        painter.save();
        painter.translate(0, 24 - n * bodyHeight);
        QRectF clip(0, n * bodyHeight, writer.width(), bodyHeight);
        painter.setClipRect(clip);
        QAbstractTextDocumentLayout::PaintContext context;
        context.clip = clip;
        doc.documentLayout()->draw(&painter, context);
        painter.restore();
        painter.drawText(QRectF(0, writer.height() - 18, writer.width(), 18), Qt::AlignRight,
                         QString("LOCAL ANALYST RECORD    |    %1 / %2").arg(n + 1).arg(pages));
      }
      require(painter.end(), "Cannot finish PDF.");
    }
    QFile pdf(stage.filePath("briefing.pdf"));
    require(pdf.open(QIODevice::ReadOnly) && pdf.size() > 1000, "PDF export failed.");
    auto manifest = briefing.manifest;
    manifest["pdfSHA256"] = digest(pdf.readAll());
    pdf.close();
    QSaveFile file(stage.filePath("evidence-manifest.json"));
    require(file.open(QIODevice::WriteOnly), "Cannot write evidence manifest.");
    const auto bytes = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    require(file.write(bytes) == bytes.size() && file.commit(), "Cannot finish evidence manifest.");
    require(dir.rename(temporary, name), "Cannot publish briefing folder.");
    return dir.filePath(name);
  } catch (...) {
    stage.removeRecursively();
    throw;
  }
}
} // namespace godeye

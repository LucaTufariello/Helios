#pragma once
#include "Core/DailyImagery.h"
#include "Core/Products.h"
#include "HeliosScene.h"
#include "TileCache.h"
#include <QColor>
#include <QPointer>
#include <QRhiWidget>
#include <QTimer>
#include <QUrl>
#include <array>
#include <map>
#include <memory>

class QRhiGraphicsPipeline;
class QRhiSampler;
class QRhiTexture;
class QRhiBuffer;
class QRhiShaderResourceBindings;
class QRhiResourceUpdateBatch;
namespace helios {
struct SurfaceHit {
  Coordinate coordinate;
  double height = 0;
  bool terrain = false;
  bool scene = false;
};
struct MapMarker {
  QString id;
  Coordinate coordinate;
  std::optional<double> altitude;
};
class MapWidget : public QRhiWidget {
  Q_OBJECT
public:
  explicit MapWidget(const QString &cacheDirectory, bool networking, QWidget *parent = nullptr);
  ~MapWidget() override;
  Camera camera() const {
    return camera_;
  }
  void setCamera(const Camera &camera);
  void focus(Bounds bounds);
  void zoom(double steps);
  void setStreets(bool streets);
  void setTerrain(bool terrain);
  bool terrainEnabled() const {
    return terrain_;
  }
  void orient(double heading, double tilt);
  void setAreas(const QVector<Bounds> &areas);
  void setMarkers(const QVector<Coordinate> &coordinates);
  bool setMarkers(const QString &layer, const QVector<MapMarker> &markers,
                  QColor color = QColor(70, 215, 255));
  bool setPaths(const QString &layer, const QVector<QVector<Coordinate>> &paths,
                const QVector<QVector<double>> &altitudes = {},
                QColor color = QColor(128, 217, 255));
  // Draws every line in one batched call. Intended for large reference layers
  // (for example bundled cable routes) whose paths are not inspected one by one.
  bool setBatchedPaths(const QString &layer, const QVector<QVector<Coordinate>> &paths,
                       QColor color = QColor(128, 217, 255));
  bool hasPathLayer(const QString &layer) const {
    return paths_.contains(layer);
  }
  std::optional<QPointF> screenPoint(Coordinate point, std::optional<double> altitude = {}) const;
  bool streets() const {
    return streets_;
  }
  bool setDailyImagery(DailySource source, QDate date, double opacity);
  DailyImageryCache &dailyImagery() {
    return daily_;
  }
  double dailyOpacity() const {
    return dailyOpacity_;
  }
  void setOverlays(const QVector<ProductLayer> &products);
  QStringList overlayIDs() const {
    QStringList ids;
    for (const auto &p : overlays_)
      ids.append(p.id);
    return ids;
  }
  void setBlend(double blend);
  void setPixelGrid(bool grid);
  void beginPointCapture(QObject *owner);
  void endPointCapture(QObject *owner);
  void beginRegion(QObject *owner = nullptr);
  void cancelRegion();
  bool loadTileset(const QUrl &url);
  void removeTileset();
  bool focusTileset();
  QString sceneStatus() const;
  QString sceneCredits() const;
  int sceneMeshes() const {
    return sceneDrawn_;
  }
  bool hardwareGpu() const {
    return hardwareGpu_;
  }
  std::optional<SurfaceHit> pick(QPointF position) const;
  TileCache &tiles() {
    return tiles_;
  }
  QString backend() const {
    return backend_;
  }
  QString gpu() const {
    return gpu_;
  }
  QString error() const {
    return error_;
  }
  quint64 frames() const {
    return frames_;
  }
  int renderedTiles() const {
    return renderedTiles_;
  }
signals:
  void cameraChanged();
  void viewportChanged();
  void markerSelected(QString id);
  void statusChanged();
  void pointSelected(helios::Coordinate point, double height, bool terrain, bool scene);
  void regionSelected(helios::Bounds bounds, QObject *owner);
  void selectionChanged(bool active);

protected:
  void initialize(QRhiCommandBuffer *cb) override;
  void render(QRhiCommandBuffer *cb) override;
  void releaseResources() override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  bool event(QEvent *event) override;

private:
  struct CpuMesh {
    GEVector3 origin{};
    QVector<GEVector3> positions;
    QVector<float> vertices;
    QVector<quint32> indices;
    QVector<QRectF> cutouts;
    QImage image;
    QString textureKey;
    bool line = false, lineList = false, overlay = false, terrain = false, scene = false,
         daily = false, background = false;
    bool doubleSided = false, blend = false;
    float alphaCutoff = -1;
    int wrapS = 10497, wrapT = 10497, magFilter = 9729, minFilter = 9987;
    float opacity = 1;
    std::array<float, 4> tint{1, 1, 1, 1};
  };
  struct GpuMesh;
  bool networking_ = true;
  TileCache tiles_;
  DailyImageryCache daily_;
  Camera camera_;
  QTimer selectionTimer_;
  QTimer sceneTimer_;
  QTimer terrainTimer_;
  std::unique_ptr<GEScene, decltype(&GE_scene_destroy)> scene_{nullptr, GE_scene_destroy};
  QVector<GEMesh> sceneMeshes_;
  QHash<uint64_t, QImage> sceneImages_;
  int sceneDrawn_ = 0, sceneSettled_ = 0;
  bool sceneFocusPending_ = false, sceneBudgetExceeded_ = false, hardwareGpu_ = false;
  QVector<TileID> wantedTiles_;
  QVector<Bounds> areas_;
  struct MarkerLayer {
    QVector<MapMarker> markers;
    QColor color;
  };
  struct PathLayer {
    QVector<QVector<Coordinate>> paths;
    QVector<QVector<double>> altitudes;
    QColor color;
    bool batched = false;
  };
  QMap<QString, MarkerLayer> markers_;
  QMap<QString, PathLayer> paths_;
  QVector<ProductLayer> overlays_;
  std::map<QString, std::unique_ptr<GpuMesh>> meshes_;
  std::map<QString, std::shared_ptr<QRhiTexture>> productTextures_;
  QVector<QString> drawn_;
  std::unique_ptr<QRhiBuffer> layoutUniform_;
  std::unique_ptr<QRhiTexture> white_;
  std::unique_ptr<QRhiSampler> linear_, nearest_;
  std::unique_ptr<QRhiShaderResourceBindings> layoutBindings_;
  std::unique_ptr<QRhiGraphicsPipeline> triangles_, overlayPipeline_, lines_, lineList_,
      background_;
  std::array<std::unique_ptr<QRhiGraphicsPipeline>, 4> scenePipelines_;
  bool streets_ = false, terrain_ = true, grid_ = true, selecting_ = false;
  std::optional<Coordinate> firstCorner_;
  QPointer<QObject> regionOwner_, pointCaptureOwner_;
  QPointF press_, previous_;
  double blend_ = 0.9, dailyOpacity_ = 0.7;
  quint64 frames_ = 0, geometryRevision_ = 1, markerRevision_ = 1, pathRevision_ = 1;
  int renderedTiles_ = 0;
  bool statusPending_ = false;
  QString backend_, gpu_, error_;
  QMatrix4x4 frameMatrix_;
  GEVector3 frameTarget_{};
  double targetHeight() const;
  void refresh();
  void requestTiles();
  void ensureClearance();
  void updateTileset();
  void queueStatusChanged();
  CpuMesh sceneMesh(const GEMesh &mesh);
  CpuMesh tileMesh(TileID id, const QImage &image) const;
  CpuMesh productMesh(const ProductLayer &product, TileID tile) const;
  CpuMesh outlineMesh(Bounds bounds) const;
  CpuMesh pathMesh(const QVector<Coordinate> &path, const QVector<double> &altitudes = {},
                   double step = 0.25, int maxSegments = 32) const;
  CpuMesh pathBatchMesh(const QVector<QVector<Coordinate>> &paths) const;
  bool setPathLayer(const QString &layer, const QVector<QVector<Coordinate>> &paths,
                    const QVector<QVector<double>> &altitudes, QColor color, bool batched);
  CpuMesh markerMesh(const MarkerLayer &layer) const;
  CpuMesh globeMesh() const;
  void vertex(CpuMesh &mesh, Coordinate point, double height, double u, double v) const;
  void drawMesh(const QString &key, CpuMesh mesh, quint64 revision, QRhiCommandBuffer *cb,
                QRhiResourceUpdateBatch *updates);
};
} // namespace helios

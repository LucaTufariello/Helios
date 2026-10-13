#pragma once
#include "Core/Products.h"
#include "TileCache.h"
#include <QRhiWidget>
#include <QTimer>
#include <array>
#include <map>
#include <memory>

class QRhiGraphicsPipeline;
class QRhiSampler;
class QRhiTexture;
class QRhiBuffer;
class QRhiShaderResourceBindings;
class QRhiResourceUpdateBatch;
namespace godeye {
struct SurfaceHit {
  Coordinate coordinate;
  double height = 0;
  bool terrain = false;
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
  void setOverlays(const QVector<ProductLayer> &products);
  void setBlend(double blend);
  void setPixelGrid(bool grid);
  void beginRegion();
  void cancelRegion();
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
  void statusChanged();
  void pointSelected(godeye::Coordinate point, double height, bool terrain);
  void regionSelected(godeye::Bounds bounds);
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
    bool line = false, overlay = false, terrain = false;
    float opacity = 1;
    std::array<float, 4> tint{1, 1, 1, 1};
  };
  struct GpuMesh;
  TileCache tiles_;
  Camera camera_;
  QTimer selectionTimer_;
  QVector<TileID> wantedTiles_;
  QVector<Bounds> areas_;
  QVector<Coordinate> markers_;
  QVector<ProductLayer> overlays_;
  std::map<QString, std::unique_ptr<GpuMesh>> meshes_;
  std::map<QString, std::shared_ptr<QRhiTexture>> productTextures_;
  QVector<QString> drawn_;
  std::unique_ptr<QRhiBuffer> layoutUniform_;
  std::unique_ptr<QRhiTexture> white_;
  std::unique_ptr<QRhiSampler> linear_, nearest_;
  std::unique_ptr<QRhiShaderResourceBindings> layoutBindings_;
  std::unique_ptr<QRhiGraphicsPipeline> triangles_, overlayPipeline_, lines_;
  bool streets_ = false, terrain_ = true, grid_ = true, selecting_ = false;
  std::optional<Coordinate> firstCorner_;
  QPointF press_, previous_;
  double blend_ = 0.9;
  quint64 frames_ = 0, geometryRevision_ = 1;
  int renderedTiles_ = 0;
  QString backend_, gpu_, error_;
  QMatrix4x4 frameMatrix_;
  GEVector3 frameTarget_{};
  double targetHeight() const;
  void refresh();
  void requestTiles();
  void ensureClearance();
  CpuMesh tileMesh(TileID id, const QImage &image) const;
  CpuMesh productMesh(const ProductLayer &product, TileID tile) const;
  CpuMesh outlineMesh(Bounds bounds) const;
  CpuMesh globeMesh() const;
  void vertex(CpuMesh &mesh, Coordinate point, double height, double u, double v) const;
  void drawMesh(const QString &key, CpuMesh mesh, quint64 revision, QRhiCommandBuffer *cb,
                QRhiResourceUpdateBatch *updates);
};
} // namespace godeye

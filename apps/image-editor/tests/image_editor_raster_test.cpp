#include "image_document_session.h"
#include "recovery_store.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace image_editor;
namespace {
void require(bool ok, const QString& cause) {
    if (!ok) throw std::runtime_error(cause.toStdString());
}
QByteArray bytes(const QString& path) {
    QFile f(path); require(f.open(QIODevice::ReadOnly), f.errorString()); return f.readAll();
}
void write(const QString& path, const QByteArray& data) {
    QFile f(path); require(f.open(QIODevice::WriteOnly), f.errorString());
    require(f.write(data) == data.size(), f.errorString());
}
QString imageFile(const QString& root, const QString& name, QSize size = {16,16}, QColor color = Qt::red) {
    QImage image(size, QImage::Format_ARGB32); image.fill(color);
    const QString path = root + "/" + name; require(image.save(path), "Fixture encoding failed."); return path;
}
QJsonObject json(const QString& path) { return QJsonDocument::fromJson(bytes(path)).object(); }
ImageObjectPlacement placement(const ImageDocumentSession& session, const QString& id) {
    for (const auto& p : session.visibleObjects())
        if (p.operation.kind == OperationKind::RasterImage && p.operation.raster.id == id) return p;
    throw std::runtime_error("Missing raster placement.");
}
const ImageLayerData& layerById(const ImageDocumentSession& session, const QString& id) {
    for (const auto& layer : session.data().layers) if (layer.id == id) return layer;
    throw std::runtime_error("Missing layer.");
}
void decodeTests(const QString& root) {
    const QList<QByteArray> formats{"png","jpeg","bmp","webp","tiff"};
    for (const auto& format : formats) {
        const QString path = root + "/format." + QString::fromLatin1(format);
        QImage source(3,2,QImage::Format_ARGB32); source.fill(QColor(40,100,180,128));
        QImageWriter writer(path, format); require(writer.write(source), "encode " + QString::fromLatin1(format) + ": " + writer.errorString());
        auto decoded = prepareRasterImport({path,path});
        require(decoded.status == RasterImportStatus::Ready && decoded.images.size() == 2, "decode " + QString::fromLatin1(format) + ": " + decoded.cause);
        require(decoded.images[0].image.size() == QSize(3,2) &&
            decoded.images[0].image.cacheKey() == decoded.images[1].image.cacheKey(), "Duplicate decode did not share pixels.");
        if (format == "png" || format == "tiff")
            require(decoded.images[0].image.pixelColor(1,1).alpha() == 128, "Transparency lost.");
    }
    const QString jpg = imageFile(root, "orientation.jpg", {3,2});
    auto encoded = bytes(jpg);
    const auto exif = QByteArray::fromHex("ffe1002245786966000049492a0008000000010012010300010000000600000000000000");
    encoded.insert(2,exif); write(jpg,encoded);
    const auto oriented = prepareRasterImport({jpg});
    require(oriented.status == RasterImportStatus::Ready && oriented.images.front().image.size() == QSize(2,3),
        "EXIF orientation was not applied.");
    std::atomic_bool cancelled{true};
    require(prepareRasterImport({jpg}, &cancelled).status == RasterImportStatus::Cancelled, "Cancellation ignored.");
    require(prepareRasterImport({}).status == RasterImportStatus::Failed, "Empty batch accepted.");
    QStringList huge; for (int i=0; i<513; ++i) huge.append(jpg);
    require(prepareRasterImport(huge).status == RasterImportStatus::Failed, "Decode batch limit ignored.");
    const auto failure = prepareRasterImport({jpg,root+"/missing.png"});
    require(failure.status == RasterImportStatus::Failed && failure.images.isEmpty() && !failure.failed_path.isEmpty(),
        "Failed batch retained partial images.");
    const QString bad=root+"/corrupt.png"; write(bad,"bad image");
    require(prepareRasterImport({bad}).status == RasterImportStatus::Failed, "Corrupt image accepted.");
    QImage unsupported(2,2,QImage::Format_RGB32); unsupported.fill(Qt::red);
    require(unsupported.save(root+"/unsupported.ppm"), "PPM fixture failed.");
    require(prepareRasterImport({root+"/unsupported.ppm"}).status == RasterImportStatus::Failed, "Unsupported image accepted.");
}
void geometryTests(const QString& root) {
    QString error;
    const QString source=imageFile(root,"geometry.png");
    const QByteArray original=bytes(source);
    const auto decoded=prepareRasterImport({source,source});
    ImageDocumentSession empty;
    require(!empty.importRasterImages(decoded.images,{},&error), "Import without document accepted.");
    ImageDocumentSession s; require(s.createCanvas({64,64},Qt::transparent,&error),error);
    const auto before=s.data();
    auto invalid=decoded.images; invalid[1].image={};
    require(!s.importRasterImages(invalid,{},&error) && s.data()==before, "Invalid batch partially committed.");
    require(!s.importRasterImages(decoded.images,QPointF(std::numeric_limits<double>::infinity(),0),&error), "Invalid anchor accepted.");
    require(s.importRasterImages(decoded.images,QPointF(20,20),&error),error);
    require(s.data().layers.size()==4 && s.selectedLayerId()==s.data().layers.back().id, "Batch order/selection incorrect.");
    require(s.data().layers.back().name=="geometry.png", "Layer file name incorrect.");
    const auto raster=s.data().layers.back().operations.front().raster;
    require(raster.transform.map(QPointF(0,0))==QPointF(12,12) &&
        raster.transform.m11()==1, "Drop placement or no-upscale rule incorrect.");
    require(s.undo() && s.data()==before && s.redo() && s.data().layers.size()==4, "Batch undo was not atomic.");
    const QString layer=s.selectedLayerId();
    require(s.setLayerVisible(s.data().layers[2].id,false), "Isolation setup failed.");
    require(s.addLayerMask(layer) && s.applyLayerMaskEraseStroke({QPointF(20,20)},4,&error),error);
    require(s.applyPaintStroke({QPointF(4,4)},Qt::yellow,4,&error),error);
    const auto mask=s.data().layers.back().mask;
    const auto paint=s.data().layers.back().operations.back();
    auto p=placement(s,raster.id);
    p.operation.raster.transform*=QTransform::fromTranslate(8,0);
    const auto stable=s.data();
    const auto preview=s.renderedImageWithObjects({p});
    require(s.data()==stable && preview.pixelColor(20,20).alpha()==0 &&
        preview.pixelColor(28,20).red()==255 && preview.pixelColor(4,4)==QColor(Qt::yellow),
        "Core preview moved the mask/paint or changed history.");
    require(s.updateObjectsRendered({p},&error),error);
    require(s.renderedImage()==preview && s.data().layers.back().mask==mask &&
        s.data().layers.back().operations.back()==paint, "Raster move changed other operations.");
    require(s.undo() && s.data()==stable && s.redo() && s.renderedImage()==preview, "Geometry undo/redo failed.");
    auto scale=placement(s,raster.id);
    scale.operation.raster.transform=QTransform(2,0,0,1, -4,5);
    require(s.updateObjectsRendered({scale},&error),error);
    QTransform rotation; rotation.translate(32,32); rotation.rotate(37); rotation.translate(-32,-32);
    auto rotated=placement(s,raster.id); rotated.operation.raster.transform*=rotation;
    require(s.updateObjectsRendered({rotated},&error),error);
    const auto unchanged=s.data();
    rotated.operation.raster.transform=QTransform(0,0,0,0,0,0);
    require(!s.updateObjectsRendered({rotated},&error) && s.data()==unchanged, "Singular geometry committed.");
    rotated=placement(s,raster.id); rotated.operation.raster.source_path=root+"/other.png";
    require(!s.updateObjectsRendered({rotated},&error), "Geometry edit changed a reference.");
    const QImage image=s.renderedImage();
    s.flipHorizontal(); require(s.renderedImage()==image.mirrored(true,false), "Existing layer flip left mask behind.");
    auto flipped=placement(s,raster.id);
    flipped.operation.raster.transform*=QTransform::fromTranslate(2,0);
    require(s.updateObjectsRendered({flipped},&error),error);
    require(placement(s,raster.id).operation.raster.transform==flipped.operation.raster.transform, "Suffix geometry inverse failed.");
    s.rotateRight(); s.rotateLeft();
    require(s.applyCrop({2,2,50,50},&error),error);
    require(!s.renderedLayerThumbnails({32,32}).value(layer).isNull(), "Raster thumbnail missing.");
    require(bytes(source)==original, "Editing changed the original file.");
    const QString large=imageFile(root,"large.png",{80,40});
    require(s.importRasterImages(prepareRasterImport({large}).images,{},&error),error);
    const auto fitted=s.data().layers.back().operations.front().raster;
    require(fitted.transform.m11()==0.8 && fitted.transform.dx()==0 && fitted.transform.dy()==16, "Fit to canvas incorrect.");
    const QString group=s.groupLayers({layer,s.selectedLayerId()},&error); require(!group.isEmpty(),error);
    require(s.selectLayer(layer), "Cannot select group child.");
    require(s.importRasterImages(decoded.images,{},&error),error);
    require(layerById(s,s.selectedLayerId()).parent_group_id==group, "Import outside selected child's group.");
    require(s.selectGroup(group) && s.importRasterImages(decoded.images,{},&error),error);
    require(s.data().layers.back().parent_group_id.isEmpty() && !s.data().root_stack.back().group, "Group-selected import did not use root.");
    require(s.selectGroup(group), "Cannot select group.");
    s.flipVertical();
    require(!s.renderedImage().isNull() && !s.renderedLayerThumbnails({32,32}).value(group).isNull(), "Group composition failed.");
    require(s.selectLayer(layer) && s.importRasterImages({decoded.images.front()},QPointF(8,11)), "Transformed-group import failed.");
    const auto imported=layerById(s,s.selectedLayerId()).operations.front().raster;
    require(placement(s,imported.id).operation.raster.transform.map(QPointF(8,8))==QPointF(8,11),
        "Transformed group changed the drop center.");
    const auto before_delete=s.renderedImage();
    require(s.deleteObjects({imported.id}) && !s.findRaster(imported.id,nullptr), "Raster object deletion failed.");
    require(s.undo() && s.renderedImage()==before_delete && s.redo() && !s.findRaster(imported.id,nullptr),
        "Raster deletion undo/redo failed.");
    ImageDocumentSession limits; require(limits.createCanvas({4,4},Qt::transparent), "Limit fixture failed.");
    QVector<PreparedRasterImage> batch;
    for (int i=0;i<510;++i) batch.append(decoded.images.front());
    require(limits.importRasterImages(batch), "Allowed layer count rejected.");
    require(!limits.importRasterImages({decoded.images.front()}), "Layer limit exceeded.");
}
void persistenceTests(const QString& root) {
    QString error;
    const QString source=imageFile(root,"linked.png",{16,16},QColor(90,30,200,180));
    ImageDocumentSession s; require(s.createCanvas({32,32},Qt::transparent), "Canvas failed.");
    require(s.importRasterImages(prepareRasterImport({source,source}).images), "Import failed.");
    const QString id=s.data().layers.back().operations.front().raster.id;
    const QString doc=root+"/raster.cimg";
    require(s.saveDocument(doc,&error),error);
    auto encoded=json(doc);
    require(encoded["version"].toInt()==11 &&
        encoded["layers"].toArray().last().toObject()["operations"].toArray().first().toObject()["path"].toString()=="linked.png",
        "v11/relative reference incorrect.");
    ImageDocumentSession reopened; require(reopened.openDocument(doc,&error),error);
    require(reopened.data()==s.data() && reopened.renderedImage()==s.renderedImage(), "v11 round trip failed.");
    require(QDir().mkpath(root+"/save-as"), "Save As fixture failed.");
    require(s.saveDocument(root+"/save-as/copy.cimg",&error),error);
    require(QDir::isAbsolutePath(json(root+"/save-as/copy.cimg")["layers"].toArray().last().toObject()["operations"].toArray().first().toObject()["path"].toString()),
        "Save As did not rebase paths.");
    require(s.setLayerOpacity(s.selectedLayerId(),70), "Recovery dirty fixture failed.");
    RecoveryStore recovery(root+"/recovery"); require(recovery.save(s,&error),error);
    const auto envelope=json(recovery.pathFor(s));
    require(envelope["version"].toInt()==1 && envelope["document"].toObject()["version"].toInt()==11, "Recovery version incorrect.");
    ImageDocumentSession restored; require(restored.restoreRecovery(recovery.pathFor(s),&error) &&
        restored.renderedImage()==s.renderedImage(),error);
    const auto frozen=s.exportSnapshot();
    const auto original_bytes=bytes(source);
    require(!s.exportImage(source,&error) && bytes(source)==original_bytes, "Export overwrote an imported source.");
    const QString png=root+"/export.png";
    require(s.exportImage(png,&error),error); const auto exported=bytes(png);
    require(QFile::remove(source), "Cannot remove fixture.");
    require(s.rasterSourceProblems().isEmpty() && s.exportImage(png,&error), "Loaded source did not remain cached.");
    require(reopened.openDocument(doc,&error) && reopened.rasterSourceProblems().size()==2,error);
    require(!reopened.exportImage(png,&error) && bytes(png)==exported, "Missing-source export replaced previous output.");
    require(reopened.saveDocument(doc,&error),error);
    ImageExportOptions quick; quick.scope=ImageExportScope::SelectedLayer;
    require(!reopened.exportImage(png,quick,&error) && bytes(png)==exported, "Missing Quick Export succeeded.");
    require(reopened.selectLayer(reopened.data().layers[1].id) && reopened.exportImage(png,quick,&error),error);
    const QString replacement=imageFile(root,"replacement.png",{16,16},Qt::blue);
    const auto prepared=prepareRasterImport({replacement});
    require(reopened.relinkRaster(id,prepared.images.front(),&error),error);
    require(reopened.rasterSourceProblems().size()==1, "Relink changed duplicate references.");
    require(reopened.undo() && reopened.rasterSourceProblems().size()==2 &&
        reopened.redo() && reopened.rasterSourceProblems().size()==1, "Relink history failed.");
    require(reopened.findRaster(id,nullptr) && !reopened.findRaster("none",nullptr), "Raster lookup incorrect.");
    require(!reopened.relinkRaster(id,PreparedRasterImage{replacement,QImage(3,3,QImage::Format_ARGB32)},&error), "Incompatible relink accepted.");
    const QString other_id=reopened.data().layers[2].operations.front().raster.id;
    require(reopened.relinkRaster(other_id,prepared.images.front(),&error) && reopened.exportImage(png,&error),error);
    require(exportImageSnapshot(frozen,root+"/snapshot.png").status==ImageExportStatus::Succeeded &&
        QImage(root+"/snapshot.png")==s.renderedImage(), "Export snapshot lost shared pixels.");
    // Same-path refresh changes only the chosen object and is undoable.
    imageFile(root,"replacement.png",{16,16},Qt::green);
    auto refreshed=prepareRasterImport({replacement});
    const auto before=reopened.renderedImage();
    require(reopened.relinkRaster(id,refreshed.images.front(),&error),error);
    require(reopened.renderedImage()!=before && reopened.undo() && reopened.renderedImage()==before, "Same-path relink undo failed.");
    imageFile(root,"linked.png",{4,4});
    ImageDocumentSession mismatch; require(mismatch.openDocument(doc,&error) &&
        mismatch.rasterSourceProblems().size()==2,error);
    write(source,"corrupt");
    require(mismatch.openDocument(doc,&error) && mismatch.rasterSourceProblems().size()==2,error);
    // Reject malformed raster data and forbidden operation locations.
    auto layers=encoded["layers"].toArray(); auto layer=layers.last().toObject();
    const auto original_op=layer["operations"].toArray().first().toObject();
    const auto rejects=[&](QJsonObject op) {
        auto bad=encoded; auto ls=layers; auto l=layer; l["operations"]=QJsonArray{op}; ls[ls.size()-1]=l; bad["layers"]=ls;
        write(root+"/invalid.cimg",QJsonDocument(bad).toJson()); ImageDocumentData data;
        require(!ImageDocumentStore::loadDocument(root+"/invalid.cimg",&data,&error), "Invalid raster JSON accepted.");
    };
    auto op=original_op; op["width"]=0; rejects(op);
    op=original_op; op["width"]=32769; rejects(op);
    op=original_op; op["id"]="invalid"; rejects(op);
    op=original_op; op["path"]=""; rejects(op);
    op=original_op; op["transform"]=QJsonArray{0,0,0,0,0,0}; rejects(op);
    op=original_op; op["transform"]=QJsonArray{1,0,0,1,"bad",0}; rejects(op);
    auto bad=encoded; auto ls=layers; auto l=layer;
    l["mask"]=QJsonObject{{"enabled",true},{"operations",QJsonArray{original_op}}}; ls[ls.size()-1]=l; bad["layers"]=ls;
    write(root+"/invalid.cimg",QJsonDocument(bad).toJson()); ImageDocumentData invalid;
    require(!ImageDocumentStore::loadDocument(root+"/invalid.cimg",&invalid,&error), "Raster accepted in mask.");
    bad=encoded; ls=layers; l=ls.first().toObject(); l["operations"]=QJsonArray{original_op}; ls[0]=l; bad["layers"]=ls;
    write(root+"/invalid.cimg",QJsonDocument(bad).toJson());
    require(!ImageDocumentStore::loadDocument(root+"/invalid.cimg",&invalid,&error), "Raster accepted in Background.");
    bad=encoded; bad["operations"]=QJsonArray{original_op};
    write(root+"/invalid.cimg",QJsonDocument(bad).toJson());
    require(!ImageDocumentStore::loadDocument(root+"/invalid.cimg",&invalid,&error), "Raster accepted in base operations.");
    bad=encoded; ls=layers;
    ls.append(QJsonObject{{"id","4f25f053-ecc2-4318-9d4e-fcc629fa1cf8"}, {"name","Invalid group"},
        {"kind","group"}, {"visible",true}, {"opacity",100},
        {"operations",QJsonArray{original_op}}, {"children",QJsonArray{}}});
    bad["layers"]=ls; write(root+"/invalid.cimg",QJsonDocument(bad).toJson());
    require(!ImageDocumentStore::loadDocument(root+"/invalid.cimg",&invalid,&error), "Raster accepted in group operations.");
    bad=encoded; bad["version"]=10;
    write(root+"/invalid.cimg",QJsonDocument(bad).toJson());
    require(!ImageDocumentStore::loadDocument(root+"/invalid.cimg",&invalid,&error), "v10 raster accepted.");
    ImageDocumentSession legacy; require(legacy.createCanvas({16,16},Qt::transparent) && legacy.saveDocument(root+"/legacy.cimg"), "Migration fixture failed.");
    auto v10=json(root+"/legacy.cimg"); v10["version"]=10; write(root+"/legacy.cimg",QJsonDocument(v10).toJson());
    require(legacy.openDocument(root+"/legacy.cimg") && legacy.saveDocument() &&
        json(root+"/legacy.cimg")["version"].toInt()==11, "v10 migration failed.");
}
}
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv); QTemporaryDir temp;
    const char* phase = "setup";
    try { require(temp.isValid(),"Temporary directory failed.");
        phase = "decode"; decodeTests(temp.path());
        phase = "geometry"; geometryTests(temp.path());
        phase = "persistence"; persistenceTests(temp.path());
    } catch (const std::exception& e) { std::cerr<<phase<<": "<<e.what()<<'\n'; return 1; }
    return 0;
}

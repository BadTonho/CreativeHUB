#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>

#include <iostream>

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const auto supported = QImageReader::supportedImageFormats();
    const QList<QByteArray> required = {
        QByteArray("png"), QByteArray("jpeg"), QByteArray("bmp"),
        QByteArray("webp"), QByteArray("tiff")};
    QStringList missing;
    for (const auto& format : required) {
        if (!supported.contains(format)) missing.append(QString::fromLatin1(format));
    }
    if (!missing.isEmpty()) {
        std::cerr << "Qt image format plugins are unavailable: "
                  << missing.join(QStringLiteral(", ")).toStdString() << '\n';
        return 77;
    }

    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    QImage source(3, 2, QImage::Format_ARGB32);
    source.fill(QColor(40, 100, 180, 210));
    for (const auto& format : required) {
        const QString path = QDir(temporary.path()).filePath(
            QStringLiteral("sample.%1").arg(QString::fromLatin1(format)));
        QImageWriter writer(path, format);
        if (!writer.write(source)) {
            std::cerr << "Qt could not encode test format: " << format.constData() << '\n';
            return 1;
        }
        QImageReader reader(path);
        if (reader.read().isNull()) {
            std::cerr << "Qt could not decode test format: " << format.constData() << '\n';
            return 1;
        }
    }
    return 0;
}

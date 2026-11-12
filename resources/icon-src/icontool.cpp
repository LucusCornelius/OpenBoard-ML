// icontool: the small helper make-icons.sh builds and uses.
//
//   icontool render <input.svg> <output.png> <size>
//       Renders the SVG to a <size> x <size> PNG with alpha (Qt's SVG renderer).
//   icontool ico <output.ico> <image.png>...
//       Writes a Windows .ico that holds each PNG unchanged as a 32-bit
//       BGRA bitmap entry with an AND mask (the classic format that every
//       Windows version, resource compiler and installer builder reads).
//       Images must be square and at most 256 px.

#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QtEndian>
#include <cstdio>
#include <vector>

static int render(const char *in, const char *out, const char *sizeArg)
{
    const int size = QByteArray(sizeArg).toInt();
    if (size <= 0) {
        std::fprintf(stderr, "icontool: invalid size %s\n", sizeArg);
        return 2;
    }
    QSvgRenderer renderer(QString::fromLocal8Bit(in));
    if (!renderer.isValid()) {
        std::fprintf(stderr, "icontool: cannot parse %s\n", in);
        return 1;
    }
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&painter, QRectF(0, 0, size, size));
    }
    if (!image.convertToFormat(QImage::Format_ARGB32).save(QString::fromLocal8Bit(out), "PNG")) {
        std::fprintf(stderr, "icontool: cannot write %s\n", out);
        return 1;
    }
    return 0;
}

static void put16(QByteArray &b, quint16 v) { char c[2]; qToLittleEndian(v, c); b.append(c, 2); }
static void put32(QByteArray &b, quint32 v) { char c[4]; qToLittleEndian(v, c); b.append(c, 4); }

static QByteArray bitmapEntry(const QImage &src)
{
    const QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();
    const int maskStride = ((w + 31) / 32) * 4;
    QByteArray b;
    // BITMAPINFOHEADER; the height counts the colour bitmap and the mask
    put32(b, 40); put32(b, w); put32(b, 2 * h);
    put16(b, 1); put16(b, 32);
    put32(b, 0); put32(b, w * h * 4 + maskStride * h);
    put32(b, 0); put32(b, 0); put32(b, 0); put32(b, 0);
    for (int y = h - 1; y >= 0; --y) {           // bottom-up BGRA
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const char px[4] = { char(qBlue(line[x])), char(qGreen(line[x])),
                                 char(qRed(line[x])), char(qAlpha(line[x])) };
            b.append(px, 4);
        }
    }
    for (int y = h - 1; y >= 0; --y) {           // AND mask: 1 = transparent
        QByteArray row(maskStride, '\0');
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; ++x)
            if (qAlpha(line[x]) == 0)
                row[x / 8] = char(row[x / 8] | (0x80 >> (x % 8)));
        b.append(row);
    }
    return b;
}

static int ico(int count, char **files, const char *out)
{
    std::vector<QImage> images;
    for (int i = 0; i < count; ++i) {
        QImage img(QString::fromLocal8Bit(files[i]));
        if (img.isNull() || img.width() != img.height() || img.width() > 256) {
            std::fprintf(stderr, "icontool: %s is not a square image of at most 256 px\n", files[i]);
            return 1;
        }
        images.push_back(img);
    }
    QByteArray header, data;
    put16(header, 0); put16(header, 1); put16(header, quint16(images.size()));
    quint32 offset = 6 + 16 * quint32(images.size());
    for (const QImage &img : images) {
        const QByteArray entry = bitmapEntry(img);
        const char dim = char(img.width() == 256 ? 0 : img.width());
        header.append(dim); header.append(dim);
        header.append('\0'); header.append('\0');  // no palette, reserved
        put16(header, 1); put16(header, 32);       // planes, bits per pixel
        put32(header, quint32(entry.size())); put32(header, offset);
        offset += quint32(entry.size());
        data.append(entry);
    }
    QFile f(QString::fromLocal8Bit(out));
    if (!f.open(QIODevice::WriteOnly) || f.write(header + data) != header.size() + data.size()) {
        std::fprintf(stderr, "icontool: cannot write %s\n", out);
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc == 5 && QByteArray(argv[1]) == "render")
        return render(argv[2], argv[3], argv[4]);
    if (argc >= 4 && QByteArray(argv[1]) == "ico")
        return ico(argc - 3, argv + 3, argv[2]);
    std::fprintf(stderr, "usage: icontool render <input.svg> <output.png> <size>\n"
                         "       icontool ico <output.ico> <image.png>...\n");
    return 2;
}

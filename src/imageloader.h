#ifndef IMAGELOADER_H
#define IMAGELOADER_H

#include <QImage>
#include <QString>

// Load image files for the USB gallery without calling Qt's JPEG plugin
// (avoids libjpeg 8 vs 9 clash with MuPDF's bundled libjpeg).
QImage loadImageFile(const QString &filePath);

#endif // IMAGELOADER_H

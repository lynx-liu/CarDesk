#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include "mainwindow.h"
#include "devicedetect.h"

static void preloadSystemFonts() {
    const QString bundledFontDir = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../fonts"));
    if (QFileInfo(bundledFontDir).isDir()) {
        QDirIterator bundledIt(bundledFontDir,
                               QStringList() << QStringLiteral("*.ttf") << QStringLiteral("*.ttc") << QStringLiteral("*.otf"),
                               QDir::Files,
                               QDirIterator::NoIteratorFlags);
        while (bundledIt.hasNext()) {
            const QString filePath = bundledIt.next();
            const int fontId = QFontDatabase::addApplicationFont(filePath);
            if (fontId >= 0) {
                qDebug() << "Loaded bundled font file:" << filePath
                         << "families:" << QFontDatabase::applicationFontFamilies(fontId);
            }
        }
    }

    const QStringList fontFiles = {
        QStringLiteral("/usr/share/fonts/truetype/wqy/wqy-microhei.ttc"),
        QStringLiteral("/usr/share/fonts/truetype/arphic/ukai.ttc"),
        QStringLiteral("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"),
        QStringLiteral("/usr/share/fonts/opentype/noto/NotoSansCJKSC-Regular.otf"),
        QStringLiteral("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
        QStringLiteral("/usr/share/fonts/ttf-dejavu/DejaVuSans.ttf"),
        QStringLiteral("/usr/share/fonts/DroidSansFallback.ttf")
    };

    for (const QString &fontFile : fontFiles) {
        if (QFileInfo::exists(fontFile)) {
            const int fontId = QFontDatabase::addApplicationFont(fontFile);
            if (fontId >= 0) {
                qDebug() << "Loaded font file:" << fontFile
                         << "families:" << QFontDatabase::applicationFontFamilies(fontId);
            }
        }
    }

    if (!QFontDatabase().families().isEmpty()) {
        return;
    }

    const QStringList fontDirs = {
        QStringLiteral("/usr/share/fonts"),
        QStringLiteral("/usr/local/share/fonts")
    };

    for (const QString &fontDir : fontDirs) {
        if (!QFileInfo(fontDir).isDir()) {
            continue;
        }
        QDirIterator it(fontDir,
                        QStringList() << QStringLiteral("*.ttf") << QStringLiteral("*.ttc") << QStringLiteral("*.otf"),
                        QDir::Files,
                        QDirIterator::Subdirectories);
        int loadedCount = 0;
        while (it.hasNext() && loadedCount < 8) {
            const QString filePath = it.next();
            const int fontId = QFontDatabase::addApplicationFont(filePath);
            if (fontId >= 0) {
                ++loadedCount;
                qDebug() << "Loaded fallback font file:" << filePath;
            }
        }
    }
}

static QString pickAvailableFontFamily() {
    const QFontDatabase fontDb;
    const QStringList families = fontDb.families();
    const QStringList preferredFamilies = {
        QStringLiteral("思源黑体"),
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("Source Han Sans SC"),
        QStringLiteral("WenQuanYi Micro Hei"),
        QStringLiteral("Droid Sans Fallback"),
        QStringLiteral("DejaVu Sans"),
        QStringLiteral("Sans Serif")
    };

    for (const QString &family : preferredFamilies) {
        if (families.contains(family)) {
            return family;
        }
    }

    return families.isEmpty() ? QString() : families.first();
}

static void configureApplicationFont(QApplication &app) {
    preloadSystemFonts();
    const QFontDatabase fontDb;
    qDebug() << "Available font family count:" << fontDb.families().size();

    const QString family = pickAvailableFontFamily();
    if (family.isEmpty()) {
        qWarning() << "No usable font family found. UI text may not render.";
        return;
    }

    QFont font(family);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);
    qDebug() << "Using application font:" << family;
}

int main(int argc, char *argv[]) {
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    configureApplicationFont(app);
    
    // 检测设备信息
    const DeviceDetect &device = DeviceDetect::instance();
    
    qDebug() << "========== CarDesk Application Started ==========";
    qDebug() << "Device Type:" << device.getDeviceTypeString();
    qDebug() << "Platform:" << device.getPlatform();
    qDebug() << "Architecture:" << device.getArchitecture();
    qDebug() << "Screen Resolution:" << device.getScreenWidth() 
             << "x" << device.getScreenHeight();
    qDebug() << "Touch Device:" << (device.isTouchDevice() ? "Yes" : "No");
    qDebug() << "==============================================";
    
    MainWindow window;
    window.show();
    
    return app.exec();
}

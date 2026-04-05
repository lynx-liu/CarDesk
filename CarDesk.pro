TEMPLATE = app
TARGET = CarDesk

QT += core gui widgets multimedia multimediawidgets

CONFIG += c++11

# 平台特定配置 (需要放在 DEFINES 之前)
# 通过命令行传入 CONFIG+=arm_build / CONFIG+=arm64_build 来触发 ARM 模式
contains(CONFIG, arm_build)|contains(CONFIG, arm64_build) {
    DEFINES += CAR_DESK_DEVICE_CARUNIT
    DEFINES += CAR_DESK_USE_T507_SDK
    # Ensure runtime linker searches the actual Qt location in the firmware image.
    QMAKE_RPATHDIR = /usr/local/Qt_5.12.5/lib
    message("Building for ARM (T507)")

    # ── T507 SDK 头文件路径 ──
    # 默认使用开发机上的 SDK，可通过环境变量 WSDK_T507_PATH 覆盖
    isEmpty(WSDK_T507_PATH): WSDK_T507_PATH = /data/sdk-work/WSDK-T507-Linux-Auto-V2.1/lichee/platform/framework/auto
    SDK_LIB = $$WSDK_T507_PATH/sdk_lib
    isEmpty(T507_SYSROOT): T507_SYSROOT = /data/sdk-work/WSDK-T507-Linux-Auto-V2.1/lichee/out/t507/my507/longan/buildroot/host/aarch64-buildroot-linux-gnu/sysroot

    INCLUDEPATH += \
        $$SDK_LIB/include \
        $$SDK_LIB/include/disp2 \
        $$SDK_LIB/include/utils \
        $$SDK_LIB/include/cutils \
        $$SDK_LIB/include/media \
        $$SDK_LIB/include/camera \
        $$SDK_LIB/cedarx/include \
        $$SDK_LIB/cedarx/include/libcedarc/include \
        $$SDK_LIB/cedarx/include/libcore/base/include \
        $$SDK_LIB/cedarx/include/libcore/common/include \
        $$SDK_LIB/cedarx/include/libcore/muxer/include \
        $$SDK_LIB/cedarx/include/libcore/parser/include \
        $$SDK_LIB/cedarx/include/libcore/playback/include \
        $$SDK_LIB/cedarx/include/libcore/stream/include \
        $$SDK_LIB/cedarx/include/xplayer/include \
        $$SDK_LIB/cedarx/include/external/include \
        $$SDK_LIB/cedarx/include/external/include/adecoder \
        $$SDK_LIB/cedarx/include/external/include/aencoder \
        $$SDK_LIB/sdk_camera \
        $$SDK_LIB/sdk_camera/moudle \
        $$SDK_LIB/include/storage \
        $$SDK_LIB/include/sound \
        $$SDK_LIB/include/memory \
        $$SDK_LIB/sdk_misc \
        $$SDK_LIB/include/audioenc

    # SDK 编译宏（对齐 sdktest Makefile）
    DEFINES += HAVE_PTHREADS HAVE_SYS_UIO_H HAVE_POSIX_CLOCKS HAVE_PRCTL
    DEFINES += WATERMARK CDX_V27 SUPPORT_NEW_DRIVER
    DEFINES += _GNU_SOURCE CONFIG_CHIP=7 CONFIG_PRODUCT=2
    DEFINES += VE_PHY_OFFSET=0x40000000 CONFIG_LOG_LEVEL=0

    LIBS += -L$$SDK_LIB/lib64
    LIBS += -L$$SDK_LIB/cedarx/lib
    LIBS += -L$$T507_SYSROOT/usr/lib/aarch64-linux-gnu
    LIBS += -L$$T507_SYSROOT/lib/aarch64-linux-gnu
    LIBS += -lsdk_camera -lsdk_g2d -lsdk_dvr -lsdk_player
    LIBS += -lsdk_log -lsdk_memory -lsdk_sound -lsdk_storage
    LIBS += -lsdk_audenc -lsdk_cfg -lsdk_ctrl -lsdk_egl -lsdk_misc -lsdk_compose
    LIBS += -ladecoder -laencoder -lcdx_base -lcdx_common -lcdx_muxer -lcdx_parser
    LIBS += -lcdx_playback -lcdx_stream -lMemAdapter -lcdc_base -lsubdecoder
    LIBS += -lvdecoder -lvencoder -lVE -lvideoengine -lxmetadataretriever -lxplayer
    LIBS += -lvenc_base -lvenc_codec -lcdx_ion -lasound -ldbus-1
    LIBS += -lrt -Wl,--no-as-needed -lsdk_disp -Wl,--as-needed -lpthread
} else {
    DEFINES += CAR_DESK_DEVICE_PC
    message("Building for x86/x64 (PC)")
}

# 编译输出目录
CONFIG(debug, debug|release) {
    DESTDIR = $$PWD/build/debug
    OBJECTS_DIR = $$PWD/build/debug/.obj
    MOC_DIR = $$PWD/build/debug/.moc
} else {
    contains(CONFIG, arm64_build) {
        DESTDIR = $$PWD/build/arm64
        OBJECTS_DIR = $$PWD/build/arm64/.obj
        MOC_DIR = $$PWD/build/arm64/.moc
    } else: contains(CONFIG, pc_build) {
        DESTDIR = $$PWD/build/pc
        OBJECTS_DIR = $$PWD/build/pc/.obj
        MOC_DIR = $$PWD/build/pc/.moc
    } else: contains(CONFIG, arm_build) {
        DESTDIR = $$PWD/build/arm
        OBJECTS_DIR = $$PWD/build/arm/.obj
        MOC_DIR = $$PWD/build/arm/.moc
    } else {
        DESTDIR = $$PWD/build/release
        OBJECTS_DIR = $$PWD/build/release/.obj
        MOC_DIR = $$PWD/build/release/.moc
    }
}

# 源文件和头文件
HEADERS += \
    src/mainwindow.h \
    src/devicedetect.h \
    src/bluetoothmanager.h \
    src/mediamanager.h \
    src/usbmanager.h \
    src/videolistwindow.h \
    src/videoplaywindow.h \
    src/musicplayerwindow.h \
    src/radiowindow.h \
    src/phonewindow.h \
    src/diagnosticwindow.h \
    src/systemsettingwindow.h \
    src/drivingimagewindow.h \
    src/imageviewingwindow.h \
    src/t507sdkbridge.h \
    src/ahdmanager.h \
    src/otamanager.h \
    src/progressmonitor.h \
    src/appsignals.h \
    src/topbarwidget.h

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/devicedetect.cpp \
    src/bluetoothmanager.cpp \
    src/mediamanager.cpp \
    src/usbmanager.cpp \
    src/videolistwindow.cpp \
    src/videoplaywindow.cpp \
    src/musicplayerwindow.cpp \
    src/radiowindow.cpp \
    src/phonewindow.cpp \
    src/diagnosticwindow.cpp \
    src/systemsettingwindow.cpp \
    src/drivingimagewindow.cpp \
    src/imageviewingwindow.cpp \
    src/t507sdkbridge.cpp \
    src/ahdmanager.cpp \
    src/otamanager.cpp \
    src/progressmonitor.cpp \
    src/appsignals.cpp \
    src/topbarwidget.cpp

# 资源文件（如果存在）
exists(resources.qrc) {
    RESOURCES += resources.qrc
}

# 包含路径
INCLUDEPATH += $$PWD/src

# Linux DBus 支持
unix {
    !contains(CONFIG, arm_build):!contains(CONFIG, arm64_build) {
        CONFIG += link_pkgconfig
        PKGCONFIG += dbus-1
    }
}

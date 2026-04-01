#ifndef AHDMANAGER_H
#define AHDMANAGER_H

#include <QObject>
#include <QString>

// AhdManager – T507 SDK AHD 摄像头预览管理器（SDK 头文件完全隔离在 ahdmanager.cpp 中）
//
// 在 CAR_DESK_USE_T507_SDK 构建下，直接调用 dvr_factory API：
//   start() → startPriview(view_info{x,y,w,h}) → stopPriview() → stop()
//
// 在 PC 构建下，所有方法均为安全的 stub（无实际操作）。
//
// 摄像头 ID 说明（与 T507 SDK 对应）：
//   2..5 → /dev/video2-5，单路 AHD 通道 1-4
//   360  → 4-in-1 四路合一，SDK 内部渲染 2x2 分屏
//
// 使用流程：
//   AhdManager::globalInit();           // 应用启动时调用一次
//   AhdManager mgr(360);               // 默认 4-in-1
//   mgr.startCamera();                  // 初始化硬件
//   mgr.startPreview(x, y, w, h);      // 以物理屏幕坐标启动预览
//   mgr.stopPreview(); mgr.stopCamera();
//   AhdManager::globalCleanup();       // 应用退出时调用

class AhdManager : public QObject {
    Q_OBJECT
public:
    // 应用生命周期内调用一次
    static void globalInit();
    static void globalCleanup();

    // cameraId: 2/3/4/5 = 单路; 360 = 4-in-1 四分屏
    explicit AhdManager(int cameraId, QObject *parent = nullptr);
    ~AhdManager() override;

    // 动态切换通道（会自动停止当前预览/摄像头）
    void setCameraId(int cameraId);
    int  cameraId() const;

    // 初始化并启动摄像头硬件 (dvr_factory::start)
    bool startCamera();
    // 停止并释放摄像头硬件
    void stopCamera();

    // 在物理屏幕坐标 (x, y, w, h) 处启动 AHD 硬件显示图层预览
    // 坐标由 Qt widget->mapToGlobal() 获取后传入
    bool startPreview(int x, int y, int w, int h);
    // 360 模式下指定预览某一路: -1=四分屏合成, 0..3=单路直出
    void setPreviewCameraIndex(int previewCameraIndex);
    // 停止 AHD 显示图层预览
    void stopPreview();

    bool isCameraReady()   const;
    bool isPreviewActive() const;

    // 将安全提示文字通过 SDK 水印嵌入视频画面
    // 水印始终跟随视频帧，不受 Qt/硬件图层叠加顺序影响
    //
    // ⚠️  SDK bitmap 水印仅支持以下字符：
    //     数字 0-9、大写字母 A-Z、空格、- : . /
    //     以及星期汉字（星期日一二三四五六）
    //   传入其他汉字会被渲染为空格（不可见）
    void enableSafetyWatermark(const QString &text = QStringLiteral("CAUTION"));
    void clearWatermark();

signals:
    void cameraError(const QString &msg);
    void previewStarted();
    void previewStopped();

private:
    struct Private;
    Private *d;
    int      m_cameraId;
};

#endif // AHDMANAGER_H

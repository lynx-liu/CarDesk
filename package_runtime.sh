#!/bin/bash

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DEFAULT_ARM64="$PROJECT_DIR/build/arm64/CarDesk"
BIN_DEFAULT_RELEASE="$PROJECT_DIR/build/release/CarDesk"
BIN_DEFAULT_PC="$PROJECT_DIR/build/pc/CarDesk"
RELEASE_DIR="$PROJECT_DIR/release"
OUT_DEFAULT="$RELEASE_DIR/CarDesk_bundle_t507"
ARCHIVE_DEFAULT="$RELEASE_DIR/CarDesk_bundle_t507_linux_arm.tar.gz"

BIN_PATH="${1:-}"
if [[ -z "$BIN_PATH" ]]; then
    if [[ -f "$BIN_DEFAULT_ARM64" ]]; then
        BIN_PATH="$BIN_DEFAULT_ARM64"
    elif [[ -f "$BIN_DEFAULT_RELEASE" ]]; then
        BIN_PATH="$BIN_DEFAULT_RELEASE"
    elif [[ -f "$BIN_DEFAULT_PC" ]]; then
        BIN_PATH="$BIN_DEFAULT_PC"
    else
        BIN_PATH="$BIN_DEFAULT_ARM64"
    fi
fi

OUT_DIR="${2:-$OUT_DEFAULT}"
ARCHIVE_PATH="${ARCHIVE_PATH:-$ARCHIVE_DEFAULT}"

if [[ ! -f "$BIN_PATH" ]]; then
    echo "[ERROR] 可执行文件不存在: $BIN_PATH"
    echo "用法: $0 [binary_path] [output_dir]"
    exit 1
fi

APP_NAME="$(basename "$BIN_PATH")"
APP_DIR="$OUT_DIR"

echo "[INFO] 输出目录: $OUT_DIR"
echo "[INFO] 使用系统自带 so/Qt 插件（不打包 lib/plugins）"
echo "[INFO] 使用系统字体文件（不打包字体，已集成到SDK）"
echo "[INFO] 使用系统 /etc/ota.sh（不打包 ota.sh）"

rm -rf "$OUT_DIR"
mkdir -p "$APP_DIR"

echo "[INFO] 复制主程序"
cp -L "$BIN_PATH" "$APP_DIR/$APP_NAME"
chmod +x "$APP_DIR/$APP_NAME"

# 系统插件模式，不写 qt.conf
rm -f "$APP_DIR/qt.conf"

echo "[INFO] 生成启动脚本"
cat > "$OUT_DIR/run.sh" << EOF
#!/bin/bash
set -e
SCRIPT_DIR="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"

if [[ -z "\${XDG_RUNTIME_DIR:-}" ]]; then
    export XDG_RUNTIME_DIR="/tmp/runtime-\$(id -u)"
    mkdir -p "\$XDG_RUNTIME_DIR"
    chmod 700 "\$XDG_RUNTIME_DIR" || true
fi

QT_SYS_LIB_DIR=""
QT_SYS_PLUGIN_DIR=""
QT_SYS_QML_DIR=""
for qt_prefix in /usr/local/Qt_5.12.5 /usr/local/Qt-5.12.5; do
    if [[ -d "\$qt_prefix/lib" ]]; then
        QT_SYS_LIB_DIR="\$qt_prefix/lib"
    fi
    if [[ -d "\$qt_prefix/plugins" ]]; then
        QT_SYS_PLUGIN_DIR="\$qt_prefix/plugins"
    fi
    if [[ -d "\$qt_prefix/qml" ]]; then
        QT_SYS_QML_DIR="\$qt_prefix/qml"
    fi
done

if [[ -n "\$QT_SYS_LIB_DIR" ]]; then
    export LD_LIBRARY_PATH="\$QT_SYS_LIB_DIR:\${LD_LIBRARY_PATH:-}"
fi

if [[ -n "\$QT_SYS_PLUGIN_DIR" ]]; then
    export QT_PLUGIN_PATH="\$QT_SYS_PLUGIN_DIR"
    export QT_QPA_PLATFORM_PLUGIN_PATH="\$QT_SYS_PLUGIN_DIR/platforms"
fi

if [[ -n "\$QT_SYS_QML_DIR" ]]; then
    export QML2_IMPORT_PATH="\$QT_SYS_QML_DIR:\${QML2_IMPORT_PATH:-}"
fi

if [[ -f "\${QT_SYS_PLUGIN_DIR:-}/egldeviceintegrations/libqeglfs-mali-integration.so" ]]; then
    export QT_QPA_EGLFS_INTEGRATION="\${QT_QPA_EGLFS_INTEGRATION:-eglfs_mali}"
fi
if [[ -f "\${QT_SYS_PLUGIN_DIR:-}/platforms/libqeglfs.so" ]]; then
    export QT_QPA_PLATFORM="\${QT_QPA_PLATFORM:-eglfs}"
elif [[ -f "\${QT_SYS_PLUGIN_DIR:-}/platforms/libqlinuxfb.so" ]]; then
    export QT_QPA_PLATFORM="\${QT_QPA_PLATFORM:-linuxfb}"
fi

exec "\$SCRIPT_DIR/$APP_NAME" "\$@"
EOF
chmod +x "$OUT_DIR/run.sh"

echo "[INFO] 打包目录已生成: $OUT_DIR"
echo "[INFO] 启动方式: $OUT_DIR/run.sh"

echo "[INFO] 生成压缩包: $ARCHIVE_PATH"
mkdir -p "$(dirname "$ARCHIVE_PATH")"
tar -czf "$ARCHIVE_PATH" -C "$(dirname "$OUT_DIR")" "$(basename "$OUT_DIR")"
echo "[INFO] 压缩包已生成: $ARCHIVE_PATH"

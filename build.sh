#!/bin/bash
# 跨平台编译脚本 - 支持 PC Ubuntu 和 T507 Ubuntu

set -eo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
INSTALL_DIR="$PROJECT_DIR/install"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# 解析 SDK 路径与工具链（优先使用 WSDK-T507 的 buildroot host 工具链）
resolve_sdk_env() {
    local sdk_auto="/data/sdk-work/WSDK-T507-Linux-Auto-V2.1/lichee/out/t507/my507/longan/buildroot/host/bin"
    local sdk_qmake="/data/sdk-work/WSDK-T507-Linux-Auto-V2.1/lichee/platform/framework/qt/qt-everywhere-src-5.12.5/Qt_5.12.5/bin/qmake"
    local sdk_bin="${SDK_TOOLCHAIN_BIN:-$sdk_auto}"
    local sdk_gxx=""

    if [[ -x "$sdk_bin/aarch64-buildroot-linux-gnu-g++" ]]; then
        sdk_gxx="$sdk_bin/aarch64-buildroot-linux-gnu-g++"
    elif [[ -x "$sdk_bin/aarch64-linux-gnu-g++" ]]; then
        sdk_gxx="$sdk_bin/aarch64-linux-gnu-g++"
    fi

    if [[ -z "${ARM_TOOLCHAIN:-}" && -n "$sdk_gxx" ]]; then
        ARM_TOOLCHAIN="$sdk_bin"
        export ARM_TOOLCHAIN
        print_info "Using SDK toolchain: $ARM_TOOLCHAIN ($(basename "$sdk_gxx"))"
    fi

    if [[ -n "${ARM_TOOLCHAIN:-}" ]]; then
        export PATH="$ARM_TOOLCHAIN:$PATH"
        print_info "Prepended SDK toolchain to PATH"
    fi

    if [[ -z "${QT_ARM_QMAKE:-}" ]]; then
        # 优先使用 SDK 自带 qmake（与 SDK sysroot/toolchain 版本匹配）
        if [[ -x "$sdk_qmake" ]]; then
            QT_ARM_QMAKE="$sdk_qmake"
            export QT_ARM_QMAKE
            print_info "Using SDK qmake: $QT_ARM_QMAKE"
        # 兼容保底：使用本机包装器
        elif [[ -x "/usr/local/bin/aarch64-linux-gnu-qmake" ]]; then
            QT_ARM_QMAKE="/usr/local/bin/aarch64-linux-gnu-qmake"
            export QT_ARM_QMAKE
            print_warning "Fallback to wrapper qmake: $QT_ARM_QMAKE"
        fi
    fi
}

# 检查前置依赖
check_dependencies() {
    print_info "Checking dependencies..."
    
    # 检查 Qt
    if ! command -v qmake &> /dev/null; then
        print_error "Qt is not installed. Please install Qt development libraries."
        echo "On Ubuntu: sudo apt-get install qt5-qmake qt5-default qtbase5-dev"
        exit 1
    fi
    
    # 检查 g++
    if ! command -v g++ &> /dev/null; then
        print_error "g++ is not installed. Please install build-essential."
        echo "On Ubuntu: sudo apt-get install build-essential"
        exit 1
    fi

    # 检查 make
    if ! command -v make &> /dev/null; then
        print_error "make is not installed. Please install GNU Make."
        echo "On Ubuntu: sudo apt-get install make"
        exit 1
    fi
    
    print_info "All dependencies are available."
}

# 仅保留 make 错误输出：隐藏子仓库的警告与编译信息，但保留错误信息
filter_make_errors() {
    grep -E --line-buffered 'error:|undefined reference|fatal error|ld:|collect2:|Project ERROR:|make: \*\*\*|错误'
}

build_mupdf_host() {
    print_info "Building MuPDF for host (PC)..."
    cd "$PROJECT_DIR/thirdparty/mupdf"
    if git rev-parse --is-inside-work-tree > /dev/null 2>&1; then
        git checkout -- Makefile || true
    fi
    mkdir -p build/pc-release
    local _rc=0
    make -j$(nproc) build=release OUT=build/pc-release 2>&1 | filter_make_errors || true
    _rc=${PIPESTATUS[0]}
    [[ $_rc -eq 0 ]] || { print_error "MuPDF host build failed (exit $_rc)"; return $_rc; }
}

build_mupdf_arm() {
    print_info "Building MuPDF for ARM (T507)..."
    cd "$PROJECT_DIR/thirdparty/mupdf"
    if git rev-parse --is-inside-work-tree > /dev/null 2>&1; then
        git checkout -- Makefile || true
    fi
    mkdir -p build/arm64-release

    local cc=""
    local cxx=""
    local ld=""
    local ar=""
    local ranlib=""
    if [[ -n "${ARM_TOOLCHAIN:-}" ]]; then
        if [[ -x "$ARM_TOOLCHAIN/aarch64-buildroot-linux-gnu-gcc" ]]; then
            cc="$ARM_TOOLCHAIN/aarch64-buildroot-linux-gnu-gcc"
            cxx="$ARM_TOOLCHAIN/aarch64-buildroot-linux-gnu-g++"
            ld="$ARM_TOOLCHAIN/aarch64-buildroot-linux-gnu-ld"
            ar="$ARM_TOOLCHAIN/aarch64-buildroot-linux-gnu-ar"
            ranlib="$ARM_TOOLCHAIN/aarch64-buildroot-linux-gnu-ranlib"
        elif [[ -x "$ARM_TOOLCHAIN/aarch64-linux-gnu-gcc" ]]; then
            cc="$ARM_TOOLCHAIN/aarch64-linux-gnu-gcc"
            cxx="$ARM_TOOLCHAIN/aarch64-linux-gnu-g++"
            ld="$ARM_TOOLCHAIN/aarch64-linux-gnu-ld"
            ar="$ARM_TOOLCHAIN/aarch64-linux-gnu-ar"
            ranlib="$ARM_TOOLCHAIN/aarch64-linux-gnu-ranlib"
        fi
    fi

    if [[ -z "$cc" ]]; then
        if command -v aarch64-buildroot-linux-gnu-gcc &> /dev/null; then
            cc=$(command -v aarch64-buildroot-linux-gnu-gcc)
            cxx=$(command -v aarch64-buildroot-linux-gnu-g++ 2>/dev/null || true)
            ld=$(command -v aarch64-buildroot-linux-gnu-ld 2>/dev/null || true)
            ar=$(command -v aarch64-buildroot-linux-gnu-ar 2>/dev/null || true)
            ranlib=$(command -v aarch64-buildroot-linux-gnu-ranlib 2>/dev/null || true)
        elif command -v aarch64-linux-gnu-gcc &> /dev/null; then
            cc=$(command -v aarch64-linux-gnu-gcc)
            cxx=$(command -v aarch64-linux-gnu-g++ 2>/dev/null || true)
            ld=$(command -v aarch64-linux-gnu-ld 2>/dev/null || true)
            ar=$(command -v aarch64-linux-gnu-ar 2>/dev/null || true)
            ranlib=$(command -v aarch64-linux-gnu-ranlib 2>/dev/null || true)
        fi
    fi

    if [[ -z "$cc" ]]; then
        print_error "ARM compiler not found. Please export ARM_TOOLCHAIN or install an aarch64 cross compiler."
        exit 1
    fi

    if [[ -z "$ar" ]]; then
        ar=$(dirname "$cc")/aarch64-buildroot-linux-gnu-ar
    fi
    if [[ -z "$ranlib" ]]; then
        ranlib=$(dirname "$cc")/aarch64-buildroot-linux-gnu-ranlib
    fi
    if [[ -z "$cxx" ]]; then
        cxx=$(dirname "$cc")/aarch64-linux-gnu-g++
    fi
    if [[ -z "$ld" ]]; then
        ld=$(dirname "$cc")/aarch64-linux-gnu-ld
    fi

    local _rc=0
    make -j$(nproc) CC="$cc" CXX="$cxx" LD="$ld" AR="$ar" RANLIB="$ranlib" XCFLAGS="-fPIC" XCXXFLAGS="-fPIC" build=release OUT=build/arm64-release USE_SYSTEM_LIBS=no libs 2>&1 | filter_make_errors || true
    _rc=${PIPESTATUS[0]}
    [[ $_rc -eq 0 ]] || { print_error "MuPDF ARM build failed (exit $_rc)"; return $_rc; }
}

# 检测目标架构
detect_target_arch() {
    local arch=$(uname -m)
    
    case "$arch" in
        armv7l|armv8l|aarch64)
            echo "arm"
            ;;
        x86_64|i386|i686)
            echo "x86"
            ;;
        *)
            echo "unknown"
            ;;
    esac
}

# 清理构建
clean_build() {
    print_info "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
}

# 编译 PC 版本
build_pc() {
    print_info "Building PC version (x86/x64)..."
    build_mupdf_host
    
    mkdir -p "$BUILD_DIR/pc"
    cd "$BUILD_DIR/pc"
    
    qmake -config release CONFIG+=pc_build DEFINES+=CAR_DESK_DEVICE_PC "$PROJECT_DIR/CarDesk.pro"
    if [[ $? -ne 0 ]]; then
        print_error "qmake failed for PC build"
        return 1
    fi
    local _rc=0
    make -j$(nproc) 2>&1 | filter_make_errors || true
    _rc=${PIPESTATUS[0]}
    [[ $_rc -eq 0 ]] || { print_error "make failed (exit $_rc)"; return $_rc; }
    
    print_info "PC build completed successfully!"
    echo "Binary location: $PROJECT_DIR/build/pc/CarDesk"
}

# 编译 T507 版本（ARM）
build_t507() {
    print_info "Building T507 SDK version (ARM)..."

    resolve_sdk_env

    local arm_qmake="${QT_ARM_QMAKE:-${QMAKE_BIN:-qmake}}"
    local host_arch
    host_arch="$(uname -m)"
    
    # 检查交叉编译工具链
    if [ ! -z "${ARM_TOOLCHAIN:-}" ]; then
        print_info "Using ARM toolchain: $ARM_TOOLCHAIN"
    else
        print_warning "ARM_TOOLCHAIN not set, using native compilation"
        print_warning "For cross-compilation, set ARM_TOOLCHAIN environment variable"
    fi

    if [[ "$host_arch" != aarch64 && "$host_arch" != arm* && -z "${QT_ARM_QMAKE:-}" ]]; then
        print_error "T507 SDK build on non-ARM host requires QT_ARM_QMAKE"
        echo "Example: QT_ARM_QMAKE=/opt/qt5-arm64/bin/qmake ./build.sh t507"
        exit 1
    fi
    
    # 避免 x86/ARM 目标文件混用导致链接错误（EM:62 等）
    rm -rf "$BUILD_DIR/arm64/.obj" "$BUILD_DIR/arm64/.moc"
    # 重建编译产物目录：qmake 生成的 Makefile 不会自动创建 OBJECTS_DIR/MOC_DIR，
    # 删除后若不重建，编译会写不进 .o 而最终在链接阶段报“.o 不存在”。
    mkdir -p "$BUILD_DIR/arm64/.obj" "$BUILD_DIR/arm64/.moc"
    cd "$BUILD_DIR/arm64"
    
    if ! command -v "$arm_qmake" &> /dev/null; then
        print_error "ARM qmake not found: $arm_qmake"
        echo "Please set QT_ARM_QMAKE to your ARM Qt qmake path"
        echo "Example: QT_ARM_QMAKE=/usr/local/bin/aarch64-linux-gnu-qmake ./build.sh t507"
        exit 1
    fi

    print_info "Building MuPDF for ARM before T507 build..."
    build_mupdf_arm

    print_info "Using qmake: $arm_qmake"
    "$arm_qmake" -config release CONFIG+=arm64_build DEFINES+="CAR_DESK_DEVICE_CARUNIT CAR_DESK_USE_T507_SDK" "$PROJECT_DIR/CarDesk.pro"
    if [[ $? -ne 0 ]]; then
        print_error "qmake failed for T507 build"
        return 1
    fi
    local _rc=0
    set -o pipefail
    make -j$(nproc) 2>&1 | filter_make_errors
    _rc=${PIPESTATUS[0]}
    set +o pipefail
    [[ $_rc -eq 0 ]] || { print_error "make failed (exit $_rc)"; return $_rc; }

    local arm_bin="$PROJECT_DIR/build/arm64/CarDesk"
    if [[ ! -f "$arm_bin" ]]; then
        print_error "T507 binary not found: $arm_bin"
        exit 1
    fi

    local file_out
    file_out="$(file "$arm_bin" 2>/dev/null || true)"
    if ! echo "$file_out" | grep -qiE 'arm|aarch64'; then
        print_error "T507 build output is not ARM binary"
        echo "$file_out"
        echo "Please use ARM Qt qmake (QT_ARM_QMAKE) and correct sysroot"
        exit 1
    fi
    
    print_info "T507 SDK build completed successfully!"
    echo "Binary location: $arm_bin"
}

# 显示帮助信息
show_help() {
    cat << EOF
CarDesk Build Script - Cross-platform Car Unit / PC Application

Usage: $0 [COMMAND] [OPTIONS]

Commands:
    all              Build for all platforms (default)
    pc               Build PC version (x86/x64)
    t507             Build T507 version (ARM, SDK bindings)
    clean            Clean build directory
    rebuild          Clean and build all
    check            Check dependencies only
    help             Show this help message

Environment Variables:
    ARM_TOOLCHAIN    Path to ARM cross-compiler (for T507 cross-compilation)
    QT_PATH          Path to Qt installation (if non-standard)

Examples:
    # Build for current platform
    ./build.sh

    # Build for specific platform
    ./build.sh pc      # PC version
    ./build.sh t507    # T507 version

    # Cross-compile for ARM on x86 machine
    ARM_TOOLCHAIN=/path/to/arm-toolchain ./build.sh t507

EOF
}

# 主函数
main() {
    local cmd="${1:-all}"
    
    case "$cmd" in
        all)
            check_dependencies
            clean_build
            build_pc
            build_t507
            print_info "All builds completed successfully!"
            ;;
        pc)
            check_dependencies
            mkdir -p "$BUILD_DIR"
            build_pc
            ;;
        t507)
            check_dependencies
            mkdir -p "$BUILD_DIR"
            build_t507
            ;;

        clean)
            clean_build
            print_info "Build directory cleaned."
            ;;
        rebuild)
            clean_build
            check_dependencies
            build_pc
            build_t507
            print_info "All builds completed successfully!"
            ;;
        check)
            check_dependencies
            local arch=$(detect_target_arch)
            print_info "Current architecture: $arch"
            ;;
        help)
            show_help
            ;;
        *)
            print_error "Unknown command: $cmd"
            show_help
            exit 1
            ;;
    esac
}

main "$@"

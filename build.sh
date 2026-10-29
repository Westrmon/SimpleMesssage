#!/usr/bin/env bash
# SimpleMessage 构建脚本（Git Bash / Linux / macOS）
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/build"
CONFIG="Release"
JOBS=""
CLEAN=0
RUN_TESTS=0
BUILD_TESTS=1
BUILD_SAMPLES=1
WIN7_COMPAT=0

usage() {
  cat <<'EOF'
用法: ./build.sh [选项]

选项:
  -c, --config <名称>       构建配置，默认 Release
  -b, --build-dir <目录>    构建目录，默认 <仓库>/build
  -j, --jobs <数量>         并行任务数，默认交给 CMake 决定
      --clean               先删除已有的构建目录
      --test                构建后运行 ctest
      --no-tests            不构建单元测试
      --no-samples          不构建示例插件
      --win7-compat         开启 SM_WIN7_COMPAT
  -h, --help                显示本帮助
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -c|--config)
      [[ $# -ge 2 ]] || { echo "错误: $1 缺少参数。" >&2; exit 2; }
      CONFIG="$2"
      shift 2
      ;;
    -b|--build-dir)
      [[ $# -ge 2 ]] || { echo "错误: $1 缺少参数。" >&2; exit 2; }
      BUILD_DIR="$2"
      shift 2
      ;;
    -j|--jobs)
      [[ $# -ge 2 ]] || { echo "错误: $1 缺少参数。" >&2; exit 2; }
      JOBS="$2"
      shift 2
      ;;
    --clean)       CLEAN=1; shift ;;
    --test)        RUN_TESTS=1; shift ;;
    --no-tests)    BUILD_TESTS=0; shift ;;
    --no-samples)  BUILD_SAMPLES=0; shift ;;
    --win7-compat) WIN7_COMPAT=1; shift ;;
    -h|--help)     usage; exit 0 ;;
    *)             echo "错误: 未知参数 $1。" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ $RUN_TESTS -eq 1 && $BUILD_TESTS -eq 0 ]]; then
  echo "错误: --test 与 --no-tests 不能同时使用。" >&2
  exit 2
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "错误: 找不到 cmake，请先安装 CMake 3.20 或更高版本。" >&2
  exit 1
fi

case "$BUILD_DIR" in
  /*|[A-Za-z]:[\\/]*) ;;
  *) BUILD_DIR="$ROOT/$BUILD_DIR" ;;
esac

if [[ $CLEAN -eq 1 && -e "$BUILD_DIR" ]]; then
  if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    rm -rf -- "$BUILD_DIR"
  elif [[ -d "$BUILD_DIR" && -z "$(ls -A -- "$BUILD_DIR")" ]]; then
    rmdir -- "$BUILD_DIR"
  else
    echo "错误: $BUILD_DIR 不是 CMake 构建目录（缺少 CMakeCache.txt），拒绝删除。" >&2
    exit 1
  fi
fi

case "$(uname -s 2>/dev/null || echo unknown)" in
  MINGW*|MSYS*|CYGWIN*) IS_WINDOWS=1 ;;
  *)                    IS_WINDOWS=0 ;;
esac

CMAKE_CONFIGURE=(-S "$ROOT" -B "$BUILD_DIR")
if [[ $IS_WINDOWS -eq 0 ]]; then
  CMAKE_CONFIGURE+=(-DCMAKE_BUILD_TYPE="$CONFIG")
fi
if [[ $BUILD_TESTS -eq 1 ]]; then
  CMAKE_CONFIGURE+=(-DSM_BUILD_TESTS=ON)
else
  CMAKE_CONFIGURE+=(-DSM_BUILD_TESTS=OFF)
fi
if [[ $BUILD_SAMPLES -eq 1 ]]; then
  CMAKE_CONFIGURE+=(-DSM_BUILD_SAMPLES=ON)
else
  CMAKE_CONFIGURE+=(-DSM_BUILD_SAMPLES=OFF)
fi
if [[ $WIN7_COMPAT -eq 1 ]]; then
  CMAKE_CONFIGURE+=(-DSM_WIN7_COMPAT=ON)
fi

echo "[build] 配置: $CONFIG"
echo "[build] 目录: $BUILD_DIR"
cmake "${CMAKE_CONFIGURE[@]}"

echo "[build] 编译 (配置 $CONFIG)"
if [[ -n "$JOBS" ]]; then
  cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel "$JOBS"
else
  cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel
fi

if [[ -d "$BUILD_DIR/$CONFIG" ]]; then
  ARTIFACT_DIR="$BUILD_DIR/$CONFIG"
else
  ARTIFACT_DIR="$BUILD_DIR"
fi
echo "[build] 产物目录: $ARTIFACT_DIR"

if [[ $RUN_TESTS -eq 1 ]]; then
  echo "[build] 运行测试"
  ctest --test-dir "$BUILD_DIR" -C "$CONFIG" --output-on-failure
fi

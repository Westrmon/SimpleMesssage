#!/bin/sh
# ============================================================================
# SimpleMessage — 撤销开机自启（Linux 桌面）
#
# 只删除自己的 desktop 文件。文件不存在时视为成功（幂等）。
# ============================================================================

set -eu

APP_NAME="simplemessage"
AUTOSTART_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/autostart"
DESKTOP_FILE="$AUTOSTART_DIR/$APP_NAME.desktop"

if [ -f "$DESKTOP_FILE" ]; then
    rm -f "$DESKTOP_FILE"
    echo "[完成] 已撤销开机自启：$DESKTOP_FILE"
else
    echo "[提示] 找不到 $DESKTOP_FILE，无需撤销。"
fi

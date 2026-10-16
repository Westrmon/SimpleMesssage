#!/bin/sh
# ============================================================================
# SimpleMessage — 注册开机自启（Linux 桌面）
#
# 【为什么用 .desktop 而不是 systemd user unit】
# 通知浮层需要一个可用的图形会话。systemd user service 默认不绑定到图形
# 会话环境（DISPLAY/WAYLAND_DISPLAY），要么额外配置 import-environment，
# 要么就会在登录后因为连不上显示服务器而静默失败。
# ~/.config/autostart/*.desktop 是 freedesktop 约定，GNOME/KDE/XFCE 都认，
# 且天然在图形会话就绪之后启动。
#
# 用法：
#   ./install-autostart.sh [可执行文件路径]
# 不带参数时，默认使用本脚本上两级目录下的 sm_host。
#
# 撤销请运行同目录下的 uninstall-autostart.sh（幂等，删文件而已）。
# ============================================================================

set -eu

APP_NAME="simplemessage"
EXE_PATH="${1:-$(dirname "$0")/../../sm_host}"

# 转成绝对路径。desktop 文件的 Exec 必须是绝对路径或 PATH 里的命令名，
# 相对路径不会被展开。
EXE_PATH=$(cd "$(dirname "$EXE_PATH")" 2>/dev/null && pwd)/$(basename "$EXE_PATH") || {
    echo "[错误] 找不到可执行文件：$1" >&2
    exit 1
}

if [ ! -x "$EXE_PATH" ]; then
    echo "[错误] 文件不存在或没有执行权限：$EXE_PATH" >&2
    echo "        请先 chmod +x，或把正确路径作为参数传进来。" >&2
    exit 1
fi

AUTOSTART_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/autostart"
DESKTOP_FILE="$AUTOSTART_DIR/$APP_NAME.desktop"

mkdir -p "$AUTOSTART_DIR"

# X-GNOME-Autostart-enabled 是 GNOME 认的开关；其它桌面环境会忽略未知键，
# 因此写上它对所有环境都无害。
cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=SimpleMessage
Comment=通知聚合与投递
Exec=$EXE_PATH
Terminal=false
NoDisplay=false
X-GNOME-Autostart-enabled=true
EOF

chmod 644 "$DESKTOP_FILE"

echo "[完成] 已注册开机自启："
echo "        desktop 文件：$DESKTOP_FILE"
echo "        可执行文件：  $EXE_PATH"
echo
echo "撤销请运行：$(dirname "$0")/uninstall-autostart.sh"

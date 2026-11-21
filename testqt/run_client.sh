#!/bin/bash
# 启动客户端并启用 fcitx5 中文输入法
#
# 背景：WSLg 的 Wayland 合成器不允许 fcitx5 绑定 input_method，
# 直接跑 fcitx5 会报 "permission to bind input_method denied" 然后退出。
# 这里禁用 fcitx5 的 wayland 插件，走 X11(xcb)，并给 Qt 客户端设置输入法环境变量。

export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
export GTK_IM_MODULE=fcitx
# WSLg 下强制走 xcb，fcitx5 的 xim/xcb 才能接管输入
export QT_QPA_PLATFORM=xcb

if ! pgrep -x fcitx5 > /dev/null 2>&1; then
    echo "starting fcitx5..."
    DISPLAY=:0 setsid fcitx5 -d --replace --disable=wayland,waylandim \
        > /tmp/fcitx5.log 2>&1 < /dev/null
    sleep 1
fi

cd "$(dirname "$0")" || exit 1
exec ./bin/qingxun "$@"

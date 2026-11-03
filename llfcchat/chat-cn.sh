#!/bin/bash
# 中文输入启动器（系统级 fcitx5）：设好输入法环境后启动 llfcchat
export GTK_IM_MODULE=fcitx QT_IM_MODULE=fcitx XMODIFIERS=@im=fcitx
export DISPLAY=${DISPLAY:-:0}
unset WAYLAND_DISPLAY   # Wayland 会话下 fcitx5 绑定输入法协议会被拒，禁用之

if ! pgrep -x fcitx5 >/dev/null; then
  [ -z "$DBUS_SESSION_BUS_ADDRESS" ] && eval $(dbus-launch --sh-syntax)
  fcitx5 -d --replace >/dev/null 2>&1 &
  sleep 1
fi
exec /home/xulei/cpp/llfcchat/bin/llfcchat

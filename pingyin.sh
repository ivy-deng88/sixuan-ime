#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
用法：
  pingyin.sh fcitx5   切换到 Fcitx 5（下次登录生效）
  pingyin.sh ibus     切换到 IBus / 思玄输入法（下次登录生效）
  pingyin.sh sixuan   ibus 的别名
  pingyin.sh status   查看当前配置状态

脚本会立即停止另一套输入法框架，避免 IBus 与 Fcitx 5 同时接管键盘。
由于 GTK_IM_MODULE、QT_IM_MODULE 和 XMODIFIERS 在登录时设置，执行后请注销并重新登录。
EOF
}

fail() {
  printf '错误：%s\n' "$*" >&2
  exit 1
}

have() {
  command -v "$1" >/dev/null 2>&1
}

stop_fcitx5() {
  if have fcitx5-remote && pgrep -x fcitx5 >/dev/null 2>&1; then
    fcitx5-remote -e >/dev/null 2>&1 || true
  fi
}

stop_ibus() {
  if have ibus && pgrep -x ibus-daemon >/dev/null 2>&1; then
    ibus exit >/dev/null 2>&1 || true
  fi
}

show_status() {
  printf '%s\n' 'im-config：'
  if have im-config; then
    im-config -m || true
  else
    printf '%s\n' '  未安装 im-config'
  fi

  printf '%s\n' '用户服务：'
  if have systemctl; then
    for unit in fcitx5.service sixuan-ime.service; do
      state=$(systemctl --user is-active "$unit" 2>/dev/null || true)
      enabled=$(systemctl --user is-enabled "$unit" 2>/dev/null || true)
      printf '  %-22s active=%-10s enabled=%s\n' "$unit" "${state:-unknown}" "${enabled:-unknown}"
    done
  fi

  printf '%s\n' '运行中的输入法进程：'
  pgrep -a -x fcitx5 2>/dev/null || true
  pgrep -a -x ibus-daemon 2>/dev/null || true
}

main() {
  local target=${1:-}
  case "$target" in
    -h|--help|help)
      usage
      return 0
      ;;
    status)
      show_status
      return 0
      ;;
    fcitx5|fcitx)
      target=fcitx5
      ;;
    ibus|sixuan|pinyin)
      target=ibus
      ;;
    '')
      usage >&2
      return 2
      ;;
    *)
      fail "未知目标：$target（可用目标：fcitx5、ibus）"
      ;;
  esac

  have im-config || fail '未安装 im-config，请先执行：sudo apt install im-config'
  have systemctl || fail '未找到 systemctl'

  if [[ "$target" == fcitx5 ]]; then
    # 思玄服务依赖真正的 IBus 引擎宿主；Fcitx 模式下必须停用。
    systemctl --user disable --now sixuan-ime.service >/dev/null 2>&1 || true
    im-config -n fcitx5
    stop_ibus
    printf '%s\n' '已配置为 Fcitx 5。请注销并重新登录以完整切换输入法环境。'
    return 0
  fi

  # IBus / 思玄模式：让六元输入法服务在下次登录时自动启动。
  systemctl --user disable --now sixuan-ime.service >/dev/null 2>&1 || true
  if ! systemctl --user enable sixuan-ime.service >/dev/null 2>&1; then
    fail '未找到 sixuan-ime.service，请先重新安装 sixuan-ime 软件包'
  fi
  im-config -n ibus
  stop_fcitx5

  if have gsettings; then
    sources=$(gsettings get org.gnome.desktop.input-sources sources 2>/dev/null || true)
    if [[ "$sources" != *sixuan-pinyin* ]]; then
      printf '%s\n' '提示：GNOME 输入源中未找到“思玄拼音”，登录后请运行：sixuan-ime setup'
    fi
  fi

  printf '%s\n' '已配置为 IBus / 思玄输入法。请注销并重新登录以完整切换输入法环境。'
}

main "$@"

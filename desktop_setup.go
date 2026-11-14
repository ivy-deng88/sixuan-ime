package main

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"
)

// Keep the original GVariant text and append one tuple; never rebuild or remove sources.
func appendInputSource(value, engine string) (string, error) {
	value = strings.TrimSpace(strings.TrimPrefix(strings.TrimSpace(value), "@a(ss)"))
	tuple := `\(\s*('(?:[^'\\]|\\.)*'|"(?:[^"\\]|\\.)*")\s*,\s*('(?:[^'\\]|\\.)*'|"(?:[^"\\]|\\.)*")\s*\)`
	valid := regexp.MustCompile(`^\[\s*(?:` + tuple + `\s*(?:,\s*` + tuple + `\s*)*,?\s*)?\]$`)
	if !valid.MatchString(value) {
		return "", errors.New("输入源列表格式无效，未修改")
	}
	target := regexp.MustCompile(`\(\s*['"]ibus['"]\s*,\s*['"]` + regexp.QuoteMeta(engine) + `['"]\s*\)`)
	if target.MatchString(value) {
		return value, nil
	}
	body := strings.TrimSpace(value[1 : len(value)-1])
	body = strings.TrimSuffix(body, ",")
	if body != "" {
		body += ", "
	}
	return "[" + body + "('ibus', '" + engine + "')]", nil
}
func isGNOMEDesktop() bool {
	for _, desktop := range strings.Split(os.Getenv("XDG_CURRENT_DESKTOP"), ":") {
		if strings.EqualFold(desktop, "GNOME") {
			return true
		}
	}
	return false
}

// Preserve the original strv text and order, appending only a missing engine.
func appendPreloadEngine(value, engine string) (string, error) {
	value = strings.TrimSpace(strings.TrimPrefix(strings.TrimSpace(value), "@as"))
	item := `'(?:[^'\\]|\\.)*'|"(?:[^"\\]|\\.)*"`
	valid := regexp.MustCompile(`^\[\s*(?:(?:` + item + `)\s*(?:,\s*(?:` + item + `)\s*)*,?\s*)?\]$`)
	if !valid.MatchString(value) {
		return "", errors.New("IBus 引擎列表格式无效，未修改")
	}
	for _, quoted := range regexp.MustCompile(item).FindAllString(value, -1) {
		// GVariant string escapes include backslash and either quote character.
		decoded := regexp.MustCompile(`\\(.)`).ReplaceAllString(quoted[1:len(quoted)-1], "$1")
		if decoded == engine {
			return value, nil
		}
	}
	body := strings.TrimSuffix(strings.TrimSpace(value[1:len(value)-1]), ",")
	if body != "" {
		body += ", "
	}
	engine = strings.ReplaceAll(strings.ReplaceAll(engine, `\`, `\\`), "'", `\'`)
	return "[" + body + "'" + engine + "']", nil
}

func setupIBusPreload() error {
	current, err := runDesktopCommand("gsettings", "get", "org.freedesktop.ibus.general", "preload-engines")
	if err != nil {
		return err
	}
	updated, err := appendPreloadEngine(current, pinyinEngineID)
	if err != nil {
		return err
	}
	if updated != strings.TrimSpace(strings.TrimPrefix(strings.TrimSpace(current), "@as")) {
		_, err = runDesktopCommand("gsettings", "set", "org.freedesktop.ibus.general", "preload-engines", updated)
	}
	return err
}

func printNonGNOMEInstructions() {
	fmt.Println("配置完成，候选窗外观（皮肤）仅 GNOME 支持。")
	if strings.Contains(strings.ToUpper(os.Getenv("XDG_CURRENT_DESKTOP")), "KDE") {
		panel := ""
		for _, path := range []string{"/usr/lib/kimpanel-ibus-panel", "/usr/libexec/kimpanel-ibus-panel", "/usr/lib/x86_64-linux-gnu/libexec/kimpanel-ibus-panel"} {
			if info, err := os.Stat(path); err == nil && !info.IsDir() {
				panel = path
				break
			}
		}
		if panel == "" {
			fmt.Println("请先安装 kimpanel：Arch 执行 sudo pacman -S kdeplasma-addons；Debian/Ubuntu 安装 plasma-widgets-addons。")
			panel = "/usr/lib/kimpanel-ibus-panel"
		}
		fmt.Println("KDE Wayland 配置：")
		fmt.Println("1. 设置会话环境变量 GTK_IM_MODULE=ibus QT_IM_MODULE=ibus XMODIFIERS=@im=ibus，注销并重新登录。")
		fmt.Printf("2. 将 IBus 启动命令配置为 ibus-daemon -drx --panel=%s。\n", panel)
		fmt.Println("3. 在 Plasma 面板添加「输入法面板」小部件，系统设置里的虚拟键盘选择「无」。")
		fmt.Println("IBus 自带候选窗 ibus-ui-gtk3 在 KDE Wayland 下可能抢焦点、使候选消失；KWin「IBus Wayland」虚拟键盘模式可能使它崩溃，属于上游问题。")
	}
	fmt.Printf("使用 ibus engine %s 或桌面自带输入法设置切换到「思玄拼音」。\n", pinyinEngineID)
}

func setupDesktop(registerFcitx bool) error {
	dir := configDirectory()
	path := filepath.Join(dir, "config.json")
	if e := os.MkdirAll(dir, 0700); e != nil {
		return e
	}
	if e := os.Chmod(dir, 0700); e != nil {
		return e
	}
	// O_EXCL makes repeated setup safe even when another setup is running.
	f, e := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0600)
	if e == nil {
		_, e = f.WriteString("{\n  \"pinyin_enabled\": true,\n  \"hold_ctrl\": true,\n  \"text_polish\": \"basic\"\n}\n")
		ce := f.Close()
		if e == nil {
			e = ce
		}
	} else if os.IsExist(e) {
		e = nil
	}
	if e != nil {
		return e
	}
	if _, e = runDesktopCommand("systemctl", "--user", "enable", "--now", "sixuan-fcitx5.service"); e != nil {
		return e
	}
	gnome := isGNOMEDesktop()
	if gnome {
		current, e := runDesktopCommand("gsettings", "get", "org.gnome.desktop.input-sources", "sources")
		if e != nil {
			return e
		}
		updated, e := appendInputSource(current, pinyinEngineID)
		if e != nil {
			return e
		}
		if updated != strings.TrimSpace(current) {
			if _, e = runDesktopCommand("gsettings", "set", "org.gnome.desktop.input-sources", "sources", updated); e != nil {
				return e
			}
		}
		enabled, e := runDesktopCommand("gsettings", "get", "org.gnome.shell", "enabled-extensions")
		if e != nil {
			return e
		}
		enabled = strings.TrimSpace(strings.TrimPrefix(enabled, "@as"))
		uuid := "sixuan-candidates@sixuan.dev"
		if !strings.HasPrefix(enabled, "[") || !strings.HasSuffix(enabled, "]") {
			return errors.New("扩展列表格式无效，未修改")
		}
		if !strings.Contains(enabled, "'"+uuid+"'") && !strings.Contains(enabled, `"`+uuid+`"`) {
			body := strings.TrimSpace(enabled[1 : len(enabled)-1])
			if body != "" {
				body += ", "
			}
			if _, e = runDesktopCommand("gsettings", "set", "org.gnome.shell", "enabled-extensions", "["+body+"'"+uuid+"']"); e != nil {
				return e
			}
		}
	} else if err := setupIBusPreload(); err != nil {
		fmt.Printf("提示：未能自动添加 IBus 引擎，请通过桌面输入法设置添加「思玄拼音」：%v\n", err)
	}
	dataHome := os.Getenv("XDG_DATA_HOME")
	if dataHome == "" {
		h, _ := os.UserHomeDir()
		dataHome = filepath.Join(h, ".local/share")
	}
	apps := filepath.Join(dataHome, "applications")
	if e = os.MkdirAll(apps, 0700); e != nil {
		return e
	}
	desktop := "[Desktop Entry]\nType=Application\nName=思玄语音输入\nComment=思玄输入法设置\nExec=/usr/bin/sixuan-fcitx5 settings\nIcon=audio-input-microphone\nTerminal=false\nCategories=Settings;Utility;\n"
	if e = os.WriteFile(filepath.Join(apps, "sixuan-fcitx5.desktop"), []byte(desktop), 0644); e != nil {
		return e
	}
	if gnome {
		fmt.Println("配置完成，请注销并重新登录，然后按 Super+空格切换到思玄拼音。")
	} else {
		printNonGNOMEInstructions()
	}
	if fcitx5Desktop() {
		if registerFcitx {
			if err := registerFcitx5(); err != nil {
				fmt.Printf("未能自动注册 Fcitx 5 输入法：%v\n", err)
				fmt.Println("请重启 Fcitx 5 后，在 Fcitx 5 配置工具中手动添加「思玄拼音」。")
			} else {
				fmt.Println("已将「思玄拼音」加入当前 Fcitx 5 输入法组，Fcitx 5 已重启。")
			}
		} else {
			printFcitx5Instructions()
		}
	} else if registerFcitx {
		fmt.Println("未检测到正在运行的 Fcitx 5，跳过注册。")
	}
	fmt.Println("配置语音识别：" + officialASRSetupInstructions)
	return nil
}

// Changing the live Fcitx session restarts the running input method, so
// automatic registration only happens behind the explicit --register-fcitx5
// flag; plain setup only prints guidance.
func printFcitx5Instructions() {
	fmt.Println("检测到当前会话正在使用 Fcitx 5：")
	fmt.Println("· 思玄拼音同时提供 Fcitx 5 插件，可与 fcitx5-rime 等输入法并存。")
	fmt.Println("· 请重启 Fcitx 5 或重新登录以加载插件，然后在 Fcitx 5 配置工具（fcitx5-config-qt）的「输入法」列表中添加「思玄拼音」。")
	fmt.Println("· 也可以运行 sixuan-fcitx5 setup --register-fcitx5 自动添加（会重启 Fcitx 5）。")
	fmt.Println("· 当前阶段 Fcitx 5 仅支持本地拼音；语音输入与云候选请继续使用 IBus 版思玄拼音。")
}

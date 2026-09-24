# QQMusic NS 插件（sysmodule + Ultrahand overlay）
#
# 依赖：devkitPro 工具链（WSL）——见 tools/dkp-setup.sh
#   . /opt/devkitpro/devkitpro_vars
# 构建：make -j$(nproc)      （产物：sysmodule/qqmusic.nsp + overlay/qqmusic-overlay.ovl）
# 分发：make dist            （产物：dist/ns-qqmusic-v$(VERSION).zip，解压到 SD 根目录）

export VERSION     := 0.1.0
export API_VERSION := 3

TID := 42000000004B4D51

.PHONY: all module overlay ovll clean dist

all: module overlay

OVLLDIR := vendor/nx-ovlloader

module:
	$(MAKE) -C sysmodule/nxExt
	$(MAKE) -C sysmodule

overlay:
	$(MAKE) -C overlay

# 补丁版 ovlloader（含异常/流程日志器 + 2MB 主栈）：
# 安装 = 备份 SD 上 atmosphere/contents/420000000007E51A/ 后，
# 用 dist/ovll-logger.nsp 覆盖其中的 exefs.nsp。不进入主 zip，避免误装。
ovll:
	$(MAKE) -C $(OVLLDIR) build
	mkdir -p dist
	cp $(OVLLDIR)/ovll.nsp dist/ovll-logger.nsp
	@echo "ovll built: dist/ovll-logger.nsp (安装: 备份后替换 SD atmosphere/contents/420000000007E51A/exefs.nsp)"

clean:
	$(MAKE) -C sysmodule/nxExt clean
	$(MAKE) -C sysmodule clean
	$(MAKE) -C overlay clean
	rm -rf dist

dist: all
	rm -rf dist
	mkdir -p dist/switch/.overlays dist/atmosphere/contents/$(TID)/flags
	touch dist/atmosphere/contents/$(TID)/flags/boot2.flag
	cp sysmodule/qqmusic.nsp dist/atmosphere/contents/$(TID)/exefs.nsp
	cp sysmodule/toolbox.json dist/atmosphere/contents/$(TID)/
	cp overlay/qqmusic-overlay.ovl dist/switch/.overlays/
	cd dist && zip -qr ns-qqmusic-v$(VERSION).zip switch atmosphere
	@echo "dist built: dist/ns-qqmusic-v$(VERSION).zip (copy contents to SD root)"
	$(MAKE) ovll
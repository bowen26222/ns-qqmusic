#!/bin/sh
# devkitA64 工具链安装（WSL Ubuntu 24.04，root 执行）
# 镜像 devkitpro/devkita64 docker 镜像的安装内容（Streamfin CI 同款）。
# 用法: wsl -u root -d Ubuntu-24.04 -- sh /mnt/e/Codes/ns-qqmusic/tools/dkp-setup.sh
set -eu
export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y --no-install-recommends \
  wget ca-certificates curl gpg bison build-essential git zip apt-transport-https

# WSL: /etc/mtab 缺失时补符号链接（apt 源需要）
[ -e /etc/mtab ] || ln -s /proc/self/mounts /etc/mtab

# devkitPro apt 仓库（等价官方 install-devkitpro-pacman，补 -y 防交互）
mkdir -p /usr/share/keyring
[ -f /usr/share/keyring/devkitpro-pub.gpg ] || \
  wget -U "dkp apt" -O /usr/share/keyring/devkitpro-pub.gpg https://apt.devkitpro.org/devkitpro-pub.gpg
echo "deb [signed-by=/usr/share/keyring/devkitpro-pub.gpg] https://apt.devkitpro.org stable main" \
  > /etc/apt/sources.list.d/devkitpro.list
apt-get update
apt-get install -y devkitpro-pacman

# devkitPro 包（与 devkitpro/devkita64 镜像一致；yes '' 对交互提示喂空行取默认值）
yes '' | dkp-pacman -Syu
yes '' | dkp-pacman -S switch-dev switch-portlibs devkitARM dkp-toolchain-vars hactool
# 验证
# 验证（环境脚本为 switchvars.sh）
. /opt/devkitpro/switchvars.sh
aarch64-none-elf-gcc --version | head -n1
aarch64-none-elf-g++ --version | head -n1
command -v hactool && hactool --version | head -n1 || true
echo DKP_SETUP_DONE
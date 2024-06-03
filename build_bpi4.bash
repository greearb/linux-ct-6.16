#!/bin/bash

# kernel version for pkg
head -5 Makefile  > tmp_ver
sed -i 's/[[:space:]]*//g' tmp_ver
. tmp_ver
KVER_PKG=$VERSION.$PATCHLEVEL.$SUBLEVEL$EXTRAVERSION+

# Use cross compiler
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

make -j32 || exit 1

dst="bpi-r4-3"
rm -fr $dst
mkdir -p $dst/boot/
mkdir -p $dst/lib/modules/

INSTALL_MOD_PATH=$dst make -j4 modules_install || exit 2

./make-itb-r4.sh || exit 3

cp -a bpi-r4.itb $dst/boot/ || exit 4

# Copy firmware
mkdir -p $dst/lib/firmware/mediatek
cp -ar /lib/firmware/mediatek/* $dst/lib/firmware/mediatek/
# be200, ax210
cp -ar /lib/firmware/iwlwifi-gl* $dst/lib/firmware/
cp -ar /lib/firmware/iwlwifi-ma* $dst/lib/firmware/
cp -ar /lib/firmware/iwlwifi-so* $dst/lib/firmware/
cp -ar /lib/firmware/iwlwifi-ty* $dst/lib/firmware/

cd $dst
tar -cvzf ../ct${KVER_PKG}.bpi4.tar.gz .
cd -

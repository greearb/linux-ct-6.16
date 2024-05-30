# Script to copy the boot and modules to another folder
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
dst="adtran05"
mkdir /usr/src/$dst
mkdir /usr/src/$dst/rootfs
mkdir /usr/src/$dst/boot
make -j12
INSTALL_MOD_STRIP=1 INSTALL_MOD_PATH=/usr/src/$dst/rootfs make -j4 modules_install
cp -a fit-multi.itb /usr/src/$dst/boot

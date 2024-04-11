# Script to copy the boot and modules to another folder
dst="bpi-r4-3"
mkdir /usr/src/$dst
mkdir /usr/src/$dst/rootfs
mkdir /usr/src/$dst/boot
make -j12
INSTALL_MOD_STRIP=1 INSTALL_MOD_PATH=/usr/src/$dst/rootfs make -j4 modules_install
cp -a bpi-r4.itb /usr/src/$dst/boot

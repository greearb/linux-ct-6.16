# For BPI-R3
uimagearch=arm
IMAGE=arch/arm64/boot/Image
LADDR=40080000
ENTRY=40080000
kernver=$(make kernelversion | grep -v 'make')
gitbranch=
board=bpi-r4
ARCH=arm64


mkimage -A ${uimagearch} -O linux -T kernel -C none -a $LADDR -e $ENTRY -n "Linux Kernel $kernver$gitbranch" -d $IMAGE ./uImage_nodt
sed "s/%version%/$kernver$gitbranch/" ${board}.its > ${board}.its.tmp
mkimage -f ${board}.its.tmp ${board}.itb
cp ${board}.itb ${board}-$kernver$gitbranch.itb
rm ${board}.its.tmp

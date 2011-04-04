#!/bin/sh

TESTFILE="/usr/portage/distfiles/qt-everywhere-opensource-src-4.7.2.tar.gz"

grep -q " $PWD/zram0mnt " /proc/mounts && umount zram0mnt
mkdir -p zram0mnt
echo 1 >/sys/block/zram0/reset || exit
sleep 2
echo $((1024*1024*1024)) > /sys/block/zram0/disksize
mke2fs -t ext4 -m 0 -I 128 -O ^has_journal,^ext_attr /dev/zram0 >/dev/null || exit
tune2fs -l /dev/zram0 | grep 'RAID'
debugfs -w -f debugfs_input.txt /dev/zram0
tune2fs -l /dev/zram0 | grep 'RAID'
mount -o noatime,barrier=0,data=writeback,nobh,discard /dev/zram0 zram0mnt
dd if=${TESTFILE} of=/dev/null >/dev/null 2>&1
cd zram0mnt
time tar xzf ${TESTFILE}
sleep 10
cat /sys/block/zram0/orig_data_size
cat /sys/block/zram0/compr_data_size
cat /sys/block/zram0/mem_used_total
cd ..
umount zram0mnt
echo 1 >/sys/block/zram0/reset
rmdir zram0mnt

#!/bin/bash

rootfs_dir=$1
rootfs_file=$2
rootfs_mnt="mnt"

if [ ! $rootfs_dir ] || [ ! $rootfs_file ];
then
	echo "Folder or target is empty."
	exit 0
fi

if [ -f "$rootfs_file" ]; then
    echo "-- Delete exist $rootfs_file ..."
    rm -f "$rootfs_file"
fi

echo "-- Create $rootfs_file ..."
dd if=/dev/zero of="$rootfs_file" bs=1M count=4096
sudo mkfs.ext4 -F -L linuxroot "$rootfs_file"

if [ ! -d "$rootfs_mnt" ]; then  
	mkdir $rootfs_mnt
fi 

echo "-- Copy data to $rootfs_file ..."
sudo mount $rootfs_file $rootfs_mnt
sudo cp -rfp $rootfs_dir/* $rootfs_mnt
sudo sync
sudo umount $rootfs_mnt
rm -r $rootfs_mnt

echo "-- Resize $rootfs_file ..."
/sbin/e2fsck -p -f "$rootfs_file"
/sbin/resize2fs -M "$rootfs_file"

echo "-- Done."

#!/bin/bash -e

# first boot configure
if [ ! -e "/usr/local/first_boot_flag" ] ;
then
	echo "Resizing /dev/mmcblk2p3..."
    resize2fs /dev/mmcblk2p3
    touch /usr/local/first_boot_flag
fi


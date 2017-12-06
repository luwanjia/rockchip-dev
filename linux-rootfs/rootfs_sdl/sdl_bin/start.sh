#!/bin/bash
temp_path=$(pwd)
cd /opt/sdl_bin
rm app_info.dat
rm -rf storage
LD_LIBRARY_PATH=. ./smartDeviceLinkCore & ./Gen3UI &
cd $temp_path

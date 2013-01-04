#!/bin/sh
adb -d push ../../../../out/target/product/galaxysmtd/system/bin/screenrec /storage/sdcard0/
adb -d shell su root -c "cp /storage/sdcard0/screenrec /data/local/screenrec"
adb -d shell su root -c "/data/local/screenrec"




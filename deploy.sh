#!/bin/sh
adb -d push $OUT/system/bin/screenrec /data/local/tmp/screenrec
adb -d shell su -c "/data/local/tmp/screenrec /sdcard/screenrec.mp4"
adb pull /sdcard/screenrec.mp4  ../../../../..




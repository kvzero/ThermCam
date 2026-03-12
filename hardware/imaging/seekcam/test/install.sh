#!/bin/bash
set -e

adb push ../lib/libseekcamera.so.4.4 /usr/lib/

adb push 10-seekthermal.rules /etc/udev/rules.d/
adb shell "udevadm control --reload && udevadm trigger"

adb push seekcamera-fb .
adb shell "./seekcamera-fb"


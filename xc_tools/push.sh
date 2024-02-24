#!/usr/bin/env bash

unset ART_TEST_ANDROID_ROOT
unset CUSTOM_TARGET_LINKER
unset ART_TEST_ANDROID_ART_ROOT
unset ART_TEST_ANDROID_RUNTIME_ROOT
unset ART_TEST_ANDROID_I18N_ROOT
unset ART_TEST_ANDROID_TZDATA_ROOT

export ART_TEST_CHROOT=/data/local/art-test-chroot

#m adb

#Build ART and required dependencies:
#art/tools/buildbot-build.sh --target

# Clean up the device:
art/tools/buildbot-cleanup-device.sh

# Setup the device (including setting up mount points and files in the chroot directory):
art/tools/buildbot-setup-device.sh

# Populate the chroot tree on the device (including "activating" APEX packages
art/tools/buildbot-sync.sh

adb shell mkdir -p /data/dalvik-cache/loongarch64

# Run gtest:
# art/tools/run-gtests.sh -j4
# Run Java Tests
# art/test/testrunner/testrunner.py --target --64
# Run Libcore tests:
# art/tools/run-libcore-tests.sh --mode=device --variant=X64
# Run JDWP tests:
# art/tools/run-libjdwp-tests.sh --mode=device --variant=X64

# Tear down device setup:
# art/tools/buildbot-teardown-device.sh

# Clean up the device:
# art/tools/buildbot-cleanup-device.sh


#More
#adb shell "mkdir -p /data/local/art-test-chroot/data/nativetest64/com.android.art/lib64"
#adb shell "cp /data/local/art-test-chroot/data/nativetest64/art/riscv64/lib* /data/local/art-test-chroot/data/nativetest64/com.android.art/lib64/"

# gdb
# adb push ./prebuilts/misc/gdbserver/android-riscv64/gdbserver64 /data/local/art-test-chroot/apex/com.android.art/bin

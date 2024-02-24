unset ART_TEST_ANDROID_ROOT
unset CUSTOM_TARGET_LINKER
unset ART_TEST_ANDROID_ART_ROOT
unset ART_TEST_ANDROID_RUNTIME_ROOT
unset ART_TEST_ANDROID_I18N_ROOT
unset ART_TEST_ANDROID_TZDATA_ROOT

export ART_TEST_CHROOT=/data/local/art-test-chroot


# Run gtest on devices/emulator
if [ $# -gt 0 ]; then
  # for example /apex/com.android.art/bin/art/riscv64/art_cmdline_tests
  adb shell chroot "$ART_TEST_CHROOT" $@
else
  # if U run single test without any paras, please use art/tools/run-gtests.sh -j4 xxx
  # for example art/tools/run-gtests.sh -j4 /apex/com.android.art/bin/art/riscv64/art_cmdline_tests
  art/tools/run-gtests.sh -j4
fi


# Run gtest on host
#m test-art-host-gtest64
# or :
# art/test.py --host --64 -g

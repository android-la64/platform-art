#!/bin/bash

########################################
# Run Bionic functional tests
########################################

## Makesure you in adb root status
# adb root

## create test directory
export NATIVE_TEST_DIR="/data/local/data"

log_dir="./native-test-log"

## upload tests
adb shell "mkdir -p $NATIVE_TEST_DIR"
adb push ${OUT}/data/nativetest64 "$NATIVE_TEST_DIR"

if [[ ! -d $log_dir ]]; then
  mkdir -p $log_dir
fi

## run tests
all_tests="KernelLibcutilsTest libcutils_test_static init_kill_services_test libcutils_test libcutils_sockets_test libstatspush_compat_test bootstat_tests charger_test libhealthd_charger_test libutils_test storaged-unit-tests sync-unit-tests libkeyutils-tests libpackagelistparser_test libstatspush_compat_test libappfuse_test"
all_tests2="libstatspull_lazy_test  libstatssocket_lazy_test"
# secure-storage-unit-tes : will added later

time_stamp=`date +%m%d_%H%M%S`
idx=1
for test in $all_tests; do
  echo "Testing ... $test"
  adb shell "${NATIVE_TEST_DIR}/nativetest64/$test/$test" | tee "$log_dir/${time_stamp}_gtest_${idx}_$test.log"
  idx=$(( $idx + 1 ))
done
for test in $all_tests2; do
  echo "Testing ... ${test}64"
  adb shell "${NATIVE_TEST_DIR}/nativetest64/$test/${test}64" | tee "$log_dir/${time_stamp}_gtest_${idx}_$test.log"
  idx=$(( $idx + 1 ))
done

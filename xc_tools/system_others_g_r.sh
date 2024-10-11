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
all_tests="iorapd-tests libapexutil_tests flattened_apex_test ApexTestCases logd-unit-tests liblog-unit-tests libprofile-extras-test memory_replay_tests memunreachable_test memunreachable_unit_test memunreachable_binder_test  hwbinderThroughputTest libhwbinder_latency"
# logcat-unit-tests: logcat.blocking

all_tests2="CtsApexSharedLibrariesTestCases"

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


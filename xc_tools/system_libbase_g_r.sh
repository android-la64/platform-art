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
adb push ${OUT}/data/nativetest64/libbase_test "$NATIVE_TEST_DIR/nativetest64/"

if [[ ! -d $log_dir ]]; then
  mkdir -p $log_dir
fi

## run tests
all_tests="libbase_test"

time_stamp=`date +%m%d_%H%M%S`
idx=1
for test in $all_tests; do
  echo "Testing ... $test"
  adb shell "${NATIVE_TEST_DIR}/nativetest64/$test/${test}64 " | tee "$log_dir/${time_stamp}_gtest_${idx}_$test.log"
  idx=$(( $idx + 1 ))
done

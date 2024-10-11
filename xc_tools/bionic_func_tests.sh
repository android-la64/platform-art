#!/bin/bash

########################################
# Run Bionic functional tests
########################################

## Makesure you in adb root status
# adb root

## create test directory
export BIONIC_TEST_DIR="/data/local/bionic-tests/data"

# out_dir="${OUT_DIR:=out}"
log_dir="./bionic-test-log"

## upload tests
adb shell "mkdir -p $BIONIC_TEST_DIR"
adb push ${OUT}/data/nativetest64 "$BIONIC_TEST_DIR"

if [[ ! -d $log_dir ]]; then
  mkdir -p $log_dir
fi

## run tests
all_tests="bionic-unit-tests"
all_tests+=" bionic-unit-tests-static"
all_tests+=" linker-unit-tests"
all_tests+=" bionic-fortify-runtime-asan-test"
all_tests+=" malloc_debug_unit_tests"
all_tests+=" malloc_debug_system_tests"
all_tests+=" malloc_hooks_system_tests"
all_tests+=" fdtrack_test/fdtrack_test"
all_tests+=" memunreachable_test"
all_tests+=" memunreachable_binder_test"
all_tests+=" memunreachable_unit_test"

time_stamp=`date +%m%d_%H%M%S`
idx=1
for test in $all_tests; do
  adb shell "${BIONIC_TEST_DIR}/$test/$test" | tee "$log_dir/${time_stamp}_gtest_${idx}_$test.log"
  idx=$(( $idx + 1 ))
done

#!/bin/bash

TEST_NUM=$1

# Pad single digit with leading zero if needed
if [ ${#TEST_NUM} -eq 1 ]; then
    TEST_NUM="0$TEST_NUM"
fi

# Capture output to files and then diff
make rtest$TEST_NUM > expected_output.txt 2>&1
make test$TEST_NUM > actual_output.txt 2>&1

echo "Comparing trace$TEST_N.txt - Reference vs Your Shell"
diff expected_output.txt actual_output.txt

if [ $? -eq 1 ]; then
    echo "Trace$TEST_NUM PASSED - Output matches reference!"
else
    echo "❌ Trace$TEST_NUM FAILED - Output differs from reference"
fi

#!/bin/bash

# Check dir
DIR=`pwd`;
if [ `basename $DIR` != "tests" ]; then
    echo "Please run this script from tests/"
	exit
fi

# Parametrize output files
OUT_X="$DIR/X.txt"

# Clear previous test results
rm -rf $OUT_X

# Start and detach process, save pid
echo "Starting X"
./X > $OUT_X &
X_PID=$!;
EXITVAL=$?
# Wait for test termination
sleep 3

# Kill Processes
kill X_PID

# Check results
X_LINES=`cat $OUT_X | wc -l` 

# Return $SUCCESS or $FAILURE
if [[ $EXITVAL == 0 ]]; then
	exit $SUCCESS
fi

if [[ $EXITVAL == 1 ]]; then
	echo "X some error!"
	exit $FAILURE
fi

if [[ $EXITVAL == 2 ]]; then
	echo "X some error!"
	exit $FAILURE
fi

if [[ $EXITVAL == 3 ]]; then
	echo "X some error!"
	exit $FAILURE
fi

echo "Unknow exit status: $EXITVAL"
exit $FAILURE



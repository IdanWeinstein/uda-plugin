#!/bin/bash


echo "Hello from Regression 1!"
cd /tmp/mars_tests/UDA-uda.db/tests/scripts/regression/
su eladi -c "expect expect.sh performBM_regression.sh /.autodirect/mtrswgwork/eladi/regression/csvs/cluster_conf_1.csv eladi11"
exitStatus=$?
if (($exitStatus == 0));then
	echo "SUCCESS!"
else
	echo "FAILURE!"
fi

exit $exitStatus

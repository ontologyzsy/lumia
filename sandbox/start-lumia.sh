#!/usr/bin/env sh
SANDBOX_HOME=`pwd`
echo "start nexus"
cd ../ins/sandbox && nohup ./start_all.sh > ins_start.log 2>&1 &
sleep 2
echo "start lumia"
nohup ../lumia_ctrl --flagfile=lumia.flag > lumia.log 2>&1 &

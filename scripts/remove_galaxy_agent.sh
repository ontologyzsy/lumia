#!/bin/bash

cd /home/galaxy/agent/bin && babysitter galaxy-agent.conf stop

killall -9 initd

su galaxy && ps xf | grep -v PID | xargs kill -9

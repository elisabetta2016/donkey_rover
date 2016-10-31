#!/bin/bash
export DISPLAY=:0

rosnode kill -a

sleep 1
poweroff

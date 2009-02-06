#!/bin/bash

# Check dir
DIR=`pwd`;
if [ `basename $DIR` != "tests" ]; then
    echo "Please run this script from tests/"
	exit
fi

# xterm -geometry 100x20+800+500 -e "./packet_logger; echo Press enter to close this window && read" &
# sleep 1
# 
# xterm -geometry 80x24+10+500 -e "./learner_main -k 100; echo Press enter to close this window && read" &
# sleep 1

xterm -geometry 80x24+10+250 -e "./acceptor_main -i 0; echo Press enter to close this window && read" &
sleep 1

xterm -geometry 80x24+100+250 -e "./acceptor_main -i 1; echo Press enter to close this window && read" &
sleep 1

xterm -geometry 80x24+200+250 -e "./acceptor_main -i 2; echo Press enter to close this window && read" &
sleep 1

# xterm -geometry 80x24+10+20 -e "./leader_main; echo Press enter to close this window || read" &
# sleep 1

read
# gdb ./proposer_main


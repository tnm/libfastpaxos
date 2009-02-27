PROGDIR="/Users/bridge/blackcode/SourceForge/libpaxos/trunk/libfastpaxos/tests"

xterm -geometry 80x24+10+50 -e "cd $PROGDIR && ./acceptor_main -i 0; echo Press enter to close this window && read" &
sleep 1

xterm -geometry 80x24+100+50 -e "cd $PROGDIR && ./acceptor_main -i 1; echo Press enter to close this window && read" &
sleep 1

xterm -geometry 80x24+200+50 -e "cd $PROGDIR && ./acceptor_main -i 2; echo Press enter to close this window && read" &
sleep 1

xterm -geometry 80x24+200+250 -e "cd $PROGDIR && ./leader_main; echo Press enter to close this window && read" &
sleep 1

xterm -geometry 80x24+10+500 -e "cd $PROGDIR && ./learner_main -p 1 -k 20000 &> l1.out" &
sleep 1

xterm -geometry 80x24+200+500 -e "cd $PROGDIR && ./learner_main -p 1 -k 20000 &> l2.out" &
sleep 1


xterm -geometry 80x24+10+250 -e "cd $PROGDIR && ./proposer_main -i 1 -k 10000; echo Press enter to close this window && read" &

xterm -geometry 80x24+100+250 -e "cd $PROGDIR && ./proposer_main -i 2 -k 10000; echo Press enter to close this window && read" &

echo "press enter to execute diff"
read


killall -INT proposer_main learner_main acceptor_main leader_main
sleep 2
# killall proposer_main learner_main acceptor_main leader_main

wc -l "$PROGDIR/l1.out"
wc -l "$PROGDIR/l2.out"
diff --brief l1.out l2.out

killall xterm
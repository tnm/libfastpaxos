xterm -geometry 80x24+10+250 -e "./acceptor_main -i 0; echo Press enter to close this window && read" &
sleep 1

xterm -geometry 80x24+100+250 -e "./acceptor_main -i 1; echo Press enter to close this window && read" &
sleep 1

xterm -geometry 80x24+200+250 -e "./acceptor_main -i 2; echo Press enter to close this window && read" &
sleep 1

xterm -geometry 80x24+200+250 -e "./leader_main; echo Press enter to close this window && read" &
sleep 1

xterm -geometry 80x24+10+250 -e "./learner_main > l1.out" &
sleep 1

xterm -geometry 80x24+10+250 -e "./learner_main > l2.out" &
sleep 1


xterm -geometry 80x24+10+250 -e "./proposer_main 1; echo Press enter to close this window && read" &

xterm -geometry 80x24+100+250 -e "./proposer_main 2; echo Press enter to close this window && read" &

echo "press enter to execute diff"
read


killall xterm
sleep 2

wc -l l1.out
wc -l l2.out
diff --brief l1.out l2.out

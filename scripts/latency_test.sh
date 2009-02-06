VALUES=10000
VALUE_SIZE=400

# Acceptors
xterm -geometry 80x24+10+250 -e "../tests/acceptor_main -i 0; echo Press enter to close this window && read" &
sleep 1

xterm -geometry 80x24+100+250 -e "../tests/acceptor_main -i 1; echo Press enter to close this window && read" &
sleep 1

xterm -geometry 80x24+200+250 -e "../tests/acceptor_main -i 2; echo Press enter to close this window && read" &
sleep 1

# Leader
xterm -geometry 80x24+10+10 -e "../tests/leader_main; echo Press enter to close this window && read" &
sleep 3

# Learner
../tests/proposer_main -i 1 -s $VALUE_SIZE -k $VALUES -r

killall xterm

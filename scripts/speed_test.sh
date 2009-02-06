VALUES=50000
N_OF_PROPOSERS=1

# Acceptors
xterm -geometry 80x24+10+250 -e "../tests/acceptor_main -i 0; echo Press enter to close this window && read" &
sleep 1

xterm -geometry 80x24+100+250 -e "../tests/acceptor_main -i 1; echo Press enter to close this window && read" &
sleep 1

xterm -geometry 80x24+200+250 -e "../tests/acceptor_main -i 2; echo Press enter to close this window && read" &
sleep 1

# xterm -geometry 80x24+10+250 -e "../tests/acceptor_main -i 3; echo Press enter to close this window && read" &
# sleep 1
# 
# xterm -geometry 80x24+100+250 -e "../tests/acceptor_main -i 4; echo Press enter to close this window && read" &
# sleep 1
# 
# xterm -geometry 80x24+200+250 -e "../tests/acceptor_main -i 5; echo Press enter to close this window && read" &
# sleep 1

# Leader
xterm -geometry 80x24+10+10 -e "../tests/leader_main; echo Press enter to close this window && read" &
sleep 1

# Proposers

let 'init_sleep=N_OF_PROPOSERS+3'
for (( i = 1; i <= N_OF_PROPOSERS; i++ )); do
    let 'sleep_delay=init_sleep - i'
    xterm -geometry 80x24+300+10 -e "sleep $sleep_delay;../tests/proposer_main -i $i; echo Press enter to close this window && read" &
    sleep 1
done

# Learner
../tests/learner_main -m -k $VALUES

killall xterm

#ifndef _CONFIG_H_
#define _CONFIG_H_

/* 
    TODO comment
*/
#define N_OF_ACCEPTORS  3

/* 
    TODO comment
*/
#define MAX_PROPOSERS   10

/* 
    TODO comment
    in milliseconds
*/
#define LEARNER_LSYNC_INTERVAL 5000

/* 
    TODO comment
    in milliseconds
*/
#define PROMISE_TIMEOUT 2000

/* 
    TODO comment
    in milliseconds
*/
#define ACCEPT_TIMEOUT 2000

/* 
    TODO comment
    in milliseconds
*/
#define LEADER_P2_TIMEOUT 3000

/* 
    Force Acceptors to write and flush to disk
    Before answering to any prepare or accept
*/
#define PAXOS_ACCEPTOR_FORCE_DISK_FLUSH 0

/* 
    Number of phase1 instances
    to be pre-executed by leader
*/
#define PROPOSER_PREEXEC_WIN_SIZE 5

/* 
   ACCEPTOR_ARRAY_SIZE is chosen to be at least 
   10 times larger than PROPOSER_PREEXEC_WIN_SIZE
   IMPORTANT: This must be a power of 2
*/
#define ACCEPTOR_ARRAY_SIZE 1024

/* 
   LEARNER_ARRAY_SIZE can be any size, to ensure proper
   buffering it can be set to a multiple of the 
   PROPOSER_PREEXEC_WIN_SIZE
   IMPORTANT: This must be a power of 2
*/
#define LEARNER_ARRAY_SIZE 512


/* 
   PROPOSER_ARRAY_SIZE can be any size, to ensure proper
   buffering it can be set to a multiple of the 
   PROPOSER_PREEXEC_WIN_SIZE
   IMPORTANT: This must be a power of 2
*/
#define PROPOSER_ARRAY_SIZE 512

/* 
   Multicast <address, port> for the respective groups
*/
#define PAXOS_LEARNERS_NET  "239.0.0.1", 6001
#define PAXOS_ACCEPTORS_NET "239.0.0.1", 6002
#define PAXOS_PROPOSERS_NET "239.0.0.1", 6003

/* 
   Verbosity for libpaxos routines
   from 4 (veeery verbose) to 0 (silent)
*/
#define VERBOSITY_LEVEL 4

#endif /* _CONFIG_H_ */

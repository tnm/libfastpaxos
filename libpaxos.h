#ifndef _LIBPAXOS_H_
#define _LIBPAXOS_H_

/*
    (MTU) - 8 (multicast header) - 32 (biggest paxos header)
*/
#define MAX_UDP_MSG_SIZE 9000
#define PAXOS_MAX_VALUE_SIZE 8960
typedef void (* deliver_function)(char*, size_t, int, int, int);

//Starts a learner and does not return unless an error occours
int learner_init(deliver_function f);

//Starts a learner in a thread and returns
int learner_init_threaded(deliver_function f);

//Starts a proposer and returns
int proposer_init(int proposer_id, int is_leader);

//Starts a proposer which also delivers values from it's internal learner.
//IMPORTANT: 
// This function starts the libevent loop in a new thread. The function F is invoked by this thread.
// Therefore if the callback F accessess data shared with other threads (i.e. the one that calls proposer_submit()), F must be made thread-safe!!!
// At the moment it is thread-safe only if the other thread is blocked-in or trying to call proposer_submit.
// F must be as fast as possible since the proposer is blocked in the meanwhile.
// The contents of the value buffer passed as first argument to F should not be modified!
int proposer_init_and_deliver(int prop_id, int is_leader, deliver_function f);

//Submit a value paxos, returns when the value is delivered
void proposer_submit_value(char * value, size_t val_size);

//Starts an acceptor and does not return unless an error occours
int acceptor_start(int acceptor_id);

//// TO DO - Not Implemented! ///

int proposer_queue_size();

void proposer_print_event_counters();


int learner_get_next_value(char** valuep);
int learner_get_next_value_nonblock(char** valuep);

void learner_print_event_counters();


#endif /* _LIBPAXOS_H_ */

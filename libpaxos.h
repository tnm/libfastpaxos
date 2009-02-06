#ifndef _LIBPAXOS_H_
#define _LIBPAXOS_H_

/*
    (MTU) - 8 (multicast header) - 32 (biggest paxos header)
*/
#define MAX_UDP_MSG_SIZE 9000
#define PAXOS_MAX_VALUE_SIZE 8960
typedef void (* deliver_function)(char*, size_t, int, int, int);

// int leader_init(int proposer_id);
int learner_init(deliver_function f);
int learner_init_threaded(deliver_function f);

int proposer_init(int proposer_id, int is_leader);
void proposer_submit_value(char * value, size_t val_size);

int acceptor_start(int acceptor_id);

//// TO DO ///

#define PAXOS_MAX_VALUE_SIZE 8960

int proposer_queue_size();

void proposer_print_event_counters();


/*
    Starts the acceptor loop.
    Returns only if an error occurs.
*/



int learner_get_next_value(char** valuep);
int learner_get_next_value_nonblock(char** valuep);

void learner_print_event_counters();

/* 
Alternative API to submit multiple values
locking a single time, use with care, 
it blocks the proposer
*/
int lock_proposer_submit();
int nolock_proposer_submit_value(char * value, int size);
int unlock_proposer_submit();

#endif /* _LIBPAXOS_H_ */

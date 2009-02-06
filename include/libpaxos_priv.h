#ifndef _LIBPAXOS_PRIV_H_
#define _LIBPAXOS_PRIV_H_
#include <netinet/in.h>
#include <arpa/inet.h>

#include "libpaxos.h"
#include "config.h"

/*
    Paxos message types
*/
#define PAXOS_PREPARE   1
#define PAXOS_ACCEPT    2
#define PAXOS_LSYNC     3
#define PAXOS_PROMISE   4
#define PAXOS_LEARN     5
#define PAXOS_ANYVAL    6


/* 
    Paxos messages 
*/
typedef struct paxos_msg_t {
    int size; //Size of 'data' in bytes
    int type;
    char data[0];
} paxos_msg;
#define PAXOS_MSG_SIZE(m) (sizeof(paxos_msg) + m->size)

typedef struct prepare_msg_t {
    int iid;
    int ballot;
} prepare_msg;

typedef struct prepare_batch_msg_t {
    int     count;
    char    data[0];
} prepare_batch_msg;

typedef struct promise_msg_t {
    int     iid;
    int     ballot;
    int     value_ballot;
    int     value_size;
    char    value[0];
} promise_msg;

typedef struct promise_batch_msg_t {
    int     acceptor_id;
    int     count;
    char    data[0];
} promise_batch_msg;

typedef struct accept_msg_t {
    int     iid;
    int     ballot;
    int     value_size;
    int     proposer_id;//
    char    value[0];
} accept_msg;

typedef struct learn_msg_t {
    int     acceptor_id;
    int     iid;
    int     ballot;
    int     proposer_id;//
    int     value_size;
    char    value[0];
} learn_msg;

typedef struct learner_sync_msg_t {
    int     count;
    int     ids[0];
} learner_sync_msg;

typedef struct anyval_msg_t {
    int     ballot;
    int     count;
    int     ids[0];
} anyval_msg;

int leader_init(int prop_id, int send_socket, struct sockaddr_in * acc_addr);
int proposer_is_leader();
void leader_handle_proposer_msg(int fd, short event, void *arg);
void leader_deliver_value(char * value, size_t size, int iid, int ballot, int proposer);

/* 
   This is equivalent to n mod ACCEPTOR_ARRAY_SIZE, 
   works only if ACCEPTOR_ARRAY_SIZE is a power of 2.
*/
#define GET_ACC_INDEX(n) (n & (ACCEPTOR_ARRAY_SIZE-1))

/* 
   This is equivalent to n mod LEARNER_ARRAY_SIZE, 
   works only if LEARNER_ARRAY_SIZE is a power of 2.
*/
#define GET_LEA_INDEX(n) (n & (LEARNER_ARRAY_SIZE-1))


/* 
   This is equivalent to n mod PROPOSER_ARRAY_SIZE, 
   works only if PROPOSER_ARRAY_SIZE is a power of 2.
*/
#define GET_PRO_INDEX(n) (n & (PROPOSER_ARRAY_SIZE-1))


/*** LOGGING MACROS ***/

#define VRB 1
#define V_VRB 2
#define DBG 3
#define V_DBG 4

#define LOG(L, S) if(VERBOSITY_LEVEL >= L) {\
   printf("%s: ", __func__) ;\
   printf S ;\
}

#endif /* _LIBPAXOS_PRIV_H_ */


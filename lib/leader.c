#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> 
#include <memory.h> 
#include <math.h>
#include <assert.h>

#include "event.h"

#include "libpaxos.h"
#include "libpaxos_priv.h"
#include "paxos_udp.h"

static int quorum;
static int proposer_id;
static int acceptors_sock;
static struct sockaddr_in * acceptors_net_addr;
static int current_iid = 0;
    
static struct timeval leader_p1_interval;
static struct event phase1_check_event;

static struct timeval leader_p2_interval;
static struct event phase2_check_event;

static struct event leader_msg_event;
static struct sockaddr_in prop_net_addr;
static char leader_recv_buffer[MAX_UDP_MSG_SIZE];

typedef struct phase1_info_t {
    int pending_count;
    int ready_count;
    int highest_ready;
    int first_to_check;
    int last_to_check;
} phase1_info;
static phase1_info p1info;

typedef struct phase2_info_t {
    int current_iid;
} phase2_info;
static phase2_info p2info;

static char leader_send_buffer[MAX_UDP_MSG_SIZE];
static paxos_msg* msg = (paxos_msg*) leader_send_buffer;

typedef enum status_flag_t {
  p1_new,
  p1_pending,
  p1_ready,
  p1_finished
} status_flag;

typedef struct promise_info_t {
    int     iid;
    int     value_ballot;
    int     value_size;
    char *  value;
} promise_info;

typedef struct proposer_record_t {
    int iid;
    int ballot;
    status_flag status;
    int promise_count;
    promise_info promises[N_OF_ACCEPTORS];
    // promise_msg* reserved_promise;
} proposer_record;


static proposer_record proposer_array[PROPOSER_ARRAY_SIZE];
#define FIRST_BALLOT (2 * MAX_PROPOSERS + proposer_id)
#define BALLOT_NEXT(lastBal) (lastBal + MAX_PROPOSERS)
#define VALUE_OWNER(bal) (bal % MAX_PROPOSERS)

void leader_deliver_value(char * value, size_t size, int iid, int ballot, int proposer) {
    LOG(V_VRB, ("Value delivered for instance %d\n", iid));
    current_iid = iid + 1;
    p1info.ready_count--;
}

void flush_send_buffer() {
    int msg_size = sizeof(paxos_msg) + msg->size;
    udp_send(acceptors_sock, acceptors_net_addr, leader_send_buffer, msg_size);
}

void set_phase1_check_timeout() {
	event_add(&phase1_check_event, &leader_p1_interval);
}


void set_phase2_check_timeout() {
    p2info.current_iid = current_iid;
    event_add(&phase2_check_event, &leader_p2_interval);
}

void add_prepare_to_buffer(int iid, int ballot) {
    prepare_batch_msg* batch = (prepare_batch_msg*)msg->data;
    prepare_msg* prep = (prepare_msg*)&msg->data[msg->size];
    prep->iid = iid;
    prep->ballot = ballot;

    batch->count++;
    msg->size += (sizeof(prepare_msg));

    if (msg->size + sizeof(paxos_msg) + sizeof(prepare_msg) > MAX_UDP_MSG_SIZE) {
        LOG(DBG, ("Sending propose batch for %d instances\n", ((prepare_batch_msg*)msg->data)->count));
        flush_send_buffer();
        msg->type = PAXOS_PREPARE;
        msg->size = sizeof(prepare_batch_msg);
        batch->count = 0;
    }   
}

int execute_phase1() {
    proposer_record* rec;
    int from = (p1info.highest_ready + 1);
    int to = (from + PROPOSER_PREEXEC_WIN_SIZE);
    LOG(DBG, ("Leader pre-executing phase 1 from %d to %d\n", from, to));

    int i;
    for(i = from; i <= to; i++) {
        rec = &proposer_array[GET_PRO_INDEX(i)];     
        
        if(rec->iid < i || rec->status == p1_new) {
            p1info.pending_count++;
            rec->iid = i;
            rec->status = p1_pending;
            rec->ballot = FIRST_BALLOT;
            rec->promise_count = 0;
            int j;
            for(j = 0; j < N_OF_ACCEPTORS; j++) {
                rec->promises[j].iid = -1;
                rec->promises[j].value_ballot = -1;
                rec->promises[j].value_size = 0;
                if (rec->promises[j].value != NULL) {
                    free(rec->promises[j].value);
                    rec->promises[j].value = NULL;
                }
            }
            add_prepare_to_buffer(rec->iid, rec->ballot);
        }
    }

    if(p1info.last_to_check < to) {
        p1info.last_to_check = to;
    }
    if(p1info.first_to_check > from) {
        p1info.first_to_check = from;
    }
    
    if(((prepare_batch_msg*)msg->data)->count > 0) {
        LOG(DBG, ("Sending propose batch for %d instances\n", ((prepare_batch_msg*)msg->data)->count));
        flush_send_buffer();
    }
    return 0;
}

static void phase1_check_cb(int fd, short event, void *arg) {    
    LOG(DBG, ("Leader periodic p1 check\n"));
    proposer_record* rec;
    prepare_batch_msg* batch;
    
    //init send buf (prepare batch)
    msg->type = PAXOS_PREPARE;
    msg->size = sizeof(prepare_batch_msg);
    batch = (prepare_batch_msg*)msg->data;
    batch->count = 0;
    
    //Check answer from last time
    int i;
    for(i = p1info.first_to_check; i <= p1info.last_to_check; i++) {
        rec = &proposer_array[GET_PRO_INDEX(i)];

        if (rec->iid == i && rec->status == p1_pending) {
            //add prepare to sendbuf
            rec->ballot = BALLOT_NEXT(rec->ballot);
            rec->promise_count = 0;
            int j;
            for(j = 0; j < N_OF_ACCEPTORS; j++) {
                rec->promises[j].iid = -1;
                rec->promises[j].value_ballot = -1;
                rec->promises[j].value_size = 0;
                if (rec->promises[j].value != NULL) {
                    free(rec->promises[j].value);
                    rec->promises[j].value = NULL;
                }
            }
            
            add_prepare_to_buffer(rec->iid, rec->ballot);
        }
    }    

    LOG(V_VRB, ("pending count: %d\n", p1info.pending_count));
    LOG(V_VRB, ("ready count: %d\n", p1info.ready_count));
    if (p1info.pending_count + p1info.ready_count < (PROPOSER_PREEXEC_WIN_SIZE/2)) {
        execute_phase1(); //add some stuff to sendbuf
    }
    
    if(batch->count > 0) {
        flush_send_buffer();
    }

    set_phase1_check_timeout();
}

int handle_promise(promise_msg* prom, int acceptor_id, proposer_record* rec) {
    
    //Ignore because there is no info about
    //this iid in proposer_array
    if(rec->iid != prom->iid) {
        LOG(DBG, ("P: Promise for %d ignored, no info in array\n", prom->iid));
        return 0;
    }
    
    //This promise is not relevant, because
    //we are not waiting for promises for instance iid
    if(rec->status != p1_pending) {
        LOG(DBG, ("P: Promise for %d ignored, instance is not p1_pending\n", prom->iid));
        return 0;        
    }
    
    //This promise is old, or belongs to another
    if(rec->ballot != prom->ballot) {
        LOG(DBG, ("P: Promise for %d ignored, not our ballot\n", prom->iid));
        return 0;
    }
    
    //Already received this promise
    if(rec->promises[acceptor_id].iid != -1 && rec->ballot == prom->ballot) {
        LOG(DBG, ("P: Promise for %d ignored, already received\n", prom->iid));
        return 0;
    }
    
    //Received promise, save it in proposer_array
    rec->promises[acceptor_id].iid = prom->iid;
    rec->promises[acceptor_id].value_ballot = prom->value_ballot;
    if(prom->value_ballot != -1) {
        char * value = malloc(prom->value_size);
        memcpy(value, prom->value, prom->value_size);
        rec->promises[acceptor_id].value = value;
        rec->promises[acceptor_id].value_size = prom->value_size;
    }
    rec->promise_count++;
    
    if(rec->promise_count < quorum) {
        LOG(DBG, ("P: Promise for %d received, not a quorum yet\n", prom->iid));
        return 0;
    }
        
    LOG(DBG, ("P: Promise for %d received, quorum reached!\n", prom->iid));
    rec->status = p1_ready;
    p1info.pending_count--;
    p1info.ready_count++;
    if(p1info.highest_ready < rec->iid) {
        p1info.highest_ready = rec->iid;
    }
    if(p1info.first_to_check > rec->iid) {
        p1info.first_to_check = rec->iid;
    }
    if(p1info.last_to_check < rec->iid) {
        p1info.last_to_check = rec->iid;
    }
    return 1;
}

// Checked when exactly a quorum is received.
// No values -> return null
// Some value(s) -> return the one with highest ballot
static promise_info* phase2_value(proposer_record * rec) {
    int i;
    int max_ballot = -1;
    promise_info* pi = NULL;
    
    for(i = 0; i < N_OF_ACCEPTORS; i++) {
        //Not a valid record
        if(rec->promises[i].iid == -1)
            continue;
        
        if(rec->promises[i].value_ballot != -1) {
            if(rec->promises[i].value_ballot > max_ballot) {
                max_ballot = rec->promises[i].value_ballot;
                pi = &rec->promises[i];
            }
        }
    }
    
    return pi;
}

static void append_anymsg_to_buffer(anyval_msg* anymsg, int iid) {
    anymsg->ids[anymsg->count] = iid;
    anymsg->count++;
    msg->size += sizeof(int);
    if (msg->size + sizeof(paxos_msg) + sizeof(int) > MAX_UDP_MSG_SIZE) {
        LOG(DBG, ("Sending anyval msg batch for %d instances\n", (anymsg->count)));
        flush_send_buffer();
        anymsg->count = 0;
        msg->size = sizeof(anyval_msg);
        anymsg->ballot = -1;
    }
}

static void send_accept(proposer_record* rec, promise_info* pi) {
    char buffer[MAX_UDP_MSG_SIZE];
    paxos_msg* pmsg = (paxos_msg*)buffer;
    accept_msg* accmsg = (accept_msg*)pmsg->data;
    
    accmsg->iid = rec->iid;
    accmsg->ballot = rec->ballot;
    accmsg->value_size = pi->value_size;
    accmsg->proposer_id = VALUE_OWNER(pi->value_ballot);
    memcpy(accmsg->value, pi->value, pi->value_size);
    
    pmsg->size = sizeof(accept_msg) + pi->value_size;
    pmsg->type = PAXOS_ACCEPT;
    
    int msg_size = sizeof(paxos_msg) + pmsg->size;
    udp_send(acceptors_sock, acceptors_net_addr, buffer, msg_size);
}

//for all p1_ready, if there's no value -> send any
//for all p1_ready, if there's a val -> send accept
//-> set accept timeout(first_iid, last_iid)
static void execute_phase2(int first_iid, int last_iid) {
    proposer_record* rec;
    anyval_msg* anymsg = (anyval_msg*)msg->data;
    promise_info* pi = NULL;
    
    msg->size = sizeof(anyval_msg);
    msg->type = PAXOS_ANYVAL;
    anymsg->count = 0;
    anymsg->ballot = -1;
    
    int i;
    for(i = first_iid; i <= last_iid; i++) {
        rec = &proposer_array[GET_PRO_INDEX(i)];
        if(rec->status != p1_ready)
            continue;
        
        if((pi = phase2_value(rec)) == NULL) {
            if(rec->ballot > anymsg->ballot)
                anymsg->ballot = rec->ballot;
            append_anymsg_to_buffer(anymsg, i);
        } else {
            send_accept(rec, pi);
        }
        
        rec->status = p1_finished;
    }
    
    if (anymsg->count > 0) {
        LOG(DBG, ("Sending anyval msg batch for %d instances\n", (anymsg->count)));
        flush_send_buffer();
    }
}

static void leader_handle_promise_msg(promise_batch_msg* batch) {
    int i, offset = 0;
    int first_iid = -1;
    int last_iid = -1;
    proposer_record* rec;
    promise_msg * prom;

    for(i = 0; i < batch->count; i++) {
        prom = (promise_msg *)&batch->data[offset];
        rec = &proposer_array[GET_PRO_INDEX(prom->iid)];        
        if(i == 0) {
            first_iid = prom->iid;
        }
        if(i == (batch->count - 1)) {
            last_iid = prom->iid;
        }

        handle_promise(prom, batch->acceptor_id, rec);
        offset += (sizeof(promise_msg) + prom->value_size);
    }
    
    execute_phase2(first_iid, last_iid);    
}

void leader_handle_proposer_msg(int fd, short event, void *arg) {
    // struct sockaddr_in * prop_net_addr = arg;
    socklen_t addrlen = sizeof(struct sockaddr);
    int msg_size = recvfrom(fd, leader_recv_buffer, MAX_UDP_MSG_SIZE, MSG_WAITALL, (struct sockaddr *)&prop_net_addr, &addrlen);
    
    paxos_msg * pmsg = (paxos_msg*) leader_recv_buffer;
    LOG(DBG, ("Leader got messag type:%d, size %d (%d bytes tot.)\n", pmsg->type, pmsg->size, msg_size));
    
    switch(pmsg->type) {
        case PAXOS_PROMISE: {
            if (msg_size < (sizeof(paxos_msg) + sizeof(promise_batch_msg))) {
                printf("Bad promise packet: too small: %d\n", msg_size);
            } else {
                leader_handle_promise_msg((promise_batch_msg*)pmsg->data);
            }
        }
        break;
        default: {
            printf("Invalid packet type %d received from proposers network\n", pmsg->type);
        }
    }
}

struct event sock_register_event;
void register_leader_sock(int fd, short event, void *arg) {
    /* Initalize leader message event */
    if(proposer_is_leader()) {
        int sock;
        sock = create_udp_socket(PAXOS_PROPOSERS_NET, &prop_net_addr);
        if (sock < 0) {
            printf("Error creating leader recv socket\n");
            return;
        }
        LOG(V_VRB, ("Leader listening on socket %d\n", sock));
        event_set(&leader_msg_event, sock, EV_READ|EV_PERSIST, leader_handle_proposer_msg, NULL);
        if (event_add(&leader_msg_event, NULL) != 0) {
            printf("Failed to add leader message event\n");
            return;
        }
    }
}

static void phase2_check_cb(int fd, short event, void *arg) {
    LOG(DBG, ("Leader periodic p2 check\n"));
    
    if (current_iid == p2info.current_iid) {
        LOG(DBG, ("No progress from last time!\n"));
        
        //Check in learner:
        //Quorum has my ballot?
        
        // Y -> directly 2a with v (collision)
        
        // N -> (my any was lost?
                // another leader?
                // no proposer sending value?)
        //Restart from p1
        // (collect promises and send any/accept)
        
        proposer_record* rec;
        prepare_batch_msg* batch;
        
        //init send buf (prepare batch)
        msg->type = PAXOS_PREPARE;
        msg->size = sizeof(prepare_batch_msg);
        batch = (prepare_batch_msg*)msg->data;
        batch->count = 0;
        
        rec = &proposer_array[GET_PRO_INDEX(current_iid)];
        assert(rec->iid == current_iid);
        
        if(rec->status == p1_finished) {
            p1info.ready_count--;
            p1info.pending_count++;
            rec->status = p1_pending;
        }
        
        rec->ballot = BALLOT_NEXT(rec->ballot);
        rec->promise_count = 0;
        int i;
        for(i = 0; i < N_OF_ACCEPTORS; i++) {
            rec->promises[i].iid = -1;
            rec->promises[i].value_ballot = -1;
            rec->promises[i].value_size = 0;
            if (rec->promises[i].value != NULL) {
                free(rec->promises[i].value);
                rec->promises[i].value = NULL;
            }
        }
        
        add_prepare_to_buffer(rec->iid, rec->ballot);
        flush_send_buffer();
    }
    
    set_phase2_check_timeout();    
}

int leader_init(int prop_id, int send_socket, struct sockaddr_in * acc_addr) {   
    proposer_id = prop_id;
    acceptors_sock = send_socket;
    acceptors_net_addr = acc_addr;
    
    quorum = 1 + (int)((double)(N_OF_ACCEPTORS*2)/3);
    LOG(V_VRB, ("Quorum is %d (%d acceptors)\n", quorum, N_OF_ACCEPTORS));

    
    p1info.pending_count = 0;
    p1info.ready_count = 0;
    p1info.highest_ready = -1;
    p1info.first_to_check = 0;
    p1info.last_to_check = 0;
    
    evutil_timerclear(&leader_p1_interval);
    leader_p1_interval.tv_sec = (PROMISE_TIMEOUT/1000);
    leader_p1_interval.tv_usec = (PROMISE_TIMEOUT % 1000)*1000;

    struct timeval sock_register_timeout;
    evutil_timerclear(&sock_register_timeout);
    sock_register_timeout.tv_sec = 0;
    sock_register_timeout.tv_usec = 0;
    evtimer_set(&sock_register_event, register_leader_sock, NULL);
	event_add(&sock_register_event, &sock_register_timeout);
	
	
	evutil_timerclear(&leader_p2_interval);
    leader_p2_interval.tv_sec = (LEADER_P2_TIMEOUT/1000);
    leader_p2_interval.tv_usec = (LEADER_P2_TIMEOUT % 1000)*1000;
    evtimer_set(&phase2_check_event, phase2_check_cb, NULL);

    p2info.current_iid = -1;
    set_phase2_check_timeout();
    
    //Exec phase1 and set timeout
    msg->type = PAXOS_PREPARE;
    msg->size = sizeof(prepare_batch_msg);
    ((prepare_batch_msg*)msg->data)->count = 0;
    
    evtimer_set(&phase1_check_event, phase1_check_cb, NULL);
    execute_phase1();
    set_phase1_check_timeout();
    
    LOG(VRB, ("Leader init completed\n"));
    return 0;
}

void leader_exit() {
    //Unregister promise callback
    //UR timeouts
}

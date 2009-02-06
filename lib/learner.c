#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <pthread.h> 
#include "event.h"
#include "evutil.h"

#include "libpaxos_priv.h"
#include "paxos_udp.h"


typedef struct learner_record_t {
    int         iid;
    int         ballot;
    int         proposer_id;
    char*       final_value;
    int         final_value_size;
    learn_msg*  learns[N_OF_ACCEPTORS];
} learner_record;

static unsigned int quorum;

static int highest_delivered = -1;
static int highest_seen = -1;
static learner_record learner_array[LEARNER_ARRAY_SIZE];

static struct sockaddr_in lear_net_addr;
static struct sockaddr_in acceptor_net_addr;
static int send_socket;

struct event learner_msg_event;
static char lear_recv_buffer[MAX_UDP_MSG_SIZE];
static char lear_send_buffer[MAX_UDP_MSG_SIZE];

static struct event lsync_event;
static struct timeval lsync_interval;

static char deliver_buffer[PAXOS_MAX_VALUE_SIZE];
static deliver_function delfun;

static int learner_ready = 0;
static pthread_mutex_t ready_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  ready_cond = PTHREAD_COND_INITIALIZER;
static pthread_t learner_thread;

int is_closed(learner_record* rec) {
    return (rec->final_value != NULL);
}

//Returns 0 if the learn was dropped
//Or 1 if it was interesting and stored
int add_learn_to_record(learner_record* rec, learn_msg* lmsg) {
    learn_msg * old_learn, * new_learn;
    
    //Check that is in bounds
    if(lmsg->acceptor_id < 0 || lmsg->acceptor_id >= N_OF_ACCEPTORS) {
        printf("Error: received acceptor_id:%d\n", lmsg->acceptor_id);
        return 0;
    }
    
    old_learn = rec->learns[lmsg->acceptor_id];
    
    //First learn from this acceptor for this instance
    if(old_learn == NULL) {
        LOG(DBG, ("Got first learn for instance:%d, acceptor:%d\n", rec->iid,  lmsg->acceptor_id));
        new_learn = malloc(sizeof(learn_msg) + lmsg->value_size);
        memcpy(new_learn, lmsg, (sizeof(learn_msg) + lmsg->value_size));
        rec->learns[lmsg->acceptor_id] = new_learn;
        return 1;
    }
    
    //This message is old, drop
    if(old_learn->ballot >= lmsg->ballot) {
        LOG(DBG, ("Dropping learn for instance:%d, more recent ballot already seen\n", rec->iid));
        return 0;
    }
    
    //This message is relevant! Overwrite previous learn
    LOG(DBG, ("Overwriting previous learn for instance %d\n", rec->iid));
    free(old_learn);
    new_learn = malloc(sizeof(learn_msg) + lmsg->value_size);
    memcpy(new_learn, lmsg, (sizeof(learn_msg) + lmsg->value_size));
    rec->learns[lmsg->acceptor_id] = new_learn;
    return 1;    
}

//returns 1 if message was relevant, 
//return 0 if message was already received
int update_record(learn_msg* lmsg) {
    int i;
    learner_record * rec;

    //We are late, drop to not overwrite
    if(lmsg->iid >= highest_delivered + LEARNER_ARRAY_SIZE) {
        LOG(DBG, ("Dropping learn for instance too far in future:%d\n", lmsg->iid));
        return 0;
    }

    //Already sent to client, drop
    if(lmsg->iid <= highest_delivered) {
        LOG(DBG, ("Dropping learn for already enqueued instance:%d\n", lmsg->iid));
        return 0;
    }

    //Retrieve record for this iid
    rec = &learner_array[GET_LEA_INDEX(lmsg->iid)];

    //First message for this iid
    if(rec->iid != lmsg->iid) {
        LOG(DBG, ("Received first message for instance:%d\n", lmsg->iid));

        //Clean record
        rec->iid = lmsg->iid;
        if (rec->final_value != NULL) 
            free(rec->final_value);
        rec->final_value = NULL;
        rec->final_value_size = -1;
        for(i = 0; i < N_OF_ACCEPTORS; i++) {
            if(rec->learns[i] != NULL) free(rec->learns[i]);
            rec->learns[i] = NULL;
        }
        //Add
        return add_learn_to_record(rec, lmsg);

    //Found record containing some info
    } else {
        if(is_closed(rec)) {
            LOG(DBG, ("Dropping learn for closed instance:%d\n", lmsg->iid));
            return 0;
        }
        //Add
        return add_learn_to_record(rec, lmsg);
    }
}


static void deliver_values(int instance_id) {    
    while(1) {
        learner_record* rec = &learner_array[GET_LEA_INDEX(instance_id)];  

        if(!is_closed(rec)) break;
        
        memcpy(deliver_buffer, rec->final_value, rec->final_value_size);
        delfun(deliver_buffer, rec->final_value_size, instance_id, rec->ballot, rec->proposer_id);
        free(rec->final_value);
        rec->final_value = NULL;
        highest_delivered = instance_id;
        instance_id++;
    }
}

//Checks if we have a quorum and returns 1 if the case
int check_quorum(learn_msg* lmsg) {
    int i, count = 0;
    learn_msg* stored_learn;
    learner_record* rec;
    
    rec = &learner_array[GET_LEA_INDEX(lmsg->iid)];

    for(i = 0; i < N_OF_ACCEPTORS; i++) {
        stored_learn = rec->learns[i];

        //No value
        if(stored_learn == NULL) 
            continue;

        //Different value        
        if(stored_learn->ballot != lmsg->ballot) 
            continue;
            
        //Same ballot
        count++;
    }
    
    //Mark as closed, we have a value
    if (count >= quorum) {
        LOG(DBG, ("Reached quorum, instance %d is now closed!\n", rec->iid));
        
        rec->ballot = lmsg->ballot;
        rec->proposer_id = lmsg->proposer_id;
        rec->final_value_size = lmsg->value_size;
        rec->final_value = malloc(lmsg->value_size);
        memcpy(rec->final_value, lmsg->value, lmsg->value_size);
            
        return 1;
    }
    
    return 0;
}

static void learner_handle_learn_msg(learn_msg* lmsg) {    
    if (lmsg->iid > highest_seen) {
        highest_seen = lmsg->iid;
    }
    
    //for each message update instance record
    int relevant = update_record(lmsg);
    if(!relevant) {
        LOG(DBG, ("Learner discarding learn for instance %d\n", lmsg->iid));
        return;
    }

    //if quorum reached, close the instance
    int closed = check_quorum(lmsg);
    if(!closed) {
        LOG(DBG, ("Not yet a quorum for instance %d\n", lmsg->iid));
        return;
    }

    //If the closed instance is last delivered + 1
    if (lmsg->iid == highest_delivered+1) {
        deliver_values(lmsg->iid);
    }
}

static void learner_handle_learner_msg(int fd, short event, void *arg) {
    socklen_t addrlen = sizeof(struct sockaddr);
    int msg_size = recvfrom(fd, lear_recv_buffer, MAX_UDP_MSG_SIZE, MSG_WAITALL, (struct sockaddr *)&lear_net_addr, &addrlen);
    
    if (msg_size < 0) {
        perror("recvfrom");
        return;
    }
    
    paxos_msg * pmsg = (paxos_msg*) lear_recv_buffer;
    if (pmsg->size != (msg_size - sizeof(paxos_msg))) {
        printf("Invalid paxos packet, size %d does not match packet size %lu\n", pmsg->size, (long unsigned)(msg_size - sizeof(paxos_msg)));
    }
    
    switch(pmsg->type) {
        case PAXOS_LEARN: {
            learner_handle_learn_msg((learn_msg*) pmsg->data);
        }
        break;
        
        default: {
            printf("Invalid packet type %d received from learners network\n", pmsg->type);
        }
    }
}

void ask_retransmission() {
    int i;
    learner_record* rec;
    paxos_msg* pmsg = (paxos_msg*)lear_send_buffer;
    pmsg->type = PAXOS_LSYNC;
    
    learner_sync_msg* lsync = (learner_sync_msg*)pmsg->data;
    lsync->count = 0;
    int buffer_size = sizeof(learner_sync_msg) + sizeof(paxos_msg);

    for(i = highest_delivered+1; i < highest_seen; i++) {
        rec = &learner_array[GET_LEA_INDEX(i)];
        
        //Not closed or never seen, request sync
        if((rec->iid == i && !is_closed(rec)) || rec->iid < i) {
            LOG(V_DBG, ("Adding %d to next lsync message\n", i));
            lsync->ids[lsync->count] = i;
            lsync->count++;
            buffer_size += sizeof(int);
            
            //Flush send buffer if full
            if((MAX_UDP_MSG_SIZE - buffer_size) < sizeof(int)) {
                pmsg->size = buffer_size - sizeof(paxos_msg);
                udp_send(send_socket, &acceptor_net_addr, lear_send_buffer, buffer_size);
                LOG(V_VRB, ("Requested %d lsyncs from %d to %d\n", lsync->count,  highest_delivered+1, highest_seen));
                lsync->count = 0;
                buffer_size = sizeof(learner_sync_msg) + sizeof(paxos_msg);
            }
        }
    }
    
    //Flush send buffer if not empty
    if(lsync->count > 0) {
        pmsg->size = buffer_size - sizeof(paxos_msg);
        udp_send(send_socket, &acceptor_net_addr, lear_send_buffer, buffer_size);
        LOG(V_VRB, ("Requested %d lsyncs from %d to %d\n", lsync->count,  highest_delivered+1, highest_seen));
    }
}

static void lsync_check(int fd, short event, void *arg) {
    if (highest_seen > highest_delivered) {
        ask_retransmission();
    }     
	event_add(&lsync_event, &lsync_interval);
}

static int libevent_start() {
    struct event_base* eb = event_init();
    if(eb == NULL) {
        printf("Error in libevent init\n");   
        return -1;
    }
    
    /* Initalize learner message event */
    int sock;
    sock = create_udp_socket(PAXOS_LEARNERS_NET, &lear_net_addr);
    if (sock < 0) {
        printf("Error creating learner socket\n");
        return -1;
    }
    LOG(V_VRB, ("Learner listening on socket %d\n", sock));
    event_set(&learner_msg_event, sock, EV_READ|EV_PERSIST, learner_handle_learner_msg, NULL);
    event_add(&learner_msg_event, NULL);

	/* Initalize lsync event */
	evtimer_set(&lsync_event, lsync_check, NULL);
	evutil_timerclear(&lsync_interval);
	lsync_interval.tv_sec = LEARNER_LSYNC_INTERVAL / 1000;
    lsync_interval.tv_usec = (LEARNER_LSYNC_INTERVAL % 1000) * 1000;
	event_add(&lsync_event, &lsync_interval);
	
    return 0;    
}


void learner_wait_ready() {
    pthread_mutex_lock(&ready_lock);
    
    while(!learner_ready) {
        pthread_cond_wait(&ready_cond, &ready_lock);
    }
    
    pthread_mutex_unlock(&ready_lock);
}

int learner_init(deliver_function f) {
    delfun = f;
    if(delfun == NULL) {
        printf("Error NULL callback!\n");
        return -1;
    }
    
    quorum =  1 + (int)((double)(N_OF_ACCEPTORS*2)/3);
    LOG(V_VRB, ("Quorum is %d (%d acceptors)\n", quorum, N_OF_ACCEPTORS));
    
    send_socket = create_udp_sender(PAXOS_ACCEPTORS_NET, &acceptor_net_addr);
    if (send_socket < 0) {
        printf("Error creating acceptor socket\n");
        return -1;
    }
    
    // libevent_init
    // Add Read from events (proposers and learners net)
    if(libevent_start() < 0) {
        printf("Error starting libevent\n");
        return -1;
    }
    
    // Signal client, learner is ready
    pthread_mutex_lock(&ready_lock);
    learner_ready = 1;
    pthread_cond_signal(&ready_cond);
    pthread_mutex_unlock(&ready_lock);

    // Start thread that calls event_dispatch()
    // and never returns
    LOG(1, ("Learner init complete\n"));
    event_dispatch();
    printf("Event loop terminated\n");
    return -1;
}

static void* start_learner (void * arg) {
    if(learner_init((deliver_function) arg) < 0) {
        printf("learner init failed!\n");
    }
    
    return NULL;
}

int learner_init_threaded(deliver_function f) {
    // Start learner (which starts event_dispatch())
    if (pthread_create(&learner_thread, NULL, start_learner, (void*) f) != 0) {
        perror("P: Failed to initialize learer thread");
        return -1;
    }
    
    learner_wait_ready();
    return 0;
}

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "event.h"
#include "paxos_udp.h"
#include "libpaxos_priv.h"


static struct sockaddr_in learner_net_addr;
static struct sockaddr_in proposer_net_addr;
static struct sockaddr_in acceptor_net_addr;

static int learner_socket;
static int proposer_socket;
static int acceptor_socket;

static struct event acceptor_msg_event;

typedef struct acceptor_record_t {
    int     iid;
    int     proposer_id;
    int     ballot;
    int     value_ballot;
    int     value_size;
    int     any_enabled;
    char*   value;
} acceptor_record;

#include "acceptor_disk_helpers.c"

static int acceptor_id;
static acceptor_record acceptor_array[ACCEPTOR_ARRAY_SIZE];
static int send_buffer_size;
static char send_buffer[MAX_UDP_MSG_SIZE];
static char recv_buffer[MAX_UDP_MSG_SIZE];

static int min_ballot = 2 * MAX_PROPOSERS;

int flush_send_buffer(int sock, struct sockaddr_in* addr, int msg_type) {
    int msg_size;
    paxos_msg* msg;
    
    msg = (paxos_msg*)send_buffer;
    msg_size = send_buffer_size;
    msg->size = msg_size - sizeof(paxos_msg);
    msg->type = msg_type;
    send_buffer_size = 0;
    
    LOG(V_DBG, ("Flushing acceptor send buffer, message size: %d\n", msg_size));
    return udp_send(sock, addr, send_buffer, msg_size);
}

int add_promise_to_send_buffer(acceptor_record* rec) {
    paxos_msg* msg;
    promise_msg* promise;
    promise_batch_msg* promise_batch;
    int promise_size = sizeof(promise_msg) + rec->value_size;

    msg = (paxos_msg*)send_buffer;
    promise_batch = (promise_batch_msg*)msg->data;

    // promise doesn't fit in buffer
    if (promise_size + send_buffer_size > MAX_UDP_MSG_SIZE) {
        disk_force_syncrony();
        flush_send_buffer(proposer_socket, &proposer_net_addr, PAXOS_PROMISE);
    }

    if (send_buffer_size == 0) {
        promise_batch->count = 0;
        promise_batch->acceptor_id = acceptor_id;
        send_buffer_size += (sizeof(paxos_msg) + sizeof(promise_batch_msg));
    }

    promise = (promise_msg*) &send_buffer[send_buffer_size];
    promise->iid = rec->iid;
    promise->ballot = rec->ballot;
    promise->value_ballot = rec->value_ballot;
    promise->value_size = rec->value_size;

    if (rec->value_size > 0) {
        memcpy(promise->value, rec->value, rec->value_size);
    }

    promise_batch->count++;
    send_buffer_size += promise_size;
    
    LOG(V_DBG, ("Promise for iid:%d added to buffer\n", promise->iid));

    return 1;
}

int handle_prepare(prepare_msg* msg) {
    int ret = 0;
    acceptor_record* rec;

    rec = &acceptor_array[GET_ACC_INDEX(msg->iid)];

    // Handle a new instance, 
    // possibly get rid of an old instance
    if (msg->iid > rec->iid) {
        rec->iid = msg->iid;
        rec->ballot = msg->ballot;
        rec->value_ballot = -1;
        rec->value_size = 0;
        if (rec->value != NULL) {
            free(rec->value);
        }
        rec->value = NULL;
        
        LOG(DBG, ("Promising for instance %d with ballot %d, never seen before\n", msg->iid, msg->ballot));
        disk_update_record(rec);
        return add_promise_to_send_buffer(rec);
    }

    //Handle a previously written instance
    if (msg->iid == rec->iid) {
        if (msg->ballot <= rec->ballot) {
            LOG(DBG, ("Ignoring prepare for instance %d with ballot %d, already promised to %d\n", msg->iid, msg->ballot, rec->ballot));
            return 1;
        }
        //Answer if ballot is greater then last one
        LOG(DBG, ("Promising for instance %d with ballot %d, previously promised to %d\n", msg->iid, msg->ballot, rec->ballot));
        rec->ballot = msg->ballot;
        disk_update_record(rec);
        return add_promise_to_send_buffer(rec);
    }

    //Record was overwritten in memory, retrieve from disk
    if (msg->iid < rec->iid) {
        rec = disk_lookup_record(msg->iid);
        if(rec == NULL) {
            //No record on disk
            rec = malloc(sizeof(acceptor_record));
            rec->iid           = msg->iid;
            rec->ballot        = -1;
            rec->value_ballot  = -1;
            rec->value_size    = 0;
            rec->value = NULL;            
        }
        
        if(msg->ballot > rec->ballot) {
            rec->ballot = msg->ballot;
            disk_update_record(rec);
            ret = add_promise_to_send_buffer(rec);
            
        } else {
            LOG(DBG, ("Ignoring prepare for instance %d with ballot %d, already promised to %d [info from disk]\n", msg->iid, msg->ballot, rec->ballot));
            ret = 0;
        }
    }

    if(rec != NULL) {
        if(rec->value != NULL) {
            free(rec->value);
        }
        free(rec);
    }

    return ret;
}

int handle_prepare_batch(prepare_batch_msg* bmsg) {
    int i, offset = 0;
    paxos_msg* msg;
    prepare_msg* pmsg;

    msg = (paxos_msg*)send_buffer;

    send_buffer_size = sizeof(paxos_msg) + sizeof(promise_batch_msg);
    
    ((promise_batch_msg*)msg->data)->count = 0;
    ((promise_batch_msg*)msg->data)->acceptor_id = acceptor_id;

    for(i = 0; i < bmsg->count; i++) {
        pmsg = (prepare_msg*) &bmsg->data[offset];
        handle_prepare(pmsg);
        offset += sizeof(prepare_msg);
    }

    //Flush if there is still something
    if (((promise_batch_msg*)msg->data)->count > 0) {
        disk_force_syncrony();
        flush_send_buffer(proposer_socket, &proposer_net_addr, PAXOS_PROMISE);
    }

    return 1;
}

void apply_accept(acceptor_record* rec, accept_msg* amsg) {    
    //Update record
    rec->iid = amsg->iid;
    rec->ballot = amsg->ballot;
    rec->value_ballot = amsg->ballot;
    rec->proposer_id = amsg->proposer_id;
    if (rec->value != NULL) {
        free(rec->value);
    }
    rec->value = malloc(amsg->value_size);
    memcpy(rec->value, amsg->value, amsg->value_size);
    rec->value_size = amsg->value_size;
    
    //Write to disk
    disk_update_record(rec);
    disk_force_syncrony();
    LOG(V_DBG, ("Accept for iid:%d applied\n", rec->iid));
}


void apply_anyval(acceptor_record* rec, int iid, int ballot) {
    //Update record
    rec->iid = iid;
    rec->ballot = ballot;
    rec->value_ballot = -1;
    rec->proposer_id = -1;
    if (rec->value != NULL) {
        free(rec->value);
    }
    rec->value = NULL;
    rec->value_size = 0;
    rec->any_enabled = 1;
    
    //Write to disk
    disk_update_record(rec);
    LOG(V_DBG, ("Anyval for iid:%d applied\n", rec->iid));
}

int send_learn_message(acceptor_record* rec) {
    paxos_msg* msg = (paxos_msg*)send_buffer;
    learn_msg* lm = (learn_msg*)msg->data;

    lm->acceptor_id = acceptor_id;
    lm->iid = rec->iid;
    lm->proposer_id = rec->proposer_id;
    lm->ballot = rec->ballot;
    lm->value_size = rec->value_size;
    memcpy(lm->value, rec->value, rec->value_size);
    send_buffer_size = sizeof(learn_msg) + sizeof(paxos_msg) + rec->value_size;
    
    LOG(V_DBG, ("Sending learn for iid:%d\n", rec->iid));

    return flush_send_buffer(learner_socket, &learner_net_addr, PAXOS_LEARN);
}

int handle_accept(accept_msg* amsg) {
    int ret;
    acceptor_record* rec;

    //Lookup
    rec = &acceptor_array[GET_ACC_INDEX(amsg->iid)];

    //Found record previously written
    if (amsg->iid == rec->iid) {
        if (rec->any_enabled || (amsg->ballot >= rec->ballot && amsg->ballot >= min_ballot)) {
            if (rec->any_enabled) {
                rec->any_enabled = 0;
                LOG(DBG, ("Accepting value for instance %d, any message enabled\n", amsg->iid));
            } else {
                LOG(DBG, ("Accepting value for instance %d with ballot %d\n", amsg->iid, amsg->ballot));
            }
            
            apply_accept(rec, amsg);
            return send_learn_message(rec);    
        }
        
        LOG(DBG, ("Ignoring value for instance %d with ballot %d.\n", amsg->iid, amsg->ballot));
        return 0;
    }

    //Record not found in acceptor array
    if (amsg->iid > rec->iid) {
        if (amsg->ballot >= min_ballot) {
            LOG(DBG, ("Accepting value instance %d with ballot %d, never seen before\n", amsg->iid, amsg->ballot));
            apply_accept(rec, amsg);
            return send_learn_message(rec);
        } else {
            LOG(DBG, ("Ignoring value for instance %d with ballot %d. Never seen before.\n", amsg->iid, amsg->ballot));
            return 0;
        }
    }

    //Record was overwritten in acceptor array
    //We must scan the logfile before accepting or not
    if (amsg->iid < rec->iid) {
        rec = disk_lookup_record(amsg->iid);
        if (rec == NULL) {
            if (amsg->ballot >= min_ballot) {
                LOG(DBG, ("Accepting value instance %d with ballot %d, never seen before [info from disk]\n", amsg->iid, amsg->ballot));
                rec = malloc(sizeof(acceptor_record));
                rec->value = NULL;
                apply_accept(rec, amsg);
                return send_learn_message(rec);
            } else {
                LOG(DBG, ("Ignoring value for instance %d with ballot %d. Never seen before (info from disk).\n", amsg->iid, amsg->ballot));
                return 0;
            }
        }
        
        if (rec->any_enabled || (amsg->ballot >= rec->ballot && amsg->ballot >= min_ballot)) {
            if (rec->any_enabled) {
                rec->any_enabled = 0;
                LOG(DBG, ("Accepting value for instance %d, any message enabled [info from disk]\n", amsg->iid));
            } else {
                LOG(DBG, ("Accepting value for instance %d with ballot %d [info from disk]\n", amsg->iid, amsg->ballot));
            }
            
            apply_accept(rec, amsg);
            ret = send_learn_message(rec);    
        } else {
            LOG(DBG, ("Ignoring accept for instance %d with ballot %d, already given to ballot %d [info from disk]\n", amsg->iid, amsg->ballot, rec->ballot));
            ret = 0;            
        }

        if(rec->value != NULL) {
            free(rec->value);
        }
        free(rec);
        
        return ret;
    }
    
    return 0;
}

int handle_lsync(int instance_id) {
    acceptor_record* rec;
    //Lookup
    rec = &acceptor_array[GET_ACC_INDEX(instance_id)];

    //Found record in memory
    if (instance_id == rec->iid) {
        //A value was accepted
        if (rec->value != NULL) {
            LOG(DBG, ("Answering to lsync for instance %d\n", instance_id));
            return send_learn_message(rec);
        }
        //No value was accepted
        LOG(DBG, ("Ignoring lsync for instance %d, record present but no value accepted\n", instance_id));
        return 0;
    }
    
    //Record was overwritten in acceptor array
    //We must scan the logfile before answering
    if (instance_id < rec->iid) {
        rec = disk_lookup_record(instance_id);
        //A record was found, we accepted something 
        if (rec != NULL) {
            LOG(DBG, ("Answering to lsync for instance %d [info from disk]\n", instance_id));
            send_learn_message(rec);
            free(rec->value);
            free(rec);
            return 1;
        } else {
            //Nothing from disk, nothing accepted
            LOG(DBG, ("Ignoring lsync for instance %d, no value accepted [info from disk]\n", instance_id));            
        }        
    }

    //Record not found in acceptor array
    //Nothing was accepted
    LOG(DBG, ("Ignoring lsync for instance %d, no info is available\n", instance_id));
    return 0;
    
}

int handle_batch_lsync(learner_sync_msg* lsmsg) {
    int i;
    for(i = 0; i < lsmsg->count; i++) {
        handle_lsync(lsmsg->ids[i]);
    }
    return 1;
}

int handle_anyval(int iid, int ballot) {    
    // found in array -> 
    // not in array
        // -> new
        // -> old
            // fetch from disk
            //  -> found
            //  -> not found
    
    // found  -> check ballot and enabled
    // not found -> return 0
    
    
    int ret;
    acceptor_record* rec;

    //Lookup
    rec = &acceptor_array[GET_ACC_INDEX(iid)];

    //Found record previously written
    if (iid == rec->iid) {
        if (rec->any_enabled || (ballot >= rec->ballot)) {
            LOG(DBG, ("Accepting anyval for instance %d with ballot %d\n", iid, ballot));
            apply_anyval(rec, iid, ballot);
            return 1;    
        }
        
        LOG(DBG, ("Ignoring anyval for instance %d with ballot %d.\n", iid, ballot));
        return 0;
    }

    //Record not found in acceptor array
    if (iid > rec->iid) {
        LOG(DBG, ("Accepting anyvalue instance %d with ballot %d, never seen before\n", iid, ballot));
        apply_anyval(rec, iid, ballot);
        return 1;
    }

    //Record was overwritten in acceptor array
    //We must scan the logfile before accepting or not
    if (iid < rec->iid) {
        rec = disk_lookup_record(iid);
        if (rec == NULL) {
            LOG(DBG, ("Accepting anyvalue instance %d with ballot %d, never seen before\n", iid, ballot));
            apply_anyval(rec, iid, ballot);
            return 1;
        }

        if (rec->any_enabled || (ballot >= rec->ballot)) {
            LOG(DBG, ("Accepting anyval for instance %d with ballot %d [info from disk]\n", iid, ballot));
            apply_anyval(rec, iid, ballot);
            ret = 1;
        } else {
            LOG(DBG, ("Ignoring anyval for instance %d with ballot %d, already given to ballot %d [info from disk]\n", iid, ballot, rec->ballot));
            ret = 0;            
        }

        if(rec->value != NULL) {
            free(rec->value);
        }
        free(rec);
        
        return ret;
    }
    
    return 0;   
}

int handle_batch_anyval(anyval_msg* avmsg) {
    int i;
    for(i = 0; i < avmsg->count; i++) {
        handle_anyval(avmsg->ids[i], avmsg->ballot);
    }
    return 1;
}

int handle_message(paxos_msg* msg) {

    switch(msg->type) {
        case PAXOS_PREPARE: {
            return handle_prepare_batch((prepare_batch_msg*) msg->data);
        }

        case PAXOS_ACCEPT: {
            return handle_accept((accept_msg*) msg->data);
        }

        case PAXOS_LSYNC: {
            return handle_batch_lsync((learner_sync_msg*) msg->data);
        }
        
        case PAXOS_ANYVAL: {
            return handle_batch_anyval((anyval_msg*) msg->data);
        }

        default: {
            printf("Unkown message type: %d.\n", msg->type);            
        }
        
    }
    return 1;
}


static void get_next_message(int fd, short event, void *arg) {
    socklen_t addrlen = sizeof(struct sockaddr_in);
    int msg_size = recvfrom(fd, recv_buffer, MAX_UDP_MSG_SIZE, MSG_WAITALL, (struct sockaddr *)&acceptor_net_addr, &addrlen);
    
    if (msg_size < 0) {
        perror("recvfrom");
        return;
    }
    
    paxos_msg * pmsg = (paxos_msg*)recv_buffer;
    LOG(V_DBG, ("Received %d bytes, packet size: %d type: %d\n", msg_size, pmsg->size, pmsg->type));
    if (pmsg->size != (msg_size - sizeof(paxos_msg))) {
        printf("Invalid paxos packet, size %d does not match packet size %lu\n", pmsg->size, (long unsigned)(msg_size - sizeof(paxos_msg)));
        return;
    }
    
    handle_message(pmsg);
}

static int libevent_start() {
    struct event_base* eb = event_init();
    if(eb == NULL) {
        printf("Error in libevent init\n");   
        return -1;
    }

    event_set(&acceptor_msg_event, acceptor_socket, EV_READ|EV_PERSIST, get_next_message, NULL);
    event_add(&acceptor_msg_event, NULL);

    return 0;
}

int acceptor_start(int id) {
    int i;
    
    char path_buffer[30];
    
    sprintf(path_buffer, "/tmp/acceptor_%d.bdb", id);
        
    acceptor_id = id;
    send_buffer_size = 0;
    
    LOG(DBG, ("Acceptor %d: Initializing permastorage\n", acceptor_id));
    i = disk_init_permastorage(path_buffer);
    if (i != 0) {
        printf("Permastorage initialization failed\n");
        return -1;
    }
    
    LOG(DBG, ("Acceptor %d: Initializing multicast\n", acceptor_id));
    learner_socket = create_udp_sender(PAXOS_LEARNERS_NET, &learner_net_addr);
    proposer_socket = create_udp_sender(PAXOS_PROPOSERS_NET, &proposer_net_addr);
    acceptor_socket = create_udp_socket (PAXOS_ACCEPTORS_NET, &acceptor_net_addr);

    if(learner_socket < 0 || proposer_socket < 0 || acceptor_socket < 0) {
        printf("Error: acceptor_init() failed\n");
        return -1;
    }

    for(i = 0; i < ACCEPTOR_ARRAY_SIZE; i++) {
        acceptor_array[i].iid = -1;
        acceptor_array[i].ballot = -1;
        acceptor_array[i].value_size = 0;
        acceptor_array[i].value = NULL;
    }
    
    // libevent_init
    // Add Read from events (proposers and learners net)
    if(libevent_start() < 0) {
        printf("Error starting libevent\n");
        return -1;
    }

    // Start thread that calls event_dispatch()
    // and never returns
    LOG(VRB, ("Acceptor %d: init completed\n", acceptor_id));
    event_dispatch();
    printf("Event loop terminated\n");
    return -1;
}

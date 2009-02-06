#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> 
#include <memory.h> 
#include <assert.h>

#include "event.h"

#include "libpaxos.h"
#include "libpaxos_priv.h"
#include "paxos_udp.h"

static pthread_mutex_t current_iid_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t value_delivered_cond = PTHREAD_COND_INITIALIZER;

static int fixed_ballot;
static int proposer_id;
static struct timeval proposer_to_interval;

//Lock
static int client_waiting = 0;
static int current_iid;
static int value_delivered;
static int last_accept_iid;
static int last_accept_hash;
//Lock

static char proposer_send_buffer[MAX_UDP_MSG_SIZE];
static int msg_size;
static paxos_msg * msg = (paxos_msg*)proposer_send_buffer;
static accept_msg * amsg = (accept_msg*)((paxos_msg*)proposer_send_buffer)->data;
static int send_socket;
static struct sockaddr_in acceptor_net_addr;
static int leader = 0;

typedef struct timeout_info_t {
    int instance_id;
    int hash;
    struct event to_event;
} timeout_info;
int proposer_is_leader() {
    return leader; 
}
static void proposer_check_timeout(int fd, short event, void *arg) {
    timeout_info * ti = arg;
    pthread_mutex_lock(&current_iid_lock);
    
    //Nothing new since our last accept, retry to send it
    if(client_waiting && ti->instance_id == last_accept_iid && ti->hash == last_accept_hash) {
        LOG(4, ("Instance %d, timed-out (%d times), re-sending accept\n", ti->instance_id, last_accept_hash));
        pthread_mutex_unlock(&current_iid_lock);
        last_accept_hash++;
        ti->hash = last_accept_hash;
        //Resend in same instance
        LOG(1, ("Proposer %d send accept message of size %d\n", proposer_id, msg_size));

        udp_send(send_socket, &acceptor_net_addr, proposer_send_buffer, msg_size);

        event_add(&ti->to_event, &proposer_to_interval);
    } else {
        LOG(4, ("Timeout out-of-date, removed\n"));
        //Something changed, stop
        // (decided or outdated timeout)
        pthread_mutex_unlock(&current_iid_lock);
        free(ti);
    }
}

void set_proposer_check_timeout(int instance_id, int hash) {
    timeout_info * ti = malloc(sizeof(timeout_info));
    ti->instance_id = instance_id;
    ti->hash = hash;
    evtimer_set(&ti->to_event, proposer_check_timeout, ti);
	event_add(&ti->to_event, &proposer_to_interval);
}
 
void proposer_deliver_callback(char * value, size_t size, int iid, int ballot, int proposer) {
    int just_accepted;
    
    if(proposer_is_leader()) {
        leader_deliver_value(value, size, iid, ballot, proposer);
    }
    
    pthread_mutex_lock(&current_iid_lock);
    
    just_accepted = current_iid;
    current_iid++;
    
    assert(just_accepted == iid);
    //No client values: Update current_iid only
    if(!client_waiting) {
        LOG(4, ("Value %d delivered, client is not even waiting\n", just_accepted));
        pthread_mutex_unlock(&current_iid_lock);
        return;
    }
    
    // Value delivered, my val?
    if(proposer_id == proposer) {
        LOG(4, ("Client value delivered (iid: %d) Wake up!.\n", just_accepted));
        //Wake up client
        client_waiting = 0;
        pthread_cond_signal(&value_delivered_cond);
        pthread_mutex_unlock(&current_iid_lock);
    } else {
        //Send for next instance
        LOG(4, ("Someone else's value delivered (iid: %d), try next instance.\n", current_iid));        
        pthread_mutex_unlock(&current_iid_lock);
        amsg->iid = current_iid;
        last_accept_iid = just_accepted;
        last_accept_hash++;
        udp_send(send_socket, &acceptor_net_addr, proposer_send_buffer, msg_size);
        
        //Set Timeout T
        set_proposer_check_timeout(amsg->iid, last_accept_hash);
    }
}

int proposer_init(int prop_id, int is_leader) {
    if(is_leader) {
        leader = 1;
    }
    proposer_id = prop_id;
    
    learner_init_threaded(proposer_deliver_callback);
    
    current_iid = 0;
    if (pthread_mutex_init(&current_iid_lock, NULL) != 0) {
        printf("Could not init lock!\n");
        return -1;
    }
    
    send_socket = create_udp_sender(PAXOS_ACCEPTORS_NET, &acceptor_net_addr);
    if (send_socket < 0) {
        printf("Error creating acceptor socket\n");
        return -1;
    }
    
    fixed_ballot = (MAX_PROPOSERS + proposer_id);

	evutil_timerclear(&proposer_to_interval);
	proposer_to_interval.tv_sec = (ACCEPT_TIMEOUT / 1000);
    proposer_to_interval.tv_usec = (ACCEPT_TIMEOUT % 1000) *1000;
    LOG(1, ("Proposer %d ready\n", proposer_id));
    
    if(is_leader) {
        if (leader_init(prop_id, send_socket, &acceptor_net_addr) < 0) {
            printf("Failed to start leader events\n");
            return -1;
        }
        
    }

    return 0;
}

void proposer_submit_value(char * value, size_t val_size) {
    int first_hash = 1;
    
    if (val_size > PAXOS_MAX_VALUE_SIZE) {
        printf("proposer_submit_value: value of size %d too big\n", (int)val_size);
        return;
    }
    
    pthread_mutex_lock(&current_iid_lock);
    amsg->iid = current_iid;
    last_accept_iid = current_iid;
    last_accept_hash = first_hash;
    value_delivered = 0;
    client_waiting = 1;
    pthread_mutex_unlock(&current_iid_lock);

    //Send accept for current instance
    msg->type = PAXOS_ACCEPT;
    msg->size = (sizeof(accept_msg) + val_size);
    amsg->ballot = fixed_ballot;
    amsg->value_size = val_size;
    amsg->proposer_id = proposer_id;
    memcpy(amsg->value, value, val_size);
    msg_size = msg->size + sizeof(paxos_msg);    
    udp_send(send_socket, &acceptor_net_addr, proposer_send_buffer, msg_size);
    LOG(1, ("Proposer %d sending first accept for instance %d\n", proposer_id, last_accept_iid));
    
    //Set timeout T with arg &ti
    set_proposer_check_timeout(amsg->iid, first_hash);
    pthread_mutex_lock(&current_iid_lock);
    
    //Wait until value is delivered in some instance
    while(client_waiting) {
        pthread_cond_wait( &value_delivered_cond, &current_iid_lock );
    }
    pthread_mutex_unlock(&current_iid_lock);   
}

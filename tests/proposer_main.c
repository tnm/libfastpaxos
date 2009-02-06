#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/time.h>
#include "libpaxos.h"

#define MAX_VALUE_SIZE 8192

static int proposer_id = 1;
static int values_to_propose = 50000;
static int value_size = 400;
static char buffer[MAX_VALUE_SIZE];
static int measure_avg_time = 0;

void pusage() {
    printf("-i N : Proposer id\n");
    printf("-k N : Exit after proposing N values\n");
    printf("-s N : Propose values of size N (max %d)", MAX_VALUE_SIZE);
    printf("-r   : Measure average time to propose a values\n");
    printf("-h   : prints this message\n");
}

void parse_args(int argc, char * const argv[]) {
    int c;
    while((c = getopt(argc, argv, "i:k:s:rh")) != -1) {
        switch(c) {
            case 'i': proposer_id = atoi(optarg);
            break;
            case 'k': values_to_propose = atoi(optarg);
            break;
            case 's': value_size = atoi(optarg); 
            break;
            case 'r': measure_avg_time = 1;
            break;
            case 'h':
            default: {
                pusage();
                exit(0);
            }
        }
    }
}

void generate_random_val(char * buf, int size) {
    int r = (random() % (126 - 33)) + 33;
    memset(buf, r, size);
    buf[size-1] = '\0';
}

/*
    t2 - t1 in microseconds
*/
int elapsed_time(struct timeval* t1, struct timeval* t2) {
    int us;
    us = (t2->tv_sec - t1->tv_sec) * 1000000;
    us += (t2->tv_usec - t1->tv_usec);
    return us;
}

void propose_values() {
    int i;
    for (i = 0; i < values_to_propose; i++) {
        generate_random_val(buffer, value_size);
        proposer_submit_value(buffer, value_size);
        
        if ((i % (values_to_propose/10)) == 0) {
            printf("Proposed %d values\n", i);
        }
    }
    
    printf("Proposed %d values\n", i);
}

void propose_value_instrumented() {
    int i;
    int rt;
    int tot = 0;
    int min = INT_MAX;
    int max = 0;
    struct timeval t1, t2;
    
    for (i = 0; i < values_to_propose; i++) {
        generate_random_val(buffer, value_size);
        
        gettimeofday(&t1, NULL);
        proposer_submit_value(buffer, value_size);
        gettimeofday(&t2, NULL);
        rt = elapsed_time(&t1, &t2);
        if (rt < min)
            min = rt;
        if (rt > max)
            max = rt;
        tot += rt;
        
        if ((i % (values_to_propose/10)) == 0) {
            printf("Proposed %d values\n", i);
        }
    }
    
    printf("Proposed %d values\n", i);
    printf("Min: %d\n", min);
    printf("Max: %d\n", max);
    printf("Avg: %d\n", tot / values_to_propose);
}

int main (int argc, char const *argv[]) {
    parse_args(argc, (char **)argv);
    
    printf("Proposer %d starting\n", proposer_id);    

    if (proposer_init(proposer_id, 0) < 0) {
        printf("proposer init failed!\n");
        exit(1);
    }
    
    if (measure_avg_time) {
        propose_value_instrumented();
    } else {
        propose_values();
    }
    
    return 0;
}
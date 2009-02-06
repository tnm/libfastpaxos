#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <math.h>

#include "libpaxos.h"

void handle_cltr_c (int sig) {
    printf("Caught exit signal\n");
    exit (0);
}

//Params
int exit_after = 100;
int print_multiple = 1;
int print_rate = 1;
int mute = 0;
time_t start_time;

//Priv
int learn_count = 0;
                
void pusage() {
    printf("-k N : Exits after learning N values\n");
    printf("-p N : Prints updates every N messages (default:1 = print all)\n");
    printf("-r   : Do not print the rate when exiting\n");
    printf("-m   : Mute (overrides -p)\n");
    printf("-h   : prints this message\n");
}

void parse_args(int argc, char * const argv[]) {
    int c;
    while((c = getopt(argc, argv, "k:p:mrh")) != -1) {
        switch(c) {
            case 'k': {
                exit_after = atoi(optarg);
            }
            break;

            case 'p': {
                print_multiple = atoi(optarg);
            }
            break;

            case 'm': {
                mute = 1;
            }
            break;
            
            case 'r': {
                print_rate = 0;
            }
            break;
            
            case 'h':
            default: {
                pusage();
                exit(0);
            }
        }
    }
}

int last_instance = -1;
void deliver(char * value, size_t size, int iid, int ballot, int proposer) {
    last_instance++;
    assert(iid == last_instance);
    
    if(size < 0) {
        printf("Deliver failed for instance %d\n", last_instance);
    } else {
        learn_count++;
        if(!mute && ((learn_count % print_multiple) == 0)) {
            printf("%d, %d, %s, %d\n", iid, proposer, value, (int)size);
        }

    }
    if(learn_count >= exit_after) {
        if(print_rate) {            
            time_t end_time = time(NULL);
            time_t seconds = end_time - start_time;
            float rate = ((float)learn_count)/seconds;
            printf("%d values in %d seconds -> %f VPS!\n",learn_count, (int)seconds, rate);
        }
        exit(0);
    }

}

int main (int argc, char const *argv[]) {
    signal (SIGINT, handle_cltr_c);
    parse_args(argc, (char **)argv);
    
    if(!mute) {
        printf("iid, proposer id, value, size\n");
    }
    
    start_time = time(NULL);
    
    if(learner_init(deliver) < 0) {
        printf("learner init failed!\n");
    }
    
    return 0;
}

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
     
#include "libpaxos.h"
#include "libpaxos_priv.h"

int acceptor_id = 0;

void handle_cltr_c (int sig) {
	LOG (VRB, ("Caught exit signal\n"));
    exit (0);
}

void pusage() {
    printf("-i N : starts acceptor with id N (default is 0)\n");

    printf("-h   : prints this message and exits\n");
}

void parse_args(int argc, char * const argv[]) {
    int c;
    while((c = getopt(argc, argv, "i:h")) != -1) {
        switch(c) {            
            case 'i': {
                acceptor_id = atoi(optarg);
                printf("Acceptor_id is:%d\n", acceptor_id);
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


int main (int argc, char const *argv[]) {    
    signal (SIGINT, handle_cltr_c);
    
    parse_args(argc, (char **)argv);
    
    if (acceptor_start(acceptor_id) < 0) {
        printf("Error while starting acceptor %d\n", acceptor_id);
    }
    
    return 0;
}


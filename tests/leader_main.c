#include <stdio.h>
#include <unistd.h>
#include "libpaxos.h"

int main (int argc, char const *argv[]) {
    int id = 0;
    if(proposer_init(id, 1) < 0)     {
        printf("leader init failed!\n");
    }
    while(1) {
        sleep(1);
    }
    return 0;
}
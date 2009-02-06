#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

int create_udp_socket (char* ip_addr_string, int port, struct sockaddr_in * addr){
    struct ip_mreq mreq;
    int sock;
    
    /*Clear structures*/
    memset(&mreq, '\0', sizeof(struct ip_mreq));
    /* Set up socket */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("receiver socket");
        return -1;
    }

    /* Set to reuse address */	
    int activate = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &activate, sizeof(int)) != 0) {
        perror("setsockopt, setting SO_REUSEADDR");
        return -1;
    }

    /* Set up membership to group */
    mreq.imr_multiaddr.s_addr = inet_addr(ip_addr_string);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    activate = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    if (activate != 0) {
        perror("setsockopt, setting IP_ADD_MEMBERSHIP");
        return -1;
    }

    /* Set up address */
    addr->sin_addr.s_addr = inet_addr(ip_addr_string);
    if (addr->sin_addr.s_addr == INADDR_NONE) {
        printf("Error setting receiver->addr\n");
        return -1;
    }
    addr->sin_family = AF_INET;
    addr->sin_port = htons((uint16_t)port);   

    /* Bind the socket */
    if (bind(sock, (struct sockaddr *) addr, sizeof(struct sockaddr_in)) != 0) {
        perror("bind");
        return -1;
    }

    /* Set non-blocking */
    int flag = fcntl(sock, F_GETFL);
    if(flag < 0) {
        perror("fcntl1");
        return -1;
    }

    flag |= O_NONBLOCK;
    if(fcntl(sock, F_SETFL, flag) < 0) {
        perror("fcntl2");
        return -1;
    }

    return sock;

}

int create_udp_sender(char* ip_addr_string, int port, struct sockaddr_in * addr) {
    int sock;
    int addrlen;
    
    /* Set up socket */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("sender socket");
        return -1;
    }
    
    /* Set up address */
    memset(addr, '\0', sizeof(struct sockaddr_in));
    addr->sin_addr.s_addr = inet_addr(ip_addr_string);
    if (addr->sin_addr.s_addr == INADDR_NONE) {
        printf("Error setting addr\n");
        return -1;
    }
    addr->sin_family = AF_INET;
    addr->sin_port = htons((uint16_t)port);	
    addrlen = sizeof(struct sockaddr_in);
    
    /* Set non-blocking */
    int flag = fcntl(sock, F_GETFL);
    if(flag < 0) {
        perror("fcntl1");
        return -1;
    }
    
    flag |= O_NONBLOCK;
    if(fcntl(sock, F_SETFL, flag) < 0) {
        perror("fcntl2");
        return -1;
    }

    return sock;
}


int udp_send(int sock, struct sockaddr_in* addr, char* msg, int size) {
    int cnt = -1;
    cnt = sendto(sock, msg, size, 0, (struct sockaddr *)addr, sizeof(struct sockaddr_in));
    if (cnt != size || cnt == -1) {
        perror("paxnet_sendmsg");
        return -1;
    }
    return cnt;
}

#ifndef PAXOS_UDP_H_HWWT1IRX
#define PAXOS_UDP_H_HWWT1IRX

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

int create_udp_socket (char* ip_addr_string, int port, struct sockaddr_in * addr);
int create_udp_sender (char* ip_addr_string, int port, struct sockaddr_in * addr);
int udp_send(int sock, struct sockaddr_in * addr, char* msg, int size);

#endif /* end of include guard: PAXOS_UDP_H_HWWT1IRX */

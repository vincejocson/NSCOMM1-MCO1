#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

// winsock headers
#include <winsock2.h>
#include <ws2tcpip.h>

#define PORT 8080
#define MAX_PAYLOAD_SIZE 1024
#define MAX_RETRIES 5
#define TIMEOUT_MS 1000 

// message types
#define TYPE_SYN 0
#define TYPE_ACK 1
#define TYPE_DATA 2
#define TYPE_FIN 3
#define TYPE_ERROR 4

// operations
#define OP_DOWNLOAD 0
#define OP_UPLOAD 1

// main format for messages
typedef struct {
    uint8_t type;               // SYN, DATA, ACK, FIN, ERROR
    uint32_t session_id;        
    uint32_t seq_num;           
    uint16_t length;            
    char payload[MAX_PAYLOAD_SIZE]; 
} Packet;

// struct for payload params
typedef struct {
    uint8_t operation;          
    uint32_t filesize;          
    char filename[256];      
} SynPayload;

// error handler
void die(const char *s) {
    printf("%s. Error Code : %d\n", s, WSAGetLastError());
    exit(1);
}

#endif

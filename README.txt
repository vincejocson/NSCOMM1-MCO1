## Implementation of Reliable Data Transfer over UDP
A custom application-layer protocol providing TCP-like reliability, sequencing, and session management over UDP for binary-safe file 
transfers, including a comprehensive mini-RFC documenting the protocol’s state machines, packet formats, and error-handling mechanisms.

## Operation
gcc client.c -o client.exe -lws2_32
gcc server.c -o server.exe -lws2_32

type this on the server cmd
server.exe

and this on the client cmd
client.exe 127.0.0.1 UPLOAD/DOWNLOAD file.extension

To test with real files:
when uploading, place real files in the client folder 
when downloading, place real files in the server folder

toggle code: Uncomment when demonstrating error handling

				/* --- inject this toggle for packet loss simulation --- 
                static int dropped_once = 0;
                if (data_pkt.seq_num == 1004 && !dropped_once && retries == 0) {
                    printf("[!] SIMULATING PACKET LOSS: Dropping Sequence 1004 (Attempt 1)\n");
                    dropped_once = 1;
                    // We intentionally skip the sendto() here to simulate the network eating the packet
                } else {
                    // normal operation
                    sendto(sockfd, (char*)&data_pkt, sizeof(Packet), 0, (struct sockaddr *)&server_addr, slen);
                }
                
                if (recvfrom(sockfd, (char*)&ack_pkt, sizeof(Packet), 0, NULL, NULL) != SOCKET_ERROR) {
                    if (ack_pkt.type == TYPE_ACK && ack_pkt.seq_num == data_pkt.seq_num + 1) acked = 1;
                } else {
                    retries++;
                    printf("[!] Timeout hit! Retransmitting sequence %u (Attempt %d)\n", data_pkt.seq_num, retries + 1);
                }
                /* -------------------------------------------- */

## Contributors
1. Vince Jocson
2. Benjamin Barlaaan

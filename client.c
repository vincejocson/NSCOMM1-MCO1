#include "protocol.h"

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <server_ip> <UPLOAD/DOWNLOAD> <filename>\n", argv[0]);
        exit(1);
    }

    char *server_ip = argv[1];
    char *operation_str = argv[2];
    char *filename = argv[3];
    uint8_t operation = (strcmp(operation_str, "UPLOAD") == 0) ? OP_UPLOAD : OP_DOWNLOAD;

    WSADATA wsa;
    SOCKET sockfd;
    struct sockaddr_in server_addr;
    int slen = sizeof(server_addr);
    Packet send_pkt, recv_pkt;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Failed. Error Code : %d", WSAGetLastError());
        return 1;
    }

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
        die("socket()");
    }

    memset((char *) &server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    DWORD timeout = TIMEOUT_MS;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    srand((unsigned int)time(NULL));
    uint32_t my_session_id = rand();
    uint32_t my_seq_num = 1000;

    // 1. session setup
    send_pkt.type = TYPE_SYN;
    send_pkt.session_id = my_session_id;
    send_pkt.seq_num = my_seq_num;
    send_pkt.length = sizeof(SynPayload);

    SynPayload syn_data;
    syn_data.operation = operation;
    strcpy(syn_data.filename, filename);
    memcpy(send_pkt.payload, &syn_data, sizeof(SynPayload));

    int retries = 0;
    int session_established = 0;

    // send syn and wait for ack
    while (retries < MAX_RETRIES && !session_established) {
        printf("Initiating session (Attempt %d)...\n", retries + 1);
        sendto(sockfd, (char*)&send_pkt, sizeof(Packet), 0, (struct sockaddr *)&server_addr, slen);

        if (recvfrom(sockfd, (char*)&recv_pkt, sizeof(Packet), 0, NULL, NULL) != SOCKET_ERROR) {
            if (recv_pkt.type == TYPE_ERROR) {
                printf("Server Error: %s\n", recv_pkt.payload);
                closesocket(sockfd);
                WSACleanup();
                exit(1);
            }
            if (recv_pkt.type == TYPE_ACK && recv_pkt.session_id == my_session_id && recv_pkt.seq_num == my_seq_num + 1) {
                session_established = 1;
                printf("Session established!\n");
            }
        } else {
            retries++;
        }
    }

    if (!session_established) {
        printf("Fatal: Server unreachable.\n");
        closesocket(sockfd);
        WSACleanup();
        exit(1);
    }

    // 2. transferring
    // upload
    if (operation == OP_UPLOAD) {
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "ClientFiles/%s", filename);
        FILE *file = fopen(filepath, "rb");
        if (!file) {
            printf("Error: Local file not found at %s\n", filepath);
            closesocket(sockfd);
            WSACleanup();
            exit(1);
        }

        Packet data_pkt, ack_pkt;
        data_pkt.type = TYPE_DATA;
        data_pkt.session_id = my_session_id;
        data_pkt.seq_num = my_seq_num + 2;
        size_t bytes_read;

        while ((bytes_read = fread(data_pkt.payload, 1, MAX_PAYLOAD_SIZE, file)) > 0) {
            data_pkt.length = (uint16_t)bytes_read;
            int acked = 0;
            retries = 0;

            while (retries < MAX_RETRIES && !acked) {
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
                
                sendto(sockfd, (char*)&data_pkt, sizeof(Packet), 0, (struct sockaddr *)&server_addr, slen);

                if (recvfrom(sockfd, (char*)&ack_pkt, sizeof(Packet), 0, NULL, NULL) != SOCKET_ERROR) {
                    if (ack_pkt.type == TYPE_ACK && ack_pkt.seq_num == data_pkt.seq_num + 1) acked = 1;
                } else {
                    retries++;
                    printf("[!] Timeout hit! Retransmitting sequence %u (Attempt %d)\n", data_pkt.seq_num, retries + 1);
                }
            }
            if (!acked) {
                printf("Connection lost during transfer\n");
                fclose(file);
                closesocket(sockfd);
                WSACleanup();
                exit(1);
            }
            data_pkt.seq_num += 2;
        }
        fclose(file);

        // terminate protocol
        data_pkt.type = TYPE_FIN;
		data_pkt.length = 0;
		data_pkt.seq_num += 2;
		
		int fin_acked = 0;
		int retries = 0;
		
		while (retries < MAX_RETRIES && !fin_acked) {
		    sendto(sockfd, (char*)&data_pkt, sizeof(Packet), 0, (struct sockaddr *)&server_addr, slen);
		    if (recvfrom(sockfd, (char*)&ack_pkt, sizeof(Packet), 0, NULL, NULL) != SOCKET_ERROR) {
		        if (ack_pkt.type == TYPE_ACK && ack_pkt.seq_num == data_pkt.seq_num + 1) {
		            fin_acked = 1;
		        }
		    } else {
		        retries++;
		    }
		}
		
		if (!fin_acked) {
		    printf("Warning: Did not receive ACK for FIN. Closing anyway.\n");
		} else {
		    printf("Upload complete. Session cleanly closed.\n");
		}

    } else if (operation == OP_DOWNLOAD) {
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "ClientFiles/%s", filename);
        FILE *file = fopen(filepath, "wb");
        if (!file) {
            printf("Error: Cannot create local file at %s\n", filepath);
            closesocket(sockfd);
            WSACleanup();
            exit(1);
        }

        uint32_t expected_seq = my_seq_num + 2;
        
        int timeout_count = 0;

        while (1) {
            if (recvfrom(sockfd, (char*)&recv_pkt, sizeof(Packet), 0, NULL, NULL) != SOCKET_ERROR) {
        		timeout_count = 0;
                if (recv_pkt.session_id != my_session_id) continue;

                if (recv_pkt.type == TYPE_FIN) {
				    send_pkt.type = TYPE_ACK;
				    send_pkt.seq_num = recv_pkt.seq_num + 1;
				    sendto(sockfd, (char*)&send_pkt, sizeof(Packet), 0, (struct sockaddr *)&server_addr, slen); //
				    printf("File received. Entering TIME_WAIT state...\n");
				    
				    int time_wait_loops = 3; 
				    while(time_wait_loops > 0) {
				        if (recvfrom(sockfd, (char*)&recv_pkt, sizeof(Packet), 0, NULL, NULL) != SOCKET_ERROR) {
				            if (recv_pkt.type == TYPE_FIN) {
				                sendto(sockfd, (char*)&send_pkt, sizeof(Packet), 0, (struct sockaddr *)&server_addr, slen);
				            }
				        }
				        time_wait_loops--;
				    }
				    
				    printf("Session successfully closed.\n");
				    break;
				}

                if (recv_pkt.type == TYPE_DATA && recv_pkt.seq_num == expected_seq) {
                    fwrite(recv_pkt.payload, 1, recv_pkt.length, file);
                    expected_seq += 2;
                }

                send_pkt.type = TYPE_ACK;
                send_pkt.session_id = my_session_id;
                send_pkt.seq_num = expected_seq - 1;
                sendto(sockfd, (char*)&send_pkt, sizeof(Packet), 0, (struct sockaddr *)&server_addr, slen);
            }
            else {
		        // catch timeout
		        timeout_count++;
		        if (timeout_count >= MAX_RETRIES) {
		            printf("Error: Server unresponsive during download. Aborting.\n");
		            break; // break error
		        }
		    }
        }
        fclose(file);
    }

    closesocket(sockfd);
    WSACleanup();
    return 0;
}

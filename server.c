#include "protocol.h"

int main() {
    WSADATA wsa;
    SOCKET sockfd;
    struct sockaddr_in server_addr, client_addr;
    int slen = sizeof(client_addr);
    Packet recv_pkt, send_pkt;

    // 1. initialize winsock
    printf("Initializing Winsock...\n");
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Failed. Error Code : %d", WSAGetLastError());
        return 1;
    }

    // 2. udp socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
        die("Could not create socket");
    }

    memset((char *)&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // 3. bind
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        die("Bind failed");
    }

    // 4. set receive timeout
    DWORD timeout = TIMEOUT_MS;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    printf("Server listening on port %d...\n", PORT);

    uint32_t active_session = 0;
    int session_active = 0;

    while (1) {
        if (recvfrom(sockfd, (char*)&recv_pkt, sizeof(Packet), 0, (struct sockaddr *)&client_addr, &slen) != SOCKET_ERROR) {

            // establish session
            if (recv_pkt.type == TYPE_SYN && !session_active) {
                SynPayload *syn_data = (SynPayload *)recv_pkt.payload;
                active_session = recv_pkt.session_id;
                session_active = 1;
                uint32_t seq = recv_pkt.seq_num;

                printf("\n--- New Session [%u] ---\n", active_session);

                if (syn_data->operation == OP_DOWNLOAD) {
                    printf("Client requested DOWNLOAD of: %s\n", syn_data->filename);  
                    char filepath[512];
                    snprintf(filepath, sizeof(filepath), "ServerFiles/%s", syn_data->filename);
                    FILE *file = fopen(filepath, "rb");

                    if (!file) {
                        printf("File not found. Sending ERROR.\n");
                        send_pkt.type = TYPE_ERROR;
                        send_pkt.session_id = active_session;
                        strcpy(send_pkt.payload, "File not found on server.");
                        sendto(sockfd, (char*)&send_pkt, sizeof(Packet), 0, (struct sockaddr *)&client_addr, slen);
                        session_active = 0;
                        continue;
                    }

                    // send ack
                    send_pkt.type = TYPE_ACK;
                    send_pkt.session_id = active_session;
                    send_pkt.seq_num = seq + 1;
                    sendto(sockfd, (char*)&send_pkt, sizeof(Packet), 0, (struct sockaddr *)&client_addr, slen);

                    // rft send data
                    Packet data_pkt, ack_pkt;
                    data_pkt.type = TYPE_DATA;
                    data_pkt.session_id = active_session;
                    data_pkt.seq_num = seq + 2;
                    size_t bytes_read;

                    while ((bytes_read = fread(data_pkt.payload, 1, MAX_PAYLOAD_SIZE, file)) > 0) {
                        data_pkt.length = (uint16_t)bytes_read;
                        int retries = 0;
                        int acked = 0;

                        while (retries < MAX_RETRIES && !acked) {
                            sendto(sockfd, (char*)&data_pkt, sizeof(Packet), 0, (struct sockaddr *)&client_addr, slen);
                            if (recvfrom(sockfd, (char*)&ack_pkt, sizeof(Packet), 0, NULL, NULL) != SOCKET_ERROR) {
                                if (ack_pkt.type == TYPE_ACK && ack_pkt.session_id == active_session && ack_pkt.seq_num == data_pkt.seq_num + 1) {
                                    acked = 1;
                                }
                            } else {
                                retries++;
                            }
                        }
                        if (!acked) { printf("Client unresponsive. Dropping session.\n"); break; }
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
					    sendto(sockfd, (char*)&data_pkt, sizeof(Packet), 0, (struct sockaddr *)&client_addr, slen); // Use &client_addr in server.c
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
					    printf("File transfer complete. Session cleanly closed.\n");
					}
                    session_active = 0;
                }
                else if (syn_data->operation == OP_UPLOAD) {
                    printf("Client initiated UPLOAD for: %s\n", syn_data->filename);
                    char filepath[512];
                    snprintf(filepath, sizeof(filepath), "ServerFiles/%s", syn_data->filename);
                    FILE *file = fopen(filepath, "wb");

                    // accept data
                    send_pkt.type = TYPE_ACK;
                    send_pkt.session_id = active_session;
                    send_pkt.seq_num = seq + 1;
                    sendto(sockfd, (char*)&send_pkt, sizeof(Packet), 0, (struct sockaddr *)&client_addr, slen);

                    uint32_t expected_seq = seq + 2;

                    // rft send data
                    int timeout_count = 0;
                    while (1) {
                        if (recvfrom(sockfd, (char*)&recv_pkt, sizeof(Packet), 0, (struct sockaddr *)&client_addr, &slen) != SOCKET_ERROR) {
        					timeout_count = 0;
                            if (recv_pkt.session_id != active_session) continue; 
                            if (recv_pkt.type == TYPE_FIN) {
							    send_pkt.type = TYPE_ACK;
							    send_pkt.seq_num = recv_pkt.seq_num + 1;
							    sendto(sockfd, (char*)&send_pkt, sizeof(Packet), 0, (struct sockaddr *)&client_addr, slen);
							    printf("File received. Entering TIME_WAIT state...\n");
							    
							    int time_wait_loops = 3; 
							    while(time_wait_loops > 0) {
							        if (recvfrom(sockfd, (char*)&recv_pkt, sizeof(Packet), 0, NULL, NULL) != SOCKET_ERROR) {
							            if (recv_pkt.type == TYPE_FIN) {
							                sendto(sockfd, (char*)&send_pkt, sizeof(Packet), 0, (struct sockaddr *)&client_addr, slen);
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

                            // send ack
                            send_pkt.type = TYPE_ACK;
                            send_pkt.session_id = active_session;
                            send_pkt.seq_num = expected_seq - 1;
                            sendto(sockfd, (char*)&send_pkt, sizeof(Packet), 0, (struct sockaddr *)&client_addr, slen);
                        }
                        else {
					        // catch timeout
					        timeout_count++;
					        if (timeout_count >= MAX_RETRIES) {
					            printf("Error: Client unresponsive during upload. Aborting session.\n");
					            break; // exit loop
					        }
					    }
                    }
                    fclose(file);
                    session_active = 0;
                }
            }
        }
    }

    closesocket(sockfd);
    WSACleanup();
    return 0;
}

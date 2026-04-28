#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

struct ClientInfo {
    int fd;
    int num_topic;
    int state[64];
    char topic[64][64];
};

void removeClient(struct ClientInfo client[], int *num_client, int index) {
    close(client[index].fd);
    for (int i = index; i < *num_client - 1; i++) {
        client[i] = client[i+1];
    }
    (*num_client) --;
}

void removeTopic(struct ClientInfo client[],int index, int pos) {
    for (int i = pos; i < client[index].num_topic - 1; i++) {
        strcpy(client[index].topic[i],client[index].topic[i + 1]);
    }
    client[index].num_topic --;
}
                
int main() {
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) {
        perror("socket() failed.\n");
        exit(1);
    }

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(9000);

    if (bind(listener, (struct sockaddr*) &addr, sizeof (addr))) {
        perror("bind() failed.\n");
        exit(1);
    }

    if (listen(listener, 10) == -1) {
        perror("listen() failed.\n");
        exit(1);
    }

    printf("Server is listening on port 9000.\n");

    struct ClientInfo client[64];
    for (int i = 0; i < 63; i++) {
        client[i].num_topic = 0;
    }
    struct timeval tv;
    fd_set fdread;
    int num_client = 0;
    char buf[2048];

    while (1) {
        FD_ZERO(&fdread);
        FD_SET(listener, &fdread);
        
        int maxdp = listener + 1;
        for (int i = 0; i < num_client; i++) {
            FD_SET(client[i].fd, &fdread);
            if (client[i].fd + 1 > maxdp) maxdp = client[i].fd + 1;
        }

        tv.tv_sec = 5;
        tv.tv_usec = 0;

        int ret = select(maxdp, &fdread, NULL, NULL, &tv);
        if (ret < 0) {
            perror("select() failed.\n");
            exit(1);
        }

        if (ret == 0) {
            continue;
        }

        if (FD_ISSET(listener, &fdread)) {
            client[num_client].fd = accept(listener, NULL, NULL);
            printf("Client %d connected.\n", client[num_client].fd);
            client[num_client].state[client[num_client].num_topic] = 0;
            client[num_client].num_topic = 0;
            char* ask = "Please subscribe to a topic: SUB <topic>.\n";
            send(client[num_client].fd, ask, strlen(ask), 0);
            num_client++;
        }

        char topic[64];

        for (int i = 0; i < num_client; i++) {
            if (FD_ISSET(client[i].fd, &fdread)) {
                ret = recv(client[i].fd, buf, sizeof(buf) - 1, 0);
                
                if (ret <= 0) {
                    printf("Client %d disconnected.\n", client[i].fd);
                    removeClient(client, &num_client, i);
                    i--;
                    continue;
                }
                
                buf[ret] = 0;
                buf[strcspn(buf, "\r\n")] = 0;

                if (strncmp(buf, "SUB ", 4) == 0) {
                    if (client[i].state[client[i].num_topic] == 0) {
                        if (sscanf(buf + 4, "%s", topic) == 1) {
                            strcpy(client[i].topic[client[i].num_topic], topic);
                            client[i].state[client[i].num_topic] = 1;
                            client[i].num_topic ++;
                            char mes[2048];
                            snprintf(mes, sizeof (mes), "Successfully subscribed to topic: %s.\n", topic);
                            send(client[i].fd, mes, strlen(mes), 0);
                        }  
                        else {
                            char *mes = "Syntax error. Must be: SUB <topic>!\n";
                            send(client[i].fd, mes, strlen(mes), 0);
                        }
                    }
                }
                else if (strncmp(buf, "UNSUB ", 6) == 0) {
                    if (sscanf(buf + 6, "%s", topic) == 1) {
                        int pos = -1;
                        for (int p = 0; p < client[i].num_topic; p++) {
                            if (strcmp(topic, client[i].topic[p]) == 0) {
                                pos = p;
                                break;
                            }
                        }
                        if (pos != -1) {
                            removeTopic(client, i, pos);
                            char mes[2048];
                            snprintf(mes, sizeof(mes), "Successfully unsubsribed to topic %s.\n", topic);
                            send(client[i].fd, mes, strlen(mes), 0);
                        }
                        else {
                            char *mes = "Cannot find topic to unsubscribe. Please retry.\n";
                            send(client[i].fd, mes, strlen(mes), 0);
                        }
                    }
                    else {
                        char *mes = "Syntax error. Must be: UNSUB <topic>!\n";
                        send(client[i].fd, mes, strlen(mes), 0);
                    }
                }
                else if (strncmp(buf, "PUB ", 4) == 0) {
                    char msg[2048];
                    if (sscanf(buf + 4, "%s %[^\n]", topic, msg) == 2) {
                        for (int j = 0; j < num_client; j++) {
                            for (int p = 0; p < client[j].num_topic; p++) {
                                if (strcmp(client[j].topic[p], topic) == 0) {
                                    if (client[j].state[p] == 1) {
                                        char mes[5000];
                                        snprintf(mes, sizeof(mes), "%s: %s\n", topic, msg);
                                        send(client[j].fd, mes, strlen(mes), 0);
                                    }
                                }
                            }
                        }
                    }
                    else {
                        char *mes = "Syntax error. Must be: PUB <topic> <msg>!\n";
                        send(client[i].fd, mes, strlen(mes), 0);
                    }
                }
                else {
                    char *mes = "Syntax error. Must be: SUB <topic> or UNSUB <topic> or PUB <topic> <msg>!\n";
                    send(client[i].fd, mes, strlen(mes), 0);
                }
            }
        }
    }
    close(listener);
    return 0;
}
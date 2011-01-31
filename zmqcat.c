
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include <zmq.h>

#include "sds.h"
#include "sdsutils.h"

#define USAGE           "Usage: zmqcat -t [REQ|REP] <transport>"

void usage_error(int code,char *msg) {
    fprintf(stderr,"%s\n",USAGE); 
    if (strlen(msg) > 0) {
        fprintf(stderr,"\nError: %s\n",msg);
    }
    exit(code);
}

void check(void *p,char *s,int code) {
    if (p == NULL) {
        perror(s);
        exit(code);
    }
}

int starts_with(char *s, char *prefix) {
    int l1 = strlen(s);
    int l2 = strlen(prefix);
    if (l1 < l2) return 0;
    return strncmp(s,prefix,l2) == 0;
}

void send(void *socket,sds data) {
    zmq_msg_t msg;
    zmq_msg_init_data(&msg,data,sdslen(data),NULL,NULL);
    zmq_send(socket,&msg,0);
    zmq_msg_close(&msg);
}

sds recv(void *socket) {
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    zmq_recv(socket,&msg,0);
    sds data = sdsnewlen(zmq_msg_data(&msg),zmq_msg_size(&msg));
    zmq_msg_close(&msg);
    return data;
}

int main(int argc, char **argv) {

    int i = 1;
    char *transport = NULL;
    int socket_type = ZMQ_REQ;

    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i],"-t")==0) {
            if (++i >= argc) usage_error(EX_USAGE,"-t <socket_type>");
            if (strcmp(argv[i],"REQ")==0) {
                socket_type = ZMQ_REQ;
            } else if (strcmp(argv[i],"REP") == 0) {
                socket_type = ZMQ_REP;
            } else {
                usage_error(EX_USAGE,"Invalid socket type");
            }
        } else if (strcmp(argv[i],"-h")==0) {
            usage_error(EX_USAGE,"Invalid socket type");
        }
        i++;
    }

    if (i >= argc) usage_error(EX_USAGE,"No transport specified");

    transport = argv[i];

    if (!(starts_with(transport,"ipc:") || 
          starts_with(transport,"tcp:"))) { 
        usage_error(EX_USAGE,"Invalid transport - must be ipc: or tcp:");
    }

    void *context;
    void *socket;

    check(context = zmq_init(1),"zmq_init",EX_UNAVAILABLE);
    check(socket = zmq_socket(context,socket_type),"zmq_socket",EX_UNAVAILABLE);

    if (socket_type == ZMQ_REQ) {
        if (zmq_connect(socket,transport) == -1) {
            perror("zmq_connect");
            exit(EX_UNAVAILABLE);
        }
        sds local = sdsreadfile(stdin);
        send(socket,local);
        sds net = recv(socket);
        printf("Received: %d\n",(int) sdslen(net));
        sdsfree(local);
        sdsfree(net);
    } else if (socket_type == ZMQ_REP) {
        if (zmq_bind(socket,transport) == -1) {
            perror("zmq_bind");
            exit(EX_UNAVAILABLE);
        }
        sds net = recv(socket);
        printf("Received: %d\n",(int) sdslen(net));
        sds local = sdsreadfile(stdin);
        send(socket,local);
        sdsfree(local);
        sdsfree(net);
    }

    zmq_close(socket);
    zmq_term(context);
    exit(0);
}

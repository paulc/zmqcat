
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

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

void check_zero(int val,char *s, int code) {
    if (val != 0) {
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
    check_zero(zmq_msg_init_data(&msg,data,sdslen(data),NULL,NULL),
                   "zmq_msg_init_data",EX_UNAVAILABLE);
    check_zero(zmq_send(socket,&msg,0),"zmq_send",EX_UNAVAILABLE);
    check_zero(zmq_msg_close(&msg),"zmq_msg_close",EX_UNAVAILABLE);
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

	sds local = sdsreadfile(stdin);

    if (socket_type == ZMQ_REQ) {
        check_zero(zmq_connect(socket,transport),"zmq_connect",EX_UNAVAILABLE);
    } else if (socket_type == ZMQ_REP) {
        check_zero(zmq_bind(socket,transport),"zmq_bind",EX_UNAVAILABLE);
        zmq_pollitem_t items[1];
        items[0].socket = socket;
        items[0].events = ZMQ_POLLIN;
        int ready = zmq_poll(items,1,10);
        printf("Poll: %d\n",ready);
    }
	send(socket,local);
	sdsfree(local);

    zmq_close(socket);
    zmq_term(context);
    exit(0);
}

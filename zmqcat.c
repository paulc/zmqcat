
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <zmq.h>

#include "sds.h"
#include "sdsutils.h"

#define USAGE           "Usage: zmqcat -t [REQ|REP] [-e <cmd>] [-n] [-v] <transport>"

#define USAGE_FULL      USAGE "\n\n" \
                        "   -t [REQ|REP]        Socket type (default: REQ)\n" \
                        "   -e <cmd>            Exec <cmd> on connect and pipe to socket\n" \
                        "   -n                  Close stdin\n" \
                        "   -v                  Verbose\n" \
                        "\n" \
                        "   <transport>         Socket endpoint\n"

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

size_t recv(void *socket,sds *buffer) {
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    zmq_recv(socket,&msg,0);
    size_t n = zmq_msg_size(&msg);
    *buffer = sdscatlen(*buffer,zmq_msg_data(&msg),n);
    zmq_msg_close(&msg);
    return n;
}

int main(int argc, char **argv) {

    /* Parse command line */

    char *transport = NULL;
    int socket_type = ZMQ_REQ;
    int verbose = 0;
    int timeout = -1;
    int no_stdin = 0;
    char *exec = NULL;

    int i = 1;

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
        } else if (strcmp(argv[i],"-e")==0) {
            if (++i >= argc) usage_error(EX_USAGE,"-e <cmd>");
            exec = argv[i];
            no_stdin = 1;
        } else if (strcmp(argv[i],"-n")==0) {
            no_stdin = 1;
        } else if (strcmp(argv[i],"-v")==0) {
            verbose++;
        } else if (strcmp(argv[i],"-h")==0) {
            fprintf(stderr,"%s\n",USAGE_FULL); 
            exit(EX_USAGE);
        }
        i++;
    }

    if (i >= argc) usage_error(EX_USAGE,"No transport specified");

    transport = argv[i];

    if (!(starts_with(transport,"ipc://") || 
          starts_with(transport,"tcp://"))) { 
        usage_error(EX_USAGE,"Invalid transport - must be ipc:// or tcp://");
    }

    /* Setup connections */

    void *context;
    void *remote;
    FILE *local = (no_stdin == 0) ? stdin : NULL;

    check(context = zmq_init(1),"zmq_init",EX_UNAVAILABLE);
    check(remote = zmq_socket(context,socket_type),"zmq_socket",EX_UNAVAILABLE);

    if (socket_type == ZMQ_REQ) {
        check_zero(zmq_connect(remote,transport),"zmq_connect",EX_UNAVAILABLE);
    } else if (socket_type == ZMQ_REP) {
        check_zero(zmq_bind(remote,transport),"zmq_bind",EX_UNAVAILABLE);
    } else {
        usage_error(EX_USAGE,"Invalid socket type");
    }

    zmq_pollitem_t items[2];
    items[0].socket = remote;
    items[0].events = (socket_type == ZMQ_REQ) ? ZMQ_POLLIN : ZMQ_POLLIN | ZMQ_POLLOUT;
    items[1].socket = NULL;
    items[1].fd = (local != NULL) ? fileno(local) : -1;
    items[1].events = ZMQ_POLLIN;

    /* Handle connections */

    sds rx_buffer = sdsempty();
    sds tx_buffer = sdsempty();
    int local_done = 0, remote_done = 0, local_eof = 0;
    
    if (no_stdin && socket_type == ZMQ_REQ) {
        if (exec != NULL) {
            sdsfree(tx_buffer);
            tx_buffer = sdsexec(exec);
            printf("EXEC: %s\n",tx_buffer);
        }
        send(remote,tx_buffer);
        local_eof = 1;
        local_done = 1;
    }

    int count = 0;

    while (!(local_done && remote_done)) {

        int ready = zmq_poll(items,2,timeout);

        printf("[%d] Poll...(%d): %d-%d\n",++count,ready,items[0].revents & ZMQ_POLLIN,items[1].revents & ZMQ_POLLIN);

        if (items[0].revents & ZMQ_POLLIN) {
            int n = recv(remote,&rx_buffer);
            printf("Remote: Read %d bytes\n",n);
            fwrite(rx_buffer,1,sdslen(rx_buffer),stdout);
            remote_done = 1;
        }
        if (items[0].revents & ZMQ_POLLOUT) {
            if (local_eof) {
                printf("Remote: Sent %d bytes\n",sdslen(tx_buffer));
                send(remote,tx_buffer);
                local_done = 1;
            }
        }
        if (items[1].revents & ZMQ_POLLIN) {
            if (!local_eof) {
                char buf[1024];
                int n = read(fileno(local),buf,1024);
                if (n == -1) {
                    perror("Error reading from local fd");
                    exit(EX_IOERR);
                } else if (n == 0) {
                    printf("+++ Local EOF\n");
                    items[1].events = 0;
                    local_eof = 1;
                    if (socket_type == ZMQ_REQ) {
                        send(remote,tx_buffer);
                        local_done = 1;
                    }
                } else {
                    printf("Local: Read %d bytes\n",n);
                    tx_buffer = sdscatlen(tx_buffer,buf,n);
                }
            }
        }
    }

    sdsfree(tx_buffer);
    sdsfree(rx_buffer);

    zmq_close(remote);
    zmq_term(context);
    exit(0);
}

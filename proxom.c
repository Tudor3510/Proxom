/*
    Copyright 2005,2006,2007,2008,2009 Luigi Auriemma

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

    http://www.gnu.org/licenses/gpl-2.0.txt
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <ctype.h>
#include "acpdump2.h"
#include "show_dump.h"
#include <signal.h>
#include <SFML/Network.h>


#ifdef _WIN32
    #include <winsock.h>
    #include <windows.h>
    #include "winerr.h"

    #define close           closesocket
    #define sleep           Sleep
    #define in_addr_t       uint32_t

    #define set_priority    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)

    void winerr(void);
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <netdb.h>
    #include <pthread.h>
    #include <sys/time.h>

    #define set_priority    nice(-10)
    #define SOCKET          int
    #define SOCKET_ERROR    (-1)
#endif

typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;



#define VER         "0.4.1"
#define BUFFSZ      0xffff
#define RECVFROMF(X) \
                    psz = sizeof(struct sockaddr_in); \
                    len = recvfrom(X, buff, BUFFSZ, 0, (struct sockaddr *)&peerl, &psz); \
                    if(len < 0) continue;
#define SENDTOFC(X,Y) \
                    if(sendtof(sdl, buff, len, &c->peer, X) != len) { \
                        c = check_sd(&c->peer, 1); /* it's ever c->peer */ \
                        Y; \
                        if(!c) break; \
                    }

#ifndef IP_TOS
    #define IP_TOS 3
#endif



// default: __cdecl
static int (* sudp_init)(u8 *)     = NULL;  // initialization
static int (* sudp_pck)(u8 *, int) = NULL;  // modification of the packet
static int (* sudp_vis)(u8 *, int) = NULL;  // modification for visualization only

//static SOCKET (*mysocket)(int af, int type, int protocol) = NULL;
//static int (*myconnect)(SOCKET s, const struct sockaddr *name, int namelen) = NULL;
//static SOCKET (*myaccept)(SOCKET s, const struct sockaddr *name, int *namelen) = NULL;
//static int (*mybind)(SOCKET s, const struct sockaddr *name, int namelen) = NULL;
//static int (*myclose)(SOCKET s) = NULL;
//static int (*myrecv)(SOCKET s, char *buf, int len, int flags) = NULL;
static int (*myrecvfrom)(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen) = NULL;
//static int (*mysend)(SOCKET s, char **retbuf, int len, int flags) = NULL;
static int (*mysendto)(SOCKET s, char **retbuf, int len, int flags, const struct sockaddr *to, int tolen) = NULL;



struct clients_struct {
    int     sd; // it's needed to use a different source port for each packet
    struct  sockaddr_in peer;
    time_t  timez;
    struct  clients_struct  *next;
} *clients  = NULL;

struct sockaddr_in *dhost   = NULL;
in_addr_t   lhost           = INADDR_ANY,
            Lhost           = INADDR_ANY;
int         multisock       = 0,
            samesock        = 0,
            quiet           = 0,
            timeout         = 60;   // NAT uses a timeout of 5 minutes (300 seconds)



int sendtof(int s, char *buf, int len, struct sockaddr_in *to, int do_mysendto);
int bind_udp_socket(struct sockaddr_in *peer, in_addr_t iface, u16 port);
struct clients_struct *check_sd(struct sockaddr_in *peer, int force_remove);
struct sockaddr_in *create_peer_array(u8 *list, u16 default_port);
void show_peer_array(u8 *str, struct sockaddr_in *peer);
in_addr_t resolv(char *host);
void std_err(void);

const int PORT_AMONG_US = 22023;
const int PORT_AMONG_US_BROADCAST = 47777;

char destinationHostAux[100];
int running = 1;
volatile int runningBroadcast = 0;
volatile int stoppingProxyShown = 0;
volatile int shouldInitialize = 1;
volatile int messageThreadInitialized = 0;
volatile int shouldRunMessageThread = 1;

int firstTimeBroadcast = 1;
sfUdpSocket *broadcastSocket;
char broadcastMessage[100];
char finalBroadcastMessage[100];

const char AUX_MESSAGE[] = "Server";
const char AUX_MESSAGE_FINAL[] = "~Open~1~";

char combinationKey[10] = "ALT";
char startHotKey = 'B';
char stopHotKey = 'J';

DWORD messageThreadId = 0;
HANDLE broadcastGameHandle, messageThreadHandle;

const int BROADCAST_THREAD_WAITING_TIME = 2000;
const int PROXY_WAITING_TIME = 400;
const int HALF_BROADCAST_TIME = 500;
const int TIME_TO_CHECK = 15;

void INThandler(int sig){
    if (runningBroadcast){
        runningBroadcast = 0;
        printf("- stopping the broadcast of the server...\n");
    }

    shouldInitialize = 0;
    running = 0;
    PostThreadMessage(messageThreadId, WM_CLOSE, 0, 0);


    WaitForSingleObject(broadcastGameHandle, INFINITE);
    WaitForSingleObject(messageThreadHandle, INFINITE);

    Sleep(PROXY_WAITING_TIME);

    if(!stoppingProxyShown){
        printf("- stopping the proxy...\n");
        stoppingProxyShown = 1;
    }

    exit(0);
}

void initializeBroadcasting(){
    broadcastSocket = sfUdpSocket_create();
    sfUdpSocket_setBlocking(broadcastSocket, sfFalse);

    finalBroadcastMessage[0] = 4;
    finalBroadcastMessage[1] = 2;

    if (strlen(broadcastMessage) > 0){
        strcpy(finalBroadcastMessage + strlen(finalBroadcastMessage), broadcastMessage);
        printf("- the broadcast message is: %s\n", broadcastMessage);
    }else{ 
        strcpy(finalBroadcastMessage + strlen(finalBroadcastMessage), AUX_MESSAGE);
        printf("- the broadcast message is: %s\n", AUX_MESSAGE);
    }

    strcpy(finalBroadcastMessage + strlen(finalBroadcastMessage) , AUX_MESSAGE_FINAL);

    runningBroadcast = 1;
}

DWORD WINAPI broadcastGame(LPVOID lpParam){

    while (runningBroadcast){
        sfUdpSocket_send(broadcastSocket, finalBroadcastMessage, strlen(finalBroadcastMessage), sfIpAddress_Broadcast, PORT_AMONG_US_BROADCAST);
        Sleep(HALF_BROADCAST_TIME);
        if (!runningBroadcast) break;
        Sleep(HALF_BROADCAST_TIME);
    }

    return 0;

}

DWORD WINAPI messageThread(LPVOID lpParam){
    enum{
        START_BROADCASTING = 137,
        STOP_BROADCASTING = 197
    };

    UINT finalCombinationKey = 0;
    UINT finalStartHotKey = 0;
    UINT finalStopHotKey = 0;

    if (strcmpi(combinationKey, "alt") == 0){
        finalCombinationKey = MOD_ALT;
        strcpy(combinationKey, "Alt");
    }

    if (strcmpi(combinationKey, "shift") == 0){
        finalCombinationKey = MOD_SHIFT;
        strcpy(combinationKey, "Shift");
    }

    if (strcmpi(combinationKey, "ctrl") == 0){
        finalCombinationKey = MOD_CONTROL;
        strcpy(combinationKey, "Ctrl");
    }

    if (startHotKey <= 'z' && startHotKey >= 'a'){
        finalStartHotKey = VkKeyScanExA(startHotKey, GetKeyboardLayout(0));
        startHotKey -= ('a' - 'A');
    }else if (startHotKey <= 'Z' && startHotKey >= 'A'){
        finalStartHotKey = VkKeyScanExA(startHotKey + ('a' - 'A'), GetKeyboardLayout(0));
    }

    if (stopHotKey <= 'z' && stopHotKey >= 'a'){
        finalStopHotKey = VkKeyScanExA(stopHotKey, GetKeyboardLayout(0));
        stopHotKey -= ('a' - 'A');
    }else if (stopHotKey <= 'Z' && stopHotKey >= 'A'){
        finalStopHotKey = VkKeyScanExA(stopHotKey + ('a' - 'A'), GetKeyboardLayout(0));
    }

    printf("- the broadcast of the game can be stopped using %s + %c\n", combinationKey, stopHotKey);
    printf("- the broadcast of the game can be started using %s + %c\n", combinationKey, startHotKey);

    messageThreadInitialized = 1;

    RegisterHotKey(0, START_BROADCASTING, finalCombinationKey, finalStartHotKey);
    RegisterHotKey(0, STOP_BROADCASTING, finalCombinationKey, finalStopHotKey);

    

    MSG msg;
    DWORD threadResult;
    while(GetMessage(&msg, 0, 0, 0)){
        PeekMessage(&msg, 0, 0, 0, PM_REMOVE);

        if (msg.message == WM_HOTKEY){
            switch (msg.wParam){

                case START_BROADCASTING:

                    if (!runningBroadcast){
                        threadResult = WaitForSingleObject(broadcastGameHandle, BROADCAST_THREAD_WAITING_TIME);

                        if (threadResult == WAIT_OBJECT_0){
                            runningBroadcast = 1;
                            broadcastGameHandle = CreateThread(0, 0, broadcastGame, NULL, 0, 0);
                            printf("- starting the broadcast of the server on port %d\n", PORT_AMONG_US_BROADCAST);
                        }
                    }
                    break;

                case STOP_BROADCASTING:
                    if (runningBroadcast){
                        runningBroadcast = 0;
                        printf("- stopping the broadcast of the server...\n");
                    }
                    break;
            }
        }

        if (msg.message == WM_CLOSE) break;
    }

    UnregisterHotKey(0, START_BROADCASTING);
    UnregisterHotKey(0, STOP_BROADCASTING);


    return 0;
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    signal(SIGINT, INThandler);
    signal(SIGBREAK, INThandler);
#else
    signal(SIGQUIT, INThandler);
    signal(SIGINT, INThandler2);
#endif

    running = 1;
    shouldInitialize = 1;
    messageThreadInitialized = 0;
    shouldRunMessageThread = 1;

    struct clients_struct   *c  = NULL,
                            *tmpc;
    struct  sockaddr_in peerl,
                        peer0,
                        *psrc   = NULL,
                        *pdst   = NULL;
    struct  timeval tout;
    FILE    *fdcap      = NULL;
    fd_set  readset;
    int     sdl         = 0,
            sdi         = 0,
            sd0         = 0,
            selsock     = 0,
            i,
            len         = 0,
            psz         = 0,
            hexdump     = 0,
            t,
            everyone    = 0,
            priority    = 0;
    u16     port,
            lport,
            inject      = 0;
    u8      tmp[16],
            *buff       = NULL,
            *acpfile    = NULL,
            *dllname    = NULL,
            *dllpar     = NULL;

#ifdef _WIN32
    WSADATA    wsadata;
    WSAStartup(MAKEWORD(1,0), &wsadata);
#endif

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    fputs("\n"
        "Simple UDP proxy/pipe "VER"\n"
        "by Luigi Auriemma\n"
        "Enhanced by Tudor for Among Us\n"
        "\n", stderr);


    int ipAddressCompleted = 0;

    if (argc > 2){
        for (int index_arg = 1; index_arg < argc - 1; index_arg++){
            if (strcmp(argv[index_arg], "-s") == 0){
                strcpy(destinationHostAux, argv[index_arg + 1]);
                ipAddressCompleted = 1;
            }

            if (strcmp(argv[index_arg], "-m") == 0){
                strcpy(broadcastMessage, argv[index_arg + 1]);
            }

            if (strcmp(argv[index_arg], "-b") == 0){
                if (strcmp(argv[index_arg + 1], "false") == 0)
                    shouldRunMessageThread = 0;
            }

            if (strcmp(argv[index_arg], "-c") == 0){
                strcpy(combinationKey, argv[index_arg + 1]);
            }

            if (strcmp(argv[index_arg], "-r") == 0){
                startHotKey = argv[index_arg + 1][0];
            }

            if (strcmp(argv[index_arg], "-t") == 0){
                stopHotKey = argv[index_arg + 1][0];
            }
        }
    }

    if(!ipAddressCompleted) {
        printf("\n"
            "Enter the server ip address:\n");
        scanf("%s", destinationHostAux);
    }

    port  = PORT_AMONG_US;
    lport = PORT_AMONG_US;
    dhost = create_peer_array(destinationHostAux, port);

    if(lhost == INADDR_NONE) std_err();
    if(Lhost == INADDR_NONE) std_err();

    sdl = bind_udp_socket(NULL, lhost, lport);

    if(inject) sdi = bind_udp_socket(NULL, lhost, inject);

    if(samesock) {
        if(!quiet) fprintf(stderr, "- same socket/port mode\n");
        samesock = bind_udp_socket(NULL, Lhost, 0);
    }
    if(multisock) {
        if(!quiet) fprintf(stderr, "- multi socket/port mode\n");
    }

    if(!quiet) {
        if(dhost[0].sin_addr.s_addr) {
            show_peer_array("- remote hosts:  ", dhost);
        } else {
            fprintf(stderr, "- double binding\n");
            fprintf(stderr, "- dest_port      %hu\n", port);
        }
    }


    if(acpfile) {
        if(!quiet) printf("- create ACP file %s\n", acpfile);
        fdcap = fopen(acpfile, "rb");
        if(fdcap) {
            fclose(fdcap);
            fprintf(stderr, "- do you want to overwrite (Y) or append (A) the file? (y/a/N)\n  ");
            fgets(tmp, sizeof(tmp), stdin);
            t = tmp[0];
            if(t == 'a') {
            } else if(t == 'y') {
            } else return(0);
        } else {
            t = 0;
        }
        fdcap = fopen(acpfile, (t == 'a') ? "ab" : "wb");
        if(!fdcap) std_err();
        if(t != 'a') create_acp(fdcap);
    }

    //set_priority;


    if (shouldInitialize){
        initializeBroadcasting();
        printf("- starting the broadcast of the server on port %d\n", PORT_AMONG_US_BROADCAST);
        broadcastGameHandle = CreateThread (0, 0, broadcastGame, NULL, 0, 0);

        if (shouldRunMessageThread)
            messageThreadHandle = CreateThread (0, 0, messageThread, NULL, 0, &messageThreadId);
    }

    if(!dhost[0].sin_addr.s_addr) {
        sd0 = bind_udp_socket(&peer0, Lhost, port);
        printf("- wait first packet from the server (double-binding mode)\n");
        FD_ZERO(&readset);      // wait first client's packet, this is NEEDED!
        FD_SET(sd0, &readset);
        if(select(sd0 + 1, &readset, NULL, NULL, NULL)
          < 0) std_err();
    }

    while (!messageThreadInitialized) Sleep(TIME_TO_CHECK);
    printf("- ready\n");
    FD_ZERO(&readset);      // wait first client's packet, this is NEEDED!
    FD_SET(sdl, &readset);
    if(select(sdl + 1, &readset, NULL, NULL, NULL)
      < 0) std_err();

    buff = malloc(BUFFSZ);
    if(!buff) std_err();
    clients = NULL;

    for(;running;) {
        FD_ZERO(&readset);
        FD_SET(sdl, &readset);
        selsock = sdl;
        if(sd0) {
            FD_SET(sd0, &readset);
            if(sd0 > selsock) selsock = sd0;
        }
        if(sdi) {
            FD_SET(sdi, &readset);
            if(sdi > selsock) selsock = sdi;
        }
        for(c = clients; c; c = c->next) {
            FD_SET(c->sd, &readset);
            if(c->sd > selsock) selsock = c->sd;
        }

        tout.tv_sec  = timeout;     // this is useful if we want to free memory
        tout.tv_usec = 0;           // ...rarely used but I think it's good here
        t = select(selsock + 1, &readset, NULL, NULL, &tout);
        if(t < 0) std_err();
        if(!t) {    // timeout reached, call check_sd for removing the old clients
            memset(&peerl, 0, sizeof(struct sockaddr_in));
            check_sd(&peerl, 0);
            continue;
        }

        if(sdi && (FD_ISSET(sdi, &readset))) {
            RECVFROMF(sdi)

            if(!quiet) printf("- packet injection from %s:%hu (%d bytes)\n",
                inet_ntoa(peerl.sin_addr), ntohs(peerl.sin_port), len);

            psrc = &peerl;
            pdst = &dhost[0];   // the first one is enough, it's used only for the CAP file

            if(sudp_pck) len = sudp_pck(buff, len);  // packets modification

            for(c = clients; c; c = c->next) {
                if(sd0) sendtof(sd0, buff, len, &peer0, 1);
                for(i = 0; dhost[i].sin_addr.s_addr; i++) {
                    sendtof(c->sd, buff, len, &dhost[i], 1);
                }
            }

            if(everyone) {
                for(c = clients; c; c = c->next) {
                    SENDTOFC(1,)
                }
            }

        } else if(sd0 && FD_ISSET(sd0, &readset)) {    // experimental and useless
            RECVFROMF(sd0)

            psrc = &peerl;
            pdst = &peer0;  // yes it's wrong but it's not necessary

            if(sudp_pck) len = sudp_pck(buff, len);  // packets modification

            for(c = clients; c; c = c->next) {
                SENDTOFC(1,)
            }

            if(everyone) {
                // nothing to do here
            }
        } else if(FD_ISSET(sdl, &readset)) {
            RECVFROMF(sdl)

            c = check_sd(&peerl, 0);    // check if this is a new or existent client
            if(!c) continue;

            psrc = &c->peer;
            pdst = &dhost[0];   // the first one is enough, it's used only for the CAP file

            if(sudp_pck) len = sudp_pck(buff, len);  // packets modification

            if(sd0) sendtof(sd0, buff, len, &peer0, 1);
            if(multisock) {
                i = 0;
                for(c = clients; c; c = c->next) {
                    if(!dhost[i].sin_addr.s_addr) break;
                    if(memcmp(&c->peer, &peerl, sizeof(struct sockaddr_in))) continue;
                    sendtof(c->sd, buff, len, &dhost[i], 1);
                    i++;
                }
            } else {
                for(i = 0; dhost[i].sin_addr.s_addr; i++) {
                    sendtof(c->sd, buff, len, &dhost[i], 1);
                }
            }

            if(everyone) {
                tmpc = c;
                for(c = clients; c; c = c->next) {
                    if(c == tmpc) continue;
                    SENDTOFC(1,)
                }
            }
        } else {
            for(c = clients; c; c = c->next) {
                if(!FD_ISSET(c->sd, &readset)) continue;
                RECVFROMF(c->sd)

                psrc = &peerl;
                pdst = &c->peer;

                if(sudp_pck) len = sudp_pck(buff, len);  // packets modification
                if(myrecvfrom) len = myrecvfrom(c->sd, buff, len, 0, (struct sockaddr *)psrc, &psz);

                SENDTOFC(0, pdst = NULL)    // like SENDTOFC but without the handling of mysendto

                if(everyone || samesock) {
                    tmpc = c;
                    for(c = clients; c; c = c->next) {
                        if(c == tmpc) continue;
                        SENDTOFC(1,)
                    }
                }
                break;
            }
        }

        if(!psrc || !pdst) continue;    // the following is only for visualization

        if(fdcap) acp_dump(
            fdcap, SOCK_DGRAM, IPPROTO_UDP,
            psrc->sin_addr.s_addr, psrc->sin_port, pdst->sin_addr.s_addr, pdst->sin_port,
            buff, len,
            NULL, NULL, NULL, NULL);

        if(sudp_vis) len = sudp_vis(buff, len);

        if(hexdump) {
            if(!quiet) {
                printf("\n%s:%hu -> ", inet_ntoa(psrc->sin_addr), ntohs(psrc->sin_port));
                printf("%s:%hu\n",     inet_ntoa(pdst->sin_addr), ntohs(pdst->sin_port));
            }
            show_dump(buff, len, stdout);
        }
    }
    WaitForSingleObject(broadcastGameHandle, INFINITE);
    WaitForSingleObject(messageThreadHandle, INFINITE);

    if(!stoppingProxyShown){
        printf("- stopping the proxy...\n");
        stoppingProxyShown = 1;
    }

    close(sdl);
    if(sdi)   close(sdi);
    if(sd0)   close(sd0);
    if(fdcap) fclose(fdcap);
    free(buff);
    return(0);
}



int sendtof(int s, char *buf, int len, struct sockaddr_in *to, int do_mysendto) {
    int     oldlen  = 0,
            ret;
    char    *oldbuf = NULL;

    if(!do_mysendto) {
        return(sendto(s, buf, len, 0, (struct sockaddr *)to, sizeof(struct sockaddr_in)));
    }

    if(mysendto) {
        oldbuf = buf;
        oldlen = len;
        ret = mysendto(s, &buf, len, 0, (struct sockaddr *)to, sizeof(struct sockaddr_in));
        if(ret >= 0) {
            // call real function
        } else if(ret == SOCKET_ERROR) {
            goto quit_and_free;
        } else {
            ret = oldlen;
            goto quit_and_free;
        }
        len = ret;
    }
    ret = sendto(s, buf, len, 0, (struct sockaddr *)to, sizeof(struct sockaddr_in));
quit_and_free:
    if(mysendto) {
        if(oldbuf != buf) free(buf);
        if(ret == len) ret = oldlen;
    }
    return(ret);
}



struct clients_struct *check_sd(struct sockaddr_in *peer, int force_remove) {
    struct clients_struct   *c,
                            *tmp,
                            *prev,
                            *ret;
    time_t  curr;
    int     multi = 0;

    curr = time(NULL);
    prev = NULL;
    ret  = NULL;

    for(c = clients; c; ) {
        if((c->peer.sin_addr.s_addr == peer->sin_addr.s_addr) && (c->peer.sin_port == peer->sin_port)) {
            c->timez = curr;
            ret      = c;
            if(force_remove) {
                c->timez = (curr - timeout) - 1;
                ret  = prev;
            }
        }
        if((curr - c->timez) >= timeout) {
            if(!quiet) printf("- remove %s:%hu\n",
                inet_ntoa(c->peer.sin_addr), ntohs(c->peer.sin_port));

            tmp = c->next;
            if(samesock) {
                // do NOT close c->sd!
            } else {
                close(c->sd);
            }
            free(c);
            if(prev) {      // second, third and so on
                prev->next = tmp;
            } else {        // the first only
                clients    = tmp;
            }
            c = tmp;        // already the next
        } else {
            prev = c;
            c    = c->next; // get the next
        }
    }

    if(force_remove) return(ret);
    if(ret) return(ret);
    if((peer->sin_addr.s_addr == INADDR_ANY) || (peer->sin_addr.s_addr == INADDR_NONE) || !peer->sin_port) return(NULL);

multisock_doit:
    c = malloc(sizeof(struct clients_struct));
    if(!c) return(NULL);
    if(prev) {
        prev->next = c;
    } else {
        clients    = c;
    }

    if(samesock) {
        c->sd = samesock;
    } else {
        c->sd = bind_udp_socket(NULL, Lhost, 0);
    }
    memcpy(&c->peer, peer, sizeof(struct sockaddr_in));
    c->timez = curr;
    c->next  = NULL;

    if(!quiet) printf("- add %s:%hu\n",
        inet_ntoa(c->peer.sin_addr), ntohs(c->peer.sin_port));

    if(multisock) {
        prev = c;
        multi++;
        if(dhost[multi].sin_addr.s_addr) goto multisock_doit;
    }

    return(c);
}



struct sockaddr_in *create_peer_array(u8 *list, u16 default_port) {
    struct sockaddr_in *ret;
    int     i,
            size = 1;
    u16     port;
    u8      *p1,
            *p2;

    for(p2 = list; (p1 = strchr(p2, ',')); size++, p2 = p1 + 1);

    ret = calloc(size + 1, sizeof(struct sockaddr_in));
    if(!ret) std_err();

    for(i = 0;;) {
        p1 = strchr(list, ',');
        if(p1) *p1 = 0;

        port = default_port;
        p2 = strchr(list, ':');
        if(p2) {
            *p2 = 0;
            port = atoi(p2 + 1);
        }

        while(*list == ' ') list++;
        ret[i].sin_addr.s_addr = resolv(list);
        ret[i].sin_port        = htons(port);
        ret[i].sin_family      = AF_INET;

        i++;
        if(!p1) break;
        list = p1 + 1;
    }
    return(ret);
}



void show_peer_array(u8 *str, struct sockaddr_in *peer) {
    int     i;

    fputs(str, stderr);
    for(i = 0; peer[i].sin_addr.s_addr; i++) {
        if(i) fprintf(stderr, ", ");
        fprintf(stderr, "%s:%hu", inet_ntoa(peer[i].sin_addr), ntohs(peer[i].sin_port));
    }
    fputc('\n', stderr);
}



int bind_udp_socket(struct sockaddr_in *peer, in_addr_t iface, u16 port) {
    struct sockaddr_in  peer_tmp;
    int     sd;
    static const int
            on      = 1,
            tos     = 0x10;
    static const struct
            linger  ling = {1,1};

    if(!peer) peer = &peer_tmp;
    peer->sin_addr.s_addr = iface;
    peer->sin_port        = htons(port);
    peer->sin_family      = AF_INET;

    if((iface != INADDR_ANY) || port) {
        if(!quiet) printf("- bind UDP port %hu on interface %s\n",
            ntohs(peer->sin_port), inet_ntoa(peer->sin_addr));
    }

    sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sd < 0) std_err();
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
    if(bind(sd, (struct sockaddr *)peer, sizeof(struct sockaddr_in))
      < 0) std_err();

    setsockopt(sd, SOL_SOCKET, SO_LINGER,    (char *)&ling, sizeof(ling));
    setsockopt(sd, SOL_SOCKET, SO_BROADCAST, (char *)&on,   sizeof(on));
    setsockopt(sd, IPPROTO_IP, IP_TOS,       (char *)&tos,  sizeof(tos));
    return(sd);
}




in_addr_t resolv(char *host) {
    struct      hostent *hp;
    in_addr_t   host_ip;

    host_ip = inet_addr(host);
    if(host_ip == INADDR_NONE) {
        hp = gethostbyname(host);
        if(!hp) {
            fprintf(stderr, "\nError: unable to resolv hostname (%s)\n", host);
            exit(1);
        } else host_ip = *(in_addr_t *)hp->h_addr;
    }
    return(host_ip);
}



#ifndef _WIN32
    void std_err(void) {
        perror("\nError");
        exit(1);
    }
#else
void winerr(void) {
    char *error;

    if(!FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL,
        GetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&error,
        0,
        NULL)) {
        error = strerror(errno);
    }
    printf("\nError: %s\n", error);
    //LocalFree(error);
    exit(1);
}
#endif

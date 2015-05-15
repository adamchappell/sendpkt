// sendpkt - generic network testing utility
//
// Copyright Adam Chappell, October 2014.

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

/* for getopt */
extern char *optarg;
extern int optind;
extern int optopt;
extern int opterr;
extern int optreset;

// Configuration parameters and associated defaults
unsigned int debug=0;
unsigned int packetLimit=0;
unsigned int timeLimit=5;
unsigned int rate=1;
unsigned int size=1472;
unsigned int port=6012;
unsigned int setTOS=0;

// Run-time variables
unsigned int interval_usec=0;
unsigned int interval_sec=0;
unsigned int TOS=0;
int inetfd;
char* dest=NULL;
struct sockaddr_in destaddr;
char* prefixLenStr;
unsigned int prefixLen;
unsigned int mask;
char* data;

// Stats
unsigned int packets=0;
unsigned int bytes=0;
time_t start;
time_t now;

// Flags for signals
sig_atomic_t run=1;
sig_atomic_t sigalrm=0;
sig_atomic_t sighup=0;
sig_atomic_t sigint=0;
sig_atomic_t sigterm=0;
sig_atomic_t siginfo=0;

void runloop(void);

void usage(char* name) {
    fprintf(stderr, "%s [-d] [-c packet-count-limit] [-s size] [-r rate] [-t time-limit (secs)] [-p port] [-Q IP TOS byte] destination\n", name);
    fprintf(stderr, "    default params: size/-s: %u; rate/-r: %u; port/-p: %u\n",
    	size, rate, port);
    fprintf(stderr, "    default limits: packets/-c: %u; time:/-t: %u\n",
    	packetLimit, timeLimit);
	exit(1);
}

void signal_handler(int signum) { 
	if (signum==SIGTERM) sigterm=1;
	if (signum==SIGINT) sigint=1;
	if (signum==SIGHUP) sighup=1;
	if (signum==SIGALRM) sigalrm=1;
	if (signum==SIGINFO) siginfo=1;
}

void stats(void) {
	now = time(NULL);
	if ((now - start) > 0) {
		printf("STATS: %u packet(s); %u byte(s); %lu second(s); average pps: %lu; average bps: %lu\n",
			packets, bytes, now - start, packets / (now - start), ((long)(bytes)*8) / (now - start));
	} else {
		printf("STATS: %u packet(s); %u byte(s); %lu second(s)\n",
			packets, bytes, now - start);		
	}
}

// General run loop
void runloop(void) {

	int result;		// select's return
	struct timeval select_timeout={0};

	while (run) {

		// Set the select timeout
        select_timeout.tv_usec = interval_usec;
        select_timeout.tv_sec = interval_sec;

		// Call select()
		result = select(0, NULL, NULL, NULL, &select_timeout);
		if (result==-1) {
			if (errno!=EINTR) {
				fprintf(stderr, "select() returned error: %s\n", strerror(errno));
				run=0;		// exit run loop unless its interrupted sys call			
			}
		}
            
		// Check for signals
		if (sigalrm) {
			sigalrm=0;	// clear the flag afterwards!!
			run=0; continue;
		}
		if (sighup) {
			// do something useful for SIGHUP
			sighup=0;	// clear the flag afterwards!!
		}
		if (sigterm) {
			if (debug) fprintf(stderr, "Received SIGTERM: exiting run loop\n");
			run=0; continue;
		}
		if (sigint) {
			if (debug) fprintf(stderr, "Received SIGINT: exiting run loop\n");
			run=0; continue;
		}
		if (siginfo) {
			stats();
			siginfo=0;
		}

		// Make a random destination address, within confines
		// of destination mask
	    destaddr.sin_addr.s_addr=inet_addr(dest);
	    destaddr.sin_addr.s_addr=destaddr.sin_addr.s_addr&(htonl(~mask));
	    destaddr.sin_addr.s_addr+=htonl((random()&mask));

        // send the packet
        if (debug) fprintf(stderr, "writing %d byte(s) to %s: ", size, inet_ntoa(destaddr.sin_addr));
        result=sendto(inetfd, data, size, 0x0, (struct sockaddr*)&destaddr,
               sizeof(destaddr));
        if (result==-1) {
			fprintf(stderr, "sendto() returned error: %s\n", strerror(errno));
		} else {
	        if (debug) fprintf(stderr, "wrote %d byte(s)\n", result);
	        packets+=1;
	        bytes+=result;		
		}
        
        if (packetLimit) {
            packetLimit--;
            if (packetLimit==0) run=0;
        }

	}
}


// Main code stream:
// - parse command line args
// - setup socket
// - call runloop
int main (int argc, char** argv) {
	struct sigaction sa;
	struct timeval maxwait, poll;

	int cmdflag;
	while ((cmdflag=getopt(argc, argv, "dc:s:r:t:p:Q:"))!=-1) {
		switch (cmdflag) {
			case 'd': debug++; break;
			case 's': size=atoi(optarg); break;
            case 'c': packetLimit=atoi(optarg); break;
			case 'r': rate=atoi(optarg); break;
			case 't': timeLimit=atoi(optarg); break;
			case 'p': port=atoi(optarg); break;
			case 'Q': TOS=atoi(optarg); setTOS=1; break;
			default: usage("sendpkt");
		}
	}
	argc -= optind;
    argv += optind;
    if (argc!=1) {
    	usage("sendpkt");
    }

	// Read the prefix length if specified, and create
	// as mask to help generate random addresses within
	// the specified range
	dest=strdup(argv[0]);
	(void) strtok(dest, "/");
	if ((prefixLenStr=strtok(NULL, "/"))==NULL) prefixLenStr="32";
	prefixLen = atoi(prefixLenStr);
	if (prefixLen>32) {
		fprintf(stderr, "Prefix length, prefixLen,  must satisfy 0 <= prefixLen <= 32\n");
		exit(1);
	}

	// Generate the mask through a bit-shift, and -1. /32 and /0 are identical this
	// way on hosts with a 32-bit unsigned int, so special prefixLen==0 to set all ones
	mask=(1<<(32-prefixLen))-1;
	if (prefixLen==0) mask=(unsigned int) -1;
        
    // signal handler
	sa.sa_handler=signal_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGALRM, &sa, NULL);
	sigaction(SIGINFO, &sa, NULL);

	// Parameters for select timeout. Special case for rate==1 or rate==0
	// which overflow the usec field and cause div-by-zero respectively
	if (rate<=1) {
    	interval_usec=0;
    	interval_sec=rate;		
	} else {
	    interval_usec=1000000/rate;		
	}
	printf("SENDPKT Dest %s/%u; target pps rate: %u pps (derived send interval %u usec)\n",
		dest, prefixLen, rate, interval_usec);

    // Create the socket. (Note: blocking...)
    if ((inetfd=socket(AF_INET, SOCK_DGRAM, 0))<0) {
        fprintf(stderr, "Can't make AF_INET socket: %s\n",
            strerror(errno));
        exit(1);
    }

    // non-changing destination parameters
    destaddr.sin_family=AF_INET;
    destaddr.sin_port=htons(port);

    if (setTOS) {
    	if (setsockopt(inetfd, IPPROTO_IP, IP_TOS, &TOS, sizeof(TOS))==-1) {
    		fprintf(stderr, "setsocketopt() for IP ToS failed: %s\n", strerror(errno));
    	}
    }

    // zero out a data block for the payload
    data=(char*)malloc(size);
    bzero(data, size);

    // record the start time
    start=time(NULL);
    srandom(start);

	// If operator specified a time limit, set an alarm
	if (timeLimit)
			alarm(timeLimit);

	// Go...
	runloop();

	// Output final stats
	stats();

	return(0);

}

	

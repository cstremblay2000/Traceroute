/**
 * File:        trace.c
 * Author:      Chris Tremblay <cst1465@rit.edu>
 * Date:        10/08/2021, National Fluff Day!
 * Description:
 *      An implementation of traceroute
 **/

#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h> // 
#include <string.h> // stroerror
#include <error.h> 
#include <unistd.h>
#include <poll.h> // self explanatory
#include <sys/time.h>
#include <time.h>
#include <sys/time.h>
#include <getopt.h> // commandline processing
#include <netinet/ip_icmp.h> // icmp headers
#include <netdb.h> // 
#include <signal.h> // signal handlers
#include <sys/uio.h> // iovecs
#include <limits.h> // LONG_MAX and LONG_MIN

#include "networking.h"
#include "set_uid.h"
#include "err.h"

// The usage message
const char USAGE_MSG[] = "Usage: ./trace [-n] [-q N] host\n";

// the getopt string for flags
const char FLAGS[] = "nq:";

// the descriptions for usage message
const char *USG_DESC[] = {"Description", 
        "print hop addresses just numerically",
        "number of probes per TTL" };

// the flags for usage message
const char *USG_FLAGS[] = {"Flags", "-n","-q"};

// the default values for options
const char *USG_DEF[] = {"Default", "numeric and symbolic", "3" };

// the templage for usage message
const char USG_TEMP[] = "\t%-6s %-25s %-s\n"; 

// the traceroute message template
const char HOP_MSG[] = "%2d %s (%s) %3dms %3dms %3dms\n";

// the intit message
const char INIT_MSG[] = 
"traceroute to %s (%s), %d hops max, %d byte packets\n";

// time out for poll
const int TIMEOUT = 1 * 1000; // seconds

// buffer size
const int BUFF_SIZE = 64;

// the flags
int NQUERY = 3;
int SYMNUM = 1;

// the signal handler flag to stop while loop from running
int looping = 1;
int NHOPS = 30;

// Descriptoin:
//      Print the usage message
void usage(){
        fprintf( stderr, "%s\n", USAGE_MSG );
        for( int i = 0; i < 3; i++ )
                printf( USG_TEMP, USG_FLAGS[i], USG_DEF[i], USG_DESC[i]);
        return;
}

// Description:
//      Print and error nicely
// Parameters:
//      msg -> the message associated with error
void print_error( char *msg ){
        fprintf( stderr, "[error] %d <%s>: %s\n", errno,strerror( errno ),msg);
        return;
}

// Description:
//      The signal handler to stop the program
// Parameters:
//      sog -> the signal number
void sig_handler( int sig ){
        if( sig == SIGKILL || sig == SIGINT )
                looping = 0;
}

// Description:
//      Process the commandline arguments
// Parameters
//      argc -> the number of arguments
//      argv -> the args
void process_commandline( int argc, char **argv ){
        char opt;
        long t;
        while( (opt=getopt( argc, argv, FLAGS )) != -1 ){
                switch( opt ){
                        case 'n':
                                SYMNUM = 0;
                                break;
                        
                        case 'q':
                                t = strtol( optarg, NULL, 10 );
                                if( t == LONG_MAX || t == LONG_MIN ){
                                        print_error( "-q parameter" );
                                        exit( EXIT_FAILURE );
                                }

                                if( t <= 0 ){
                                        print_error( "queries must be >0" );
                                        exit( EXIT_FAILURE );
                                }
                                NQUERY = (int)t;
                                break;

                        case '?':
                                usage();
                                print_error( "check flags" );
                                exit( EXIT_FAILURE );
                                break;

                        default:
                                print_error( "default" );
                                exit( EXIT_FAILURE );
                }
        }
        return;
}

// Description:
//      Resolve an ip from a hostname
// Parameters:
//      host_name -> the host name to resolve
// Returns:
//      The ip_addr if possible, NULL if not
char *get_ip( char *host_name ){
        struct hostent *he;
        struct in_addr **addr_list;
        int i;

        if( (he=gethostbyname( host_name )) == NULL ){
                print_error( "error resolving ip" );
                return NULL;
        }

        addr_list = (struct in_addr**) he->h_addr_list;

        char buffer[3*4+4];
        for( i=0; addr_list[i] != NULL; i++ ){
                strcpy( buffer, inet_ntoa( *addr_list[i]) );
                char *ip = (char *)malloc( (strlen(buffer)+1)*sizeof(char));
                if( ip == NULL )
                        return NULL;
                strcpy( ip, buffer );
                return ip;
        }
        return NULL;
}

// Description:
//      Get a host name from an IP
// Parameters:
//      iphdr -> the iphdr from a host
// Returns:
//      the hostname
char *get_host( struct IP_HDR *iphdr ){
        struct hostent *h;
        struct in_addr ai = iphdr->src_addr;
        h = gethostbyaddr(&ai, sizeof(ai), AF_INET);
        static char buffer[150];
        if( h == NULL || h->h_name == NULL )
                return inet_ntoa( iphdr->src_addr );
        strcpy( buffer, h->h_name );
        return buffer;
}

// Description:
//      Calc the rtt in milliseconds
//      this is a different function declaration than
//      i usually do but I think I am going to start using it
//      more. This was a test
// Parameters:
//      t1 -> the initial time
//      t2 -> the final time
// Returns:
//      the delta time
double 
calc_millis( t1, t2 )
        struct timeval *t1,*t2;
{       
        double dt = (double)(t2->tv_sec - t1->tv_sec)*1000.0 +
                    (double)(t2->tv_usec - t1->tv_usec)/1000.0;
        return dt;
}

// Description:
//      The driver program
// Parameters:
//      argc -> number of argument
//      argv -> the arguments
// Note:
//      See usage message for details
int main( int argc, char **argv ){
        // drop privileges
        drop_priv_temp( getuid() );

        // make sure we have at least something on command line 
        if( argc < 2 ){
                usage();
                exit( EXIT_FAILURE );
        }

        // extract commandline args
        process_commandline( argc, argv );

        // set up signal handlers
        signal( SIGKILL, sig_handler );
        signal( SIGINT, sig_handler );

        // create socket
        restore_priv();
        int sockfd = create_socket();
        drop_priv_temp( getuid() );
        if( sockfd < 0 ){
                print_error( "error opening socket" );
                exit( EXIT_FAILURE );
        }
        
        // get destination address from host 
        char *host_name = argv[argc-1];
        char *host_ip;
        if( inet_addr( host_name ) == INADDR_NONE ){
                host_ip = get_ip(host_name);
                if( host_ip == NULL ){
                        print_error( "hostname resolve error" );
                        exit( EXIT_FAILURE );
                }
        } else {
                host_ip = host_name;
        }
        struct sockaddr_in *sa_in = create_sockaddr( host_ip, 0 );
        if( sa_in == NULL ){
                print_error( "createsockaddr: error" );
                exit( EXIT_FAILURE );
        }

        // create struct of icmp headers
        struct icmphdr *hdrs = malloc( sizeof( struct icmphdr ) * NQUERY );
        if( hdrs == NULL ){
                print_error( "malloc failed" );
                exit( EXIT_FAILURE );
        }

        memset( hdrs, 0, sizeof(struct icmphdr)*NQUERY );
        for( int i = 0; i < NQUERY; i++ ){
                // allocate memory
                // iovecs[i] = malloc( sizeof( struct iovec) );

                // init icmp header
                (hdrs + i)->code = 0;
                (hdrs + i)->type = ICMP_ECHO;
                (hdrs + i)->checksum = 0;
                (hdrs + i)->checksum = make_cksum((u_short*)(hdrs + i), 8);
        }

        // create pfd structure
        struct pollfd pfd;
        pfd.events = POLLIN;
        pfd.fd = sockfd;

        // start traceroute
        char ttl = 0;
        printf( INIT_MSG, host_name, host_ip, NHOPS, 64 );
        struct timeval rtt_init[NQUERY];
        struct timeval recv_time;
        int buffer[BUFF_SIZE*2];
        int got_to_dest = 0;

        // so the destination address gets set and write() can be used
        while( looping && ttl < NHOPS ){

                // update ttl
                ++ttl;
                if( setsockopt( sockfd, IPPROTO_IP, IP_TTL, 
                                        (char*)&ttl, sizeof(ttl))){
                        print_error( "setsockopt(): ttl failed" );
                        break;
                }
                
                // init rtt_init times
                for( int i = 0; i < NQUERY;i++){
                        sendto( sockfd, (hdrs+i),
                                        sizeof(hdrs[i]), 0, 
                                        (struct sockaddr*)sa_in, 
                                        sizeof( struct sockaddr ) );
                        gettimeofday( &rtt_init[i], NULL);
                }
               
                int n = 0; // count number of responses from queries
                printf( " %2d ", ttl );
                (void)fflush(stdout);

                // recieve the message
                while( looping && n<NQUERY ){

                        // listen for input
                        int p = poll( &pfd, 1, TIMEOUT );
                        
                        // poll failed
                        if( p < 0 ){
                                print_error( "poll failed" );
                                exit( EXIT_FAILURE );
                        }

                        // poll timed out
                        if( p == 0 ){
                                printf( " *" );
                                (void)fflush( stdout );
                                n++;
                        }

                        // poll worked
                        if( p >= 1 && pfd.revents == POLLIN ){
                                // read iovecs
                                //int bytes = recvmsg(sockfd, &read_msghdr, 0);
                                int bytes = recv( sockfd, 
                                                buffer, 
                                                BUFF_SIZE*2, 0);
                                // save time and calculate dt
                                gettimeofday( &recv_time, NULL );
                                double dt = calc_millis( &rtt_init[n],&recv_time );

                                // recv failed
                                if( bytes == -1 ){
                                        print_error( "readv" );
                                        exit( EXIT_FAILURE );
                                }

                                // print host and rtt time
                                struct IP_HDR *iphdr = (struct IP_HDR*)buffer;
                                if( SYMNUM ){
                                        char *name = get_host( iphdr );
                                        char *ip = inet_ntoa( iphdr->src_addr);
                                        printf( " %s (%s) %2.3f ms",name,ip,dt);
                                } else {
                                        printf( " %s %2.3f ms", 
                                                inet_ntoa( iphdr->src_addr ),
                                                dt );
                                }
                                (void)fflush( stdout );
                                
                                // update response counter and check if
                                // found destination
                                n++;
                                if( iphdr->src_addr.s_addr ==
                                         sa_in->sin_addr.s_addr){
                                        got_to_dest = 1;
                                }
                        }

                }

                // stop looping if found dest
                if( got_to_dest )
                        looping = 0;
                printf( "\n" );

        }
               
        // done
        free( host_ip );
        free( hdrs );
        free( sa_in );
        return 0;
}

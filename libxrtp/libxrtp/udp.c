/***************************************************************************
                          port_udp.c  -  UDP port
                             -------------------
    begin                : Fri Mar 28 2003
    copyright            : (C) 2003 by Heming Ling
    email                : 
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
 
 #include "session.h"
 #include "const.h"
 #include <stdlib.h>
 #include <stdio.h>
 #include <unistd.h>    /* FIXME: port to win32 ? used by close(socket) */
 #include <string.h>
 #include <sys/socket.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
 
 #include <sys/select.h>

 #ifdef UDP_LOG
   const int udp_log = 1;
 #else
   const int udp_log = 0;
 #endif
 #define udp_log(fmt, args...)  do{if(udp_log) printf(fmt, ##args);}while(0)

 #define LOOP_RTP_PORT  5000
 #define LOOP_RTCP_PORT  5001

 #define UDP_FLAGS 0  /* Refer to man(sendto) */

 #define UDP_MAX_LEN   5000

 struct session_connect_s{
 
    xrtp_port_t * port;
    
    xrtp_hrtime_t hrt_arrival;
    xrtp_lrtime_t lrt_arrival;

    struct sockaddr_in remote_addr;
    int addrlen;

    char *data_in;
    int datalen_in;
 };

  struct xrtp_port_s{

    enum port_type_e type;

    int socket;

    xrtp_session_t * session;

    xthr_lock_t * lock;
  };

  struct xrtp_teleport_s{

    uint32 portno;
    uint32 addr;
  };

  session_connect_t * connect_new(xrtp_port_t * port, xrtp_teleport_t * tport){

     session_connect_t * udp = (session_connect_t *)malloc(sizeof(struct session_connect_s));
     if(!udp)
        return NULL;
        
     memset(udp, 0, sizeof(struct session_connect_s));
    
     udp->port = port;

     udp->remote_addr.sin_family = AF_INET;
     udp->remote_addr.sin_port = tport->portno;       /* Already in Network byteorder, see teleport_new() */
     udp->remote_addr.sin_addr.s_addr = tport->addr;  /* Already in Network byteorder, see inet_addr() */

     udp_log("connect_new: new conect to ip[%u]:%u\n", udp->remote_addr.sin_addr.s_addr, ntohs(udp->remote_addr.sin_port));

     return udp;   
  }

  int connect_done(session_connect_t * conn){

     udp_log("connect_done: will free connect[@%d] io=%d\n", (int)(conn), connect_io(conn));

     if(conn->data_in) free(conn->data_in);
     free(conn);

     return XRTP_OK;
  }

  int connect_io(session_connect_t * conn){

     return conn->port->socket;
  }

  int connect_match(session_connect_t * conn1, session_connect_t * conn2){

      return conn1->remote_addr.sin_family == conn2->remote_addr.sin_family &&
             conn1->remote_addr.sin_addr.s_addr == conn2->remote_addr.sin_addr.s_addr &&
             conn1->remote_addr.sin_port == conn2->remote_addr.sin_port;
  }

  int connect_from_teleport(session_connect_t * conn, xrtp_teleport_t * tport){

      return conn->remote_addr.sin_family == AF_INET &&
             conn->remote_addr.sin_addr.s_addr == tport->addr &&
             conn->remote_addr.sin_port == tport->portno;
  }

  int connect_receive(session_connect_t * conn, char **r_buff, int bufflen, xrtp_hrtime_t *r_hrts, xrtp_lrtime_t *r_lrts){

      int datalen = conn->datalen_in;
      *r_buff = conn->data_in;
      
      conn->data_in = NULL;
      conn->datalen_in = 0;

      *r_hrts = conn->hrt_arrival;
      *r_lrts = conn->lrt_arrival;
      
      return datalen;
  }

  int connect_send(session_connect_t * conn, char *data, int datalen){
                                                                                 
      /* test */                                                                               
      udp_log("connect_send: socket[%d] sent %d bytes data[@%u] to [%s:%u] \n", conn->port->socket, datalen, (int)data, inet_ntoa(conn->remote_addr.sin_addr), ntohs(conn->remote_addr.sin_port));
      int n = sendto(conn->port->socket, data, datalen, 0, (struct sockaddr *)&(conn->remote_addr), sizeof(struct sockaddr_in));

      return n;
  }

  /* These are for old static port protocol, which is rtcp is rtp + 1, It's NOT suggest to use, Simply for back compatability */
  session_connect_t * connect_rtp_to_rtcp(session_connect_t * rtp_conn){

      session_connect_t * rtcp_conn = (session_connect_t *)malloc(sizeof(struct session_connect_s));
      if(!rtcp_conn)
        return NULL;

      memset(rtcp_conn, 0, sizeof(struct session_connect_s));

      xrtp_port_t *rtp_port, *rtcp_port;
      
      xrtp_session_t * ses = rtp_conn->port->session;
      session_ports(ses, &rtp_port, &rtcp_port);
      
      rtcp_conn->port = rtcp_port;

      rtcp_conn->remote_addr.sin_family = AF_INET;

      uint32 pno = ntohs(rtp_conn->remote_addr.sin_port);      
      rtcp_conn->remote_addr.sin_port = htons(++pno);       /* rtcp port = rtp port + 1 */

      rtcp_conn->remote_addr.sin_addr.s_addr = rtp_conn->remote_addr.sin_addr.s_addr;  /* Already in Network byteorder, see inet_addr() */

      udp_log("connect_rtp_to_rtcp: rtp connect is ip[%s:%u] at socket[%d]\n", inet_ntoa(rtp_conn->remote_addr.sin_addr), ntohs(rtp_conn->remote_addr.sin_port), rtp_conn->port->socket);
      udp_log("connect_rtp_to_rtcp: rtcp connect is ip[%s:%u] at socket[%d]\n", inet_ntoa(rtcp_conn->remote_addr.sin_addr), ntohs(rtcp_conn->remote_addr.sin_port), rtcp_conn->port->socket);

      return rtcp_conn;
  }
  
  session_connect_t * connect_rtcp_to_rtp(session_connect_t * rtcp_conn){

      session_connect_t * rtp_conn = (session_connect_t *)malloc(sizeof(struct session_connect_s));
      if(!rtp_conn){
        
        udp_log("connect_rtcp_to_rtp: Fail to allocate memery for rtp connect\n");
        return NULL;
      }
      
      udp_log("connect_rtcp_to_rtp: rtcp_connect_io=%d\n", rtcp_conn->port->socket);
      udp_log("connect_rtcp_to_rtp: rtp_conn[@%d:%d], rtcp_conn[@%d:%d]\n", (int)(rtp_conn), sizeof(struct session_connect_s), (int)(rtcp_conn), sizeof(struct session_connect_s));
      
      memset(rtp_conn, 0, sizeof(struct session_connect_s));

      xrtp_port_t *rtp_port, *rtcp_port;

      udp_log("connect_rtcp_to_rtp: rtcp_conn->port[@%d]\n", (int)(rtcp_conn->port));
      xrtp_session_t * ses = rtcp_conn->port->session;
      session_ports(ses, &rtp_port, &rtcp_port);

      
      rtp_conn->port = rtp_port;

      rtp_conn->remote_addr.sin_family = AF_INET;

      uint32 pno = ntohs(rtcp_conn->remote_addr.sin_port);
      rtp_conn->remote_addr.sin_port = htons(--pno);       /* rtp port = rtcp port - 1 */

      rtp_conn->remote_addr.sin_addr.s_addr = rtcp_conn->remote_addr.sin_addr.s_addr;  /* Already in Network byteorder, see inet_addr() */

      udp_log("connect_rtcp_to_rtp: rtcp connect is ip[%s:%u] at socket[%d]\n", inet_ntoa(rtcp_conn->remote_addr.sin_addr), ntohs(rtcp_conn->remote_addr.sin_port), rtcp_conn->port->socket);
      udp_log("connect_rtcp_to_rtp: rtp connect is ip[%s:%u] at socket[%d]\n", inet_ntoa(rtp_conn->remote_addr.sin_addr), ntohs(rtp_conn->remote_addr.sin_port), rtp_conn->port->socket);

      return rtp_conn;
  }

  xrtp_port_t * port_new(char * local_addr,  uint32 local_portno, enum port_type_e type){

     if(!local_addr) return NULL;

     xrtp_port_t * port = (xrtp_port_t *)malloc(sizeof(struct xrtp_port_s));

     if(!port) return NULL;

     port->session = NULL;
     port->type = type;

     port->socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
     /*
     setsockopt(port->socket, SOL_IP, IP_PKTINFO, (void *)1, sizeof(int));
     */
              
     struct sockaddr_in sin;

     sin.sin_family = AF_INET;
     sin.sin_port = htons(local_portno);
     if(inet_aton(local_addr, &(sin.sin_addr)) == 0){ /* string to int addr */

        udp_log("port_new: Illegal ip address\n");
        close(port->socket);
        free(port);
        
        return NULL;
     }
     
     if(bind(port->socket, (struct sockaddr *)&sin, sizeof(struct sockaddr_in)) == -1){

        udp_log("port_new: Fail to bind socket\n");
        close(port->socket);
        free(port);
           
        return NULL;
     }
        
     port->lock = xthr_new_lock();

     udp_log("port_new: socket[%u] opened as local ip[%u]:%u\n", port->socket, sin.sin_addr.s_addr, ntohs(sin.sin_port));

     return port;
  }

  int port_set_session(xrtp_port_t * port, xrtp_session_t * ses){

     port->session = ses;

     return XRTP_OK;
  }

  int port_done(xrtp_port_t * port){

     close(port->socket);
     xthr_done_lock(port->lock);
     free(port);

     return XRTP_OK;
  }

  int port_io(xrtp_port_t * port){

     return port->socket;
  }

  int port_match_io(xrtp_port_t * port, int io){

     return port->socket == io;
  }

  /* Not implemented multicast yet */
  int port_is_multicast(xrtp_port_t * port){

     return XRTP_NO;
  }

  int port_poll(xrtp_port_t * port, xrtp_hrtime_t timeout){

      /* Note: if need nanosecond time resolution, use pselect and timespec
       * Not a little confusion in glibc about its sys/select.h, which related to
       * __USE_XOPEN2K varible, and pselect is not recognized on my system, Strange!
       *
       * Currently use select insteed but only microsecond resolution
       */
     struct timeval tout;
     tout.tv_sec = timeout / HRTIME_SECOND_DIVISOR;     
     tout.tv_usec = (timeout % HRTIME_SECOND_DIVISOR) / HRTIME_MICRO;

     fd_set io_set;
     
     FD_ZERO(&io_set);
     FD_SET(port->socket, &io_set);

     /* Warning: High Frequent output */
     udp_log("port_poll: check incoming and timeout is set to %d seconds %d microseconds\n", (int)(tout.tv_sec), (int)(tout.tv_usec));

     int n = select(port->socket+1, &io_set, NULL, NULL, &tout);

     if(n == 1)
        port_incoming(port);

     return n;
  } 

  int port_incoming(xrtp_port_t * port){

     char data[UDP_MAX_LEN];
     struct sockaddr_in remote_addr;
     int addrlen, datalen;

     udp_log("port_incoming: incoming\n");
     
     /* Determine the incoming packet address from recvfrom return */
     datalen = recvfrom(port->socket, data, UDP_MAX_LEN, UDP_FLAGS, (struct sockaddr *)&remote_addr, &addrlen);

     if(!port->session){

        udp_log("port_incoming: discard data from [%s:%d] as no session bound to the port\n", inet_ntoa(remote_addr.sin_addr), ntohs(remote_addr.sin_port));

        return XRTP_EREFUSE;
     }

     /* From Address 0:0 is unacceptable */
     if(remote_addr.sin_addr.s_addr == 0 && remote_addr.sin_port == 0){     

        udp_log("port_incoming: discard data from [%s:%d] which is unacceptable !\n", inet_ntoa(remote_addr.sin_addr), ntohs(remote_addr.sin_port));

        return XRTP_EREFUSE;
     }
        
     session_connect_t * conn = (session_connect_t *)malloc(sizeof(struct session_connect_s));
     if(!conn){

        udp_log("port_incoming: fail to allocate connect of incoming\n");
        return XRTP_EMEM;
     }     
     bzero(conn, sizeof(struct session_connect_s));

     udp_log("port_incoming: connect[@%d] created\n", (int)(conn));

     conn->data_in = (char *)malloc(datalen);
     if(!conn->data_in){

        udp_log("port_incoming: fail to allocate data space of incoming connect\n");
        
        free(conn);
        return XRTP_EMEM;
     }
     bzero(conn->data_in, datalen);
     
     conn->addrlen = sizeof(struct sockaddr_in);
     conn->datalen_in = datalen;
     conn->remote_addr = remote_addr;
     conn->port = port;
     
     if(conn->datalen_in > 0)
        memcpy(conn->data_in, data, datalen);

     time_now(port->session->clock, &(conn->lrt_arrival), &(conn->hrt_arrival));
     
     udp_log("port_incoming: socket[%d] received %d bytes data by connect_io[%d] from [%s:%d] on lrt[%d]:hrt[%d]\n", conn->port->socket, datalen, (int)(conn), inet_ntoa(conn->remote_addr.sin_addr), ntohs(conn->remote_addr.sin_port), conn->lrt_arrival, conn->hrt_arrival);

     if(port->type == RTP_PORT){
       
        session_rtp_arrived(port->session, conn); /* High speed schedle */
         
     }else{
       
        session_rtcp_arrival_notified(port->session, conn, conn->lrt_arrival); /* Low speed schedule */
     }
        
     return XRTP_OK;
  }

  xrtp_teleport_t * teleport_new(char * addr_str, uint32 pno){

     xrtp_teleport_t * tp = malloc(sizeof(struct xrtp_teleport_s));
     if(tp){

        tp->portno = htons(pno);
        tp->addr = inet_addr(addr_str);
        if(inet_aton(addr_str, (struct in_addr *)&(tp->addr)) == 0){ /* string to int addr */

            udp_log("teleport_new: Illegal ip address\n");
            free(tp);

            return NULL;
        }
     }
     
     udp_log("teleport_new: new remote port is ip[%u]:%u\n", tp->addr, ntohs(tp->portno));

     return tp;
  }

  int teleport_done(xrtp_teleport_t * tport){

     free(tport);
     return XRTP_OK;
  }

  char * teleport_name(xrtp_teleport_t * tport){

     struct in_addr addr;
     addr.s_addr = tport->addr;
     
     return inet_ntoa(addr);
  }

  uint32 teleport_portno(xrtp_teleport_t * tport){

     return ntohs(tport->portno);
  }

/***************************************************************************
                          speex_rtp.c  -  Speex profile for rtp
                             -------------------
    begin                : Mon Sep 6 2004
    copyright            : (C) 2004 by Heming
    email                : heming@bigfoot.com; heming@sf.net
 ***************************************************************************/
 
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "../devices/dev_rtp.h"
#include "speex_info.h"

#include <timedia/xmalloc.h>
#include <timedia/xstring.h>
#include <timedia/xthread.h>
#include <ogg/ogg.h>
 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SPXRTP_LOG
#define SPXRTP_DEBUG
 
#ifdef SPXRTP_LOG
 #define spxrtp_log(fmtargs)  do{printf fmtargs;}while(0)
#else
 #define spxrtp_log(fmtargs)
#endif

#ifdef SPXRTP_DEBUG
 #define spxrtp_debug(fmtargs)  do{printf fmtargs;}while(0)
#else
 #define spxrtp_debug(fmtargs)
#endif

#define MAX_PAYLOAD_BYTES 1280 /* + udp header < 1500 bytes */
#define MAX_BUNDLE_PACKET_BYTES 256
#define MAX_PACKET_BUNDLE 32

#define JITTER_ADJUSTMENT_LEVEL 3
#define SPX_SAMPLING_INSTANCE 160
#define SPEEX_MIME "audio/speex"

char speex_mime[] = "audio/speex";

char spxrtp_desc[] = "Speex Profile for RTP";

int num_handler;

module_loadin_t xrtp_handler;

profile_class_t *spxrtp;

typedef struct spxrtp_handler_s spxrtp_handler_t;
typedef struct spxrtp_media_s spxrtp_media_t;

struct spxrtp_handler_s
{
   struct profile_handler_s profile;

   void *master;

   spxrtp_media_t *speex_media;
   xrtp_session_t * session;

   uint rtp_size;
   uint rtcp_size;

   int frame_per_rtp;

   int new_group;
   rtp_frame_t *group_first_frame;

   /* a queue of speex packet to bundle into one rtp packet */
   xlist_t *packets;

   buffer_t *payload_buf; /* include paylaod head */

   uint32 usec_payload_timestamp;

   uint32 timestamp_send;  /* of current payload to send, frome randem value */

   int max_nframe_per_rtp;	/* frames in the rtp packet */
   int nframe_per_packet;	/* frames in the ogg packet */
   int penh;

   /* thread attribute */
   xthread_t *thread;

   int64 recent_samplestamp;
   rtp_frame_t *recent_frame;

   int run;
   int idle;
   int receiving;

   xthr_lock_t *packets_lock;
   xthr_cond_t *pending;

   int rtp_portno;
   int rtcp_portno;
};

struct spxrtp_media_s
{
   struct xrtp_media_s rtp_media;

   spxrtp_handler_t *speex_profile;
};

/**
 * Parse rtp packet payload
 *
 * RFC3550 indicate: 
 * Applications transmitting stored data rather than data sampled in real time typically 
 * use a virtual presentation timeline derived from wallclock time to determine when the 
 * next frame or other unit of each medium in the stored data should be presented.
 * 
 * So, the timestamp is the usec frome the randem value when the stream start.
 *
 * Timestamp initial value is random, generate timestamp CAN NOT relay on stamp in the 
 * media storage, MUST generate on the fly according to the media clock that increase in 
 * a linear and monotonic fashion (except wrap-around); 
 **/

/**
 * Handle inward rtp packet
 */
int spxrtp_rtp_in(profile_handler_t *h, xrtp_rtp_packet_t *rtp)
{
   spxrtp_handler_t *spxh = (spxrtp_handler_t *)h;
   
   uint16 seqno;
   uint32 src;

   uint32 rtpts_payload;
   uint32 rtpts_arrival;
   uint32 rtpts_toplay;
   uint32 rtpts_playing;
   uint32 rtpts_sync;
   
   member_state_t * sender = NULL;
   session_connect_t *rtp_conn = NULL;
   session_connect_t *rtcp_conn = NULL;

   rtime_t ms_arrival,us_arrival,ns_arrival;
   rtime_t ms_playing, us_playing, ns_playing;
   rtime_t ms_sync, us_sync, ns_sync;
   rtime_t us_since_sync;

   xrtp_media_t *rtp_media;

   rtp_unpack(rtp);

   seqno = rtp_packet_seqno(rtp);
   src = rtp_packet_ssrc(rtp);

   rtpts_payload = rtp_packet_timestamp(rtp);
   spxrtp_log(("audio/speex.spxrtp_rtp_in: packet ssrc=%u, seqno=%u, timestamp=%u, rtp->connect=%d\n", src, seqno, rtpts_payload, (int)rtp->connect));

   sender = session_member_state(rtp->session, src);
   if(sender->media_playable == -1)
   {
	  spxrtp_debug(("audio/speex.vrtp_rtp_in: Member[%s] unplayable!\n", sender->cname));

      rtp_packet_done(rtp);

      return XRTP_CONSUMED;
   }

   if(sender && sender->valid && !connect_match(rtp->connect, sender->rtp_connect))
   {
      /* this could be true if sender behind the firewall */
	  spxrtp_debug(("audio/speex.vrtp_rtp_in: put NAT in mind\n"));

	  session_solve_collision(sender, src);

      rtp_packet_done(rtp);

      return XRTP_CONSUMED;
   }

   if(!sender)
   {
      /* The rtp packet is recieved before rtcp arrived, so the participant is not identified yet */
      if(!spxh->session->$state.receive_from_anonymous)
	  {
         spxrtp_log(("audio/speex.spxrtp_rtp_in: participant waiting for identifed before receiving media\n"));
         rtp_packet_done(rtp);

         return XRTP_CONSUMED;
      }

      /* Get connect of the participant */
      rtp_conn = rtp->connect;
      rtcp_conn = NULL;

      rtp->connect = NULL; /* Dump the connect from rtcp packet */

      sender = session_new_member(spxh->session, src, NULL);
      if(!sender)
	  {
         /* Something WRONG!!! packet disposed */
         rtp_packet_done(rtp);
         return XRTP_CONSUMED;
      }

      if(spxh->session->$callbacks.member_connects)
	  {
         spxh->session->$callbacks.member_connects(spxh->session->$callbacks.member_connects_user, src, &rtp_conn, &rtcp_conn);
         session_member_set_connects(sender, rtp_conn, rtcp_conn);
      }
	  else
	  {
         rtcp_conn = connect_rtp_to_rtcp(rtp->connect); /* For RFC1889 static port allocation: rtcp port = rtp port + 1 */
         session_member_set_connects(sender, rtp->connect, rtcp_conn);
      }

      if(spxh->session->join_to_rtp_port && connect_from_teleport(rtp->connect, spxh->session->join_to_rtp_port))
	  {
         /* participant joined, clear the join desire */
         teleport_done(spxh->session->join_to_rtp_port);
         spxh->session->join_to_rtp_port = NULL;

         teleport_done(spxh->session->join_to_rtcp_port);
         spxh->session->join_to_rtcp_port = NULL;
      }
   }

   if(!sender->valid)
   {
      /* The rtp packet is recieved before rtcp arrived, so the participant is not identified yet */
      if(!spxh->session->$state.receive_from_anonymous)
	  {
         spxrtp_log(("audio/speex.spxrtp_rtp_in: participant waiting for validated before receiving media\n"));
         rtp_packet_done(rtp);

         return XRTP_CONSUMED;
      }

      if(!session_update_seqno(sender, seqno))
	  {
         rtp_packet_done(rtp);
         return XRTP_CONSUMED;
      }
	  else
	  {
         if(!sender->rtp_connect)
		 {
            /* Get connect of the participant */
            rtp_conn = rtp->connect;
            rtcp_conn = NULL;

            rtp->connect = NULL; /* Dump the connect from rtcp packet */

            if(spxh->session->$callbacks.member_connects)
			{
               spxh->session->$callbacks.member_connects(spxh->session->$callbacks.member_connects_user, src, &rtp_conn, &rtcp_conn);
               session_member_set_connects(sender, rtp_conn, rtcp_conn);
            }
			else
			{
               rtcp_conn = connect_rtp_to_rtcp(rtp->connect); /* For RFC1889 static port allocation: rtcp port = rtp port + 1 */
               session_member_set_connects(sender, rtp->connect, rtcp_conn);
            }
         }

         sender->valid = 1;
      }
   }

   if(session_member_update_by_rtp(sender, rtp) < XRTP_OK)
   {
	   spxrtp_log(("audio/speex.spxrtp_rtp_in: bad packet\n"));
	   rtp_packet_done(rtp);

	   return XRTP_CONSUMED;
   }

   if(session_member_seqno_stall(sender, seqno))
   {
	   spxrtp_log(("audio/speex.spxrtp_rtp_in: seqno[%u] is stall, discard\n", seqno));
	   rtp_packet_done(rtp);

	   return XRTP_CONSUMED;
   }

   /* calculate play timestamp */
   rtp_packet_arrival_time(rtp, &ms_arrival, &us_arrival, &ns_arrival);
   session_member_media_playing(sender, &rtpts_playing, &ms_playing, &us_playing, &ns_playing);

   rtp_media = (xrtp_media_t*)spxh->speex_media;

   rtpts_arrival = rtpts_playing - (uint32)((us_playing - us_arrival)/1000000.0 * rtp_media->clockrate);

   rtpts_toplay = session_member_mapto_local_time(sender, rtpts_payload, rtpts_arrival, JITTER_ADJUSTMENT_LEVEL);

   if(!sender->media_playable && sender->media_playable != -1)
   {
	   uint32 rtpts_mi;
	   int signum;

	   speex_info_t *spxinfo;
	   
	   spxinfo = (speex_info_t*)session_member_mediainfo(sender, &rtpts_mi, &signum);
	   
	   if(!spxinfo)
	   {
			spxrtp_log(("audio/vorbis.vrtp_rtp_in: invalid media info\n"));
			rtp_packet_done(rtp);

			return XRTP_CONSUMED;
	   }

	   {
		   int ret;
		   media_player_t *player, *explayer = NULL;
		   media_control_t *ctrl;

		   ctrl = (media_control_t*)session_media_control(spxh->session);

		   player = ctrl->find_player(ctrl, "playback", SPEEX_MIME, "", NULL);

		   ret = player->open_stream(player, (media_info_t*)spxinfo);
		   if( ret < MP_OK)
		   {
				sender->media_playable = -1;
				player->done(player);
			
				spxrtp_log(("audio/speex.rtp_in: media is not playable\n"));
				rtp_packet_done(rtp);
			
				return XRTP_CONSUMED;
		   }         

		   explayer = (media_player_t*)session_member_set_player(sender, player);
		   if(explayer)
			   explayer->done(explayer);

		   sender->media_playable = 1;

		   /* start media delivery */
		   session_member_deliver(sender, seqno, spxinfo->nheader);
	   }
   }
   else if(!sender->deliver_thread)
   {
		int signum;
		uint32 rtpts_mi;
		speex_info_t *spxinfo;
	   
		spxinfo = (speex_info_t*)session_member_mediainfo(sender, &rtpts_mi, &signum);
	   
		/* start media delivery */
		session_member_deliver(sender, seqno, spxinfo->nheader);
   }

   /* convert rtp timestamp to usec */
   session_member_sync_point(sender, &rtpts_sync, &ms_sync, &us_sync, &ns_sync);
   us_since_sync = (uint32)((rtpts_toplay - rtpts_sync)/1000000.0 * rtp_media->clockrate);
   
   /* convert rtp packet to media frame */
   {   
		int last = 1;
		char *payload;

		/* single complete ogg packet */
		media_frame_t *mf = xmalloc(sizeof(media_frame_t));

		mf->bytes = rtp_packet_dump_payload(rtp, &payload);
		mf->raw = payload;

		/* convert rtp timestamp to granulepos */
		mf->samplestamp = session_member_samples(sender, rtpts_payload);
			
		session_member_hold_media(sender, (void*)mf, mf->bytes, seqno, rtpts_toplay, us_sync, us_since_sync, last, payload);
   }

   rtp_packet_done(rtp);

   return XRTP_CONSUMED;
}

/**
 * put payload into the outward rtp packet
 */
int spxrtp_rtp_out(profile_handler_t *handler, xrtp_rtp_packet_t *rtp)
{
   spxrtp_handler_t * profile = (spxrtp_handler_t *)handler;

   /* Mark always '0', Audio silent suppression not used */
   rtp_packet_set_mark(rtp, 0);

   /* timestamp */
   rtp_packet_set_timestamp(rtp, profile->timestamp_send);

   rtp_packet_set_payload(rtp, profile->payload_buf);
   /*
   vrtp_log(("audio/speex.vrtp_rtp_out: payload[%u] (%d bytes)\n", profile->timestamp_send, buffer_datalen(profile->payload_buf)));
   */
   if(!rtp_pack(rtp))
   {
      spxrtp_log(("audio/speex.vrtp_rtp_out: Fail to pack rtp data\n"));

      return XRTP_FAIL;
   }

   /* unset after pack into the rtp buffer */
   rtp_packet_set_payload(rtp, NULL);
   
   return XRTP_OK;
}

/**
 * When a rtcp come in, need to do following:
 * - If a new src, add to member list, waiting to validate.
 * - If is a SR, do nothing before be validated, otherwise check
 * - If is a RR, check with session self
 */
int spxrtp_rtcp_in(profile_handler_t *handler, xrtp_rtcp_compound_t *rtcp)
{
	uint32 src_sender = 0;
	uint32 rtcpts = 0, packet_sent = 0,  octet_sent = 0;

	uint8 frac_lost = 0;
	uint32 total_lost = 0, full_seqno, jitter = 0;
	uint32 lsr_stamp = 0, lsr_delay = 0;

	member_state_t * sender = NULL;

	rtime_t ms,us,ns;

	xrtp_session_t * ses = rtcp->session;
	xrtp_media_t* rtp_media = session_media(ses);
   
	spxrtp_handler_t *profile = (spxrtp_handler_t*)handler;
	member_state_t *myself = session_owner(ses);

	/* Here is the implementation */
	profile->rtcp_size = rtcp->bytes_received;

	if(!rtcp->valid_to_get)
	{
		rtcp_unpack(rtcp);
		rtcp->valid_to_get = 1;

		/* rtcp_show(rtcp); */
	}

	session_count_rtcp(ses, rtcp);

	src_sender = rtcp_sender(rtcp);

	rtcp_arrival_time(rtcp, &ms, &us, &ns);
	spxrtp_log(("audio/speex.vrtp_rtcp_in: arrived %dB at %dms/%dus/%dns\n", rtcp->bytes_received, ms, us, ns));

	/* rtp_conn and rtcp_conn will be uncertain after this call */

	sender = session_update_member_by_rtcp(rtcp->session, rtcp);
	if(!sender)
	{
		rtcp_compound_done(rtcp);

		spxrtp_log(("audio/speex.vrtp_rtcp_in: Ssrc[%d] refused\n", src_sender));

		return XRTP_CONSUMED;
	}

	if(!profile->receiving)
	{
		session_start_reception(ses);
		profile->receiving = 1;
	}

	if(!sender->valid)
	{
		spxrtp_log(("audio/speex.spxrtp_rtcp_in: Member[%d] is not valid yet\n", src_sender));

		rtcp_compound_done(rtcp);

		return XRTP_CONSUMED;
	}
	else
	{
		uint32 hi_ntp, lo_ntp;

		if(rtcp_sender_info(rtcp, &src_sender, &hi_ntp, &lo_ntp,
                          &rtcpts, &packet_sent, &octet_sent) >= XRTP_OK)
		{
			/* Check the SR report */
		  
			/* record the sync reference point */
			rtcp_arrival_time(rtcp, &ms, &us, &ns);

			/* convert rtcpts to local ts */
			session_member_synchronise(sender, rtcpts, hi_ntp, lo_ntp, rtp_media->clockrate);

			/* Check the report */
			session_member_check_senderinfo(sender, hi_ntp, lo_ntp,
										 rtcpts, packet_sent, octet_sent);

			sender->lsr_msec = ms;
			sender->lsr_usec = us;

			spxrtp_log(("audio/speex.vrtp_rtcp_in:========================\n"));
			spxrtp_log(("audio/speex.vrtp_rtcp_in: SR report ssrc[%u][%s]\n", sender->ssrc, sender->cname));
			spxrtp_log(("audio/speex.vrtp_rtcp_in: ntp[%u,%u];ts[%u]\n", hi_ntp,lo_ntp,rtcpts));
			spxrtp_log(("audio/speex.vrtp_rtcp_in: sent[%dp;%dB]\n", packet_sent, octet_sent));

			/******************************
			 * Create a player for the remote sender
			 */
			if(!sender->media_playable && sender->media_playable != -1)
			{
				/* useless in Speex codec */
				uint32 rtpts_mi;
				int signum;

				int ret;
				media_player_t *player, *explayer = NULL;
				media_control_t *ctrl;

				speex_info_t *spxinfo = (speex_info_t*)session_member_mediainfo(sender, &rtpts_mi, &signum);
	   
				if(!spxinfo)
				{
					spxrtp_log(("audio/speex.spxrtp_rtcp_in: no valid media info\n"));
					rtcp_compound_done(rtcp);
					return XRTP_CONSUMED;
				}

				ctrl = (media_control_t*)session_media_control(profile->session);
				player = ctrl->find_player(ctrl, "playback", SPEEX_MIME, "", NULL);

				ret = player->open_stream(player, (media_info_t*)spxinfo);
				if( ret < MP_OK)
				{
					sender->media_playable = -1;
					player->done(player);
			
					spxrtp_log(("audio/speex.spxrtp_rtp_in: media is not playable\n"));
					rtcp_compound_done(rtcp);
			
					return XRTP_CONSUMED;
				}         

				explayer = (media_player_t*)session_member_set_player(sender, player);
				if(explayer)
					explayer->done(explayer);

				sender->media_playable = 1;
			}
		}
		else
		{
			/* RR */
			spxrtp_log(("audio/speex.spxrtp_rtcp_in:========================\n"));
			spxrtp_log(("audio/speex.spxrtp_rtcp_in: RR report ssrc[%u][%s] \n", sender->ssrc, sender->cname));
		}
	}

	/******************************
	 * Create a local sender for the remote receiver
	 */
	if(ses->rtp_send_pipe && !myself->media_playable)
	{
		/* useless in Speex codec */
		uint32 rtpts_mi;
		int signum;

		speex_info_t *spxinfo = (speex_info_t*)session_member_mediainfo(myself, &rtpts_mi, &signum);
		if(spxinfo)
		{
			media_control_t *ctrl;

			media_device_t *audio_in;
			media_receiver_t *creater;
			media_player_t *player;

			ctrl = (media_control_t*)session_media_control(ses);

			/* create a netcast player associate to the rtp session */
			player = ctrl->new_player(ctrl, "netcast", SPEEX_MIME, "", ses);

			audio_in = ctrl->find_device(ctrl, "audio_in");
			creater = ctrl->find_creater(ctrl, SPEEX_MIME, (media_info_t*)&spxinfo);

			creater->new_media_stream(creater, audio_in, player, (media_info_t*)&spxinfo);
			
			audio_in->start(audio_in, ctrl);

			myself->media_playable = 1;
		}
	}

	/* find out minority report */
	if(rtcp_report(rtcp, myself->ssrc, &frac_lost, &total_lost,
                      &full_seqno, &jitter, &lsr_stamp, &lsr_delay) >= XRTP_OK)
	{
		session_member_check_report(myself, frac_lost, total_lost, full_seqno,
                                jitter, lsr_stamp, lsr_delay);
	}
	else
	{ /* Strange !!! */ }

	rtcp_compound_done(rtcp);

	spxrtp_log(("audio/speex.vrtp_rtcp_in:************ RTCP[%u]\n", src_sender));

	return XRTP_CONSUMED;
}

/*
 * Following need to be sent by rtcp:
 * - SR/RR depends on the condition
 * - SDES:CNAME
 * - BYE if bye
 */
int spxrtp_rtcp_out(profile_handler_t *handler, xrtp_rtcp_compound_t *rtcp)
{
   spxrtp_handler_t *h = (spxrtp_handler_t *)handler;
   xrtp_session_t *ses = rtcp->session;

   uint8 *app_mi = NULL;

   session_report(h->session, rtcp, h->timestamp_send);
    
   /* Profile specified */
   if(!rtcp_pack(rtcp))
   {
      spxrtp_log(("audio/speex.vrtp_rtcp_out: Fail to pack rtcp data\n"));
      return XRTP_FAIL;
   }

   h->rtcp_size = rtcp_length(rtcp);

   return XRTP_OK;
}

int spxrtp_rtp_size(profile_handler_t *handler, xrtp_rtp_packet_t *rtp)
{
   return ((spxrtp_handler_t *)handler)->rtp_size;
}

int spxrtp_rtcp_size(profile_handler_t *handler, xrtp_rtcp_compound_t *rtcp)
{
   return ((spxrtp_handler_t *)handler)->rtcp_size;
}

/**
 * Methods for module class
 */
profile_class_t * rtp_speex_module(profile_handler_t * handler)
{
   return spxrtp;
}

int rtp_speex_set_master(profile_handler_t *handler, void *master)
{
   spxrtp_handler_t *h = (spxrtp_handler_t *)handler;
   h->master = master;

   return XRTP_OK;
}

void * rtp_speex_master(profile_handler_t *handler)
{
   spxrtp_handler_t *h = (spxrtp_handler_t *)handler;

   return h->master;
}

int rtp_speex_match_mime(xrtp_media_t * media, char *mime)
{
   return !strncmp(speex_mime, mime, strlen(speex_mime));
}

int rtp_speex_done(xrtp_media_t *media)
{
   spxrtp_handler_t *h = ((spxrtp_media_t *)media)->speex_profile;
   h->speex_media = NULL;

   xfree(media);
   return XRTP_OK;
}

int rtp_speex_set_parameter(xrtp_media_t* media, char* key, void* val)
{
	spxrtp_handler_t *profile = ((spxrtp_media_t*)media)->speex_profile;
	
	if(0==strcmp(key, "nframe_per_packet"))
	{
		profile->nframe_per_packet = (int)val;
      
		spxrtp_log(("rtp_speex_set_parameter: nframe_per_packet[%d]\n", profile->nframe_per_packet));
	
		return XRTP_OK;
	}
	
	if(0==strcmp(key, "ptime_max"))
	{
		profile->max_nframe_per_rtp = (int)val / SPX_FRAME_MSEC;
      
		spxrtp_log(("rtp_speex_set_parameter: max_nframe_per_rtp[%d]\n", profile->max_nframe_per_rtp));
	
		return XRTP_OK;
	}
	
	if(0==strcmp(key, "penh"))
	{
		profile->penh = (int)val;
      
		spxrtp_log(("rtp_speex_set_parameter: penh[%d]\n", profile->penh));
	
		return XRTP_OK;
	}
	
	return XRTP_INVALID;
}

int rtp_speex_parameter(xrtp_media_t *media, char* key, void* value)
{
	spxrtp_media_t *spxrtp = (spxrtp_media_t*)media;

	if(0==strcmp(key, "rtp_portno"))
	{
		int *v = (int*)value;
		*v = spxrtp->speex_profile->rtp_portno;
		return MP_OK;
	}
	
	if(0==strcmp(key, "rtcp_portno"))
	{
		int *v = (int*)value;
		*v = spxrtp->speex_profile->rtcp_portno;
		return MP_OK;
	}
	
	if(0==strcmp(key, "nframe_per_packet"))
	{
		int *v = (int*)value;
		*v = spxrtp->speex_profile->nframe_per_packet;
		return MP_OK;
	}
	
	if(0==strcmp(key, "ptime_max"))
	{
		int *v = (int*)value;
		*v = spxrtp->speex_profile->max_nframe_per_rtp * SPX_FRAME_MSEC;
		return MP_OK;
	}
	
	if(0==strcmp(key, "penh"))
	{
		int *v = (int*)value;
		*v = spxrtp->speex_profile->penh;
		return MP_OK;
	}
	
	return MP_INVALID;
}

void* rtp_speex_info(xrtp_media_t* media, void* rtp_cap)
{
	rtpcap_descript_t *rtpcap = (rtpcap_descript_t*)rtp_cap;

	speex_info_t* spxinfo;

	spxinfo = malloc(sizeof(speex_info_t));
	if(!spxinfo)
	{
		spxrtp_debug(("rtp_speex_info: No memory\n"));
		return NULL;
	}

	memset(spxinfo, 0, sizeof(speex_info_t));

	speex_info_from_sdp((media_info_t*)spxinfo, rtpcap->profile_no, rtpcap->sdp_message, 0);

	return spxinfo;
}

int rtp_speex_sdp(xrtp_media_t* media, void* sdp_info)
{
	int ptime_max = 0;
	int penh = 0;
	int pt, rtp_portno, rtcp_portno;
	char *ipaddr;

	int clockrate, coding_param;
	int ret;

	speex_info_t *spxinfo;

	rtpcap_descript_t *rtpcap;

	rtpcap_sdp_t *sdp = (rtpcap_sdp_t*)sdp_info;

	ipaddr = session_address(media->session);

	session_portno(media->session, &rtp_portno, &rtcp_portno);

	pt = session_payload_type(media->session);

	spxrtp_log(("rtp_speex_sdp: #%d %s:%d/%d '%s'\n", pt, ipaddr, rtp_portno, rtcp_portno, speex_mime));
   
	spxinfo = (speex_info_t*)session_mediainfo(media->session);
	if(spxinfo)
	{
		spxrtp_log(("rtp_speex_sdp: clockrate[%dHz], channels[%d]\n", spxinfo->audioinfo.info.sample_rate, spxinfo->audioinfo.channels));
		spxrtp_log(("rtp_speex_sdp: %d frames per packet, %dms per rtp packet, penh=%d\n", spxinfo->nframe_per_packet, spxinfo->ptime, spxinfo->penh));
		
		clockrate = spxinfo->audioinfo.info.sample_rate;
		coding_param = spxinfo->audioinfo.channels;
	}
	else
	{
		clockrate = 8000;
		coding_param = 1;
	}

	/*FIXME: nettype, addrtype */
	rtpcap = rtp_capable_descript(pt, "IN", "IP4", ipaddr, rtp_portno, rtcp_portno, speex_mime, clockrate, coding_param, sdp->sdp_message);

	if(spxinfo)
		ret = speex_info_to_sdp((media_info_t*)spxinfo, rtpcap, sdp->sdp_message, sdp->sdp_media_pos);
	else
	{
		char fmt[4];
		ret = rtp_capable_to_sdp(rtpcap, sdp->sdp_message, sdp->sdp_media_pos);
		sdp_message_m_payload_add (sdp->sdp_message, sdp->sdp_media_pos, xstr_clone(itoa(rtpcap->profile_no, fmt, 10)));
	}

	rtpcap->descript.done(&rtpcap->descript);

	if(ret < MP_OK)
	   return XRTP_FAIL;
	   
	sdp->sdp_media_pos++;

	return XRTP_OK;
}

int rtp_speex_new_sdp(xrtp_media_t *media, char* nettype, char* addrtype, char* netaddr, int* rtp_portno, int* rtcp_portno, int pt, int clockrate, int coding_param, int bw_budget, void* control, void* sdp_info)
{
	int ptime_max = 0;
	int penh = 0;

	speex_setting_t *spxset;
	speex_info_t *spxinfo;
	media_control_t* mctrl = (media_control_t*)control;

	int ret, bw;

	rtpcap_descript_t *rtpcap;

	rtpcap_sdp_t *sdp = (rtpcap_sdp_t*)sdp_info;

	spxset = speex_setting(mctrl);

	/* override default port */
	if(spxset->rtp_setting.rtp_portno > 0)
		*rtp_portno = spxset->rtp_setting.rtp_portno;

	if(*rtp_portno <= 0)
		return 0;

	if(spxset->rtp_setting.rtcp_portno > 0)
		*rtcp_portno = spxset->rtp_setting.rtcp_portno;

	if(*rtcp_portno <= 0)
		return 0;

	spxinfo = xmalloc(sizeof(speex_info_t));
	if(!spxinfo)
	{
		return 0;
	}
		
	speex_info_setting(spxinfo, spxset);

	if(spxinfo->audioinfo.info.bps > bw_budget)
	{
		xfree(spxinfo);
		return 0;
	}

	bw = (int)((2 * (spxinfo->audioinfo.info.bps / OS_BYTE_BITS)) / 0.95);

	rtpcap = rtp_capable_descript(pt, nettype, addrtype, netaddr, *rtp_portno, *rtcp_portno, speex_mime, clockrate, coding_param, sdp->sdp_message);

	ret = speex_info_to_sdp((media_info_t*)spxinfo, rtpcap, sdp->sdp_message, sdp->sdp_media_pos);

	rtpcap->descript.done(&rtpcap->descript);
	xfree(spxinfo);

	if(ret < MP_OK)
	   return 0;
	   
	sdp->sdp_media_pos++;

	return bw;
}

/**
 * Get the media clockrate when the stream is opened
 */
int rtp_speex_set_coding(xrtp_media_t* media, int clockrate, int param)
{
   media->clockrate = clockrate;
   media->coding_parameter = param;

   session_set_rtp_rate(media->session, clockrate);

   return XRTP_OK;
}

int rtp_speex_coding(xrtp_media_t *media, int* clockrate, int* param)
{
	*clockrate = media->clockrate;
	*param = media->coding_parameter;

	return XRTP_OK;
}

uint32 rtp_speex_sign(xrtp_media_t *media)
{
   srand((int)media);
   
   return rand();
}

int rtp_speex_done_frame(void *gen)
{
	rtp_frame_t *rtpf = (rtp_frame_t*)gen;

	rtpf->frame.owner->recycle_frame(rtpf->frame.owner, (media_frame_t*)rtpf);

	return MP_OK;
}

/**
 * Make rtp packet payload
 */
int rtp_speex_send_loop(void *gen)
{
	rtp_frame_t *rtpf = NULL;

	int eos=0, eots=0;

	int first_loop= 1;
	int first_group = 1;
	int first_payload = 1;

	spxrtp_handler_t *profile = (spxrtp_handler_t*)gen;

	//rtp_frame_t *remain_frame = NULL;
	char *packet_data = NULL;
	int packet_bytes = 0;

	uint8 payload_nframe = 0;

	int new_group = 0;
	int discard = 0;

	int in_group = 0;

	xclock_t *ses_clock = session_clock(profile->session);

	int payload_samples = 0;

	int first_group_payload = 0;

	rtime_t usec_group_start;
	rtime_t usec_group_end;

	rtime_t usec_stream_payload;

	rtime_t usec_group = 0;

	xlist_t *discard_frames = xlist_new();
	if(!discard_frames)
	{
		spxrtp_log(("rtp_speex_send_loop: no more memory, discard all packets!\n"));
		
		xlist_reset(profile->packets, rtp_speex_done_frame);

		return MP_EMEM;
	}

	/* randem value from current nano second */
	spxrtp_log(("rtp_speex_send_loop: FIXME - timestamp should start at randem value\n"));
   
	profile->run = 1;

	while(1)
	{
		speex_info_t *spxinfo;

		rtime_t usec_now;

		xthr_lock(profile->packets_lock);

		if (profile->run == 0 || eos)
		{			
			profile->run = 0;
			xthr_unlock(profile->packets_lock);
			break;
		}

		if (xlist_size(profile->packets) == 0) 
		{
			profile->idle = 1;
			
			spxrtp_log(("rtp_speex_send_loop: waiting packet to send...\n"));

			xthr_cond_wait(profile->pending, profile->packets_lock);

			spxrtp_log(("rtp_speex_send_loop: waiting is over!\n"));

			profile->idle = 0;
		}

		/* sometime it's still empty, weired? */
		if (xlist_size(profile->packets) == 0)
		{
			xthr_unlock(profile->packets_lock);
			continue;
		}

		new_group = profile->new_group;

		/* retrieve packet */
		rtpf = (rtp_frame_t*)xlist_remove_first(profile->packets);
		packet_data = rtpf->media_unit;
		packet_bytes = rtpf->frame.bytes;

		xthr_unlock(profile->packets_lock);

		/* check time when new group arrived */
		if(profile->new_group && !first_payload)
		{
			usec_now = time_usec_now(ses_clock);
			if(in_group && usec_now > usec_group_end)
			{
				spxrtp_log(("rtp_speex_send_loop: timeout for samplestamp[%lld]\n", profile->recent_samplestamp));
				discard = 1;

				/* new group notice consumed */
				profile->new_group = 0;
			}
		}

		/* discard frames when time is late */
		if(discard)
		{
			xlist_reset(discard_frames, rtp_speex_done_frame);

			if(rtpf->samplestamp == profile->recent_samplestamp)
				rtpf->frame.owner->recycle_frame(rtpf->frame.owner, (media_frame_t*)rtpf);
			
			discard = 0;
			continue;
		}
   
		spxinfo = (speex_info_t*)rtpf->media_info;

		/* frame is retrieved, process ... */
		eots = rtpf->frame.eots;
		eos = rtpf->frame.eos;
		
		/* start to make payload */

		/* If a new timestamp coming */
		if(rtpf->samplestamp != profile->recent_samplestamp || first_loop)
		{
			int64 delta_samplestamp = rtpf->samplestamp - profile->recent_samplestamp;
				
			usec_group = (rtime_t)(1000000 * delta_samplestamp / spxinfo->audioinfo.info.sample_rate);
				
			spxrtp_log(("rtp_speex_send_loop: new group[%lld], ", rtpf->samplestamp));
			spxrtp_log(("usec_group=%dus\n", usec_group));

			profile->recent_samplestamp = rtpf->samplestamp;

			in_group = 1;
			first_group_payload = 1;

			first_loop = 0;
		}

		if(payload_nframe == 0)
			payload_samples = rtpf->samples;
		else
			payload_samples += rtpf->samples;

		/* make payload now */

		/* send packet bundle, the number of frame depend on sdp.ptime attribute */
		{
			buffer_add_data(profile->payload_buf, packet_data, packet_bytes);
			payload_nframe += profile->nframe_per_packet;

			/* frame is done, ask owner to recycle it */
			rtpf->frame.owner->recycle_frame(rtpf->frame.owner, (media_frame_t*)rtpf);
		}

		/* send payload in a rtp packet when end of stream or the rtp nframe limit is reached */
		if(eots || payload_nframe + profile->nframe_per_packet > profile->max_nframe_per_rtp)
		{
			rtime_t usec_payload;
			rtime_t usec_payload_deadline;

			usec_now = time_usec_now(ses_clock);

			if(first_payload)
				usec_stream_payload = usec_now;

			if(first_group_payload)
			{
				/* group usec range from the time send the first rtp payload of the samplestamp */
				if(first_group)
					usec_group_start = usec_now;
				else
					usec_group_start = usec_group_end + 1;

					
				usec_group_end = usec_group_start + usec_group;

				spxrtp_log(("rtp_speex_send_loop: %dus...%dus\n", usec_group_start, usec_group_end));
			}

			usec_payload = (rtime_t)((int64)payload_samples * 1000000 / spxinfo->audioinfo.info.sample_rate);
			usec_payload_deadline = usec_stream_payload + usec_payload;

			/* Test purpose
			spxrtp_log(("audio/speex.rtp_speex_send_loop: payload[%d]@%dus (%dP,%dS,%dus)\n", profile->usec_payload_timestamp, usec_stream_payload, payload_packets, payload_samples, usec_payload));
			spxrtp_log(("audio/speex.rtp_speex_send_loop: %dus now, deadline[%dus]...in #%dus\n", usec_now, usec_payload_deadline, usec_payload_deadline - usec_now));
			 Test end */

			/* call for session sending 
			 * usec left before send rtp packet to the net
			 * deadline<=0, means ASAP, the quickness decided by bandwidth
			 */
			session_rtp_to_send(profile->session, usec_payload_deadline, eots);  

			/* timestamp update */
			profile->timestamp_send += payload_samples;

			profile->usec_payload_timestamp += (int)usec_payload; 

			usec_stream_payload += (rtime_t)usec_payload;

			/* buffer ready to next payload */
			buffer_clear(profile->payload_buf, BUFFER_QUICKCLEAR);
			payload_nframe = 0;
			payload_samples = 0;

			first_group = 0;
			first_payload = 0;
			first_group_payload = 0;
		}

		/* group process on time */
		if(eots)
		{
			in_group = 0;
			spxrtp_log(("audio/speex.rtp_speex_send_loop: end of group @%dus\n", time_usec_now(ses_clock)));
		}

		/* when the stream ends, quit the thread */
		if(eos)
		{
			spxrtp_log(("audio/speex.rtp_speex_send_loop: End of Media, quit\n"));
			break;
		}
	}

	return MP_OK;
}

/*
 * the media enter point of the rtp session 
 */
int rtp_speex_post(xrtp_media_t* media, media_data_t* frame, int data_bytes, uint32 timestamp)
{
   rtp_frame_t *rtpf = (rtp_frame_t*)frame;

   spxrtp_handler_t *profile = ((spxrtp_media_t*)media)->speex_profile;

   xthr_lock(profile->packets_lock);
   
   xlist_addto_last(profile->packets, rtpf);
   if(rtpf->samplestamp != profile->recent_samplestamp)
   {
	   /* when new group coming, old group will be discard 
	    * if could not catch up the time */
	   profile->group_first_frame = rtpf;
	   profile->new_group = 1;
   }

   xthr_unlock(profile->packets_lock);

   if(profile->idle) 
	   xthr_cond_signal(profile->pending);

   return MP_OK;
}

int rtp_speex_play(void *player, void *media, int64 packetno, int ts_last, int eos)
{
	media_player_t *mp = (media_player_t*)player;

	media_frame_t* mf = (media_frame_t*)media;

	mf->sno = packetno;

	if(eos)
		mp->receive_media(mp, mf, mf?mf->samplestamp:-1, MP_EOS);
	else
		mp->receive_media(mp, mf, mf->samplestamp, ts_last);

	xfree(mf->raw);
	xfree(mf);

	return XRTP_OK;
}

int rtp_speex_sync(void *play, uint32 stamp, uint32 hi_ntp, uint32 lo_ntp)
{
    spxrtp_log(("audio/speex.rtp_speex_sync: sync isn't implemented\n"));
	return XRTP_OK;
}

xrtp_media_t* rtp_speex(profile_handler_t *handler, int clockrate, int coding_param)
{
	media_control_t *mc;
	spxrtp_handler_t *profile;

	speex_setting_t *spxset;
	speex_info_t *spxinfo;
	xrtp_media_t* media = NULL;

	spxrtp_log(("audio/speex.rtp_speex: create media handler\n"));

	profile = (spxrtp_handler_t *)handler;

	if(profile->speex_media)
		return (xrtp_media_t*)profile->speex_media;

	if(profile->session)
	{
		mc = session_media_control(profile->session);

		spxset = speex_setting(session_media_control(profile->session));

		profile->rtp_portno = spxset->rtp_setting.rtp_portno;
		profile->rtcp_portno = spxset->rtp_setting.rtcp_portno;

		spxinfo = (speex_info_t*)xmalloc(sizeof(speex_info_t));
		if(!spxinfo)
		{
			spxrtp_log(("audio/speex.rtp_speex: NO memory\n"));
			return NULL;
		}

		memset(spxinfo, 0, sizeof(speex_info_t));
	}

	profile->speex_media = (spxrtp_media_t *)xmalloc(sizeof(struct spxrtp_media_s));
	if(!profile->speex_media)
	{
		xfree(spxinfo);
	}
	else
	{
		memset(profile->speex_media, 0, sizeof(struct spxrtp_media_s));

		profile->speex_media->speex_profile = profile;
      
		media = ((xrtp_media_t*)(profile->speex_media));

		media->clockrate = clockrate;

		media->coding_parameter = coding_param;

		media->sampling_instance = SPX_SAMPLING_INSTANCE;

		media->session = profile->session;

		media->match_mime = rtp_speex_match_mime;
		media->done = rtp_speex_done;
      
		media->set_parameter = rtp_speex_set_parameter;
		media->parameter = rtp_speex_parameter;

		media->info = rtp_speex_info;
		media->sdp = rtp_speex_sdp;
		media->new_sdp = rtp_speex_new_sdp;

		media->set_callback = rtp_media_set_callback;

		media->set_coding = rtp_speex_set_coding;
		media->coding = rtp_speex_coding;
      
		media->sign = rtp_speex_sign;
		media->post = rtp_speex_post;
		media->play = rtp_speex_play;
		media->sync = rtp_speex_sync;

		if(profile->session)
		{
			speex_info_setting(spxinfo, spxset);

			media->bps = spxinfo->audioinfo.info.bps;

			profile->packets = xlist_new();
			if(!profile->packets)
			{
				spxrtp_debug(("audio/speex.rtp_speex: no input buffer created\n"));

				xfree(profile->speex_media);
				profile->speex_media = NULL;
		
				return NULL;
			}
	   
			profile->thread = xthr_new(rtp_speex_send_loop, profile, XTHREAD_NONEFLAGS); 
			if(!profile->thread)
			{
				xlist_done(profile->packets, NULL);
				xfree(profile->speex_media);
				profile->speex_media = NULL;

				return NULL;
			}

			session_issue_mediainfo(profile->session, spxinfo, 1);
		}
	}

	spxrtp_log(("audio/speex.rtp_speex: media handler created\n"));

	return media;
}

int spxrtp_match_id(profile_class_t * clazz, char *id)
{
   spxrtp_log(("audio/speex.spxrtp_match_id: i am '%s' handler\n", speex_mime));

   //return strncmp(speex_mime, id, strlen(speex_mime)) == 0;
   return strcmp(speex_mime, id) == 0;
}

int spxrtp_type(profile_class_t * clazz)
{
   return XRTP_HANDLER_TYPE_MEDIA;
}

char * spxrtp_description(profile_class_t * clazz)
{
   return spxrtp_desc;
}

int spxrtp_capacity(profile_class_t * clazz){

   return XRTP_CAP_NONE;
}

int spxrtp_handler_number(profile_class_t *clazz)
{
   return num_handler;
}

int spxrtp_done(profile_class_t * clazz)
{
   xfree(clazz);
   return XRTP_OK;
}

profile_handler_t * spxrtp_new_handler(profile_class_t * clazz, xrtp_session_t * ses)
{
	spxrtp_handler_t * spxh;
	profile_handler_t * h;

	spxh = (spxrtp_handler_t *)xmalloc(sizeof(spxrtp_handler_t));
	memset(spxh, 0, sizeof(spxrtp_handler_t));

	spxh->session = ses;

	/* thread oriented */
	spxh->packets = xlist_new();
	spxh->packets_lock = xthr_new_lock();
	spxh->pending = xthr_new_cond(XTHREAD_NONEFLAGS);

	spxh->payload_buf = buffer_new(MAX_PAYLOAD_BYTES, NET_ORDER);
	spxrtp_log(("audio/speex.vrtp_new_handler: FIXME - error probe\n"));

	h = (profile_handler_t *)spxh;

	spxrtp_log(("audio/speex.spxrtp_new_handler: New handler created\n"));

	h->module = rtp_speex_module;

	h->master = rtp_speex_master;
	h->set_master = rtp_speex_set_master;

	h->media = rtp_speex;

	h->rtp_in = spxrtp_rtp_in;
	h->rtp_out = spxrtp_rtp_out;
	h->rtp_size = spxrtp_rtp_size;

	h->rtcp_in = spxrtp_rtcp_in;
	h->rtcp_out = spxrtp_rtcp_out;
	h->rtcp_size = spxrtp_rtcp_size;

	++num_handler;

	return h;
}

int spxrtp_done_handler(profile_handler_t * h)
{
	xfree(h);

	num_handler--;

	return XRTP_OK;
}

/**
 * Methods for module initializing
 */
module_interface_t * module_init()
{
	spxrtp = xmalloc(sizeof(profile_class_t));

	spxrtp->match_id = spxrtp_match_id;
	spxrtp->type = spxrtp_type;
	spxrtp->description = spxrtp_description;
	spxrtp->capacity = spxrtp_capacity;
	spxrtp->handler_number = spxrtp_handler_number;
	spxrtp->done = spxrtp_done;

	spxrtp->new_handler = spxrtp_new_handler;
	spxrtp->done_handler = spxrtp_done_handler;

	num_handler = 0;

	spxrtp_log(("audio/speex.module_init: Module['audio/speex'] loaded.\n"));

	return spxrtp;
}

/**
 * Plugin Infomation Block
 */
extern DECLSPEC
module_loadin_t mediaformat = 
{
	"rtp_profile",   /* Plugin ID */

	000001,         /* Plugin Version */
	000001,         /* Minimum version of lib API supportted by the module */
	000001,         /* Maximum version of lib API supportted by the module */

	module_init     /* Module initializer */
};

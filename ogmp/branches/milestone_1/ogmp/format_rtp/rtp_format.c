/***************************************************************************
                          rtp_format.c  -  Format defined by rtp capabilities
                             -------------------
    begin                : Web Jul 22 2004
    copyright            : (C) 2004 by Heming Ling
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

#include "rtp_format.h"

#include <timedia/xmalloc.h>
#include <timedia/xstring.h>

#define CATALOG_VERSION  0x0001
#define DIRNAME_MAXLEN  128

#define RTP_LOG
#define RTP_DEBUG

#ifdef RTP_LOG
 #define rtp_log(fmtargs)  do{printf fmtargs;}while(0)
#else
 #define rtp_log(fmtargs)  
#endif

#ifdef RTP_DEBUG
 #define rtp_debug(fmtargs)  do{printf fmtargs;}while(0)
#else
 #define rtp_debug(fmtargs)  
#endif

int rtp_done_stream(rtp_stream_t *strm)
{
	strm->rtp_cap->descript.done((capable_descript_t*)strm->rtp_cap);

	xstr_done_string(strm->source_cname);
	session_done(strm->session);

	xfree(strm);

	return MP_OK;
}

int rtp_done_format(media_format_t * mf)
{
   media_stream_t *cur = mf->first;
   media_stream_t *next;

   while (cur != NULL)
   {
      next = cur->next;
      
      if(cur->player)
         cur->player->done(cur->player);
         
      rtp_done_stream((rtp_stream_t*)cur);
      xfree(cur);
      
      cur = next;
   }

   xfree(mf);

   return MP_OK;
}

/* New stream add to group */
int rtp_add_stream(media_format_t * rtp, media_stream_t *strm, int strmno, unsigned char type)
{
  media_stream_t *cur = rtp->first;

  if (rtp->first == NULL)
  {
    rtp->first = strm;
    rtp->first->next = NULL;
  }
  else
  {
    cur = rtp->first;
    while (cur->next != NULL)
      cur = cur->next;

    cur->next = strm;
    strm->next = NULL;
  }

  switch(type)
  {
    case(MP_AUDIO): rtp->nastreams++;
      
		/* default first audio stream as time reference */
        if (rtp->nastreams == 1)
		{
			rtp_log(("rtp_set_mime_player: stream #%d as default time stream\n", rtp->numstreams));
            rtp->time_ref = strmno;
        }
                  
        break;
               
    case(MP_VIDEO): rtp->nvstreams++; break;
    
    case(MP_TEXT): rtp->ntstreams++; break;
  }

  rtp->numstreams++;

  return MP_OK;
}

media_stream_t * rtp_find_stream(media_format_t * mf, int strmno)
{
  media_stream_t *cur = mf->first;

  while (cur != NULL)
  {
    if ((((rtp_stream_t*)cur)->sno) == strmno)
		break;

    cur = cur->next;
  }

  return cur;
}

media_player_t * rtp_stream_player(media_format_t * mf, int strmno)
{
  media_stream_t *cur = mf->first;

  while (cur != NULL)
  {
    if ((((rtp_stream_t*)cur)->sno) == strmno)
      return cur->player;
      
    cur = cur->next;
  }

  return NULL;
}

media_stream_t * rtp_find_mime(media_format_t * mf, const char * mime)
{
   media_stream_t *cur = mf->first;

   while (cur != NULL)
   {
      if (!strcmp(cur->mime, mime))
	  {
         return cur;
      }

      cur = cur->next;
   }

   return NULL;
}

media_player_t * rtp_mime_player(media_format_t * mf, const char * mime)
{
   media_stream_t *cur = mf->first;

   while (cur != NULL)
   {

      if (!strcmp(cur->mime, mime))
	  {
         return cur->player;
      }
    
      cur = cur->next;
   }

   return NULL;
}

int rtp_set_mime_player (media_format_t * mf, media_player_t * player, const char * mime)
{
   media_stream_t *cur = mf->first;

   while (cur != NULL)
   {
      if (!strcmp(cur->mime, mime))
	  {
         cur->player = player;

         return MP_OK;
      }

      cur = cur->next;
   }

   rtp_log(("rtp_set_mime_player: stream '%s' not exist\n", mime));
   
   return MP_FAIL;
}

media_stream_t * rtp_find_fourcc(media_format_t * mf, const char *fourcc)
{
   media_stream_t *cur = mf->first;

   while (cur != NULL)
   {
      if ( strncmp(cur->fourcc, fourcc, 4) )
	  {
         return cur;
      }

      cur = cur->next;
   }

   return NULL;
}

media_player_t * rtp_fourcc_player(media_format_t * mf, const char *fourcc)
{
   media_stream_t *cur = mf->first;

   while (cur != NULL)
   {
      if ( strncmp(cur->fourcc, fourcc, 4) )
         return cur->player;

      cur = cur->next;
   }

   return NULL;
}

int rtp_set_fourcc_player (media_format_t * mf, media_player_t * player, const char *fourcc)
{
   media_stream_t *cur = mf->first;

   while (cur != NULL)
   {
      if ( !strncmp(cur->fourcc, fourcc, 4) )
	  {
         cur->player = player;

         return MP_OK;
      }

      cur = cur->next;
   }

   rtp_log(("rtp_set_fourcc_player: stream [%c%c%c%c] not exist\n"
                  , fourcc[0], fourcc[1], fourcc[2], fourcc[3]));

   return MP_FAIL;
}

int rtp_support_type (media_format_t *mf, char *type, char *subtype)
{
	if(!strcmp(type, "mime") && !strcmp(type, "application/sdp"))
		return 1;

	return 0;
}

/**
 * There is no operate on file, return opened player number is Zero
 */
int rtp_open (media_format_t *mf, char * fname, media_control_t *ctrl)
{
	return 0;
}

int rtp_stream_on_member_update(void *gen, uint32 ssrc, char *cn, int cnlen)
{
   rtp_stream_t *rtpstrm = (rtp_stream_t*)gen;

   if(strcmp(rtpstrm->source_cname, cn) != 0)
   {
	   rtp_log(("rtp_stream_on_member_update: Stream Receiver[%s] discovered\n", cn));
	   return MP_OK;
   }

   rtp_log(("rtp_stream_on_member_update: source[%s] connected\n", cn));
   
   return MP_OK;
}

rtp_stream_t* rtp_open_stream(rtp_format_t *rtp_format, int sno, rtpcap_descript_t *rtpcap, media_control_t *ctrl, char *mode, int bw_budget, void* extra)
{
	unsigned char stype;
	rtp_stream_t *strm;

	rtp_setting_t *rset = NULL;

	int rtp_portno, rtcp_portno;

	module_catalog_t *cata = ctrl->modules(ctrl);

	rtpcap_set_t* rtpcapset = (rtpcap_set_t*)extra;
	user_profile_t* user_prof = rtpcapset->user_profile;

	char* remote_nettype;
	char* remote_addrtype;
	char* remote_netaddr;
/*
	if(!cap->match_type(cap, "rtp"))
	{
		rtp_log(("rtp_open_stream: Not RTP capable\n"));
		return NULL;
	}
*/
	strm = xmalloc(sizeof(rtp_stream_t));
	if(!strm)
	{
		rtp_log(("rtp_open_stream: No memory for rtp stream\n"));
		return NULL;
	}
	memset(strm, 0, sizeof(rtp_stream_t));

	if(strncmp(rtpcap->profile_mime, "audio", 5) == 0)
	{
		rtp_log(("rtp_open_stream: audio media\n"));
		stype = MP_AUDIO;
	}
	else if(strncmp(rtpcap->profile_mime, "video", 5) == 0)
	{
		rtp_log(("rtp_open_stream: video media type\n"));
		stype = MP_VIDEO;
	}
	else if(strncmp(rtpcap->profile_mime, "text", 4) == 0)
	{
		rtp_log(("rtp_open_stream: text media type\n"));
		stype = MP_TEXT;
	}
	else
	{
		rtp_log(("rtp_open_stream: Unknown media type\n"));
		xfree(strm);
		return NULL;
	}

	rset = (rtp_setting_t*)ctrl->fetch_setting(ctrl, "rtp", (media_device_t*)rtp_format->rtp_in);
	if(!rset)
	{
		rtp_log(("rtp_open_stream: No rtp setting\n"));
		xfree(strm);
		return NULL;
	}

	strm->session = rtp_format->rtp_in->rtp_session(rtp_format->rtp_in, cata, ctrl,
									user_prof->cname, strlen(user_prof->cname)+1,
									user_prof->user->nettype, user_prof->user->addrtype, 
									user_prof->user->netaddr, 
									rset->default_rtp_portno, rset->default_rtcp_portno,
									rtpcap->profile_no, rtpcap->profile_mime, 
									rtpcap->clockrate, rtpcap->coding_param,
									bw_budget);

	if(!strm->session)
	{
		xfree(strm);
		return NULL;
	}

	/* get real portno and payload type */
	session_portno(strm->session, &rtp_portno, &rtcp_portno);
	rtpcap->profile_no = session_payload_type(strm->session);

	/* the stream rtp capable 
	strm->rtp_cap = rtp_capable_descript(rtpcap->profile_no, 
						user_prof->user->nettype, user_prof->user->addrtype, user_prof->user->netaddr, 
						rtp_portno, rtcp_portno, 
						rtpcap->profile_mime, rtpcap->clockrate, 
						rtpcap->coding_param, NULL);
	*/

	strm->rtp_format = rtp_format;

	rtp_log(("rtp_open_stream: FIXME - stream mime string overflow possible!!\n"));
	strcpy(strm->stream.mime, rtpcap->profile_mime);

	rtp_add_stream((media_format_t*)rtp_format, (media_stream_t*)strm, sno, stype);

	rtp_log(("rtp_open_stream: for [%s:%u|%u]\n", rtpcapset->cname, rtpcap->rtp_portno, rtpcap->rtcp_portno));
	
	if(rtpcap->netaddr)
	{
		remote_nettype = rtpcap->nettype;
		remote_addrtype = rtpcap->addrtype;
		remote_netaddr = rtpcap->netaddr;
	}
	else
	{
		remote_nettype = rtpcapset->nettype;
		remote_addrtype = rtpcapset->addrtype;
		remote_netaddr = rtpcapset->netaddr;
	}
	
	session_add_cname(strm->session, rtpcapset->cname, strlen(rtpcapset->cname), remote_netaddr, rtpcap->rtp_portno, rtpcap->rtcp_portno, rtpcap, ctrl);

	/* waiting to source to be available */
	strm->source_cname = xstr_clone(rtpcapset->cname);
	strm->bandwidth = session_bandwidth(strm->session);

	rtp_format->bandwidth += strm->bandwidth;

	session_set_callback(strm->session, CALLBACK_SESSION_MEMBER_UPDATE, rtp_stream_on_member_update, strm);

	return strm;
}

capable_descript_t* rtp_stream_capable(rtp_stream_t *strm)
{
	return (capable_descript_t*)strm->rtp_cap;
}

/**
 * return used bandwidth
 */
int rtp_open_capables(media_format_t *mf, rtpcap_set_t *rtpcapset, media_control_t *ctrl, char *mode, int bandwidth)
{
   int sno=0;
   rtp_stream_t *strm;

   rtpcap_descript_t *rtpcap;
   xlist_user_t u;

   rtp_format_t *rtp = (rtp_format_t *)mf;

   rtp_log(("rtp_open_capables: open %d capables\n", xlist_size(rtpcapset->rtpcaps)));

   if(strlen(mode) >= MAX_MODEBYTES)
   {
	   rtp_log(("rtp_open_capables: mode unacceptable\n"));
	   return MP_INVALID;
   }

   strcpy(mf->default_mode, mode);
   mf->control = ctrl;

   rtp->rtp_in = (dev_rtp_t*)ctrl->find_device(ctrl, "rtp");

   rtpcap = xlist_first(rtpcapset->rtpcaps, &u);
   while(rtpcap)
   {
	   if(rtpcap->enable)
	   {
			strm = rtp_open_stream(rtp, sno++, rtpcap, ctrl, mode, bandwidth, rtpcapset);
			if(strm)
				bandwidth -= strm->bandwidth;
	   }
	   
	   rtpcap = xlist_next(rtpcapset->rtpcaps, &u);
   }

   return rtp->bandwidth;
}

/* Return the milliseconds of the rtp media, set by sdp */
int demux_rtp_millisec (media_format_t * mf, int rescan)
{
   rtp_format_t *rtp = (rtp_format_t *)mf;

   return rtp->millisec;
}

/* Can not seek, unless talk to server with rtsp */
int rtp_seek_millisecond (media_format_t *mf, int ms)
{
   rtp_log (("rtp_seek_millisecond: unseekable to %dms\n", ms));

   return 0;
}

/************************************************************************
 * send stream granulepos data to rtp channels from preseeked position
 * return value:
 * - return positive value means interval usec to next timestamp;
 *          negetive value indicate error;
 *          zero means end of stream
 */
int rtp_demux_next (media_format_t *mf, int stream_end)
{
   return MP_NOP;
}

int rtp_close (media_format_t * mf)
{
   return MP_OK;
}

int rtp_nstream (media_format_t * mf)
{
   return mf->numstreams;
}

const char* rtp_stream_mime (media_format_t * mf, int strmno)
{
   media_stream_t * ms = rtp_find_stream(mf, strmno);
   if(!ms)  return "";

   return ms->mime;
}

const char* rtp_stream_fourcc (media_format_t * mf, int strmno)
{
   media_stream_t * ms = rtp_find_stream(mf, strmno);
   if(!ms)  return "\0\0\0\0";

   return ms->fourcc;
}

int rtp_players(media_format_t * mf, char *type, media_player_t* players[], int nmax)
{
  media_stream_t *cur = mf->first;
  int n = 0;

  while (cur != NULL)
  {
    if(cur->player->match_play_type(cur->player, type))
	{
		/* playtype match */
		players[n++] = cur->player;
	}

    cur = cur->next;
  }

  return n;
}

int rtp_set_control (media_format_t * mf, media_control_t * control)
{
   mf->control = control;

   return MP_OK;
}

int rtp_set_player (media_format_t * mf, media_player_t * player)
{
   int ret;
   const char * type;

   type = player->media_type(player);
   if ( (ret = rtp_set_mime_player (mf, player, type)) >= MP_OK )
   {
      rtp_log (("rtp_set_player: '%s' stream playable\n", type));
      return ret;
   }
   
   type = player->codec_type(player);
   if ( (ret = rtp_set_mime_player (mf, player, type)) >= MP_OK )
   {
      rtp_log (("rtp_set_player: play '%s' stream\n", type));
      return ret;
   }

   rtp_debug(("rtp_set_player: can't play the media with player\n"));

   return ret;
}

module_interface_t * media_new_format()
{
   media_format_t * mf = NULL;

   rtp_format_t * rtp = xmalloc(sizeof(struct rtp_format_s));
   if(!rtp)
   {
      rtp_log(("rtp_new_rtp_group: No memery to allocate\n"));
      return NULL;
   }

   memset(rtp, 0, sizeof(struct rtp_format_s));

   rtp->open_capables = rtp_open_capables;

   mf = (media_format_t *)rtp;

   /* release a format */
   mf->done = rtp_done_format;

   mf->support_type = rtp_support_type;

   /* Open/Close a media source */
   mf->open = rtp_open;
   mf->close = rtp_close;

   /* Stream management */
   mf->add_stream = rtp_add_stream;

   mf->find_stream = rtp_find_stream;
   mf->find_mime = rtp_find_mime;
   mf->find_fourcc = rtp_find_fourcc;

   mf->set_control = rtp_set_control;
   mf->set_player = rtp_set_player;

   /* Stream info */
   mf->nstream = rtp_nstream;
   mf->stream_mime = rtp_stream_mime;
   mf->stream_fourcc = rtp_stream_fourcc;

   mf->stream_player = rtp_stream_player;
   mf->mime_player = rtp_mime_player;
   mf->fourcc_player = rtp_fourcc_player;

   mf->players = rtp_players;

   /* Seek the media by time */
   mf->seek_millisecond = rtp_seek_millisecond;

   /* demux next sync stream group */
   mf->demux_next = rtp_demux_next;

   return mf;
}

/**
 * Loadin Infomation Block
 */
extern DECLSPEC module_loadin_t mediaformat =
{
   "format",   /* Label */

   000001,         /* Plugin version */
   000001,         /* Minimum version of lib API supportted by the module */
   000001,         /* Maximum version of lib API supportted by the module */

   media_new_format   /* Module initializer */
};

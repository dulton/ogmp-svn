/***************************************************************************
                          vorbis_rtp.c  -  Vorbis rtp stream
                             -------------------
    begin                : Mon Jan 19 2004
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
 
#include <timedia/xstring.h>

#include "dev_rtp.h"

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

int rtp_done_setting(control_setting_t *gen){

   free(gen);

   return MP_OK;
}

control_setting_t* rtp_new_setting(media_device_t *dev){

   rtp_setting_t * set = malloc(sizeof(struct rtp_setting_s));
   if(!set){

      rtp_debug(("rtp_new_setting: No memory"));

      return NULL;
   }

   memset(set, 0, sizeof(struct rtp_setting_s));

   set->setting.done = rtp_done_setting;

   return (control_setting_t*)set;
}

/*
 * this pipe ONLY make and destroy frame, not for delivery
 */
media_frame_t* rtp_new_frame (media_pipe_t *pipe, int bytes, char *init_data)
{
   /* better recycle to garrantee */
   rtp_frame_t * rtpf = (rtp_frame_t*)malloc(sizeof(struct rtp_frame_s));
   if(!rtpf)
   {
      rtp_debug(("rtp_new_frame: No memory for frame\n"));
      return NULL;
   }   
   memset(rtpf, 0, sizeof(*rtpf));

   rtpf->frame.owner = pipe;

   rtpf->media_unit = (char*)malloc(bytes);
   if(rtpf->media_unit == NULL)
   {
	   free(rtpf);
	   rtp_debug(("rtp_new_frame: No memory for data"));
	   
	   return NULL;
   }

   rtpf->unit_bytes = bytes;

   /* initialize data */
   if(init_data != NULL)
   {
		//rtp_log(("rtp_new_frame: initialize %d bytes data\n", bytes));
		memcpy(rtpf->media_unit, init_data, bytes);
   }

   return (media_frame_t*)rtpf;
}

int rtp_recycle_frame (media_pipe_t *pipe, media_frame_t *f)
{
   rtp_frame_t * rtpf = (rtp_frame_t*)f;

   if(rtpf->media_unit) 
	   free(rtpf->media_unit);

   free(rtpf);

   return MP_OK;
}

int rtp_put_frame (media_pipe_t *pipe, media_frame_t *frame, int last)
{
   rtp_log(("rtp_put_frame: NOP, put frame to rtp_media instead\n"));

   return MP_EIMPL;
}

media_frame_t* rtp_pick_frame (media_pipe_t *pipe)
{
   rtp_log(("rtp_pick_frame: NOP, pick frame in media_post() instead\n"));

   return NULL;
}

int rtp_pick_content (media_pipe_t *pipe, media_info_t *media_info, char* raw, int nraw_once)
{
   rtp_log(("rtp_pick_content: Can only pick whole frame\n"));

   return MP_EUNSUP;
}

int rtp_pipe_done (media_pipe_t *pipe)
{
   free(pipe);
 
   return MP_OK;
}

media_pipe_t * rtp_pipe_new()
{
   media_pipe_t *mp = (media_pipe_t*)malloc(sizeof(struct media_pipe_s));
   if(!mp)
   {
      rtp_debug(("rtp_pipe_new: No memory to allocate\n"));
      return NULL;
   }
   memset(mp, 0, sizeof(struct media_pipe_s));

   mp->new_frame = rtp_new_frame;
   mp->recycle_frame = rtp_recycle_frame;
   mp->put_frame = rtp_put_frame;
   mp->pick_frame = rtp_pick_frame;
   mp->pick_content = rtp_pick_content;
   mp->done = rtp_pipe_done;

   return mp;
}

media_pipe_t * rtp_pipe (media_device_t *dev)
{
	dev_rtp_t* dr = (dev_rtp_t*)dev;

	if(dr->frame_maker) return dr->frame_maker;

	dr->frame_maker = rtp_pipe_new();

	return dr->frame_maker;
}

int rtp_set_callbacks (xrtp_session_t *ses, rtp_callback_t *cbs, int ncbs)
{
   int i;
   for (i=0; i<ncbs; i++)
   {
      session_set_callback (ses, cbs[i].type, cbs[i].callback, cbs[i].callback_user);
   }

   return MP_OK;
}

/**
 * A new rtp session will be created here.
 , rtp_setting_t *rtpset
 */
xrtp_session_t* rtp_session(dev_rtp_t *rtp,
							module_catalog_t *cata, media_control_t *ctrl,

							char *cname, int cnlen,
							char *ip, uint16 rtp_portno, uint16 rtcp_portno,
							
							uint8 profile_no, char *profile_mime,
							int total_bw, int rtp_bw)
{
   xrtp_session_t *ses = NULL;
   xrtp_media_t *rtp_media;

   rtp_debug(("rtp_session: [%s@%s:%u|%u], mime[%s]\n", cname, ip, rtp_portno, rtcp_portno, profile_mime));

   /* If the session is exist */
   ses = xrtp_find_session(cname, cnlen, ip, rtp_portno, rtcp_portno, profile_no, profile_mime);
   if(!ses)
   {
		ses = session_new(cname, cnlen, ip, rtp_portno, rtcp_portno, cata, ctrl);
		if(!ses)
			return NULL;

		session_set_bandwidth(ses, total_bw, rtp_bw);

		/* set xrtp handler */
		if (session_add_handler(ses, profile_mime) == NULL)
		{
			rtp_debug(("vrtp_new: Fail to set vorbis profile !\n"));
			session_done(ses);

			return NULL;
		}

		session_new_media(ses, profile_mime, profile_no);
		rtp_media = session_media(ses);

		session_set_scheduler(ses, xrtp_scheduler());
   }

   return ses;
}

int rtp_setting (media_device_t *dev, control_setting_t *setting, module_catalog_t *cata)
{
	rtp_log(("rtp_setting: do nothing here\n"));
	rtp_log(("rtp_setting: use rtp_device->rtp_session() to get rtp session\n"));

	return MP_OK;
}

/**
 * libxrtp initilized
 */
int rtp_start (media_device_t * dev)
{
   /* Realised the xrtp lib */
   int ret = xrtp_init();  
   if(ret < XRTP_OK) return ret;
   
   rtp_debug(("rtp.rtp_start: started...\n"));
   return MP_OK;
}

/**
 * libxrtp finialized
 */
int rtp_stop (media_device_t * dev)
{
	xrtp_done();
	
	return MP_OK;
}

int rtp_set_media_info (media_device_t *dev, media_info_t *info)
{
   dev_rtp_t *rtp = (dev_rtp_t*)dev;
   
   return MP_OK;
}

int rtp_match_type(media_device_t *dev, char *type)
{
   rtp_log(("rtp.rtp_match_type: I am rtp device\n"));

   if( !strcmp("rtp", type) ) return 1;

   return 0;
}

int rtp_done (media_device_t *dev)
{
   dev_rtp_t *rtp = (dev_rtp_t *)dev;

   if(!dev)
   {
      rtp_debug(("rtp.rtp_done: NULL dev\n"));
      return MP_OK;
   }

   rtp_stop(dev);

   if(rtp->frame_maker)
	   rtp->frame_maker->done(rtp->frame_maker);

   free(rtp);

   return MP_OK;
}

module_interface_t * rtp_new ()
{
   media_device_t * dev;

   dev_rtp_t *dr = (dev_rtp_t*)malloc(sizeof(struct dev_rtp_s));
   if (!dr)
   {
      rtp_debug(("vrtp_new: No free memory\n"));
      return NULL;
   }

   memset (dr, 0, sizeof(struct dev_rtp_s));

   dr->rtp_session = rtp_session;

   dev = (media_device_t*)dr;

   dev->done = rtp_done;

   dev->pipe = rtp_pipe;

   dev->start = rtp_start;
   dev->stop = rtp_stop;

   dev->set_media_info = rtp_set_media_info;

   dev->new_setting = rtp_new_setting;
   dev->setting = rtp_setting;

   dev->match_type = rtp_match_type;

   return dev;
}

/**
 * Loadin Infomation Block
 */
extern DECLSPEC
module_loadin_t mediaformat =
{
   "device",   /* Label */

   000001,         /* Plugin version */
   000001,         /* Minimum version of lib API supportted by the module */
   000001,         /* Maximum version of lib API supportted by the module */

   rtp_new   /* Module initializer */
};


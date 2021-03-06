/***************************************************************************
                          speex_codec.h
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

#include "speex_info.h"

#include <timedia/xthread.h>
#include <ogg/ogg.h>

#define SPEEX_SAMPLE_BITS 16  /* See speex_decode() */

typedef struct speex_decoder_s speex_decoder_t;
struct speex_decoder_s
{
   struct media_player_s player;

   speex_info_t *speex_info;

   int receiving_media;

   int stream_opened;

   int ts_usec_now;
   int64 last_samplestamp;

   /* Failure occurred */
   int fail;

   void *callback_on_ready_user;
   int (*callback_on_ready)();

   void *callback_on_media_stop_user;
   int (*callback_on_media_stop)();

   /* parallel with demux thread */
   xthread_t *thread;

   /* stop the thread */
   int stop;

   /* queue protected by lock*/
   xlist_t *pending_queue;

   /* mutex pretected */
   xthr_lock_t *pending_lock;

   /* thread running condiftion */
   xthr_cond_t *packet_pending;
};

typedef struct speex_encoder_s speex_encoder_t;
struct speex_encoder_s
{
   struct media_maker_s maker;

   speex_info_t *speex_info;

   char* encoding_frame;			/* 20ms frame size */
   char* encoding_gap;

   int encoding_frame_bytes;
   int encoding_nsample_expect;
   int encoding_nsample_buffered;

   int lookahead;
   int nframe_group;	/* current how many speex frames in a group */

   int nsample_per_group;
   int64 group_samplestamp;

   media_device_t* input_device;
   media_stream_t* media_stream;

   /* Failure occurred */
   int fail;

   /* parallel with demux thread */
   xthread_t *thread;

   /* stop the thread */
   int stop;

   /* queue protected by lock*/
   xlist_t *pending_queue;

   /* mutex pretected */
   xthr_lock_t *pending_lock;

   /* thread running condiftion */
   xthr_cond_t *packet_pending;
};

media_frame_t* spxc_decode(speex_info_t* spxinfo, media_frame_t *spxf, media_pipe_t* output);
int spxc_encode(speex_encoder_t* enc, speex_info_t* spxinfo, char* pcm, int bytes, char** spx);

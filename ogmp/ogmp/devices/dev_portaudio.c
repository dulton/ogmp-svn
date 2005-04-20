/***************************************************************************
                          dev_portaudio.c  -  PortAudio Device
                             -------------------
    begin                : Tue Feb 3 2004
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
 
#include "dev_portaudio.h"
#include "timed_pipe.h"

#include <portaudio.h>

#include <timedia/os.h>
#include <timedia/ui.h>
#include <timedia/xthread.h>
#include <timedia/xmalloc.h>
#include <string.h>

#define PA_SAMPLE_TYPE  paInt16

#define DEFAULT_SAMPLE_FACTOR  2

#define DEFAULT_OUTPUT_NSAMPLE_PULSE  2048
#define DEFAULT_INPUT_NSAMPLE_PULSE  2048
#define PA_DEFAULT_INBUF_N 2

#define DELAY_WHILE 1000

/*
#define PORTAUDIO_LOG
*/
#define PORTAUDIO_DEBUG

#ifdef PORTAUDIO_LOG
 #define pa_log(fmtargs)  do{ui_print_log fmtargs;}while(0)
#else
 #define pa_log(fmtargs)
#endif

#ifdef PORTAUDIO_DEBUG
 #define pa_debug(fmtargs)  do{printf fmtargs;}while(0)
#else
 #define pa_debug(fmtargs)
#endif

typedef struct pa_input_s pa_input_t;
struct pa_input_s
{
	PaTimestamp stamp;
	int npcm_write;
   int npcm_read;
   int nbyte_read;
	char *pcm;
};

typedef struct portaudio_device_s 
{
   struct media_device_s dev;

   /* Input */
   audio_info_t ai_input;
   media_receiver_t* receiver;

   int inbuf_n;
   int inbuf_r;
   int inbuf_w;
   
   pa_input_t *inbuf;

   int input_stop;
   xthread_t *input_thread;
   PortAudioStream *pa_instream;

   int input_usec_pulse;

   /* Output */
   media_pipe_t * out;
   audio_info_t ai_output;

   PortAudioStream *pa_outstream;
   int time_adjust;     /* adjust PortAudio time */
   int nbuf_internal;   /* PortAudio Internal Buffer number, for performance tuning */

   int sample_factor;
   int usec_pulse;


   int online;

   xrtp_clock_t *clock;

   int time_match;      /* try delay to match pulse time */
   int last_pick;

   rtime_t while_ns;
   
   media_control_t *control;
   
   int input_npcm_once;
   int input_nbyte_once;
   
} portaudio_device_t;

static int pa_input_callback( void *inbuf, void *outbuf, unsigned long npcm_in,
								PaTimestamp intime, void *udata )
{
   int next;
   portaudio_device_t* pa = (portaudio_device_t*)udata;

   next = (pa->inbuf_w+1) % pa->inbuf_n;
   if(next == pa->inbuf_r)
   {
	   /* encoding slower than input */
	   return 0;
   }
   
   memcpy(pa->inbuf[pa->inbuf_w].pcm, inbuf, npcm_in);
   
   pa->inbuf[pa->inbuf_w].stamp = intime;
   pa->inbuf[pa->inbuf_w].npcm_write = npcm_in;

   pa->inbuf_w = next;

   return 0;
}

int pa_input_loop(void *gen)
{
	int err;
	int sample_bytes;
   pa_input_t* inputbuf;
   
	media_frame_t auf;
	portaudio_device_t* pa = (portaudio_device_t*)gen;

   int npcm_need, npcm_left, nbyte_need;
   char *auf_raw;

	if (!pa->receiver) return MP_FAIL;
   
	err = Pa_StartStream ( pa->pa_instream );

	if( err != paNoError )
	{
		Pa_Terminate();
		pa_debug(("pa_input_loop: An error occured when start portaudio stream\n"));

		pa->input_stop = 1;
		return MP_FAIL;
	}

	sample_bytes = pa->ai_input.info.sample_bits / OS_BYTE_BITS;
	pa_debug(("pa_input_loop: PortAudio started, %d bytes/sample\n\n", sample_bytes));
   
   auf.raw = (char*)xmalloc(pa->input_nbyte_once);
   
	pa->input_stop = 0;
   
   npcm_need = pa->input_npcm_once;
   nbyte_need = pa->input_nbyte_once;
   auf_raw = auf.raw;
   
	while(!pa->input_stop)
	{
		rtime_t us_left;
		
      inputbuf = &pa->inbuf[pa->inbuf_r];
      npcm_left = inputbuf->npcm_write;
      
		if(inputbuf->npcm_write == 0)
		{
			/* encoding catch up input */
			time_usec_sleep(pa->clock, pa->input_usec_pulse, &us_left);
			continue;
		}
      
      inputbuf->npcm_read = 0;
      
      while(inputbuf->npcm_read < inputbuf->npcm_write)
      {
		   auf.nraw = pa->input_npcm_once;
         auf.samplestamp = inputbuf->stamp;
         
         if(npcm_left < pa->input_npcm_once)
            auf.eots = 0;
         else
            auf.eots = 1;

         memcpy(auf_raw, &inputbuf->pcm[inputbuf->nbyte_read], nbyte_need);
		   auf.bytes = pa->input_nbyte_once;
         
		   //pa->receiver->receive_media(pa->receiver, &auf, (int64)(pa->inbuf[pa->inbuf_r].stamp * 1000000), 0);

         memset(&inputbuf->pcm[inputbuf->nbyte_read], 0, nbyte_need);
         inputbuf->npcm_read += npcm_need;
         
		   pa_debug(("pa_input_loop: Input %d/%d bytes\n", inputbuf->npcm_read, inputbuf->npcm_write));

         npcm_left = inputbuf->npcm_write - inputbuf->npcm_read;
         if(npcm_left < pa->input_npcm_once)
         {
            int nbyte_left = npcm_left * pa->ai_input.channels_bytes;
            
            memcpy(auf_raw, &inputbuf->pcm[inputbuf->nbyte_read], nbyte_left);
            auf_raw += nbyte_left;
            
            npcm_need = pa->input_npcm_once - npcm_left;
            nbyte_need = pa->input_nbyte_once - nbyte_left;

            inputbuf->npcm_read = inputbuf->npcm_write;
            npcm_left = 0;
         }
         else
         {
            auf_raw = auf.raw;
            npcm_need = pa->input_npcm_once;
            nbyte_need = pa->input_nbyte_once;
         }
      }

      inputbuf->npcm_write = 0;
      pa->inbuf_r = (pa->inbuf_r + 1) % pa->inbuf_n;
	}

	auf.bytes = 0;
	auf.nraw = 0;
	auf.raw = pa->inbuf[pa->inbuf_r].pcm;
   
	pa->receiver->receive_media(pa->receiver, &auf, (int64)(pa->inbuf[pa->inbuf_r].stamp * 1000000), MP_EOS);

	err = Pa_StopStream ( pa->pa_outstream );
	if( err != paNoError )
	{
		Pa_Terminate();
		pa_debug(("pa_input_loop: An error occured while stop the portaudio\n"));

		return MP_FAIL;
	}

	return MP_OK;
}

static int pa_output_callback( void *inbuf, void *outbuf, unsigned long npcm_once,
								PaTimestamp outtime, void *udata )
{
   portaudio_device_t* pa = (portaudio_device_t*)udata;

   int pulse_us = 0;
   int itval = 0;

   int pick_us_start;
   int ret;
   int pick_us;

   pa_log(("pa_output_callback: 1\n"));

   pick_us_start = time_usec_now(pa->clock);
   ret = pa->out->pick_content(pa->out, (media_info_t*)&pa->ai_output, outbuf, npcm_once);
   pick_us = time_usec_spent(pa->clock, pick_us_start);

   if (pa->dev.running == 0) pa->dev.running = 1;
   else 
   {
      /* time adjustment */
      pulse_us = time_usec_spent(pa->clock, pa->last_pick);
   
      if (!pa->time_match) 
	  {
         pa_log(("pa_callback: %dus picking, %dus interval %dus pulse, no more delay\n"
            , pick_us, pulse_us, pa->usec_pulse));
      } 
	  else 
	  {
         int delay_ns = (pa->usec_pulse - pulse_us) * 1000;

         int delay = delay_ns  / pa->while_ns;

         if ( delay > 0 ) {

            pa_log(("pa_callback: %dus picking, %dus interval, need %dns delay (%d loop), %dns delay unit\n"
                  , pick_us, pulse_us, delay_ns, delay, pa->while_ns));

            while (delay) delay--;
         }

         itval = time_usec_spent(pa->clock, pa->last_pick);
         pa_log(("pa_callback: %dus match %dus audio\n", itval, pa->usec_pulse));
      }

   }
   
   pa->last_pick = pick_us_start;

   return ret;
}

int pa_done_setting(control_setting_t *gen)
{
   xfree(gen);

   return MP_OK;
}

control_setting_t* pa_new_setting(media_device_t *dev)
{
   pa_setting_t * set = xmalloc(sizeof(struct pa_setting_s));
   if(!set){

      pa_debug(("rtp_new_setting: No memory"));

      return NULL;
   }
   memset(set, 0, sizeof(struct pa_setting_s));

   set->setting.done = pa_done_setting;

   return (control_setting_t*)set;
}

media_pipe_t * pa_pipe(media_device_t *dev) 
{
   return ((portaudio_device_t *)dev)->out;
}

int pa_online (media_device_t * dev)
{
   PaError err;

   portaudio_device_t *pa_dev = (portaudio_device_t *)dev;

   if (pa_dev->online) return MP_OK;

   err = Pa_Initialize();
   if ( err != paNoError )
   {
      pa_debug(("pa_online: %s\n", Pa_GetErrorText(err) ));
      Pa_Terminate();

      return MP_FAIL;
   }

   pa_dev->online = 1;

   pa_log(("pa_online: portaudio initialized\n"));
   
   return MP_OK;
}

int pa_sample_type (int sample_bits)
{
   switch (sample_bits)
   {
      case 16:
      
         pa_log(("pa_sample_type: 16 bits integer value\n"));
         return paInt16;

      case 32:

         pa_log(("pa_sample_type: 32 bits float value\n"));
         return paFloat32;
   }

   pa_log(("pa_sample_type: apply default 16 bits integer value\n"));
   
   return paInt16;
}

#if 0
media_stream_t* pa_new_media_stream(media_device_t *dev, media_receiver_t* recvr, media_info_t *info)
{
   int nsp = 0;
   int nsample_pulse = 0;
   int sample_type = 0;

   PaError err = 0;

   device_stream_t* strm;

   portaudio_device_t *pa = (portaudio_device_t *)dev;
   
   audio_info_t* ai = (audio_info_t*)info;

   strm = xmalloc(sizeof(device_stream_t));
   if(!strm) return NULL;
   memset(strm, 0, sizeof(device_stream_t));

   if(!pa->online && pa_online (dev) < MP_OK)
   {
      pa_debug (("pa_set_media_info: Can't activate PortAudio device\n"));

      return MP_FAIL;
   }
   
   pa->ai_input = *ai;
   
   /*
   nsp = pa->ai_input.info.sample_rate / pa->sample_factor;   
   nsample_pulse = 1;
   while (nsample_pulse * 2 <= nsp) 
	   nsample_pulse *= 2;
   */   
   nsample_pulse = DEFAULT_NSAMPLE_PULSE;
   
   if(pa->ai_input.info.sample_rate > nsample_pulse)
      pa->input_usec_pulse = (int)(1000000 / (pa->ai_input.info.sample_rate / (double)nsample_pulse));
   else
      pa->input_usec_pulse = (int)((double)nsample_pulse / pa->ai_input.info.sample_rate * 1000000);
      
   pa_log(("pa_set_media_info: %d channels, %d rate, %d sample per pulse (%dus)\n", 
			pa->ai_input.channels, pa->ai_input.info.sample_rate, nsample_pulse, pa->usec_pulse));
   
   pa->ai_input.channels_bytes = pa->ai_input.channels * pa->ai_input.info.sample_bits / OS_BYTE_BITS;

   sample_type = pa_sample_type(pa->ai_input.info.sample_bits);
   
   err = Pa_OpenStream 
					(	&pa->pa_instream,
						Pa_GetDefaultInputDeviceID(),
						pa->ai_input.channels,
						sample_type,
						NULL,				/* void *inputDriverInfo */
						paNoDevice,
						0,					/* int numOutputChannels */
						sample_type,		/* PaSampleFormat outputSampleFormat */
						NULL,
						pa->ai_input.info.sample_rate,

						nsample_pulse,		/* unsigned long framesPerBuffer */
						pa->nbuf_internal,	/* unsigned long numberOfBuffers */
						0,					/* PaStreamFlags streamFlags: paDitherOff */
						pa_input_callback,
						pa 
					);

   if ( err != paNoError )
   {
      pa_debug(("pa_set_media_info: %s\n", Pa_GetErrorText(err) ));
      Pa_Terminate();

	  xfree(strm);

      return NULL;
   }

   strm->device = dev;
   strm->media_info = info;
   strm->creater = receiver;

   return strm;
}
#endif

int pa_make_inbuf(portaudio_device_t *pa, int nsample_pulse)
{
   int i;
   pa->inbuf = xmalloc(sizeof(pa_input_t) * pa->inbuf_n);
   memset(pa->inbuf, 0, sizeof(pa_input_t) * pa->inbuf_n);

   for(i=0; i<pa->inbuf_n; i++)
   {
      pa->inbuf[i].pcm = xmalloc(pa->ai_input.channels_bytes * nsample_pulse);
      memset(pa->inbuf[i].pcm, 0, pa->input_nbyte_once);
   }

   return MP_OK;   
}

int pa_done_inbuf(portaudio_device_t *pa)
{
   int i;
   
   for(i=0; i<pa->inbuf_n; i++)
      xfree(pa->inbuf[i].pcm);

   xfree(pa->inbuf);

   return MP_OK;
}

int pa_set_input_media(media_device_t *dev, media_receiver_t* recvr, media_info_t *in_info)
{
   portaudio_device_t *pa = (portaudio_device_t *)dev;
   
   int nsp = 0;
   int nsample_pulse = 0;
   int sample_type = 0;

   PaError err = 0;

   audio_info_t* ai = (audio_info_t*)in_info;

   if(!pa->online && pa_online (dev) < MP_OK)
   {
      pa_debug (("pa_set_media_info: Can't activate PortAudio device\n"));
      return MP_FAIL;
   }
   
   pa->ai_input = *ai;

   nsp = pa->ai_input.info.sample_rate / pa->sample_factor;
   /*
   nsp = pa->ai_input.info.sample_rate / pa->sample_factor;
   nsample_pulse = 1;
   while (nsample_pulse * 2 <= nsp)
	   nsample_pulse *= 2;
   */
   nsample_pulse = DEFAULT_OUTPUT_NSAMPLE_PULSE;

   if(pa->ai_input.info.sample_rate > nsample_pulse)
      pa->input_usec_pulse = (int)(1000000 / (pa->ai_input.info.sample_rate / (double)nsample_pulse));
   else
      pa->input_usec_pulse = (int)((double)nsample_pulse / pa->ai_input.info.sample_rate * 1000000);
      
   pa_log(("pa_set_input_media: %d channels, %d rate, %d sample per pulse (%dus)\n", 
			pa->ai_input.channels, pa->ai_input.info.sample_rate, nsample_pulse, pa->usec_pulse));
   
   pa->ai_input.channels_bytes = pa->ai_input.channels * pa->ai_input.info.sample_bits / OS_BYTE_BITS;

   pa->input_npcm_once = ai->info.sampling_constant;
   pa->input_nbyte_once = pa->ai_input.channels_bytes * ai->info.sampling_constant;
   
   sample_type = pa_sample_type(pa->ai_input.info.sample_bits);
   
   err = Pa_OpenStream 
					(	&pa->pa_instream,
						Pa_GetDefaultInputDeviceID(),
						pa->ai_input.channels,
						sample_type,
						NULL,				/* void *inputDriverInfo */
						paNoDevice,
						0,					/* int numOutputChannels */
						sample_type,		/* PaSampleFormat outputSampleFormat */
						NULL,
						pa->ai_input.info.sample_rate,
						nsample_pulse,		/* unsigned long framesPerBuffer */
						pa->nbuf_internal,	/* unsigned long numberOfBuffers */
						0,					/* PaStreamFlags streamFlags: paDitherOff */
						pa_input_callback,
						pa 
					);

   if ( err != paNoError )
   {
      pa_debug(("pa_set_input_media: %s\n", Pa_GetErrorText(err) ));
      Pa_Terminate();

      return MP_FAIL;
   }

   pa->receiver = recvr;
   
   if(pa->inbuf_n == 0)
      pa->inbuf_n = PA_DEFAULT_INBUF_N;

   pa_make_inbuf(pa, nsample_pulse);
      
   pa_log(("\n[pa_set_input_media: PortAudio use %d internal buffers]\n\n", Pa_GetMinNumBuffers (nsample_pulse, pa->ai_input.info.sample_rate)));

   /* Start PortAudio */
   return MP_OK;
}

int pa_set_output_media(media_device_t *dev, media_info_t *out_info)
{
   portaudio_device_t *pa = (portaudio_device_t *)dev;
   
   /*int nsp = 0;*/
   int nsample_pulse = 0;
   int sample_type = 0;

   PaError err = 0;
   int pa_min_nbuf = 0;

   audio_info_t* ai = (audio_info_t*)out_info;

   if(!pa->online && pa_online (dev) < MP_OK)
   {
      pa_debug (("pa_set_media_info: Can't activate PortAudio device\n"));

      return MP_FAIL;
   }
   
   pa->ai_output = *ai;
   
   /*
   nsp = pa->ai_input.info.sample_rate / pa->sample_factor;
   nsample_pulse = 1;
   while (nsample_pulse * 2 <= nsp)
	   nsample_pulse *= 2;
   */
   
   /* *
    * It looks to set 2048 per pulse will get a more smooth audio out result
    * for both 16000 and 8000 rate.
    */
   nsample_pulse = DEFAULT_OUTPUT_NSAMPLE_PULSE;

   if(pa->ai_output.info.sample_rate > nsample_pulse)
      pa->usec_pulse = (int)(1000000 / (pa->ai_output.info.sample_rate / (double)nsample_pulse));
   else
      pa->usec_pulse = (int)((double)nsample_pulse / pa->ai_output.info.sample_rate * 1000000);
      
   pa_debug(("pa_set_output_media: %d channels, %d rate, %d sample per pulse (%dus)\n", 
			pa->ai_output.channels, pa->ai_output.info.sample_rate, nsample_pulse, pa->usec_pulse));
   
   pa->ai_output.channels_bytes = pa->ai_output.channels * pa->ai_output.info.sample_bits / OS_BYTE_BITS;

   pa->out = timed_pipe_new(pa->ai_output.info.sample_rate, pa->usec_pulse);
         
   sample_type = pa_sample_type(pa->ai_output.info.sample_bits);
   
   err = Pa_OpenDefaultStream
                  (  &pa->pa_outstream
                     , 0                        /* no input channels */
                     , pa->ai_output.channels   /* output channels */
                     , sample_type              /* paInt16: 16 bit integer output, etc */
                     , pa->ai_output.info.sample_rate  /* sample rate, cd is 44100hz */
                     , nsample_pulse
                     , pa->nbuf_internal        /* use default minimum audio buffers in portaudio */
                     , pa_output_callback
                     , pa
                  );

   if ( err != paNoError )
   {
      pa_debug(("pa_set_output_media: %s\n", Pa_GetErrorText(err) ));
      Pa_Terminate();

      return MP_FAIL;
   }

	pa_min_nbuf = Pa_GetMinNumBuffers (nsample_pulse, pa->ai_output.info.sample_rate);

	pa_log(("\n[pa_set_output_media: stream opened, PortAudio use %d internal buffers]\n\n", pa_min_nbuf));

	err = Pa_StartStream ( pa->pa_outstream );

	if( err != paNoError )
	{
		Pa_Terminate();
		pa_debug(("pa_set_output_media: An error occured while using the portaudio stream\n"));

		pa->online = 0;
		return MP_FAIL;
	}

	return MP_OK;
}

int pa_init(media_device_t * dev, media_control_t *ctrl)
{
	portaudio_device_t *pa = (portaudio_device_t *)dev;

   pa->control = ctrl;
   
   return MP_OK;
}

int pa_start(media_device_t * dev, int mode)
{
	int err;

	portaudio_device_t *pa = (portaudio_device_t *)dev;
   
	pa_log(("pa_start: pa_set_media_info() to start portaudio\n"));

	/* Start PortAudio */
	if((mode & DEVICE_INPUT) && pa->pa_instream)
   {
		pa->input_thread = xthr_new(pa_input_loop, pa, XTHREAD_NONEFLAGS);

	   pa_debug(("pa_set_media_info: PortAudio input started\n"));
   }

	if((mode & DEVICE_OUTPUT) && pa->pa_outstream)
	{
		err = Pa_StartStream ( pa->pa_outstream );

		if( err != paNoError )
		{
			Pa_Terminate();
			pa_debug(("pa_start: An error occured while using the portaudio stream\n"));

			pa->online = 0;
			return MP_FAIL;
		}
      
	   pa_debug(("pa_set_media_info: PortAudio output started\n"));
	}

	return MP_OK;
}

int pa_stop(media_device_t * dev, int mode)
{
   int inloop_ret;
   
   PaError err = 0;
	portaudio_device_t *pa_dev = (portaudio_device_t *)dev;

	/* Start PortAudio */
	if((mode & DEVICE_INPUT) && pa_dev->pa_instream)
   {
	   pa_dev->input_stop = 1;
      xthr_wait(pa_dev->input_thread, &inloop_ret);
   }

	if (!dev->running)
	{
		pa_debug(("pa_stop: PortAudio is idled\n"));

		return MP_OK;
	}
   
	if(pa_dev->pa_outstream)
	{
		err = Pa_StopStream ( pa_dev->pa_outstream );
		if( err != paNoError )
		{
			Pa_Terminate();
			pa_debug(("pa_stop: An error occured while stop the portaudio\n"));

			pa_dev->online = 0;
			return MP_FAIL;
		}

		dev->running = 0;
	}
   
	if(pa_dev->pa_instream)
	{
		int ret;

		xthr_wait(pa_dev->input_thread, &ret);
	}

	pa_dev->out->done (pa_dev->out);
	pa_dev->out = NULL;

	pa_debug(("pa_stop: PortAudio stopped\n"));

	return MP_OK;
}

int pa_offline (media_device_t * dev) {

   portaudio_device_t *pa_dev = NULL;

   if (dev->running)
      pa_stop (dev, DEVICE_INPUT|DEVICE_OUTPUT);

   Pa_Terminate();
   pa_debug(("pa_stop: portaudio terminated.\n"));

   pa_dev = (portaudio_device_t *)dev;

   pa_dev->online = 0;

   return MP_OK;
}

int pa_done (media_device_t * dev) {

   portaudio_device_t *pa_dev = (portaudio_device_t *)dev;
   
   if (pa_dev->online) pa_offline (dev);

   if (pa_dev->out) pa_dev->out->done (pa_dev->out);

   if(pa_dev->clock) time_end(pa_dev->clock);

   pa_done_inbuf(pa_dev);
   
   xfree(pa_dev);
   
   return MP_OK;
}

int pa_setting (media_device_t * dev, control_setting_t *setting, module_catalog_t *cata) {

   /* module_catalog is not used, as no more module needed */
   
   portaudio_device_t * pa = (portaudio_device_t *)dev;
   
   pa->sample_factor = 0;
   
   if (setting)
   {
      pa_setting_t *pa_setting = (pa_setting_t *)setting;      
      pa->time_match = pa_setting->time_match;      
      pa->nbuf_internal = pa_setting->nbuf_internal;

      pa->sample_factor = pa_setting->sample_factor;
      pa->inbuf_n = pa_setting->inbuf_n;
   }

   if(pa->sample_factor == 0)
      pa->sample_factor = DEFAULT_SAMPLE_FACTOR;
   
   return MP_OK;
}

int pa_match_mode(media_device_t *dev, char *mode)
{
   pa_log(("portaudio.pa_match_type: input/output device\n"));

   if( !strcmp("output", mode) ) return 1;
   if( !strcmp("input", mode) ) return 1;
   if( !strcmp("inout", mode) ) return 1;

   return 0;
}

int pa_match_type(media_device_t *dev, char *type)
{
   pa_log(("portaudio.pa_match_type: I am audio device\n"));

   if( !strcmp("audio", type) ) return 1;

   return 0;
}

module_interface_t* media_new_device ()
{
   media_device_t *dev = NULL;
   int delay = 0;
   
   xrtp_hrtime_t hz_start = 0;
   xrtp_hrtime_t hz_passed = 0;

   portaudio_device_t * pa = xmalloc (sizeof(struct portaudio_device_s));
   if (!pa) {

      pa_debug(("media_new_device: No enough memory\n"));

      return NULL;
   }
   
   memset(pa, 0, sizeof(struct portaudio_device_s));

   pa->time_match = 1;

   pa->sample_factor = DEFAULT_SAMPLE_FACTOR;

   pa->clock = time_start();

   dev = (media_device_t *)pa;

   dev->pipe = pa_pipe;

   dev->init = pa_init;
   dev->start = pa_start;
   dev->stop = pa_stop;
   
   dev->done = pa_done;

   dev->set_input_media = pa_set_input_media;
   dev->set_output_media = pa_set_output_media;
   
   dev->new_setting = pa_new_setting;
   dev->setting = pa_setting;
   
   dev->match_type = pa_match_type;
   dev->match_mode = pa_match_mode;

   /* test speed */
   delay = DELAY_WHILE;

   hz_start = time_nsec_now(pa->clock);
   while (delay)
      delay--;
      
   hz_passed = time_nsec_spent(pa->clock, hz_start);

   pa->while_ns = hz_passed / DELAY_WHILE;


   if ( pa_online(dev) >= MP_OK) 
	   pa->online = 1;

   pa_log(("media_new_device: PortAudio device created, with %dus pulse\n", pa->usec_pulse));

   return dev;
}

/**
 * Loadin Infomation Block
 */
extern DECLSPEC module_loadin_t mediaformat = 
{
   "device",   /* Label */

   000001,         /* Plugin version */
   000001,         /* Minimum version of lib API supportted by the module */
   000001,         /* Maximum version of lib API supportted by the module */

   media_new_device   /* Module initializer */
};

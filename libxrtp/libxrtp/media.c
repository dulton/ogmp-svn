/***************************************************************************
                          media.c  -  media commen function
                             -------------------
    begin                : Wed Dec 31 2003
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

#include "internal.h"

 #ifdef MEDIA_LOG
   const int media_log = 1;
 #else
   const int media_log = 0;
 #endif
 #define media_log(fmt, args...)  do{if(media_log) printf(fmt, ##args);}while(0)

/* Convert realtime to rtp ts */
uint32 media_realtime_to_rtpts(xrtp_media_t * media, xrtp_lrtime_t lrt, xrtp_hrtime_t hrt){

   uint32 rtpts = lrt / LRTIME_SECOND_DIVISOR * media->clockrate * media->sampling_instance;
   rtpts += (hrt % LRT_HRT_DIVISOR) / (HRTIME_SECOND_DIVISOR / media->clockrate) * media->sampling_instance;

   media_log("media_realtime_to_rtpts: lrt[%d]:hrt[%d] on arrival\n", lrt, hrt);
   media_log("media_realtime_to_rtpts: rtp_ts on arrival is %d\n", rtpts);
   return rtpts;
}

int media_set_callback(xrtp_media_t *media, int type, void* call, void* user){

    switch(type){

       case(CALLBACK_MEDIA_ARRIVED):

           media->$callbacks.media_arrived = call;
           media->$callbacks.media_arrived_user = user;
           media_log("text/rtp-test.tm_media_set_callback: 'media_arrived' callback set\n");
           break;

       case(CALLBACK_MEDIA_FINISH):

           media->$callbacks.media_finish = call;
           media->$callbacks.media_finish_user = user;
           media_log("text/rtp-test.tm_media_set_callback: 'media_finish' callback set\n");
           break;

       case(CALLBACK_MEDIA_REPORT):

           media->$callbacks.media_report = call;
           media->$callbacks.media_report_user = user;
           media_log("text/rtp-test.tm_media_set_callback: 'media_report' callback set\n");
           break;

       default:

           media_log("< text/rtp-test.tm_media_set_callback: callback[%d] unsupported >\n", type);
           return XRTP_EUNSUP;
    }

    return XRTP_OK;
}



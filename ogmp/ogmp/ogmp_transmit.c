/***************************************************************************
                          ogmp_transmit.c  -  Play Ogg Media
                             -------------------
    begin                : Wed Jan 28 2004
    copyright            : (C) 2004 by Heming Ling
    email                : heming@timedia.org, heming@bigfoot.com
 ***************************************************************************/
 
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "ogmp.h"
#include <timedia/xstring.h>

#define TRAN_LOG

#ifdef TRAN_LOG
 #define tran_log(fmtargs)  do{printf fmtargs;}while(0)
#else
 #define tran_log(fmtargs)
#endif

/**************************************************************
 * rtp capable descript module
 */
int rtp_descript_done(capable_descript_t *cap)
{
	rtpcap_descript_t *rtpcap = (rtpcap_descript_t*)cap;

	xstr_done_string(rtpcap->profile_mime);
	xstr_done_string(rtpcap->ipaddr);
	free(rtpcap);

	return MP_OK;
}

int rtp_descript_match_type(capable_descript_t *cap, char *type)
{
	return !strcmp("rtp", type);
}

int rtp_descript_match(capable_descript_t *me, capable_descript_t *oth)
{
	rtpcap_descript_t *rtpoth;
	rtpcap_descript_t *rtpme = (rtpcap_descript_t*)me;

	if(!oth->match_type(oth, "rtp"))
		return 0;

	rtpoth = (rtpcap_descript_t*)oth;
	
	return !strcmp(rtpme->profile_mime, rtpoth->profile_mime);
}

rtpcap_descript_t* rtp_capable_descript()
{
	rtpcap_descript_t *rtpcap;

	rtpcap = malloc(sizeof(rtpcap_descript_t));
	if(!rtpcap)
	{
		tran_log(("vsend_capable: no memory for rtp capable"));
		return NULL;
	}
	memset(rtpcap, 0, sizeof(rtpcap_descript_t));

	rtpcap->descript.done = rtp_descript_done;
	rtpcap->descript.match = rtp_descript_match;
	rtpcap->descript.match_type = rtp_descript_match_type;

	return rtpcap;
}

/***************************************************************************
                          ogmp_server.c  -  description
                             -------------------
    begin                : Wed May 19 2004
    copyright            : (C) 2004 by Heming
    email                : heming@bigfoot.com; heming@timedia.org
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

speex_setting_t spx_setting = 
{
	/* rtp_portno, rtcp_portno, media_bps */
	{3080, 3081, 16*1024},

	8000,	/*sample_rate*/
	1,		/*channels*/

	2,		/*mode*/

	100,	/*ptime_max*/
	0,		/*cng*/
	1,		/*penh*/

	0,		/*vbr*/
	0.0,	/*vbr_quality*/
	0,		/*abr*/

	0,		/*cbr*/
	4,		/*cbr_quality*/

	3,		/*complexity*/
	1,		/*denoise*/
	1		/*agc*/
};

speex_setting_t* speex_setting(media_control_t *control)
{
	return &spx_setting;
}

int speex_info_setting(speex_info_t *spxinfo, speex_setting_t *spxset)
{
	if(spxset->rtp_setting.media_bps > 0)
	{
		spxinfo->audioinfo.info.bps = spxset->rtp_setting.media_bps;
	}

#if 0
	{
		void *enc_state = NULL;
		int clockrate = spxset->sample_rate;
		int spxbps = spxset->mode;

		if(clockrate <= 8000)
		{
			/* NarrowBand */
			enc_state = speex_encoder_init(&speex_nb_mode);
			speex_encoder_ctl(enc_state, SPEEX_SET_MODE, &spxset->mode);
			speex_mode_query(&speex_nb_mode, SPEEX_SUBMODE_BITRATE, &spxbps);
		}
		else if(clockrate <= 16000)
		{
			/* WideBand */
			enc_state = speex_encoder_init(&speex_wb_mode);
			speex_encoder_ctl(enc_state, SPEEX_SET_MODE, &spxset->mode);
			speex_mode_query(&speex_wb_mode, SPEEX_SUBMODE_BITRATE, &spxbps);
		}
		else if(clockrate <= 32000)
		{
			/* UltraBand */
			enc_state = speex_encoder_init(&speex_uwb_mode);
			speex_encoder_ctl(enc_state, SPEEX_SET_MODE, &spxset->mode);
			speex_mode_query(&speex_uwb_mode, SPEEX_SUBMODE_BITRATE, &spxbps);
		}
		else
		{
			spxbps = 0;
		}

		spxinfo->audioinfo.info.bps = spxbps;

		speex_encoder_destroy(enc_state);
	}
#endif

	spxinfo->audioinfo.info.sample_rate = spxset->sample_rate;
	spxinfo->audioinfo.channels = spxset->channels;

	spxinfo->penh = spxset->penh;
	spxinfo->cng = spxset->cng;
	spxinfo->ptime = spxset->ptime_max;

	spxinfo->vbr = spxset->vbr;
	spxinfo->abr = spxset->abr;
	spxinfo->cbr = spxset->cbr;

	spxinfo->complexity = spxset->complexity;

	spxinfo->vbr_quality = spxset->vbr_quality;
	spxinfo->cbr_quality = spxset->cbr_quality;

	spxinfo->denoise = spxset->denoise;
	spxinfo->agc = spxset->agc;

	return MP_OK;
}

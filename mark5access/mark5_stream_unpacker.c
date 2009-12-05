/***************************************************************************
 *   Copyright (C) 2006, 2007 by Walter Brisken                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
//===========================================================================
// SVN properties (DO NOT CHANGE)
//
// $Id$
// $HeadURL$
// $LastChangedRevision$
// $Author$
// $LastChangedDate$
//
//============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mark5access/mark5_stream.h"

static int mark5_stream_unpacker_next(struct mark5_stream *ms)
{
	ms->payload += ms->framebytes;

	/* successfully got new frame, so increment it */
	ms->framenum++;
	ms->readposition = 0;

	return 0;
}

static int mark5_stream_unpacker_next_noheaders(struct mark5_stream *ms)
{
	ms->payload += ms->databytes;

	/* successfully got new frame, so increment it */
	ms->framenum++;
	ms->readposition = 0;

	return 0;
}

static int mark5_stream_unpacker_init(struct mark5_stream *ms)
{
	ms->frame = 0;
	ms->payload = 0;
	ms->datawindow = 0;
	ms->datawindowsize = 0;
	ms->blanker = blanker_none;
	ms->log2blankzonesize = 30;
	ms->mjd = -1;
	if(ms->next == mark5_stream_unpacker_next_noheaders)
	{
		sprintf(ms->streamname, "Unpacker-no-headers");
	}
	else
	{
		sprintf(ms->streamname, "Unpacker-with-headers");
	}
	
	return 0;
}

struct mark5_stream_generic *new_mark5_stream_unpacker(int noheaders)
{
	struct mark5_stream_generic *V;

	V = (struct mark5_stream_generic *)calloc(1,
		sizeof(struct mark5_stream_generic));

	V->init_stream = mark5_stream_unpacker_init;
	if(noheaders)
	{
		V->next = mark5_stream_unpacker_next_noheaders;
	}
	else
	{
		V->next = mark5_stream_unpacker_next;
	}
	V->seek = 0;
	V->final_stream = 0;
	V->inputdata = 0;

	return V;
}

int mark5_unpack(struct mark5_stream *ms, void *packed, float **unpacked, 
	int nsamp)
{
	int v;
	int c;

	if(ms->next == mark5_stream_unpacker_next_noheaders)
	{
		ms->payload = (unsigned char *)packed;
	}
	else
	{
		ms->frame = (unsigned char *)packed;
		v = ms->validate(ms);
		if(!v)
		{
			/* If validation fails, blank entire block of data */
			ms->nvalidatefail++;
			for(c = 0; c < ms->nchan; c++)
			{
				memset(unpacked[c], 0, nsamp*sizeof(float));
			}
			return 0;
		}
		else
		{
			ms->nvalidatepass++;
		}
		ms->frame = 0;

		ms->payload = (unsigned char *)packed + ms->payloadoffset;
	}
	ms->readposition = 0;
	
	ms->blanker(ms);

	return ms->decode(ms, nsamp, unpacked);
}

int mark5_unpack_with_offset(struct mark5_stream *ms, void *packed, 
	int offsetsamples, float **unpacked, int nsamp)
{
	int v;

	if(ms->next == mark5_stream_unpacker_next_noheaders)
	{
		ms->payload = (unsigned char *)packed;
	}
	else
	{
		ms->frame = (unsigned char *)packed + (offsetsamples/ms->framesamples)*ms->framebytes;
		v = ms->validate(ms);
		if(!v)
		{
			ms->nvalidatefail++;
		}
		else
		{
			ms->nvalidatepass++;
		}
		ms->frame = 0;

		ms->payload = (unsigned char *)packed + ms->payloadoffset;
	}
	/* add to offset the integer number of frames */
	ms->payload += ms->framebytes*(offsetsamples/ms->framesamples);

	/* set readposition to first desired sample */
	ms->readposition = (offsetsamples % ms->framesamples)*ms->nchan*ms->nbit*ms->decimation/8;

	ms->blanker(ms);

	return ms->decode(ms, nsamp, unpacked);
}

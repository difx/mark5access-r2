/***************************************************************************
 *   Copyright (C) 2007 by Walter Brisken                                  *
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

#include <stdlib.h>
#include "../mark5access/mark5_stream.h"

const char program[] = "m5test";
const char author[]  = "Walter Brisken";
const char version[] = "1.0";
const char verdate[] = "2007 Oct 6";

int usage(const char *pgm)
{
	printf("\n");

	printf("%s ver. %s   %s  %s\n\n", program, version, author, verdate);
	printf("A Mark5 tester.  Can verify VLBA, Mark3/4, and Mark5B "
		"formats using the\nmark5access library.\n\n");
	printf("Usage : %s <file> <dataformat> [<offset>]\n\n", pgm);
	printf("  <file> is the name of the input file\n\n");
	printf("  <dataformat> should be of the form: "
		"<FORMAT>-<Mbps>-<nchan>-<nbit>, e.g.:\n");
	printf("    VLBA1_2-256-8-2\n");
	printf("    MKIV1_4-128-2-1\n");
	printf("    Mark5B-512-16-2\n\n");
	printf("  <offset> is number of bytes into file to start decoding\n\n");

	return 0;
}

int verify(const char *filename, const char *formatname, const char *f,
	long long offset)
{
	struct mark5_stream *ms;
	float **data;
	int i, j, k, status;
	long long chunk = 1024;
	long long total, unpacked;

	total = unpacked = 0;

	printf("offset = %lld\n", offset);

	ms = new_mark5_stream(
		new_mark5_stream_file(filename, offset),
		new_mark5_format_generic_from_string(formatname) );

	if(!ms)
	{
		printf("problem opening %s\n", filename);
		return 0;
	}

	data = (float **)malloc(ms->nchan*sizeof(float *));
	for(i = 0; i < ms->nchan; i++)
	{
		data[i] = (float *)malloc(chunk*sizeof(float ));
	}

	mark5_stream_print(ms);

	for(i = 0;; i++)
	{
		status = mark5_stream_decode(ms, chunk, data);
		
		if(status < 0)
		{
			printf("<EOF> status=%d\n", status);
			break;
		}
		else
		{
			total += chunk;
			unpacked += status;
		}
		if(i%1024 == 0)
		{
			int mjd, sec;
			double ns;
			mark5_stream_get_frame_time(ms, &mjd, &sec, &ns);
			printf("f=%lld mjd=%d sec=%d ns=%011.1f valid=%d invalid=%d\n", 
				ms->framenum, mjd, sec, ns,
				ms->nvalidatepass, ms->nvalidatefail);
		}

		if(ms->nvalidatefail > 20)
		{
			break;
		}
	}

	fprintf(stderr, "%Ld / %Ld samples unpacked\n", unpacked, total);

	for(i = 0; i < ms->nchan; i++)
	{
		free(data[i]);
	}
	free(data);

	delete_mark5_stream(ms);

	return 0;
}

int main(int argc, char **argv)
{
	long long offset = 0;

	if(argc == 2)
	{
		struct mark5_format *mf;
		int bufferlen = 1<<11;
		char *buffer;
		FILE *in;

		buffer = malloc(bufferlen);
		
		in = fopen(argv[1], "r");
		fread(buffer, bufferlen, 1, in);
		
		mf = new_mark5_format_from_stream(
			new_mark5_stream_memory(buffer, bufferlen/2));

		print_mark5_format(mf);
		delete_mark5_format(mf);

		mf = new_mark5_format_from_stream(
			new_mark5_stream_memory(buffer, bufferlen/2));

		print_mark5_format(mf);
		delete_mark5_format(mf);

		free(buffer);

		return 0;
	}

	else if(argc < 3)
	{
		return usage(argv[0]);
	}

	if(argc > 3)
	{
		offset=atoll(argv[3]);
	}

	verify(argv[1], argv[2], "%2.0f ", offset);

	return 0;
}


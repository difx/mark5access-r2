/***************************************************************************
 *   Copyright (C) 2010 by Walter Brisken                                  *
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
// $Id: m5spec.c 2293 2010-07-05 16:42:46Z WalterBrisken $
// $HeadURL: https://svn.atnf.csiro.au/difx/libraries/mark5access/trunk/mark5access/mark5_stream.c $
// $LastChangedRevision: 2293 $
// $Author: WalterBrisken $
// $LastChangedDate: 2010-07-05 10:42:46 -0600 (Mon, 05 Jul 2010) $
//
//============================================================================

#include <stdio.h>
#include <complex.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fftw3.h>
#include <math.h>
#include "../mark5access/mark5_stream.h"

const char program[] = "zerocorr";
const char author[]  = "Walter Brisken";
const char version[] = "0.1";
const char verdate[] = "2010 Aug 06";

const int MaxLineLen = 256;

int die = 0;

typedef void (*sighandler_t)(int);

void siginthand(int j)
{
	printf("\nBeing killed\n");
	die = 1;
}


typedef struct
{
	struct mark5_stream *ms;
	double **data;
	fftw_complex *zdata, *spec;
	fftw_plan plan;

	char *inputFile;
	char *dataFormat;
	long long offset;
	int subBand;
	int fftSize;
	int startChan;
	int nChan;
} DataStream;

typedef struct
{
	DataStream *ds1, *ds2;
	char *confFile;
	char *outputFile;
	int nFFT;
	int nChan;
	fftw_complex *visibility;
	double *ac1, *ac2;
	FILE *out;
} Baseline;

void deleteDataStream(DataStream *ds);
void deleteBaseline(Baseline *B);


void stripEOL(char *str)
{
	int i;
	int lastGood = 0;

	for(i = 0; str[i]; i++)
	{
		if(str[i] == '#')
		{
			break;
		}
		if(str[i] > ' ')
		{
			lastGood = i;
		}
	}

	str[lastGood+1] = 0;
}


DataStream *newDataStream(FILE *in)
{
	const int NItem = 7;
	DataStream *ds;
	char buffer[NItem][MaxLineLen+1];
	int i;
	char *v;

	ds = (DataStream *)calloc(1, sizeof(DataStream));

	for(i = 0; i < NItem; i++)
	{
		v = fgets(buffer[i], MaxLineLen, in);
		if(!v)
		{
			deleteDataStream(ds);
			
			return 0;
		}
		stripEOL(buffer[i]);
	}

	ds->inputFile = strdup(buffer[0]);
	ds->dataFormat = strdup(buffer[1]);
	ds->subBand = atoi(buffer[2]);
	ds->offset = atoll(buffer[3]);
	ds->fftSize = atoi(buffer[4]);
	ds->startChan = atoi(buffer[5]);
	ds->nChan = atoi(buffer[6]);

	if(ds->startChan < 0 || 
	   ds->startChan >= ds->fftSize/2 || 
	   ds->startChan + ds->nChan < -1 || 
	   ds->startChan + ds->nChan > ds->fftSize/2)
	{
		printf("Input parameters are not legal for file %s\n", ds->inputFile);

		deleteDataStream(ds);

		return 0;
	}

	ds->ms = new_mark5_stream(
		new_mark5_stream_file(ds->inputFile, ds->offset),
		new_mark5_format_generic_from_string(ds->dataFormat) );

	if(!ds->ms)
	{
		printf("problem opening %s\n", ds->inputFile);

		deleteDataStream(ds);
		
		return 0;
	}

	mark5_stream_print(ds->ms);

	ds->data = (double **)calloc(ds->ms->nchan, sizeof(double *));
	for(i = 0; i < ds->ms->nchan; i++)
	{
		ds->data[i] = (double *)calloc(ds->fftSize+2, sizeof(double));
	}
	ds->zdata = (fftw_complex *)calloc(ds->fftSize/2+2, sizeof(fftw_complex));
	ds->spec = (fftw_complex *)calloc(abs(ds->nChan), sizeof(fftw_complex));
	ds->plan = fftw_plan_dft_r2c_1d(ds->fftSize, ds->data[ds->subBand], ds->zdata, FFTW_MEASURE);

	return ds;
}

void deleteDataStream(DataStream *ds)
{
	int i;

	if(ds)
	{
		if(ds->ms)
		{
			if(ds->data)
			{
				for(i = 0; i < ds->ms->nchan; i++)
				{
					if(ds->data[i])
					{
						free(ds->data[i]);
						ds->data[i] = 0;
					}
				}
				free(ds->data);
				ds->data = 0;
			}
			delete_mark5_stream(ds->ms);
			ds->ms = 0;
		}
		if(ds->zdata)
		{
			free(ds->zdata);
			ds->zdata = 0;
		}
		if(ds->spec)
		{
			free(ds->spec);
			ds->spec = 0;
		}
		if(ds->inputFile)
		{
			free(ds->inputFile);
			ds->inputFile = 0;
		}
		if(ds->dataFormat)
		{
			free(ds->dataFormat);
			ds->inputFile = 0;
		}
		if(ds->plan)
		{
			fftw_destroy_plan(ds->plan);
			ds->plan = 0;
		}
	}
}

int feedDataStream(DataStream *ds)
{
	int i, status;

	status = mark5_stream_decode_double(ds->ms, ds->fftSize/2, ds->data);

	if(status < 0)
	{
		return status;
	}

	fftw_execute(ds->plan);

	if(ds->nChan > 0)
	{
		for(i = 0; i < ds->nChan; i++)
		{
			ds->spec[i] = ds->zdata[ds->startChan+i];
		}
	}
	else
	{
		for(i = 0; i < -ds->nChan; i++)
		{
			/* FIXME : I think this conjugation is needed! -WFB 20100806 */
			ds->spec[i] = ~ds->zdata[ds->startChan-i];
		}
	}

	return 0;
}

void printDataStream(const DataStream *ds)
{
	printf("  DataStream [%p]\n", ds);
	if(!ds)
	{
		return;
	}
	printf("    input file = %s\n", ds->inputFile);
	printf("    data format = %s\n", ds->dataFormat);
	printf("    file offset = %Ld\n", ds->offset);
	printf("    sub band to process = %d\n", ds->subBand);
	printf("    fft size = %d\n", ds->fftSize);
	printf("    start channel = %d\n", ds->startChan);
	printf("    number of channels to keep = %d\n", ds->nChan);
}

Baseline *newBaseline(const char *confFile)
{
	Baseline *B;
	FILE *in;
	const int NItem = 2;
	char buffer[NItem][MaxLineLen+1];
	int i;
	char *v;

	in = fopen(confFile, "r");
	if(!in)
	{
		fprintf(stderr, "Cannot open conf file %s\n", confFile);
	}

	B = (Baseline *)calloc(1, sizeof(Baseline));
	B->confFile = strdup(confFile);
	B->ds1 = newDataStream(in);
	B->ds2 = newDataStream(in);

	if(!B->ds1 || !B->ds2)
	{
		deleteBaseline(B);

		fclose(in);

		return 0;
	}
	
	for(i = 0; i < NItem; i++)
	{
		v = fgets(buffer[i], MaxLineLen, in);
		if(!v)
		{
			deleteBaseline(B);

			fclose(in);

			return 0;
		}
		stripEOL(buffer[i]);
	}

	fclose(in);

	if(abs(B->ds1->nChan) != abs(B->ds2->nChan) || B->ds1->nChan <= 0)
	{
		fprintf(stderr, "Number of channels per datastream must match and be positive (%d %d)\n",
			B->ds1->nChan, B->ds2->nChan);

		deleteBaseline(B);

		return 0;
	}
	
	B->nChan = abs(B->ds1->nChan);
	B->outputFile = strdup(buffer[0]);
	B->nFFT = atoi(buffer[1]);
	if(B->nFFT <= 0)
	{
		B->nFFT = 0x7FFFFFFF;	/* effectively no limit */
	}
	B->visibility = (fftw_complex *)calloc(B->nChan, sizeof(fftw_complex));
	B->ac1 = (double *)calloc(B->nChan, sizeof(double));
	B->ac2 = (double *)calloc(B->nChan, sizeof(double));

	B->out = fopen(B->outputFile, "w");
	if(!B->out)
	{
		fprintf(stderr, "Cannot open %s for output\n", B->outputFile);

		deleteBaseline(B);

		return 0;
	}

	return B;
}

void deleteBaseline(Baseline *B)
{
	if(B)
	{
		if(B->confFile)
		{
			free(B->confFile);
			B->confFile = 0;
		}
		if(B->outputFile)
		{
			free(B->outputFile);
			B->outputFile = 0;
		}
		if(B->visibility)
		{
			free(B->visibility);
			B->visibility = 0;
		}
		if(B->ac1)
		{
			free(B->ac1);
			B->ac1 = 0;
		}
		if(B->ac2)
		{
			free(B->ac2);
			B->ac2 = 0;
		}
		if(B->out)
		{
			fclose(B->out);
			B->out = 0;
		}
		free(B);
	}
}

void printBaseline(const Baseline *B)
{
	printf("Baseline [%p]\n", B);
	if(B == 0)
	{
		return;
	}
	printDataStream(B->ds1);
	printDataStream(B->ds2);
	printf("  conf file = %s\n", B->confFile);
	printf("  output file = %s\n", B->outputFile);
	printf("  nFFT = %d\n", B->nFFT);
	printf("  nChan = %d\n", B->nChan);
}





int usage(const char *pgm)
{
	printf("\n");

	printf("%s ver. %s   %s  %s\n\n", program, version, author, verdate);
	printf("A zero baseline cross correlator\n\n");
	printf("Usage: %s [ -h | <conf file> ]\n\n", pgm);
	printf("The conf file should have 16 lines as follows:\n"
"For the first datastream:\n"
"   1  Input file name\n"
"   2  Input format (e.g., Mark5B-2048-16-2)\n"
"   3  Input sub-band to process (0-based index)\n"
"   4  Offset into the file (bytes)\n"
"   5  Size of FFT to perform over the original bandwidth\n"
"   6  First channel (spectral point) to correlate\n"
"   7  Number of channels to correlate (negative for LSB)\n"
"For the second datastream:\n"
"   8  Input file name\n"
"   9  Input format (e.g., Mark5B-2048-16-2)\n"
"  10  Input sub-band to process (0-based index)\n"
"  11  Offset into the file (bytes)\n"
"  12  Size of FFT to perform over the original bandwidth\n"
"  13  First channel to correlate\n"
"  14  Number of channels to correlate (negative for LSB)\n"
"Other general parameters:\n"
"  15  Name of output file\n"
"  16  Number of FFTs to process\n\n");
	printf("The output file (specified in line 16 above) has 6 columns:\n"
"   1  Channel (spectral point) number\n"
"   2  Frequency relative to first spectral channel (NYI)\n"
"   3  Real value of the visibility\n"
"   4  Imaginary value of the visibility\n"
"   5  Autocorrelation of the first datastream (real only)\n"
"   6  Autocorrelation of the second datastream (real only)\n\n");

	return 0;
}

int zerocorr(const char *confFile, int verbose)
{
	Baseline *B;
	int n, j, v;
	sighandler_t oldsiginthand;

	oldsiginthand = signal(SIGINT, siginthand);

	B = newBaseline(confFile);
	if(!B)
	{
		return -1;
	}

	if(verbose > 0)
	{
		printBaseline(B);
	}

	for(n = 0; n < B->nFFT; n++)
	{
		if(verbose > 1 && n % 10 == 0)
		{
			printf("%d of %d FFTs complete\n", n, B->nFFT);
		}

		v = feedDataStream(B->ds1);
		if(v < 0)
		{
			break;
		}
		v = feedDataStream(B->ds2);
		if(v < 0)
		{
			break;
		}

		for(j = 0; j < B->nChan; j++)
		{
			B->visibility[j] += B->ds1->spec[j]*~B->ds2->spec[j];
			B->ac1[j] += creal(B->ds1->spec[j]*~B->ds1->spec[j]);
			B->ac2[j] += creal(B->ds2->spec[j]*~B->ds2->spec[j]);
		}

		if(die)
		{
			fprintf(stderr, "\nStopping early at %d / %d\n", n+1, B->nFFT);
			break;
		}
	}

	if(n == 0)
	{
		fprintf(stderr, "No data correlated!\n");
	}
	else
	{
		printf("%d FFTs processed\n", n);
		for(j = 0; j < B->nChan; j++)
		{
			fprintf(B->out, "%d %f %f %f %f %f\n", j, 0.0, creal(B->visibility[j])/n, cimag(B->visibility[j])/n, B->ac1[j]/n, B->ac2[j]/n);
		}
	}

	deleteBaseline(B);

	return 0;
}

int main(int argc, char **argv)
{
	if(argc == 2)
	{
		if(strcmp(argv[1], "-h") == 0 ||
		   strcmp(argv[1], "--help") == 0)
		{
			return usage(argv[0]);
		}
		else
		{
			zerocorr(argv[1], 2);
		}
	}
	else
	{
		return usage(argv[0]);
	}

	return 0;
}


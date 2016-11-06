/*
 * Copyright (c) 2012 Institute of Geological & Nuclear Sciences Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 */

/* system includes */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <math.h>

/* libmseed library includes */
#include <libmseed.h>
#include <libtidal.h>
#include <libcrex.h>

#define PROGRAM "msdetide" /* program name */

#ifndef FIRFILTERS
#define FIRFILTERS "filters.fir"
#endif // FIRFILTERS

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "xxx"
#endif

/*
 * msdetide: process raw miniseed tidal records to convert from recorded counts into sea level heights, with and without the tidal components
 *
 */

/* program variables */
static char *program_name = PROGRAM;
static char *program_version = PROGRAM " (" PACKAGE_VERSION ") (c) GNS 2012 (m.chadwick@gns.cri.nz)";
static char *program_usage = PROGRAM " [-hv][-G <dir>][-A <alpha>][-B <beta>][-O <orient>][-L <latitude>][-Z <zone>][-T <label/amp/lag> ...][<files> ... ]";
static char *program_prefix = "[" PROGRAM "] ";

static int verbose = 0; /* program verbosity */

static char *tag = "";
static double alpha = 0.0;
static double beta = 10.0;

static double zone = 0.0;
static double latitude = 0.0;
static char *gts = ".";

static char *firfile = FIRFILTERS;

static void log_print(char *message) {
  if (verbose)
    fprintf(stderr, "%s", message);
}

static void err_print(char *message) {
  fprintf(stderr, "error: %s", message);
}

static void record_handler (char *record, int reclen, void *extra) {
    static MSRecord *msr = NULL;
    BTime btime;
    int mon, mday;
    hptime_t endtime;
    char streamid[100];
    char samples[512];
    char tmpfile[1024];
    char outfile[1024];
    int errsv = 0;
    FILE *fp = NULL;
    int rv;

    if ((rv = msr_unpack (record, reclen, &msr, 1, 0)) != MS_NOERROR) {
        ms_log (2, "error unpacking mseed record: %s", ms_errorstr(rv)); return;
    }
    msr_srcname (msr, streamid, 0);
    if (ms_hptime2btime (msr->starttime, &btime) < 0) {
        ms_log (2, "error unpacking hptime"); return;
    }
    ms_doy2md(btime.year, btime.day, &mon, &mday);
    if ((msr->sampletype == 'a') && (msr->numsamples > 0)) {
        sprintf(outfile, "%s/%s.%04d%02d%02d%02d%02d.txt", gts, streamid, btime.year, mon, mday, btime.hour, btime.min);
        sprintf(tmpfile, "%s/.%s.%04d%02d%02d%02d%02d.txt", gts, streamid, btime.year, mon, mday, btime.hour, btime.min);
        if ((fp = fopen(tmpfile, "a")) == NULL) {
            errsv = errno; ms_log(2, "failed to open output file: %s - %s\n", tmpfile, strerror(errsv)); return;
        }
        memset(samples, 0, sizeof(samples));
        strncpy(samples, msr->datasamples, msr->numsamples);
        fprintf(fp, "%s", samples);
        fclose(fp);
        if (rename(tmpfile, outfile) != 0) {
            errsv = errno; ms_log(2, "failed to rename temporary file: %s - %s\n", outfile, strerror(errsv)); return;
        }
    }
}

int main(int argc, char **argv) {
    int n;

  MSRecord *msr = NULL;

    char srcname[100];
    crex_tidal_t tidal;

    crex_stream_t *sp = NULL;
    crex_stream_t *stream = NULL;
    crex_stream_t *streams = NULL;

    int nfirs = 0;
    char *firnames[FIR_MAX_FILTERS];

    int psamples = 0;

  int rc;
  int option_index = 0;
  struct option long_options[] = {
    {"help", 0, 0, 'h'},
    {"verbose", 0, 0, 'v'},
        {"firfile", 1, 0, 'N'},
        {"filter", 1, 0, 'F'},
    {"tag", 1, 0, 'I'},
    {"alpha", 1, 0, 'A'},
    {"beta", 1, 0, 'B'},
    {"orient", 1, 0, 'O'},
    {"latitude", 1, 0, 'L'},
    {"zone", 1, 0, 'Z'},
    {"tide", 1, 0, 'T'},
    {"gts", 1, 0, 'G'},
    {0, 0, 0, 0}
  };

  /* adjust output logging ... -> syslog maybe? */
  ms_loginit (log_print, program_prefix, err_print, program_prefix);

    memset(&tidal, 0, sizeof(crex_tidal_t));

  while ((rc = getopt_long(argc, argv, "hvN:F:I:G:A:B:T:L:Z:", long_options, &option_index)) != EOF) {
    switch(rc) {
    case '?':
      (void) fprintf(stderr, "usage: %s\n", program_usage);
      exit(-1); /*NOTREACHED*/
    case 'h':
      (void) fprintf(stderr, "\n[%s] miniseed tidal correction\n\n", program_name);
      (void) fprintf(stderr, "usage:\n\t%s\n", program_usage);
      (void) fprintf(stderr, "version:\n\t%s\n", program_version);
      (void) fprintf(stderr, "options:\n");
      (void) fprintf(stderr, "\t-h --help\tcommand line help (this)\n");
      (void) fprintf(stderr, "\t-v --verbose\trun program in verbose mode\n");
            (void) fprintf(stderr, "\t-N --firfile\tprovide an alternative fir-filters file [%s]\n", firfile);
            (void) fprintf(stderr, "\t-F --filter\tadd a decimation firfilter\n");
      (void) fprintf(stderr, "\t-I --tag\tprovide CREX ID tag [%s]\n", tag);
      (void) fprintf(stderr, "\t-G --gts\tprovide a directory for GTS minute files [%s]\n", gts);
      (void) fprintf(stderr, "\t-A --alpha\tadd offset to calculated tidal heights [%g]\n", alpha);
      (void) fprintf(stderr, "\t-B --beta\tscale calculated tidal heights [%g]\n", beta);
      (void) fprintf(stderr, "\t-L --latitude\tprovide reference latitude [%g]\n", latitude);
      (void) fprintf(stderr, "\t-Z --zone\tprovide reference time zone offet [%g]\n", zone);
      (void) fprintf(stderr, "\t-T --tide\tprovide tidal constants [<label>/<amplitude>/<lag>]\n");
      exit(0); /*NOTREACHED*/
    case 'v':
      verbose++;
      break;
    case 'I':
      tag = optarg;
      break;
    case 'G':
      gts = optarg;
      break;
    case 'A':
      alpha = atof(optarg);
      break;
    case 'B':
      beta = atof(optarg);
      break;
    case 'L':
      tidal.latitude = atof(optarg);
      break;
    case 'Z':
      tidal.zone = atof(optarg);
      break;
    case 'T':
            if (tidal.num_tides < LIBTIDAL_MAX_CONSTITUENTS) {
                strncpy(tidal.tides[tidal.num_tides].name, strtok(strdup(optarg), "/"), LIBTIDAL_CHARLEN - 1);
                tidal.tides[tidal.num_tides].amplitude = atof(strtok(NULL, "/"));
                tidal.tides[tidal.num_tides].lag = atof(strtok(NULL, "/")) / 360.0;
                tidal.num_tides++;
            }
      break;
    }
  }

  /* report the program version */
  if (verbose)
    ms_log (0, "%s\n", program_version);

  if (verbose) {
      ms_log (0, "tidal zone=%g latitude=%g alpha=%g beta=%g\n", tidal.zone, tidal.latitude, alpha, beta);
        for (rc = 0; rc < tidal.num_tides; rc++) {
      ms_log (0, "\t[%s] %g (%6.3f)\n", tidal.tides[rc].name, tidal.tides[rc].amplitude, tidal.tides[rc].lag);
        }
    }

    tidal.zone = zone;
    tidal.latitude = latitude;

    /* load the base firfilter definitions if required */
    if ((nfirs > 0) && (firfilter_load(firfile) < 0)) {
        ms_log(1, "could not load fir filter file [%s]\n", firfile); exit(-1);
    }

    do {
        if (verbose)
      ms_log (0, "process miniseed data from %s\n", (optind < argc) ? argv[optind] : "<stdin>");

    while ((rc = ms_readmsr (&msr, (optind < argc) ? argv[optind] : "-", 0, NULL, NULL, 1, 1, (verbose > 1) ? 1 : 0)) == MS_NOERROR) {
      if (verbose > 1)
        msr_print(msr, (verbose > 2) ? 1 : 0);
            msr_srcname(msr, srcname, 0);
            for (stream = streams; stream != NULL; stream = stream->next) {
                if (strcmp(stream->srcname, srcname) == 0)
                    break;
            }
            if (stream == NULL) {
                if ((stream = (crex_stream_t *) malloc(sizeof(crex_stream_t))) == NULL) {
                    ms_log(1, "memory error!\n"); exit(-1);
                }
                memset(stream, 0, sizeof(crex_stream_t));
                strcpy(stream->srcname, srcname);

                /* Insert passed ctd values. */
                strncpy(stream->ctd.id, tag, 24);

                stream->alpha = alpha;
                stream->beta = beta;

                /* Insert default ctd values. */
                stream->ctd.time = 0;
                stream->ctd.temp = -1;
                stream->ctd.autoQC = 11;
                stream->ctd.manualQC = 7;
                stream->ctd.offset = 0;
                stream->ctd.increment = 1;

                /* reset the CREX data arrays */
                for (n = 0; n < CREX_BUF_SIZE; n++) {
                    stream->ctd.mes[n] = CREX_NO_DATA;
                    stream->ctd.res[n] = CREX_NO_DATA;
                }

                if (streams != NULL) {
                    stream->next = streams;
                }
                streams = stream;
            }

      if (process_crex(msr, &tidal, stream, record_handler, NULL, &psamples, -1.0, verbose) < 0) {
            ms_log (1, "error processing mseed block\n"); break;
      }

            if (verbose)
                ms_log(0, "packed: %d samples\n", psamples);
    }
    if (rc != MS_ENDOFFILE )
        ms_log (2, "error reading stdin: %s\n", ms_errorstr(rc));

    /* Cleanup memory and close file */
    ms_readmsr (&msr, NULL, 0, NULL, NULL, 0, 0, (verbose > 1) ? 1 : 0);
    } while((++optind) < argc);

    stream = streams;
    while (stream != NULL) {
        sp = stream; stream = stream->next;
        free((char *) sp);
    }

  /* closing down */
  if (verbose)
    ms_log (0, "terminated\n");

  /* done */
  return(0);
}

/*
 * Copyright (c) 2014 Institute of Geological & Nuclear Sciences Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *		notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *		notice, this list of conditions and the following disclaimer in the
 *		documentation and/or other materials provided with the distribution.
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
 */

/* system includes */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <math.h>

/* libmseed library includes */
#include <libmseed.h>
#include <libslink.h>
#include <libtidal.h>
#include <libcrex.h>

#define PROGRAM "slgts" /* program name */

#ifndef FIRFILTERS
#define FIRFILTERS "filters.fir"
#endif // FIRFILTERS

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "xxx"
#endif

/*
 * sldetide: convert raw tidal counts into sea level heights, with and without a tidal correction
 *
 */

/* program variables */
static char *program_name = PROGRAM;
static char *program_version = PROGRAM " (" PACKAGE_VERSION ") (c) GNS 2014 (m.chadwick@gns.cri.nz)";
static char *program_usage = PROGRAM " [-hv][-I <tag>][-A <alpha>][-B <beta>][-L <latitude>][-Z <zone>][-T <label/amp/lag> ...][<seedlink_options>] [<server>] [<gts_dir>]";
static char *program_prefix = "[" PROGRAM "] ";

static int verbose = 0; /* program verbosity */

static char *tag = "";
static double alpha = 0.0;
static double beta = 1.0;

static double zone = 0.0;
static double latitude = 0.0;

static char *seedlink = ":18000"; /* datalink server to use */
static char *gts = NULL; /* gts directory to use */

/* possible options */
static int unimode = 0;
static char *multiselect = NULL;
static char *selectors = "?TZ";
static char *statefile = NULL;
static char *streamfile = NULL;
static int stateint = 300;

static SLCD *slconn = NULL;
static char *firfile = FIRFILTERS;

/* handle any KILL/TERM signals */
static void term_handler(int sig) {
	sl_terminate(slconn); return;
}

static void dummy_handler (int sig) {
	return;
}

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

	/* logging */
	if (verbose > 0)
		msr_print(msr, (verbose > 2) ? 1 : 0);

    if ((rv = msr_unpack (record, reclen, &msr, 1, 0)) != MS_NOERROR) {
        ms_log (2, "error unpacking mseed record: %s", ms_errorstr(rv)); return;
    }
    if ((msr->sampletype == 'a') && (msr->numsamples > 0)) {
        memset(samples, 0, sizeof(samples));
        strncpy(samples, msr->datasamples, msr->numsamples);

        if (gts) {
            msr_srcname (msr, streamid, 0);
            if (ms_hptime2btime (msr->starttime, &btime) < 0) {
                ms_log (2, "error unpacking hptime"); return;
            }
            ms_doy2md(btime.year, btime.day, &mon, &mday);
            sprintf(outfile, "%s/%s.%04d%02d%02d%02d%02d.txt", gts, streamid, btime.year, mon, mday, btime.hour, btime.min);
            sprintf(tmpfile, "%s/.%s.%04d%02d%02d%02d%02d.txt", gts, streamid, btime.year, mon, mday, btime.hour, btime.min);
            if ((fp = fopen(tmpfile, "a")) == NULL) {
                errsv = errno; ms_log(2, "failed to open output file: %s - %s\n", tmpfile, strerror(errsv)); return;
            }
            fprintf(fp, "%s", samples);
            fclose(fp);
            if (rename(tmpfile, outfile) != 0) {
                errsv = errno; ms_log(2, "failed to rename temporary file: %s - %s\n", outfile, strerror(errsv)); return;
            }
        }
        else {
            fprintf(stdout, "%s\n", samples);
        }
    }
}

int main(int argc, char **argv) {
    int n;

	char buf[1024];

    char srcname[100];
    crex_tidal_t tidal;

    crex_stream_t *sp = NULL;
    crex_stream_t *stream = NULL;
    crex_stream_t *streams = NULL;

    /* FIR filter config */
    int nfirs = 0;
    char *firnames[FIR_MAX_FILTERS];

	MSRecord *msr = NULL;
	SLpacket *slpack = NULL;
	int packetcnt = 0;
    int psamples = 0;

	int rc;
	int option_index = 0;
	struct option long_options[] = {
		{"help", 0, 0, 'h'},
		{"verbose", 0, 0, 'v'},
		{"delay", 1, 0, 'd'},
		{"timeout", 1, 0, 't'},
		{"heartbeat", 1, 0, 'k'},
		{"streamlist", 1, 0, 'l'},
		{"streams", 1, 0, 'S'},
		{"selectors", 1, 0, 's'},
		{"statefile", 1, 0, 'x'},
		{"update", 1, 0, 'u'},
		{"firfile", 1, 0, 'N'},
		{"filter", 1, 0, 'F'},
		{"tag", 1, 0, 'I'},
		{"alpha", 1, 0, 'A'},
		{"beta", 1, 0, 'B'},
		{"latitude", 1, 0, 'L'},
		{"zone", 1, 0, 'Z'},
		{"tide", 1, 0, 'T'},
		{0, 0, 0, 0}
	};

	/* posix signal handling */
	struct sigaction sa;

	sa.sa_handler = dummy_handler;
	sa.sa_flags	= SA_RESTART;
	sigemptyset (&sa.sa_mask);
	sigaction (SIGALRM, &sa, NULL);

	sa.sa_handler = term_handler;
	sigaction (SIGINT, &sa, NULL);
	sigaction (SIGQUIT, &sa, NULL);
	sigaction (SIGTERM, &sa, NULL);

	sa.sa_handler = SIG_IGN;
	sigaction (SIGHUP, &sa, NULL);
	sigaction (SIGPIPE, &sa, NULL);

	/* adjust output logging ... -> syslog maybe? */
	ms_loginit (log_print, program_prefix, err_print, program_prefix);

	/* get a new connection description */
	slconn = sl_newslcd();

	while ((rc = getopt_long(argc, argv, "hvd:t:k:l:S:s:x:u:N:F:I:A:B:L:T:Z:", long_options, &option_index)) != EOF) {
		switch(rc) {
		case '?':
			(void) fprintf(stderr, "usage: %s\n", program_usage);
			exit(-1); /*NOTREACHED*/
		case 'h':
			(void) fprintf(stderr, "\n[%s] seedlink crex conversion\n\n", program_name);
			(void) fprintf(stderr, "usage:\n\t%s\n", program_usage);
			(void) fprintf(stderr, "version:\n\t%s\n", program_version);
			(void) fprintf(stderr, "options:\n");
			(void) fprintf(stderr, "\t-h --help\tcommand line help (this)\n");
			(void) fprintf(stderr, "\t-v --verbose\trun program in verbose mode\n");
			(void) fprintf(stderr, "\t-d --delay\talternative seedlink delay [%d]\n", slconn->netdly);
			(void) fprintf(stderr, "\t-t --timeout\talternative seedlink timeout [%d]\n", slconn->netto);
			(void) fprintf(stderr, "\t-k --heartbeat\talternative seedlink heartbeat [%d]\n", slconn->keepalive);
			(void) fprintf(stderr, "\t-l --streamlist\tuse a stream list file [%s]\n", (streamfile) ? streamfile : "<null>");
			(void) fprintf(stderr, "\t-S --streams\talternative seedlink streams [%s]\n", (multiselect) ? multiselect : "<null>");
			(void) fprintf(stderr, "\t-s --selectors\talternative seedlink selectors [%s]\n", (selectors) ? selectors : "<null>");
			(void) fprintf(stderr, "\t-x --statefile\tseedlink statefile [%s]\n", (statefile) ? statefile : "<null>");
			(void) fprintf(stderr, "\t-u --update\talternative state flush interval [%d]\n", stateint);
            (void) fprintf(stderr, "\t-N --firfile\tprovide an alternative fir-filters file [%s]\n", firfile);
            (void) fprintf(stderr, "\t-F --filter\tadd a decimation firfilter\n");
            (void) fprintf(stderr, "\t-I --tag\tprovide CREX ID tag [%s]\n", tag);
			(void) fprintf(stderr, "\t-A --alpha\tadd offset to calculated tidal heights [%g]\n", alpha);
			(void) fprintf(stderr, "\t-B --beta\tscale calculated tidal heights [%g]\n", beta);
            (void) fprintf(stderr, "\t-L --latitude\tprovide reference latitude [%g]\n", latitude);
            (void) fprintf(stderr, "\t-Z --zone\tprovide reference time zone offet [%g]\n", zone);
            (void) fprintf(stderr, "\t-T --tide\tprovide tidal constants [<label>/<amplitude>/<lag>]\n");
			exit(0); /*NOTREACHED*/
		case 'v':
			verbose++;
			break;
		case 'd':
			slconn->netdly = atoi(optarg);
			break;
		case 't':
			slconn->netto = atoi(optarg);
			break;
		case 'k':
			slconn->keepalive = atoi(optarg);
			break;
		case 'l':
			streamfile = optarg;
			break;
		case 's':
			selectors = optarg;
			break;
		case 'S':
			multiselect = optarg;
			break;
		case 'x':
			statefile = optarg;
			break;
		case 'u':
			stateint = atoi(optarg);
			break;
        case 'N':
            firfile = optarg;
            break;
        case 'F':
            if (nfirs < FIR_MAX_FILTERS) {
                firnames[nfirs++] = optarg;
            }
            break;
		case 'I':
			tag = optarg;
			break;
		case 'A':
			alpha = atof(optarg);
			break;
		case 'B':
			beta = atof(optarg);
			break;
        case 'L':
            latitude = atof(optarg);
            break;
        case 'Z':
            zone = atof(optarg);
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

	/* who to connect to ... */
	seedlink = ((optind < argc) ? argv[optind++] : seedlink);
	gts = ((optind < argc) ? argv[optind++] : gts);

	/* report the program version */
	if (verbose)
		ms_log (0, "%s\n", program_version);

    tidal.zone = zone;
    tidal.latitude = latitude;

    /* load the base firfilter definitions if required */
    if ((nfirs > 0) && (firfilter_load(firfile) < 0)) {
        ms_log(1, "could not load fir filter file [%s]\n", firfile); exit(-1);
    }

    slconn->sladdr = seedlink;

	if (streamfile) {
		if (sl_read_streamlist (slconn, streamfile, selectors) < 0) {
		 ms_log(1, "unable to read streams [%s]\n", streamfile); exit(-1);
		}
	}
	else if (multiselect) {
		if (sl_parse_streamlist (slconn, multiselect, selectors) < 0) {
			ms_log(1, "unable to load streams [%s]\n", multiselect); exit(-1);
		}
	}
	else {
		if (sl_setuniparams (slconn, selectors, -1, 0) < 0) {
			ms_log(1, "unable to load selectors [%s]\n", selectors); exit(-1);
		}
	}

	/* recover any statefile info ... */
	if ((statefile) && (sl_recoverstate (slconn, statefile) < 0)) {
		ms_log (1, "unable to recover statefile [%s]\n", statefile);
	}

	/* loop with the connection manager */
	while (sl_collect (slconn, &slpack)) {
		if (sl_packettype(slpack) != SLDATA)
            continue;
		
		/* unpack record header and data samples */
		if ((rc = msr_unpack (slpack->msrecord, SLRECSIZE, &msr, 1, 1)) != MS_NOERROR) {
			sl_log(2, 0, "error parsing record\n");
		}

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

            /* and the fir filters themselves */
            stream->nfirs = nfirs;
            for (n = 0; n < stream->nfirs; n++) {
                if (firfilter_find(firnames[n], &stream->firs[n]) < 0) {
                    ms_log(1, "could not find fir filter [%s]\n", firnames[n]); exit(-1);
                }
            }

            stream->delay = 0LL;
            stream->samprate = msr->samprate;
            for (n = 0; n < stream->nfirs; n++) {
                stream->delay -= (hptime_t) MS_EPOCH2HPTIME(((stream->firs[n].minimum) ? 0.0 : ((double) stream->firs[n].length / 2.0 - 0.5) / stream->samprate));
                stream->samprate /= (double) stream->firs[n].decimate;
            }

            if (streams != NULL) {
                stream->next = streams;
            }

            streams = stream;
        }

        if (process_crex(msr, &tidal, stream, record_handler, NULL, &psamples, -1.0, verbose) < 0) {
            ms_log (1, "error processing mseed block\n"); break;
        }

        if ((verbose) && (psamples > 0))
             ms_log(0, "packed: %d samples\n", psamples);

		/* done with it */
		msr_free(&msr);

		/* Save intermediate state files */
		if (statefile && stateint) {
			if (++packetcnt >= stateint) {
				sl_savestate (slconn, statefile);
				packetcnt = 0;
			}
		}
	}

	/* closing down */
	if (verbose)
		ms_log (0, "stopping\n");

	if (statefile && slconn->terminate)
		(void) sl_savestate (slconn, statefile);

	if (slconn->link != -1)
		(void) sl_disconnect (slconn);

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

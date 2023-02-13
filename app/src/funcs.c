#define USE_TYPED_RSET
#include<registryFunction.h>
#include<epicsExport.h>
#include<dbCommon.h>
#include<dbDefs.h>
#include<dbAccess.h>
#include<recGbl.h>
#include<devSup.h>
#include<aSubRecord.h>
#include<menuFtype.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<strings.h>

/* We assume this is a power of two. */
#define EBSIZE 1024

struct eb_pvt {
    int cur;                            /* The most recent timestamp we have received. */
    double *data[EBSIZE];               /* Built arrays of data */
    epicsTimeStamp ts[EBSIZE];          /* The corresponding timestamps */
    int mask[EBSIZE];                   /* Which pieces of data do we have? */
    int off[21];                        /* Offsets into data for each input. */
    int all;                            /* Mask for all of the data */
    int cnt;                            /* How many inputs do we have? */
    int size;                           /* The total size of the data we are sending */
};

long eventBuildInit(struct aSubRecord *psub)
{
    int i = 0, cnt = 0;
    DBLINK      *inp = &psub->inpa;
    epicsEnum16 *ft = &psub->fta;
    epicsUInt32 *no = &psub->noa;
    int off[21];
    struct eb_pvt *pvt = NULL;
    bzero(off, sizeof(off));
    while (inp->type != CONSTANT) {
	off[i] = cnt;
	if (*ft != menuFtypeDOUBLE) {
	    printf("%s: FT%c is not double?!?\n", psub->name, 'A' + i);
	    return 0;
	}
	cnt += *no++;
	inp++;
	ft++;
	i++;
    }
    if (psub->ftva != menuFtypeDOUBLE) {
	printf("%s: FTVA is not double?!?\n", psub->name);
	return 0;
    }
    if (psub->nova < cnt) {
	printf("%s: NOVA must be at least %d (currently %d)\n", psub->name, cnt, psub->nova);
	return 0;
    }
    /* OK, looks good.  Let's initialize our private structure! */
    pvt = (struct eb_pvt *)malloc(sizeof(*pvt));
    pvt->cur = 0;
    pvt->data[0] = (double *)malloc(cnt * EBSIZE * sizeof(double));
    bzero(pvt->data[0], cnt * EBSIZE * sizeof(double));
    bzero(pvt->ts, sizeof(pvt->ts));
    bzero(pvt->mask, sizeof(pvt->mask));
    bcopy(off, pvt->off, sizeof(off));
    pvt->all = (1 << i) - 1;
    pvt->cnt = i;
    pvt->size = cnt;
    for (i = 1; i < EBSIZE; i++)
	pvt->data[i] = pvt->data[i-1] + cnt;
    psub->dpvt = pvt;
    return 0;
}

long eventBuild(struct aSubRecord *psub)
{
    struct eb_pvt *pvt = (struct eb_pvt *)psub->dpvt;
    DBLINK      *inp = &psub->inpa;
    epicsFloat64 **d = (epicsFloat64 **)&psub->a;
    epicsUInt32 *no = &psub->noa;
    int i;
    int lastidx = -1;
    if (!pvt)
	return 0; /* Some problem, just skip! */
    for (i = 0; i < pvt->cnt; i++, inp++, no++, d++) {
	epicsTimeStamp ts;
	int idx = pvt->cur;
	dbGetTimeStamp(inp, &ts);
	/*
	 * Search for this timestamp.  We're hoping it's either
	 * new or the most recent.
	 */
	if (pvt->ts[idx].secPastEpoch < ts.secPastEpoch ||
	    (pvt->ts[idx].secPastEpoch == ts.secPastEpoch &&
	     pvt->ts[idx].nsec < ts.nsec)) {
	    /* This data is more recent, make a new entry!! */
	    idx = (idx+1) & (EBSIZE-1);
	    pvt->ts[idx] = ts;
	    pvt->mask[idx] = 0;
	    pvt->cur = idx;
	} else if (pvt->ts[idx].secPastEpoch > ts.secPastEpoch ||
		   (pvt->ts[idx].secPastEpoch == ts.secPastEpoch &&
		    pvt->ts[idx].nsec > ts.nsec)) {
	    /* This data is older than the current. */
	    do {
		idx = idx ? (idx-1) : (EBSIZE-1);
	    } while ((pvt->ts[idx].secPastEpoch > ts.secPastEpoch ||
		      (pvt->ts[idx].secPastEpoch == ts.secPastEpoch &&
		       pvt->ts[idx].nsec > ts.nsec)) &&
		     idx != pvt->cur);
	    if (pvt->ts[idx].secPastEpoch != ts.secPastEpoch ||
		pvt->ts[idx].nsec != ts.nsec)
		continue; /* If we didn't find a match, give up! */
	}
	/* The third case is cur *is* our timestamp! */
	if (!(pvt->mask[idx] & (1<<i))) {
	    /* We don't have this data yet. */
	    bcopy(*d, pvt->data[idx] + pvt->off[i], (*no) * sizeof(double));
	    pvt->mask[idx] |= 1<<i;
	    if (pvt->mask[idx] == pvt->all) { /* We have everything! */
		if (lastidx == -1 || 
		    (pvt->ts[lastidx].secPastEpoch < ts.secPastEpoch ||
		     (pvt->ts[lastidx].secPastEpoch == ts.secPastEpoch &&
		      pvt->ts[lastidx].nsec < ts.nsec))) {
		    /* If this is our first completion, or it's more recent, send it! */
		    lastidx = idx; 
		}
	    }
	}
    }
    if (lastidx >= 0) {
	bcopy(pvt->data[lastidx], psub->vala, pvt->size * sizeof(double));
	bcopy(&pvt->ts[lastidx], &psub->time, sizeof(epicsTimeStamp));
	psub->neva = pvt->size;
    }
    return 0;
}

long descBuild(struct aSubRecord *psub)
{
    epicsInt8 **d = (epicsInt8 **)&psub->a;
    int remain = psub->nova - 1;
    epicsInt8 *o = (epicsInt8 *)psub->vala;

    while (**d) {
	int len = strlen(*d);
	if (remain > len) {
	    if (remain != psub->nova - 1) {
		*o++ = ',';
		remain--;
	    }
	    strcpy(o, *d);
	    o += len;
	    remain -= len;
	} else
	    break;
	d++;
    }
    *o = 0;
    psub->neva = o - (epicsInt8 *)psub->vala;
    return 0;
}

epicsRegisterFunction(eventBuildInit);
epicsRegisterFunction(eventBuild);
epicsRegisterFunction(descBuild);

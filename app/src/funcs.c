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

#define LCLS1_FID_MAX        0x1ffe0
#define LCLS1_FID_ROLL_LO    0x00200
#define LCLS1_FID_ROLL_HI    (LCLS1_FID_MAX-LCLS1_FID_ROLL_LO)
#define LCLS1_FID_ROLL(a,b)  ((b) < LCLS1_FID_ROLL_LO && (a) > LCLS1_FID_ROLL_HI)
#define LCLS1_FID_GT(a,b)    (LCLS1_FID_ROLL(b, a) || ((a) > (b) && !LCLS1_FID_ROLL(a, b)))

struct eb_pvt {
    double *data[LCLS1_FID_MAX];        /* Built arrays of data */
    epicsTimeStamp ts[LCLS1_FID_MAX];   /* The corresponding timestamps */
    int mask[LCLS1_FID_MAX];            /* Which pieces of data do we have? */
    int off[21];                  /* Offsets into data for each input. */
    int all;                      /* Mask for all of the data */
    int cnt;                      /* How many inputs do we have? */
    int size;                     /* The total size of the data we are sending */
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
    pvt->data[0] = (double *)malloc(cnt * LCLS1_FID_MAX * sizeof(double));
    bzero(pvt->data[0], cnt * LCLS1_FID_MAX * sizeof(double));
    bzero(pvt->ts, sizeof(pvt->ts));
    bzero(pvt->mask, sizeof(pvt->mask));
    bcopy(off, pvt->off, sizeof(off));
    pvt->all = (1 << i) - 1;
    pvt->cnt = i;
    pvt->size = cnt;
    for (i = 1; i < LCLS1_FID_MAX; i++)
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
    int lastfid = -1;
    if (!pvt)
	return 0; /* Some problem, just skip! */
    for (i = 0; i < pvt->cnt; i++, inp++, no++, d++) {
	epicsTimeStamp ts;
	int fid;
	dbGetTimeStamp(inp, &ts);
	fid = ts.nsec & 0x1ffff;
	if (pvt->ts[fid].nsec != ts.nsec || 
	    pvt->ts[fid].secPastEpoch != ts.secPastEpoch) {
	    /* A brand new timestamp! */
	    pvt->ts[fid] = ts;
	    pvt->mask[fid] = 0;
	}
	if (!(pvt->mask[fid] & (1<<i))) {
	    /* We don't have this data yet. */
	    bcopy(*d, pvt->data[fid] + pvt->off[i], (*no) * sizeof(double));
	    pvt->mask[fid] |= 1<<i;
	    if (pvt->mask[fid] == pvt->all) {
		if (lastfid == -1 || LCLS1_FID_GT(fid, lastfid)) {
		    lastfid = fid;
		}
	    }
	}
    }
    if (lastfid >= 0) {
	bcopy(pvt->data[lastfid], psub->vala, pvt->size * sizeof(double));
	bcopy(&pvt->ts[lastfid], &psub->time, sizeof(epicsTimeStamp));
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

/*------------------------------------------------------------------------------
* mbs.c : multi-baseline functions
*
*    Copyright (C) 2014 by Geospatial Information Authority of Japan,
*    All rights reserved.
*    
*    Released under the BSD, and GPL Licenses.
*
*
*  Original software: RTKLIB ver.2.4.2 p4
*
*    Copyright (C) 2007-2013 by T.Takasu, All rights reserved.
*
*
* references :
*
* history : 2015/01/08  1.0  new
*-----------------------------------------------------------------------------*/

/* mbs.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "rtklib.h"

#if defined LAPACK
#include <f2c.h>
#include <clapack.h>
#elif defined MKL11
#include "mkl_lapack.h"
//#include <mkl.h>
#else

#endif

/*** �萔��` ***/
#define MAX_FILEPATH	1024 /* �t���p�X���� */
#define FILENAME_SOL	"/AverageCalculation.out" /* ���όv�Z��p�t�@�C���� */
#define FILENAME_TRACE	"/mbs.trace"
#define FILENAME_POS	"/posest.txt"
#define FILENAME_SINEX	"/mbs.snx"
#define FILENAME_CRINEX	"/rinex.clc"

#define NINCSEMI   262144 /* inclimental number of semi data */

#define CLK_ERR_LIMIT 0.01  /* readobsrcv() */
//#define NEXOBS        256   /* readobsrcv() */

#define SIG_ROV       100.0
#define SIG_BASE      100.0
#define SIG_ZTD       0.3
#define SIG_GRA       0.01	/* setinitval() */
#define SIG_AMB       100.0
#define SIG_CLOCK_RCV 100.0	/* setinitval() */
#define SIG_CLOCK_SAT 100.0	/* setinitval() */
#define SIG_ISB       1.0	/* setinitval() */
#define SIG_CLOCK_SUM 0.001	/* base station clock sum (m) */

#define ERR_SAAS    0.3  /* saastamoinen model error std (m) */
#define REL_HUMI    0.7  /* relative humidity for saastamoinen model */

#define POS_THRES  6000000.0
#define RE_GRS80   6378137.0
#define FE_GRS80   (1.0/298.257222101)
#define PLH_TO_XYZ 0
#define XYZ_TO_PLH 1

#define NP(opt)     ((opt)->dynamics?9:3) /* number of pos solution */
#define IC(s,opt)   (NP(opt)+(s))         /* state index of clocks (s=0:gps,1:glo) */
#define IT(opt)     (IC(0,opt)+NSYS)      /* state index of tropos */

#define RFE_GRS80   298.257222101
#define PROGRAM_MANAGER "���y��ʏȍ��y�n���@"

#define CYC2M		0.19029367279836488

#define MAXLCVAR	40.0
#define TIMEDIGIT   2
#define TRENA		2	/* trace level for enable */
#define TRDIS		9	/* trace level for disable */
#define EP2TOD		0	/* epoch to true of date */
#define TOD2EP		1	/* true of date to epoch */
#define COV_THRES	1E-10
//#define OMCTHRES	100.0
#define OMCTHRES	150.0

#define IPOS(p,r)   ((p).ipos+(r)*3)        /* ignore opt.dynamics */
#define IZTD(p,r,t) ((p).iztd+(p).ntztd*(r)+(t))
#define IGNS(p,r,t) ((p).igra+(p).ntgra*(r)+(t))
#define IGEW(p,r,t) ((p).igra+(p).ntgra*(r)+(t)+(p).ngra/2)
#define IAMB(p,i)   ((p).iamb+(i))
#define ICLR(p,r,t) ((p).ng+(p).nep*(t)+(r))
#define ICLS(p,s,t) ((p).ng+(p).nep*((t)+1)-MAXSAT+(s))

extern int readerr(const char *, nav_t *);
extern int search_errtbl(const char *, const char *, int sys, unsigned char code, int type,
                         const nav_t *nav, double *a, double *b);

/*** static�֐���` ***/
static int mbssortpass( const void *, const void * );
static void mbsfree( mbs_t *mbs );
static int nextobsf2(const obs_t *, int *, int );
static double prectrop2(gtime_t, const double *, const double *, const prcopt_t *,
						const double *, double *, double *);

/*** �O���[�o���ϐ���` ***/
double *par_wk; /* readobsrcv()����setinitval()�܂ł̈ꎞ�̈�[MAXSAT][ne] */

int itr;        /* �C�^���[�V������ */
int iobs;       /* �ϑ��f�[�^�ʒu */
gtime_t ptime;  /* �ϑ��f�[�^���� */
double jpast;

int *ddtb = NULL, indd, inddm; /* �p�X�g�����e�[�u�� */
double Hdd[4], HwH[16]; /* ��d���A���r�M���C�e�B�v��s�� */

rtk_t *rtk = NULL;

char tstr0[64];
char satid[64];
FILE *fp_omc;

/* basename function for UNIX and Windows style path delimiter */
static char *basename_mp(const char *path)
{
	char *s=strrchr(path,'/');
	char *b=strrchr(path,'\\');
	if (s||b) return (s>b?s+1:b+1);
	return (char *)path;
}

#if 0
/* dirname function for UNIX and Windows style path delimiter */
static int dirname_mp(char *dirname, const char *path, size_t sz)
{
	size_t len=1;
	size_t n=strlen(path);
	char *s=strrchr(path,'/');
	char *b=strrchr(path,'\\');
	if (n>sz||sz<2) return 0;
	strcpy(dirname,".");
	if (s>path||b>path) {
		len=(s>b?s:b)-path;
		strncpy(dirname,path,len);
	}
	return len;
}
#endif

/*-----------------------------------------------------------------------------
* 	extract partial covariance matrix of dimension n
*  	from large covariance matrix of dimension lda.
*  	mat is rectangular form, cov is triangular form.
*----------------------------------------------------------------------------*/
static int pcov_get(const double *mat, int lda, int i, int n, double *cov)
{
	int j,k,l=0;
	if (i+n>=lda) return 0;		/* buffer overrun check */
	for (k=0;k<n;k++) for (j=0;j<n-k;j++) cov[l++]=mat[i+j+(i+j+k)*lda];
	return 1;
}

/*-----------------------------------------------------------------------------
* 	extract scattered partial covariance matrix of dimension n
*  	from large covariance matrix of dimension lda.
*  	mat is rectangular form, cov is triangular form.
*----------------------------------------------------------------------------*/
static int acov_get(const double *mat, int lda, const int *ih, int n, double *cov)
{
	int j,k,l=0;
	for (k=0;k<n;k++) if (ih[k]>=lda) return 0;		/* buffer overrun check */
	for (k=0;k<n;k++) for (j=0;j<n-k;j++) cov[l++]=mat[ih[j]+ih[j+k]*lda];
	return 1;
}

/* variance of double difference */
static double ddvar(const double *c)
{
	/* V=HPH'=[1,-1,-1,1]P[1,-1,-1,1]'=p11+p22+p33+p44+2*(-p12+p23-p34-p13-p24+p14) */
	return c[0]+c[1]+c[2]+c[3]+2.0*(-c[4]+c[5]-c[6]-c[7]-c[8]+c[9]);
}

/* variance of measurement error model */
static double varerr(const nav_t *nav, const sta_t *sta1, int sys, unsigned char code,
			  double el, int type, const prcopt_t *opt )
{
	double a = opt->err[1], b = opt->err[2];
	double c = type ? opt->eratio[0] : 1.0; /* type=0:phase,1:code */
	double f = sys == SYS_GLO ? EFACT_GLO : ( sys == SYS_SBS ? EFACT_SBS : EFACT_GPS );
	if( opt->ionoopt == IONOOPT_IFLC ) f *= 3.0;

	if (opt->errmodel==ERRMODEL_TABLE) {		/* update coefficients if error table exists */
		search_errtbl(sta1->rectype,sta1->antdes,sys,code,type,nav,&a,&b);
	}
	else {
		f*=c;
	}

	return (SQR(f)*(SQR(a)+SQR(b/sin(el))));
}

/* ���������͍\���̂̏����� */
mbs_t *mbsnew( gtime_t ts, gtime_t te, double ti, nav_t *navs, pcvs_t *pcvsr,
			   pcvs_t *pcvss, prcopt_t *popt ) {

	/* [1]���������͍\���̂̊m�� */
	int r;
	double span1;
	mbs_t *mbs;
	obs_t *obss;
	sbs_t *sbss;
	lex_t *lexs;
	sta_t *sta;
	trace( 3, "mbsnew:\n" );

	if( !(mbs = calloc( 1, sizeof( mbs_t )))) {
		showmsg( "mbsnew : alloc error mbs" );
		trace( 1, "mbsnew : alloc error mbs\n" );
		return NULL;
	}
	if( !(obss = calloc( 1, sizeof( obs_t )))) {
		showmsg( "mbsnew : alloc error obss" );
		trace( 1, "mbsnew : alloc error obss\n" );
		return NULL;
	}
	if( !(sbss = calloc( 1, sizeof( sbs_t )))) {
		showmsg( "mbsnew : alloc error sbss" );
		trace( 1, "mbsnew : alloc error sbss\n" );
		return NULL;
	}
	if( !(lexs = calloc( 1, sizeof( lex_t )))) {
		showmsg( "mbsnew : alloc error sbss" );
		trace( 1, "mbsnew : alloc error sbss\n" );
		return NULL;
	}
	if( !(sta = calloc( MAXRCV, sizeof( sta_t )))) {
		showmsg( "mbsnew : alloc error sbss" );
		trace( 1, "mbsnew : alloc error sbss\n" );
		return NULL;
	}

	/* [2]���������͍\���̂̏����� */
	span1 = timediff( te, ts ) + DTTOL;
	mbs->param.ne = (int)floor( span1 / ti ) + 1;
	mbs->param.ntztd = (int)floor( span1 / popt->mopt.tiztd ) + 1;
	mbs->param.ntgra = (int)floor( span1 / popt->mopt.tigra ) + 1;
	mbs->ts = ts;
	mbs->te = te;
	mbs->ti = ti;
	mbs->pcvss = pcvss;
	mbs->pcvsr = pcvsr;
	mbs->navs = navs;
	mbs->obss = obss;
	mbs->sbss = sbss;
	mbs->lexs = lexs;
	mbs->stas.sta = sta;
 //	mbs->nlitvl=popt->nlfcbitvl;
	mbs->nlitvl=popt->mopt.tifcb;
	for( r = 0; r < MAXRCV; r++ ) {
		mbs->stas.sta[r].dtr = zeros( 1, mbs->param.ne );
		mbs->stas.sta[r].ztd0 = zeros( 1, mbs->param.ntztd );
		mbs->stas.sta[r].ztdv = zeros( 1, mbs->param.ntztd );
	}
	mbs->neq = NULL;
	mbs->fcb = NULL;
	if( !(par_wk = zeros( 1, mbs->param.ne * MAXSAT ))) { /* �ꎞ�̈� */
		showmsg( "mbsnew : alloc error par_wk" );
		trace( 1, "mbsnew : alloc error par_wk\n" );
		return NULL;
	}

	return( mbs );
}

/* ���������͑O���� */
int mbspre( mbs_t *mbs, prcopt_t *popt, filopt_t *fopt, char **infile, int *index,
			int n, char *rov, char *base ) {

	/* [0]�ϐ��錾�� */
	int i, m;
	//char ifile[MAXINFILE][1024];
	//char *ifilep[MAXINFILE];
	char ifile[100][1024];
	char *ifilep[100];
	char cwrk[64];
	int idx[MAXINFILE];
	trace( 3, "mbspre:\n" );
	for (i=0;i<n;i++) trace(3,"infile[%d]=%s\n",i,infile[i]);

	/* [1]�Ǐ��O���� */
	if( !stainfopre( infile, index, n, rov, base, &mbs->stas, ifile, idx, &m )) {
		return( 1 );
	}

	/* [2]������̓��� */
	for( i = 0; i < n; i++ ) if( index[i] == 3 ) break;
	if( i >= n || !readnavclk( infile + i, n - i, mbs->navs )) {
		showmsg( "no nav data" );
		trace( 1, "no nav data\n" );
		return( 1 );
	}

	/* [4]�ϑ��f�[�^�̓��� */
	for( i = 0; i < m; i++ ) ifilep[i] = ifile[i] ;
	if( !readobs( mbs, popt, fopt, ifilep, idx, m )) {
		return( 1 );
	}

	/* [5]PCV�f�[�^�̐ݒ� */
	if( popt->mode != PMODE_SINGLE ) {
		setpcv2( mbs->obss->n > 0 ? mbs->obss->data[0].time : timeget(), popt,
				 mbs->navs, mbs->pcvss, mbs->pcvsr, &mbs->stas );
	}

    /* 1/4�T�C�N���V�t�g */
	for (i=0;i<mbs->stas.nsta;i++) {
//		setL2Csft(popt->phasshft, popt->rectype[i], &navs.sfts, mbs->stas.sta+i);
		setL2Csft(popt->phasshft, mbs->stas.sta[i].rectype, &navs.sfts, mbs->stas.sta+i);
	}
	/* receiver DCB */
	for (i=0;i<mbs->stas.nsta;i++) {
	//	setdcb(&navs, popt->rectype[i], mbs->stas.sta+i);
		setdcb(&navs, mbs->stas.sta+i);
	}
	/* ISB */
	for (i=0;i<mbs->stas.nsta;i++) {
	//	setisb(&navs, popt->rectype[i], mbs->stas.sta+i);
//		setisb(&navs, mbs->stas.sta[i].rectype, mbs->stas.sta+i);
		setisb(&navs, mbs->stas.sta[i].rectype, NULL, mbs->stas.sta+i,NULL);
//		setisb(2, &navs, mbs->stas.sta[i].rectype, NULL, mbs->stas.sta+i,NULL);
	}

	/* [6]����p�����[�^�̃Z�b�g�A�b�v */
	setparam( mbs, popt );

	/* [7]�����l�ݒ� */
	setinitval( mbs, popt );

	time2str(timeget(),cwrk,TIMEDIGIT);
	trace(1,"%s mbspre end\n",cwrk);

	return( 0 );
}

/* ����G�|�b�N�ϑ��f�[�^���o */
int inputobs2( obs_t *obss, obsd_t *obs, int ista, int *iobss ) {
	int i, nw, n = 0;
	trace( 3, "inputobs2: rcv=%d iobs=%d\n", ista, *iobss );

	if(( nw = nextobsf2( obss, iobss, ista ))<= 0 ) return( -1 );
	for( i = 0; i < nw && n < MAXOBS; i++ ) obs[ n++ ] = obss->data[ *iobss + i ];
	*iobss += n;
	return( n );
}

/* calculate Melbourne-Wubbena and ionosphere free linear combination -----------------------------
*	args :   popt		I	process option
*	         obs		I	observation data
*	         nav		I	navigation data
*	         mw			O	Melbourne-Wubbena linear combination [cycle]
*	         mwv		O	MW variance [cycle^2]
*	         lcamb		O	LC ambiguitye [m]
*	         code[]		O	observation code
*	return : none
*	note   : receiver DCB and phase cycle shift correction is not implemented
*----------------------------------------------------------------------------*/
void mwmeas( const prcopt_t *popt, obsd_t *obs, nav_t *nav, const sta_t *sta, double *mw,
			 double *mwv, double *lcamb, unsigned char *code) {

	/* [1]�ϐ���` */
	int i = 0, j = 1;
	double f1, f2, *lam, lamw, sig, sigp;
	double Pi,Pj,Li,Lj;		/* DCB or phase cycle shift corrected obs [m] */
	double Pw, Pv, Lw, Lv;
	double Lc, Pc;
    double lc2p;
    double rP1_C1, rP2_C2;
    int sys;
    int s_code;
    double ydifIdb[NFREQ][2] = {0};
	trace(4,"mwmeas:\n");

	lam = nav->lam[ obs->sat - 1 ];
	f1 = CLIGHT / lam[i];
	f2 = CLIGHT / lam[j];
	lamw = CLIGHT / ( f1 - f2 );

    Pi = obs->P[i];
	Pj = obs->P[j];
    Li = obs->L[i];
	Lj = obs->L[j];

	/* satellite DCB correction of pseudo range */
	Pi += (obs->code[0]==CODE_L1C ? nav->cbias[obs->sat-1][1] : 0.0);
	Pj += (obs->code[1]==CODE_L2S || obs->code[1]==CODE_L2L ||
					  obs->code[1]==CODE_L2X ? nav->cbias[obs->sat-1][2] : 0.0);
	/* receiver DCB correction */
    sys=satsys(obs->sat,NULL);
    s_code = sysind((char)sys);
    if(obs->code[0]==CODE_L1C) {
        if     (i==0) Pi += sta->dcb[s_code-1][1];
        else if(j==0) Pj += sta->dcb[s_code-1][1];
    }
	if(isL2C(obs->code[1]) && (GL2OPT_TABLE==popt->gl2bias)) {
        if     (i==1) Pi += sta->dcb[s_code-1][2];
        else if(j==1) Pj += sta->dcb[s_code-1][2];
    }

    /* phase cycle shift correction */
	if(isL2C(obs->code[i])) Li+=chk_L2Csft(obs, popt, sta);
	if(isL2C(obs->code[j])) Lj+=chk_L2Csft(obs, popt, sta);

    /* ISB */       //  corrmeas�Ŏ��{
    chk_isb(sys, popt, sta, ydifIdb);
//	chk_isb(2, sys, popt, sta, ydifIdb);
//    if(i==0) Pi+=ydifIdb[0][0];


	/* [2]�ϑ��덷���f���ɂ��W���΍� */
	sig = sqrt( SQR( popt->err[1] ) + SQR( popt->err[2] / sin( obs->azel[1] )));
	sigp = sig * popt->eratio[0];

	/* [3]Pw�̌v�Z */
	Pw = ( f1 * Pi + f2 * Pj ) / ( f1 + f2 );
	Pv = ( f1 * f1 + f2 * f2 ) / ( f1 + f2 ) / ( f1 + f2 ) * sigp * sigp;

	/* [4]Lw�̌v�Z */
	Lw = CLIGHT * ( Li - Lj ) / ( f1 - f2 );
	Lv = ( f1 * f1 + f2 * f2 ) / ( f1 - f2 ) / ( f1 - f2 ) * sig * sig;

	/* [5]MW�̌v�Z */
	*mw = ( Lw - Pw ) / lamw;
	*mwv = ( Lv + Pv ) / lamw / lamw;

	/* [6]Lc-Pc�̌v�Z */
	Lc = ( f1 * f1 * Li * lam[i] - f2 * f2 * Lj * lam[j] );
	Pc = ( f1 * f1 * Pi - f2 * f2 * Pj );
	*lcamb = ( Lc - Pc ) / ( f1 * f1 - f2 * f2 );

	/* [7]�R�[�h���ʂ̐ݒ� */
	code[0] = obs->code[i];
	code[1] = obs->code[j];

	return;
}

/* �p�X���̒ǉ� */
int addpass( pass_t *pass, gtime_t ts, int sat, int sta, unsigned char *code ) {
	passd_t *passdw;
	trace( 3, "addpass:\n" );

	/* passd�\���̂̑��� */
	if( pass->nmax <= pass->n ) {
		if( pass->nmax <= 0 ) {
			pass->nmax = NUM_PASSD_ADD;
		}
		else {
			pass->nmax *= 2;
		}
		if (( passdw = (passd_t *)realloc( pass->data, sizeof( passd_t ) * pass->nmax ))
			 == (passd_t *)NULL ) {
			free( pass->data );
			pass->data = (passd_t *)NULL;
			pass->n = pass->nmax = 0;
			return( -1 );
		}
		pass->data = passdw;
	}

	/* �ǉ��p�X���̊i�[ */
	pass->data[pass->n].ts = ts;
	pass->data[pass->n].te = ts;
	pass->data[pass->n].sat = sat;
	pass->data[pass->n].sta = sta;
	pass->data[pass->n].nd = 0;
	pass->data[pass->n].nout = 0;
	pass->data[pass->n].exc = 0;
	pass->data[pass->n].code[0] = code[0];
	pass->data[pass->n].code[1] = code[1];
	pass->data[pass->n].mw = 0.0;
	pass->data[pass->n].mwv = 0.0;
	pass->data[pass->n].wsum = 0.0;
	pass->data[pass->n].LC = 0.0;
	pass->data[pass->n].lcamb = 0.0;
	pass->data[pass->n].lcambv = 0.0;
	pass->n++;

	return( pass->n - 1 );
}

/* �p�X���̍X�V */
void setpass( passd_t *pass, gtime_t te, double mw, double lcamb, double el ) {

	/* [0]�ϐ���` */
	passd_t tmp;
	double wn;
	trace( 3, "setpass:\n" );
	trace( 5, "mw=%e,lcamb=%e,el=%f\n", mw, lcamb, el );
	trace( 5, "(IN)pass->mw=%e,mwv=%e,wsum=%f,nd=%d\n",
			pass->mw, pass->mwv, pass->wsum, pass->nd );

	/* [1]MW�̕��ςƃЌv�Z */
	tmp = *pass;
	wn = el >= 30.0 * D2R ? 1.0 : 2.0 * sin( el );
	pass->nd++;
	pass->wsum += wn;
//	pass->mw = ( tmp.wsum * tmp.mw + wn * mw) / pass->wsum;
	pass->mw=tmp.mw+wn*(mw-tmp.mw)/pass->wsum;
	if( pass->nd > 1 ) {
		//pass->mwv = ( wn * mw * mw
		//			+ ( pass->nd - 2 ) * tmp.wsum * tmp.mwv
		//			- pass->wsum * pass->mw * pass->mw
		//			+ tmp.wsum * tmp.mw * tmp.mw )
		//			/ ( pass->nd -1 ) / pass->wsum;
		pass->mwv=(tmp.wsum*tmp.mwv+wn*(mw-pass->mw)*(mw-tmp.mw))/pass->wsum;
	}
	else {
		pass->mwv = 0.0;
	}

	/* [2]Lc-Pc�̕��ςƃЌv�Z */
//	pass->lcamb = ( tmp.nd * tmp.lcamb + lcamb ) / pass->nd;
	pass->lcamb=tmp.lcamb+wn*(lcamb-tmp.lcamb)/pass->wsum;
	if( pass->nd > 1 ) {
		//pass->lcambv = ( SQR( lcamb ) + tmp.nd * ( tmp.lcambv + SQR( tmp.lcamb )))
		//				/ pass->nd - SQR( pass->lcamb );
        pass->lcambv=(tmp.wsum*tmp.lcambv+wn*(lcamb-pass->lcamb)*(lcamb-tmp.lcamb))/pass->wsum;
	}
	else {
		pass->lcambv = 0.0;
	}
	trace( 5, "(OUT)pass->mw=%e,mwv=%e,wsum=%f,nd=%d,lcamb=%e,lcambv=%e\n",
			pass->mw, pass->mwv, pass->wsum, pass->nd, pass->lcamb, pass->lcambv );

	/* [3]���̑��̃����o�ϐ��̍X�V */
	pass->te = te;

	return;
}

/* �p�X���̃\�[�g */
void sortpass( pass_t *pass ) {
	trace( 3, "sortpass:\n" );

	/* ��->�q��->�����Ń\�[�g */
	qsort( pass->data, pass->n, sizeof( passd_t ), mbssortpass );

	return;
}

/* �p�X���� */
int searchpass( pass_t *pass, int r, int s, gtime_t tt ) {
    /* argument : r=0..n, s=1..n */
    /* pass_t   : r=0..n, s=1..n */

	/* [0]�ϐ���` */
	int i,j,k;
	trace( 3, "searchpass:\n" );

	/* [1]�ǋ�Ԍ��� */
	i=0;
	while( i < pass->n && pass->data[i].sta != r ) i++;
	j=i;
    while( j < pass->n && pass->data[j].sta == r ) j++;

	/* [2]�q����Ԍ��� */
    k=j;
	while( i < k && pass->data[i].sat != s ) i++;
    j=i;
    while( j < k && pass->data[j].sat == s ) j++;

	/* [3]�������� */
	while( i < j ) {
        if( timediff( tt, pass->data[i].ts ) >= -DTTOL &&
            timediff( tt, pass->data[i].te ) <= DTTOL ) return i;
        i++;
	}

	/* [4]�I������ */
	return -1;
}

/* delete short pass */
void pass_select(pass_t *pass, double thres)
{
    int s,d;
	trace(3,"pass_select:\n");

    for (d=s=0;s<pass->n;s++) {
        if (timediff(pass->data[s].te,pass->data[s].ts)<thres) continue;
        if (pass->data[s].lcambv>MAXLCVAR) continue;
        memcpy(pass->data+d,pass->data+s,sizeof(passd_t));
        d++;
    }
    pass->n=d;
    trace(1,"\nnumber of pass reduced %d to %d\n",s,d);
}

/* �p�����[�^�̃Z�b�g�A�b�v */
void setparam( mbs_t *mbs, prcopt_t *popt ) {
	int s, i, nsys = 0;
	trace( 3, "setparam:\n" );

	/* [1]�p�����[�^�\���̂̐ݒ� */
	mbs->param.ipos = 0;
	mbs->param.npos = 3 * mbs->stas.nsta;
	mbs->param.iztd = mbs->param.ipos + mbs->param.npos;
	mbs->param.nztd = popt->tropopt < TROPOPT_EST ? 0 : mbs->param.ntztd *
					  mbs->stas.nsta;
	mbs->param.igra = mbs->param.iztd + mbs->param.nztd;
	mbs->param.ngra = popt->tropopt < TROPOPT_ESTG ? 0 : mbs->param.ntgra * 2 *
					  mbs->stas.nsta;

	s = popt->navsys;
	s >>= 2;
	for( i = 0; i < 30; i++ ) {
		nsys += ( s & 1 );
		s >>= 1;
	}
	mbs->nsatsys = nsys + 1;
	mbs->param.iisbP = mbs->param.igra + mbs->param.ngra;
	mbs->param.nisbP = ((popt->isb != ISBOPT_EST) && (popt->isb != ISBOPT_EST_P)) ? 0 : nsys * mbs->stas.nsta;
	mbs->param.iisbL = mbs->param.iisbP + mbs->param.nisbP;
	mbs->param.nisbL = ((popt->isb != ISBOPT_EST) && (popt->isb != ISBOPT_EST_L)) ? 0 : nsys * mbs->stas.nsta;

	mbs->param.il2b = mbs->param.iisbL + mbs->param.nisbL;
	mbs->param.nl2b = (popt->gl2bias != GL2OPT_EST) ? 0 : mbs->stas.nsta;

	mbs->param.iamb = mbs->param.il2b + mbs->param.nl2b;
	mbs->param.namb = mbs->pass.n;
	mbs->param.ng = mbs->param.iamb + mbs->param.namb;
	mbs->param.nep = mbs->stas.nsta + ( popt->mopt.estsatclk > 0 ? MAXSAT : 0 );
	mbs->param.na   = mbs->param.ng + mbs->param.nep * mbs->param.ne;

	/* [2]�z��̃A���P�[�g */
	mbs->param.par = zeros( mbs->param.na, 1 );
	mbs->param.sig = zeros( mbs->param.na, 1 );
	mbs->param.par0 = zeros( mbs->param.na, 1 );
	mbs->param.sig0 = zeros( mbs->param.na, 1 );

	mbs->param.stncov = zeros( mbs->stas.nsta * 3 * 3, 1 );
	mbs->param.stncov0 = zeros( mbs->stas.nsta * 3 * 3, 1 );

	return;
}

/* �����l�ݒ� */
void setinitval( mbs_t *mbs, prcopt_t *popt ) {
	int i, r; /* loop parameters */
	int ap, ac; /* address of pos and sig/cov */
	double sig1,sig2,cs;
	size_t isz;
	trace( 3, "setinitval:\n" );

	/* [1]�ǈʒu�̐ݒ� */
	ap = mbs->param.ipos;
	for( r = 0; r < mbs->stas.nsta; r++ ) {
		/* position */
		for( i = 0; i < 3; i++ ) mbs->param.par[ap++] = mbs->stas.sta[r].pos[i];

		/* covariance */
		ac = r * 9;
		mbs->param.stncov[ac+0] = mbs->stas.sta[r].cov[0];
		mbs->param.stncov[ac+1] = mbs->stas.sta[r].cov[3];
		mbs->param.stncov[ac+2] = mbs->stas.sta[r].cov[5];
		mbs->param.stncov[ac+3] = mbs->stas.sta[r].cov[3];
		mbs->param.stncov[ac+4] = mbs->stas.sta[r].cov[1];
		mbs->param.stncov[ac+5] = mbs->stas.sta[r].cov[4];
		mbs->param.stncov[ac+6] = mbs->stas.sta[r].cov[5];
		mbs->param.stncov[ac+7] = mbs->stas.sta[r].cov[4];
		mbs->param.stncov[ac+8] = mbs->stas.sta[r].cov[2];
	}

	/* [2]�Η����x���̐ݒ� */
	/* zenith tropospheric delay */
	sig1 = popt->std[2]==0.0 ? SIG_ZTD : popt->std[2];
	if( popt->tropopt >= TROPOPT_EST ) {
		ap = mbs->param.iztd;
		for( r = 0; r < mbs->stas.nsta; r++ ) {
			for( i = 0; i < mbs->param.ntztd; i++ ) {
				mbs->param.par[ap] = mbs->stas.sta[r].ztd0[i];
				mbs->param.sig[ap++] = sig1;
			}
		}
	}

	/* tropospheric gradient */
	sig1 = popt->std[3]==0.0 ? SIG_GRA : popt->std[3];
	if( popt->tropopt >= TROPOPT_ESTG ) {
		ap = mbs->param.igra;
		for( i = 0; i < mbs->stas.nsta * mbs->param.ntgra * 2; i++ ) {
			mbs->param.par[ap] = 0.0;
			mbs->param.sig[ap++] = sig1;
		}
	}

	/* [3]�[�������V�X�e���ԃo�C�A�X�̐ݒ� */
	sig1 = popt->std[6]==0.0 ? SIG_ISB : popt->std[6];
//	sig1 = SIG_ISB;
//	if( popt->navsys & SYS_GLO ) {
		ap = mbs->param.iisbP;
		for( i = 0; i < mbs->param.nisbP; i++ ) {
			mbs->param.par[ap] = 0.0;
			mbs->param.sig[ap++] = sig1;
		}
//	}
	/* �����g�ʑ��V�X�e���ԃo�C�A�X�̐ݒ� */
	sig1 = popt->std[6]==0.0 ? SIG_ISB : popt->std[6];
//	if( popt->navsys & SYS_GLO ) {
		ap = mbs->param.iisbL;
		for( i = 0; i < mbs->param.nisbL; i++ ) {
			mbs->param.par[ap] = 0.0;
			mbs->param.sig[ap++] = sig1;
		}
//	}
	/* L2�o�C�A�X�̐ݒ� */
	sig1 = popt->std[6]==0.0 ? SIG_ISB : popt->std[6];
	ap = mbs->param.il2b;
	for( i = 0; i < mbs->param.nl2b; i++ ) {
		mbs->param.par[ap] = 0.0;
		mbs->param.sig[ap++] = sig1;
	}

	ap = mbs->param.ng;
	sig1 = popt->std[4]==0.0 ? SIG_CLOCK_RCV : popt->std[4];
	sig2 = popt->std[5]==0.0 ? SIG_CLOCK_SAT : popt->std[5];
	for( i = 0; i < mbs->param.ne; i++ ) {

		/* [4]�ǃN���b�N�o�C�A�X�̐ݒ� */
		for( r = 0; r < mbs->stas.nsta; r++ ) {
			mbs->param.par[ap] = CLIGHT * mbs->stas.sta[r].dtr[i];
			mbs->param.sig[ap++] = sig1;
		}

		/* [5]�q���N���b�N�o�C�A�X�̐ݒ� */
		if( popt->mopt.estsatclk ) {
			for( r = 0; r < MAXSAT; r++ ) {
				mbs->param.par[ap] = CLIGHT * par_wk[ r * mbs->param.ne + i ];
				mbs->param.sig[ap++] = sig2;
			}
		}

        /* adjust clock offset to reference clock sum time system */
		if (popt->mopt.estsatclk) {
			/* calculate reference clock sum */
			cs=0.0;
			for (r=0;r<mbs->stas.nsta;r++) {
				if (mbs->stas.sta[r].id==2) cs+=mbs->param.par[ICLR(mbs->param,r,i)];
			}
			/* subtract reference clock sum from clock parameters */
			for (r=0;r<mbs->stas.nsta;r++) mbs->param.par[ICLR(mbs->param,r,i)]-=cs;
			for (r=0;r<MAXSAT        ;r++) mbs->param.par[ICLS(mbs->param,r,i)]-=cs;
		}

	}
	free( par_wk );

	/* �A���r�M���C�e�B�̐ݒ� */
	ap = mbs->param.iamb;
	sig1  = popt->std[0]==0.0 ? SIG_AMB : popt->std[0];
	for( i = 0; i < mbs->param.namb; i++ ) {
			mbs->param.par[ap] = mbs->pass.data[i].lcamb;
			mbs->param.sig[ap++] = sig1;
			//trace(2,"PASS,%3d,%d,%5.0f,%3d,%2d,%7.2f,%7.2f\n",i,mbs->pass.data[i].ts.time,
			//        timediff(mbs->pass.data[i].te,mbs->pass.data[i].ts),
			//        mbs->pass.data[i].sat,mbs->pass.data[i].sta,
			//        mbs->pass.data[i].mw,mbs->pass.data[i].lcamb);
			trace(2,"PASS,%3d,%d,%5.0f,%3d,%2d,%7.2f,%7.2f,%7.2f,%7.2f\n",
					i,mbs->pass.data[i].ts.time,
			        timediff(mbs->pass.data[i].te,mbs->pass.data[i].ts),
			        mbs->pass.data[i].sat,mbs->pass.data[i].sta,
			        mbs->pass.data[i].mw,sqrt(mbs->pass.data[i].mwv),
					mbs->pass.data[i].lcamb,sqrt(mbs->pass.data[i].lcambv));
	}

	/* [6]�S�̃R�s�[���� */
	isz = sizeof( double ) * mbs->param.na;
	memcpy( mbs->param.par0, mbs->param.par, isz );
	memcpy( mbs->param.sig0, mbs->param.sig, isz );
	memcpy( mbs->param.stncov0, mbs->param.stncov, sizeof( double ) *
			mbs->stas.nsta * 9 );

	return;
}

/* PCV�f�[�^�̐ݒ� */
void setpcv2( gtime_t time, prcopt_t *popt, nav_t *nav, const pcvs_t *pcvs,
			  const pcvs_t *pcvr, stas_t *stas) {
	pcv_t *pcv;
	double del[3];
	int i,j;
	char id[64];
	trace( 3, "setpcv2:\n" );

	/* set satellite antenna parameters */
	for (i=0;i<MAXSAT;i++) {
		if (!(satsys(i+1,NULL)&popt->navsys)) continue;
		if (!(pcv=searchpcv(i+1,"",time,pcvs))) {
			satno2id(i+1,id);
			trace(2,"no satellite antenna pcv: %s\n",id);
			continue;
		}
		nav->pcvs[i]=*pcv;
	}

	/* ���n���W�n�ł̋Ǎ��W->APR�x�N�g�� */
	for( i = 0; i < stas->nsta; i++ ) {
		if( stas->sta[i].deltype == 1 ) { /* xyz */
			if( norm( stas->sta[i].pos, 3 ) > POS_THRES ) {
				ecef2enu( stas->sta[i].plh, stas->sta[i].del, del );
				for( j = 0; j < 3; j++ ) stas->sta[i].antdel[j] = del[j];
			}
		}
		else {
			for( j = 0; j < 3; j++ ) {
				stas->sta[i].antdel[j] = stas->sta[i].del[j];
			}
		}

		/* PCO/PCV�f�[�^ */
		if( !( pcv = searchpcv( 0, stas->sta[i].antdes, time, pcvr ))) {
			trace(2,"no receiver antenna pcv: %s\n",stas->sta[i].antdes );
			*stas->sta[i].antdes = '\0';
			continue;
		}
		strcpy( stas->sta[i].antdes, pcv->type );
		stas->sta[i].pcvr = *pcv;
	}

	return;
}

/*
 * �֐����F mbsmain
 * �T�v  �F ���������͎又��
 *   IN  �F mbs_t *mbs ���������͍\����
 *   OUT �F int �X�e�[�^�X(0:ok,0>:error,1:aborted)
 */
static int mbsmain(mbs_t *mbs, prcopt_t *popt, solopt_t *sopt, const char* outdir, char **infile, int n) {
//	gtime_t gt;
	char cwrk[256];
//	int i,j;
	int r,s;
	char omcf[MAX_FILEPATH];
    char snxfile[MAX_FILEPATH];

	/* 1 �ϐ��錾�������� */
	int iret, iep;
	trace( 3, "mbsmain:\n" );

    if( !rtk ) {
        rtk = calloc( 1, sizeof( rtk_t ));
        rtkinit( rtk, popt );
    }

	if( !(mbs->neq = calloc( 1, sizeof( neqmat_t )))) {
		showmsg( "alloc error : neq" );
		trace( 1, "alloc error : neq\n" );
		return 1;
	}
	if( !(mbs->param.dx = calloc( mbs->param.na, sizeof( double )))) {
		showmsg( "alloc error : dx" );
		trace( 1, "alloc error : dx\n" );
		return 1;
	}
	if( !(mbs->param.pg = calloc( mbs->param.ng, sizeof( double )))) {
		showmsg( "alloc error : pg" );
		trace( 1, "alloc error : pg\n" );
		return 1;
	}
	if( !(mbs->param.pe = calloc( mbs->param.ne * mbs->param.nep, sizeof( double )))) {
		showmsg( "alloc error : pe" );
		trace( 1, "alloc error : pe\n" );
		return 1;
	}
	if( !(mbs->neq->pgmat = calloc( mbs->param.ng * mbs->param.ng, sizeof( double )))) {
		showmsg( "alloc error : pgmat" );
		trace( 1, "alloc error : pgmat\n" );
		return 1;
	}
	if( !(mbs->neq->pemat = calloc( mbs->param.ne * mbs->param.nep * mbs->param.nep,
		  sizeof( double )))) {
		showmsg( "alloc error : pemat" );
		trace( 1, "alloc error : pemat\n" );
		return 1;
	}
	mbs->neq->neeq = mbs->neq->ngeq = mbs->neq->ngeeq = NULL;

	/* 2 ���K�������s�񏀔� */
	iret = init_nomeqmat(mbs);
	if (iret > 0) {
		return 1;
	}

	/* 3 �C�^���[�V�����J�n */
	jpast = 0;
	itr = 0;
	while (1) {

		showmsg("processing iteration %d",itr+1);

	    strcpy(omcf,outdir);
	    sprintf(cwrk,"/omcf_%03d.csv",itr);
	    strcat(omcf,cwrk);
		fp_omc=fopen(omcf,"w");
   //		rtkopencrinex(popt, sopt, "crinex.rnx");

		if (fp_omc) fprintf(fp_omc,
	    		"date_and_time,rcv,sat,rej,resc[m],resp[m],az[deg.],el[deg.],"
	    		"clock_rcv[m],clock_sat[m],trop[m],bias[m],antr[m],ants[m],phw[m]\n");

	    /* clear phase wind-up table */
	    for (r=0;r<MAXRCV;r++) for (s=0;s<MAXSAT;s++) mbs->stas.sta[r].phw[s]=0.0;

		/* 4 �O���[�o���p�����[�^�̍S�������ݒ� */
		iret = init_globalparam(mbs,popt);
		if (iret > 0) {
			showmsg( "init_globalparam error" );
			trace( 1, "init_globalparam error\n" );
			return 1;
		}

		/* �G�|�b�N���[�v�G�|�b�N�P�ʂŏ��������[�v����B*/
		for (iep = 0; iep < mbs->param.ne; iep++) {

			/* 5 ���K�������̃G�|�b�N�p�����[�^�������� */
			iret = gen_neqepochparam(mbs);
			if (iret > 0) {
				showmsg( "gen_neqepochparam error" );
				trace( 1, "gen_neqepochparam error\n" );
				return 1;
			}

			/* 6 �G�|�b�N�p�����[�^�̍S�������ݒ� */
			if (set_epochparaconst(mbs, popt, iep)) return 1;

			/* 7 �ϑ��X�V */
			iret = obs_update(mbs,popt, sopt,iep);
			if (iret > 0) {
				showmsg( "obs_update error" );
				trace( 1, "obs_update error\n" );
				return 1;
			}

			/* 8 �G�|�b�N�p�����[�^�̏����A�ۑ� */
			iret = del_epochparam(mbs,popt,mbs->param.ng,mbs->param.nep,iep);
			if (iret > 0) {
				showmsg( "del_epochparam error" );
				trace( 1, "del_epochparam error\n" );
				return 1;
			}

		} /* �G�|�b�N���[�v�G���h */

		fclose(fp_omc);
	//	rtkclosecrinex();


	    time2str(timeget(),cwrk,TIMEDIGIT);
		trace(1,"%s [%d] obs stack end\n",cwrk,itr+1);

	    /* 9 �O���[�o���p�����[�^���� */
		iret = est_globalparam(mbs);
		if (iret > 0) {
			showmsg( "est_globalparam error" );
			trace( 1, "est_globalparam error\n" );
			return 1;
		}

        time2str(timeget(),cwrk,TIMEDIGIT);
        trace(1,"%s [%d] est global param end\n",cwrk,itr+1);

		/* 10 ��d��LC�A���r�M���C�e�B�ɂ��S�����̐��� */
		iret = est_ddlcambi(mbs,popt);
		if (iret > 0) {
			showmsg( "est_ddlcambi error" );
			trace( 1, "est_ddlcambi error\n" );
			return 1;
		}

		time2str(timeget(),cwrk,TIMEDIGIT);
        trace(1,"%s [%d] fix ambiguities end\n",cwrk,itr+1);

		/* 11 �G�|�b�N�p�����[�^���� */
		iret = est_epochparam(mbs,popt);
		if (iret > 0) {
			showmsg( "est_epochparam error" );
			trace( 1, "est_epochparam error\n" );
			return 1;
		}

		/* 12 ����p�����[�^�X�V */
		update_param( mbs );

		/* 13 �������� */
		iret = check_itr_conv(mbs,popt);
		if( iret == 0 ) continue;	/* next iteration */
		else if( iret == 1 ) {
			trace(2,"iteration converged\n");
			break;
		}
		else if( iret == 2 ) {
			trace(2,"iteration aborted by condition\n");
			break;
		}

	} /* 14 �C�^���[�V�����I�� */

	/* 15 �I������ */
	return 0;
}

/*
 * �֐����F init_nomeqmat
 * �T�v  �F ���������͎又��
 *   IN  �F mbs_t *mbs ���������͍\����
 *   OUT �F int �X�e�[�^�X (0:ok,0>:error,1:aborted)
 */
int init_nomeqmat(mbs_t *mbs) {

	int n;
	double *p_neq = NULL;
	double *p_beq = NULL;
	trace( 3, "init_nomeqmat:\n" );

	/* ����p�����[�^���擾 */
	n=mbs->param.ng;

	if( !mbs->neq->ngeq ) p_neq = (double *)malloc(sizeof(double)*n*n);
	if( !p_neq ) {
		showmsg( "alloc error : neq.ngeq" );
		trace( 1, "alloc error : neq.ngeq\n" );
		return 1;
	}

	if( !mbs->neq->bgeq ) p_beq = (double *)malloc(sizeof(double)*n);
	if( !p_beq) {
		showmsg( "alloc error : neq.bgeq" );
		trace( 1, "alloc error : neq.bgeq\n" );
		return 1;
	}
	mbs->neq->ngeq = p_neq;
	mbs->neq->bgeq = p_beq;

	return 0;
}

/*
 * �֐����F init_globalparam
 * �T�v  �F �O���[�o���p�����[�^�S�������ݒ菈��
 *   I/O  �F mbs_t *mbs ���������͍\����
 */
int init_globalparam(mbs_t *mbs,prcopt_t *popt) {

	int i,j;
	int ret = 0;
	int n=0;
	trace( 3, "init_globalparam:\n" );

	/* ���K�������s��̏����� */
	n = mbs->param.ng;
	for(i=0;i<n;i++){
		for(j=0;j<n;j++){
			mbs->neq->ngeq[i*n + j] = 0.0;
		}
		mbs->neq->bgeq[i] = 0.0;
	}
	mbs->neq->j = 0.0;

	/* �ǈʒu�̍S�������ݒ� */
	ret = set_glopstnposi_const(mbs);

	/* ���̑��̃O���[�o���p�����[�^�S�������ݒ� */
	set_glopother_const(mbs);

	/* ���ԑ��ւ̋����U�ݒ� */
	if((ret = set_rwpar_const(mbs,popt)) == 1 ) {
		showmsg( "set_rwpar_const skip" );
		trace( 2, "set_rwpar_const skip\n" );
	}

	return ret;
}

/*
 * �֐����F set_glopstnposi_const
 * �T�v  �F �ǈʒu�S�������ݒ菈��
 *   IN  �F mbs_t *mbs ���������͍\����
 *   OUT �F int �X�e�[�^�X (0:ok, 0>:error, 1:aborted)
 */
int set_glopstnposi_const(mbs_t *mbs) {

	/* [1] �ϐ��錾�������� */
	double workvg[9];
	double workx[3];
	double workb[3];
	double workj = 0.0;
	int    i, j, ppos;
	trace( 3, "set_glopstnposi_const:\n" );

	/* [2] �����l�p�����[�^ */
	if( !itr ) {
		/* �O�񐄒�p�����[�^�l */
		memcpy(mbs->param.par0, mbs->param.par, (sizeof(double) * mbs->param.na));
		/* ����p�����[�^���U�l */
		memcpy(mbs->param.sig0, mbs->param.sig, (sizeof(double) * mbs->param.na));
	}
	for( i = 0; i < mbs->param.na; i++ ) {
		trace( 5, "par/sig[%d]=%f,%f\n", i, mbs->param.par[i],
			   mbs->param.sig[i] );
	}
	for( i = 0; i < mbs->stas.nsta*9; i++ ) {
		trace( 5, "stncov[%d]=%f\n", i, mbs->param.stncov[i] );
	}

	/* [3] �ǐ������[�v */
	for (ppos = 0; ppos < mbs->stas.nsta; ppos++) {

		/* [4] �ǈʒu�����U�s��̋t�s��v�Z */
        for (i = 0; i < 3; i++) {
			for (j = 0; j < 3; j++) {
				/* �����U(�ǈʒu�p�����[�^�p) */
				workvg[ i * 3 + j ] = mbs->param.stncov[ ppos * 9 + i * 3 + j ];
			}
		}
		if (matinv(workvg,3)) {
			/* �G���[�o�� */
			showmsg( "matinv error" );
			trace( 1, "matinv error\n" );
			return 1;
		}

		/* [5] B�̎Z�o */
		for( i = 0; i < 3; i++ ) {
			workx[i] = mbs->param.par0[ 3 * ppos + i ] - mbs->param.par[ 3 * ppos + i ];
		}
		matmul( "NN", 3, 1, 3, 1.0, (double *)workvg, workx, 0.0, workb );

		/* [6] J�̎Z�o */
		workj = 0;
		for( i = 0; i < 3; i++ ) workj += workx[i] * workb[i];

		/* [7] ���K�������s��ւ̔��f */
		for (i = 0; i < 3; i++) {
			for (j = 0; j < 3; j++) {
				/* �O���[�o���p�����[�^�pNg�s�� */
				mbs->neq->ngeq[ ( ppos * 3 + i ) * mbs->param.ng  + ppos * 3 + j ]
															= workvg[ i * 3 + j ];
			}
			/* �O���[�o���p�����[�^�pBg�x�N�g�� */
			mbs->neq->bgeq[ ppos * 3 + i ] = workb[i];

			/* J */
			mbs->neq->j += workj;
		}
	} /* [3] ���[�v�G���h */

	return 0;
}

/*
 * �֐����F set_glopother_const
 * �T�v  �F ���̑��O���[�o���p�����[�^�S�������ݒ�
 *   I/O �F mbs_t *mbs ���������͍\����
 */
void set_glopother_const(mbs_t *mbs) {

	double wpk  = 0.e0;	/* ����p�����[�^�l */
	double wp0  = 0.e0;     /* ����p�����[�^�����l */
	double wsig = 0.e0;     /* ����p�����[�^���U�l */
	int i;			/* ���[�v�J�E���^ */
	int roop_end;           /* ���[�v�I�� */
	trace( 3, "set_glopother_const:\n" );

//	roop_end = mbs->param.iztd + mbs->param.nztd + mbs->param.ngra +
//			   mbs->param.nisbP + mbs->param.namb;

	roop_end = mbs->param.iztd + mbs->param.nztd + mbs->param.ngra +
			   mbs->param.nisbP + mbs->param.nisbL + mbs->param.nl2b + mbs->param.namb;
	for (i = mbs->param.iztd; i < roop_end; i++) {

		/* �p�����[�^������	*/
		wpk = 0.0;
		wp0 = 0.0;
		wsig = 0.0;

		/* �p�����[�^�̎擾	*/
		wpk = mbs->param.par[i];
		wp0 = mbs->param.par0[i];
		wsig = mbs->param.sig0[i];

		/* ���K�������s��ւ̔��f */
		(void)entry_weightmat(
				mbs->neq->ngeq,
				mbs->neq->bgeq,
				&mbs->neq->j,
				i, mbs->param.ng, wpk, wp0, wsig );
	}

	return;
}

/*
 * �֐����F entry_weightmat
 * �T�v  �F ���K�������s��ւ̏d�ݍs��̔��f
 *   IN  �F double *nio ���K�������s��N��IO�p�s��
 *          double *bio B��IO�p�z��
 *          double *jio J
 *          int iin �p�����[�^�̔��f�ʒu
 *          double par �O�񐄒�p�����[�^�l
 *          double par0 ����p�����[�^�����l
 *          double sig ����p�����[�^���U
 *   OUT �F int �X�e�[�^�X (0:ok,0>:error,1:aborted)
 */
void entry_weightmat(double *nio,
					double *bio,
					double *jio,
					int iin,
					int nx,
					double par,
					double par0,
					double sig){

	double sig2;
	double delx;

	sig2 = sig * sig;
	delx = par0 - par;

	/* Neq�ւ̕��U�̔��f
	   �ː��K�������s��Neq�̃p�����[�^���f�s�̑Ίp�����ɕ��U�̋t����ݒ� */
	if( sig2 < 1e-20 ) { /* 0������ */
		trace( 2, "entry_weightmat() : error iin=%d,nx=%d,sig2=%f\n", iin, nx, sig2 );
		nio[ iin * nx + iin ] = 1.0 / 1e-20;
		bio[ iin ] = delx / 1e-20;
		*jio += delx * delx / 1e-20;
	}
	else{
		nio[ iin * nx + iin ] = 1.0 / sig2;

		/* Beq�ւ̒l�̔��f */
		bio[ iin ] = delx / sig2;

		/* Jeq�ւ̒l�̔��f */
		*jio += delx * delx / sig2;
	}

	return;
}

/*
 * �֐����F set_rwpar_const
 * �T�v  �F �����_���E�H�[�N�p�����[�^�̍S�������ݒ�
 *   I/O  �F mbs_t     *mbs     ���������͍\����
 *   I    �F prcopt_t  *popt    ���������͓��I�v�V�����\����
 *   O    �F int �X�e�[�^�X�@(0:OK,1:warning,>1:NG)
 */
int set_rwpar_const(mbs_t *mbs,prcopt_t *popt) {

	double sig2ti[3];
	int tpst[3];
	int tpnum[3];
	int iret = 0;
	int i, j,s;
	int n = 0;
	trace( 3, "set_rwpar_const:\n" );

	/*  Random Walk�d�ݐݒ� */
	sig2ti[0] = pow(popt->mopt.sigtrop[0],(double)2) * popt->mopt.tiztd;
	sig2ti[1] = pow(popt->mopt.sigtrop[1],(double)2) * popt->mopt.tigra;
	sig2ti[2] = pow(popt->mopt.sigtrop[2],(double)2) * popt->mopt.tigra;


	/* �����Ɠ�k�̊i�[�ʒu����o�� */
	tpst[0] = mbs->param.iztd;
	tpst[1] = mbs->param.igra;
	tpst[2] = mbs->param.igra + mbs->param.ngra / 2;

	/* �Η����x���p�����[�^�� */
	tpnum[0] = mbs->param.nztd / mbs->stas.nsta;
	tpnum[1] = mbs->param.ngra / mbs->stas.nsta / 2;
	tpnum[2] = mbs->param.ngra / mbs->stas.nsta / 2;

	/* �Η����x���ʐ��������A�������z�����A
	�@ ��k���z�����̃p�����[�^���������[�v */
	for(i=0;i<3;i++) {
	    if (tpnum[i]==1) continue;      /* skip number of time = 1 */

		/* �����_���E�H�[�N�d�݂�0�Ȃ�0������̂��߁A�l���Ȃ� */
		if ( fabs(sig2ti[i]) < 1e-50  ) {
			iret = 1;
			continue;
		}

		/* �����_���E�H�[�N�̎��ԑ��֋����U�̔��f */
		for (s=0;s<mbs->stas.nsta;s++) {
		    j=tpst[i]+s*tpnum[i];       /* first address of parameter */
		    for(n=1;n<tpnum[i];n++,j++) {
		        mbs->neq->ngeq[j    *mbs->param.ng+j  ] += 1.0/sig2ti[i];
		        mbs->neq->ngeq[(j+1)*mbs->param.ng+j  ] -= 1.0/sig2ti[i];
		        mbs->neq->ngeq[j    *mbs->param.ng+j+1] -= 1.0/sig2ti[i];
		        mbs->neq->ngeq[(j+1)*mbs->param.ng+j+1] += 1.0/sig2ti[i];
		    }
		}
	}

	return iret;

}

/*
 * �֐����F gen_neqepochparam
 * �T�v  �F �G�|�b�N�p�����[�^�p���K�������s��̐���
 *   I/O �F mbs_t *mbs ���������͍\����
 *   O   �F int �X�e�[�^�X (0:ok,0>:error,1:aborted)
 */
int gen_neqepochparam(mbs_t *mbs) {

	int ne = 0; /* �G�|�b�N�p�����[�^�� */
	int ng = 0; /* �O���[�o���p�����[�^�� */
	int i, j;
	trace( 3, "gen_neqepochparam:\n" );

	/* �s��T�C�Y�̎擾 */
	ne = mbs->param.nep;
	ng = mbs->param.ng;

	/* Ni�ANgi�ABi�̍s��̃������m�� */
	if( !mbs->neq->neeq ) mbs->neq->neeq = (double *)malloc(sizeof(double) * ne * ne);
	if (mbs->neq->neeq == NULL) {
		showmsg( "alloc error : neq.neeq" );
		trace( 1, "alloc error : neq.neeq\n" );
		return 1;
	}
	if( !mbs->neq->ngeeq ) mbs->neq->ngeeq = (double *)malloc(sizeof(double) * ne * ng);
	if (mbs->neq->ngeeq == NULL) {
		showmsg( "alloc error : neq.ngeeq" );
		trace( 1, "alloc error : neq.ngeeq\n" );
		return 1;
	}
	if( !mbs->neq->beeq ) mbs->neq->beeq = (double *)malloc(sizeof(double) * ne);
	if (mbs->neq->beeq == NULL) {
		showmsg( "alloc error : neq.beeq" );
		trace( 1, "alloc error : neq.beeq\n" );
		return 1;
	}

	/* Ngn�ABgn�̃[���N���A */
	for (i = 0; i < ne; i++) {
		for (j = 0; j < ne; j++) {
			mbs->neq->neeq[i * ne + j] = 0.0;
		}
		for (j = 0; j < ng; j++) {
			mbs->neq->ngeeq[i * ng + j] = 0.0;
		}
		mbs->neq->beeq[i] = 0.0;
	}

	return 0;

}

/*
 * �֐����F set_epochparaconst
 * �T�v  �F ���������͎又��
 *   IN  �F mbs_t *mbs ���������͍\����
 *       �F int    ieq �G�|�b�N�ԍ�
 *   OUT �F int �X�e�[�^�X (0:ok, 0>:error, 1:aborted)
 */
int set_epochparaconst(mbs_t *mbs, const prcopt_t *opt, int iep) {

	/* [1] �ϐ������� */
	double wpk  = 0.e0;
	double wp0  = 0.e0;
	double wsig = 0.e0;
	int    ico  = mbs->param.ng + mbs->param.nep * iep;
	int    i;
	int j,k,m,nr,na,w,stat,*ih;
	double y=0.0,*hmat;
	trace( 3, "set_epochparaconst:\n" );

	/* [2] �ǃN���b�N�A�q���N���b�N�����[�v */
	for (i = 0; i < mbs->param.nep; i++) {

		/* [3] �p�����[�^������ */
		wpk  = 0.0;
		wp0  = 0.0;
		wsig = 0.0;

		/* [4] �p�����[�^�擾 */
		wpk  = mbs->param.par[ico+i];
		wp0  = mbs->param.par0[ico+i];
		wsig = mbs->param.sig0[ico+i];

		/* [5] ���K�������s��ւ̔��f */
		entry_weightmat(mbs->neq->neeq, mbs->neq->beeq, &(mbs->neq->j), i,
						mbs->param.nep, wpk, wp0, wsig);
	}

	/* constraint to base station clock sum */
	if (opt->mopt.estsatclk) {
		nr=mbs->stas.nsta;
		ih=imat(nr,1); hmat=mat(nr,1);
		for (na=i=0;i<nr;i++) {
			if (mbs->stas.sta[i].id==1) continue;		/* skip rover */
			k=ICLR(mbs->param,i,iep);
//			y+=mbs->param.par0[k]-mbs->param.par[k];	/* clock parameter unit is meters */
			y-=mbs->param.par[k];						/* clock parameter unit is meters */
			ih[na]=k;
			hmat[na++]=1.0;
		}
		w=1.0/SQR(SIG_CLOCK_SUM);
		j=mbs->neq->j; m=mbs->neq->m;					/* backup cost function */
		stat=update_nomeqmat(mbs,hmat,ih,na,y,w);
		mbs->neq->j=j; mbs->neq->m=m;					/* restore cost function */
		free(ih); free(hmat);
		if (stat) {
			showmsg("set_epochparaconst: memory allocation error");
			trace(1,"set_epochparaconst: memory allocation error\n");
			return 1;
		}
	}

	return 0;
}

/*
 * �֐����F reject_omcoutage
 * �T�v  �F �ϑ��c���ɂ��ϑ��f�[�^�̃A�E�g���C�A����
 *   IN  �F prcopt_t *opt ���������\����
 *          double y[2] �ϑ��c��
 *   OUT �F int �X�e�[�^�X�i0:ok,1:reject)
 */
static int reject_omcoutage( const prcopt_t *opt, const double *y, const double *v)
{
	double thres[2]={OMCTHRES,OMCTHRES};
	trace( 4, "reject_omcoutage:\n" );

	if (itr>0) {
		if (opt->mopt.cpomcth>0.0) thres[0]=sqrt(v[0])*opt->mopt.cpomcth;
		if (opt->mopt.promcth>0.0) thres[1]=sqrt(v[1])*opt->mopt.promcth;
	}

	if (fabs(y[0])>thres[0]) return 1;
	if (fabs(y[1])>thres[1]) return 1;

    return 0;
}

/*
 * �֐����F obs_update
 * �T�v  �F �ϑ��X�V
 *   I/O �F mbs_t *mbs ���������͍\����
 *   OUT �F int �X�e�[�^�X (0:ok,0>:error,1:aborted)
 */
int obs_update(mbs_t *mbs, prcopt_t *popt, solopt_t *sopt, int iep ) {

	/* [1]�ϐ��錾�������� */
	double     omc[2], *hmat, vardata[2];
	int        *ih, na, i, n, rcv;
	double     rs[6], dts[2], var;
	int        svh;
	int        ret = 0;
	char       msg[128];
//	char cwrk[256];
	int it;
	double tr,ts;
	int i0;
	trace( 3, "obs_update:\n" );
    
//    fprintf(fp_omc,"3 obs_update:\n" );
	/* �ϑ��s��i1�f�[�^���̂���1�sih��j */
	na = mbs->param.na;
	if ((hmat = zeros(2, na)) == NULL) {
		showmsg( "alloc error : hmat" );
		trace( 1, "alloc error : hmat\n" );
		return 1;
	}

	/* �ϑ��s��H�̗� */
	if ((ih = imat(2, na)) == NULL) {
		showmsg( "alloc error : ih" );
		trace( 1, "alloc error : ih\n" );
		free(hmat);
		return 1;
	}

	/* �C�^���[�V�������̏���G�|�b�N�A�N�Z�X����iobs�����Z�b�g */
	if ( !iep ) {
		iobs = 0;
		memcpy(&ptime, &(mbs->obss->data[iobs].time), sizeof(gtime_t));
		mbs->neq->m = 0;
		mbs->neq->mdd = 0;
	}

	/*  [2]�ϑ��f�[�^���Ƀ��[�v */
	/* �ϑ��f�[�^�P�ʂŉ��L���������[�v���� */
	/* �ϑ��f�[�^�������ς��܂Ń��[�v */
	rcv = -1;
	while (fabs(timediff(mbs->obss->data[iobs].time, ptime ) < 0.5) && iobs < mbs->obss->n ) {
		trace(2,"obs_update: tws=%.0f rcv=%6s sat=%3d\n",
				time2gpst(mbs->obss->data[iobs].time,NULL),
				mbs->stas.sta[mbs->obss->data[iobs].rcv-1].name2,
				mbs->obss->data[iobs].sat);

		/* [3]�q���ʒu�Z�o */
		satposs(mbs->obss->data[iobs].time, (mbs->obss->data + iobs), 1, mbs->navs,
			EPHOPT_PREC, rs, dts, &var, &svh);
		trace( 5, "rs=%f,%f,%f,%f,%f,%f\n", rs[0], rs[1], rs[2], rs[3], rs[4], rs[5] );
		testeclipse(mbs->obss->data+iobs, 1, mbs->navs, rs);
		trace( 5, "rs=%f,%f,%f,%f,%f,%f,dts=%f,%f,var=%f\n", rs[0], rs[1], rs[2], rs[3], rs[4], rs[5],
				dts[0], dts[1], var );

		/* �P�Ƒ���(�ǖ��ɉq���X�e�[�^�X�擾) */
		if( rcv != mbs->obss->data[iobs].rcv ) {
			rcv = mbs->obss->data[iobs].rcv;
			n = 0;
			while (fabs(timediff(mbs->obss->data[iobs+n].time, ptime ) < 0.5) &&
				iobs+n < mbs->obss->n && rcv == mbs->obss->data[iobs+n].rcv ) n++;
			matcpy(rtk->sol.rr,mbs->param.par+(rcv-1)*3,3,1);
			if( !pntpos( &(mbs->obss->data[iobs]), n, mbs->navs, popt, &rtk->sol, NULL, rtk->ssat, msg )) {
				trace( 2, "obs_update():pntpos error.msg=%s\n", msg );
			}
		}

		/* [4]�ϑ��ʗ��_�l�v�Z */
		/* �ϑ��ʗ��_�l�v�Z�A����юc���̎Z�o���s�� */
		na = mbs->param.na;
		omc[0] = omc[1] = 0.0;


		it = (int)floor((timediff(mbs->obss->data[iobs].time, mbs->ts) + DTTOL) / mbs->ti);

		i0=ICLR(mbs->param,mbs->obss->data[iobs].rcv-1,it);
		tr=mbs->param.par[ICLR(mbs->param,mbs->obss->data[iobs].rcv-1,it)];
		i0=ICLS(mbs->param,mbs->obss->data[iobs].sat-1,it);
		ts=popt->mopt.estsatclk ? mbs->param.par[ICLS(mbs->param,mbs->obss->data[iobs].sat-1,it)] : dts[0]*CLIGHT;

	//	if(sopt->recclout) outsolcrinexrec(&mbs->obss->data[iobs].time, mbs->stas.sta[mbs->obss->data[iobs].rcv-1].name2, tr);
	//	if(sopt->satclout) outsolcrinexsat(&mbs->obss->data[iobs].time, mbs->obss->data[iobs].sat, ts);

		ret = res_LCPC(&(mbs->obss->data[iobs]), rs, dts, &var, &svh,
									   rtk->ssat, popt, sopt, mbs, omc, vardata, &na, ih, hmat);


	//	if(sopt->recclout) outsolcrinexrec(&mbs->obss->data[iobs].time, mbs->stas.sta[mbs->obss->data[iobs].rcv-1].name2, (double)iobs);
	//	if(sopt->satclout) outsolcrinexsat(&mbs->obss->data[iobs].time, mbs->obss->data[iobs].sat, (double)iobs);

		trace( 4, "obs_update():res_LCPC=%d,na=%d,omc=%f,%f,vardata=%f,%f\n", ret, na, omc[0], omc[1],
				vardata[0], vardata[1] );
		for( i = 0; i < na; i++ )
			 trace( 5, "obs_update():hmat[0][%d]=%f,hmat[1][%d]=%f,ih[0][%d]=%d,ih[1][%d]=%d\n",
			 i, hmat[i], i, hmat[i+mbs->param.na], i, ih[i], i, ih[i+mbs->param.na] );
		if (ret != 0) {
			iobs++;	continue;
		}

		/* [6]�t�B���^�X�V */
		/* �ϑ��s��𐳋K�������s��ɔ��f����B����ɍX�V�������s��ꂽ�ꍇ�A
		   �X�V�����ϑ��f�[�^�����J�E���g���Ă��� */
		/* �^�������Ɣ����g�ʑ���2�񕪌ďo */
		update_nomeqmat(mbs, hmat,                 ih,                 na, omc[0], 1.0/vardata[0]);
		update_nomeqmat(mbs, hmat + mbs->param.na, ih + mbs->param.na, na, omc[1], 1.0/vardata[1]);

		/* [7]���[�v�G���h */
		iobs++;
	}

	if( iobs < mbs->obss->n ) memcpy(&ptime, &(mbs->obss->data[iobs].time), sizeof(gtime_t));

	/* [8]�I������ */
	free(ih);
	free(hmat);
	return 0;
}

/*
 * �֐����F res_LCPC
 * �T�v  �F �c���ƌv��s��̌v�Z
 *  IN   �F *obs obsd_t �ϑ��f�[�^�\����
 *			*rs double �q���ʒu���x(6)
 *			*dts double �q�����v�o�C�A�X�h���t�g(2)
 *			*vare double �q���ʒu�E���v�Ɋւ�鑍���U(1)
 *			*svh int �q���w���X(1)
 *			*popt prcopt_t ��������
 *          *sopt solopt_t �o�͏���
 *			*mbs mbs_t ���������͍\����
 *	OUT		*omc double LC��PC�̎c��(2)
 *			*vard double LC��PC�̕W���΍�(2)
 *	IN/OUT	*na int IN:�v��s��񐔁AOUT:����p�����[�^��
 *	OUT		*ih int �e����p�����[�^��NEQ�̈ʒu(nx)
 *			*hmat double �v��s��(2*na)
 * �߂�l�F int �X�e�[�^�X
 */
int res_LCPC(obsd_t *obs, double *rs, double *dts, double *vare,
			 int *svh, ssat_t *ssat, prcopt_t *popt, solopt_t *sopt, mbs_t *mbs, double *omc,
			 double *vard, int *na, int *ih, double *hmat )
{
	int i, k, brk=0;
	int it, itztd, itgra;
	int sat, sys;
	int ii;
	double r, e[3], azel[2];
	double x[3], dtdx[3], dtrp = 0.0, vart = 0.0;
	double dantr[NFREQ] = {0}, dants[NFREQ] = {0};
	double meas[2] = {0.0}, varm[2] = {0.0};

	/* [0]�ϐ���` */
	double rr[3], disp[3], pos[3];
	int nn = 0;
	int sysno = 0;
	int ap, bp;
	double tws,tr,ts,C1,C2;
	sta_t *sta1=mbs->stas.sta+obs->rcv-1;
	int stat=0;
	trace( 4, "res_LCPC:\n" );
//    fprintf(fp_omc,"4 res_LCPC:\n" );
	for (i = 0; i < 3; i++) dtdx[i] = 0.0;
	ap = popt->navsys;
	bp = satsys( obs->sat, NULL );
	ap >>= 2;
	bp >>= 2;

	/* [1]�����ԍ��̌v�Z */
	tws=time2gpst(obs->time,NULL);
	it = (int)floor((timediff(obs->time, mbs->ts) + DTTOL) / mbs->ti);
	itztd = (int)floor((timediff(obs->time, mbs->ts) + DTTOL) / popt->mopt.tiztd);
	itgra = (int)floor((timediff(obs->time, mbs->ts) + DTTOL) / popt->mopt.tigra);
	trace( 5, "it=%d,itztd=%d,itgra=%d\n", it, itztd, itgra );

	/* [2]�ǈʒu�̎擾 */
	for (i=0;i<3;i++) rr[i] = mbs->param.par[IPOS(mbs->param,obs->rcv-1)+i];

	/* [3]�����̕␳ */
	if (popt->tidecorr) {
		tidedisp(gpst2utc(obs->time), rr, popt->tidecorr, NULL, NULL, disp);
		for (i = 0; i < 3; i++)
			rr[i] += disp[i];
	}

	/* [4]���n���W�ւ̕ϊ� */
	ecef2pos(rr, pos);
	/* antenna delta correction */
	enu2ecef(pos,mbs->stas.sta[obs->rcv-1].del,disp);
	for (i=0;i<3;i++) rr[i]+=disp[i];

	/* [5]�q���̃X�e�[�^�X�m�F */
	i = 0;
	sat = obs->sat;
	trace( 5, "sat=%d,sys=%d,vs=%d\n", sat, satsys(sat,NULL), ssat[sat-1].vs );
	if (!(sys = satsys(sat, NULL)) || !ssat[sat - 1].vs) {
		trace(2,"res_LCPC  : tws=%.0f rcv=%6s sat=%3d reject by ssat\n",tws,sta1->name2,sat);
		*na = 0; return( 1 );
	}

	/* [6]������AzEl�̌v�Z */
	if ((r = geodist(rs, rr, e)) <= 0.0 || satazel(pos, e, azel) < popt->elmin) {
		trace(2,"res_LCPC  : tws=%.0f rcv=%6s sat=%3d reject by elcut\n",tws,sta1->name2,sat);
		*na = 0; return( 1 );
	}

	/* [7]�q����Ԃ̊m�F */
	if (svh[i] < 0 || popt->exsats[sat - 1] == 1) {
		trace( 2, "res_LCPC : data reject[7]\n" );
		*na = 0; return( 1 );
	}
	if (popt->exsats[sat - 1] != 2 && svh[i]) {
		trace(2, "res_LCPC : unhealthy satellite: sat=%2d svh=%02X\n", sat, svh[i]);
		*na = 0; return( 1 );
	}

	/* [8]�Η����x���ʂ̌v�Z */
	if (popt->tropopt >= TROPOPT_EST) {

	    /* x�ɐ���l���i�[ */
	    for (i=0;i<3;i++) x[i]=0.0;
	    x[0]=mbs->param.par[IZTD(mbs->param,obs->rcv-1,itztd)];
		if( popt->tropopt >= TROPOPT_ESTG ) {
            x[1]=mbs->param.par[IGNS(mbs->param,obs->rcv-1,itgra)];
            x[2]=mbs->param.par[IGEW(mbs->param,obs->rcv-1,itgra)];
		}
		dtrp = prectrop2(obs->time, pos, azel, popt, x, dtdx, &vart);
	}
	else if (popt->tropopt == TROPOPT_SAAS) {
		dtrp = tropmodel(obs->time, pos, azel, REL_HUMI);
		vart = SQR(ERR_SAAS);
	}
	else if (popt->tropopt == TROPOPT_SBAS) {
		dtrp = sbstropcorr(obs->time, pos, azel, &vart);
	}

	/* [9]�q���Ƌǂ�PCV�␳�l�̌v�Z */
	satantpcv(rs, rr, mbs->navs->pcvs + sat - 1, dants);
	antmodel(&mbs->stas.sta[obs->rcv-1].pcvr, satsys(obs->sat,NULL), popt->antdel[0], azel,popt->posopt[1], dantr);

	/* [A]�ʑ���]�␳�ʂ̌v�Z */
	windupcorr(obs->time, rs, rr, &mbs->stas.sta[obs->rcv - 1].phw[sat - 1]);

	/* [B]�d���w�x���ƈʑ���]�␳�����ϑ��ʂ̌v�Z */
	//if (!corrmeas(obs, mbs->navs, pos, azel, popt, dantr, dants, mbs->stas.sta[obs->rcv - 1].phw[sat - 1], meas, varm, &brk)) {
	//	trace( 2, "res_LCPC : data reject[B]\n" );
	//	*na = 0; return( 1 );
	//}
	if (!ifmeas(obs, mbs->navs, azel, popt, dantr, dants, mbs->stas.sta[obs->rcv - 1].phw[sat - 1], meas, varm, &mbs->stas.sta[obs->rcv - 1])) {
		trace( 2, "res_LCPC : data reject[B]\n" );
		*na = 0; return( 1 );
	}

	/* [C]�q���N���b�N�ƑΗ����x���̕␳ */
	if (popt->mopt.estsatclk) {
        r += -mbs->param.par[ICLS(mbs->param,obs->sat-1,it)] + dtrp;
		trace( 5, "r=%f,par[%d]=%f,dtrp=%f\n", r, mbs->param.ng + mbs->param.nep * it +
				  mbs->stas.nsta + obs->sat - 1, mbs->param.par[mbs->param.ng + mbs->param.nep * it +
			  mbs->stas.nsta + obs->sat - 1 ], dtrp );
	}
	else {
		r += -CLIGHT * dts[0] + dtrp;
		trace( 5, "r=%f,CLIGHT=%f,dts[0]=%f,dtrp=%f\n", r, CLIGHT, dts[0], dtrp );
	}
	trace(5,
		"sat=%2d azel=%6.1f %5.1f dtrp=%.3f dantr=%6.3f %6.3f dants=%6.3f %6.3f phw=%6.3f\n",
		sat, azel[0]*R2D, azel[1]*R2D, dtrp, dantr[0], dantr[1], dants[0],
		dants[1], mbs->stas.sta[obs->rcv - 1].phw[sat - 1]);

	/* [D]�␳�ϊϑ��f�[�^�̌��� */
	trace( 5, "meas=%f,%f,r=%f\n", meas[0], meas[1], r );
	if (meas[0] == 0.0 || meas[1] == 0.0){
		trace( 2, "res_LCPC : data reject[D]\n" );
		*na = 0; return( 1 );
	}

	/* [H]�v��s��̃N���A */
	for (k = 0; k < mbs->param.na * 2; k++) {
		hmat[k] = 0.0;
		ih[k]   = 0;
	}

	/* [F]�c���̌v�Z */
	omc[0] = meas[0] - r;
	omc[1] = meas[1] - r;
	trace( 5, "LCPC[F]omc=%f,%f\n", omc[0], omc[1] );

	/* [G]�ǈʒu�Δ��W���̐ݒ� */
	for (k = 0; k < 3; k++) {
	    ih  [nn+(*na)]=ih  [nn]=IPOS(mbs->param,obs->rcv-1)+k;
	    hmat[nn+(*na)]=hmat[nn]=-e[k];
		nn++;
	}

	/* [H]�ǃN���b�N�̐ݒ� */
	for(i = 0; i < 2; i++) {
	    omc[i]-=mbs->param.par[ICLR(mbs->param,obs->rcv-1,it)];
		trace( 5, "LCPC[H]omc[%d]=%f,par=%e\n", i, omc[i],
				mbs->param.par[mbs->param.ng + it * mbs->param.nep + obs->rcv - 1 ] );
		ih  [nn+i*(*na)]=ICLR(mbs->param,obs->rcv-1,it);
		hmat[nn+i*(*na)]=1.0;
	}
	nn++;

	sysno = 0;
	while(bp != 0) {
		sysno += (ap & 1);
		ap >>= 1;
		bp >>= 1;
	}
	if(((popt->isb == ISBOPT_EST) || (popt->isb == ISBOPT_EST_P) || (popt->isb == ISBOPT_EST_L)) && (sys != SYS_GPS)) {
		trace(5,"res_LCPC():sysno=%d\n",sysno);
	}
	/* �^�������V�X�e���ԃo�C�A�X */
	if((popt->isb == ISBOPT_EST) || (popt->isb == ISBOPT_EST_P)) {
		if (sys != SYS_GPS) {
			ih[nn] = ih[nn+(*na)] = mbs->param.iisbP + (sysno - 1) * mbs->stas.nsta +
					 obs->rcv - 1;
			omc[1] -= mbs->param.par[ih[nn]];
			trace( 5, "LCPC[X]omc[1]=%f,par=%e\n", omc[1], mbs->param.par[ih[nn]] );
			hmat[nn] = 0.0;
			hmat[nn+(*na)] = 1.0;
			nn++;
		}
	}
	/* �����g�ʑ��V�X�e���ԃo�C�A�X */
	if((popt->isb == ISBOPT_EST) || (popt->isb == ISBOPT_EST_L)) {
		if (sys != SYS_GPS) {
			ih[nn] = ih[nn+(*na)] = mbs->param.iisbL + (sysno - 1) * mbs->stas.nsta +
					 obs->rcv - 1;
			omc[0] -= mbs->param.par[ih[nn]];
			trace( 5, "LCPC[X]omc[0]=%f,par=%e\n", omc[0], mbs->param.par[ih[nn]] );
			hmat[nn] = 1.0;
			hmat[nn+(*na)] = 0.0;
			nn++;
		}
	}
	/*L2PC*/

	/* L2�ԃo�C�A�X */
	if(popt->gl2bias == GL2OPT_EST) {
		if (isL2C(obs->code[1])) {
			ih[nn] = ih[nn+(*na)] = mbs->param.il2b + obs->rcv - 1;
			omc[1] -= mbs->param.par[ih[nn]];
			trace( 5, "LCPC[X]omc[0]=%f,par=%e\n", omc[0], mbs->param.par[ih[nn]] );
			hmat[nn] = 0.0;
			hmat[nn+(*na)] = 1.0;
			nn++;
		}
	}

//	sysno = 0;
//	while(bp != 0) {
//		sysno += (ap & 1);
//		ap >>= 1;
//		bp >>= 1;
//	}
//	if((popt->isb == ISBOPT_EST) || (popt->isb == ISBOPT_EST_P)) {
//		ih[nn] = ih[nn+(*na)] = mbs->param.iisbP + (sysno - 1) * mbs->stas.nsta +
//				 obs->rcv - 1;
//		for(i=0;i<mbs->param.nisbP;++i) {
//			if(sysno-1==i) {
//				hmat[nn] = 0.0;
//				hmat[nn+(*na)] = 1.0;
//			}
//			else {
//				hmat[nn] = 0.0;
//				hmat[nn+(*na)] = 0.0;
//			}
//			nn++;
//		}
//	}
//	if((popt->isb == ISBOPT_EST) || (popt->isb == ISBOPT_EST_L)) {
//		ih[nn] = ih[nn+(*na)] = mbs->param.iisbL + (sysno - 1) * mbs->stas.nsta +
//				 obs->rcv - 1;
//		for(i=0;i<mbs->param.nisbL;++i) {
//			if(sysno-1==i) {
//				hmat[nn] = 0.0;
//				hmat[nn+(*na)] = 1.0;
//			}
//			else {
//				hmat[nn] = 0.0;
//				hmat[nn+(*na)] = 0.0;
//			}
//			nn++;
//		}
//	}



	//if (sys != SYS_GPS) {
	//	sysno = 0;
	//	while(bp != 0) {
	//		sysno += (ap & 1);
	//		ap >>= 1;
	//		bp >>= 1;
	//	}
	//	trace(5,"res_LCPC():sysno=%d\n",sysno);
	//	ih[nn] = ih[nn+(*na)] = mbs->param.iisbP + (sysno - 1) * mbs->stas.nsta + obs->rcv - 1;
	//	omc[1] -= mbs->param.par[ih[nn+(*na)]];
	//	trace( 5, "LCPC[X]omc[0]=%f,par=%e\n", omc[0], mbs->param.par[ih[nn      ]] );
	//	hmat[nn]       = 0.0;
	//	hmat[nn+(*na)] = 1.0;
	//	nn++;
 //       
	//	ih[nn] = ih[nn+(*na)] = mbs->param.iisbL + (sysno - 1) * mbs->stas.nsta + obs->rcv - 1;
	//	omc[0] -= mbs->param.par[ih[nn]];
	//	trace( 5, "LCPC[X]omc[1]=%f,par=%e\n", omc[1], mbs->param.par[ih[nn+(*na)]] );
	//	hmat[nn]       = 1.0;
	//	hmat[nn+(*na)] = 0.0;
	//	nn++;
	//}

	/* [I]�Η����x���̐ݒ� */
	if (popt->tropopt >= TROPOPT_EST) {
	    ih  [nn+(*na)]=ih  [nn]=IZTD(mbs->param,obs->rcv-1,itztd);
	    hmat[nn+(*na)]=hmat[nn]=dtdx[0];
		nn++;
	}

	/* [J]�Η������z�̐ݒ� */
	if (popt->tropopt >= TROPOPT_ESTG) {
	    ih  [nn+(*na)]=  ih[nn]=IGNS(mbs->param,obs->rcv-1,itgra);
	    hmat[nn+(*na)]=hmat[nn]=dtdx[1];
	    nn++;
	    ih  [nn+(*na)]=  ih[nn]=IGEW(mbs->param,obs->rcv-1,itgra);
	    hmat[nn+(*na)]=hmat[nn]=dtdx[2];
	    nn++;
	}

	/* [K]�q���N���b�N�̐ݒ� */
	if (popt->mopt.estsatclk) {
	    ih  [nn+(*na)]=ih  [nn]=ICLS(mbs->param,obs->sat-1,it);
		hmat[nn+(*na)]=hmat[nn]=-1.0;
		nn++;
	}

	/* [L]�A���r�M���C�e�B�̐ݒ� */
	if ((ii = searchpass(&mbs->pass, obs->rcv - 1, obs->sat, obs->time)) < 0) {
		trace(2, "res_LCPC  : tws=%.0f rcv=%6s sat=%3d reject by invalid pass\n",tws,sta1->name2,sat);
		*na = 0; return( 1 );
	}
	omc[0]-=mbs->param.par[IAMB(mbs->param,ii)];
	ih  [nn+(*na)]=ih  [nn]=IAMB(mbs->param,ii);
    hmat[nn]      =1.0;
	hmat[nn+(*na)]=0.0;
    trace( 5, "LCPC[L]omc[1]=%f,par=%e\n", omc[0], mbs->param.par[ih[nn]] );
	nn++;

    /* 1/4�T�C�N���V�t�g�̐ݒ� */
//    omc[0]-=mbs->param.par[IAMB(mbs->param,ii)];


	/* [M]�W���΍��̐ݒ� */
	for (k = 0; k < 2; k++) {
		vard[k] = varerr(mbs->navs,sta1,sys,obs->code[k],azel[1],k,popt) + varm[k] + (*vare) + vart;
	}

	/* [O]�I������ */
	*na = nn;

	if ((stat=reject_omcoutage( popt, omc, vard ))) {
		trace( 2, "res_LCPC: reject_omcoutage.omc=%f,%f\n", omc[0], omc[1] );
	}

	if (fp_omc) {
		time2str(obs->time,tstr0,0);
		//tr=mbs->param.par[mbs->param.ng+it*mbs->param.nep+obs->rcv-1];
		//ts=dts[0]*CLIGHT;
		tr=mbs->param.par[ICLR(mbs->param,obs->rcv-1,it)];
		ts=popt->mopt.estsatclk ? mbs->param.par[ICLS(mbs->param,obs->sat-1,it)] : dts[0]*CLIGHT;

		C1=1.0/(1.0-SQR(lam_carr[1]/lam_carr[0])); C2=1.0-C1;
		fprintf(fp_omc,"%s,%6s,%3d,%d,%.4f,%.4f,%.1f,%.1f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
				tstr0,mbs->stas.sta[obs->rcv-1].name2,obs->sat,stat,
	            omc[0],omc[1],obs->azel[0]*R2D,obs->azel[1]*R2D,
				tr,ts,dtrp,mbs->param.par[IAMB(mbs->param,ii)],
				C1*dantr[0]+C2*dantr[1],C1*dants[0]+C2*dants[1],
				(C1*lam_carr[0]+C2*lam_carr[1])*sta1->phw[sat-1]);
	}

 //	if(sopt->recclout) outsolcrinexrec(&obs->time, mbs->stas.sta[obs->rcv-1].name2, tr);
//	if(sopt->satclout) outsolcrinexsat(&obs->time, obs->sat, ts);
	//    strcpy(clcfile,outfile);
	//    strcat(clcfile,".clc");
	//	rtkclosecrinex();
	//	rtkopencrinex(clcfile);



    //fprintf(fp_omc, "AR %-4s %04.0f %02.0f %02.0f %02.0f %02.0f %09.6f  1   %19.12f\n"
    //    ,rtk->opt.id4char[0],ep[0],ep[1],ep[2],ep[3],ep[4],ep[5], rtk->sol.dtr[0]);

//    }
//    popt->st
    //fprintf(fp_omc, "AR %-4s %04.0f %02.0f %02.0f %02.0f %02.0f %09.6f  1   %19.12f\n"
    //    ,rtk->opt.id4char[0],ep[0],ep[1],ep[2],ep[3],ep[4],ep[5], rtk->sol.dtr[0]);

	return stat;
}

/*
 * �֐����F update_nomeqmat
 * �T�v  �F �G�|�b�N�p�����[�^�p���K�������s��̐���
 *   I/O �F mbs_t  *mbs  ���������͍\����
 *   I   �F double *hmat �ϑ��s��i1�f�[�^���̂���1�sih��j
 *   I   �F int    *ih   �ϑ��s��̒l�̃A�h���X
 *   I   �F int    nh    �ϑ��s��H�̗v�f��
 *   I   �F double y     �ϑ��c��
 *   I   �F double w     �ϑ��f�[�^�d��
 *   O   �F int �X�e�[�^�X (0:ok,0>:error,1:aborted)
 */
int update_nomeqmat(mbs_t *mbs, double *hmat, int *ih, int nh, double y, double w) {

	/* [1]�ϐ��錾�������� */
	double *HwH;
	char   *tr = {"TN"};

	int i, j, iw, jw;
	int ng, ne;
	trace( 3, "update_nomeqmat:\n" );

	ng = mbs->param.ng;
	ne = mbs->param.nep;

	if ((HwH = zeros(nh, nh)) == NULL) {
		showmsg( "alloc error : HmH" );
		trace( 1, "alloc error : HwH\n" );
		return 1;
	}

	/* [2]N�̍X�V */
	/* ���K������N�s���obs->i�Ɍv��s��ɂ��X�V�ʂ����L�s�񉉎Z�ɂ��Z�o���A
	   N�s��ɉ��Z���� */
	matmul(tr, nh, nh, 1, w, hmat, hmat, 0.0, HwH);

	/* N�̍s��̓O���[�o���p�����[�^�A�G�|�b�N�p�����[�^�ŕʂ�Ă��邽�߁A
	   �͈͂��l�����Ď������� */
	/* H�̌v��s��͒l�������Ă���v�f�݂̂ň��k����Ă��邽�߃A�h���X�l��
	   �i�[���ꂽih�s��œW�J���� */
	for (i = 0; i < nh; i++) {          /* column i */
		for (j = 0; j < nh; j++) {      /* row j */
			if (ih[i] < ng && ih[j] < ng ) {
				mbs->neq->ngeq[ih[i] * ng + ih[j]] += HwH[i * nh + j];
			}
			else if (ih[j] < ng && ih[i] >= ng) {
				iw = ( ih[i] - ng ) % ne;
				mbs->neq->ngeeq[iw * ng + ih[j]] += HwH[i * nh + j];
			}
			else if(ih[i] >= ng && ih[j] >= ng) {
				iw = ( ih[i] - ng ) % ne;
				jw = ( ih[j] - ng ) % ne;
				mbs->neq->neeq[iw * ne + jw] += HwH[i * nh + j];
			}
			else {
				;
			}
		}
	}

	/* [3]B�̍X�V */
	/* ���K������B�Ɋϑ��c���ɂ��X�V�ʂ����L�s�񉉎Z�ɂ��Z�o���A���Z���� */
	/* B�̍s��̓O���[�o���p�����[�^�A�G�|�b�N�p�����[�^�ŕʂ�Ă���̂Ŕ͈͂�
	   �l�����Ĕ��f���� */
	/* H�̌v��s��͒l�������Ă���v�f�݂̂ň��k����Ă��邽�߃A�h���X�l��
	   �i�[���ꂽih�s��œW�J���� */
	for (i = 0; i < nh; i++) {
		if (ih[i] < ng) {
			mbs->neq->bgeq[ih[i]] += hmat[i] * w * y;
		}
		if (ih[i] >= ng) {
			iw = ( ih[i] - ng ) % ne;
			mbs->neq->beeq[iw] += hmat[i] * w * y;
		}
	}

	/* [4]J�̍X�V */
	mbs->neq->j += y * y * w;
	mbs->neq->m++;

	free( HwH );
	return 0;
}

/*
 * �֐����F del_epochparam
 * �T�v  �F �G�|�b�N�p�����[�^�̏����A�ۑ�����
 *   IN  �F mbs_t    *mbs    ���������͍\����
 *       �F prcopt_t *prcopt ���������͓��I�v�V�����\����
 *       �F int      ng      �O���[�o���p�����[�^��
 *       �F int      ne       �G�|�b�N�p�����[�^��
 *   OUT �F int �X�e�[�^�X (0:ok, 0>:error, 1:aborted)
 */
int del_epochparam(mbs_t *mbs, prcopt_t *prcopt, int ng, int ne,int iep) {

	/* [1] ��Ɨp�ϐ���錾 */
	double *inv_ne;
	double *upng;
	double *upbg;
	double *workn;
	FILE *fp;
	char cwrk[256];
	int    i, ret = 0;
	trace( 3, "del_epochparam:\n" );

	if ((inv_ne = (double *)malloc(sizeof(double) * ne * ne)) == NULL) {
		showmsg( "alloc error : inv_ne" );
		trace( 1, "alloc error : inv_ne\n" );
		return 1;
	}
	if ((upng = (double *)malloc(sizeof(double) * ng * ng)) == NULL) {
		showmsg( "alloc error : upng" );
		trace( 1, "alloc error : upng\n" );
		free(inv_ne);
		return 1;
	}
	if ((upbg = (double *)malloc(sizeof(double) * ng)) == NULL) {
		showmsg( "alloc error : upbg" );
		trace( 1, "alloc error : upbg\n" );
		free(inv_ne); free(upng);
		return 1;
	}
	if ((workn  = (double *)malloc(sizeof(double) * ng * ne)) == NULL) {
		showmsg( "alloc error : workn" );
		trace( 1, "alloc error : workn\n" );
		free(inv_ne); free(upng); free(upbg);
		return 1;
	}
	for (i = 0; i < (ne * ne); i++) inv_ne[i] = 0.0;
	memset(upng,0,sizeof(double)*ng*ng);
	for (i = 0; i < ng; i++) upbg[i] = 0.0;
	for (i = 0; i < (ng * ne); i++) workn[i] = 0.0;

	/* [2] Ng�̍X�V */
	memcpy(inv_ne, mbs->neq->neeq, (sizeof(double) * (ne * ne)));
	if( matinv(inv_ne, ne) ) {
		showmsg("matinv error");
		trace(2,"matinv error\n");
		free(workn); free(upbg); free(upng); free(inv_ne);
		return 1;
	}

	matmul("NN", ng, ne, ne, 1.0, mbs->neq->ngeeq, inv_ne, 0.0, workn);
	matmul("NT", ng, ng, ne, 1.0, workn, mbs->neq->ngeeq, 0.0, upng);

	for (i = 0; i < (ng * ng); i++) {
		mbs->neq->ngeq[i] -= upng[i];
	}

	/* [3] Bg�̍X�V */
	matmul("NN", ng, 1, ne, 1.0, workn, mbs->neq->beeq, 0.0, upbg);
	for (i = 0; i < ng; i++) {
		 mbs->neq->bgeq[i] -= upbg[i];
	}

	/* [4] */
	sprintf( cwrk, "%s.%d", prcopt->mopt.eptmp, iep );
	if ((fp = fopen(cwrk, "wb")) == NULL) {
		showmsg("fopen error %s", cwrk);
		trace(1,"fopen error %s\n", cwrk);
		free(workn); free(upbg); free(upng); free(inv_ne);
		return 1;
	}

	/* �G�|�b�N������ */
	if ((fwrite(&ng, sizeof(int), 1, fp)) != 1) {
		ret = 1;
	}
	else if ((fwrite(&ne, sizeof(int), 1, fp)) != 1) {
		ret = 1;
	}
	else if ((fwrite(inv_ne, (sizeof(double) * ne * ne), 1, fp)) != 1) {
		ret = 1;
	}
	else if ((fwrite(mbs->neq->ngeeq, (sizeof(double) * ne * ng), 1, fp)) != 1) {
		ret = 1;
	}
	else if ((fwrite(mbs->neq->beeq, (sizeof(double) * ne), 1, fp)) != 1) {
		ret = 1;
	}
	else {
		;
	}
	if (ret == 1) {
		showmsg("fwrite error");
		trace(1,"fwrite error\n");
	}
	fclose(fp);
	free(workn); free(upbg); free(upng); free(inv_ne);

	return ret;
}

/*
 * �֐����F est_globalparam
 * �T�v  �F �O���[�o���p�����[�^����
 *   I/O �F mbs_t *mbs ���������͍\����
 *   OUT �F int �X�e�[�^�X (0:ok,0>:error,1:aborted)
 */
int est_globalparam(mbs_t *mbs) {
	int ret    = 0;		/* ���֐����A�l */

	/* �ϐ��錾�������� */
	char uplo = 'U';
	int n, nrhs, lda, ldb, info=0, i,j,k;
	int *ipiv;
	double *neq, *x, *work;
	trace( 3, "est_globalparam:\n" );

	/* �O���[�o���p�����[�^�� */
	n = mbs->param.ng;
	nrhs = 1;
	lda = ldb = n;

	ipiv = (int *)malloc( sizeof(int) * n );
	if (ipiv == NULL) {
		showmsg( "alloc error : ipiv" );
		trace( 1, "alloc error : ipiv\n" );
		return 1;
	}
	for(i = 0; i < n; i++) ipiv[i] = i;

	neq = (double *)malloc( sizeof(double) * n * n );
	if (neq == NULL) {
		showmsg( "alloc error : neq" );
		trace( 1, "alloc error : neq\n" );
		free( ipiv );
		return 1;
	}
	memcpy( neq, mbs->neq->ngeq, sizeof(double) * n * n );
	for( i = 0; i < n * n; i++ ) trace( 5, "neq[%d]=%f\n", i, neq[i] );
//	for( i = 0; i < n * n; i++ ) trace( 1, "neq[%d]=%f\n", i, neq[i] );
//    trace(1,"neq=\n"); tracemat(1,neq, n,n,15,12);
	x = (double *)malloc( sizeof(double) * n );
	if (x == NULL) {
		showmsg( "alloc error : x" );
		trace( 1, "alloc error : x\n" );
		free( ipiv ); free( neq );
		return 1;
	}
	memcpy( x, mbs->neq->bgeq, sizeof(double) * n );
	for( i = 0; i < n; i++ ) trace( 5, "x[%d]=%f\n", i, x[i] );

	work = (double *)malloc( sizeof(double) * n );
	if (work == NULL) {
		showmsg( "alloc error : work" );
		trace( 1, "alloc error : work\n" );
		free( ipiv ); free( neq ); free( x );
		return 1;
	}
	for (i = 0; i < n; i++) work[i] = 0.0;

	/* ���K������������ */
	dposv_( &uplo, &n, &nrhs, neq, &lda, x, &ldb, &info );
//    trace(1,"neq=\n"); tracemat(1,neq, n,n,15,12);
	memcpy( mbs->param.dx, x, sizeof(double) * n );
	for( i = 0; i < n; i++ ) trace( 5, "dx[%d]=%e\n", i, mbs->param.dx[i] );

	/* ����㕪�U�����U�s��Z�o */
#if 0
	memcpy( neq, mbs->neq->ngeq, sizeof(double) * n * n );
	if( matinv( neq, n ) ) {
#endif
	dpotri_(&uplo,&n,neq,&lda,&info);
 //   trace(1,"neq=\n"); tracemat(1,neq, n,n,15,12);
	if (info) {
		showmsg("matinv error");
		trace(2,"matinv error\n");
		ret = 1;
	}
	else {
		trace( 5, "est_globalparam():neq.j=%f,m=%d,mdd=%d\n", mbs->neq->j, mbs->neq->m, mbs->neq->mdd );

		if( mbs->neq->m + mbs->neq->mdd == 0 ) {
		    showmsg("error : neq->m+mdd = 0");
		    trace(1,"error : neq->m+mdd = 0\n");
		    ret = 1;
		}
		else {
			for (i = 0; i < n; i++) {
			for (j = 0; j < n; j++) {
                k=j+i*n;
				mbs->neq->pgmat[k] = mbs->neq->j / (double)( mbs->neq->m +
								 mbs->neq->mdd ) * neq[k];
                if(i==j) {
                    if(mbs->neq->pgmat[k]<0) {
                        //trace(1, "mbs->neq->j=%.15e\n", mbs->neq->j);
                        //trace(1, "mbs->neq->m=%d\n", mbs->neq->m);
                        //trace(1, "mbs->neq->mdd=%d\n", mbs->neq->mdd);
                        //trace(1,"mbs->neq->pgmat=\n"); tracemat(1,mbs->neq->pgmat, n,n,15,12);
                        //trace(1,"neq=\n"); tracemat(1,neq, n,n,15,12);
//                        exit(1);
                    }
                }
			}
            }
			for( i = 0; i < n; i++ ) trace( 5, "pgmat[%d][%d]=%e,neq(INV)[%d][%d]=%e\n",
					i, i, mbs->neq->pgmat[i*n+i], i, i, neq[i*n+i] ); /* �Ίp�����̂ݏo�� */
		}
	}

	/* �������J�� */
	free( ipiv ); free( neq ); free( x ); free( work );

	return ret;

}

/*
 * �֐����F est_ddlcambi
 * �T�v  �F ��d��LC�A���r�M���C�e�B�ɂ��S���𐄒菈��
 *   IN  �F mbs_t *mbs ���������͍\����
 *          prcopt_t *opt ���������\����
 *   OUT �F int �X�e�[�^�X (0:ok,0>:error,1:aborted)
 */
int est_ddlcambi(mbs_t *mbs, prcopt_t *opt){

	/* [1]�ϐ��錾�������� */
	int rc = 0, i, j, iam;
	int ipmax, ips1, ips2, ips3, ips4, *psadd, *ddtbw;
	int sta1, sta2, nsta, nsat;
	int sta1p, sta1p_e, sta2p, sta2p_e, sta3p, sta3p_e, sta4p, sta4p_e;
	int sat1, sat2;
	double b1;
	gtime_t st1, et1, st2, et2, st3, et3;
	double e[3];
	double ddmw, ddmwv, ddmwfix;
	double f1, f2, bc, b1v, b1fix, bcfix;
	double p0,c1,c2,cov[10];
	int ih[4],stat,nfw=0,nfn=0;
	char cwrk[32];
    passd_t *pd;
	trace( 3, "est_ddlcambi:\n" );

	nsta = mbs->stas.nsta;
	nsat = MAXSAT;
	ipmax = mbs->pass.n;

	/* [2]��d���g�����쐬 */
	if( !itr ) {
		indd = 0;
		inddm = 100;
		ddtb = calloc( inddm * 4, sizeof( int ));
		psadd = calloc( nsta * nsat, sizeof( int ));
		for( i = 0; i < nsta * nsat; i++ ) psadd[i] = -1;

		/* [3]�p�X�f�[�^�ǉq���A�h���X�e�[�u���쐬 */
		for( i = 0; i < ipmax; i++ ) {
			if( psadd[ mbs->pass.data[i].sta * nsat + mbs->pass.data[i].sat - 1 ] == -1 ) {
				psadd[ mbs->pass.data[i].sta * nsat + mbs->pass.data[i].sat - 1 ] = i;
			}
		}

		/* [4]�Ǒg���� */
		for( sta1 = 0; sta1 < nsta - 1; sta1++ ) {
			for( sta2 = sta1 + 1; sta2 < nsta; sta2++ ) {
				if( geodist( mbs->stas.sta[sta1].pos, mbs->stas.sta[sta2].pos, e ) > opt->mopt.maxdddist ) {
				  continue;
				}

				/* [5]�q��1���� */
				for( sat1 = 0; sat1 < nsat - 1; sat1++ ) {
					if(( sta1p = psadd[ sta1 * nsat + sat1 ] ) == -1 ) continue;
					sta1p_e = ipmax;
					for( i = sta1 * nsat + sat1 + 1; i < nsta * nsat; i++ ) {
						if( psadd[ i ] != -1 ) { sta1p_e = psadd[ i ]; break; }
					}
					if(( sta2p = psadd[ sta2 * nsat + sat1 ] ) == -1 ) continue;
					sta2p_e = ipmax;
					for( i = sta2 * nsat + sat1 + 1; i < nsta * nsat; i++ ) {
						if( psadd[ i ] != -1 ) { sta2p_e = psadd[ i ]; break; }
					}
					for( ips1 = sta1p; ips1 < sta1p_e; ips1++ ) {
						if( mbs->pass.data[ips1].nd == 1 ||
							mbs->pass.data[ips1].mwv > 1.0 ) continue;
						for(ips2 = sta2p; ips2 < sta2p_e; ips2++ ) {
							if( mbs->pass.data[ips2].nd == 1 ||
								mbs->pass.data[ips2].mwv > 1.0 ) continue;

							if( !check_oltime( &mbs->pass.data[ips1].ts,
											   &mbs->pass.data[ips1].te,
											   &mbs->pass.data[ips2].ts,
											   &mbs->pass.data[ips2].te,
											   opt->mopt.minddpass,
											   &st1, &et1 )) {
								continue;
							}

							/* [6]�q��2���� */
							for( sat2 = sat1 + 1; sat2 < nsat; sat2++ ) {
								if(( sta3p = psadd[ sta1 * nsat + sat2 ] ) == -1 ) continue;
								sta3p_e = ipmax;
								for( i = sta1 * nsat + sat2 + 1; i < nsta * nsat; i++ ) {
									if( psadd[ i ] != -1 ) { sta3p_e = psadd[ i ]; break; }
								}
								if(( sta4p = psadd[ sta2 * nsat + sat2 ] ) == -1 ) continue;
								sta4p_e = ipmax;
								for( i = sta2 * nsat + sat2 + 1; i < nsta * nsat; i++ ) {
									if( psadd[ i ] != -1 ) { sta4p_e = psadd[ i ]; break; }
								}
								for( ips3 = sta3p; ips3 < sta3p_e; ips3++ ) {
									if( mbs->pass.data[ips3].nd == 1 ||
										mbs->pass.data[ips3].mwv > 1.0 ) continue;
									for( ips4 = sta4p; ips4 < sta4p_e; ips4++ ) {
										if( mbs->pass.data[ips4].nd == 1 ||
											mbs->pass.data[ips4].mwv > 1.0 ) continue;

										if( !check_oltime( &mbs->pass.data[ips3].ts,
														   &mbs->pass.data[ips3].te,
														   &mbs->pass.data[ips4].ts,
														   &mbs->pass.data[ips4].te,
														   opt->mopt.minddpass,
														   &st2, &et2 )) {
											continue;
										}

										/* [7]�q���g�������� */
										if( !check_oltime( &st1, &et1, &st2, &et2,
											 opt->mopt.minddpass, &st3, &et3 )) {
											continue;
										}

										/* [8]�p�X�g�����e�[�u���쐬 */
										ddtb[indd*4+0] = ips1;
										ddtb[indd*4+1] = ips2;
										ddtb[indd*4+2] = ips3;
										ddtb[indd*4+3] = ips4;
										indd++;
										if( indd >= inddm ) {
											inddm += 100;
											if( !(ddtbw = (int *)realloc( ddtb, inddm * 4 *
												   sizeof(int)))) {
												showmsg( "realloc error : ddtb" );
												trace( 1, "realloc error : ddtb\n" );
												free( psadd );
												return 1;
											}
											ddtb = ddtbw;
										}
									}
								}
							}
						}
					}
				}
			}
		}
		free( psadd );
		if (indd==0) trace(1,
				"no dd combination is detected. "
				"check [Mininum path length DD] at setting3 dialog.\n"
				"process is proceeded with no dd fixiation.\n");

		/* [9]��d���A���r�M���C�e�B�v��s��̍쐬 */
		Hdd[0] = Hdd[3] =  1.0;
		Hdd[1] = Hdd[2] = -1.0;
		matmul( "TN", 4, 4, 1, opt->mopt.wdd, Hdd, Hdd, 0.0, HwH );
	}
	if (indd==0) return 0;	/* skip DD fixiation if no DD available */

	for (i=mbs->param.iamb;i<mbs->param.ng;i++) {
		trace(TRDIS,"#AMB=%3d SIG=%10.2f\n",
				i-mbs->param.iamb,
				sqrt(mbs->neq->pgmat[i+i*mbs->param.ng])/0.10695337814214668464);
	}
	trace(TRDIS,"number of double difference %d\n",indd);

	/* [10]�p�X�e�[�u������ */
	//trace( 2, "indd=%d\n", indd );
	//trace(TRENA,"%d] R1 R2 S1 S2 FracBw  StdBw  P0_Bw / -    R1 R2 S1 S2 FracBn  StdBn  P0_Bn CorrBc[m]\n",itr+1);
	trace( 2, "#DD = %d\n", indd );
	trace(TRENA,"%02d R1 R2 S1 S2 FracBw  StdBw  P0_Bw / -    R1 R2 S1 S2 FracBn  StdB1  P0_B1  dBc[m]\n",itr+1);
    for( i = 0; i < indd; i ++ ) {
		trace( 5, "ddtb[%d]:%d,%d,%d,%d\n", i, ddtb[i*4+0], ddtb[i*4+1],
			  ddtb[i*4+2], ddtb[i*4+3] );
		trace( 5, "sta=%d,%d,%d,%d:sat=%d,%d,%d,%d\n", mbs->pass.data[ddtb[i*4+0]].sta,
			  mbs->pass.data[ddtb[i*4+1]].sta, mbs->pass.data[ddtb[i*4+2]].sta,
			  mbs->pass.data[ddtb[i*4+3]].sta, mbs->pass.data[ddtb[i*4+0]].sat,
			  mbs->pass.data[ddtb[i*4+1]].sat, mbs->pass.data[ddtb[i*4+2]].sat,
			  mbs->pass.data[ddtb[i*4+3]].sat );

		/* [11]MW������d���Z�o */
		ddmw = mbs->pass.data[ddtb[i*4+0]].mw - mbs->pass.data[ddtb[i*4+1]].mw
			 -(mbs->pass.data[ddtb[i*4+2]].mw - mbs->pass.data[ddtb[i*4+3]].mw);
		//ddmwv = mbs->pass.data[ddtb[i*4+0]].mwv + mbs->pass.data[ddtb[i*4+1]].mwv
		//	   +mbs->pass.data[ddtb[i*4+2]].mwv + mbs->pass.data[ddtb[i*4+3]].mwv;
		pd=mbs->pass.data+ddtb[i*4];
		ddmwv=pd[0].mwv/pd[0].wsum+pd[1].mwv/pd[1].wsum
			 +pd[2].mwv/pd[2].wsum+pd[3].mwv/pd[3].wsum;

		/* [12]MW�����̓�d���̐����� */
		ddmwfix = ROUND(ddmw);
		if( ddmwv < 1e-30 ) {
			showmsg( "ddmwv sigma = 0" );
			trace( 2, "ddmwv sigma = 0\n" );
			continue;
		}
		stat=check_intdd( opt->mopt.wlddfix, ddmw, ddmwfix, ddmwv, &p0 );
		//trace(TRENA,"WL %2d %2d %2d %2d %6.2f %6.2f %6.4f / ",
		//		mbs->pass.data[ddtb[i*4+0]].sta,
		//		mbs->pass.data[ddtb[i*4+3]].sta,
		//		mbs->pass.data[ddtb[i*4+0]].sat,
		//		mbs->pass.data[ddtb[i*4+3]].sat,ddmw-ddmwfix,sqrt(ddmwv),p0);
		trace(TRENA,"WL %2d %2d %2d %2d %6.2f %6.2f %6.4f / ",
				mbs->pass.data[ddtb[i*4+0]].sta,
				mbs->pass.data[ddtb[i*4+3]].sta,
				mbs->pass.data[ddtb[i*4+0]].sat,
				mbs->pass.data[ddtb[i*4+3]].sat,
				ddmw-ddmwfix,sqrt(ddmwv),p0);
//		if (!stat) continue;
		if (!stat) {
			trace(TRENA,"SKIP NL FIX (POOR WL PROBABILITY)\n",p0);
			continue;
		}
		nfw++;

		/* [13]L1��d���̎Z�o */
		iam=mbs->param.iamb;
		f1 = CLIGHT / mbs->navs->lam[ mbs->pass.data[ddtb[i*4+1]].sat - 1][0];
		f2 = CLIGHT / mbs->navs->lam[ mbs->pass.data[ddtb[i*4+1]].sat - 1][1];
		bc =(mbs->param.par[iam+ddtb[i*4+0]] + mbs->param.dx[iam+ddtb[i*4+0]]
		   - mbs->param.par[iam+ddtb[i*4+1]] - mbs->param.dx[iam+ddtb[i*4+1]])
		   -(mbs->param.par[iam+ddtb[i*4+2]] + mbs->param.dx[iam+ddtb[i*4+2]]
		   - mbs->param.par[iam+ddtb[i*4+3]] - mbs->param.dx[iam+ddtb[i*4+3]]);
		bc /= CYC2M;
		c1=f1/(f1+f2); c2=f2/(f1-f2);
		/* b1 = ( f1 + f2 ) / f1 * bc - f2 / ( f1 - f2 ) * ddmwfix */
		b1=bc/c1-c2*ddmwfix;
		/* Extract DD variance from inv(Ng) */
		//b1v = mbs->neq->pgmat[(iam+ddtb[i*4+0])*mbs->param.ng+(iam+ddtb[i*4+0])]
		//	+ mbs->neq->pgmat[(iam+ddtb[i*4+1])*mbs->param.ng+(iam+ddtb[i*4+1])]
		//	+ mbs->neq->pgmat[(iam+ddtb[i*4+2])*mbs->param.ng+(iam+ddtb[i*4+2])]
		//	+ mbs->neq->pgmat[(iam+ddtb[i*4+3])*mbs->param.ng+(iam+ddtb[i*4+3])];
        for (j=0;j<4;j++) ih[j]=iam+ddtb[j+i*4];
		acov_get(mbs->neq->pgmat,mbs->param.ng,ih,4,cov);
		b1v = ddvar(cov);
		b1v /= pow(c1*CYC2M,2);
		if (b1v>2.0) {
			trace(TRENA,"SKIP NL FIX (NL STD = %9.2fC)\n",sqrt(b1v));
			continue;
		}

		/* [14]L1��d���̐����� */
		b1fix = ROUND( b1 );
		if( b1v < 1e-30 ) {
//			showmsg( "b1v sigma = 0" );
			trace( 2, "b1v sigma = 0\n" );
			continue;
		}
		stat=check_intdd( opt->mopt.l1ddfix, b1, b1fix, b1v, &p0 );

		/* [15]��d��LC�A���r�M���C�e�BFIX���̎Z�o */
		/* bcf = f1/(f1+f2) * (b1 + f2/(f1-f2) * bw) */
		bcfix = c1 * (b1fix + c2*ddmwfix);
		bcfix *= CYC2M;

		/* [16]���K�������s��֔��f */
		bc *= CYC2M;
		trace(TRENA,"NL %2d %2d %2d %2d %6.2f %6.2f %6.4f %7.4f\n",
				mbs->pass.data[ddtb[i*4+0]].sta,
				mbs->pass.data[ddtb[i*4+3]].sta,
				mbs->pass.data[ddtb[i*4+0]].sat,
				mbs->pass.data[ddtb[i*4+3]].sat,b1-b1fix,sqrt(b1v),p0,bcfix-bc);
		if (fabs(b1-b1fix)>0.1) continue;
		if (!stat) continue;
		nfn++;
		trace( 5, "est_ddlcambi():bcfix=%e,bc=%e,bcfix-bc=%e\n", bcfix, bc, bcfix - bc );

		for (j=0;j<4;j++) ih[j]=iam+ddtb[j+i*4];
		update_nomeqmat(mbs,Hdd,ih,4,bcfix-bc,opt->mopt.wdd); mbs->neq->m--;
		mbs->neq->mdd++;

	} /* ���̂Q�d���g������ */
	time2str(timeget(),cwrk,TIMEDIGIT);
	trace(1,"%s [%d] FIXR (WL/NL) = %5.1f/%5.1f %%\n",
			cwrk,itr+1,nfw*100.0/indd,nfn*100.0/indd);

	/* [17]���K�������s������� */
	rc = est_globalparam(mbs);

	/* [18]�I������ */
	return rc;

}

/* 2�̋@�ւ̃I�[�o�[���b�v���� */
int check_oltime( gtime_t *st1, gtime_t *et1, gtime_t *st2, gtime_t *et2,
				  double oltime, gtime_t *maxst, gtime_t *minet ) {

	/* [1]�J�n�����̔�r */
	if( timediff( *st1, *st2 ) > 0.0 ) memcpy( maxst, st1, sizeof( gtime_t ));
	else memcpy( maxst, st2, sizeof( gtime_t ));

	/* [2]�I�������̔�r */
	if( timediff( *et1, *et2 ) > 0.0 ) memcpy( minet, et2, sizeof( gtime_t ));
	else memcpy( minet, et1, sizeof( gtime_t ));

	/* [3]�d������ */
	if( timediff( *minet, *maxst ) >= oltime ) return 1; /* �d������ */
	else return 0; /* �d���Ȃ� */

}

/*
 * �֐����F est_epochparam
 * �T�v  �F �G�|�b�N�p�����[�^���菈��
 *   IN  �F mbs_t    *mbs    ���������͍\����
 *       �F prcopt_t *prcopt ���������͓��I�v�V�����\����
 *   OUT �F int �X�e�[�^�X (0:ok, 0>:error, 1:aborted)
 */
int est_epochparam(mbs_t *mbs, prcopt_t *prcopt) {

	/* [1] �ϐ��錾�������� */
	double *inv_ne = NULL, *ngeq = NULL, *beeq = NULL, *xe = NULL;
	int    ng, ne;

	int    i, iep, ret = 0;
	FILE   *fp;
	char  cwrk[256];
	trace( 3, "est_epochparam:\n" );

	/* [2] �S�G�|�b�N���[�v */
	for (iep = 0; iep < mbs->param.ne; iep++) {

		sprintf( cwrk, "%s.%d", prcopt->mopt.eptmp, iep );
		if ((fp = fopen(cwrk, "rb")) == NULL) {
			showmsg("eptmp file fopen error %s", cwrk);
			trace(1,"eptmp file fopen error %s\n", cwrk);
			return 1;
		}

		/* [3] �ꎞ�t�@�C���ǂݏo�� */
		/* �s��T�C�Y�ǂݏo�� */
		if ((fread(&ng, sizeof(int), 1, fp)) != 1) {
			ret = 1;
		}
		else if ((fread(&ne, sizeof(int), 1, fp)) != 1) {
			ret = 1;
		}

		/* �̈�m�� */
		else if ((inv_ne = zeros(ne,ne)) == NULL) {
			ret = 1;
		}
		else if ((ngeq   = zeros(ne,ng)) == NULL) {
			free(inv_ne);
			ret = 1;
		}
		else if ((beeq   = zeros(ne,1)) == NULL) {
			free(inv_ne); free(ngeq);
			ret = 1;
		}
		else {
			;
		}
		if (ret == 1)
		{
			showmsg("eptmp file malloc error");
			trace(1,"eptmp file malloc error\n");
			free(inv_ne); free(ngeq); free(beeq);
			fclose(fp);
			return ret;
		}

		if ((fread(inv_ne, (sizeof(double) * ne * ne), 1, fp)) != 1) {
			ret = 1;
		}

		else if ((fread(ngeq, (sizeof(double) * ne * ng), 1, fp)) != 1) {
			ret = 1;
		}

		else if ((fread(beeq, (sizeof(double) * ne), 1, fp)) != 1) {
			ret = 1;
		}
		else {
			;
		}

		if (ret == 1) {
			showmsg("eptmp file fread error");
			trace(1,"eptmp file fread error\n");
			free(inv_ne); free(ngeq); free(beeq); fclose(fp);
			return ret;
		}

		/* [4] �G�|�b�N�p�����[�^���� */
		if( !(xe = zeros(ne,1))) {
			showmsg("eptmp file malloc error");
			trace(1,"eptmp file malloc error\n");
			free(inv_ne); free(ngeq); free(beeq); fclose(fp);
			return ret;
		}

		matmul("TN", ne, 1, ng, -1.0, ngeq,   mbs->param.dx, 1.0, beeq);
		matmul("NN", ne, 1, ne,  1.0, inv_ne, beeq,          0.0, xe);

		/* [5] ����l���f */
		memcpy((mbs->param.dx + ng + iep * ne), xe, (sizeof(double) * ne));

		for (i = 0; i < (ne * ne); i++) {
			mbs->neq->pemat[iep * ne * ne + i] = mbs->neq->j / ( mbs->neq->m + mbs->neq->mdd )
					* inv_ne[i];
		}

		free(xe);
		free(beeq);
		free(ngeq);
		free(inv_ne);
		fclose(fp);
		remove( cwrk );
	}
	return ret;
}

/*
 * �֐����F update_param
 * �T�v  �F ����p�����[�^�X�V
 *   I/O �F mbs_t     *mbs     ���������͍\����
 */
void update_param(mbs_t *mbs) {

	int i, j, iep;
	int na = 0;
	trace( 3, "update_param:\n" );

	for( i = 0; i < mbs->param.na; i++ ) {
		if( i < mbs->param.ng ) {
			mbs->param.pg[i] = sqrt( mbs->neq->pgmat[ i * mbs->param.ng + i ] );
		}
		else {
			iep = floor(( i - mbs->param.ng ) / mbs->param.nep );
			j = ( i - mbs->param.ng ) % mbs->param.nep;
			mbs->param.pe[i - mbs->param.ng] = sqrt( mbs->neq->pemat[ iep *
				mbs->param.nep * mbs->param.nep + j * mbs->param.nep + j ] );
		}
	}

	/* ����p�����[�^�X�V */
	na = mbs->param.na;
	for (i = 0; i < na; i++) {
		trace(3,"DX,%d,%d,%e+%e=>%e\n",itr+1,i,mbs->param.par[i],mbs->param.dx[i],mbs->param.par[i]+mbs->param.dx[i]);
		mbs->param.par[i] += mbs->param.dx[i];
	 	trace(TRDIS,"DX,%d,%d,%e\n",itr+1,i,mbs->param.dx[i]);
	 //	trace(3,"DX,%d,%d,%e,%e\n",itr+1,i,mbs->param.par[i],mbs->param.dx[i]);
	}

	return;

}

/*
 * �֐����F check_itr_conv
 * �T�v  �F �G�|�b�N�p�����[�^���菈��
 *   IN  �F mbs_t *mbs ���������͍\����
 *          prcopt_t *opt ���������\����
 *   OUT �F int ���茋�ʁi0:�p���A1:�����A2:�ł��؂�)
 */
int check_itr_conv(mbs_t *mbs,
				  prcopt_t *opt){

	double jnew = 0.0;
	int ret = 0;
	trace( 3, "check_itr_conv:\n" );

	/* �Ăяo���񐔂��J�E���g�A�b�v */
	itr++;

	/* �C�^���[�V������������l����������ꍇ�A�����Ɣ��� */
	jnew = sqrt( mbs->neq->j / ( mbs->neq->m + mbs->neq->mdd ));
	trace( 5, "itr=%d,jpast=%f,jnew=%f,(jnew-jpast)/jnew=%f,itrconv=%f\n",
			itr, jpast, jnew, fabs((jnew-jpast)/jnew), opt->mopt.itrconv );
	if(fabs((jnew-jpast)/jnew) < opt->mopt.itrconv){
		ret = 1;

	/* �C�^���[�V�����ő�񐔂𒴉� */
	}else if(itr >= opt->mopt.itrmax){
		ret = 2;

	/* ��L�ȊO�͌p�� */
	}else{
		jpast = jnew;
		ret = 0;
	}

	trace( 5, "check_itr_conv : ret=%d\n", ret );
	return ret;
}

/*-----------------------------------------------------------------------------
* 	save estimated positions and standard deviations
* 	in the form of ECEF and geographic.
*----------------------------------------------------------------------------*/
int pos_save(const mbs_t *mbs, const char *outdir)
{
	FILE *fp;
	char filename[MAX_FILEPATH];
	double ggc[3],cove[6],covg[6];
	int i;
	trace(3,"pos_save:\n");

	strcpy(filename,outdir);
	strcat(filename,FILENAME_POS);
	if (!(fp=fopen(filename,"w"))) return 0;

	/* output ecef coordinates and stds */
	fprintf(fp,"# Estimated coordinates (ECEF)\n");
	fprintf(fp,"# ID        X  (m)        Y  (m)        Z  (m)  dX(m)  dY(m)  dZ(m)\n");
	for (i=0;i<mbs->stas.nsta;i++) {
		fprintf(fp,"%-4s %13.4f %13.4f %13.4f %.4f %.4f %.4f\n",
				mbs->stas.sta[i].name2,
				mbs->param.par[i*3],mbs->param.par[i*3+1],mbs->param.par[i*3+2],
				mbs->param.pg [i*3],mbs->param.pg [i*3+1],mbs->param.pg [i*3+2]);
	}

	/* output geographic coordinates and stds */
	fprintf(fp,"# Estimated coordinates (geographic, GRS80 ellipsoid)\n");
	fprintf(fp,"# ID    Lat.(deg.)     Lon.(deg.)    Height(m)  dN(m)  dE(m)  dU(m)\n");
	for (i=0;i<mbs->stas.nsta;i++) {
		pcov_get(mbs->neq->pgmat,mbs->param.ng,i*3,3,cove);
		cov2ecef(mbs->param.par+i*3,cove,ggc,covg,RE_GRS80,FE_GRS80,XYZ_TO_PLH);
		fprintf(fp,"%-4s %13.9f %14.9f %12.4f %.4f %.4f %.4f\n",
				mbs->stas.sta[i].name2,
				ggc[0]*R2D,ggc[1]*R2D,ggc[2],
				sqrt(covg[0]),sqrt(covg[1]),sqrt(covg[2]));
	}

	fclose(fp);
	return 1;
}

/* save title part of multi baseline analysis result file */
static int savesol_title(FILE *fp, const mbs_t *mbs, const filopt_t *fopt, const prcopt_t *popt)
{
	int k,n;
	double dms[3],B0,L0;
	time_t timer;
	struct tm *t_st;
	trace( 3, "savesol_title:\n" );

	/* ��b���o�� */
	fprintf( fp, "CD_FileType=%s\n", "4" );						/* �t�@�C���^�C�v */
	fprintf( fp, "AC_Title=%s\n", "�O�����ԕ��όv�Z" );			/* �^�C�g�� */
	fprintf( fp, "AC_AreaName=\n");								/* �n�於 */
	fprintf( fp, "AC_EllipsoidLongRadius=%.1f\n", RE_GRS80 );	/* �ȉ~�̌��q�����a */
	fprintf( fp, "AC_Ellipticity=%.9f\n", RFE_GRS80 );			/* �G���� */
	if( mbs->neq->m == 0 ){
		showmsg("error : no observation data");
		trace(1,"no observation data\n");
		return 0;
	}
	else{
		fprintf( fp, "AC_UnitWeightSD=%.10e\n", sqrt(mbs->neq->j / mbs->neq->m) );/* �P�ʏd�ʓ�����̕W���΍� */
	}
	fprintf( fp, "AC_VarCovDN=%f\n", popt->mopt.sigr[0] );		/* ���U�E�����U�l dN */
	fprintf( fp, "AC_VarCovDE=%f\n", popt->mopt.sigr[1] );		/* ���U�E�����U�l dE */
	fprintf( fp, "AC_VarCovDU=%f\n", popt->mopt.sigr[2] );		/* ���U�E�����U�l dU */
	fprintf( fp, "AC_ScaleCorrection=%.10e\n", 0.0 );			/* �X�P�[���␳�l */

	/* B0, L0 : average of base station coordinates */
	for (B0=L0=0.0,n=0,k=0;k<mbs->stas.nsta;k++) {
		if (mbs->stas.sta[k].id==1) continue;		/* skip rover */
		B0+=mbs->stas.sta[k].plh[0];
		L0+=mbs->stas.sta[k].plh[1];
		n++;
	}
	deg2dms(B0/n*R2D,dms);
	fprintf( fp, "AC_B0=%.0f�x%2.0f��%9.6f�b\n", dms[0],dms[1],dms[2] ); /* B0 */
	deg2dms(L0/n*R2D,dms);
	fprintf( fp, "AC_L0=%.0f�x%2.0f��%9.6f�b\n", dms[0],dms[1],dms[2] ); /* L0 */

	fprintf( fp, "AC_RotationHorizontal=%f\n", 0.0 );	/* �����ʓ��̉�] */
	fprintf( fp, "AC_XI=%f\n", 0.0 );					/* �� */
	fprintf( fp, "AC_ETA=%f\n", 0.0 );					/* �� */

	/* print analysis condition */
	if (mbs->semi != NULL) {
		fprintf( fp, "AC_CalculationCondition=%s\n",
				"�W�I�C�h�␳����A�������΍�����Ȃ��A��]����Ȃ��A�X�P�[������Ȃ�" );
	}else{
		fprintf( fp, "AC_CalculationCondition=%s\n",
				"�W�I�C�h�␳�Ȃ��A�������΍�����Ȃ��A��]����Ȃ��A�X�P�[������Ȃ�" );
	}

	/* print geoid file name */
	if (mbs->semi == NULL) {
		fprintf( fp, "AC_GeoidName=\n" );
	}
	else if (fopt->geoid[0] == '\0') {
		fprintf( fp, "AC_GeoidName=%s\n", "Internal" );
	}
	else {
		fprintf( fp, "AC_GeoidName=%s\n", basename_mp(fopt->geoid) );
	}

	/* print semi-dynamic parameter file name */
	fprintf( fp, "AC_SemiDynamicCorrect=%s\n", basename_mp(popt->mopt.ifsdp) );

	time(&timer);
	t_st = localtime(&timer);
	fprintf( fp, "AC_CalculationDate=%d�N%2d��%2d��\n",
				t_st->tm_year+1900,t_st->tm_mon+1,t_st->tm_mday );	/* �v�Z�� */

	fprintf( fp, "AC_ProgramManager=%s\n", PROGRAM_MANAGER );		/* �v���O�����Ǘ��� */
	return 1;
}

/* save known point part of multi baseline analysis result file */
static int savesol_knownp(FILE *fp, const mbs_t *mbs)
{
	int i,icnt;
	double dms[3];
	sta_t *sta1;
	trace( 3, "savesol_knownp:\n" );

	/* ���m�_���o�� */
	for (i=0,icnt=0;icnt<mbs->stas.nsta;icnt++) if (mbs->stas.sta[icnt].id==2) i++;
	fprintf( fp, "AC_NumberKnownPoints=%d\n", i );

	i = 0;
	for( icnt = 0; icnt < mbs->stas.nsta; icnt++ ) {
		sta1=mbs->stas.sta+icnt;
		if( sta1->id != 2 ) continue;

		fprintf( fp, "AC_KnownPointNumber[%d]=%s\n", i, sta1->name2 );	/* �_�ԍ� */
		fprintf( fp, "AC_KnownPointName[%d]=%s\n", i, sta1->name3 );	/* �_���� */
		if (mbs->semi != NULL) {
			/* ���͒l */
			deg2dms(sta1->plh0[0]*R2D,dms);
			fprintf( fp, "AC_InputB[%d]=%.0f�x%2.0f��%9.6f�b\n", i, dms[0],dms[1],dms[2] ); /* ���͒l B */
			deg2dms(sta1->plh0[1]*R2D,dms);
			fprintf( fp, "AC_InputL[%d]=%.0f�x%2.0f��%9.6f�b\n", i, dms[0],dms[1],dms[2] ); /* ���͒l L */
			fprintf( fp, "AC_InputEllipsoidHeight[%d]=%f\n", i, sta1->plh0[2] ); /* ���͒l �ȉ~�̍� */
			fprintf( fp, "AC_InputGeoidHeight[%d]=%f\n", i, sta1->plh0[2] - sta1->plh0[3] ); /* ���͒l �W�I�C�h�� */
			fprintf( fp, "AC_InputTrueHeight[%d]=%f\n", i, sta1->plh0[3] ); /* ���͒l �W�� */
			/* �␳�� */
			fprintf( fp, "AC_CorrectInputB[%d]=%f\n", i, ( sta1->plh[0] - sta1->plh0[0] ) * R2D * 3600 ); /* �␳�� B */
			fprintf( fp, "AC_CorrectInputL[%d]=%f\n", i, ( sta1->plh[1] - sta1->plh0[1] ) * R2D * 3600 ); /* �␳�� L */
			fprintf( fp, "AC_CorrectInputEllipsoidHeight[%d]=%f\n", i, sta1->plh[2] - sta1->plh0[2] ); /* �␳�� �ȉ~�̍� */
		}
		else {
			/* ���͒l */
			fprintf( fp, "AC_InputB[%d]=%s\n", i, "0.0" );				/* ���͒l B */
			fprintf( fp, "AC_InputL[%d]=%s\n", i, "0.0" );				/* ���͒l L */
			fprintf( fp, "AC_InputEllipsoidHeight[%d]=%f\n", i, 0.0 );	/* ���͒l �ȉ~�̍� */
			fprintf( fp, "AC_InputGeoidHeight[%d]=%f\n", i, 0.0 );		/* ���͒l �W�I�C�h�� */
			fprintf( fp, "AC_InputTrueHeight[%d]=%f\n", i, 0.0 );		/* ���͒l �W�� */
			/* �␳�� */
			fprintf( fp, "AC_CorrectInputB[%d]=%f\n", i, 0.0 );			/* �␳�� B */
			fprintf( fp, "AC_CorrectInputL[%d]=%f\n", i, 0.0 );			/* �␳�� L */
			fprintf( fp, "AC_CorrectInputEllipsoidHeight[%d]=%f\n", i, 0.0 );	/* �␳�� �ȉ~�̍� */

		}
		/* �������W */
		deg2dms(sta1->plh[0]*R2D,dms);
		fprintf( fp, "AC_CurrentB[%d]=%.0f�x%2.0f��%9.6f�b\n", i, dms[0],dms[1],dms[2] ); /* �������W B */
		deg2dms(sta1->plh[1]*R2D,dms);
		fprintf( fp, "AC_CurrentL[%d]=%.0f�x%2.0f��%9.6f�b\n", i, dms[0],dms[1],dms[2] ); /* �������W L */
		fprintf( fp, "AC_CurrentEllipsoidHeight[%d]=%f\n", i, sta1->plh[2] );	/* �������W �ȉ~�̍� */
		i++;
	}
	return 1;
}

/* save new point part of multi baseline analysis result file */
static int savesol_newp(FILE *fp, const mbs_t *mbs)
{
	int j,icnt,icnt2;
	double solcov[6],pp[3],ppbk[3],pc[6],dms[3],hg;
	sta_t *sta1;
	trace( 3, "savesol_newp:\n" );

	/* �V�_���o�� */
	for (j=0,icnt=0;icnt<mbs->stas.nsta;icnt++) if (mbs->stas.sta[icnt].id==1) j++;
	fprintf( fp, "AC_NumberNewPoints=%d\n", j );

	for (j=0, icnt = 0; icnt < mbs->stas.nsta; icnt++ ) {
		sta1=mbs->stas.sta+icnt;
		if( sta1->id != 1 ) continue;

		/* ����l��GRS80�ȉ~�̂ł̒n�����W */
		pcov_get(mbs->neq->pgmat,mbs->param.ng,j*3,3,solcov);
		cov2ecef(&mbs->param.par[icnt*3],solcov,pp,pc,RE_GRS80,FE_GRS80,XYZ_TO_PLH);
		/* �_�ԍ��A�_���� */
		fprintf( fp, "AC_NewPointNumber[%d]=%s\n", j, sta1->name2 );
		fprintf( fp, "AC_NewPointName[%d]=%s\n", j, sta1->name3 );
		/* ���W�ߎ��l */
		deg2dms(mbs->stas.sta[icnt].plh[0]*R2D,dms);
		fprintf( fp, "AC_ApproxB[%d]=%.0f�x%2.0f��%9.6f�b\n", j, dms[0],dms[1],dms[2] ); /* ���W�ߎ��l B */
		deg2dms(mbs->stas.sta[icnt].plh[1]*R2D,dms);
		fprintf( fp, "AC_ApproxL[%d]=%.0f�x%2.0f��%9.6f�b\n", j, dms[0],dms[1],dms[2] ); /* ���W�ߎ��l L */
		fprintf( fp, "AC_ApproxEllipsoidHeight[%d]=%f\n", j, sta1->plh[2] ); /* ���W�ߎ��l �ȉ~�̍� */
		/* �␳�� */
		fprintf( fp, "AC_CorrectApproxB[%d]=%f\n", j, ( pp[0] - sta1->plh[0] ) * R2D * 3600 ); /* �␳�� B */
		fprintf( fp, "AC_CorrectApproxL[%d]=%f\n", j, ( pp[1] - sta1->plh[1] ) * R2D * 3600 ); /* �␳�� L */
		fprintf( fp, "AC_CorrectApproxEllipsoidHeight[%d]=%f\n", j, pp[2] - sta1->plh[2] ); /* �␳�� �ȉ~�̍� */
		/* ���W�Ŋm�l */
		deg2dms(pp[0]*R2D,dms);
		fprintf( fp, "AC_OptimulB[%d]=%.0f�x%2.0f��%9.6f�b\n", j, dms[0],dms[1],dms[2] ); /* ���W�œK�l B */
		deg2dms(pp[1]*R2D,dms);
		fprintf( fp, "AC_OptimulL[%d]=%.0f�x%2.0f��%9.6f�b\n", j, dms[0],dms[1],dms[2] ); /* ���W�œK�l L */
		fprintf( fp, "AC_OptimulEllipsoidHeight[%d]=%f\n", j, pp[2] ); /* ���W�œK�l �ȉ~�̍� */
		/* �W���΍� */
		fprintf( fp, "AC_SDB[%d]=%f\n", j, sqrt(pc[0]) ); /* �W���΍� B */
		fprintf( fp, "AC_SDL[%d]=%f\n", j, sqrt(pc[1]) ); /* �W���΍� L */
		fprintf( fp, "AC_SDEllipsoidHeight[%d]=%f\n", j, sqrt(pc[2]) ); /* �W���΍� �ȉ~�̍� */
		fprintf( fp, "AC_SDMS[%d]=%f\n", j, sqrt(pc[0]+pc[1]) ); /* �W���΍� MS */
		fprintf( fp, "AC_SDMH[%d]=%f\n", j, sqrt(pc[2]) ); /* �W���΍� MH */

		if (mbs->semi != NULL){
			for( icnt2 = 0; icnt2 < 3; icnt2++ ) ppbk[icnt2] = pp[icnt2];
			/* �����ւ̕␳�� */
			semi_corr(mbs->semi,pp,TOD2EP);
			hg=geoidh(pp);
			fprintf( fp, "AC_CorrectOriginB[%d]=%f\n", j, ( pp[0] - ppbk[0] ) * R2D * 3600 ); /* �����ւ̕␳�� B */
			fprintf( fp, "AC_CorrectOriginL[%d]=%f\n", j, ( pp[1] - ppbk[1] ) * R2D * 3600 ); /* �����ւ̕␳�� L */
			fprintf( fp, "AC_CorrectOriginEllipsoidHeight[%d]=%f\n", j, pp[2] - ppbk[2] ); /* �����ւ̕␳�� �ȉ~�̍� */
			/* �������W */
			deg2dms(pp[0]*R2D,dms);
			fprintf( fp, "AC_OriginB[%d]=%.0f�x%2.0f��%9.6f�b\n", j, dms[0],dms[1],dms[2] ); /* �������W B */
			deg2dms(pp[1]*R2D,dms);
			fprintf( fp, "AC_OriginL[%d]=%.0f�x%2.0f��%9.6f�b\n", j, dms[0],dms[1],dms[2] ); /* �������W L */
			fprintf( fp, "AC_OriginEllipsoidHeight[%d]=%f\n", j, pp[2] ); /* �������W �ȉ~�̍� */
			fprintf( fp, "AC_OriginGeoidHeight[%d]=%f\n", j, hg ); /* �������W �W�I�C�h�� */
			fprintf( fp, "AC_OriginTrueHeight[%d]=%f\n", j, pp[2] - hg ); /* �������W �W�� */
		}else{
			/* �����ւ̕␳�� */
			fprintf( fp, "AC_CorrectOriginB[%d]=%f\n", j, 0.0 ); /* �����ւ̕␳�� B */
			fprintf( fp, "AC_CorrectOriginL[%d]=%f\n", j, 0.0 ); /* �����ւ̕␳�� L */
			fprintf( fp, "AC_CorrectOriginEllipsoidHeight[%d]=%f\n", j, 0.0 ); /* �����ւ̕␳�� �ȉ~�̍� */
			/* �������W */
			fprintf( fp, "AC_OriginB[%d]=%s\n", j, "0.0" ); /* �������W B */
			fprintf( fp, "AC_OriginL[%d]=%s\n", j, "0.0" ); /* �������W L */
			fprintf( fp, "AC_OriginEllipsoidHeight[%d]=%f\n", j, 0.0 ); /* �������W �ȉ~�̍� */
			fprintf( fp, "AC_OriginGeoidHeight[%d]=%f\n", j, 0.0 ); /* �������W �W�I�C�h�� */
			fprintf( fp, "AC_OriginTrueHeight[%d]=%f\n", j, 0.0 ); /* �������W �W�� */

		}
		j++;
	}
	return 1;
}

/* save baseline part of multi baseline analysis result file */
static int savesol_baseline(FILE *fp, const mbs_t *mbs)
{
	int i,j,k,ii,ix[MAXRCV];
	double dobs[3],dest[3],cov1[6];
	sta_t *sta1,*sta2;
	trace( 3, "savesol_baseline:\n" );

	/* ����x�N�g�� */
	k=mbs->stas.nsta*(mbs->stas.nsta-1)/2;
	fprintf( fp, "AC_NumberBaselines=%d\n", k ); /* ����x�N�g�� */

	/* make pivot table ix[] in order of base station and rover */
	k=0;
	for (i=0;i<mbs->stas.nsta;i++) if (mbs->stas.sta[i].id==1) ix[k++]=i;
	for (i=0;i<mbs->stas.nsta;i++) if (mbs->stas.sta[i].id==2) ix[k++]=i;

	k = 0;
	for (i=0;i<mbs->stas.nsta-1;i++) {
		sta1=mbs->stas.sta+ix[i];
		for (j=i+1;j<mbs->stas.nsta;j++) {
			sta2=mbs->stas.sta+ix[j];

			/* calculate initial and estimated baseline vector */
			for (ii=0;ii<3;ii++) {
				dobs[ii]=sta2->pos[ii]-sta1->pos[ii];
				dest[ii]=mbs->param.par[ix[j]*3+ii]-mbs->param.par[ix[i]*3+ii];
			}

			/* small variance replaced by zero */
			for (ii=0;ii<6;ii++) cov1[ii]=(fabs(sta2->cov[ii])>COV_THRES?sta2->cov[ii]:0.0);

			/* output baseline information */
			fprintf( fp, "AC_StartPointNumber[%d]=%s\n", k, sta1->name2 ); /* �N�_�ԍ� */
			fprintf( fp, "AC_StartPointName[%d]=%s\n",   k, sta1->name3 ); /* �N�_���� */
			fprintf( fp, "AC_EndPointNumber[%d]=%s\n",   k, sta2->name2 ); /* �I�_�ԍ� */
			fprintf( fp, "AC_EndPointName[%d]=%s\n",     k, sta2->name3 ); /* �I�_���� */
			/* �ϑ��l */
			fprintf( fp, "AC_ObservationDeltaX[%d]=%f\n", k, dobs[0] ); /* �ϑ��l ��X */
			fprintf( fp, "AC_ObservationDeltaY[%d]=%f\n", k, dobs[1] ); /* �ϑ��l ��Y */
			fprintf( fp, "AC_ObservationDeltaZ[%d]=%f\n", k, dobs[2] ); /* �ϑ��l ��Z */
			/* ���U�E�����U */
			fprintf( fp, "AC_VarCovDXDX[%d]=%e\n", k, cov1[0] ); /* ���U�E�����U ��X*��X */
			fprintf( fp, "AC_VarCovDXDY[%d]=%e\n", k, cov1[3] ); /* ���U�E�����U ��X*��Y */
			fprintf( fp, "AC_VarCovDXDZ[%d]=%e\n", k, cov1[5] ); /* ���U�E�����U ��X*��Z */
			fprintf( fp, "AC_VarCovDYDY[%d]=%e\n", k, cov1[1] ); /* ���U�E�����U ��Y*��Y */
			fprintf( fp, "AC_VarCovDYDZ[%d]=%e\n", k, cov1[4] ); /* ���U�E�����U ��Y*��Z */
			fprintf( fp, "AC_VarCovDZDZ[%d]=%e\n", k, cov1[2] ); /* ���U�E�����U ��Z*��Z */
			/* ���ϒl */
			fprintf( fp, "AC_AverageDeltaX[%d]=%f\n", k, dest[0] ); /* ���ϒl ��X */
			fprintf( fp, "AC_AverageDeltaY[%d]=%f\n", k, dest[1] ); /* ���ϒl ��Y */
			fprintf( fp, "AC_AverageDeltaZ[%d]=%f\n", k, dest[2] ); /* ���ϒl ��Z */
			/* �c�� */
			fprintf( fp, "AC_ResidualDeltaX[%d]=%f\n", k, dest[0]-dobs[0] ); /* �c�� ��X */
			fprintf( fp, "AC_ResidualDeltaY[%d]=%f\n", k, dest[1]-dobs[1] ); /* �c�� ��Y */
			fprintf( fp, "AC_ResidualDeltaZ[%d]=%f\n", k, dest[2]-dobs[2] ); /* �c�� ��Z */
			k++;
		}
	}
	return 1;
}

/* ���όv�Z��p��͌��ʏo�� */
static int savesol( mbs_t *mbs, filopt_t *fopt, prcopt_t *popt, const char *outdir ) {

	FILE *fp;
	char path[MAX_FILEPATH];
	int stat;
	trace( 3, "savesol:\n" );

	/* ���όv�Z��p�t�@�C���̃I�[�v�� */
	strcpy(path,outdir);
	strcat( path, FILENAME_SOL );
	if(( fp = fopen( path, "w" )) == NULL ) {
		showmsg("error : cannot open solution output file");
		trace(1,"cannot open solution output file\n");
		return( 1 );
	}

	stat=1;
	if (stat) stat=savesol_title   (fp,mbs,fopt,popt);
	if (stat) stat=savesol_knownp  (fp,mbs);
	if (stat) stat=savesol_newp    (fp,mbs);
	if (stat) stat=savesol_baseline(fp,mbs);

	/* �t�@�C���N���[�Y */
	fclose( fp );

	return (!stat);
}

/* ���������͌��ʏo�� */
static int mbsoutsol( mbs_t *mbs, filopt_t *fopt, prcopt_t *popt, solopt_t *sopt, const char *outdir ) {

int it;
int itp=0;
double tr;
double ts;
int iobs=0;
int i0;
int i;
int ir=0;
int is=0;
int js=0;
int irp=0;
	int jobs[MAXSAT]={0};

	trace( 3, "mbsoutsol:\n" );

	if (!pos_save(mbs,outdir)) return 1;

	/* [1]���������̓t�@�C���̏o�� */
	if( savesol( mbs, fopt, popt, outdir )) return( 1 );

	/* [2]FCB�o�� */
	/* if( savefcb( mbs, fopt )) return( 1 ); */


	if((sopt->recclout) || (sopt->satclout))
	{
		rtkopencrinex(popt, sopt, mbs, FILENAME_CRINEX, mbs->stas.sta);

		while(iobs < mbs->obss->n )
		{
			it = (int)floor((timediff(mbs->obss->data[iobs].time, mbs->ts) + DTTOL) / mbs->ti);
			ir = mbs->obss->data[iobs].rcv;
			is = mbs->obss->data[iobs].sat;

			if(sopt->satclout)
			{
				if(itp!=it)
				{
					for(js=0;js<MAXSAT;js++)
					{
						if(jobs[js]==0) continue;
						ts=popt->mopt.estsatclk ? mbs->param.par[ICLS(mbs->param, js, itp)]/CLIGHT : 0.0;
						outsolcrinexsat(&mbs->obss->data[jobs[js]].time, js+1, ts);
						jobs[js]=0;
					}
				}
			}

			if(sopt->recclout)
			{
				if((irp!=ir) || (itp!=it))
				{
					tr=mbs->param.par[ICLR(mbs->param,ir-1, it)]/CLIGHT;
					outsolcrinexrec(&mbs->obss->data[iobs].time, mbs->stas.sta[ir-1].name2, tr);
		 //			outsolcrinexrec(&mbs->obss->data[iobs].time, "ISB", mbs->param.par[mbs->param.iisbP + ir - 1]/CLIGHT*1.0e9);
					irp = ir;
				}
			}
			jobs[is-1]=iobs;
			itp=it;
			++iobs;
		}
		if(sopt->satclout)
		{
			for(js=0;js<MAXSAT;js++)
			{
				if(jobs[js]==0) continue;
				ts=popt->mopt.estsatclk ? mbs->param.par[ICLS(mbs->param, js, itp)]/CLIGHT : 0.0;
				outsolcrinexsat(&mbs->obss->data[jobs[js]].time, js+1, ts);
				jobs[js]=0;
			}
		}
		rtkclosecrinex();
	}
	outisbtable(popt, sopt, NULL, mbs);
	outgl2table(popt, sopt, NULL, mbs);

//	if( outsolsnxm()) return 1;



	return( 0 );
}

/* �q�@����� */
int readnavclk( char **infile, int n, nav_t *nav ) {

	/* [0]�ϐ���` */
	int i;
	gtime_t t={0,0.0};
	trace( 3, "readnavclk:\n" );

	/* [1]�q�@��\���̂̏����� */
	nav->eph = NULL; nav->n = nav->nmax = 0;
	if( nav->geph ) free( nav->geph ); nav->geph = NULL; nav->ng = nav->ngmax = 0;
	if( nav->seph ) free( nav->seph ); nav->seph = NULL; nav->ns = nav->nsmax = 0;

	/* [2]�t�@�C������ */
	for( i = 0; i < n; i++ ) {
		if( readrnxt( infile[i], 0, t, t, 0.0,"", NULL, nav, NULL ) < 0 ) {
			showmsg( "insufficient memory" );
			trace( 1, "insufficient memory\n" );
			return( 0 );
		}
	}

	/* [3]�q�@����͊m�F */
	if( nav->n <= 0 && nav->ng <= 0 && nav->ns <= 0 ) return( 0 );

	/* [4]�d���q�@��폜 */
	uniqnav( nav );

	/* [5]�I������ */
	return( 1 );
}

/*
 * �֐����F stas_update
 * �T�v  �F �Ǐ��X�V����
 *   I/O �F mbs_t *mbs ���������͍\����
 *   IN  �F prcopt_t *popt ���������\����
 *   IN  �F filopt_t *fopt �t�@�C�����\����
 *   OUT �F sta_t *sta1 �ǘa��
 *   OUT �F int �����X�e�[�^�X
 */
static int stas_update( mbs_t *mbs, const prcopt_t *popt, filopt_t *fopt, sta_t *sta1 ) {

	/* [0]�ϐ��̒�` */
	int i;
	double neur[3] = { SIG_ROV,  SIG_ROV,  SIG_ROV  };
	double neub[3] = { SIG_BASE, SIG_BASE, SIG_BASE };
	double rr[4], xyz[3];
	char jpn[MAXANT];
	int stat=0;
	trace( 3, "stas_update:\n" );

	/* [1]�ǌ덷�̐ݒ� */
	if( sta1->id == 1 ){
		for( i = 0; i < 3; i++ ) {
			if( popt->mopt.sigr[i] == 0.0 ) sta1->sig[i] = neur[i];
			else sta1->sig[i] = popt->mopt.sigr[i];
		}
	}
	else {
		for( i = 0; i < 3; i++ ) {
			if( popt->mopt.sigb[i] == 0.0 ) sta1->sig[i] = neub[i];
			else sta1->sig[i] = popt->mopt.sigb[i];
		}
	}

	/* [2]�ǈʒu�t�@�C���Ǎ�����̏��� */
//	if( stapos_read(fopt->stapos, sta1->name2, rr, jpn )){
	if( getstapos(fopt->stapos, sta1->name2, xyz, jpn )){
		ecef2pos(xyz, rr);
		strcpy(sta1->name3, jpn);
		if (mbs->semi) {
			rr[3]=rr[2];						/* backup altitude */
			rr[2]+=geoidh(rr);					/* calculate elipsoidal height */
			matcpy(sta1->plh0,rr,4,1);			/* save (lat,lon,he,alt) at epoch */
			semi_corr(mbs->semi,rr,EP2TOD);		/* transform epoch to TOD */
		}
		matcpy(sta1->plh,rr,3,1);				/* save (lat,lon,he) at TOD */
		cov2ecef(sta1->plh,NULL,sta1->pos,NULL,RE_GRS80,FE_GRS80,PLH_TO_XYZ);
		stat=1;
		trace( 3, "station position [%s] : position file\n", sta1->name2 );
	}

	/* [3]RINEX�w�b�_�̏��� */
	else if( norm( sta1->pos, 3 ) > POS_THRES ) {

		/* GRS80�ȉ~�̂ł̒n�����W */
		cov2ecef(sta1->pos,NULL,sta1->plh,NULL,RE_GRS80,FE_GRS80,XYZ_TO_PLH);
		stat=2;
		trace( 3, "station position[%s] : rinex header\n", sta1->name2 );
	}

	return stat;
}

/* detect cycle slip by LLI --------------------------------------------------*/
static void detslp_ll2(ssat_t *ssat, const obsd_t *obs, int n, const prcopt_t *opt)
{
	int i,j;

	trace(3,"detslp_ll2: n=%d\n",n);

	for (i=0;i<n&&i<MAXOBS;i++) for (j=0;j<opt->nfreq;j++) {
		if (obs[i].L[j]==0.0||!(obs[i].LLI[j]&3)) continue;

		trace(2,"detslp_ll2: slip detected rcv=%03d sat=%03d f=%d\n",obs[i].rcv,obs[i].sat,j+1);

		ssat[obs[i].sat-1].slip[j]=1;
	}
}

/* detect cycle slip by geometry free phase jump -----------------------------*/
static void detslp_gf2(ssat_t *ssat, const obsd_t *obs, int n, const nav_t *nav, const prcopt_t *opt)
{
	double g0,g1 = 0.0;
	int i,j;

	trace(3,"detslp_gf2: n=%d\n",n);

	for (i=0;i<n&&i<MAXOBS;i++) {

		if ((g1=gfmeas(obs+i,nav))==0.0) continue;

		g0=ssat[obs[i].sat-1].gf;
		ssat[obs[i].sat-1].gf=g1;
		/*
		* --- original ---
		* trace(4,"detslp_gf2: sat=%2d gf0=%8.3f gf1=%8.3f\n",obs[i].sat,g0,g1);
		*/
		trace(4,"detslp_gf2: time=%s sat=%2d gf0=%8.3f gf1=%8.3f\n",time_str(obs[i].time,0),obs[i].sat,g0,g1);
		if (g0!=0.0&&fabs(g1-g0)>opt->thresslip) {
		/*
		* --- original ---
			trace(2,"detslp_gf2: slip detected rcv=%03d sat=%03d gf=%.3f (%8.3f->%8.3f)\n",
				  obs[i].rcv,obs[i].sat,fabs(g1-g0),g0,g1);
		*/
			trace(2,"detslp_gf2: slip detected time=%s rcv=%03d sat=%03d gf=%.3f (%8.3f->%8.3f)\n",
				  time_str(obs[i].time,0),obs[i].rcv,obs[i].sat,fabs(g1-g0),g0,g1);
			for (j=0;j<opt->nfreq;j++) ssat[obs[i].sat-1].slip[j]|=1;
		}
	}
}

/* �P�Ǖ��̊ϑ��f�[�^���� */
static int readobsrcv( mbs_t *mbs, const prcopt_t *popt, filopt_t *fopt, char **infile, int *index, int nn, int r ) {

	/* [0]�ϐ���` */
	obs_t *obs0 = NULL;
	obsd_t obs[MAXOBS];
	double mw[MAXOBS], mwv[MAXOBS], lcamb[MAXOBS], dtr;
	unsigned char code[MAXOBS][NFREQ+NEXOBS];
	char msg[128];
	double rs[MAXOBS*6], dts[MAXOBS*2], var[MAXOBS], e[3], azel[MAXOBS*2];
	int i, j, n, ep, svh[MAXOBS];
	int ap, padr[MAXSAT], s;
	int nep = 0;
	double avepos[3] = { 0.0 };
	int it, ad;
	double d;
	int nobs, iobss;
	double zazel[]={0.0,PI/2.0},cov[6]={0};
	ssat_t ssat[MAXSAT];
	sol_t sol={{0}};
	sta_t *sta1=mbs->stas.sta+r;
	int stat;
	trace( 3, "readobsrcv: rcv=%s\n", mbs->stas.sta[r].name2 );

	showmsg("pre-processing obs %d/%d",r+1,mbs->stas.nsta);


	for( i = 0; i < MAXSAT; i++ ) padr[i] = -1;

	for( i = 0; i < MAXOBS; i++ ) {
		mw[i] = 0.0;
		mwv[i] = 0.0;
		memset( code[i], 0, NFREQ+NEXOBS );
	}

	/* [1]����RINEX�ǂݍ��݃��[�v */
	obs0 = (obs_t *)calloc( sizeof( obs_t ), 1 );
	for( i = 0; i < nn; i++ ) {
		if( index[i] == r + 1 ) {
			if( readrnxt( infile[i], r + 1, mbs->ts, mbs->te, 0.0,"", obs0, NULL, sta1 ) < 0 ) {
				showmsg( "insufficient memory" );
				trace( 1, "insufficient memory\n" );
				free( obs0->data ); free( obs0 );
				return( 0 );
			}
		}
	}

	/* update station structure */
	stat=stas_update( mbs, popt, fopt, sta1 );

	/* [2]�ǂݍ��݃`�F�b�N */
	if( obs0->n <= 0 ) {
		showmsg( "no obs data (%s)", mbs->stas.sta[r].name2 );
		trace( 1, "no obs data (%s)\n", mbs->stas.sta[r].name2 );
		free( obs0->data ); free( obs0 );
		return( 0 );
	}

	/* [3]�ϑ��f�[�^�\�[�g */
	sortobs( obs0 );

	/* [5]�G�|�b�N���[�v */
	iobss = 0; /* inputobs2()�p�I�t�Z�b�g */
	while(( nobs = inputobs2( obs0, obs, r + 1, &iobss )) > 0 ) {

		/* [51]�q���ʒu�̎擾 */
		satposs( obs[0].time, obs, nobs, mbs->navs, popt->sateph, rs, dts, var, svh);

		/* [52]�q���̐H���o */
		testeclipse( obs, nobs, mbs->navs, rs );

		/* [53]�ΏۊO�q���̊Ԉ��� */
		for( i = j = 0; i < nobs; i++ ) {
			if(( satsys( obs[i].sat, NULL ) & popt->navsys ) &&
				popt->exsats[ obs[i].sat - 1 ] != 1 &&
				svh[i] >= 0 && rs[i*6] != 0.0 &&
				obs[i].L[0] != 0.0 && obs[i].L[1] != 0.0 &&
				obs[i].P[0] != 0.0 && obs[i].P[1] != 0.0 ) {
				    memcpy(obs+j,obs+i,sizeof(obsd_t));
					matcpy(rs+j*6,rs+i*6,6,1);
                    matcpy(dts+j*2,dts+i*2,2,1);
                    var[j]=var[i];
                    j++;
			}
		}
		if( j <= 0 ) continue;
		n = j;

		/* last resort to get station position */
		if (!stat) {
			if (!pntpos(obs,n,mbs->navs,popt,&sol,azel,ssat,msg)) continue;
			matcpy(sta1->pos,sol.rr,3,1);
			cov2ecef(sta1->pos,NULL,sta1->plh,NULL,RE_GRS80,FE_GRS80,XYZ_TO_PLH);
			stat=3;
		}

		/* [54]MW���^�����̌v�Z */
		for( i = 0; i < n; i++ ) {
			geodist( rs + i * 6, sta1->pos, e );
			satazel( sta1->plh, e, obs[i].azel );
#if 0
			trace(5,"r=%d,i=%d,plh=%f,%f,%f\n",r,i,mbs->stas.sta[r].plh[0],mbs->stas.sta[r].plh[1],mbs->stas.sta[r].plh[2]);
			trace(5,"rs=%f,%f,%f,%f,%f,%f\n",rs[i*6],rs[i*6+1],rs[i*6+2],rs[i*6+3],rs[i*6+4],rs[i*6+5]);
			trace(5,"e=%f,%f,%f,azel=%f,%f\n",e[0],e[1],e[2],obs[i].azel[0],obs[i].azel[1]);
#endif
			mwmeas( popt, obs + i, mbs->navs, sta1, mw + i, mwv + i, lcamb + i, code[i]);
		}

		/* [55]�X���b�v�J�E���^�̃[���N���A */
		for( i = 0; i < MAXSAT; i++ ) for ( j = 0; j < NFREQ; j++ ) {
			ssat[i].slip[j] = 0;
		}

		/* [56]LLI�ɂ��T�C�N���X���b�v���o */
		detslp_ll2( ssat, obs, n, popt );

		/* [57]LG�W�����v�ɂ��T�C�N���X���b�v���o */
	//	detslp_gf2( ssat, obs, n, mbs->navs, popt );
		detionslip( ssat, obs, n, mbs->navs, popt );

		/* [58]�ϑ��f�[�^���[�v */
		for( i = 0; i < n; i++ ) {

			/* [581]�ʃp�X�\���̂̒ǉ� */
			s = obs[i].sat;
			ap = 0;

			/* first pass of satellite s */
			if( padr[s-1] == -1 ) {
				ap = 1;
			}
			/* next pass candidate of satellite s */
			else if(
				timediff( obs[i].time, mbs->pass.data[padr[s-1]].te ) > MIN_ARC_GAP || /* apart from latest pass */
				ssat[obs[i].sat-1].slip[0] != 0 ||                 	/* L1 slip */
				ssat[obs[i].sat-1].slip[1] != 0 ||                      /* L2 slip */
				code[i][0] != mbs->pass.data[padr[s-1]].code[0] ||      /* L1 code differ latest pass */
				code[i][1] != mbs->pass.data[padr[s-1]].code[1]		/* L2 code differ latest pass */
				 ) {
				ap = 1;
			}

			if( ap ) {
				padr[s-1] = addpass( &mbs->pass, obs[i].time, s, r, code[i] );
			}

			/* [582]�ʃp�X�\���̂̍X�V */
			trace(9,"PID,%d,%f,%f\n",padr[s-1],mw[i],lcamb[i]);
			setpass( mbs->pass.data + padr[s-1], obs[i].time, mw[i],
					lcamb[i], obs[i].azel[1] );
		}

		/* [59]�����m�F */
		if( !screent( obs[0].time, mbs->ts, mbs->te, mbs->ti )) continue;

		/* [5A]�P�Ƒ��� */
		if( !pntpos( obs, n, mbs->navs, popt, &sol, azel, ssat, msg )) continue;
		dtr = timediff( obs[0].time, sol.time );
		if( fabs( dtr ) > CLK_ERR_LIMIT ) continue;
		ep = (int)floor(( timediff( obs[0].time, mbs->ts ) + DTTOL ) / mbs->ti );
		sta1->dtr[ep] = dtr;
		for( i = 0; i < 3; i++ ) avepos[i] += sol.rr[i];
		nep++;

		/* [5B]�q���N���b�N�̕ۑ� */
		if( popt->mopt.estsatclk ) {
			for( i = 0; i < n; i++ ) {
				it = (int)floor(( timediff( obs[i].time, mbs->ts ) + DTTOL ) / mbs->ti );
				ad = it + ( obs[i].sat - 1 ) * mbs->param.ne;
			    par_wk[ad] = dts[ i * 2 ];
			}
		}

		/* [5C]�ϑ��f�[�^�̒ǉ� */
		for( i = 0; i < n; i++ ) {
			addobsdata( mbs->obss, obs + i );
		}

		/* [5D]�����m�F */
		if( !screent( obs[0].time, mbs->ts, mbs->te, popt->mopt.tiztd )) continue;

		/* [5E]�Η����V���x�������l�̌v�Z */
        d = sbstropcorr( obs[0].time, sta1->plh, zazel, var );
		it = (int)floor(( timediff( obs[0].time, mbs->ts ) + DTTOL ) / popt->mopt.tiztd );
		sta1->ztd0[it] = d;
		sta1->ztdv[it] = *var;

	}

	/* [6]�Ǐ��\���̂̍X�V */
	if (stat==3) {
		for(i=0;i<3;i++) sta1->pos[i]=avepos[i]/nep;
		cov2ecef(sta1->pos,NULL,sta1->plh,NULL,RE_GRS80,FE_GRS80,XYZ_TO_PLH);
	}
	if (stat) {
		for (i=0;i<3;i++) cov[i]=pow(sta1->sig[i],2.0);
		cov2ecef(sta1->plh,cov,sta1->pos,sta1->cov,RE_GRS80,FE_GRS80,PLH_TO_XYZ);
	}
	if (stat>1) {
		matcpy(sta1->plh0,sta1->plh,3,1);
		semi_corr(mbs->semi,sta1->plh0,TOD2EP);
		sta1->plh0[3]=sta1->plh0[2]-(mbs->semi?geoidh(sta1->plh0):0.0);
	}

	/* [7]�I������ */
	free( obs0->data );
	free( obs0 );
	if(nep==0) trace(2,"no valid epoch: %s\n",sta1->name2);
	fprintf(stderr,"%s ",sta1->name2);
	return( 1 );
}

/* �ϑ��f�[�^���� */
int readobs( mbs_t *mbs, prcopt_t *popt, filopt_t *fopt, char **infile, int *index, int n ) {

	/* [0]�ϐ���` */
	int i;
	trace( 3, "readobs:\n" );

	/* [1]�\���̂̏����� */
	mbs->obss->data = NULL; mbs->obss->n = mbs->obss->nmax = 0;
	mbs->pass.data = NULL; mbs->pass.n = mbs->pass.nmax = 0;

	/* [2]�ǃ��[�v */
	for( i = 0; i < mbs->stas.nsta; i++ ) {
		readobsrcv( mbs, popt, fopt, infile, index, n, i );
	}
	for (i=0;i<mbs->pass.n;i++) {
		//trace(2,"PASS_RAW,%3d,%d,%5.0f,%3d,%2d,%7.2f,%7.2f,%7.2f,%7.2f\n",
		//		i,mbs->pass.data[i].ts.time,
		//		timediff(mbs->pass.data[i].te,mbs->pass.data[i].ts),
		//		mbs->pass.data[i].sat,mbs->pass.data[i].sta,
		//		mbs->pass.data[i].mw,mbs->pass.data[i].mwv,
		//		mbs->pass.data[i].lcamb,mbs->pass.data[i].lcambv);
		trace(2,"PASS_RAW,%3d,%d,%5.0f,%3d,%2d,%7.2f,%7.2f,%7.2f,%7.2f\n",
				i,mbs->pass.data[i].ts.time,
				timediff(mbs->pass.data[i].te,mbs->pass.data[i].ts),
				mbs->pass.data[i].sat,mbs->pass.data[i].sta,
				mbs->pass.data[i].mw,sqrt(mbs->pass.data[i].mwv),
				mbs->pass.data[i].lcamb,sqrt(mbs->pass.data[i].lcambv));
	}

	/* [3]�ϑ��f�[�^�̃\�[�g */
	sortobs( mbs->obss );

	/* [3.5] delete short pass */
	pass_select(&mbs->pass,popt->mopt.minpass);

	/* [4]�p�X�̃\�[�g */
	sortpass( &mbs->pass );

	/* [5]�I������ */
	return( 1 );
}

/* �Ǐ��O���� */
int stainfopre( char **infile, int *index, int n, char *rov, char *base, stas_t *stas, char ifile[][1024], int *idx, int *m ) {

	/* [0]�ϐ��̒�` */
	int nsta = 0, k;
	char *dumc, *p;
	gtime_t tz = {0,0.0};
	char *rcv[2], key[2][3] = { "%r", "%b" };
	int i, j;
	trace( 3, "stainfopre:\n" );

	/* [1]�ړ��ǁE��n�ǃ}�N���W�J */
	rcv[0] = rov;
	rcv[1] = base;
	*m = 0;
	for( j = 0; j < 2; j++ ) {
		if( rcv[j] ) {
			if( !(dumc = (char *)malloc( strlen( rcv[j] ) + 1 ))) return( 0 );
			strcpy( dumc, rcv[j] );
			p = strtok( dumc, " " );
			while( p ) {
				k = 0;
				for( i = 0; i < n; i++ ) {
					if( strstr( infile[i], key[j] )) {
						k = i;
						if( !j ) reppath( infile[i], ifile[*m], tz, p, "" );
						else reppath( infile[i], ifile[*m], tz, "", p );
						idx[(*m)++] = nsta + 1;
					}
				}
				stas->sta[nsta].id = index[k];
				strcpy( stas->sta[nsta++].name2, p );
				p = strtok( NULL, " " );
			}
			free( dumc );
		}
	}

	/* [2]�I������ */
	stas->nsta = nsta;
	return( 1 );
}
//
///*
// * �֐����F stapos_read
// * �T�v  �F �ǈʒu�t�@�C���Ǎ�2
// *   IN  �F char *file �ǈʒu�t�@�C���̃p�X��
// *   IN  �F char *name �ǖ�
// *   OUT �F double *r �ǂݍ��񂾋ǈʒu(�ܓx,�o�x,�ȉ~�̍�)
// *                    [radians,radians,m]
// *   OUT �F char *name3 �ǘa��
// *   OUT �F int �Ǎ��X�e�[�^�X(0:�ǈʒu�Ȃ�,1:����)
// */
//int stapos_read(char *file, char *name, double *r, char *name3) {
//
//	FILE *fp;
//	char buff[512],sname[256]={0},*p,*q;
//	char jpname[MAXANT];
//	double xyz[3]={0},llh[3]={0};
//	char stf[512]={0};
//	char c[500];
//
//	trace(3,"stapos_read: file=%s name=%s\n",file,name);
//
//	if (!(fp=fopen(file,"r"))) {
//		trace(1,"station position file open error: %s\n",file);
//		return 0;
//	}
//	if(!fgets(buff,sizeof(buff),fp)) {
//		trace(2,"no station position: %s %s\n",name,basename_mp(file));
//	}
//	if (0==strncmp(buff,"SITE",4)) {
//		fgets(buff,sizeof(buff),fp);
//		while (fgets(buff,sizeof(buff),fp)) {
//			if(strlen(buff) < 250) continue;
//
//			strncpy(sname, buff, 4);
//			for (p=sname,q=name;*p&&*q;p++,q++) {
//				if (toupper((int)*p)!=toupper((int)*q)) break;
//			}
//			if ((!*p)&&(!*q)) {
//				strncpy(name3,buff+6,16);
//				name3[16]='\0';
//				trim(name3);
//				sprintf(stf, "%13.13s\0", buff+140 );
//				xyz[0] = atof(stf);
//				sprintf(stf, "%13.13s\0", buff+155 );
//				xyz[1] = atof(stf);
//				sprintf(stf, "%13.13s\0", buff+170 );
//				xyz[2] = atof(stf);
//				ecef2pos(xyz, llh);
//				r[0]=llh[0];
//				r[1]=llh[1];
//				fclose(fp);
//				return 1;
//			}
//		}
//	}
//	else {
//		rewind(fp);
//		while (fgets(buff,sizeof(buff),fp)) {
//			if ((p=strchr(buff,'%'))) *p='\0';
//
//			if (sscanf(buff,"%lf %lf %lf %s %s",r,r+1,r+2,sname,jpname)<5) continue;
//
//			for (p=sname,q=name;*p&&*q;p++,q++) {
//				if (toupper((int)*p)!=toupper((int)*q)) break;
//			}
//			if ((!*p)&&(!*q)) {
//				r[0]*=D2R;
//				r[1]*=D2R;
//				strcpy(name3,jpname);
//				fclose(fp);
//				return 1;
//			}
//		}
//	}
//	fclose(fp);
//	sprintf(c,"%s", basename_mp(file));
//	trace(2,"no station position: %s %s\n",name,basename_mp(file));
//	return 0;
//
//}


/******************** �ȉ��A���[�J���֐� ********************/

/* �p�X�\���̃\�[�g */
int mbssortpass( const void *p1, const void *p2 ) {
	passd_t *data1 = (passd_t *)p1;
	passd_t *data2 = (passd_t *)p2;
	char c1[256], c2[256];
	int i1, i2, rc;

	/* �ǃ\�[�g */
	i1 = ((passd_t *)p1)->sta;
	i2 = ((passd_t *)p2)->sta;
	if(( rc = ( i1 - i2 )) != 0 ) return( rc );

	/* �q���\�[�g */
	i1 = ((passd_t *)p1)->sat;
	i2 = ((passd_t *)p2)->sat;
	if(( rc = ( i1 - i2 )) != 0 ) return( rc );

	/* �����\�[�g */
	time2str( data1->ts, c1, 0 );
	time2str( data2->ts, c2, 0 );

	return( strcmp( c1, c2 ));
}

/* �̈��� */
void mbsfree( mbs_t *mbs ) {
	int i;
	trace(3,"mbsfree:\n");

	if( mbs ) {
		if( mbs->semi ) {
			if( mbs->semi->data ) free( mbs->semi->data );
			free( mbs->semi );
		}
		if( mbs->neq ) {
			if( mbs->neq->ngeq ) free( mbs->neq->ngeq );
			if( mbs->neq->bgeq ) free( mbs->neq->bgeq );
			if( mbs->neq->neeq ) free( mbs->neq->neeq );
			if( mbs->neq->beeq ) free( mbs->neq->beeq );
			if( mbs->neq->ngeeq ) free( mbs->neq->ngeeq );
			if( mbs->neq->pgmat ) free( mbs->neq->pgmat );
			if( mbs->neq->pemat ) free( mbs->neq->pemat );
			free( mbs->neq );
		}
		if( mbs->fcb ) {
			if( mbs->fcb->nlfcb ) free( mbs->fcb->nlfcb );
			if( mbs->fcb->sdd ) free( mbs->fcb->sdd );
			free( mbs->fcb );
		}
		if( mbs->pass.data ) free( mbs->pass.data );
		if( mbs->param.par ) free( mbs->param.par );
		if( mbs->param.sig ) free( mbs->param.sig );
		if( mbs->param.stncov ) free( mbs->param.stncov );
		if( mbs->param.par0 ) free( mbs->param.par0 );
		if( mbs->param.sig0 ) free( mbs->param.sig0 );
		if( mbs->param.stncov0 ) free( mbs->param.stncov0 );
		if( mbs->param.dx ) free( mbs->param.dx );
		if( mbs->param.pg ) free( mbs->param.pg );
		if( mbs->param.pe ) free( mbs->param.pe );
		if( mbs->stas.sta ) {
			for( i = 0; i < MAXRCV; i++ ) {
				if( mbs->stas.sta[i].ztd0 ) free( mbs->stas.sta[i].ztd0 );
				if( mbs->stas.sta[i].ztdv ) free( mbs->stas.sta[i].ztdv );
				if( mbs->stas.sta[i].dtr ) free( mbs->stas.sta[i].dtr );
			}
			free( mbs->stas.sta );
		}
		if( mbs->obss ) {
			if( mbs->obss->data ) free( mbs->obss->data );
			free( mbs->obss );
		}
		if( mbs->sbss ) free( mbs->sbss );
		if( mbs->lexs ) free( mbs->lexs );
		free( mbs );
	}

	if( ddtb ) {
		free( ddtb );
		ddtb = NULL;
	}
	if( rtk ) {
		rtkfree( rtk );
		free( rtk );
		rtk = NULL;
	}

	return;

}

/* search next observation data index ----------------------------------------*/
int nextobsf2(const obs_t *obs, int *i, int rcv)
{
	double tt;
	int n;

	for (;*i<obs->n;(*i)++) if (obs->data[*i].rcv==rcv) break;
	for (n=0;*i+n<obs->n;n++) {
		tt=timediff(obs->data[*i+n].time,obs->data[*i].time);
		if (obs->data[*i+n].rcv!=rcv||tt>DTTOL) break;
	}
	return n;
}

/* precise tropospheric model ------------------------------------------------*/
double prectrop2(gtime_t time, const double *pos, const double *azel,
					   const prcopt_t *opt, const double *x, double *dtdx,
					   double *var)
{
	const double zazel[]={0.0,PI/2.0};
	double zhd,m_h,m_w,cotz,grad_n,grad_e;

	/* zenith hydrostatic delay */
	zhd=tropmodel(time,pos,zazel,0.0);

	/* mapping function */
	m_h=tropmapf(time,pos,azel,&m_w);

	if (opt->tropopt>=TROPOPT_ESTG&&azel[1]>0.0) {

		/* m_w=m_0+m_0*cot(el)*(Gn*cos(az)+Ge*sin(az)): ref [6] */
		cotz=1.0/tan(azel[1]);
		grad_n=m_w*cotz*cos(azel[0]);
		grad_e=m_w*cotz*sin(azel[0]);
		m_w+=grad_n*x[1]+grad_e*x[2];
		dtdx[1]=grad_n*(x[0]-zhd);
		dtdx[2]=grad_e*(x[0]-zhd);
	}
	dtdx[0]=m_w;
	*var=SQR(0.01);
	return m_h*zhd+m_w*(x[0]-zhd);
}

/*-----------------------------------------------------------------------------
*	check and modify processing option for multi-baseline static
*	return : 1:OK,0:NG
*----------------------------------------------------------------------------*/
static int check_opt(gtime_t ts, gtime_t te, double ti, prcopt_t *popt,
		char **infile, const char *rov, const char *base)
{
	int stat;
	char *msg=NULL;
	char msg1[MAXERRMSG];

	if      (ts.time==0)						msg="[Time Start] is mandatory";
	else if (te.time==0)						msg="[Time End] is mandatory";
	else if (ti<=0.0)							msg="[Interval] must be a multiple of data interval";
	else if (!strstr(infile[0],"%r"))			msg="[RINEX OBS: Rover] must contain %%r macro";
	else if (!strstr(infile[1],"%b"))			msg="[RINEX OBS: Base Station] must contain %%b macro";
	else if (fmod(popt->mopt.tiztd,ti)!=0.0)	msg="[Setting3/estimate interval of troposphere (zenith)] must be a multiple of [Interval]";
	else if (fmod(popt->mopt.tigra,ti)!=0.0)	msg="[Setting3/estimate interval of troposphere (EW/NS)] must be a multiple of [Interval]";
	else if (popt->mopt.sigtrop[0]<=0.0)		msg="[Setting3/Trop. random walk sigma (Zen/EW/NS) must be lager than zero]";
	else if (popt->mopt.sigtrop[1]<=0.0)		msg="[Setting3/Trop. random walk sigma (Zen/EW/NS) must be lager than zero]";
	else if (popt->mopt.sigtrop[2]<=0.0)		msg="[Setting3/Trop. random walk sigma (Zen/EW/NS) must be lager than zero]";
	else if (popt->mopt.wdd<=0.0)				msg="[Setting3/weight of DD-LC ambiguity] must be larger than zero";
	else if (popt->mopt.sigr[0]<=0.0)			msg="[Setting3/Mobile station deviation (dN/dE/dU)] must be larger than zero";
	else if (popt->mopt.sigr[1]<=0.0)			msg="[Setting3/Mobile station deviation (dN/dE/dU)] must be larger than zero";
	else if (popt->mopt.sigr[2]<=0.0)			msg="[Setting3/Mobile station deviation (dN/dE/dU)] must be larger than zero";
	else if (popt->mopt.sigb[0]<=0.0)			msg="[Setting3/Base station deviation (dN/dE/dU)] must be larger than zero";
	else if (popt->mopt.sigb[1]<=0.0)			msg="[Setting3/Base station deviation (dN/dE/dU)] must be larger than zero";
	else if (popt->mopt.sigb[2]<=0.0)			msg="[Setting3/Base station deviation (dN/dE/dU)] must be larger than zero";
	else if (!rov  || rov[0] =='\0')			msg="[Misc/Rovers] must contain at least one station";
	else if (!base || base[0]=='\0')			msg="[Misc/Base Stations] must contain at least one station";

	if (msg) {	/* option error */
		stat=0;
		strcpy(msg1,msg); strcat(msg1,"\n");
		showmsg(msg);
		trace(1,msg1);
	}
	else {		/* option normal */
		stat=1;
   //		popt->navsys=SYS_GPS;			/* GPS only */
		popt->nfreq=2;					/* iono-free */
		popt->ionoopt=IONOOPT_IFLC;		/* iono-free */
	}

	return stat;
}
/* ���������� */
int mbstatic( gtime_t ts, gtime_t te, double ti, prcopt_t *popt, solopt_t *sopt,
			  filopt_t *fopt, int flag, char **infile, int *index, int n,
			  char *rov, char *base, nav_t *navs, pcvs_t *pcvsr,
			  pcvs_t *pcvss ) {
	int i;
	int istat = 0;
	mbs_t *mbs = NULL;  /* ���������͍\���� */
	char cwrk1[256],outdir[MAX_FILEPATH],tracef[MAX_FILEPATH];
	gtime_t gt;

	/* multiple session is not supported */
	if (flag==0) {
		showmsg("multiple session is not supported");
		trace(1,"multiple session is not supported\n");
		return 0;
	}

	/* �f�o�b�O�o�͐ݒ� */
	strcpy(outdir,popt->mopt.ofdir);
	strcpy(tracef,outdir);
    strcat(tracef,FILENAME_TRACE);
	traceopen( tracef );
	tracelevel( sopt->trace );

	if (!check_opt(ts,te,ti,popt,infile,rov,base)) {	/* check processing option */
		traceclose();
		return 1;
	}

	time2str(timeget(),cwrk1,TIMEDIGIT);
    trace(1,"%s mbstatic start\n",cwrk1);

	/* read error model */
	if (popt->errmodel==ERRMODEL_TABLE && readerr(popt->mopt.iferr,navs)) {
		trace(2,"warning: no error model file\n");
	}

	/* index�ԍ��ύX */
	for( i = 0; i < n; i++) index[i] += 1;

	/* ���������͍\���̂̏����� */
	if( !( mbs = (mbs_t *)mbsnew( ts, te, ti, navs, pcvsr, pcvss, popt ))) istat = 1;

	/* �����O���̓��� */
	if( !istat ) readpreceph( infile, n, popt, mbs->navs, mbs->sbss, mbs->lexs );

	/* �Z�~�_�C�i�~�b�N�␳�f�[�^�̓��� */
	if( !istat ) {
		if( !( mbs->semi = semi_read( popt->mopt.ifsdp ))) {
			showmsg( "warning : reading semi-dynamic data" );
			trace( 2, "semi-dynamic data file read error : %s\n", popt->mopt.ifsdp );
		}
	}
    
	/* ���������͑O���� */
	if( !istat ) {
		istat = mbspre( mbs, popt, fopt, infile, index, n, rov, base );
		trace( 5, "mbstatic():nsta=%d,nobs=%d,npass=%d\n", mbs->stas.nsta, mbs->obss->n,
			   mbs->pass.n );
	}

	if(sopt->possnxout) {
		if(!rtkopensnx(popt,mbs,stas,FILENAME_SINEX,infile,n)) {
			return 1;
		}
	}

	/* SD=FCB���菈�� */
	if(popt->mopt.estsatfcb)
	{
		calsdfcb(mbs, popt, fopt, sopt);
	}
	/* obs��������pass��0���̏ꍇ�ɂ͏I�� */
	if( !mbs->obss->n || !mbs->pass.n ) {
		showmsg( "no data obs/nav" );
		trace( 1, "no data obs/nav\n" );
		istat = 1;
	}

	/* ���������͎又�� */
	if( !istat ) istat = mbsmain( mbs, popt, sopt, outdir, infile, n);

	/* ���茋�ʏo�� */
	if( !istat ) istat = mbsoutsol( mbs, fopt, popt, sopt, outdir );

	gt = timeget();
	time2str( gt, cwrk1, TIMEDIGIT );
	trace(1,"%s mbstatic end\n",cwrk1);
	traceclose();
	if(sopt->possnxout) rtkclosesnx(NULL);

	/* �q�[�v�̈�̉�� */
	mbsfree( mbs );

	return( istat );
}
/*------------------------------------------------------------------------------
* netave.c : netave functions
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

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <stdlib.h>

#include "rtklib.h"

#define SAFE_FREE(A)		if ((A)) {free((A));(A)=NULL;}
#define SEMIREAD_SKIP_LINE	(16)
#define RAD2DEG				180.0 / PI
#define PROGRAM_MANAGER		"���y��ʏȍ��y�n���@"
#define ECEF2POSEX_LOOP_MAX 50
#define ROUND(x)        ((int)floor((x)+0.5))

/* �����𐮐����A�������ɕ������A�������͎w��̌����ɕύX���� */ 
void modf_cus(double dms,int *dst,int size){
    double dsec[2];
    dsec[1]=modf(dms,&dsec[0]);
    dst[0]=ROUND(dsec[0]);
    dst[1]=ROUND(dsec[1]*pow(10.0,size));
}
/* �����̍����v�Z (�\�������ł̌v�Z���s��)*/ 
int heightdiff(int *src1,int *src2,int* dst){
    int res;
    int minusflg=1;
    int tdif;
    res=src1[1]-src2[1];
    if(res<0)
    {
        minusflg=-1;
    }
    dst[1]=abs(res);
    res=src1[0]-src2[0];
    if(res<0)
    {
        minusflg=-1;
    }
    dst[0]=abs(res);
    return minusflg;

}

//DMS��int�^�z��ɓ���Ȃ����B
int dms2int(double *dms,int *dst,int size){

    double dsec[2];
    dst[0]=ROUND(dms[0]);
    dst[1]=ROUND(dms[1]);

    dsec[1]=modf(dms[2],&dsec[0]);
    dst[2]=ROUND(dsec[0]);
    dst[3]=ROUND(dsec[1]*pow(10.0,size));

    return 0;
}

/* �x���b���̌J�艺����*/ 
int time_diff(int src){
    int res=0;
    if(src>0)
    {
        src-=1;
    }else{
        src=59;
        res=1;
    }
    return res;
}

/* �x���b�̍����v�Z (�\�������ł̌v�Z���s��)*/ 
int dmsdiff(int *src1,int *src2,int* dst){
    int res;
    int minusflg=1;
    int tdif;
    res=src1[3]-src2[3];
    if(res<0)
    {
        tdif=time_diff(src1[2]);
        if(tdif==1){
            tdif=time_diff(src1[1]);
            if(tdif==1){
                tdif=time_diff(src1[0]);
            }
        }
        minusflg=-1;
    }
    dst[1]=abs(res);
    res=src1[2]-src2[2];
    if(res<0)
    {
        tdif=time_diff(src1[1]);
        if(tdif==1){
            tdif=time_diff(src1[0]);
        }
        minusflg=-1;
    }
    dst[0]=abs(res);
    res=src1[1]-src2[1];
    if(res<0)
    {
        tdif=time_diff(src1[0]);
        minusflg=-1;
    }
    dst[0]=abs(res*60)+dst[0];
    res=src1[0]-src2[0];
    if(res<0)
    {
        minusflg=-1;
    }
    dst[0]=abs(res*60*60+dst[0]);

    return minusflg;


}

/* �Z�~�_�C�i�~�b�N�t�@�C���ǂݍ��� */
semidyna_t* semi_read(char* fsem)
{
    FILE			*fp;
    char			buff[MAXSTRPATH];
    int				meshcode;
    int				m1;
    int				m2;
    int				m3;
    int				m4;
    int				m5;
    int				m6;
    double			lat;
    double			lon;
    double			hight;
    semidynad_t		d;
    semidyna_t*		semi=NULL;
	int				i;
	
	/* �t�@�C�����s�� */
	if (!fsem)
	{
		return NULL;
	}

    /* �t�@�C���I�[�v�� */
    if (!(fp=fopen(fsem, "r")))
    {
		return NULL;
	}

    /* �w�b�_���ǂݔ�΂� */
    for (i=0;i<SEMIREAD_SKIP_LINE;i++) 
    {
        if (!fgets(buff, sizeof(buff), fp)) 
        {
            fclose(fp);
            return NULL;
        }
    }
    
    semi = malloc(sizeof(semidyna_t));
    if (!semi)
    {
		return NULL;
	}
	
	memset(semi, 0, sizeof(semidyna_t));

    /* �f�[�^���̓ǂݍ��� */
	while (fgets(buff, sizeof(buff), fp)) 
	{
		sscanf(buff, "%d %lf %lf %lf", &meshcode, &lat, &lon, &hight);
		m1 = meshcode / 1000000;
		m2 = meshcode % 1000000 / 10000;
		m3 = meshcode % 10000 / 1000;
		m4 = meshcode % 1000 / 100;
		m5 = meshcode % 100 / 10;
		m6 = meshcode % 10;
		d.grid[0] = (m1 + m3 / 8.0 + m5 / 80.0) / 1.5 * D2R;
		d.grid[1] = (100.0 + m2 + m4 / 8.0 + m6 / 80.0) * D2R;
		d.data[0] = lat / 3600.0 * D2R;
		d.data[1] = lon / 3600.0 * D2R;
		d.data[2] = hight;

		if (semi_add(semi, &d) < 0)
		{
			fclose(fp);
			SAFE_FREE(semi);
			return NULL;
		}
	}

	fclose(fp);
	
	if (semi->n == 0)
	{
		SAFE_FREE(semi);
		return NULL;
	}

	return semi;
}

/* �Z�~�_�C�i�~�b�N�p�����[�^���\���̂ɐݒ肷�� */
int semi_add(semidyna_t *semi, semidynad_t* data)
{
	semidynad_t* d=NULL;

 	if (!semi)
	{
		return -1;
	}
	if (!data)
	{
		return -1;
	}

	/* �o�b�t�@�g�� */
    if (semi->n >= semi->nmax)
    {
        if( semi->nmax <= 0)
        {
            semi->nmax = 100;
        }
        else
        {
            semi->nmax *= 2;
        }

        d = (semidynad_t *)realloc(semi->data, sizeof(semidynad_t)*semi->nmax);

        if (d == NULL)
        {
            free(semi->data);
            semi->data = NULL;
            semi->n = semi->nmax = 0;
            return -1;
        }
        semi->data = d;
    }

	memcpy(&semi->data[semi->n], data, sizeof(semidynad_t));
	semi->n++;

    return 0;
}

/* �ԕ��� */
void netaveex(net_t *net, int semidyna)
{
    rcv_t		rcv;
    int			i, j, k, l, m, n;
    double		p[3];
    double		c[6];
    double		sig=0.0;
    int*		tbl=NULL;
    int			ne;
    double*		A=NULL;
    double*		L=NULL;
    double*		P=NULL;
    double*		X=NULL;
    double*		Q=NULL;
    double*		S=NULL;
    solut_t*	s=NULL;
	int			posIdx[MAXRCV];
    
    if (!net)
    {
		return;
	}
    
    if (net->rcv.csys != CSYS_XYZ)
    {
		return;
	}

    memcpy(&rcv, &net->rcv, sizeof(rcv_t));
	memset(posIdx, 0, sizeof(posIdx));

	for (i=0;i<rcv.n;i++)
	{
		memcpy(rcv.pos[i], rcv.pos0[i], sizeof(double)*3);
	}

	/* �ǂ̍��W�ƌŒ�̋����U���APLH���W�n�֕ϊ����� */
	for (i=0;i<rcv.n;i++)
	{
		memcpy(p, rcv.pos[i], sizeof(double)*3);
		memcpy(c, rcv.cov[i], sizeof(double)*6);
        trace(5,"org_rcv.pos0=[n=%d][%15.12f][%15.12f][%15.12f]\n",i,rcv.pos0[i][0],rcv.pos0[i][1],rcv.pos0[i][2]);
        trace(5,"org_rcv.pos=[n=%d][%15.12f][%15.12f][%15.12f]\n",i,rcv.pos[i][0],rcv.pos[i][1],rcv.pos[i][2]);

        trace(5,"org_rcv.cov=[n=%d][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f]\n",i,rcv.cov[i][0],rcv.cov[i][1],rcv.cov[i][2],rcv.cov[i][3],rcv.cov[i][4],rcv.cov[i][5]);

        cov2ecef(p, c, rcv.pos[i], rcv.cov[i], RE_GRS80, FE_GRS80, XYZ_TO_PLH);

        trace(5,"conv_rcv.pos0=[n=%d][%15.12f][%15.12f][%15.12f]\n",i,rcv.pos0[i][0],rcv.pos0[i][1],rcv.pos0[i][2]);
        trace(5,"conv_rcv.pos=[n=%d][%15.12f][%15.12f][%15.12f]\n",i,rcv.pos[i][0],rcv.pos[i][1],rcv.pos[i][2]);

        trace(5,"conv_rcv.cov=[n=%d][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f]\n",i,rcv.cov[i][0],rcv.cov[i][1],rcv.cov[i][2],rcv.cov[i][3],rcv.cov[i][4],rcv.cov[i][5]);

    }

	if (semidyna == SEMIDYNA_YES)
	{
		for (i=0;i<rcv.n;i++)
		{
    		semi_corr(&net->semidyna, rcv.pos[i], SEMI_FORWARD);
		}
	}
	
	/* �ǂ̍��W�ƌŒ�̋����U���A����Ɏg�p����ECEF���W�n(x,y,z)�֕ϊ����� */
	for (i=0;i<rcv.n;i++)
	{
        memcpy(p, rcv.pos[i], sizeof(double)*3);
		memcpy(c, rcv.cov[i], sizeof(double)*6);
        trace(5,"semi_rcv.pos0=[n=%d][%15.12f][%15.12f][%15.12f]\n",i,rcv.pos0[i][0],rcv.pos0[i][1],rcv.pos0[i][2]);
        trace(5,"semi_rcv.pos=[n=%d][%15.12f][%15.12f][%15.12f]\n",i,rcv.pos[i][0],rcv.pos[i][1],rcv.pos[i][2]);

        trace(5,"semi_rcv.cov=[n=%d][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f]\n",i,rcv.cov[i][0],rcv.cov[i][1],rcv.cov[i][2],rcv.cov[i][3],rcv.cov[i][4],rcv.cov[i][5]);

        cov2ecef(p, c, rcv.pos[i], rcv.cov[i], RE_GRS80, FE_GRS80, PLH_TO_XYZ);

        trace(5,"rev_rcv.pos0=[n=%d][%15.12f][%15.12f][%15.12f]\n",i,rcv.pos0[i][0],rcv.pos0[i][1],rcv.pos0[i][2]);
        trace(5,"rev_rcv.pos=[n=%d][%15.12f][%15.12f][%15.12f]\n",i,rcv.pos[i][0],rcv.pos[i][1],rcv.pos[i][2]);
        trace(5,"rev_rcv.cov=[n=%d][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f]\n",i,rcv.cov[i][0],rcv.cov[i][1],rcv.cov[i][2],rcv.cov[i][3],rcv.cov[i][4],rcv.cov[i][5]);
    }
	
	/* �ŏ����@�p�z��̊m��*/
	tbl = imat(rcv.n, 1); /* �ǔԍ��Ɛ���ǃo���O�̕ϊ��e�[�u�� */
	for (i=0,ne=0;i<rcv.n;i++)
	{
		tbl[i] = -1;
		if (rcv.est[i] != 0)
		{
			tbl[i] = ne;
			posIdx[ne] = i;
			ne++;
		}
	}
	n = ne * 3;				/* ����p�����[�^�� */
	m = net->rel.n * 3;		/* �ϑ��f�[�^�� */
	
	A = mat(m, n); memset(A, 0, sizeof(double)*m*n);
	L = mat(m, 1); memset(L, 0, sizeof(double)*m*1);
	P = mat(m, m); memset(P, 0, sizeof(double)*m*m);
	X = mat(n, 1); memset(X, 0, sizeof(double)*n*1);
	Q = mat(n, n); memset(Q, 0, sizeof(double)*n*n);
	
	/* �v��s��̍쐬 */
	for (k=0;k<net->rel.n;k++)
	{
		i = tbl[net->rel.r1[k]];
		j = tbl[net->rel.r2[k]];
		for (l=0;l<3;l++)
		{
			if (i >= 0)
			{
				A[k*3+l+(i*3+l)*m] = -1.0;
			}
			if (j >= 0)
			{
				A[k*3+l+(j*3+l)*m] = 1.0;
			}
		}
	}
	
	/* �萔���̌v�Z */
	for (k=0;k<net->rel.n;k++)
	{
		i = net->rel.r1[k];
		j = net->rel.r2[k];
		for (l=0;l<3;l++)
		{
			L[k*3+l] = rcv.pos[j][l] - rcv.pos[i][l] - net->rel.rel[k][l];
        }
	}
	
	/* �d�ʍs��̌v�Z */
	S = mat(3, 3);
	
	for (k=0;k<net->rel.n;k++)
	{
		S[0] = net->rel.cov[k][0];
		S[4] = net->rel.cov[k][1];
		S[8] = net->rel.cov[k][2];
		S[1] = S[3] = net->rel.cov[k][3];
		S[5] = S[7] = net->rel.cov[k][4];
		S[2] = S[6] = net->rel.cov[k][5];

        trace(5,"S=\n"); tracemat(5,S, 3,3,15,12);
		matinv(S, 3);
        trace(5,"inv S=\n"); tracemat(5,S, 3,3,15,12);
		i = (m + 1) * k * 3;
		P[i] = S[0]; P[i+1] = S[1]; P[i+2] = S[2];
		i += m;
		P[i] = S[3]; P[i+1] = S[4]; P[i+2] = S[5];
		i += m;
		P[i] = S[6]; P[i+1] = S[7]; P[i+2] = S[8];
	}

    trace(5,"A=\n"); tracemat(5,A, m,n,15,12);
    trace(5,"L=\n"); tracemat(5,L, m,1,15,12);
    trace(5,"P=\n"); tracemat(5,P, m,m,15,12);

    /* ���K�������ɂ��ŏ����@ */
	lsqm(n, m, A, L, P, X, Q, &sig);

    trace(5,"X=\n"); tracemat(5,X, n,1,15,12);
    trace(5,"Q=\n"); tracemat(5,Q, n,n,15,12);

	/* ���茋�ʂ̊i�[ */
	if (semidyna == SEMIDYNA_YES) 
	{
		s = &net->semi;
	}
	else 
	{
		s = &net->nosemi;
	}


	for (i=0;i<ne;i++)
	{
		for (j=0;j<3;j++)
		{
            s->pos_xyz[i][j] = rcv.pos[posIdx[i]][j] + X[j+i*3];
		}
        //for (j=0;j<6;j++)
        {
            //s->cov_xyz[i][j] = rcv.cov[posIdx[i]][j];

            s->cov_xyz[i][0]=Q[(i*3+0)*(n+1)+0];
            s->cov_xyz[i][1]=Q[(i*3+1)*(n+1)+0];
            s->cov_xyz[i][2]=Q[(i*3+2)*(n+1)+0];
            s->cov_xyz[i][3]=Q[(i*3+0)*(n+1)+1];
            s->cov_xyz[i][4]=Q[(i*3+1)*(n+1)+1];
            s->cov_xyz[i][5]=Q[(i*3+0)*(n+1)+2];

        }
	}
	s->signote = sig;
	
    /* ���W�ϊ� */
    for (i=0;i<ne;i++) 
    {
		cov2ecef(s->pos_xyz[i], s->cov_xyz[i], s->pos_plh[i], s->cov_plh[i], RE_GRS80, FE_GRS80, XYZ_TO_PLH);

        trace(5,"last_rcv.pos=[n=%d][%15.12f][%15.12f][%15.12f]\n",i,s->pos_plh[i][0],s->pos_plh[i][1],s->pos_plh[i][2]);
        trace(5,"last_rcv.cov=[n=%d][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f]\n",i,s->cov_plh[i][0],s->cov_plh[i][1],s->cov_plh[i][2],s->cov_plh[i][3],s->cov_plh[i][4],s->cov_plh[i][5]);

    }


	/* �Z�~�_�C�i�~�b�N�␳ */
	if (semidyna == SEMIDYNA_YES)
	{
		for (i=0;i<ne;i++)
		{
            memcpy(s->sem_plh[i], s->pos_plh[i], sizeof(double)*4);
            //�������W������
            semi_corr(&net->semidyna, s->sem_plh[i], SEMI_BACKWARD);
            s->sem_plh[i][3] = s->sem_plh[i][2] - geoidh(s->sem_plh[i]);
		}
	}

	/* �I������ */
    memcpy(&net->rcv, &rcv, sizeof(rcv_t));

	SAFE_FREE(tbl);
	SAFE_FREE(A);
	SAFE_FREE(L);
	SAFE_FREE(P);
	SAFE_FREE(X);
	SAFE_FREE(Q);
	SAFE_FREE(S);
}

/* ���n�n���璼�����W�n�ւ̕ϊ� */
static void pos2ecefEX(const double *pos, double *r, double re, double fe)
{
    double sinp=sin(pos[0]),cosp=cos(pos[0]),sinl=sin(pos[1]),cosl=cos(pos[1]);
    double e2=fe*(2.0-fe),v=re/sqrt(1.0-e2*sinp*sinp);
    
    r[0]=(v+pos[2])*cosp*cosl;
    r[1]=(v+pos[2])*cosp*sinl;
    r[2]=(v*(1.0-e2)+pos[2])*sinp;
}

/* �������W�n���瑪�n�n�ւ̕ϊ� */
static void ecef2posEX(const double *r, double *pos, double re, double fe)
{
    int i;
    double e2=fe*(2.0-fe),r2=dot(r,r,2),z,zk,v=re,sinp;
    
    for (i=0, z=r[2],zk=0.0;i<ECEF2POSEX_LOOP_MAX && fabs(z-zk)>=1E-12; i++)
    {
        zk=z;
        sinp=z/sqrt(r2+z*z);
        v=re/sqrt(1.0-e2*sinp*sinp);
        z=r[2]+v*e2*sinp;
    }
    pos[0]=r2>1E-12?atan(z/sqrt(r2)):(r[2]>0.0?PI/2.0:-PI/2.0);
    pos[1]=r2>1E-12?atan2(r[1],r[0]):0.0;
    pos[2]=sqrt(r2+z*z)-v;
}

#if 0
/* �x���b����radian�ւ̕ϊ� */
static double gsi2radian(double gsi)
{
    double radian;
    double du;
    double feng;
    double miao;
    double flag;
    flag = 1.0;
    if( gsi<0.0 )
    {
        flag = -1.0 ;
        gsi = -gsi ;
    }
    du = floor(gsi/10000) ;
    gsi -= du*10000.0 ;
    feng = floor(gsi/100.0) ;
    gsi -= feng*100.0 ;
    miao = gsi ;
    radian = flag*(miao/3600.0+feng/60.0+du)/180.0*PI ;
    return(radian) ;
}
#endif
static void CalcQxyz0(double re, double fe, double latitude, double longitude, double h, double* sigma, double* weight)
{
    double phi;
    double lambda;
    double sin_phi;
    double sin_lambda;
    double cos_phi;
    double cos_lambda;
    double temp[9];
    double R1[9];

    phi		= latitude;
    lambda	= longitude;

    sin_phi		= sin(phi);
    sin_lambda	= sin(lambda);
    cos_phi		= cos(phi);
    cos_lambda	= cos(lambda);


    R1[0] = -sin_phi * cos_lambda ;
    R1[1] = -sin_lambda ;
    R1[2] = cos_phi * cos_lambda;
    R1[3] = -sin_phi * sin_lambda ;
    R1[4] = cos_lambda ;
    R1[5] = cos_phi * sin_lambda;
    R1[6] = cos_phi ;
    R1[7] = 0.0;
    R1[8] = sin_phi;

    matmul("TN", 3, 3, 3, 1.0, R1, sigma, 0.0, temp);
    matmul("NN", 3, 3, 3, 1.0, temp, R1, 0.0, weight);
}
static void CalcQxyz1(double re, double fe, double latitude, double longitude, double h, double* sigma, double* weight)
{
	double phi;
	double lambda;
	double sin_phi;
	double sin_lambda;
	double cos_phi;
	double cos_lambda;
    double temp[9];
    double R2[9];

	phi		= latitude;
	lambda	= longitude;
	
	sin_phi		= sin(phi);
	sin_lambda	= sin(lambda);
	cos_phi		= cos(phi);
    cos_lambda	= cos(lambda);

    R2[0] = -sin_phi * cos_lambda;
    R2[1] = -sin_lambda;
    R2[2] = cos_phi * cos_lambda;
    R2[3] = -sin_phi * sin_lambda ;
    R2[4] = cos_lambda ;
    R2[5] = cos_phi * sin_lambda;
    R2[6] = cos_phi ;
    R2[7] = 0.0;
    R2[8] = sin_phi;

    trace(5,"CalcQxyz1 pos=[%15.12f][%15.12f][%15.12f]\n",latitude,longitude,h);
    trace(5,"CalcQxyz1 R2=[%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f]\n",R2[0],R2[1],R2[2],R2[3],R2[4],R2[5],R2[6],R2[7],R2[8]);
    trace(5,"CalcQxyz1 sigma=[%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f]\n",sigma[0],sigma[1],sigma[2],sigma[3],sigma[4],sigma[5],sigma[6],sigma[7],sigma[8]);


    matmul("NN", 3, 3, 3, 1.0, R2, sigma, 0.0, temp);

    trace(5,"CalcQxyz1 temp=[%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f][%15.12f]\n",temp[0],temp[1],temp[2],temp[3],temp[4],temp[5],temp[6],temp[7],temp[8]);

    matmul("NT", 3, 3, 3, 1.0, temp, R2, 0.0, weight);

    trace(5,"CalcQxyz1 weight=[%e][%e][%e][%e][%e][%e][%e][%e][%e]\n",weight[0],weight[1],weight[2],weight[3],weight[4],weight[5],weight[6],weight[7],weight[8]);

}

/* ���n�n�̌Œ���W�n�ւ̕ϊ��i�����U���܂ށj */
void cov2ecef(double *ipos, double *icov, double *opos, double *ocov, double re, double fe, int flag)
{
	double bun[9];
	double weight[9];
	
	if (!ipos || !opos)
	{
		return;
	}
	if (icov && !ocov)
	{
		return;
	}

    if (flag == PLH_TO_XYZ)
    {
		/* ���W�ϊ� */
		pos2ecefEX(ipos, opos, re, fe);

		/* �����U�̕ϊ� */
        if (icov != NULL)
        {
			memset(bun, 0, sizeof(bun));
			bun[0] = icov[0] ;
			bun[4] = icov[1] ;
			bun[8] = icov[2] ;
			bun[1] = bun[3] = icov[3];
			bun[5] = bun[7] = icov[4];
			bun[2] = bun[6] = icov[5];

			CalcQxyz0(re, fe, ipos[0], ipos[1], ipos[2], bun, weight);

			ocov[0] = weight[0];
			ocov[1] = weight[4];
			ocov[2] = weight[8];
			ocov[3] = weight[1];
			ocov[4] = weight[5];
			ocov[5] = weight[2];
		}
	} 
	else 
	{
 		/* ���W�ϊ� */
		ecef2posEX(ipos, opos, re, fe);

		/* �����U�̕ϊ� */
        if (icov != NULL)
        {
        	memset(bun, 0, sizeof(bun));
			bun[0] = icov[0];
			bun[4] = icov[1];
			bun[8] = icov[2];
			bun[1] = bun[3] = icov[3];
			bun[5] = bun[7] = icov[4];
			bun[2] = bun[6] = icov[5];

			CalcQxyz1(re, fe, opos[0], opos[1], opos[2], bun, weight);

			ocov[0] = weight[0];
			ocov[1] = weight[4];
			ocov[2] = weight[8];
			ocov[3] = weight[1];
			ocov[4] = weight[5];
			ocov[5] = weight[2];
        }
	}
} 

/* �ŏ����@ */  
void lsqm(int n, int m, double* A, double* L, double* P, double* X, double* Q, double* sig)
{
	double* B=NULL;
	double* C=NULL;
	double* D=NULL;
	double* V=NULL;
	double* F=NULL;
	double* s=NULL;
	int		i;
	
	if (!A || !L || !P || !X || !Q || !sig)
	{
		return;
	}

	/* (A^PA)^-1�̌v�Z */
	B = mat(n, m);
	C = mat(n, n);
	matmul("TN", n, m, m, 1.0, A, P, 0.0, B);
	matmul("NN", n, n, m, 1.0, B, A, 0.0, C);

    trace(5,"B=\n"); tracemat(5,B, n,m,15,12);
    trace(5,"C=\n"); tracemat(5,C, n,n,15,12);

    matinv(C, n);
    trace(5,"inv C=\n"); tracemat(5,C, n,n,15,12);

	/* A'PL�̌v�Z */
	D = mat(n, 1);
	matmul("NN", n, 1, m, 1.0, B, L, 0.0, D);
    trace(5,"D=\n"); tracemat(5,D, n,1,15,12);

	/* X=(A^PA)^1A'PL�̌v�Z */
	matmul("NN", n, 1, n, -1.0, C, D, 0.0, X);
    trace(5,"X=\n"); tracemat(5,X, n,1,15,12);

	/* V�̌v�Z */
	V = mat(m, 1);
	matcpy(V, L, m, 1);
	matmul("NN", m, 1, n, 1.0, A, X, 1.0, V);
    trace(5,"V=\n"); tracemat(5,V, m,1,15,12);

	/* V'PV/f�̌v�Z */
	F = mat(1, m);
	s = mat(1, 1);
	matmul("TN", 1, m, m, 1.0, V, P, 0.0, F);
	matmul("NN", 1, 1, m, 1.0, F, V, 0.0, s);

    trace(5,"F=\n"); tracemat(5,F, 1,m,15,12);
    trace(5,"s=\n"); tracemat(5,s, 1,1,15,12);

	if (n - m == 0)
	{
		*sig = s[0];
	}
	else
	{
		*sig = s[0] / (m - n);
	}

	/* ���㕪�U�����U */
	for (i=0;i<n*n;i++)
	{
		Q[i] = *sig * C[i];
	}
	
	/* �I������ */
	SAFE_FREE(B);
	SAFE_FREE(C);
	SAFE_FREE(D);
	SAFE_FREE(V);
	SAFE_FREE(F);
	SAFE_FREE(s);	
}
/* �o�C���j�A�v�Z */
static double interpb(const double *y, double a, double b)
{
    return y[0]*(1.0-a)*(1.0-b)+y[1]*a*(1.0-b)+y[2]*(1.0-a)*b+y[3]*a*b;
}
/* �Z�~�_�C�i�~�b�N�␳ */
void semi_corr(semidyna_t* data, double* r, int flag)
{
	double		par[5][4];
	double		disp[3];
	int			i;
	double		a, b;
	
	if (!data)
	{
		return;
	}
	if (!r)
	{
		return;
	}
	if (flag != 0 && flag != 1)
	{
		return;
	}

	/* ���W�l���͂̃Z�~�_�C�i�~�b�N�p�����[�^������ */
	semi_search(data, r, &par[0][0]);
	
	/* �Z�~�_�C�i�~�b�N�p�����[�^���o�C���j�A��� */
	a = (r[1] - par[1][0]) / (par[1][1] - par[1][0]);
	b = (r[0] - par[0][0]) / (par[0][2] - par[0][0]);

	for (i=0;i<3;i++)
	{
		disp[i] = interpb(par[i+2], a, b);
	}
	
	if (flag == 0)
	{
		/* �����������ϊ� */
		for (i=0;i<3;i++)
		{
			r[i] += disp[i];
		}
	}
	else
	{
		/* �����������ϊ� */
		for (i=0;i<3;i++)
		{
			r[i] -= disp[i];
		}
	}
}

/* �w�����W�̎���S�_�̃Z�~�_�C�i�~�b�N�p�����[�^�̌��� */
void semi_search(semidyna_t *data, double *r, double* par)
{
	int	i, j;
	int ii, jj;
	
	if (!data)
	{
		return;
	}
	if (!r)
	{
		return;
	}
	if (!par)
	{
		return;
	}
	
	ii = -1;
	jj = -1;
  
    /* �Z�~�_�C�i�~�b�N�p�����[�^�̌��� */
	for (i=0;i<data->n-1;i++)
	{
		if (data->data[i].grid[0] <= r[0] && r[0] <= data->data[i+1].grid[0])
		{
			for (j=i-1;j>=0;j--)
			{
				if (data->data[j].grid[1] <= r[1] && r[1] <= data->data[j+1].grid[1])
				{
					ii = j;
					break;
				}
			}
			if (ii == -1)
			{
				return;
			}
			
			for (j=i+1;j<data->n;j++)
			{
				if (data->data[j].grid[1] <= r[1] && r[1] <= data->data[j+1].grid[1])
				{
					jj = j;
					break;
				}
			}
			if (jj == -1)
			{
				return;
			}
			
			break;
		}
	}
	
	if (ii == -1 || jj == -1)
	{
		return;
	}

	/* �ܓx */
	par[ 0] = data->data[ii  ].grid[0];
	par[ 1] = data->data[ii+1].grid[0];
	par[ 2] = data->data[jj  ].grid[0];
	par[ 3] = data->data[jj+1].grid[0];
	/* �o�x */
	par[ 4] = data->data[ii  ].grid[1];
	par[ 5] = data->data[ii+1].grid[1];
	par[ 6] = data->data[jj  ].grid[1];
	par[ 7] = data->data[jj+1].grid[1];
	/* �ܓx�ϓ��l */
	par[ 8] = data->data[ii  ].data[0];
	par[ 9] = data->data[ii+1].data[0];
	par[10] = data->data[jj  ].data[0];
	par[11] = data->data[jj+1].data[0];
	/* �o�x�ϓ��l */
	par[12] = data->data[ii  ].data[1];
	par[13] = data->data[ii+1].data[1];
	par[14] = data->data[jj  ].data[1];
	par[15] = data->data[jj+1].data[1];
	/* �����ϓ��l */
	par[16] = data->data[ii  ].data[2];
	par[17] = data->data[ii+1].data[2];
	par[18] = data->data[jj  ].data[2];
	par[19] = data->data[jj+1].data[2];

	return;
}

static char* getfilename(char* path)
{
	static char file[MAXSTRPATH];
	int s;
	int i;
	
	s = 0;
	for (i=(int)strlen(path)-1;i>=0;i--)
	{
		if (path[i] == '\\' ||
			path[i] == '/')
		{
			s = i + 1;
			break;
		}
	}

	strcpy(file, &path[s]);
	
	return file; 
}
int savenet(net_t* net)
{
	FILE*		fp;
	int			i;
	int			k;
	int			n;
	double		plh[4];
	double		wplh[4];
    double		dms[3];
    double		sxyz[4];
    double		exyz[4];
	int			bsemi=0;
	time_t		timer;
	struct tm*	t_st;
	double		geoH;
	int			nidx[MAXRCV];
	int			sidx;
	int			eidx;
    double      plh_0[2];
    int         cnt_0;
    double      calcdata_1[3];
    double      calcdata_2[3];
    int         idms[4];
    int         idiff[2];
    double      ddst[3];
    int         idst[3];
    int         dminus;
    if (!net)
	{
		return -1;
	}
	
	fp = fopen(net->fopt.fave, "w");
	if (!fp)
	{
		return -1;
	}
	
	/* �_�C�i�~�b�N�␳����̏ꍇ */
	if (net->semidyna.n > 0)
	{
		bsemi = 1;
	}

	/* �t�@�C���^�C�v */
	fprintf(fp, "CD_FileType=%s\n", "4");
	/* �^�C�g�� */ 							
	fprintf(fp, "AC_Title=%s\n", "�O�����ԕ��όv�Z");
	/* �n�於 */				
	fprintf(fp, "AC_AreaName=\n");
	/* �ȉ~�̌��q�����a */ 									
	fprintf(fp, "AC_EllipsoidLongRadius=%.1f\n", RE_GRS80);
	/* �G���� */			
	fprintf(fp, "AC_Ellipticity=%.9f\n", RFE_GRS80); 				
	
	/* �P�ʏd�ʓ�����̕W���΍� */
	if (bsemi)
	{
        fprintf(fp, "AC_UnitWeightSD=%.10e\n", sqrt(net->semi.signote));
	}
	else
	{
        fprintf(fp, "AC_UnitWeightSD=%.10e\n", sqrt(net->nosemi.signote));
	}

	/* ���U�E�����U�l dN dE dU */
	if (net->flagcov == 0)
	{
		fprintf(fp, "AC_VarCovDN=%s\n", "0.004");		
		fprintf(fp, "AC_VarCovDE=%s\n", "0.004");
		fprintf(fp, "AC_VarCovDU=%s\n", "0.007");
	}
	else
	{
        fprintf(fp, "AC_VarCovDN=%s\n", "0");
        fprintf(fp, "AC_VarCovDE=%s\n", "0");
        fprintf(fp, "AC_VarCovDU=%s\n", "0");
	}

	/* �X�P�[���␳�l */
	fprintf(fp, "AC_ScaleCorrection=%e\n", 0.0000000000E+00);
	
	/* B0 L0 */
    /* �S�Ă̊��m�_�̕��ϒl */
    cnt_0=0;
    plh_0[0]=0.0;
    plh_0[1]=0.0;
	for (i=0;i<net->rcv.n;i++)
	{
		if (net->rcv.est[i] == 0)
		{
			cov2ecef(net->rcv.pos0[i], NULL, plh, NULL, RE_GRS80, FE_GRS80, XYZ_TO_PLH);
            cnt_0++;
            plh_0[0]+=(plh[0]-plh_0[0])/cnt_0;
            plh_0[1]+=(plh[1]-plh_0[1])/cnt_0;
		}
	}
	
    /* �x���b�֕ύX */
    deg2dms(plh_0[0]*RAD2DEG, dms);
    fprintf(fp, "AC_B0=%02d��%02d�f%05.2f�h\n", (int)dms[0], (int)dms[1], dms[2]);
    deg2dms(plh_0[1]*RAD2DEG, dms);
    fprintf(fp, "AC_L0=%02d��%02d�f%05.2f�h\n", (int)dms[0], (int)dms[1], dms[2]);

 	/* �����ʓ��̉�] */
	fprintf(fp, "AC_RotationHorizontal=%f\n", 0.000);
	/* �� */
	fprintf(fp, "AC_XI=%f\n", 0.000); 	
	/* �� */				
	fprintf(fp, "AC_ETA=%f\n", 0.000); 					
	
	/* �v�Z���� */
	if (bsemi) 
	{
		fprintf(fp, "AC_CalculationCondition=%s\n",	"�W�I�C�h�␳����A�������΍����肠��A��]����Ȃ��A�X�P�[������Ȃ�");
	}
	else
	{
		fprintf(fp, "AC_CalculationCondition=%s\n", "�W�I�C�h�␳�Ȃ��A�������΍�����Ȃ��A��]����Ȃ��A�X�P�[������Ȃ�");
	}
	
	/* �W�I�C�h���� */
	if (strlen(net->fopt.fgeo) == 0) 
	{
		fprintf( fp, "AC_GeoidName=%s\n", "Internal" );						
	}
	else
	{
		fprintf(fp, "AC_GeoidName=%s\n", getfilename(net->fopt.fgeo));
	}
	
	/* �Z�~�_�C�i�~�b�N�␳ */
	fprintf(fp, "AC_SemiDynamicCorrect=%s\n", getfilename(net->fopt.fsem));		
	
	/* �v�Z�� */
	time(&timer);
	t_st = localtime(&timer);
	fprintf(fp, "AC_CalculationDate=%d�N%d��%d��\n", t_st->tm_year+1900,t_st->tm_mon+1,t_st->tm_mday);
	/* �v���O�����Ǘ��� */	
	fprintf(fp, "AC_ProgramManager=%s\n", PROGRAM_MANAGER);												

	/* ���m�_�̍������W�ւ̕␳ */
	n = 0;
	for (i=0;i<net->rcv.n;i++) 
	{
		if (net->rcv.est[i] == 0) 
		{
			n++;
		}
	}
	fprintf(fp, "AC_NumberKnownPoints=%d\n", n);	
	k=0;
	for (i=0;i<net->rcv.n;i++) 
	{
		if (net->rcv.est[i] != 0)
		{
            //�V�_�̂ݑΏ�
            continue;
		}

		/* �_�ԍ� */
		fprintf( fp, "AC_KnownPointNumber[%d]=%s\n",k, net->rcv.name1[i]);
		/* �_���� */	
		fprintf( fp, "AC_KnownPointName[%d]=%s\n",k, net->rcv.name2[i]);	
		cov2ecef(net->rcv.pos0[i], NULL, plh, NULL, RE_GRS80, FE_GRS80, XYZ_TO_PLH);
		/* B[i] */
		deg2dms(plh[0]*RAD2DEG, dms);
        dms2int(dms,idms,5);
        fprintf(fp, "AC_InputB[%d]=%02d��%02d�f%02d.%05d�h\n", k, idms[0], idms[1], idms[2],idms[3]);
        /* L[i] */
		deg2dms(plh[1]*RAD2DEG, dms);
        dms2int(dms,idms,5);
        fprintf(fp, "AC_InputL[%d]=%02d��%02d�f%02d.%05d�h\n", k, idms[0], idms[1], idms[2],idms[3]);

        modf_cus(plh[2],idms,4);

        /* ���͒l �ȉ~�̍� */
        fprintf(fp, "AC_InputEllipsoidHeight[%d]=%.4f\n", k, plh[2]);
        /* ���͒l �W�I�C�h�� */
		geoH = geoidh(plh);				

        fprintf(fp, "AC_InputGeoidHeight[%d]=%.4f\n", k, geoH);
        /* ���͒l �W�� */
        fprintf(fp, "AC_InputTrueHeight[%d]=%.4f\n", k,plh[2] - geoH);

		/* �␳�� B */
        cov2ecef(net->rcv.pos[i], NULL, wplh, NULL, RE_GRS80, FE_GRS80, XYZ_TO_PLH);

        deg2dms(plh[0]*RAD2DEG, dms);
        dms2int(dms,idms,5);
        deg2dms(wplh[0]*RAD2DEG, ddst);
        dms2int(ddst,idst,5);
        dminus=dmsdiff(idst,idms,idiff);
        if(dminus<0){
            fprintf( fp, "AC_CorrectInputB[%d]=-%d.%05d\n", k,idiff[0],idiff[1] );
        }else{
            fprintf( fp, "AC_CorrectInputB[%d]=%d.%05d\n", k,idiff[0],idiff[1] );
        }

        deg2dms(plh[1]*RAD2DEG, dms);
        dms2int(dms,idms,5);
        deg2dms(wplh[1]*RAD2DEG, ddst);
        dms2int(ddst,idst,5);
        dminus=dmsdiff(idst,idms,idiff);
        if(dminus<0){
            fprintf( fp, "AC_CorrectInputL[%d]=-%d.%05d\n", k,idiff[0],idiff[1] );
        }else{
            fprintf( fp, "AC_CorrectInputL[%d]=%d.%05d\n", k,idiff[0],idiff[1] );
        }

        modf_cus(plh[2],idms,4);
        modf_cus(wplh[2],idst,4);
        dminus=heightdiff(idst,idms,idiff);
        if(dminus<0){
            fprintf(fp, "AC_CorrectInputEllipsoidHeight[%d]=-%d.%04d\n", k,idiff[0],idiff[1]);
        }else{
            fprintf(fp, "AC_CorrectInputEllipsoidHeight[%d]=%d.%04d\n", k,idiff[0],idiff[1]);
        }

		/* �������W B */
		deg2dms(wplh[0]*RAD2DEG, dms);
        dms2int(dms,idms,5);
        fprintf(fp, "AC_CurrentB[%d]=%02d��%02d�f%02d.%05d�h\n", k, idms[0], idms[1], idms[2],idms[3]);
        /* �������W L */
		deg2dms(wplh[1]*RAD2DEG, dms);
        dms2int(dms,idms,5);
        fprintf(fp, "AC_CurrentL[%d]=%02d��%02d�f%02d.%05d�h\n", k, idms[0], idms[1], idms[2],idms[3]);
        /* �������W �ȉ~�̍� */

        modf_cus(wplh[2],idms,4);
        fprintf(fp, "AC_CurrentEllipsoidHeight[%d]=%d.%04d\n", k, idms[0],idms[1]);

        k++;
	}

    /* �V�_�̍��W�ߎ��l */
	n = 0;
	for (i=0;i<net->rcv.n;i++) 
	{
		if (net->rcv.est[i] == 1) 
		{
			nidx[i] = n;
			n++;
		}

	}

	fprintf(fp, "AC_NumberNewPoints=%d\n", n);	
	k=0;
	for (i=0;i<net->rcv.n;i++) 
	{
		if (net->rcv.est[i] != 1)
		{
			continue;
		}
	
		/* �_�ԍ� */
		fprintf( fp, "AC_NewPointNumber[%d]=%s\n",k, net->rcv.name1[i]);
		/* �_���� */	
		fprintf( fp, "AC_NewPointName[%d]=%s\n",k, net->rcv.name2[i]);	

		/* B[j] */
		cov2ecef(net->rcv.pos0[i], NULL, plh, NULL, RE_GRS80, FE_GRS80, XYZ_TO_PLH);
		deg2dms(plh[0]*RAD2DEG, dms);
        dms2int(dms,idms,5);
        fprintf(fp, "AC_ApproxB[%d]=%02d��%02d�f%02d.%05d�h\n", k, idms[0], idms[1], idms[2],idms[3]);
        /* L[j] */
		deg2dms(plh[1]*RAD2DEG, dms);
        dms2int(dms,idms,5);
        fprintf(fp, "AC_ApproxL[%d]=%02d��%02d�f%02d.%05d�h\n", k, idms[0], idms[1], idms[2],idms[3]);
        /* ���͒l �ȉ~�̍� */
        modf_cus(plh[2],idms,3);
        fprintf(fp, "AC_ApproxEllipsoidHeight[%d]=%d.%03d\n", k, idms[0],idms[1]);


        /* �␳�� B */
        deg2dms(plh[0]*RAD2DEG, dms);
        dms2int(dms,idms,5);
        if (bsemi)
        {
            deg2dms(net->semi.pos_plh[nidx[i]][0]*RAD2DEG, ddst);
        }else{
            deg2dms(net->nosemi.pos_plh[nidx[i]][0]*RAD2DEG,ddst);
        }
        dms2int(ddst,idst,5);
        dminus=dmsdiff(idst,idms,idiff);
        if(dminus<0){
            fprintf( fp, "AC_CorrectApproxB[%d]=-%d.%05d\n", k,idiff[0],idiff[1] );
        }else{
            fprintf( fp, "AC_CorrectApproxB[%d]=%d.%05d\n", k,idiff[0],idiff[1] );
        }
        /* �␳�� L */
        deg2dms(plh[1]*RAD2DEG, dms);
        dms2int(dms,idms,5);
        if (bsemi)
        {
            deg2dms(net->semi.pos_plh[nidx[i]][1]*RAD2DEG, ddst);
        }else{
            deg2dms(net->nosemi.pos_plh[nidx[i]][1]*RAD2DEG,ddst);
        }
        dms2int(ddst,idst,5);
        dminus=dmsdiff(idst,idms,idiff);
        if(dminus<0){
            fprintf( fp, "AC_CorrectApproxL[%d]=-%d.%05d\n", k,idiff[0],idiff[1] );
        }else{
            fprintf( fp, "AC_CorrectApproxL[%d]=%d.%05d\n", k,idiff[0],idiff[1] );
        }

        /* �␳�� �ȉ~�̍� */
        modf_cus(plh[2],idms,3);
        idms[1]*=10;
        if (bsemi)
        {
            modf_cus(net->semi.pos_plh[nidx[i]][2],idst,4);
        }else{
            modf_cus(net->nosemi.pos_plh[nidx[i]][2],idst,4);
        }
        dminus=heightdiff(idst,idms,idiff);
        if(dminus<0){
            fprintf(fp, "AC_CorrectApproxEllipsoidHeight[%d]=-%d.%04d\n", k,idiff[0],idiff[1]);
        }else{
            fprintf(fp, "AC_CorrectApproxEllipsoidHeight[%d]=%d.%04d\n", k,idiff[0],idiff[1]);
        }

        /* B[j] */
        if (bsemi)
        {
            deg2dms(net->semi.pos_plh[nidx[i]][0]*RAD2DEG, dms);
        }else{
            deg2dms(net->nosemi.pos_plh[nidx[i]][0]*RAD2DEG, dms);
        }
        dms2int(dms,idms,5);
        fprintf(fp, "AC_OptimulB[%d]=%02d��%02d�f%02d.%05d�h\n", k, idms[0], idms[1], idms[2],idms[3]);
        /* L[j] */
        if (bsemi)
        {
            deg2dms(net->semi.pos_plh[nidx[i]][1]*RAD2DEG, dms);
        }else{
            deg2dms(net->nosemi.pos_plh[nidx[i]][1]*RAD2DEG, dms);
        }
        dms2int(dms,idms,5);
        fprintf(fp, "AC_OptimulL[%d]=%02d��%02d�f%02d.%05d�h\n", k, idms[0], idms[1], idms[2],idms[3]);
        /* �ȉ~�̍�[j] */
        if (bsemi)
        {
            modf_cus(net->semi.pos_plh[nidx[i]][2],idms,4);
            fprintf(fp, "AC_OptimulEllipsoidHeight[%d]=%d.%04d\n", k, idms[0],idms[1]);
        }else{
            modf_cus(net->nosemi.pos_plh[nidx[i]][2],idms,4);
            fprintf(fp, "AC_OptimulEllipsoidHeight[%d]=%d.%04d\n", k, idms[0],idms[1]);
        }
        /* �W���΍� B */
        if (bsemi)
        {
            if(net->semi.cov_plh[nidx[i]][0] <=0.0){
                fprintf(fp, "AC_SDB[%d]=%f\n", k, 0.0);
            }else{
                fprintf(fp, "AC_SDB[%d]=%f\n", k, sqrt(net->semi.cov_plh[nidx[i]][0]));
            }
        }else{
            if(net->nosemi.cov_plh[nidx[i]][0] <=0.0){
                fprintf(fp, "AC_SDB[%d]=%f\n", k, 0.0);
            }else{
                fprintf(fp, "AC_SDB[%d]=%f\n", k, sqrt(net->nosemi.cov_plh[nidx[i]][0]));
            }
        }
        /* �W���΍� L */
        if (bsemi)
        {
            if(net->semi.cov_plh[nidx[i]][1] <=0.0){
                fprintf(fp, "AC_SDL[%d]=%f\n", k, 0.0);
            }else{
                fprintf(fp, "AC_SDL[%d]=%f\n", k, sqrt(net->semi.cov_plh[nidx[i]][1]));
            }
        }else{
            if(net->nosemi.cov_plh[nidx[i]][1] <=0.0){
                fprintf(fp, "AC_SDL[%d]=%f\n", k, 0.0);
            }else{
                fprintf(fp, "AC_SDL[%d]=%f\n", k, sqrt(net->nosemi.cov_plh[nidx[i]][1]));
            }
        }
        /* �W���΍� �ȉ~�̍� */
        if (bsemi)
        {
            if(net->semi.cov_plh[nidx[i]][2] <=0.0){
                fprintf(fp, "AC_SDEllipsoidHeight[%d]=%f\n", k, 0.0);
            }else{
                fprintf(fp, "AC_SDEllipsoidHeight[%d]=%f\n", k, sqrt(net->semi.cov_plh[nidx[i]][2]));
            }
        }else{
            if(net->nosemi.cov_plh[nidx[i]][2] <=0.0){
                fprintf(fp, "AC_SDEllipsoidHeight[%d]=%f\n", k, 0.0);
            }else{
                fprintf(fp, "AC_SDEllipsoidHeight[%d]=%f\n", k, sqrt(net->nosemi.cov_plh[nidx[i]][2]));
            }
        }
        /* �W���΍� MS */
        if (bsemi)
        {
            if(net->semi.cov_plh[nidx[i]][0] + net->semi.cov_plh[nidx[i]][1] <=0.0){
                fprintf(fp, "AC_SDMS[%d]=%f\n", k, 0.0);
            }else{
                fprintf(fp, "AC_SDMS[%d]=%f\n", k, sqrt(net->semi.cov_plh[nidx[i]][0] + net->semi.cov_plh[nidx[i]][1]));
            }
        }else{
            if(net->nosemi.cov_plh[nidx[i]][0] + net->nosemi.cov_plh[nidx[i]][1] <=0.0){
                fprintf(fp, "AC_SDMS[%d]=%f\n", k, 0.0);
            }else{
                fprintf(fp, "AC_SDMS[%d]=%f\n", k, sqrt(net->nosemi.cov_plh[nidx[i]][0] + net->nosemi.cov_plh[nidx[i]][1]));
            }

        }
        /* �W���΍� MH */
        if (bsemi)
        {
            if(net->semi.cov_plh[nidx[i]][2] <=0.0){
                fprintf(fp, "AC_SDMH[%d]=%f\n", k, 0.0);
            }else{
                fprintf(fp, "AC_SDMH[%d]=%f\n", k, sqrt(net->semi.cov_plh[nidx[i]][2]));
            }
        }else{
            if(net->nosemi.cov_plh[nidx[i]][2] <=0.0){
                fprintf(fp, "AC_SDMH[%d]=%f\n", k, 0.0);
            }else{
                fprintf(fp, "AC_SDMH[%d]=%f\n", k, sqrt(net->nosemi.cov_plh[nidx[i]][2]));
            }
        }
		if (bsemi)
        {
            /* �����ւ̕␳�� B */
            deg2dms(net->semi.pos_plh[nidx[i]][0]*RAD2DEG, dms);
            dms2int(dms,idms,5);
            deg2dms(net->semi.sem_plh[nidx[i]][0]*RAD2DEG, ddst);
            dms2int(ddst,idst,5);
            dminus=dmsdiff(idst,idms,idiff);
            if(dminus<0){
                fprintf( fp, "AC_CorrectOriginB[%d]=-%d.%05d\n", k,idiff[0],idiff[1] );
            }else{
                fprintf( fp, "AC_CorrectOriginB[%d]=%d.%05d\n", k,idiff[0],idiff[1] );
            }

            /* �����ւ̕␳�� L */
            deg2dms(net->semi.pos_plh[nidx[i]][1]*RAD2DEG, dms);
            dms2int(dms,idms,5);
            deg2dms(net->semi.sem_plh[nidx[i]][1]*RAD2DEG, ddst);
            dms2int(ddst,idst,5);
            dminus=dmsdiff(idst,idms,idiff);
            if(dminus<0){
                fprintf( fp, "AC_CorrectOriginL[%d]=-%d.%05d\n", k,idiff[0],idiff[1] );
            }else{
                fprintf( fp, "AC_CorrectOriginL[%d]=%d.%05d\n", k,idiff[0],idiff[1] );
            }

            /* �����ւ̕␳�� �ȉ~�̍� */
            modf_cus(net->semi.pos_plh[nidx[i]][2],idms,4);
            modf_cus(net->semi.sem_plh[nidx[i]][2],idst,4);
            dminus=heightdiff(idst,idms,idiff);
            if(dminus<0){
                fprintf(fp, "AC_CorrectOriginEllipsoidHeight[%d]=-%d.%04d\n", k,idiff[0],idiff[1]);
            }else{
                fprintf(fp, "AC_CorrectOriginEllipsoidHeight[%d]=%d.%04d\n", k,idiff[0],idiff[1]);
            }



            /* B[j] */
            deg2dms(net->semi.sem_plh[nidx[i]][0]*RAD2DEG, dms);
            dms2int(dms,idms,5);
            fprintf(fp, "AC_OriginB[%d]=%02d��%02d�f%02d.%05d�h\n", k, idms[0], idms[1], idms[2],idms[3]);
            /* L[j] */
            deg2dms(net->semi.sem_plh[nidx[i]][1]*RAD2DEG, dms);
            dms2int(dms,idms,5);
            fprintf(fp, "AC_OriginL[%d]=%02d��%02d�f%02d.%05d�h\n", k, idms[0], idms[1], idms[2],idms[3]);


            /* �ȉ~�̍�[j] */
            fprintf(fp, "AC_OriginEllipsoidHeight[%d]=%.4f\n", k, net->semi.sem_plh[nidx[i]][2]);
            /* �������W �W�I�C�h�� */
            fprintf(fp, "AC_OriginGeoidHeight[%d]=%.4f\n", k, net->semi.sem_plh[nidx[i]][2] - net->semi.sem_plh[nidx[i]][3]);
			/* �������W �W�� */			
            fprintf(fp, "AC_OriginTrueHeight[%d]=%.4f\n", k, net->semi.sem_plh[nidx[i]][3]);
        }
		else
		{
			/* �����ւ̕␳�� B */
            fprintf(fp, "AC_CorrectOriginB[%d]=0.0\n", k);
			/* �����ւ̕␳�� L */ 
            fprintf(fp, "AC_CorrectOriginL[%d]=0.0\n", k);
			/* �����ւ̕␳�� �ȉ~�̍� */ 
            fprintf(fp, "AC_CorrectOriginEllipsoidHeight[%d]=0.0\n", k);
			/* B[j] */ 
            fprintf(fp, "AC_OriginB[%d]=0.0\n", k);
			/* L[j] */		
            fprintf(fp, "AC_OriginL[%d]=0.0\n", k);
			/* �ȉ~�̍�[j] */	
            fprintf(fp, "AC_OriginEllipsoidHeight[%d]=0.0\n", k);
			/* �������W �W�I�C�h�� */							
            fprintf(fp, "AC_OriginGeoidHeight[%d]=0.0\n", k);
			/* �������W �W�� */			
            fprintf(fp, "AC_OriginTrueHeight[%d]=0.0\n", k);
		}
		k++;
	}

	/* ����x�N�g�� */
	fprintf(fp, "AC_NumberBaselines=%d\n",net->rel.n); 
	for (i=0;i<net->rel.n;i++)
	{
		sidx = net->rel.r1[i];
		eidx = net->rel.r2[i];
		
		/* �N�_�ԍ� */
        fprintf(fp, "AC_StartPointNumber[%d]=%s\n", i, net->rcv.name1[net->rel.r1[i]]);
		/* �N�_���� */
		fprintf(fp, "AC_StartPointName[%d]=%s\n", i, net->rcv.name2[net->rel.r1[i]]);	
		/* �I�_�ԍ� */
        fprintf(fp, "AC_EndPointNumber[%d]=%s\n", i, net->rcv.name1[net->rel.r2[i]]);
		/* �I�_���� */
		fprintf(fp, "AC_EndPointName[%d]=%s\n", i, net->rcv.name2[net->rel.r2[i]]);		
		/* ��X[k] */
        fprintf(fp, "AC_ObservationDeltaX[%d]=%5.4f\n", i, net->rel.rel[i][0]);
		/* ��Y[k] */
        fprintf(fp, "AC_ObservationDeltaY[%d]=%5.4f\n", i, net->rel.rel[i][1]);
		/* ��Z[k] */
        fprintf(fp, "AC_ObservationDeltaZ[%d]=%5.4f\n", i, net->rel.rel[i][2]);
		/* ��X*��X[k] */
        fprintf(fp, "AC_VarCovDXDX[%d]=%e\n", i, net->rel.cov[i][0]);
		/* ��X*��Y[k] */
        fprintf(fp, "AC_VarCovDXDY[%d]=%e\n", i, net->rel.cov[i][3]);
		/* ��X*��Z[k] */
        fprintf(fp, "AC_VarCovDXDZ[%d]=%e\n", i, net->rel.cov[i][5]);
		/* ��Y*��Y[k] */
        fprintf(fp, "AC_VarCovDYDY[%d]=%e\n", i, net->rel.cov[i][1]);
		/* ��Y*��Z[k] */
        fprintf(fp, "AC_VarCovDYDZ[%d]=%e\n", i, net->rel.cov[i][4]);
		/* ��Z*��Z[k] */
        fprintf(fp, "AC_VarCovDZDZ[%d]=%e\n", i, net->rel.cov[i][2]);


		if (net->rcv.est[sidx] == 0)
		{
            sxyz[0] = net->rcv.pos[sidx][0];
            sxyz[1] = net->rcv.pos[sidx][1];
            sxyz[2] = net->rcv.pos[sidx][2];
        }
		else
		{
            if (bsemi)
            {
                sxyz[0] = net->semi.pos_xyz[nidx[sidx]][0];
                sxyz[1] = net->semi.pos_xyz[nidx[sidx]][1];
                sxyz[2] = net->semi.pos_xyz[nidx[sidx]][2];
            }else{
                sxyz[0] = net->nosemi.pos_xyz[nidx[sidx]][0];
                sxyz[1] = net->nosemi.pos_xyz[nidx[sidx]][1];
                sxyz[2] = net->nosemi.pos_xyz[nidx[sidx]][2];
            }
		}

		if (net->rcv.est[eidx] == 0)
		{
            exyz[0] = net->rcv.pos[eidx][0];
            exyz[1] = net->rcv.pos[eidx][1];
            exyz[2] = net->rcv.pos[eidx][2];
        }
		else
		{
            if (bsemi)
            {
                exyz[0] = net->semi.pos_xyz[nidx[eidx]][0];
                exyz[1] = net->semi.pos_xyz[nidx[eidx]][1];
                exyz[2] = net->semi.pos_xyz[nidx[eidx]][2];

            }else{
                exyz[0] = net->nosemi.pos_xyz[nidx[eidx]][0];
                exyz[1] = net->nosemi.pos_xyz[nidx[eidx]][1];
                exyz[2] = net->nosemi.pos_xyz[nidx[eidx]][2];

            }
        }

		/* ���ϒl ��X */
        fprintf(fp, "AC_AverageDeltaX[%d]=%5.4f\n", i, exyz[0] - sxyz[0]);
		/* ���ϒl ��Y */
        fprintf(fp, "AC_AverageDeltaY[%d]=%5.4f\n", i, exyz[1] - sxyz[1]);
		/* ���ϒl ��Z */ 
        fprintf(fp, "AC_AverageDeltaZ[%d]=%5.4f\n", i, exyz[2] - sxyz[2]);
		
        calcdata_1[0]=floor((exyz[0] - sxyz[0])*10000.0+0.5)/10000.0;
        calcdata_1[1]=floor((exyz[1] - sxyz[1])*10000.0+0.5)/10000.0;
        calcdata_1[2]=floor((exyz[2] - sxyz[2])*10000.0+0.5)/10000.0;
        calcdata_2[0]=floor((net->rel.rel[i][0])*1000.0+0.5)/1000.0;
        calcdata_2[1]=floor((net->rel.rel[i][1])*1000.0+0.5)/1000.0;
        calcdata_2[2]=floor((net->rel.rel[i][2])*1000.0+0.5)/1000.0;

		/* �c�� ��X */
//        fprintf(fp, "AC_ResidualDeltaX[%d]=%5.4f\n", i, (exyz[0] - sxyz[0]) - net->rel.rel[i][0] );
		/* �c�� ��Y */
//        fprintf(fp, "AC_ResidualDeltaY[%d]=%5.4f\n", i, (exyz[1] - sxyz[1]) - net->rel.rel[i][1] );
		/* �c�� ��Z */
//        fprintf(fp, "AC_ResidualDeltaZ[%d]=%5.4f\n", i, (exyz[2] - sxyz[2]) - net->rel.rel[i][2] );
        /* �c�� ��X */
        fprintf(fp, "AC_ResidualDeltaX[%d]=%5.4f\n", i, (calcdata_1[0] - calcdata_2[0] ));
        /* �c�� ��Y */
        fprintf(fp, "AC_ResidualDeltaY[%d]=%5.4f\n", i, (calcdata_1[1] - calcdata_2[1] ));
        /* �c�� ��Z */
        fprintf(fp, "AC_ResidualDeltaZ[%d]=%5.4f\n", i, (calcdata_1[2] - calcdata_2[2] ));


    }

    fclose(fp);
    return 0;
}
/* �����̒P������͌��ʂ̖ԕ��� */
int netave(net_t* net)
{
	solut_t		semi;
	solut_t		nosemi;
	semidyna_t* semidyna;
	int			bsemi=0;
	
	if (!net)
	{
		return -1;
	}

	/* �Z�~�_�C�i�~�b�N�p�����[�^���� */
	semidyna = semi_read(net->fopt.fsem);
	if (semidyna)
	{
		bsemi = 1;
		memcpy(&net->semidyna, semidyna, sizeof(semidyna_t));
		SAFE_FREE(semidyna);
	}
	
	/* �W�I�C�h�f�[�^�̓��� */
	if (!(opengeoid(net->model, net->fopt.fgeo)))
	{
		SAFE_FREE(net->semidyna.data);
		memset(&net->semidyna, 0, sizeof(semidyna_t));
		closegeoid();
        return -2;
	}

	/* �Z�~�_�C�i�~�b�N�␳�Ȃ��̖ԕ��� */
	netaveex(net, SEMIDYNA_NO);
	memcpy(&nosemi, &net->nosemi, sizeof(solut_t));

	/* �Z�~�_�C�i�~�b�N�␳����̖ԕ��� */
	if (bsemi)
	{
		netaveex(net, SEMIDYNA_YES);
		memcpy(&semi, &net->semi, sizeof(solut_t));
	}
	
	/* ���������͌��ʃt�@�C���̏o�� */
	if (savenet(net) != 0)
	{
		closegeoid();
        return -3;
	}

	closegeoid();

	return 0;
}

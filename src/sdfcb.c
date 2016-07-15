/*------------------------------------------------------------------------------
* sdfcb.c : SD-FCB estimation
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
#include <stdlib.h>
#include <math.h>
#include "rtklib.h"

/* ��d���p�X�g�ݍ��킹�̍쐬 */
void pairsdmv(mbs_t *mbs, fcb_t *fcb)
{
	/* �����t���[ No.0 */
	/* �ϐ���` */
	int i,j;		/* ���[�v�J�E���^ */
	double d;       /* Melbourne-Wubbena���^������d�� */
	gtime_t ti, tj; /* pass�f�[�^�G�|�b�N */
	sdd_t sdd={{0}};/* ��d���f�[�^ */
	pass_t pass=mbs->pass;

	/* �����t���[ No.1 */
	/* �q���y�A���[�v */
	for(i=0;i<pass.n -1;i++)
	{
		if(satsys(pass.data[i].sat,NULL)!=SYS_GPS) continue;

		for(j=i+1;j<pass.n -1;j++)
		{
			/* GPS�ȊO�̉q���͑ΏۊO */
			if(satsys(pass.data[j].sat,NULL)!=SYS_GPS) continue;
			/* ����q���̈�d���͌v�Z���Ȃ��i���R�j */
			if( pass.data[i].sat == pass.data[j].sat ) continue;
			/* �p�X�i�ϑ����A�����Ă����ԁj���d�����Ȃ��q���y�A�͑ΏۊO */
			if(0==check_oltime(&pass.data[i].ts,&pass.data[i].te,&pass.data[j].ts,&pass.data[j].te,0.0,&ti,&tj)) continue;
			/* �ǂ��قȂ�y�A�̈�d���͌v�Z���Ȃ��i�z��data�͋ǁA�q���A�����Ń\�[�g�ς݁j */
			if(pass.data[i].sta != pass.data[j].sta)
			{
				break;
			}


			/* �����t���[ No.2 */
			/* ��d�����ʂ̌v�Z */
			sdd.s1 = pass.data[i].sat;
			sdd.s2 = pass.data[j].sat;
			sdd.r = pass.data[i].sta;
				/* Melbourne-Wubbena���^����(L1-L2) */
			d = pass.data[i].mw -pass.data[j].mw;
			sdd.mwv = pass.data[i].mwv+pass.data[j].mwv;/* MW���U(cycle) */
			sdd.mwf = fmod(d,1.0);						/* MW��������[0.0:1.0)(cycle) */
			if(sdd.mwf<0) sdd.mwf+=1.0;
			sdd.mwi = floor(d);			/* MW��������(cycle) */
				/* �d���w�t���[���`����(L1-L2) */
			sdd.lc = pass.data[i].lcamb - pass.data[j].lcamb;	/* LC�A���r�M���C�e�B(m) */
			sdd.lcv = pass.data[i].lcambv + pass.data[j].lcambv;/* LC�A���r�M���C�e�B���U(m) */
			ti = pass.data[i].ts;
			tj = pass.data[j].ts;
			sdd.ts = timediff(ti,tj)>0.0?ti:tj;
			ti = pass.data[i].te;
			tj = pass.data[j].te;
			sdd.te = timediff(ti,tj)>0.0?ti:tj;
			sdd.exc = mbs->stas.sta[pass.data[i].sta].cctype;

			/* �����t���[ No.3 */
			/* ��d���f�[�^�̒ǉ� */
			addsdpass(fcb,&sdd);
		}
	}
}

///* ��d���p�X�g�ݍ��킹�̍쐬 */
//void pairsdmv(pass_t pass, fcb_t *fcb)
//{
//	/* �����t���[ No.0 */
//	/* �ϐ���` */
//	int i,j;		/* ���[�v�J�E���^ */
//	double d;       /* Melbourne-Wubbena���^������d�� */
//	gtime_t ti, tj; /* pass�f�[�^�G�|�b�N */
//	sdd_t sdd={{0}};/* ��d���f�[�^ */
//
//	/* �����t���[ No.1 */
//	/* �q���y�A���[�v */
//	for(i=0;i<pass.n -1;i++)
//	{
//		if(satsys(pass.data[i].sat,NULL)!=SYS_GPS) continue;
//
//		for(j=i+1;j<pass.n -1;j++)
//		{
//			/* GPS�ȊO�̉q���͑ΏۊO */
//			if(satsys(pass.data[j].sat,NULL)!=SYS_GPS) continue;
//			/* ����q���̈�d���͌v�Z���Ȃ��i���R�j */
//			if( pass.data[i].sat == pass.data[j].sat ) continue;
//			/* �p�X�i�ϑ����A�����Ă����ԁj���d�����Ȃ��q���y�A�͑ΏۊO */
//			if(0==check_oltime(&pass.data[i].ts,&pass.data[i].te,&pass.data[j].ts,&pass.data[j].te,0.0,&ti,&tj)) continue;
//			/* �ǂ��قȂ�y�A�̈�d���͌v�Z���Ȃ��i�z��data�͋ǁA�q���A�����Ń\�[�g�ς݁j */
//			if(pass.data[i].sta != pass.data[j].sta)
//			{
//				break;
//			}
//
//
//			/* �����t���[ No.2 */
//			/* ��d�����ʂ̌v�Z */
//			sdd.s1 = pass.data[i].sat;
//			sdd.s2 = pass.data[j].sat;
//			sdd.r = pass.data[i].sta;
//				/* Melbourne-Wubbena���^����(L1-L2) */
//			d = pass.data[i].mw -pass.data[j].mw;
//			sdd.mwv = pass.data[i].mwv+pass.data[j].mwv;/* MW���U(cycle) */
//			sdd.mwf = fmod(d,1.0);						/* MW��������[0.0:1.0)(cycle) */
//			if(sdd.mwf<0) sdd.mwf+=1.0;
//			sdd.mwi = floor(d);			/* MW��������(cycle) */
//				/* �d���w�t���[���`����(L1-L2) */
//			sdd.lc = pass.data[i].lcamb - pass.data[j].lcamb;	/* LC�A���r�M���C�e�B(m) */
//			sdd.lcv = pass.data[i].lcambv + pass.data[j].lcambv;/* LC�A���r�M���C�e�B���U(m) */
//			ti = pass.data[i].ts;
//			tj = pass.data[j].ts;
//			sdd.ts = timediff(ti,tj)>0.0?ti:tj;
//			ti = pass.data[i].te;
//			tj = pass.data[j].te;
//			sdd.te = timediff(ti,tj)>0.0?ti:tj;
//
//			/* �����t���[ No.3 */
//			/* ��d���f�[�^�̒ǉ� */
//			addsdpass(fcb,&sdd);
//		}
//	}
//}


/* WL-SD-FCB ���ϒl�y�ѕ��U�̎Z�o */
void calwlsdfcb(fcb_t *fcb/*, prcopt *popt*/)
{
	/* �����t���[ No.0 */
	/* �ϐ������� */
	int ni, nj, nij;/* �z��C���f�b�N�X */
	int i;			/* ���[�v�J�E���^ */
	for(i=0;i<NSATGPS*(NSATGPS-1)/2;i++){
		fcb->wlfcb[i].n = 0;
		fcb->wlfcb[i].ave = 0.0;
		fcb->wlfcb[i].var = 0.0;
	}

	/* �����t���[ No.1 */
	/* MW������d���̏������̕��ςƕ��U�̎Z�o(�S�ǂŌv�Z�������v�l) */
	for(i=0;i<fcb->n;i++){
		if ( fcb->sdd[i].exc == 1 ) continue;
		ni=fcb->sdd[i].s1;
		nj=fcb->sdd[i].s2;
		nij=(ni-1)*(2*NSATGPS-ni)/2+nj-ni-1;
		fcb->wlfcb[nij].ave += fcb->sdd[i].mwf;
		fcb->wlfcb[nij].var += pow(fcb->sdd[i].mwf,2);
		fcb->wlfcb[nij].n ++;
		/*debug*///printf("debug: r=%d s1=%d s2=%d mwf=%8.6f\n",fcb->sdd[i].r,fcb->sdd[i].s1,fcb->sdd[i].s2,fcb->sdd[i].mwf);
	}
	for(i=0;i<NSATGPS*(NSATGPS-1)/2;i++){
		if (fcb->wlfcb[i].n == 0 ) continue;
		fcb->wlfcb[i].ave /= (double)fcb->wlfcb[i].n;
		fcb->wlfcb[i].var /= (double)fcb->wlfcb[i].n;
		fcb->wlfcb[i].var -= pow(fcb->wlfcb[i].ave,2);
	}
}

/* WL�A���r�M���C�e�BFIX���� */
int fixwlsdfcb(fcb_t *fcb, prcopt_t *popt)
{
	/* �����t���[ No.0 */
	/* �ϐ������� */
	int fixflg=0;/* FIX����t���O FIX��80��? 0:OK 1:NG  */
	int ni, nj, nij;/* �z��C���f�b�N�X */
	int i, j;			/* ���[�v�J�E���^ */
	double nw, nwfix, nwv;
	double p;
	int nrfix=0, nr=0, nf=0;

	/* �����t���[ No.1 */
	/* ��d���f�[�^���������� */
	for( i=0;i<fcb->n;i++){
		if ( fcb->sdd[i].exc == 1 ) continue;
		ni=fcb->sdd[i].s1;
		nj=fcb->sdd[i].s2;
		nij=(ni-1)*(2*NSATGPS-ni)/2+nj-ni-1;
		nw = fcb->sdd[i].mwi + fcb->sdd[i].mwf - fcb->wlfcb[nij].ave;  /* ...(10) */
		nwfix = floor(nw+0.5);
		nwv = fcb->sdd[i].mwv + fcb->wlfcb[nij].var;
		if ( fabs(nwv) < 1.0e-30 )
		{   /* TODO �[������G���[�o��
			showmsg("error: hogehoge");
			trace(1,"error: hogehoge");
			*/
			fcb->sdd[i].exc = 1;
			continue;
		}
		fcb->sdd[i].mwfix = check_intdd( popt->mopt.minconfw , nw, nwfix, nwv, &p);
		if(fcb->sdd[i].mwfix) nrfix++;
		else 
		{
			//printf("debug: r=%d s1=%d s2=%d f=%8.6f fave=%8.6f\n",fcb->sdd[i].r,fcb->sdd[i].s1,fcb->sdd[i].s2,fcb->sdd[i].mwf,fcb->wlfcb[nij].ave);
		}
		nr ++;
		/*
		*  �ǖ���FIX ���𔻒�B80%��菭�Ȃ���Ί��p�t���O�����ĂāA
		*  FIX ����l��NG �ɃZ�b�g
		*/
		if ( i+1==fcb->n || fcb->sdd[i].r != fcb->sdd[i+1].r )
		{
			if ( (double)nrfix / (double)nr < 0.8 )
			{
				for(j=i;j>i-nr;j--) fcb->sdd[j].exc = 1;
				fixflg = 1;
			}
			else
			{
				nf ++; /* 80%�ȏ�FIX �����ǐ����J�E���g */
			}
			nrfix = 0;
			nr = 0;
		}
	}

	/* �����t���[ No.2 */
	/* FIX�ǐ����� */
	if ( nf == 0 ) fixflg = 9;

	/* �����t���[ No.3 */
	/* �I������ */
	return fixflg;
}

/* �p�X����NL-SD-FCB�̎Z�o */
void calnlsdfcb(fcb_t *fcb/*, prcopt_t popt*/)
{
	/* �����t���[ No.0 */
	/* �ϐ������� */
	double b1k;
	double fai1, fai1v;
	int fain;
	int i;
	int j=0,k=0;
	gtime_t ti,tj;
	double nw;
	int ni, nj, nij;

	/* �����t���[ No.1 */
	/* �������� */
	for (i=0;i<fcb->n;i++) {


		if ( fcb->sdd[i].exc == 1 ) continue;
		if ( fcb->sdd[i].mwfix == 0 ) continue;
		
		fcb->sdd[i].b1fix = 1;

		/* �����t���[ No.2 */
		/* b1k�̎Z�o */
		ni=fcb->sdd[i].s1;
		nj=fcb->sdd[i].s2;
		nij=(ni-1)*(2*NSATGPS-ni)/2+nj-ni-1;
		nw = fcb->sdd[j].mwi + fcb->sdd[j].mwf - fcb->wlfcb[nij].ave;
		b1k = (FREQ1+FREQ2)/FREQ1 * fcb->sdd[i].lc - FREQ2/(FREQ1-FREQ2) * nw;

		/* �����t���[ No.3 */
		/* b1k�̐����E���������� */
		fcb->sdd[i].b1i = b1k>=0?floor(b1k+0.5):ceil(b1k-0.5);
		fcb->sdd[i].b1f = b1k - fcb->sdd[i].b1i;
	}
	/* �����t���[ No.4 */
	/* �I�� */
	return;
}

/* NL-SD-FCB�̃^�C���e�[�u���쐬 */
void makenlfcbtbl(fcb_t *fcb,double tintv)
{
	/* �����t���[ No.0 */
	/* �ϐ������� */
	int ni, nj;
	int nij; /* �q���y�A �C���f�b�N�X */
	//double tintv;
	gtime_t ts, te, tsdum, tedum;
	//int nlcnt[NSATGPS*(NSATGPS-1)/2][fcb->ntfcb]={0};
	int **nlcnt;
	int i, j, k; /* ���[�v�J�E���^ */
	int intnum; /* NL-FCB��Ԑ� */
	int maxpnum;  /* �q���y�A���� */
	intnum=fcb->ntfcb;
	maxpnum=NSATGPS*(NSATGPS-1)/2;

	nlcnt = (int**)malloc(sizeof(int*)*maxpnum);
	for (i=0;i<maxpnum;i++)
	{
	   nlcnt[i] = (int*)malloc(sizeof(int)*intnum);
	}

	/* �����t���[ No.1 */
	/* NL-SD-FCB �e�[�u�������ݒ� */
	//tintv = timediff( fcb->te, fcb->ts )/(double)fcb->ntfcb;
	for (i=0;i<intnum;i++)
	{
		ts = timeadd( fcb->ts, (double)i*tintv );
		te = timeadd( fcb->ts, (double)(i+1)*tintv );
		for(nij=0;nij<maxpnum;nij++)
		{
			fcb->nlfcb[nij*intnum+i].ts = ts;
			fcb->nlfcb[nij*intnum+i].te = te;
			fcb->nlfcb[nij*intnum+i].ave = 0.0;
			fcb->nlfcb[nij*intnum+i].var = 0.0;
		}
	}

	/* �����t���[ No.2 */
	/* NL-SD-FCB�̕��ρE���U�v�Z */
	for (i=0;i<fcb->n;i++)/* ��d�����[�v */
	{
		if ( fcb->sdd[i].b1fix == 0 ) continue;
		ni=fcb->sdd[i].s1;
		nj=fcb->sdd[i].s2;
		nij=(ni-1)*(2*NSATGPS-ni)/2+nj-ni-1;
		for (j=0;j<intnum;j++)
		{
			if ( check_oltime(	&fcb->sdd[i].ts,
								&fcb->sdd[i].te,
								&fcb->nlfcb[nij*intnum+j].ts,
								&fcb->nlfcb[nij*intnum+j].te,
								tintv/2.0,
								&tsdum,
								&tedum ) )
			{
				fcb->nlfcb[nij*intnum+j].ave += fcb->sdd[i].b1f;
				fcb->nlfcb[nij*intnum+j].var += pow(fcb->sdd[i].b1f,2 );
				nlcnt[nij][j] ++;
			}
		}
	}
	for (i=0;i<maxpnum;i++)
	{
		for (j=0;j<intnum;j++) {
			if(nlcnt[i][j]==0) continue;
			fcb->nlfcb[i*intnum+j].ave /= nlcnt[i][j];
			fcb->nlfcb[i*intnum+j].var /= nlcnt[i][j];
			fcb->nlfcb[i*intnum+j].var -= pow(fcb->nlfcb[i*intnum+j].ave,2);
		}
	}

	for (i=0;i<maxpnum;i++)
	{
		free(nlcnt[i]);
	}
	free(nlcnt);

	/* �����t���[ No.3 */
	/* �I�� */
	return;
}

/* SD-FCB�t�@�C���o�� */
int outputfcb(prcopt_t *popt, filopt_t *fopt, fcb_t *fcb, solopt_t *sopt)
{
	/* �����t���[ No.0 */
	/* �ϐ���` */
	FILE *fp;      /* �o�̓t�@�C���|�C���^ */
	char ts[32]={"\0"}, te[32]={"\0"}; /* �G�|�b�N������ */
	int nwl;/* WL-FCB�f�[�^�� */
	int nnl;/* NL-FCN��Ԑ� */
	int itvlnl;/* NL-FCB��ԊԊu */
	int i,j;/* ���[�v�J�E���^ */

	if(sopt->fcbout==FCBOUTOPT_OFF) return 0;

	/* �����t���[ No.1 */
	/* �t�@�C���I�[�v�� */
	if(NULL==(fp=fopen(sopt->fcb,"w")))
	{
		showmsg("error: fcb file open");
		trace(1,"error: fcb file open");
		return 1;
	}

	/* �R�����g�������� */
	fprintf(fp,"# file-satantfile=%s\n",fopt->satantp);
	fprintf(fp,"# file-rcvantfile=%s\n",fopt->rcvantp);

	/* �����t���[ No.2 */
	/* �w�b�_�������� */
	time2str(fcb->ts,ts,6);
	time2str(fcb->te,te,6);
	nwl=NSATGPS*(NSATGPS-1)/2;
//	fprintf(fp,"%s %s %d %d %d\n",
//		ts,te,nwl,fcb->ntfcb,popt->nlfcbitvl);
	fprintf(fp,"%s %s %d %d %d\n",
		ts,te,nwl,fcb->ntfcb,popt->mopt.tifcb);

	fprintf(fp,"\n");

	/* �����t���[ No.3 */
	/* WL-FCB�������� */
	for(i=0;i<nwl;i++)
	{
		 fprintf(fp,"%d %.6f %.6f\n",
			fcb->wlfcb[i].n, fcb->wlfcb[i].ave, fcb->wlfcb[i].var);
	}
	fprintf(fp,"\n");

	/* �����t���[ No.4 */
	/* NL-FCB�������� */
	nnl=fcb->ntfcb;
	for(i=0;i<nnl;i++)
	{
		for(j=0;j<nwl;j++)
		{
			time2str(fcb->nlfcb[nnl*j+i].ts,ts,6);
			fprintf(fp,"%s %.6f %.6f\n",
			ts, fcb->nlfcb[nnl*j+i].ave, fcb->nlfcb[nnl*j+i].var);
		}
		fprintf(fp,"\n");
	}

	/* �����t���[ No.5 */
	/* �t�@�C���N���[�Y */
	if(NULL!=fp)
	{
		fclose(fp);
	}

	/* �����t���[ No.6 */
	/* �I������ */
	return 0;
}

/* Cross-Correlation Receiver �e�[�u���t�@�C���ǂݍ��� */
int readCC(const char *file, char rcvname[MAXRECTYP][256]){

	FILE *fp;
	char buff[1024],ori[34]="Cross-Correlation Receivers Table";
	int line=1, nod=0;
	int len = 0;
	const int nType = 20;
	const int nBias = 22;

	trace(3,"readL2C: file=%s\n",file);

	if((fp=fopen(file,"r"))==NULL){
		trace(1,"cross-correlation receiver table file open error: %s",file);
		return -1;
	}

	while(fgets(buff,sizeof(buff),fp)){
		/*�^�C�g���`�F�b�N*/
		if(line==1){
//			/*�擪�����r*/
//			if(((strncmp(buff,ori,32)))!=0){
//				showmsg("cross-correlation receiver table file title error: %s",file);
//				trace(1,"cross-correlation receiver table file title error: %s\n",file);
//				fclose(fp);
//				return -1;
//			}
		}
		if(line>4){
			len = strlen(buff);
			if(buff[0] != '\n') {
				strncpy(rcvname[nod], buff, len-1);
				trim(rcvname[nod]);
				/* RINEX Header�Ɠ����t�H�[�}�b�g(A20)���`�F�b�N */
				if(strlen(rcvname[nod])>20)
				{
					showmsg("invalid receiver type name: file=%s line=%d\n",file,line);
					trace(2,"invalid receiver type name: file=%s line=%d\n",file,line);
				}
				nod++;
			}

			/*�f�[�^�����ő�l�ȏォ�ǂ���*/
			if(nod>=MAXRECTYP){
				showmsg("cross-correlation receiver table file size error: %s",file);
				trace(1,"cross-correlation receiver table file size error: %s\n",file);
				fclose(fp);
				return -1;
			}
		}
		line++;
	}
	fclose(fp);
	trace(4,"number of readCC data: %d\n",nod);
	if(nod <1){
		showmsg("no cross-correlation receiver data error: %s\n",file);
		trace(1,"no cross-correlation receiver data error: %s\n",file);
	}
	return nod;
}

/* cross-correlation receiver �`�F�b�N */
int checkccrcv(char *file, mbs_t *mbs)
{
	char ccrcv[MAXRECTYP][256];
	int nod=0;
	int i,j;

	if(NULL==file) return 1;

	nod=readCC(file, ccrcv);

	if(nod>0)
	{
		for(i=0;i<mbs->stas.nsta;i++)
		{
			for(j=0;j<nod;j++)
			{
				if(0==strcmp(mbs->stas.sta[i].rectype, ccrcv[j]))
				{
					mbs->stas.sta[i].cctype=1;
					break;
                }
			}
		}
	}

	return 0;
}

/* SD-FCB�Z�o */
int calsdfcb(mbs_t *mbs, prcopt_t *popt, filopt_t *fopt, solopt_t *sopt)
{
	/* �����t���[ No.0 */
	/* �ϐ���` */
	fcb_t *fcb; /* FCB�\���� */
	int status;	/* �����X�e�[�^�X 0:���� 1:�ُ� */
	int nt;		/* NL-SD-FCB�e�[�u����Ԑ� */
	double elipse;/* ���ʏ����o�ߎ��ԁi�b�j */
	double dt;/* NL-SD-FCB�����ԁi�b�j */

	/* �A���e�i�␳�t�@�C�����̓`�F�b�N */
	if( NULL==fopt->satantp)
	{
		showmsg("error : no satantfile file");
		trace(1,"no satantfile file\n");
		return 1;
	}
	if(NULL==fopt->rcvantp)
	{
		showmsg("error : no rcvantfile file");
		trace(1,"no rcvantfile file\n");
		return 1;
	}

	/* Cross-Correlation �^�C�v��M�@�t���O�ݒ� */
	checkccrcv(fopt->cc,mbs);


	/* �����t���[ No.1 */
	/* FCB�i�[�̈�m�� */
	elipse = timediff(mbs->te, mbs->ts);
//	dt=(double)(popt->nlfcbitvl);
	dt=(double)(popt->mopt.tifcb);
	nt=(int)(elipse/dt)+1;
	fcb=fcbnew(nt);
	fcb->ts=mbs->ts;
	fcb->te=mbs->te;

	/* �����t���[ No.2 */
	/* ��d���p�X�̍쐬 */
	//pairsdmv(mbs->pass, fcb);
	pairsdmv(mbs, fcb);

	/* �����t���[ No.3 */
	/* WL-SD-FCB ���ϋy�ѕ��U�̎Z�o */
	while(1)
	{
		calwlsdfcb(fcb);

		/* �����t���[ No.4 */
		/* WL�A���r�M���C�e�BFIX���� */
		status = fixwlsdfcb(fcb, popt);
		if(0==status) break;
		else if(1==status) continue;
		else if(9==status) return 1;
		else return 1;
	}

	/* �����t���[ No.5 */
	/* �p�X����NL-SD-FCB�̎Z�o */
	calnlsdfcb(fcb/* , pass*/);

	/* �����t���[ No.6 */
	/* NL-SD-FCB�̃^�C���e�[�u���쐬 */
	makenlfcbtbl(fcb,dt);

	/* �����t���[ No.7 */
	/* SD-FCB�t�@�C���o�� */
	outputfcb(popt, fopt, fcb, sopt);

	/* �����t���[ No.8 */
	/* �I������ */
	return status;
}



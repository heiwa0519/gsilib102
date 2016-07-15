/*------------------------------------------------------------------------------
* H23func : H23 functions
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

#include "rtklib.h"
#include <stdio.h>
#include <stdlib.h>

/* constants/macros ----------------------------------------------------------*/
#define CIRCULARTHAEDER1 "0h UTC    MJD    C0/ns   N0   C0'/ns   N0'   C1/ns   N1   C1'/ns   N1'"
#define CIRCULARTHAEDER2 "0h UTC   MJD        C0/ns    N0             C1/ns    N1"
#define CIRCULAREND "6 - Time links used for the computation of TAI and their uncertainties."


/* add ifb data --------------------------------------------------------------*/
static int addifbdata(nav_t *nav, const ifb_t *data)
{
    ifb_t *ifb_data;
	int i,count;

    if (nav->nifbsmax <= nav->nifbs) {
        if (nav->nifbsmax<=0) nav->nifbsmax=MAXRECTYP; else nav->nifbsmax*=2;
		if (!(ifb_data=(ifb_t *)realloc(nav->ifbs, sizeof(ifb_t)*nav->nifbsmax))) {
            trace(1,"addifbdata: memalloc error n=%dx%d\n",sizeof(ifb_t),nav->nifbsmax);
            free(nav->ifbs); nav->ifbs=NULL; nav->nifbs=nav->nifbsmax=0;
            return -1;
        }
        nav->ifbs=ifb_data;
    }
	count = nav->nifbs;
    nav->ifbs[nav->nifbs++]=*data;

	trace(5,"ifbdata number [%d]\n",count);
	trace(5,"nav->ifbs->rec1: %s\n",nav->ifbs[count].rec1);
	trace(5,"nav->ifbs->rec2: %s\n",nav->ifbs[count].rec2);
	for(i=0;i<NFREQ;i++){
		trace(5,"nav->ifbs->ifbvar[%d]: %f\n",i,nav->ifbs[count].ifbvar[i]);
	}

	return 1;
}

/* readifb---------------------------------------------------
* GLONASS�@IFB�e�[�u���̓ǂݍ���
* args   : char *file       I   IFB�e�[�u���t�@�C��
*          nav_t *nav       IO  �q�@�f�[�^
* return : int				�X�e�[�^�X (0:ok,0>:error,1:aborted)
*--------------------------------------------------------------*/
int readifb(const char *file, nav_t *nav){
	FILE *fp;
	char buff[1024],ori[17]="GLONASS IFB TABLE",cd[12];
    int countheader=1, midcount=1;
	int dataflag = 0;
	ifb_t data = { 0 };
	ifb_t dataZero = { 0 };

	trace(3,"readifb: file=%s\n",file);

	if((fp=fopen(file,"r"))==NULL){
		showmsg("ifb file open error: %s",file);
		trace(1,"ifb file open error: %s\n",file);
		return -1;
	}

	free(nav->ifbs);
	nav->ifbs=NULL;
	nav->nifbs=0;
	nav->nifbsmax=0;

	while(fgets(buff,sizeof(buff),fp)){

		/*�^�C�g���`�F�b�N*/
		if(countheader==1){
			/*�擪�����r*/
			if(((strncmp(buff,ori,17)))!=0){
				showmsg("ifb file title error: %s",file);
				trace(1,"ifb file title error: %s\n",file);
				fclose(fp);
                return -2;
			}
		}
		if(countheader>6){
			/* �Ԃɋ�s������ꍇ�ǂݔ�΂� */
			if(strcmp(buff,"\n")==0){
				continue;
			}

			if (buff[0]!=' '){
            /* 1�s�ړǍ�(��M�@���P�A��M�@���Q�AL1��IFB�␳�l) */
				if((strncmp(buff+20,"  ",2)==0)&&(strncmp(buff+42,"       ",7)==0)&&(strncmp(buff+50,"      ",6)==0)){

					/* �f�[�^�L�̏ꍇ�A�ǉ����Ă���(�f�[�^�ؑւ莞) */
					if (dataflag == 1) {
						int ret = addifbdata(nav, &data);
						if (ret < 0) {
							fclose(fp);
							return -3;
						}
					}
					data=dataZero;
					midcount=1;
                    setstr(data.rec1, buff, 20);
                    setstr(data.rec2, buff+22, 20);
					strncpy(cd, buff+56, 12);
                    data.ifbvar[0]=atof(cd);
					midcount++;
					dataflag = 1;
				}else{
					showmsg("ifb file format error: %s",file);
					trace(1,"ifb file format error: %s\n",file);
					fclose(fp);
					return -4;
				}
			}else{
				if(midcount<4){
					/* 2�s�ڈȍ~�Ǎ�(L2��L3��IFB�␳�l) */
				if(strncmp(buff+50,"      ",6)==0){
					strncpy(cd, buff+56, 12);
						data.ifbvar[midcount-1]=atof(cd);
					midcount++;
				}else{
					showmsg("ifb file format error: %s",file);
					trace(1,"ifb file format error: %s\n",file);
					fclose(fp);
                        return -5;
					}
				}else{
					/* 2�s�ڈȍ~�Ǎ�(L2��L5��IFB�␳�l) */
					showmsg("ifb file format error: %s",file);
					trace(1,"ifb file format error: %s\n",file);
					fclose(fp);
                    return -6;
				}
			}
		}
		countheader++;
	}
	fclose(fp);

	if (dataflag == 1) {
		/* �f�[�^�L�̏ꍇ�A�ǉ����Ă���(�Ō�̃f�[�^) */
		int ret = addifbdata(nav, &data);
		if (ret < 0) {
			return -7;
		}
	}else{
		showmsg("no ifb data error: %s\n",file);
		trace(1,"no ifb data error: %s\n",file);

	}


	return 0;
}

/* add err data --------------------------------------------------------------*/
static int adderrdata(nav_t *nav, const err_t *data)
{
	err_t *err_data;
	int i,count;

	if (nav->nerrsmax <= nav->nerrs) {
		if (nav->nerrsmax<=0) nav->nerrsmax=MAXRECANT; else nav->nerrsmax*=2;
		if (!(err_data=(err_t *)realloc(nav->errs, sizeof(err_t)*nav->nerrsmax))) {
			trace(1,"adderrdata: memalloc error n=%dx%d\n",sizeof(err_t),nav->nerrsmax);
			free(nav->errs); nav->errs=NULL; nav->nerrs=nav->nerrsmax=0;
            return -1;
        }
		nav->errs=err_data;
	}
	count = nav->nerrs;
	nav->errs[nav->nerrs++]=*data;

	trace(5,"readerr date number [%d]\n",nav->nerrs-1);
	trace(5,"nav->rectype: %s\n",nav->errs[count].rectype);
	trace(5,"nav->anttype: %s\n",nav->errs[count].anttype);
	for(i=0;nav->errs[count].sys[i]>0;i++){
		trace(5,"nav->sys: %d\n",nav->errs[count].sys[i]);
		trace(5,"nav->sig: %s\n",nav->errs[count].sig[i]);
		trace(5,"nav->errvar[0]: %f\n",nav->errs[count].errvar[i][0]);
		trace(5,"nav->errvar[1]: %f\n",nav->errs[count].errvar[i][1]);
	}
	return 1;
}

/* readerr---------------------------------------------------
* �ϑ��덷���f���e�[�u���̓ǂݍ���
* args   : char *file       I   �ϑ��덷���f���e�[�u���t�@�C��
*          nav_t *nav       IO  �q�@�f�[�^
* return : int				�X�e�[�^�X (0:ok,0>:error,1:aborted)
*--------------------------------------------------------------*/
int readerr(const char *file, nav_t *nav){

	FILE *fp;
	char buff[1024],ori[11]="ERROR MODEL",cd[20];
    int countheader=1, midcount=0;
	int sys=0;
	double s1,s2;
    int dataflag = 0;
    err_t data = { 0 };
    err_t dataZero = { 0 };

	trace(3,"readerr: file=%s\n",file);

	free(nav->errs);
	nav->errs=NULL;
	nav->nerrs=0;
	nav->nerrsmax=0;

	if((fp=fopen(file,"r"))==NULL){
		showmsg("err file open error: %s",file);
		trace(1,"err file open error: %s\n",file);
		return -1;
	}

	while(fgets(buff,sizeof(buff),fp)){
		/*�^�C�g���`�F�b�N*/
		if(countheader==1){
			/*�擪�����r*/
			if(((strncmp(buff,ori,11)))!=0){
				showmsg("err file title error: %s",file);
				trace(1,"err file title error: %s\n",file);
				fclose(fp);
                return -2;
			}
		}
		if(countheader>6){
			if (buff[0]!=' '){
                /* 1�s�� */
				if((strncmp(buff+20,"  ",2)==0)&&(strncmp(buff+42,"  ",2)==0)&&(strncmp(buff+47,"  ",2)==0)&&(strncmp(buff+52,"  ",2)==0)&&(strncmp(buff+59,"  ",2)==0)){
                    if (dataflag == 1) {
                        /* �f�[�^�L�̏ꍇ�A�ǉ����Ă���(�f�[�^�ؑւ莞) */
                        int ret = adderrdata(nav, &data);
                        if (ret < 0) {
							fclose(fp);
							return -3;
						}

						/* �f�[�^�N���A */
                        data = dataZero;
					}
                    dataflag = 1;

                    midcount=0;
                    setstr(data.rectype, buff, 20);
//                    setstr(data.anttype, buff+22, 20);
					strncpy(data.anttype, buff+22, 20);
					if      (!strncmp(buff+44,"GPS",3)) sys=SYS_GPS;
					else if (!strncmp(buff+44,"GLO",3)) sys=SYS_GLO;
					else if (!strncmp(buff+44,"GAL",3)) sys=SYS_GAL;
					else if (!strncmp(buff+44,"QZS",3)) sys=SYS_QZS;
					else if (!strncmp(buff+44,"CMP",3)) sys=SYS_CMP;

                    data.sys[midcount]=sys;
                    setstr(data.sig[midcount], buff+49,3);

					setstr(cd, buff+54,20);
                    sscanf(cd,"%lf  %lf",&s1,&s2);
                    data.errvar[midcount][0]=s1;
                    data.errvar[midcount][1]=s2;
					midcount++;
                    if(midcount>=MAXERRDAT){
						showmsg("err file size error: %s",file);
						trace(1,"err file size error: %s\n",file);
						fclose(fp);
						return -4;
					}
				}else{
					showmsg("err file format error: %s",file);
					trace(1,"err file format error: %s\n",file);
					fclose(fp);
                    return -5;
				}
			}else{
                /* 2�s��->N�s�� */
				if((strncmp(buff+47,"  ",2)==0)&&(strncmp(buff+52,"  ",2)==0)&&(strncmp(buff+59,"  ",2)==0)){

					if      (!strncmp(buff+44,"GPS",3)) sys=SYS_GPS;
					else if (!strncmp(buff+44,"GLO",3)) sys=SYS_GLO;
					else if (!strncmp(buff+44,"GAL",3)) sys=SYS_GAL;
					else if (!strncmp(buff+44,"QZS",3)) sys=SYS_QZS;
					else if (!strncmp(buff+44,"CMP",3)) sys=SYS_CMP;

                    data.sys[midcount]=sys;
                    setstr(data.sig[midcount], buff+49,3);

					setstr(cd, buff+54,20);
                    sscanf(cd,"%lf  %lf",&s1,&s2);
                    data.errvar[midcount][0]=s1;
                    data.errvar[midcount][1]=s2;
					midcount++;
					if(midcount>=MAXERRDAT){
						showmsg("err file size error: %s",file);
						trace(1,"err file size error: %s\n",file);
						fclose(fp);
						return -6;
					}
				}else{
					showmsg("err file format error: %s",file);
					trace(1,"err file format error: %s\n",file);
					fclose(fp);
                    return -7;
				}
			}
		}
		countheader++;
	}
	fclose(fp);

	if (dataflag == 1) {
		/* �f�[�^�L�̏ꍇ�A�ǉ����Ă���(�Ō�̃f�[�^) */
		int ret = adderrdata(nav, &data);
		if (ret < 0) {
			return -8;
		}
	}else{
		showmsg("no err data error: %s\n",file);
		trace(1,"no err data error: %s\n",file);

	}
	return 0;
}

/* readL2C---------------------------------------------------
* 1/4�g���␳�e�[�u���̓ǂݍ���
* args   : char *file       I   1/4�g���␳�e�[�u���t�@�C��
*          nav_t *nav       IO  �q�@�f�[�^
* return : int				�X�e�[�^�X (0:ok,0>:error,1:aborted)
*--------------------------------------------------------------*/
int readL2C(const char *file, nav_t *nav){

	FILE *fp;
	char buff[1024],ori[32]="Quarter-Cycle Phase Shifts Table";
	int countheader=1, nod=0;
	int lenBuf = 0;
	const int nType = 20;
	const int nBias = 22;

	trace(3,"readL2C: file=%s\n",file);

	nav->sfts.n=0;
	memset(nav->sfts.rectyp,0,sizeof(nav->sfts.rectyp));

	if((fp=fopen(file,"r"))==NULL){
		trace(1,"l2c file open error: %s",file);
		return -1;
	}

	while(fgets(buff,sizeof(buff),fp)){
		/*�^�C�g���`�F�b�N*/
		if(countheader==1){
			/*�擪�����r*/
			if(((strncmp(buff,ori,32)))!=0){
				showmsg("l2c file title error: %s",file);
				trace(1,"l2c file title error: %s\n",file);
				fclose(fp);
				return -1;
			}
		}
		if(countheader>4){
			lenBuf = strlen(buff);
			if((22 < lenBuf) && (45 > lenBuf)) {
				strncpy(nav->sfts.rectyp[nod], buff, 20);
				trim(nav->sfts.rectyp[nod]);
				nav->sfts.bias[nod] = str2num(buff, 21, lenBuf - 21);
			}
			/*����������20����+���s�����ȉ�����1�����ڂ����s����(��s)�Ŗ����ꍇ*/
			else if((23 > lenBuf) && (buff[0] != '\n')) {
			//if((strlen(buff)<=21)&&(buff[0]!='\n')){
				//sprintf(nav->sfts.rectyp[nod],"%s",buff);
				strncpy(nav->sfts.rectyp[nod], buff, lenBuf - 1);
				nav->sfts.bias[nod] = PHASE_CYCLE;
			}else{
				showmsg("l2c file format error: %s",file);
				trace(1,"l2c file format error: %s\n",file);
				fclose(fp);
				return -1;
			}
			nod++;
			nav->sfts.n=nod;
			/*�f�[�^�����ő�l�ȏォ�ǂ���*/
			if(nod>=MAXRECTYP){
				showmsg("l2c file size error: %s",file);
				trace(1,"l2c file size error: %s\n",file);
				fclose(fp);
				return -1;
			}
		}
		countheader++;
	}
	fclose(fp);
	trace(4,"number of readL2C data: %d\n",nav->sfts.n);
	if(nav->sfts.n <1){
		showmsg("no l2c data error: %s\n",file);
		trace(1,"no l2c data error: %s\n",file);
	}
	return 0;
}

/* add err data --------------------------------------------------------------*/
static int addbipmdata(nav_t *nav, const bipm_t *data)
{
	bipm_t *bipm_data;

	if (nav->nbipmmax <= nav->nbipm) {
		if (nav->nbipmmax<=0) nav->nbipmmax=MAXANT; else nav->nbipmmax*=2;
		if (!(bipm_data=(bipm_t *)realloc(nav->bipm, sizeof(bipm_t)*nav->nbipmmax))) {
			trace(1,"addbipmdata: memalloc error n=%dx%d\n",sizeof(bipm_t),nav->nbipmmax);
			free(nav->bipm); nav->bipm=NULL; nav->nbipm=nav->nbipmmax=0;
            return -1;
		}
		nav->bipm=bipm_data;
	}
	nav->bipm[nav->nbipm++]=*data;
	return 1;
}


/* readcirt---------------------------------------------------
* GLONASS���n�ϊ��p�����[�^�̓Ǎ���
* args   : char *file       I   BIPM Circular T�t�@�C��
*          nav_t *nav       IO  �q�@�f�[�^
* return : int				�X�e�[�^�X (0:ok,0>:error,1:aborted)
*--------------------------------------------------------------*/
int readcirt(const char *file, nav_t *nav){
#if 0
	FILE *fp;
	struct tm *soltime;
	char monname[4], buff[1024],daynum[3],yearnum[5];
	time_t t;
	int i,countheader=0;

	trace(3,"readcirt: file=%s\n",file);

	if((fp=fopen(file,"r"))==NULL){
		showmsg("circular file open error: %s",file);
		trace(1,"circular file open error: %s\n",file);
		return -1;
	}

	/*���Ԃ���{���Ԃɕϊ��i���E���ԂɕύX�\��j*/
	t=nav->pclk->time.time;
	/*t=rtk->sol->time.time;*/
	soltime = localtime(&t);
	/*�j�����p��3�����̗��̂ɕϊ�*/
	monthchange(monname,soltime);
	/*���t�𕶎���ɕϊ��i�ꌅ�̏ꍇ�E�l�j*/
	if(soltime->tm_mday<10){
		strcpy(daynum," ");
		sprintf(daynum+1, "%d", soltime->tm_mday);
	}else{
		sprintf(daynum, "%d", soltime->tm_mday);
	}

	while(fgets(buff,sizeof(buff),fp)){
		if(countheader==0){
			/*�\�^�C�g���`�F�b�N*/
			if(((strncmp(buff+8,CIRCULARTHAEDER,70)))==0){
				/*�N�`�F�b�N�i�N�ׂ��̏ꍇ�����j*/
				if(buff[4]=='/'){
					sprintf(yearnum, "%d", soltime->tm_year+1900-2000);
					if(((strncmp(buff+5,yearnum,2)))==0){
						countheader=1;
					}
				}else{
					sprintf(yearnum, "%d", soltime->tm_year+1900);
					if(((strncmp(buff,yearnum,4)))==0){
						countheader=1;
					}
				}
			}
		}else if(countheader==1){
			/*�W�����\���̂Ɋi�[*/
			if((((strncmp(buff+7,monname,3)))==0)&&(((strncmp(buff+11,daynum,2)))==0)){
				i=0;
				while(buff[21+i]==' '){
					i++;
					if(i>8){
						showmsg("circular file format error: %s",file);
						trace(1,"circular file format error: %s\n",file);
						fclose(fp);
						return -1;
					}
				}
				if(buff[21+i]=='-'&&buff[21+i+1]==' '){
					nav->gps_glo[0]=0;
				}else{
					nav->gps_glo[0]=atof(buff+21+i);
				}
				i=0;
				while(buff[49+i]==' '){
					i++;
					if(i>8){
						showmsg("circular file format error: %s",file);
						trace(1,"circular file format error: %s\n",file);
						fclose(fp);
						return -1;
					}
				}
				if(buff[49+i]=='-'&&buff[49+i+1]==' '){
					nav->gps_glo[1]=0;
				}else{
					nav->gps_glo[1]=atof(buff+49+i);
				}
				fclose(fp);
				return 0;
			}
		}
	}
	showmsg("no circular data error: %s",file);
	trace(1,"no circular data error: %s\n",file);
	fclose(fp);
	return -1;
#endif

	FILE *fp;
	struct tm *soltime;
	char month[4], buff[1024],*ret;
	time_t t;
	int i,countheader=0;

	int year,year1,year2=0,j=0;
	bipm_t bipm;


	trace(3,"readcirt: file=%s\n",file);

	if((fp=fopen(file,"r"))==NULL){
		showmsg("circular file open error: %s",file);
		trace(1,"circular file open error: %s\n",file);
		return -1;
	}

	while(fgets(buff,sizeof(buff),fp)){

		if(strcmp(buff,"\n")==0) continue;
		ret = strstr(buff,"\n");
		i = ret-buff;
		if(i<22) continue;
		if(strncmp(buff,CIRCULAREND,71)==0) break;

		if(countheader==0){
			/*�\�^�C�g���`�F�b�N�i�`2011/1�j*/
			if(((strncmp(buff+8,CIRCULARTHAEDER1,70)))==0){
				year1=(int)str2num(buff,0,4);
				/*�N�`�F�b�N�i�N�ׂ��̏ꍇ�����j*/
				if(buff[4]=='/'){
					year2=year1+1;
				}
				countheader=1;
				year=year1;
			}
			/*�\�^�C�g���`�F�b�N�i2010/12�`2003/4�j*/
			if(((strncmp(buff+26,CIRCULARTHAEDER2,55)))==0){
				year1=(int)str2num(buff,18,4);
				/*�N�`�F�b�N�i�N�ׂ��̏ꍇ�����j*/
				if(buff[22]=='/'){
					year2=year1+1;
				}
				countheader=2;
				year=year1;
			}
		}else if(countheader==1){
			setstr(month,buff+7,3);
			bipm.month=monthint(month);
			if(bipm.month==0) continue;
			bipm.year=year;
			bipm.day=str2num(buff,11,2);
			bipm.c0=str2num(buff,21,9);
			bipm.c1=str2num(buff,49,9);

			if(nav->nbipm>0){
				/*������v���Ȃ��ꍇ*/
				if(bipm.month != nav->bipm[nav->nbipm-1].month)
				{
					/*1����*/
					if(bipm.month==1){
						year=year2;
						bipm.year=year;
					}
				}
			}

			addbipmdata(nav,&bipm);
		}else if(countheader==2){
			setstr(month,buff+22,3);
			bipm.month=monthint(month);
			if(bipm.month==0) continue;
			bipm.year=year;
			bipm.day=str2num(buff,26,2);
			bipm.c0=str2num(buff,39,12);
			bipm.c1=str2num(buff,57,18);

			if(nav->nbipm>0){
				/*������v���Ȃ��ꍇ*/
				if(bipm.month != nav->bipm[nav->nbipm-1].month)
				{
					/*1����*/
					if(bipm.month==1){
                        year=year2;
						bipm.year=year;
					}
				}
			}

			addbipmdata(nav,&bipm);
		}
	}
	fclose(fp);

	if(nav->nbipm<1){
		showmsg("no circular data error: %s",file);
		trace(1,"no circular data error: %s\n",file);
		return -1;
	}
	return 0;

}
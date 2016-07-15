/*------------------------------------------------------------------------------
* sphharmonic.c : �A���e�i�ʑ���������
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


/** ���K�����W�����h���{�֐�
 * @brief legendre
 * @param x �p
 * @param nmax
 * @param mmax
 * @param retP [OUT]Pnm
 */
extern void legendre(double x, int nmax, int mmax, double *retP)
{
    int pNMax = nmax + 1;
    double *Pnm = zeros(pNMax, mmax + 1);
    int i, j, k, n, m, idx;
    double *ncoef;

    /* [MATLAB] p(1:2,1)=[1;x]; */
    Pnm[0] = 1;
    Pnm[1] = x;

    /* [MATLAB] for n=2:nmax(1), p(n+1,1)=((2*n-1)*x*p(n,1)-(n-1)*p(n-1,1))/n; end */
    for (i = 1; i < nmax; i++) {
        n = i + 1;
        Pnm[i + 1] = ((2 * n - 1) * x * Pnm[i] - (n - 1) * Pnm[i - 1]) / n;
    }

    for (i = 0; i < mmax; i++) {
        /* [MATLAB] p(m+1,m+1)=(2*m-1)*sqrt(1-x*x)*p(m,m); */
        m = i + 1;
        Pnm[i + 1 + (i + 1) * pNMax] = (2 * m - 1) * sqrt(1 - x * x) * Pnm[i + i * pNMax];

        for (j = i + 1; j < nmax; j++) {
            /* [MATLAB] p(n+1,m+1)=((2*n-1)*x*p(n,m+1)-(n+m-1)*p(n-1,m+1))/(n-m); */
            n = j + 1;
            Pnm[j + 1 + (i + 1) * pNMax] = ((2 * n - 1) * x * Pnm[j + (i + 1) * pNMax] - (n + m - 1) * Pnm[j - 1 + (i + 1) * pNMax]) / (n - m);
        }
    }

    /* ���K���W�� */
    ncoef = zeros(nmax + 1, mmax + 1);
    for (i = 0; i <= nmax; i++) {
        for (j = 0; j <= mmax; j++) {
            ncoef[i + j * pNMax] = 1.0;
        }
    }

    for (i = 0; i < nmax; i++) {
        int minMax = (i < mmax)? i + 1: mmax;

        /* [MATLAB] c(n+1,1)=sqrt(2*n+1); */
        n = i + 1;
        ncoef[i + 1] = sqrt((double)(2 * n + 1));

        for (j = 0; j < minMax; j++) {
            double fac = 1.0;
            int m = j + 1;

            /* [MATLAB] c(n+1,m+1)=sqrt(2*(2*n+1)*factorial(n-m)/factorial(n+m)); */
            for (k = n + m; k > (n - m) ; k--) {
                fac *= k;
            }
            ncoef[i + 1 + (j + 1) * pNMax] = sqrt(2 * (2 * n + 1) / fac);
        }
    }

    /* [MATLAB] p=ncoef(nmax).*p; */
    /* �v�f���Ƃ̐ώZ */
    for (i = 0; i <= nmax; i++) {
        for (j = 0; j <= mmax; j++) {
            idx = i + j  * pNMax;
            retP[idx] = ncoef[idx] * Pnm[idx];
        }
    }

    /* �������J�� */
    free(Pnm);
    free(ncoef);
}

/** ���ʒ��a�֐��ߎ��W���v�Z
 * @brief sphharmonic
 * @param azel ���ʊp/�p[rad]
 * @param opt �����I�v�V����
 * @param sphCoef [OUT]sphCoef(A00 A10 A11 A20 A21 ... B11, B21 ...) NULL�̏ꍇ�F�W���̐����J�E���g���邾��
 * @return �W���̐�
 */
extern int sphharmonic(const double *azel, const prcopt_t *opt, double *sphCoef)
{
    double az = azel[0];
    double el = azel[1];
    int nMax = opt->antestnmax[0];
    int mMax = opt->antestnmax[1];
    int countOnly = (sphCoef == NULL)? 1: 0;
    int pNMax = nMax + 1;
    double *Pnm = mat(pNMax, mMax + 1);
    double cosm[ANTEST_NMAX+1], sinm[ANTEST_NMAX+1];
    int i, j, idx;

    /* ���ʊp�␳ */
    if (!countOnly) {
        /* ���K�����W�����h���{�֐� */
        double cos2Z = cos(PI - 2 * el);    /* cos(2z) z:�V���p[rad] */
        legendre(cos2Z, nMax, mMax, Pnm);

        /* ������ */
        for (i = 0; i <= nMax; i++) {
            cosm[i]=cos(i*az);
            sinm[i]=sin(i*az);
        }
    }


    // ���ʒ��a�W���iAnm,Bnm)���擾
    idx = 0;
    for (i = 0; i <= nMax; i++) {
        int minMax = (i < mMax)? i: mMax;

        /* An0���Z�o */
        if (countOnly) {
            idx++;
        } else {
            sphCoef[idx++] = Pnm[i];
        }

        for (j = 1 ; j <= minMax; j++) {
            /* Anm���Z�o */
            if (countOnly) {
                idx++;
            } else {
                sphCoef[idx++] = Pnm[i + j * pNMax] * cosm[j];
            }
        }
    }
    for (i = 0; i <= nMax; i++) {
        int minMax = (i < mMax)? i: mMax;
        for (j = 1; j <= minMax; j++) {
            // Bnm���Z�o
            if (countOnly) {
                idx++;
            } else {
                sphCoef[idx++] = Pnm[i + j * pNMax] * sinm[j];
            }
        }
    }

    // �������J��
    free(Pnm);
    return idx;
}


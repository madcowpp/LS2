/*
  This file is part of LS² - the Localization Simulation Engine of FU Berlin.

  Copyright 2011-2013  Heiko Will, Marcel Kyas, Thomas Hillebrandt,
  Stefan Adler, Malte Rohde, Jonathan Gunthermann

  LS² is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  LS² is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with LS².  If not, see <http://www.gnu.org/licenses/>.

 */

/********************************************************************
 **
 **  This file is made only for including in the LS² project
 **  and not desired for stand alone usage!
 **
 ********************************************************************/

/* @algorithm_name: LMS */

/*******************************************************************
 ***
 ***   LMS;
 *** 
 *******************************************************************/

#ifndef LMS_ALGORITHM_C_INCLUDED
#define LMS_ALGORITHM_C_INCLUDED 1

#if HAVE_CONFIG_H
#  include "ls2/ls2-config.h"
#endif

#if HAVE_POPT_H
#  include <popt.h>
#endif

#include <assert.h>
#include <gsl/gsl_multimin.h>
#include "algorithm/llsq_algorithm.c"
#include "util/util_math.c"
#include <stdlib.h>

/* Returns 1 if *A is greater than *B,
   0 if *A equals *B,
   -1 if *A is less than *B. */
static inline int
compare_ints (const void *a_, const void *b_) 
{
  const int *a = a_;
  const int *b = b_;

  return *a < *b ? -1 : *a > *b;
}

static inline int
compare_floats (const void *a_, const void *b_) 
{
  const float *a = a_;
  const float *b = b_;

  return *a < *b ? -1 : *a > *b;
}

static inline void
__attribute__((__always_inline__,__gnu_inline__,__artificial__,__nonnull__))
lms_run(const VECTOR* vx, const VECTOR* vy, const VECTOR *restrict r,
              size_t no_anchors, int width, int height,
              VECTOR *restrict resx, VECTOR *restrict resy)
{
    // 1. Set number of subsets, subset size and threshold
    int k = 4;
    int N = (int)no_anchors;
    int M = N > 6 ? 20 : binom(N, k);
    float threshold = 2.5f;

    if (N < k) {
        llsq_run(vx, vy, r, no_anchors, width, height, resx, resy);
        return;
    }

    for (int ii = 0; ii < VECTOR_OPS; ii++) {
        // 2. Randomly draw M k-permutations
        int rpi = 0;
        int ran[M];
        int bino = binom(N, k);
        int permutations[k];
        int randPermutations[M][k];

        if (bino <= M) {
            // select all available permutations
            for (int i = 0; i < M; i++) {
                ran[i] = i;
            }
        } else {
            // select M permutations randomly
            for (int i = 0; i < M; i++) {
                ran[i] = -1;
                do {
                    int j;
		    double randZeroOne = ((double) rand() / (double)RAND_MAX);
                    int rn = (int) (randZeroOne * (bino - 1));
                    for (j = 0; j < i; j++) {
                        if (ran[j] == rn) break;
                    }
                    if (j == i) {
                        ran[i] = rn;
                    }
                } while (ran[i] == -1);
            }
            qsort(ran, (size_t) M, sizeof(int), compare_ints);
        }

        // initialisation for calculating k-permutations
        for (int i = 0; i < k; i++) {
            permutations[i] = i;
        }

        // build all k-permutations
        for (int i = 0; i < bino; i++) {

            // add current permutation if choosen
            if (i == ran[rpi]) {
                memcpy(randPermutations[rpi], permutations, (size_t) k*sizeof(int));
                rpi++;
                if (rpi == M) break;
            }

            // build next permutation
            if (i == bino - 1) break;
            int j = k - 1;
            while (j >= 0) {
                if (!incCounter(permutations, j, N, k)) break;
                j--;
            }
            for (int l = j+1; l < k; l++) {
                permutations[l] = permutations[l-1] + 1;
            }
        }
               
        // calculate intermediate position and median of residues
        float iPos_x[M];
        float iPos_y[M];
        float medians[M];
        VECTOR tmpAnchors_x[k];
        VECTOR tmpAnchors_y[k];
        VECTOR tmpRanges[k];
        float tmpMedian[N];
                                
        for (int j = 0; j < M; j++) {
	    for (int i = 0; i < k; i++) {               
		tmpAnchors_x[i][ii] = vx[randPermutations[j][i]][ii];
                tmpAnchors_y[i][ii] = vy[randPermutations[j][i]][ii];
                tmpRanges[i][ii] = r[randPermutations[j][i]][ii];
            }
            VECTOR pex, pey;
            llsq_run(tmpAnchors_x,tmpAnchors_y,tmpRanges,(size_t)k,width,height,&pex,&pey);
            
            iPos_x[j] = pex[ii];
            iPos_y[j] = pey[ii];
            // calculate residue for all points and find median
            for (int i = 0; i < N; i++) {
                float residue = distance_s(iPos_x[j],iPos_y[j],vx[i][ii],vy[i][ii]) - r[i][ii];
                tmpMedian[i] = residue * residue;
            }
            qsort(tmpMedian, (size_t)N, sizeof(float), compare_floats);
            medians[j] = tmpMedian[N/2];
        }

        // 3. Find index of least median
        int m = 0;
        for (int i = 1; i < M; i++) {
            if (medians[i] < medians[m]) {
                m = i;
            }
        }
        
        // 4. Calculate s0
        float s0 = 1.4826f * ((float)(1 + 5) / (float)(N - 2)) * sqrtf(medians[m]);

        // 5. Assign weights to samples
        float wei[N];
        int count = 0;
        for (int i = 0; i < N; i++) {
            float ri = distance_s((float)iPos_x[m],(float)iPos_y[m],vx[i][ii],vy[i][ii]) - r[i][ii];
            if (fabsf(ri/s0) <= threshold) {
                wei[i] = 1;
                count++;
            } else {
                wei[i] = 0;
            }
        }
        
        
        VECTOR anchors_x[count],anchors_y[count],ranges[count];
        int c=0;
        for (int i=0; i < N; i++) {
            if (wei[i]==1) {
                anchors_x[c][ii] = vx[i][ii];
                anchors_y[c][ii] = vy[i][ii];
                ranges[c][ii] = r[i][ii];
                c++;
            }
        }
        
        // 6. Calculate weighted LS and return result as final position
        VECTOR pex, pey;
        llsq_run(anchors_x,anchors_y,ranges,(size_t)count,width,height,&pex,&pey);
        
        (*resx)[ii] = pex[ii];
        (*resy)[ii] = pey[ii];
    }


}

#endif

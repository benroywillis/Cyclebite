//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <x86intrin.h>
#include <omp.h>

#define PRECISION 	double
#define SIZE 		1024
#define BLOCKSIZE 	32
#define UNROLL 		(4)

// taken from patterson-hennessy "Computer Organization and Design, RISC-V Edition" chapter 5 section 15 "Going Fast: Exploiting Memory Hierarchy", Figure 5.47 pg 466

void do_block( int n, int si, int sj, int sk, PRECISION* A, PRECISION* B, PRECISION* C )
{
	static __m256d c[UNROLL];
	for( int i = si; i < si + BLOCKSIZE; i += UNROLL*4 )
	{
		for( int j = sj; j < sj + BLOCKSIZE; j++ )
		{
			for( int x = 0; x < UNROLL; x++ )
			{
				*(c + x) = _mm256_load_pd( C + i + x*UNROLL + j*n );
			}
			for( int k = sk; k < sk + BLOCKSIZE; k++ )
			{
				__m256d b = _mm256_broadcast_sd( B + k + j * n );
				for( int x = 0; x < UNROLL; x++ )
				{
					c[x] = _mm256_add_pd( c[x], _mm256_mul_pd( _mm256_load_pd( A + n*k + x*UNROLL + i ), b ) );
				}
			}
			for( int x = 0; x < UNROLL; x++ )
			{
				_mm256_store_pd( C + i + x*UNROLL + j*n, c[x] );
			}
		}
	}
}

int main()
{
	// if we malloc our arrays the _mm256_load_pd() operations segfault
	PRECISION* A = (PRECISION* )aligned_alloc( 32, SIZE*SIZE*sizeof(PRECISION) );
	PRECISION* B = (PRECISION* )aligned_alloc( 32, SIZE*SIZE*sizeof(PRECISION) );
	PRECISION* C = (PRECISION* )aligned_alloc( 32, SIZE*SIZE*sizeof(PRECISION) );
	//PRECISION A[SIZE][SIZE];
	//PRECISION B[SIZE][SIZE];
	//PRECISION C[SIZE][SIZE];
	struct timespec start, end;
	while( clock_gettime(CLOCK_MONOTONIC, &start) ) {}
	#pragma omp parallel for
	for( int i = 0; i < SIZE; i += BLOCKSIZE )
	{
		for( int j = 0; j < SIZE; j += BLOCKSIZE )
		{
			for( int k = 0; k < SIZE; k += BLOCKSIZE )
			{
				do_block( SIZE, i, j, k, (PRECISION*)A, (PRECISION*)B, (PRECISION*)C );
			}
		}
	}
	while( clock_gettime(CLOCK_MONOTONIC, &end) ) {}
	double time_s  = (double)end.tv_sec - (double)start.tv_sec;
	double time_ns = ((double)end.tv_nsec - (double)start.tv_nsec) * pow( 10.0, -9.0 );
	double total   = time_s + time_ns;
    printf("Time: %fs\n", total);

	return 0;
}

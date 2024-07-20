//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Markov.h"
#include <stdio.h>
#include <stdlib.h>

#ifndef TRACING
#define TRACING 0
#endif

#define SIZE 512

int main()
{
    int(*in0)[SIZE] = malloc(SIZE * sizeof(int[SIZE][SIZE]));
    int(*in1)[SIZE] = malloc(SIZE * sizeof(int[SIZE][SIZE]));
    int(*out)[SIZE] = malloc(SIZE * sizeof(int[SIZE][SIZE]));

    for (int i = 0; i < SIZE; i++)
    {
        for (int j = 0; j < SIZE; j++)
        {
            in0[i][j] = rand();
            in1[i][j] = rand();
            out[i][j] = 0;
        }
    }

#if TRACING
    CyclebiteMarkovKernelEnter("MatrixMultiply,Outer");
#endif
    for (int i = 0; i < SIZE; i++)
    {
#if TRACING
        CyclebiteMarkovKernelEnter("MatrixMultiply,Inner");
#endif
        for (int j = 0; j < SIZE; j++)
        {
#if TRACING
            CyclebiteMarkovKernelEnter("MatrixMultiply,Mul");
#endif
            for (int k = 0; k < SIZE; k++)
            {
                out[i][j] += in0[i][k] * in1[k][j];
            }
#if TRACING
            CyclebiteMarkovKernelExit("MatrixMultiply,Mul");
#endif
        }
#if TRACING
        CyclebiteMarkovKernelExit("MatrixMultiply,Inner");
#endif
    }
#if TRACING
    CyclebiteMarkovKernelExit("MatrixMultiply,Outer");
#endif

    printf("Success.\n");
    return 0;
}

//Label for variable names start
//in 4000
//Label for variable names end

//Label for loops start
//44 45 46 47 48 49 50 51 52 53 54 55
//Label for loops end

#include "Markov.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int *get_input(int n)
{
    int *in = (int *)malloc(sizeof(int) * n);
    for (int i = 0; i < n; i++)
    {
        *(in + i) = rand();
    }
    return in;
}

int main(int argc, char *argv[])
{
    int SIZE;
    if (argc > 1)
    {
        SIZE = atoi(argv[1]);
    }
    else
    {
        SIZE = 1000;
    }
    printf("\nSIZE = %d", SIZE);
    TraceAtlasMarkovKernelEnter("randInit");
    int *in = get_input(SIZE);
    TraceAtlasMarkovKernelExit("randInit");

    // bubble sort
    int swap;
    TraceAtlasMarkovKernelEnter("Bubblesort");
    for (int i = 0; i < 512; i++)
    {
        for (int j = i; j < 512; j++)
        {
            if (in[i] > in[j])
            {
                swap = in[i];
                in[i] = in[j];
                in[j] = swap;
            }
        }
    }
    TraceAtlasMarkovKernelExit("Bubblesort");
    printf("\nSorting Done");
    free(in);
    return 0;
}

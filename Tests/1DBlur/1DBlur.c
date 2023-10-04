//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
//Label for variable names start
//input 4096
//output 4088
//Label for variable names end

//Label for loops start
//25 26 27 28
//30 31 32 33
//Label for loops end

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
const int WIDTH = 1024;

int main()
{
    int *input = (int *)malloc(sizeof(int) * WIDTH);
    int *output = (int *)malloc(sizeof(int) * (WIDTH - 2));

    srand(time(NULL));
    //initialize the data
    for (int i = 0; i < WIDTH; i++)
    {
        input[i] = rand();
    }

    for (int i = 1; i < WIDTH - 1; i++)
    {
        output[i - 1] = input[i - 1] + input[i] + input[i + 1];
    }

    printf("Success\n");
    return 0;
}

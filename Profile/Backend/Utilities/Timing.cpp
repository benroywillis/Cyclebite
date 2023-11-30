//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include <cmath>
#include <cstdio>
#include <ctime>

// start and end points to specifically time the profiles
struct timespec __TA_Timing_start;
struct timespec __TA_Timing_end;

extern "C"
{
    void TimingInit()
    {
        while (clock_gettime(CLOCK_MONOTONIC, &__TA_Timing_start))
        {
        }
    }
    void TimingDestroy()
    {
        // stop the timer and print
        while (clock_gettime(CLOCK_MONOTONIC, &__TA_Timing_end))
        {
        }
        double time_s = (double)__TA_Timing_end.tv_sec - (double)__TA_Timing_start.tv_sec;
        double time_ns = ((double)__TA_Timing_end.tv_nsec - (double)__TA_Timing_start.tv_nsec) * pow(10.0, -9.0);
        auto totalTime = time_s + time_ns;
        printf("\nNATIVETIME: %f\n", totalTime);
    }
}
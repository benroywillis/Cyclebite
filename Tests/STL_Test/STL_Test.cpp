// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include <string>
#include <vector>

#ifndef PRECISION
#define PRECISION int
#endif

using namespace std;

// change size, precision
// check: is an int32 and float32 and a struct {char,char,char,char} the same thing in the vector?
// use the other inserters. ie construct a 1000 member vector and populate indices directly
// std::sort(vector.begin(), vector.end(), std::less); or std::inserter(vector, vector.begin());
// what happens when we up the optimizer (-Oflag -flto)

int main(int argc, char **argv)
{
    // length of the vector
    int LENGTH = std::stoi(argv[1]);
    vector<PRECISION> vec0;
    vector<PRECISION> vec1;
    vector<PRECISION> vec2;
    for (int i = 0; i < LENGTH; i++)
    {
        vec0.push_back((PRECISION)i);
    }

    int i = 0;
    while (i < LENGTH)
    {
        vec1.push_back((PRECISION)(i + rand() % 4));
        i += rand() % 4;
    }

    for (auto i : vec0)
    {
        for (auto j : vec1)
        {
            auto a = vec0.at(i);
            vec2.push_back((PRECISION)(a * j));
        }
    }
    return 0;
}
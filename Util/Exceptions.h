//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <exception>
#include <sstream>
#include <string>

class CyclebiteException : public std::exception
{
protected:
    std::string msg;

public:
    CyclebiteException(const std::string &arg, const char *file, int line)
    {
        std::ostringstream o;
        o << file << ":" << line << ": " << arg;
        msg = o.str();
    }
    using std::exception::what;
    const char *what()
    {
        return msg.c_str();
    }
};

#define CyclebiteException(arg) CyclebiteException(arg, __FILE__, __LINE__);
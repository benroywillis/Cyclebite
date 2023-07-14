#include "Trace.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

FILE *CyclebiteTraceFile;

//trace functions
z_stream CyclebiteStrm;

int CyclebiteTraceCompressionLevel;
char *CyclebiteTraceFilename;
/// <summary>
/// The maximum ammount of bytes to store in a buffer before flushing it.
/// </summary>
// 2^7 * 2^10
#define TRACEATLASBUFSIZE 131072
unsigned int CyclebiteBufferIndex = 0;
uint8_t CyclebiteTempBuffer[TRACEATLASBUFSIZE];
uint8_t CyclebiteStoreBuffer[TRACEATLASBUFSIZE];

bool CyclebiteOpened = false;
bool CyclebiteClosed = false;

bool CyclebiteZlibInit = false;

void CyclebiteWriteStream(char *input)
{
    size_t size = strlen(input);
    if (CyclebiteBufferIndex + size >= TRACEATLASBUFSIZE)
    {
        CyclebiteBufferData();
    }
    memcpy(CyclebiteStoreBuffer + CyclebiteBufferIndex, input, size);
    CyclebiteBufferIndex += size;
}

///Modified from https://stackoverflow.com/questions/4538586/how-to-compress-a-buffer-with-zlib
void CyclebiteBufferData()
{
    if (CyclebiteTraceCompressionLevel != -2)
    {
        CyclebiteStrm.next_in = CyclebiteStoreBuffer;
        CyclebiteStrm.avail_in = CyclebiteBufferIndex;
        while (CyclebiteStrm.avail_in != 0)
        {
            int defResult = deflate(&CyclebiteStrm, Z_PARTIAL_FLUSH);
            if (defResult != Z_OK)
            {
                fprintf(stderr, "Zlib compression error");
                exit(-1);
            }
            if (CyclebiteStrm.avail_out == 0)
            {
                fwrite(CyclebiteTempBuffer, sizeof(Bytef), TRACEATLASBUFSIZE, CyclebiteTraceFile);
                CyclebiteStrm.next_out = CyclebiteTempBuffer;
                CyclebiteStrm.avail_out = TRACEATLASBUFSIZE;
            }
        }
    }
    else
    {

        fwrite(CyclebiteStoreBuffer, sizeof(char), CyclebiteBufferIndex, CyclebiteTraceFile);
    }
    CyclebiteBufferIndex = 0;
}

void CyclebiteWrite(char *inst, int line, int block, uint64_t func)
{
    char suffix[128];
#if defined _WIN32
    sprintf(suffix, ";line:%d;block:%d;function:%llu\n", line, block, func);
#else
    sprintf(suffix, ";line:%d;block:%d;function:%lu\n", line, block, func);
#endif
    size_t size = strlen(inst) + strlen(suffix);
    char fin[size];
    strcpy(fin, inst);
    strncat(fin, suffix, 128);
    CyclebiteWriteStream(fin);
}

void CyclebiteWriteAddress(char *inst, int line, int block, uint64_t func, char *address)
{
    char suffix[128];
#if defined _WIN32
    sprintf(suffix, ";line:%d;block:%d;function:%llu;address:%llu\n", line, block, func, (uint64_t)address);
#else
    sprintf(suffix, ";line:%d;block:%d;function:%lu;address:%lu\n", line, block, func, (uint64_t)address);
#endif
    size_t size = strlen(inst) + strlen(suffix);
    char fin[size];

    strcpy(fin, inst);
    strncat(fin, suffix, 128);
    CyclebiteWriteStream(fin);
}

void CyclebiteOpenFile()
{
    if (!CyclebiteOpened)
    {
        char *tcl = getenv("TRACE_COMPRESSION");
        if (tcl != NULL)
        {
            int l = atoi(tcl);
            CyclebiteTraceCompressionLevel = l;
        }
        else
        {
            CyclebiteTraceCompressionLevel = Z_DEFAULT_COMPRESSION;
        }
        if (CyclebiteTraceCompressionLevel != -2)
        {
            CyclebiteStrm.zalloc = Z_NULL;
            CyclebiteStrm.zfree = Z_NULL;
            CyclebiteStrm.opaque = Z_NULL;
            CyclebiteStrm.next_out = CyclebiteTempBuffer;
            CyclebiteStrm.avail_out = TRACEATLASBUFSIZE;
            CyclebiteZlibInit = true;
            int defResult = deflateInit(&CyclebiteStrm, CyclebiteTraceCompressionLevel);
            if (defResult != Z_OK)
            {
                fprintf(stderr, "Zlib compression error");
                exit(-1);
            }
        }
        char *tfn = getenv("TRACE_NAME");
        if (tfn != NULL)
        {
            CyclebiteTraceFilename = tfn;
        }
        else
        {
            if (CyclebiteTraceCompressionLevel != -2)
            {
                CyclebiteTraceFilename = "raw.trc";
            }
            else
            {
                CyclebiteTraceFilename = "raw.trace";
            }
        }

        CyclebiteTraceFile = fopen(CyclebiteTraceFilename, "w");
        CyclebiteWriteStream("TraceVersion:3\n");
        CyclebiteOpened = true;
    }
}

void CyclebiteCloseFile()
{
    if (!CyclebiteClosed)
    {
        if (CyclebiteTraceCompressionLevel != -2)
        {
            CyclebiteStrm.next_in = CyclebiteStoreBuffer;
            CyclebiteStrm.avail_in = CyclebiteBufferIndex;
            while (true)
            {
                int defRes = deflate(&CyclebiteStrm, Z_FINISH);
                if (defRes == Z_BUF_ERROR)
                {
                    fprintf(stderr, "Zlib buffer error");
                    exit(-1);
                }
                else if (defRes == Z_STREAM_ERROR)
                {
                    fprintf(stderr, "Zlib stream error");
                    exit(-1);
                }
                if (CyclebiteStrm.avail_out == 0)
                {
                    fwrite(CyclebiteTempBuffer, sizeof(Bytef), TRACEATLASBUFSIZE, CyclebiteTraceFile);
                    CyclebiteStrm.next_out = CyclebiteTempBuffer;
                    CyclebiteStrm.avail_out = TRACEATLASBUFSIZE;
                }
                if (defRes == Z_STREAM_END)
                {
                    break;
                }
            }
            fwrite(CyclebiteTempBuffer, sizeof(Bytef), TRACEATLASBUFSIZE - CyclebiteStrm.avail_out, CyclebiteTraceFile);

            deflateEnd(&CyclebiteStrm);
        }
        else
        {
            fwrite(CyclebiteStoreBuffer, sizeof(Bytef), CyclebiteBufferIndex, CyclebiteTraceFile);
        }

        CyclebiteClosed = true;
        //fclose(myfile); //breaks occasionally for some reason. Likely a glibc error.
    }
}

void CyclebiteLoadDump(void *address)
{
    char fin[128];
    sprintf(fin, "LoadAddress:%#lX\n", (uint64_t)address);
    CyclebiteWriteStream(fin);
}
void CyclebiteDumpLoadValue(void *MemValue, int size)
{
    char fin[128];
    uint8_t *bitwisePrint = (uint8_t *)MemValue;
    sprintf(fin, "LoadValue:");
    CyclebiteWriteStream(fin);
    for (int i = 0; i < size; i++)
    {
        if (i == 0)
        {
            sprintf(fin, "0X%02X", bitwisePrint[i]);
        }
        else
        {
            sprintf(fin, "%02X", bitwisePrint[i]);
        }
        CyclebiteWriteStream(fin);
    }
    sprintf(fin, "\n");
    CyclebiteWriteStream(fin);
}
void CyclebiteStoreDump(void *address)
{
    char fin[128];
    sprintf(fin, "StoreAddress:%#lX\n", (uint64_t)address);
    CyclebiteWriteStream(fin);
}

void CyclebiteDumpStoreValue(void *MemValue, int size)
{
    char fin[128];
    uint8_t *bitwisePrint = (uint8_t *)MemValue;
    sprintf(fin, "StoreValue:");
    CyclebiteWriteStream(fin);
    for (int i = 0; i < size; i++)
    {
        if (i == 0)
        {
            sprintf(fin, "0X%02X", bitwisePrint[i]);
        }
        else
        {
            sprintf(fin, "%02X", bitwisePrint[i]);
        }
        CyclebiteWriteStream(fin);
    }
    sprintf(fin, "\n");
    CyclebiteWriteStream(fin);
}

void CyclebiteBB_ID_Dump(uint64_t block, bool enter)
{
    char fin[128];
    if (enter)
    {
        sprintf(fin, "BBEnter:%#lX\n", block);
    }
    else
    {
        sprintf(fin, "BBExit:%#lX\n", block);
    }
    CyclebiteWriteStream(fin);
}

void CyclebiteKernelEnter(char *label)
{
    if (CyclebiteZlibInit)
    {
        char fin[128];
        strcpy(fin, "KernelEnter:");
        strcat(fin, label);
        strcat(fin, "\n");
        CyclebiteWriteStream(fin);
    }
}
void CyclebiteKernelExit(char *label)
{
    if (CyclebiteZlibInit)
    {
        char fin[128];
        strcpy(fin, "KernelExit:");
        strcat(fin, label);
        strcat(fin, "\n");
        CyclebiteWriteStream(fin);
    }
}

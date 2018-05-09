//===--------------------------------------------------------------------------------------------===
// orbit/csupport/diag.h - Orbit's compiler diagnostic API
// This source is part of Orbit - Compiler Support
//
// Created on 2018-05-08 by Amy Parent <amy@amyparent.com>
// Copyright (c) 2016-2018 Amy Parent <amy@amyparent.com>
// Licensed under the MIT License
// =^•.•^=
//===--------------------------------------------------------------------------------------------===
#ifndef orbit_csupport_diag_h
#define orbit_csupport_diag_h
#include <stdint.h>
#include <orbit/csupport/source.h>
#include <orbit/csupport/string.h>

#define ORBIT_DIAG_MAXCOUNT   64

typedef enum _OrbitDiagLevel        OrbitDiagLevel;
typedef struct _OrbitDiagManager    OrbitDiagManager;
typedef struct _OrbitDiag           OrbitDiag;
typedef struct _OrbitDiagParam      OrbitDiagParam;

//typedef uint32_t                    OrbitDiagID;

typedef struct {
    OrbitDiagManager* manager;
    uint32_t id;
} OrbitDiagID;

typedef void (*OrbitDiagConsumer)(OCSource* source, OrbitDiag*);

enum _OrbitDiagLevel {
    ORBIT_DIAGLEVEL_INFO    = 0,
    ORBIT_DIAGLEVEL_WARN    = 1,
    ORBIT_DIAGLEVEL_ERROR   = 2,
};

struct _OrbitDiagParam {
    enum { ORBIT_DPK_INT, ORBIT_DPK_STRING, ORBIT_DPK_CSTRING } kind;
    union {
        int         intValue;
        const char* cstringValue;
        OCStringID  stringValue;
    };
};

struct _OrbitDiag {
    OrbitDiagLevel  level;
    OCSourceLoc     sourceLoc;
    
    const char*     format;

    uint32_t        paramCount;
    OrbitDiagParam  params[10];
};

struct _OrbitDiagManager {
    OCSource*           source;
    OrbitDiagConsumer   consumer;
    uint32_t            diagnosticCount;
    OrbitDiag           diagnostics[ORBIT_DIAG_MAXCOUNT];
};

#define ORBIT_DIAG_INT(val)     ((OrbitDiagParam){.kind=ORBIT_DPK_INT, .intValue=(val)})
#define ORBIT_DIAG_FLOAT(val)   ((OrbitDiagParam){.kind=ORBIT_DPK_FLOAT, .floatValue=(val)})
#define ORBIT_DIAG_STRING(val)  ((OrbitDiagParam){.kind=ORBIT_DPK_STRING, .stringValue=(val)})
#define ORBIT_DIAG_CSTRING(val)  ((OrbitDiagParam){.kind=ORBIT_DPK_CSTRING, .cstringValue=(val)})

extern OrbitDiagManager orbit_defaultDiagManager;

void orbit_diagManagerInit(OrbitDiagManager* manager, OCSource* source);

OrbitDiagID orbit_diagNew(OrbitDiagManager* manager, OrbitDiagLevel level, const char* format);
void orbit_diagAddParam(OrbitDiagID id, OrbitDiagParam param);
void orbit_diagAddSourceLoc(OrbitDiagID id, OCSourceLoc loc);

void orbit_diagEmitAll(OrbitDiagManager* manager);
void orbit_diagEmitAbove(OrbitDiagManager* manager, OrbitDiagLevel level);

char* orbit_diagGetFormat(OrbitDiag* diag);


#endif /* orbit_csupport_diag_h */

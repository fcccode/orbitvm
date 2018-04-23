//===--------------------------------------------------------------------------------------------===
// orbit/console/console.h
// This source is part of Orbit - Console Support and Utilities
//
// Created on 2017-09-28 by Amy Parent <amy@amyparent.com>
// Copyright (c) 2016-2018 Amy Parent <amy@amyparent.com>
// Available under the MIT License
// =^•.•^=
//===--------------------------------------------------------------------------------------------===
#ifndef orbit_ast_console_h
#define orbit_ast_console_h

#include <stdio.h>
#include <stdint.h>
#include <orbit/utils/string.h>
#include <orbit/source/source.h>
#include <orbit/source/tokens.h>

typedef enum {
    CLI_RESET,
    CLI_BOLD,
    CLI_RED,
    CLI_GREEN,
    CLI_YELLOW,
    CLI_BLUE,
    CLI_MAGENTA,
    CLI_CYAN,
    CLI_BADCOLOR,
} CLIColor;

void console_setColor(FILE* out, CLIColor color);

void console_printToken(FILE* out, OCToken token);
void console_printPooledString(FILE* out, OCStringID id);
void console_printTokenLine(FILE* out, OCToken token);
void console_printUnderlines(FILE* out, OCToken tok, CLIColor color);

#endif /* orbit_ast_console_h */
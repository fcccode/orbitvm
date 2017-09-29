//
//  orbit/console/console.h
//  Orbit - Console Support and Utilities
//
//  Created by Amy Parent on 2017-09-28.
//  Copyright © 2017 Amy Parent. All rights reserved.
//
#ifndef orbit_ast_console_h
#define orbit_ast_console_h

#include <stdio.h>
#include <stdint.h>

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
void console_printUnderlines(FILE* out, uint64_t start, uint64_t length);

#endif /* orbit_ast_console_h */
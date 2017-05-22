//
//  orbit_parser.c
//  OrbitVM
//
//  Created by Amy Parent on 2017-05-21.
//  Copyright © 2017 Amy Parent. All rights reserved.
//
#include <stdarg.h>
#include <orbit/orbit_utils.h>
#include "orbit_parser.h"
#include "orbit_tokens.h"
#include "orbit_lexer.h"
#include "compiler_utils.h"

typedef struct {
    OCLexer     lexer;
    bool        recovering;
    OrbitVM*    vm;
} OCParser;

// MARK: - Basic RD parser utilities



static inline bool haveBinaryOp(OCParser* parser) {
    return orbit_isBinaryOp(parser->lexer.currentToken.type);
}

static inline bool haveUnaryOp(OCParser* parser) {
    return orbit_isUnaryOp(parser->lexer.currentToken.type);
}

static inline int precedence(OCParser* parser) {
    return orbit_binaryPrecedence(parser->lexer.currentToken.type);
}

static inline bool rightAssoc(OCParser* parser) {
    return orbit_binaryRightAssoc(parser->lexer.currentToken.type);
}

static inline OCToken current(OCParser* parser) {
    return parser->lexer.currentToken;
}

static void compilerError(OCParser* parser, const char* fmt, ...) {
    OASSERT(parser != NULL, "Null instance error");
    if(parser->recovering) { return; }
    parser->recovering = true;
    OASSERT(parser != NULL, "Null instance error");
    
    fprintf(stderr, "%s:%llu:%llu: error: ",
                     parser->lexer.path,
                     parser->lexer.currentToken.line,
                     parser->lexer.currentToken.column);
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    fputc('\n', stderr);
    lexer_printLine(stderr, &parser->lexer);
    orbit_printSquigglies(stderr, parser->lexer.currentToken.column, parser->lexer.currentToken.length);
}

static void syntaxError(OCParser* parser, OCTokenType type) {
    OASSERT(parser != NULL, "Null instance error");
    if(parser->recovering) { return; }
    parser->recovering = true;
    
    OCToken tok  = current(parser);
    fprintf(stderr, "%s:%llu:%llu: error: expected '%s' (found '%s')\n",
            parser->lexer.path,
            tok.line, tok.column,
            orbit_tokenString(type),
            orbit_tokenString(parser->lexer.currentToken.type));
    lexer_printLine(stderr, &parser->lexer);
    orbit_printSquigglies(stderr, tok.column, tok.length);
}

// MARK: - RD Basics

static inline bool have(OCParser* parser, OCTokenType type) {
    return parser->lexer.currentToken.type == type;
}

static inline bool match(OCParser* parser, OCTokenType type) {
    if(have(parser, type)) {
        lexer_nextToken(&parser->lexer);
        return true;
    }
    return false;
}

static bool expect(OCParser* parser, OCTokenType type) {
    if(match(parser, type)) { return true; }
    syntaxError(parser, type);
    return false;
}


// The Big List: all of our recognisers. Easier to forward-declare to avoid
// shuffling everything every time we had a recogniser.

//static void recDecl(OCParser*);
static void recBlock(OCParser*);
static void recTypeDecl(OCParser*);
static void recVarDecl(OCParser*);
static void recFuncDecl(OCParser*);
static void recParameters(OCParser*);

static void recStatement(OCParser*);
static void recConditional(OCParser*);
static void recIfStatement(OCParser*);
static void recWhileLoop(OCParser*);
static void recForLoop(OCParser*);

static void recExpression(OCParser*, int);
static void recTerm(OCParser*);
static void recName(OCParser*);
//static void recSuffix(OCParser*);
static void recSubscript(OCParser*);
static void recFieldAccess(OCParser*);
static void recFuncCall(OCParser*);
static void recExprList(OCParser*);

static void recType(OCParser*);
static void recTypename(OCParser*);
static void recPrimitive(OCParser*);
static void recArrayType(OCParser*);
static void recMapType(OCParser*);

//static void recNumberLiteral(OCParser*);
//static void recStringLiteral(OCParser*);

// MARK: - Implementation

static void recProgram(OCParser* parser) {
    for(;;) {
        if(have(parser, TOKEN_VAR))
            recVarDecl(parser);
        else if(have(parser, TOKEN_FUN))
            recFuncDecl(parser);
        else if(have(parser, TOKEN_TYPE))
            recTypeDecl(parser);
        else
            break;
    }
    expect(parser, TOKEN_EOF);
}

static void recBlock(OCParser* parser) {
    expect(parser, TOKEN_LBRACE);
    for(;;) {
        if(have(parser, TOKEN_VAR))
            recVarDecl(parser);
        else if(haveUnaryOp(parser)
                || have(parser, TOKEN_LPAREN)
                || have(parser, TOKEN_IDENTIFIER)
                || have(parser, TOKEN_STRING_LITERAL)
                || have(parser, TOKEN_INTEGER_LITERAL)
                || have(parser, TOKEN_FLOAT_LITERAL)
                || have(parser, TOKEN_IF)
                || have(parser, TOKEN_WHILE)
                || have(parser, TOKEN_FOR))
            recStatement(parser);
        else
            break;
    }
    expect(parser, TOKEN_RBRACE);
}

static void recTypeDecl(OCParser* parser) {
    expect(parser, TOKEN_TYPE);
    expect(parser, TOKEN_IDENTIFIER);
    expect(parser, TOKEN_LBRACE);
    
    do {
        recVarDecl(parser);
    } while(have(parser, TOKEN_VAR));
    
    expect(parser, TOKEN_RBRACE);
}

// 'var' identifier ((':', type) | ((':', type)? '=' expression))
static void recVarDecl(OCParser* parser) {
    expect(parser, TOKEN_VAR);
    expect(parser, TOKEN_IDENTIFIER);
    
    if(match(parser, TOKEN_COLON)) {
        recType(parser);
    }
    
    if(match(parser, TOKEN_EQUALS)) {
        recExpression(parser, 0);
    }
}

// func-proto '{' block '}'
// 'func' identifier parameters '->' type
static void recFuncDecl(OCParser* parser) {
    expect(parser, TOKEN_FUN);
    expect(parser, TOKEN_IDENTIFIER);
    expect(parser, TOKEN_LPAREN);
    
    if(have(parser, TOKEN_IDENTIFIER)) {
        recParameters(parser);
    }
    
    expect(parser, TOKEN_RPAREN);
    expect(parser, TOKEN_ARROW);
    
    recType(parser);
    
    recBlock(parser);
}


static void recParameters(OCParser* parser) {
    do {
        expect(parser, TOKEN_IDENTIFIER);
        expect(parser, TOKEN_COLON);
        recType(parser);
    } while(match(parser, TOKEN_COMMA));
}

static void recStatement(OCParser* parser) {
    if(have(parser, TOKEN_IF) || have(parser, TOKEN_WHILE) || have(parser, TOKEN_FOR)) {
        recConditional(parser);
    }
    else if(haveUnaryOp(parser)
            || have(parser, TOKEN_LPAREN)
            || have(parser, TOKEN_IDENTIFIER)
            || have(parser, TOKEN_STRING_LITERAL)
            || have(parser, TOKEN_INTEGER_LITERAL)
            || have(parser, TOKEN_FLOAT_LITERAL)) {
        recExpression(parser, 0);
    }
    else {
        compilerError(parser, "expected a statement");
    }
    match(parser, TOKEN_NEWLINE);
}

static void recConditional(OCParser* parser) {
    if(have(parser, TOKEN_IF))
        recIfStatement(parser);
    else if(have(parser, TOKEN_WHILE))
        recWhileLoop(parser);
    else if(have(parser, TOKEN_FOR))
        recForLoop(parser);
    else
        compilerError(parser, "expected an if statement or a loop");
}

static void recIfStatement(OCParser* parser) {
    expect(parser, TOKEN_IF);
    recExpression(parser, 0);
    recBlock(parser);
    
    if(match(parser, TOKEN_ELSE)) {
        if(have(parser, TOKEN_LBRACE))
            recBlock(parser);
        else if(have(parser, TOKEN_IF))
            recIfStatement(parser);
        else
            compilerError(parser, "expected block or if statement");
    }
}

static void recWhileLoop(OCParser* parser) {
    expect(parser, TOKEN_WHILE);
    recExpression(parser, 0);
    recBlock(parser);
}

static void recForLoop(OCParser* parser) {
    expect(parser, TOKEN_FOR);
    expect(parser, TOKEN_IDENTIFIER);
    expect(parser, TOKEN_COLON);
    recExpression(parser, 0);
    recBlock(parser);
}

static void recExpression(OCParser* parser, int minPrec) {
    recTerm(parser);
    for(;;) {
        if(!haveBinaryOp(parser) || precedence(parser) < minPrec) {
            break;
        }
        OCToken operator = current(parser);
        int prec = precedence(parser);
        bool right = rightAssoc(parser);
        
        int nextMinPrec = right ? prec : prec + 1;
        lexer_nextToken(&parser->lexer);
        recExpression(parser, nextMinPrec);
    }
}

static void recTerm(OCParser* parser) {
    // TODO: match unary operator
    if(haveUnaryOp(parser)) {
        lexer_nextToken(&parser->lexer);
    }
    
    if(match(parser, TOKEN_LPAREN)) {
        recExpression(parser, 0);
        expect(parser, TOKEN_RPAREN);
    }
    else if(have(parser, TOKEN_IDENTIFIER)) {
        recName(parser);
    }
    else if(have(parser, TOKEN_STRING_LITERAL)) {
        expect(parser, TOKEN_STRING_LITERAL);
    }
    else if(have(parser, TOKEN_INTEGER_LITERAL)) {
        expect(parser, TOKEN_INTEGER_LITERAL);
    }
    else if(have(parser, TOKEN_FLOAT_LITERAL)) {
        expect(parser, TOKEN_FLOAT_LITERAL);
    }
    else {
        compilerError(parser, "expected an expression term");
    }
}

static void recName(OCParser* parser) {
    expect(parser, TOKEN_IDENTIFIER);
    for(;;) {
        if(have(parser, TOKEN_LBRACKET))
            recSubscript(parser);
        else if(have(parser, TOKEN_DOT))
            recFieldAccess(parser);
        else if(have(parser, TOKEN_LPAREN))
            recFuncCall(parser);
        else
            break;
    }
}

static void recSubscript(OCParser* parser) {
    expect(parser, TOKEN_LBRACKET);
    recExpression(parser, 0);
    expect(parser, TOKEN_RBRACKET);
}

static void recFieldAccess(OCParser* parser) {
    expect(parser, TOKEN_DOT);
    expect(parser, TOKEN_IDENTIFIER);
}

static void recFuncCall(OCParser* parser) {
    expect(parser, TOKEN_LPAREN);
    recExprList(parser); // TODO: surround with proper director list
    expect(parser, TOKEN_RPAREN);
}

static void recExprList(OCParser* parser) {
    recExpression(parser, 0);
    while(match(parser, TOKEN_COMMA)) {
        recExpression(parser, 0);
    }
}

static void recType(OCParser* parser) {
    if(match(parser, TOKEN_MAYBE)) {
        // TODO: sema+codecheck
    }
    recTypename(parser);
}

static void recTypename(OCParser* parser) {
    if(have(parser, TOKEN_NUMBER)
       || have(parser, TOKEN_BOOL)
       || have(parser, TOKEN_STRING)
       || have(parser, TOKEN_NIL)
       || have(parser, TOKEN_VOID)
       || have(parser, TOKEN_ANY)) {
       recPrimitive(parser);
   }
   // TODO: change to non-ambiguous collection syntax
   else if(have(parser, TOKEN_LBRACKET)) {
       recArrayType(parser);
   }
   else if(have(parser, TOKEN_IDENTIFIER)) {
       // TODO: user type
       expect(parser, TOKEN_IDENTIFIER);
   }
   else {
       compilerError(parser, "expected a type name");
   }
}

static void recPrimitive(OCParser* parser) {
    if(have(parser, TOKEN_NUMBER))
        expect(parser, TOKEN_NUMBER);
    else if(have(parser, TOKEN_BOOL))
        expect(parser, TOKEN_BOOL);
    else if(have(parser, TOKEN_STRING))
        expect(parser, TOKEN_STRING);
    else if(have(parser, TOKEN_NIL))
        expect(parser, TOKEN_NIL);
    else if(have(parser, TOKEN_VOID))
        expect(parser, TOKEN_VOID);
    else if(have(parser, TOKEN_ANY))
        expect(parser, TOKEN_ANY);
    else
        compilerError(parser, "expected a primitive type");
}

static void recArrayType(OCParser* parser) {
    expect(parser, TOKEN_LBRACKET);
    recType(parser);
    expect(parser, TOKEN_RBRACKET);
}

static void recMapType(OCParser* parser) {
    expect(parser, TOKEN_LBRACKET);
    recPrimitive(parser);
    expect(parser, TOKEN_COLON);
    recType(parser);
    expect(parser, TOKEN_RBRACKET);
}

// static void recNumberLiteral(OCParser* parser) {
//     if(have(parser, TOKEN_INTEGER_LITERAL))
//         expect(parser, TOKEN_INTEGER_LITERAL);
//     if(have(parser, TOKEN_FLOAT_LITERAL))
//         expect(parser, TOKEN_FLOAT_LITERAL);
//     else
//         compilerError(parser, "expected a number constant");
//
// }
//
// static void recStringLiteral(OCParser* parser) {
//     expect(parser, TOKEN_STRING_LITERAL);
// }

bool orbit_compile(OrbitVM* vm, const char* sourcePath, const char* source, uint64_t length) {
    
    OCParser parser;
    parser.vm = vm;
    parser.recovering = false;
    lexer_init(&parser.lexer, sourcePath, source, length);
    
    lexer_nextToken(&parser.lexer);
    recProgram(&parser);
//  while(!have(&parser, TOKEN_EOF)) {
//         OCToken tok = current(&parser);
//         printf("%20s\t'%.*s'\n", orbit_tokenName(tok.type), (int)tok.length, tok.start);
//         lexer_nextToken(&parser.lexer);
//     }
    return true;
}

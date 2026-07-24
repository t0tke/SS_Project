%{
#include <stdio.h>
#include <stdlib.h>
#include "assembler.hpp"

extern int  yylex();
extern int  yylineno;
extern char* yytext;

void yyerror(const char* msg) {
    fprintf(stderr, "Greska u liniji %d: %s (kod: '%s')\n", yylineno, msg, yytext);
}

extern Assembler* gAssembler;
%}

%union {
    char* str;
    int   num;
}

%token EOL INVALID
%token HALT INT IRET RET
%token CALL JMP BEQ BNE BGT
%token PUSH POP NOT XCHG
%token ADD SUB MUL DIV
%token AND OR XOR SHL SHR
%token LD ST CSRRD CSRWR
%token DOT_GLOBAL DOT_EXTERN DOT_SECTION
%token DOT_WORD DOT_SKIP DOT_ASCII DOT_END
%token COLON COMMA LBRACKET RBRACKET PLUS 
%token LCRACKET RCRACKET
%token SHIFTLEFT SHIFTRIGHT

%token <str> REGISTER
%token <str> CSR
%token <str> IMMEDIATE
%token <str> NUMBER
%token <str> SYMBOL
%token <str> STRING

%%

program:
      %empty
    | program line
    ;

line:
      EOL
    | label EOL
    | label statement EOL
    |       statement EOL
    | error EOL { YYABORT; }
    ;

label:
    SYMBOL COLON                   { gAssembler->defineLabel($1); free($1); }
    ;

statement:
      directive
    | instruction
    ;

directive:
      DOT_GLOBAL sym_list
    | DOT_EXTERN ext_list
    | DOT_SECTION SYMBOL           { gAssembler->startSection($2); free($2); }
    | DOT_WORD    init_list
    | DOT_SKIP    NUMBER           { gAssembler->directiveSkip($2); free($2); }
    | DOT_ASCII   STRING           { gAssembler->directiveAscii($2); free($2); }
    | DOT_END                      { gAssembler->directiveEnd(); YYACCEPT; }
    ;

sym_list:
      SYMBOL                       { gAssembler->directiveGlobal($1); free($1); }
    | sym_list COMMA SYMBOL        { gAssembler->directiveGlobal($3); free($3); }
    ;

ext_list:
      SYMBOL                       { gAssembler->directiveExtern($1); free($1); }
    | ext_list COMMA SYMBOL        { gAssembler->directiveExtern($3); free($3); }
    ;

init_list:
      init_item
    | init_list COMMA init_item
    ;

init_item:
      NUMBER                       { gAssembler->directiveWordLit($1); free($1); }
    | SYMBOL                       { gAssembler->directiveWordSym($1); free($1); }
    ;

instruction:
      HALT                         { gAssembler->encodeHalt(); }
    | INT                          { gAssembler->encodeInt(); }
    | IRET                         { gAssembler->encodeIret(); }
    | RET                          { gAssembler->encodeRet(); }
    | PUSH REGISTER                { gAssembler->encodePush($2); free($2); }
    | PUSH LCRACKET reg_list RCRACKET {}
    | POP  REGISTER                { gAssembler->encodePop($2); free($2); }
    | NOT  REGISTER                { gAssembler->encodeNot($2); free($2); }
    | XCHG REGISTER COMMA REGISTER { gAssembler->encodeXchg($2, $4); free($2); free($4); }
    | ADD  REGISTER COMMA REGISTER { gAssembler->encodeAdd($2, $4); free($2); free($4); }
    | ADD  REGISTER COMMA REGISTER SHIFTLEFT NUMBER { gAssembler->encodeAddWithShiftLeft($2, $4, $6); free($2); free($4); free($6);}
    | ADD  REGISTER COMMA REGISTER SHIFTRIGHT NUMBER { gAssembler->encodeAddWithShiftRight($2, $4, $6); free($2); free($4); free($6);}
    | SUB  REGISTER COMMA REGISTER { gAssembler->encodeSub($2, $4); free($2); free($4); }
    | MUL  REGISTER COMMA REGISTER { gAssembler->encodeMul($2, $4); free($2); free($4); }
    | DIV  REGISTER COMMA REGISTER { gAssembler->encodeDiv($2, $4); free($2); free($4); }
    | AND  REGISTER COMMA REGISTER { gAssembler->encodeAnd($2, $4); free($2); free($4); }
    | OR   REGISTER COMMA REGISTER { gAssembler->encodeOr($2, $4); free($2); free($4); }
    | XOR  REGISTER COMMA REGISTER { gAssembler->encodeXor($2, $4); free($2); free($4); }
    | SHL  REGISTER COMMA REGISTER { gAssembler->encodeShl($2, $4); free($2); free($4); }
    | SHR  REGISTER COMMA REGISTER { gAssembler->encodeShr($2, $4); free($2); free($4); }
    | JMP  jmp_op                  { }
    | CALL jmp_op                  { gAssembler->switchToCall(); }
    | BEQ REGISTER COMMA REGISTER COMMA jmp_op
                                   { gAssembler->patchBranch(BR_BEQ, $2, $4); free($2); free($4); }
    | BNE REGISTER COMMA REGISTER COMMA jmp_op
                                   { gAssembler->patchBranch(BR_BNE, $2, $4); free($2); free($4); }
    | BGT REGISTER COMMA REGISTER COMMA jmp_op
                                   { gAssembler->patchBranch(BR_BGT, $2, $4); free($2); free($4); }
    | LD  ld_op COMMA REGISTER     { gAssembler->finalizeLD($4); free($4); }
    | ST  REGISTER COMMA st_op     { gAssembler->finalizeST($2); free($2); }
    | CSRRD CSR     COMMA REGISTER { gAssembler->encodeCsrrd($2, $4); free($2); free($4); }
    | CSRWR REGISTER COMMA CSR     { gAssembler->encodeCsrwr($2, $4); free($2); free($4); }
    ;

reg_list:
      REGISTER                       { gAssembler->encodePush($1); free($1); }
    | reg_list COMMA REGISTER        { gAssembler->encodePush($3); free($3); }
    ;

jmp_op:
      NUMBER                       { gAssembler->encodeJmpLit($1); free($1); }
    | SYMBOL                       { gAssembler->encodeJmpSym($1); free($1); }
    ;

ld_op:
      IMMEDIATE                              { gAssembler->ldImmediateOp($1); free($1); }
    | NUMBER                                 { gAssembler->ldMemLitOp($1); free($1); }
    | SYMBOL                                 { gAssembler->ldMemSymOp($1); free($1); }
    | REGISTER                               { gAssembler->ldRegOp($1); free($1); }
    | LBRACKET REGISTER RBRACKET             { gAssembler->ldMemRegOp($2); free($2); }
    | LBRACKET REGISTER PLUS NUMBER RBRACKET { gAssembler->ldMemRegLitOp($2, $4); free($2); free($4); }
    | LBRACKET REGISTER PLUS SYMBOL RBRACKET { gAssembler->ldMemRegSymOp($2, $4); free($2); free($4); }
    ;

st_op:
      NUMBER                                 { gAssembler->stMemLitOp($1); free($1); }
    | SYMBOL                                 { gAssembler->stMemSymOp($1); free($1); }
    | LBRACKET REGISTER RBRACKET             { gAssembler->stMemRegOp($2); free($2); }
    | LBRACKET REGISTER PLUS NUMBER RBRACKET { gAssembler->stMemRegLitOp($2, $4); free($2); free($4); }
    | LBRACKET REGISTER PLUS SYMBOL RBRACKET { gAssembler->stMemRegSymOp($2, $4); free($2); free($4); }
    ;

%%

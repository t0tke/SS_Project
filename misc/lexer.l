%{
  // lexer.l - Flex lekser za asembler
  // Sistemski softver 2025/2026
  // include-uje parser.hpp koji bison generise sa: bison -d -o parser.cpp parser.y
  #include <string.h>
  #include "parser.hpp"
%}

%option noyywrap
%option nounput
%option noinput
%option yylineno

%%

"#".*                       {}
[ \t\r]+                    {}
\n                          { return EOL; }

".global"                   { return DOT_GLOBAL;  }
".extern"                   { return DOT_EXTERN;  }
".section"                  { return DOT_SECTION; }
".word"                     { return DOT_WORD;    }
".skip"                     { return DOT_SKIP;    }
".ascii"                    { return DOT_ASCII;   }
".equ"                      { return DOT_EQU;     }
".end"                      { return DOT_END;     }

"halt"                      { return HALT;  }
"int"                       { return INT;   }
"iret"                      { return IRET;  }
"ret"                       { return RET;   }
"call"                      { return CALL;  }
"jmp"                       { return JMP;   }
"beq"                       { return BEQ;   }
"bne"                       { return BNE;   }
"bgt"                       { return BGT;   }
"push"                      { return PUSH;  }
"pop"                       { return POP;   }
"xchg"                      { return XCHG;  }
"add"                       { return ADD;   }
"sub"                       { return SUB;   }
"mul"                       { return MUL;   }
"div"                       { return DIV;   }
"not"                       { return NOT;   }
"and"                       { return AND;   }
"or"                        { return OR;    }
"xor"                       { return XOR;   }
"shl"                       { return SHL;   }
"shr"                       { return SHR;   }
"ld"                        { return LD;    }
"st"                        { return ST;    }
"csrrd"                     { return CSRRD; }
"csrwr"                     { return CSRWR; }

"%status"                   { yylval.str = strdup(yytext + 1); return CSR; }
"%handler"                  { yylval.str = strdup(yytext + 1); return CSR; }
"%cause"                    { yylval.str = strdup(yytext + 1); return CSR; }

"%sp"                       { yylval.str = strdup("%r14"); return REGISTER; }
"%pc"                       { yylval.str = strdup("%r15"); return REGISTER; }
"%r"(1[0-5]|[0-9])         { yylval.str = strdup(yytext); return REGISTER; }

"$"0[xX][0-9a-fA-F]+       { yylval.str = strdup(yytext); return IMMEDIATE; }
"$"-?[0-9]+                 { yylval.str = strdup(yytext); return IMMEDIATE; }
"$"[a-zA-Z_][a-zA-Z0-9_]*  { yylval.str = strdup(yytext); return IMMEDIATE; }

0[xX][0-9a-fA-F]+           { yylval.str = strdup(yytext); return NUMBER; }
-?[0-9]+                    { yylval.str = strdup(yytext); return NUMBER; }

\.?[a-zA-Z_][a-zA-Z0-9_]*  { yylval.str = strdup(yytext); return SYMBOL; }

\"([^\\"]|\\.)*\"           { yylval.str = strdup(yytext); return STRING; }

":"                         { return COLON;    }
","                         { return COMMA;    }
"["                         { return LBRACKET; }
"]"                         { return RBRACKET; }
"+"                         { return PLUS;     }
"-"                         { return MINUS;    }

.   { fprintf(stderr, "[Lekser] Greska u liniji %d: neprepoznat karakter '%s'\n", yylineno, yytext); return INVALID; }

%%

%{
#include <stdio.h>
#include "wisp.h"

/* lex declarations */
extern FILE *yyin;
int yylex ();
int yyerror (char *);
extern int line_num;

int yywrap ()
{
  return 1;
}

%}
%defines

%union {
  object_t *str;
  object_t *i;
  object_t *f;
  object_t *sym;
}

%token <str> TOK_STR
%token <i> TOK_INT
%token <f> TOK_FLOAT
%token <sym> TOK_SYMBOL
%token LP
%token RP

%%

input : /* empty */
     | input exp
;

exp : sexp   { printf("SEXP\n"); }
;

sexp : atom                 { printf("see atom\n"); }
     | lp series rp         { printf("see (s)\n"); }
     | lp sexp '.' sexp rp  { printf("see (s . s)\n"); }
;

series : sexp series
     | /* empty */
;

atom : TOK_STR            { printf("see STR\n"); }
     | TOK_INT            { printf("see INT\n"); }
     | TOK_FLOAT          { printf("see FLOAT\n"); }
     | TOK_SYMBOL         { printf("see SYMBOL\n"); }
;

lp : LP                   { printf("LP\n"); }
;

rp : RP                   { printf("RP\n"); }
;

%%

int yyerror (char *s)
{
  printf ("wisp:%d: error: %s\n", line_num, s);
  return 0;
}

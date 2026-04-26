/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_LSP_YY_PARSER_LSP_TAB_H_INCLUDED
# define YY_LSP_YY_PARSER_LSP_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef LSP_YYDEBUG
# if defined YYDEBUG
#if YYDEBUG
#   define LSP_YYDEBUG 1
#  else
#   define LSP_YYDEBUG 0
#  endif
# else /* ! defined YYDEBUG */
#  define LSP_YYDEBUG 0
# endif /* ! defined YYDEBUG */
#endif  /* ! defined LSP_YYDEBUG */
#if LSP_YYDEBUG
extern int lsp_yydebug;
#endif
/* "%code requires" blocks.  */
#line 67 "parser_lsp.y"
 #include "ast.h" 

#line 60 "parser_lsp.tab.h"

/* Token kinds.  */
#ifndef LSP_YYTOKENTYPE
# define LSP_YYTOKENTYPE
  enum lsp_yytokentype
  {
    LSP_YYEMPTY = -2,
    LSP_YYEOF = 0,                 /* "end of file"  */
    LSP_YYerror = 256,             /* error  */
    LSP_YYUNDEF = 257,             /* "invalid token"  */
    INCLUDE = 258,                 /* INCLUDE  */
    CLASS = 259,                   /* CLASS  */
    PUBLIC = 260,                  /* PUBLIC  */
    PRIVATE = 261,                 /* PRIVATE  */
    IF = 262,                      /* IF  */
    ELSE = 263,                    /* ELSE  */
    RETURN = 264,                  /* RETURN  */
    NEW = 265,                     /* NEW  */
    NIL = 266,                     /* NIL  */
    TRUE_LIT = 267,                /* TRUE_LIT  */
    FALSE_LIT = 268,               /* FALSE_LIT  */
    WHILE = 269,                   /* WHILE  */
    FOR = 270,                     /* FOR  */
    IN = 271,                      /* IN  */
    BREAK = 272,                   /* BREAK  */
    CONTINUE = 273,                /* CONTINUE  */
    SWITCH = 274,                  /* SWITCH  */
    CASE = 275,                    /* CASE  */
    DEFAULT = 276,                 /* DEFAULT  */
    DEFER = 277,                   /* DEFER  */
    MATCH = 278,                   /* MATCH  */
    UNSAFE = 279,                  /* UNSAFE  */
    CONST = 280,                   /* CONST  */
    STATIC = 281,                  /* STATIC  */
    EXTERN = 282,                  /* EXTERN  */
    AMP = 283,                     /* AMP  */
    INT = 284,                     /* INT  */
    STRING = 285,                  /* STRING  */
    ERROR = 286,                   /* ERROR  */
    BOOL = 287,                    /* BOOL  */
    ASM_BLOCK = 288,               /* ASM_BLOCK  */
    LBRACE = 289,                  /* LBRACE  */
    RBRACE = 290,                  /* RBRACE  */
    LPAREN = 291,                  /* LPAREN  */
    RPAREN = 292,                  /* RPAREN  */
    LBRACKET = 293,                /* LBRACKET  */
    RBRACKET = 294,                /* RBRACKET  */
    SEMICOLON = 295,               /* SEMICOLON  */
    COMMA = 296,                   /* COMMA  */
    DOT = 297,                     /* DOT  */
    QUESTION = 298,                /* QUESTION  */
    ASSIGN = 299,                  /* ASSIGN  */
    DECLARE_ASSIGN = 300,          /* DECLARE_ASSIGN  */
    INC = 301,                     /* INC  */
    DEC = 302,                     /* DEC  */
    PLUS_ASSIGN = 303,             /* PLUS_ASSIGN  */
    MINUS_ASSIGN = 304,            /* MINUS_ASSIGN  */
    STAR_ASSIGN = 305,             /* STAR_ASSIGN  */
    SLASH_ASSIGN = 306,            /* SLASH_ASSIGN  */
    MOD_ASSIGN = 307,              /* MOD_ASSIGN  */
    AND = 308,                     /* AND  */
    OR = 309,                      /* OR  */
    NOT = 310,                     /* NOT  */
    GE = 311,                      /* GE  */
    LE = 312,                      /* LE  */
    EQ = 313,                      /* EQ  */
    NE = 314,                      /* NE  */
    GT = 315,                      /* GT  */
    LT = 316,                      /* LT  */
    PLUS = 317,                    /* PLUS  */
    MINUS = 318,                   /* MINUS  */
    STAR = 319,                    /* STAR  */
    SLASH = 320,                   /* SLASH  */
    MOD = 321,                     /* MOD  */
    CCPINCLUDE = 322,              /* CCPINCLUDE  */
    VOID = 323,                    /* VOID  */
    ARRAY = 324,                   /* ARRAY  */
    MULTI = 325,                   /* MULTI  */
    ANY_KW = 326,                  /* ANY_KW  */
    PIPE = 327,                    /* PIPE  */
    IDENTIFIER = 328,              /* IDENTIFIER  */
    STRING_LITERAL = 329,          /* STRING_LITERAL  */
    FLOAT_LITERAL = 330,           /* FLOAT_LITERAL  */
    INTERP_STRING = 331,           /* INTERP_STRING  */
    NUMBER = 332,                  /* NUMBER  */
    UMINUS = 333                   /* UMINUS  */
  };
  typedef enum lsp_yytokentype lsp_yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined LSP_YYSTYPE && ! defined LSP_YYSTYPE_IS_DECLARED
union LSP_YYSTYPE
{
#line 71 "parser_lsp.y"

    char* str;
    int num;
    ASTNode* node;
    ProgramNode* program;
    ClassNode* class_node;
    MethodNode* method_node;
    FuncNode* func_node;
    FieldNode* field_node;
    Type type_node;
    void* node_list;

#line 168 "parser_lsp.tab.h"

};
typedef union LSP_YYSTYPE LSP_YYSTYPE;
# define LSP_YYSTYPE_IS_TRIVIAL 1
# define LSP_YYSTYPE_IS_DECLARED 1
#endif


extern LSP_YYSTYPE lsp_yylval;


int lsp_yyparse (void);


#endif /* !YY_LSP_YY_PARSER_LSP_TAB_H_INCLUDED  */

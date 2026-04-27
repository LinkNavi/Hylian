/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

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

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1

/* Substitute the type names.  */
#define YYSTYPE         LSP_YYSTYPE
/* Substitute the variable and function names.  */
#define yyparse         lsp_yyparse
#define yylex           lsp_yylex
#define yyerror         lsp_yyerror
#define yydebug         lsp_yydebug
#define yynerrs         lsp_yynerrs
#define yylval          lsp_yylval
#define yychar          lsp_yychar

/* First part of user prologue.  */
#line 1 "parser_lsp.y"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "lsp_diag.h"

/* These are defined/declared with the lsp_ prefix to avoid clashing with the
   compiler's own parser when both translation units are linked together. */
extern int lsp_yylex();
extern int lsp_yylineno;
#define yylex  lsp_yylex
#define yylineno lsp_yylineno

/* Current file being parsed — set by lsp_analysis before calling lsp_yyparse */
const char *lsp_current_file = "<unknown>";

void yyerror(const char *s) {
    lsp_diag_push(lsp_yylineno - 1, 0, lsp_yylineno - 1, 999,
                  LSP_DIAG_ERROR, s);
}

/* Stamp the current line number onto any ASTNode* or ASTNode-embedding struct */
#define SET_LINE(n) do { if (n) ((ASTNode*)(n))->line = lsp_yylineno; } while(0)

ProgramNode* root = NULL;

typedef struct NodeList {
    ASTNode** items;
    int count;
    int capacity;
} NodeList;

NodeList* list_new() {
    NodeList* l = malloc(sizeof(NodeList));
    l->items = NULL; l->count = 0; l->capacity = 0;
    return l;
}

void list_add(NodeList* l, ASTNode* node) {
    if (l->count >= l->capacity) {
        l->capacity = l->capacity == 0 ? 8 : l->capacity * 2;
        l->items = realloc(l->items, l->capacity * sizeof(ASTNode*));
    }
    l->items[l->count++] = node;
}

/* TypeList: a growable array of Type values for union_types */
typedef struct TypeList {
    Type *items;
    int count;
    int capacity;
} TypeList;

TypeList* typelist_new() {
    TypeList* l = malloc(sizeof(TypeList));
    l->items = NULL; l->count = 0; l->capacity = 0;
    return l;
}

void typelist_add(TypeList* l, Type t) {
    if (l->count >= l->capacity) {
        l->capacity = l->capacity == 0 ? 4 : l->capacity * 2;
        l->items = realloc(l->items, l->capacity * sizeof(Type));
    }
    l->items[l->count++] = t;
}

#line 148 "parser_lsp.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "parser_lsp.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_INCLUDE = 3,                    /* INCLUDE  */
  YYSYMBOL_CLASS = 4,                      /* CLASS  */
  YYSYMBOL_PUBLIC = 5,                     /* PUBLIC  */
  YYSYMBOL_PRIVATE = 6,                    /* PRIVATE  */
  YYSYMBOL_IF = 7,                         /* IF  */
  YYSYMBOL_ELSE = 8,                       /* ELSE  */
  YYSYMBOL_RETURN = 9,                     /* RETURN  */
  YYSYMBOL_NEW = 10,                       /* NEW  */
  YYSYMBOL_NIL = 11,                       /* NIL  */
  YYSYMBOL_TRUE_LIT = 12,                  /* TRUE_LIT  */
  YYSYMBOL_FALSE_LIT = 13,                 /* FALSE_LIT  */
  YYSYMBOL_WHILE = 14,                     /* WHILE  */
  YYSYMBOL_FOR = 15,                       /* FOR  */
  YYSYMBOL_IN = 16,                        /* IN  */
  YYSYMBOL_BREAK = 17,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 18,                  /* CONTINUE  */
  YYSYMBOL_SWITCH = 19,                    /* SWITCH  */
  YYSYMBOL_CASE = 20,                      /* CASE  */
  YYSYMBOL_DEFAULT = 21,                   /* DEFAULT  */
  YYSYMBOL_DEFER = 22,                     /* DEFER  */
  YYSYMBOL_MATCH = 23,                     /* MATCH  */
  YYSYMBOL_UNSAFE = 24,                    /* UNSAFE  */
  YYSYMBOL_CONST = 25,                     /* CONST  */
  YYSYMBOL_STATIC = 26,                    /* STATIC  */
  YYSYMBOL_EXTERN = 27,                    /* EXTERN  */
  YYSYMBOL_AMP = 28,                       /* AMP  */
  YYSYMBOL_INT = 29,                       /* INT  */
  YYSYMBOL_STRING = 30,                    /* STRING  */
  YYSYMBOL_ERROR = 31,                     /* ERROR  */
  YYSYMBOL_BOOL = 32,                      /* BOOL  */
  YYSYMBOL_ASM_BLOCK = 33,                 /* ASM_BLOCK  */
  YYSYMBOL_LBRACE = 34,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 35,                    /* RBRACE  */
  YYSYMBOL_LPAREN = 36,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 37,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 38,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 39,                  /* RBRACKET  */
  YYSYMBOL_SEMICOLON = 40,                 /* SEMICOLON  */
  YYSYMBOL_COMMA = 41,                     /* COMMA  */
  YYSYMBOL_DOT = 42,                       /* DOT  */
  YYSYMBOL_QUESTION = 43,                  /* QUESTION  */
  YYSYMBOL_ASSIGN = 44,                    /* ASSIGN  */
  YYSYMBOL_DECLARE_ASSIGN = 45,            /* DECLARE_ASSIGN  */
  YYSYMBOL_INC = 46,                       /* INC  */
  YYSYMBOL_DEC = 47,                       /* DEC  */
  YYSYMBOL_PLUS_ASSIGN = 48,               /* PLUS_ASSIGN  */
  YYSYMBOL_MINUS_ASSIGN = 49,              /* MINUS_ASSIGN  */
  YYSYMBOL_STAR_ASSIGN = 50,               /* STAR_ASSIGN  */
  YYSYMBOL_SLASH_ASSIGN = 51,              /* SLASH_ASSIGN  */
  YYSYMBOL_MOD_ASSIGN = 52,                /* MOD_ASSIGN  */
  YYSYMBOL_AND = 53,                       /* AND  */
  YYSYMBOL_OR = 54,                        /* OR  */
  YYSYMBOL_NOT = 55,                       /* NOT  */
  YYSYMBOL_GE = 56,                        /* GE  */
  YYSYMBOL_LE = 57,                        /* LE  */
  YYSYMBOL_EQ = 58,                        /* EQ  */
  YYSYMBOL_NE = 59,                        /* NE  */
  YYSYMBOL_GT = 60,                        /* GT  */
  YYSYMBOL_LT = 61,                        /* LT  */
  YYSYMBOL_PLUS = 62,                      /* PLUS  */
  YYSYMBOL_MINUS = 63,                     /* MINUS  */
  YYSYMBOL_STAR = 64,                      /* STAR  */
  YYSYMBOL_SLASH = 65,                     /* SLASH  */
  YYSYMBOL_MOD = 66,                       /* MOD  */
  YYSYMBOL_CCPINCLUDE = 67,                /* CCPINCLUDE  */
  YYSYMBOL_VOID = 68,                      /* VOID  */
  YYSYMBOL_ARRAY = 69,                     /* ARRAY  */
  YYSYMBOL_MULTI = 70,                     /* MULTI  */
  YYSYMBOL_ANY_KW = 71,                    /* ANY_KW  */
  YYSYMBOL_PIPE = 72,                      /* PIPE  */
  YYSYMBOL_IDENTIFIER = 73,                /* IDENTIFIER  */
  YYSYMBOL_STRING_LITERAL = 74,            /* STRING_LITERAL  */
  YYSYMBOL_FLOAT_LITERAL = 75,             /* FLOAT_LITERAL  */
  YYSYMBOL_INTERP_STRING = 76,             /* INTERP_STRING  */
  YYSYMBOL_NUMBER = 77,                    /* NUMBER  */
  YYSYMBOL_UMINUS = 78,                    /* UMINUS  */
  YYSYMBOL_YYACCEPT = 79,                  /* $accept  */
  YYSYMBOL_program = 80,                   /* program  */
  YYSYMBOL_include_stmt = 81,              /* include_stmt  */
  YYSYMBOL_ccpinclude_stmt = 82,           /* ccpinclude_stmt  */
  YYSYMBOL_include_list = 83,              /* include_list  */
  YYSYMBOL_include_path = 84,              /* include_path  */
  YYSYMBOL_class_decl = 85,                /* class_decl  */
  YYSYMBOL_func_decl = 86,                 /* func_decl  */
  YYSYMBOL_class_body = 87,                /* class_body  */
  YYSYMBOL_member_decl = 88,               /* member_decl  */
  YYSYMBOL_ctor_decl = 89,                 /* ctor_decl  */
  YYSYMBOL_field_decl = 90,                /* field_decl  */
  YYSYMBOL_method_decl = 91,               /* method_decl  */
  YYSYMBOL_param_list = 92,                /* param_list  */
  YYSYMBOL_params = 93,                    /* params  */
  YYSYMBOL_arg_list = 94,                  /* arg_list  */
  YYSYMBOL_args = 95,                      /* args  */
  YYSYMBOL_nullable_type = 96,             /* nullable_type  */
  YYSYMBOL_tuple_type_items = 97,          /* tuple_type_items  */
  YYSYMBOL_union_types = 98,               /* union_types  */
  YYSYMBOL_type = 99,                      /* type  */
  YYSYMBOL_stmt_list = 100,                /* stmt_list  */
  YYSYMBOL_stmt = 101,                     /* stmt  */
  YYSYMBOL_for_init = 102,                 /* for_init  */
  YYSYMBOL_var_decl = 103,                 /* var_decl  */
  YYSYMBOL_return_stmt = 104,              /* return_stmt  */
  YYSYMBOL_expr = 105                      /* expr  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int16 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if 1

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* 1 */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined LSP_YYSTYPE_IS_TRIVIAL && LSP_YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  2
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   2222

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  79
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  27
/* YYNRULES -- Number of rules.  */
#define YYNRULES  135
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  352

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   333


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78
};

#if LSP_YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   134,   134,   135,   136,   137,   142,   150,   159,   171,
     181,   193,   194,   195,   199,   203,   216,   238,   260,   261,
     263,   285,   310,   315,   322,   329,   330,   334,   335,   336,
     338,   342,   352,   353,   354,   358,   363,   372,   373,   377,
     382,   390,   391,   395,   396,   401,   402,   407,   413,   423,
     428,   436,   437,   438,   439,   440,   441,   443,   445,   447,
     449,   451,   457,   463,   471,   472,   480,   481,   483,   484,
     485,   486,   499,   500,   502,   506,   510,   511,   512,   513,
     514,   516,   520,   524,   529,   538,   544,   549,   555,   562,
     571,   572,   573,   574,   578,   579,   580,   584,   591,   592,
     603,   607,   611,   612,   613,   614,   615,   616,   618,   624,
     626,   632,   637,   639,   645,   647,   648,   649,   650,   651,
     652,   653,   654,   655,   656,   657,   658,   659,   661,   662,
     663,   664,   665,   666,   667,   669
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if 1
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "INCLUDE", "CLASS",
  "PUBLIC", "PRIVATE", "IF", "ELSE", "RETURN", "NEW", "NIL", "TRUE_LIT",
  "FALSE_LIT", "WHILE", "FOR", "IN", "BREAK", "CONTINUE", "SWITCH", "CASE",
  "DEFAULT", "DEFER", "MATCH", "UNSAFE", "CONST", "STATIC", "EXTERN",
  "AMP", "INT", "STRING", "ERROR", "BOOL", "ASM_BLOCK", "LBRACE", "RBRACE",
  "LPAREN", "RPAREN", "LBRACKET", "RBRACKET", "SEMICOLON", "COMMA", "DOT",
  "QUESTION", "ASSIGN", "DECLARE_ASSIGN", "INC", "DEC", "PLUS_ASSIGN",
  "MINUS_ASSIGN", "STAR_ASSIGN", "SLASH_ASSIGN", "MOD_ASSIGN", "AND", "OR",
  "NOT", "GE", "LE", "EQ", "NE", "GT", "LT", "PLUS", "MINUS", "STAR",
  "SLASH", "MOD", "CCPINCLUDE", "VOID", "ARRAY", "MULTI", "ANY_KW", "PIPE",
  "IDENTIFIER", "STRING_LITERAL", "FLOAT_LITERAL", "INTERP_STRING",
  "NUMBER", "UMINUS", "$accept", "program", "include_stmt",
  "ccpinclude_stmt", "include_list", "include_path", "class_decl",
  "func_decl", "class_body", "member_decl", "ctor_decl", "field_decl",
  "method_decl", "param_list", "params", "arg_list", "args",
  "nullable_type", "tuple_type_items", "union_types", "type", "stmt_list",
  "stmt", "for_init", "var_decl", "return_stmt", "expr", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-179)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-57)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -179,  1292,  -179,   -25,   -23,   -22,    44,  -179,  -179,  -179,
    -179,   939,   -12,  -179,     4,    22,  -179,  -179,  -179,  -179,
    -179,   -41,  -179,    17,    59,    29,    64,    26,    70,    75,
     939,  1375,    46,    81,   278,  -179,    41,    86,   371,    95,
     939,  -179,   939,  -179,  -179,   -34,    -7,   -37,  -179,    96,
     939,    91,    97,    34,    61,  -179,  -179,  -179,   114,   116,
     115,   118,  1386,  -179,  -179,  1340,  1386,  1386,  1386,  1386,
    1386,   -31,  -179,  -179,  -179,  -179,   -35,  -179,  -179,  -179,
    1605,   119,    17,    83,   120,   939,   939,  -179,   126,   442,
    -179,  -179,  -179,  -179,   -19,   513,  -179,  -179,    87,  -179,
      88,  -179,    89,  -179,   939,   939,   130,   127,   102,  -179,
    1386,  1386,  -179,   133,  1576,   134,  1386,   868,  -179,  -179,
    1634,    25,  1427,   138,   139,  2069,    -2,    -2,    31,    31,
    1386,  1386,  1386,  1386,  1386,  1386,  1386,  1386,   106,   -13,
    1386,  -179,   108,  -179,  -179,  1386,  1386,  1386,  1386,  1386,
    1386,  1386,  1386,  1386,  1386,  1386,  1386,  1386,  -179,    86,
    -179,  -179,   109,   110,   939,  -179,   149,  -179,   117,    39,
    -179,   151,   584,   128,   129,   135,  -179,   159,   164,   939,
    -179,  1457,  1386,  -179,  1386,   131,  1386,  1487,   142,     6,
     143,   177,  -179,  -179,  1386,  -179,  1386,   174,  1663,  1692,
    1721,  1750,  1779,  1808,  1837,    51,  -179,  1386,  1866,   -11,
    2127,  2098,   218,   218,  2156,  2156,   218,   218,   167,   167,
      31,    31,    31,   178,   180,   184,   939,   186,   939,  -179,
     939,  -179,  -179,  -179,  -179,   190,  -179,   153,   196,  1895,
      58,   198,   200,   209,   229,  1386,  1386,   203,  1386,    55,
    2069,  -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,
    1386,  1924,   204,  1386,  1386,  -179,  -179,   215,   214,   939,
     220,   222,  -179,   349,  -179,  -179,  -179,  -179,  -179,  -179,
    1386,  1517,  2069,  1386,  1953,  -179,  1982,  -179,  1386,   224,
    2011,  -179,   221,   225,   232,   233,   420,  -179,   491,   562,
    1547,   235,  2069,   171,  -179,  2040,  -179,  -179,   633,  -179,
     237,  -179,  -179,  -179,   244,  -179,   238,  -179,   240,   219,
     239,  -179,   655,   704,  -179,   775,   846,   241,  -179,   917,
    -179,   243,   726,  -179,   988,  -179,   655,  -179,  1059,  -179,
    1130,  -179,  -179,  -179,   797,  1201,  -179,  -179,  1272,  -179,
    -179,  -179
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       2,     0,     1,     0,     0,     0,     0,    51,    52,    53,
      55,     0,     0,    54,     0,     0,    56,     3,     4,     5,
       6,     0,    64,     0,     0,     0,     0,     0,    45,    10,
       0,     0,     0,     0,     0,    14,     0,    11,     0,     0,
       0,    63,     0,    46,     9,     0,     0,     0,    49,     0,
      37,     0,     0,     0,     0,   105,   106,   107,     0,     0,
       0,     0,     0,    71,    24,     0,    41,     0,     0,     0,
       0,   112,   103,   102,   104,   101,     0,    65,    66,    67,
       0,     7,    13,     0,     0,     0,     0,    19,    56,     0,
      25,    29,    27,    28,     0,     0,    47,    48,     0,    57,
       0,    59,     0,    61,     0,    37,     0,    38,     0,    68,
       0,     0,   100,   112,     0,     0,     0,    93,    69,    70,
       0,   112,     0,     0,    42,    43,   130,   131,   128,   129,
      41,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    73,     0,   132,   133,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     8,    12,
      15,    30,     0,     0,    37,    17,    56,    26,     0,     0,
      18,    56,     0,     0,     0,     0,    50,     0,     0,     0,
      39,     0,     0,    98,     0,     0,    41,     0,     0,    56,
       0,     0,    72,   134,     0,   108,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    95,     0,     0,   114,
     126,   127,   123,   122,   124,   125,   121,   120,   115,   116,
     117,   118,   119,     0,     0,     0,    37,     0,    37,    34,
      37,    16,    58,    60,    62,     0,    64,     0,     0,     0,
       0,   114,     0,     0,     0,     0,     0,    91,     0,     0,
      44,   110,    75,    74,    76,    77,    78,    79,    80,    97,
       0,     0,   109,    41,     0,    33,    32,     0,     0,    37,
       0,     0,    64,     0,    40,    64,   109,    99,   111,    64,
       0,     0,    92,     0,     0,   135,     0,    94,     0,     0,
       0,    64,     0,     0,     0,     0,     0,    22,     0,     0,
       0,     0,    90,     0,    96,     0,   113,    81,     0,    64,
       0,    64,    64,    23,    83,    85,     0,    64,     0,    56,
       0,    82,     0,     0,    64,     0,     0,     0,    64,     0,
      64,     0,     0,    31,     0,    35,     0,    64,     0,    88,
       0,    64,    21,    36,     0,     0,    89,    87,     0,    20,
      84,    86
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -179,  -179,  -179,  -179,  -179,   212,  -179,  -179,   -94,   -86,
    -179,  -179,  -179,   -77,  -179,  -115,  -178,    72,  -179,  -179,
      -1,  -118,  -179,   -17,  -179,  -179,   -10
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,     1,    17,    18,    36,    37,    19,    20,    89,    90,
      91,    92,    93,   106,   107,   123,   124,    26,    27,    47,
      76,    34,    77,   191,    78,    79,    80
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      21,   172,    32,   167,   102,   130,   240,    98,   138,    22,
      28,    23,   -56,   131,   132,   197,   249,   133,   134,   135,
     136,   137,   245,   103,   168,   263,    99,   206,   177,    45,
      48,   207,    33,   264,   100,   104,   182,    94,   139,    28,
     185,    28,   -56,   114,    54,    55,    56,    57,    25,   108,
     246,    24,   120,   101,   169,   122,   125,   126,   127,   128,
     129,   130,    29,    41,    28,    30,   -56,    42,   -56,   182,
     111,   242,    66,   185,   112,   228,    81,   143,   144,   229,
      67,    68,    82,    31,   162,   163,   167,   225,    94,    69,
      35,   259,   285,    38,    94,   260,   196,    70,   277,   196,
     181,   122,    39,   176,   108,    40,   187,   113,    72,    73,
      74,    75,    96,    43,    97,    44,   190,    50,   273,    49,
     125,   198,   199,   200,   201,   202,   203,   204,    83,    95,
     208,   109,   105,   110,   115,   210,   211,   212,   213,   214,
     215,   216,   217,   218,   219,   220,   221,   222,   289,   268,
     116,   270,   117,   271,   296,   118,   160,   298,   119,   158,
     161,   299,   164,   108,   173,   174,   175,   178,   179,   130,
     186,    94,   239,   308,   125,   180,   125,   195,   237,   205,
     196,   209,   223,   224,   125,   226,   250,   230,   232,   233,
     227,   323,   293,   325,   326,   234,   235,   261,   236,   329,
       7,     8,     9,    10,   241,   182,   334,    11,   318,   185,
     338,   251,   340,   143,   144,   244,   247,   248,   265,   345,
     266,   267,   269,   348,   272,   108,   274,   108,   332,   108,
     275,   155,   156,   157,   263,   281,   282,   278,   284,    13,
      14,    15,   344,   279,   319,   280,   167,   283,   288,   291,
     286,   292,   327,   125,   290,   309,   182,   294,   167,   295,
     185,   306,   310,   246,   143,   144,   311,   312,   108,   317,
     300,   324,   328,   302,   330,   337,   331,   341,   305,    51,
     153,   154,   155,   156,   157,    52,   320,    53,    54,    55,
      56,    57,    58,    59,   159,    60,    61,     0,     0,     0,
      62,     0,   190,     0,     0,     0,     0,     7,     8,     9,
      10,    63,     0,    64,    65,     0,    66,     0,     0,     0,
       0,    94,     0,     0,    67,    68,     0,     0,     0,     0,
       0,    94,     0,    69,     0,    94,     0,     0,     0,     0,
       0,    70,     0,    94,     0,     0,    13,    14,    15,     0,
      51,    71,    72,    73,    74,    75,    52,     0,    53,    54,
      55,    56,    57,    58,    59,     0,    60,    61,     0,     0,
       0,    62,    84,     0,     0,     0,    85,    86,     7,     8,
       9,    10,    63,     0,   297,    65,     0,    66,     0,     0,
       0,     0,     0,     0,     0,    67,    68,     0,     0,     0,
       7,     8,     9,    10,    69,     0,    87,    11,     0,     0,
       0,     0,    70,     0,     0,     0,     0,    13,    14,    15,
       0,    51,    71,    72,    73,    74,    75,    52,     0,    53,
      54,    55,    56,    57,    58,    59,     0,    60,    61,    13,
      14,    15,    62,    84,    88,     0,     0,    85,    86,     7,
       8,     9,    10,    63,     0,   313,    65,     0,    66,     0,
       0,     0,     0,     0,     0,     0,    67,    68,     0,     0,
       0,     7,     8,     9,    10,    69,     0,   165,    11,     0,
       0,     0,     0,    70,     0,     0,     0,     0,    13,    14,
      15,     0,    51,    71,    72,    73,    74,    75,    52,     0,
      53,    54,    55,    56,    57,    58,    59,     0,    60,    61,
      13,    14,    15,    62,    84,   166,     0,     0,    85,    86,
       7,     8,     9,    10,    63,     0,   314,    65,     0,    66,
       0,     0,     0,     0,     0,     0,     0,    67,    68,     0,
       0,     0,     7,     8,     9,    10,    69,     0,   170,    11,
       0,     0,     0,     0,    70,     0,     0,     0,     0,    13,
      14,    15,     0,    51,    71,    72,    73,    74,    75,    52,
       0,    53,    54,    55,    56,    57,    58,    59,     0,    60,
      61,    13,    14,    15,    62,    84,   171,     0,     0,    85,
      86,     7,     8,     9,    10,    63,     0,   315,    65,     0,
      66,     0,     0,     0,     0,     0,     0,     0,    67,    68,
       0,     0,     0,     7,     8,     9,    10,    69,     0,   231,
      11,     0,     0,     0,     0,    70,     0,     0,     0,     0,
      13,    14,    15,     0,    51,    71,    72,    73,    74,    75,
      52,     0,    53,    54,    55,    56,    57,    58,    59,     0,
      60,    61,    13,    14,    15,    62,    84,   166,     0,     0,
      85,    86,     7,     8,     9,    10,    63,     0,   322,    65,
       0,    66,     0,     0,     0,     0,     0,     0,     0,    67,
      68,     0,     0,     0,     7,     8,     9,    10,    69,     0,
     -31,    11,     0,     0,     0,     0,    70,     0,     0,     0,
       0,    13,    14,    15,     0,    51,    71,    72,    73,    74,
      75,    52,     0,    53,    54,    55,    56,    57,    58,    59,
       0,    60,    61,    13,    14,    15,    62,    84,   166,     0,
       0,    85,    86,     7,     8,     9,    10,    63,     0,   333,
      65,     0,    66,     0,     0,     0,     0,     0,     0,     0,
      67,    68,     0,     0,     0,     7,     8,     9,    10,    69,
       0,   342,    11,     0,     0,     0,     0,    70,     0,     0,
       0,     0,    13,    14,    15,     0,    51,    71,    72,    73,
      74,    75,    52,     0,    53,    54,    55,    56,    57,    58,
      59,     0,    60,    61,    13,    14,    15,    62,    84,   166,
       0,     0,    85,    86,     7,     8,     9,    10,    63,     0,
     335,    65,     0,    66,     0,     0,     0,     0,     0,     0,
       0,    67,    68,     0,     0,     0,     7,     8,     9,    10,
      69,     0,   349,    11,     0,     0,     0,     0,    70,     0,
       0,     0,     0,    13,    14,    15,     0,    51,    71,    72,
      73,    74,    75,    52,     0,    53,    54,    55,    56,    57,
      58,    59,     0,    60,    61,    13,    14,    15,    62,     0,
     166,     0,     0,     0,     0,     7,     8,     9,    10,    63,
       0,   336,    65,     0,    66,     0,     0,     0,     0,     0,
       0,     0,    67,    68,     0,     0,   188,     7,     8,     9,
      10,    69,     0,     0,    11,     0,     0,     0,     0,    70,
       0,     0,     0,     0,    13,    14,    15,     0,    51,    71,
      72,    73,    74,    75,    52,     0,    53,    54,    55,    56,
      57,    58,    59,     0,    60,    61,    13,    14,    15,    62,
       0,   189,     0,     0,     0,     0,     7,     8,     9,    10,
      63,     0,   339,    65,     0,    66,     0,     0,     0,     0,
       0,     0,     0,    67,    68,     0,     0,     0,     7,     8,
       9,    10,    69,     0,     0,    11,     0,     0,     0,     0,
      70,     0,     0,     0,     0,    13,    14,    15,     0,    51,
      71,    72,    73,    74,    75,    52,     0,    53,    54,    55,
      56,    57,    58,    59,     0,    60,    61,    13,    14,    15,
      62,     0,    16,     0,     0,     0,     0,     7,     8,     9,
      10,    63,     0,   343,    65,     0,    66,     0,     0,     0,
       0,     0,     0,     0,    67,    68,     0,     0,     0,     0,
       0,     0,     0,    69,     0,     0,     0,     0,     0,     0,
       0,    70,     0,     0,     0,     0,    13,    14,    15,     0,
      51,    71,    72,    73,    74,    75,    52,     0,    53,    54,
      55,    56,    57,    58,    59,     0,    60,    61,     0,     0,
       0,    62,     0,     0,     0,     0,     0,     0,     7,     8,
       9,    10,    63,     0,   346,    65,     0,    66,     0,     0,
       0,     0,     0,     0,     0,    67,    68,     0,     0,     0,
       0,     0,     0,     0,    69,     0,     0,     0,     0,     0,
       0,     0,    70,     0,     0,     0,     0,    13,    14,    15,
       0,    51,    71,    72,    73,    74,    75,    52,     0,    53,
      54,    55,    56,    57,    58,    59,     0,    60,    61,     0,
       0,     0,    62,     0,     0,     0,     0,     0,     0,     7,
       8,     9,    10,    63,     0,   347,    65,     0,    66,     0,
       0,     0,     0,     0,     0,     0,    67,    68,     0,     0,
       0,     0,     0,     0,     0,    69,     0,     0,     0,     0,
       0,     0,     0,    70,     0,     0,     0,     0,    13,    14,
      15,     0,    51,    71,    72,    73,    74,    75,    52,     0,
      53,    54,    55,    56,    57,    58,    59,     0,    60,    61,
       0,     0,     0,    62,     0,     0,     0,     0,     0,     0,
       7,     8,     9,    10,    63,     0,   350,    65,     0,    66,
       0,     0,     0,     0,     0,     0,     0,    67,    68,     0,
       0,     0,     0,     0,     0,     0,    69,     0,     0,     0,
       0,     0,     0,     0,    70,     0,     0,     0,     0,    13,
      14,    15,     0,    51,    71,    72,    73,    74,    75,    52,
       0,    53,    54,    55,    56,    57,    58,    59,     0,    60,
      61,     0,     2,     3,    62,     4,     5,     6,     0,     0,
       0,     7,     8,     9,    10,    63,     0,   351,    65,     0,
      66,     0,     0,     0,     0,     0,     0,     0,    67,    68,
       0,     7,     8,     9,    10,     0,     0,    69,    11,     0,
       0,     0,     0,     0,     0,    70,     0,     0,     0,     0,
      13,    14,    15,     0,     0,    71,    72,    73,    74,    75,
      54,    55,    56,    57,     0,     0,     0,     0,     0,    12,
      13,    14,    15,     0,     0,    16,     0,     0,     0,     7,
       8,     9,    10,     0,     0,     0,    65,     0,    66,     0,
       0,     0,     0,     0,     0,     0,    67,    68,     0,     0,
       0,     0,     0,     0,     0,    69,    54,    55,    56,    57,
       0,     0,     0,    70,     7,     8,     9,    10,    13,    14,
      15,    11,     0,   121,    72,    73,    74,    75,     0,     0,
       0,     0,   111,     0,    66,     0,     0,     0,     0,     0,
       0,     0,    67,    68,     0,     0,     0,     0,     0,     0,
       0,    69,     0,    13,    14,    15,    46,     0,    16,    70,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   113,
      72,    73,    74,    75,   193,   182,     0,     0,   194,   185,
       0,     0,     0,   143,   144,     0,     0,     0,     0,     0,
     145,   146,     0,   147,   148,   149,   150,   151,   152,   153,
     154,   155,   156,   157,   238,   182,     0,     0,     0,   185,
       0,     0,     0,   143,   144,     0,     0,     0,     0,     0,
     145,   146,     0,   147,   148,   149,   150,   151,   152,   153,
     154,   155,   156,   157,   243,   182,     0,     0,     0,   185,
       0,     0,     0,   143,   144,     0,     0,     0,     0,     0,
     145,   146,     0,   147,   148,   149,   150,   151,   152,   153,
     154,   155,   156,   157,   301,   182,     0,     0,     0,   185,
       0,     0,     0,   143,   144,     0,     0,     0,     0,     0,
     145,   146,     0,   147,   148,   149,   150,   151,   152,   153,
     154,   155,   156,   157,   316,   182,     0,     0,     0,   185,
       0,     0,     0,   143,   144,     0,     0,     0,     0,     0,
     145,   146,     0,   147,   148,   149,   150,   151,   152,   153,
     154,   155,   156,   157,   182,     0,   183,   184,   185,     0,
       0,     0,   143,   144,     0,     0,     0,     0,     0,   145,
     146,     0,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   140,     0,   141,     0,   142,     0,     0,
       0,   143,   144,     0,     0,     0,     0,     0,   145,   146,
       0,   147,   148,   149,   150,   151,   152,   153,   154,   155,
     156,   157,   182,     0,   192,     0,   185,     0,     0,     0,
     143,   144,     0,     0,     0,     0,     0,   145,   146,     0,
     147,   148,   149,   150,   151,   152,   153,   154,   155,   156,
     157,   182,     0,   252,     0,   185,     0,     0,     0,   143,
     144,     0,     0,     0,     0,     0,   145,   146,     0,   147,
     148,   149,   150,   151,   152,   153,   154,   155,   156,   157,
     182,     0,   253,     0,   185,     0,     0,     0,   143,   144,
       0,     0,     0,     0,     0,   145,   146,     0,   147,   148,
     149,   150,   151,   152,   153,   154,   155,   156,   157,   182,
       0,   254,     0,   185,     0,     0,     0,   143,   144,     0,
       0,     0,     0,     0,   145,   146,     0,   147,   148,   149,
     150,   151,   152,   153,   154,   155,   156,   157,   182,     0,
     255,     0,   185,     0,     0,     0,   143,   144,     0,     0,
       0,     0,     0,   145,   146,     0,   147,   148,   149,   150,
     151,   152,   153,   154,   155,   156,   157,   182,     0,   256,
       0,   185,     0,     0,     0,   143,   144,     0,     0,     0,
       0,     0,   145,   146,     0,   147,   148,   149,   150,   151,
     152,   153,   154,   155,   156,   157,   182,     0,   257,     0,
     185,     0,     0,     0,   143,   144,     0,     0,     0,     0,
       0,   145,   146,     0,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156,   157,   182,     0,   258,     0,   185,
       0,     0,     0,   143,   144,     0,     0,     0,     0,     0,
     145,   146,     0,   147,   148,   149,   150,   151,   152,   153,
     154,   155,   156,   157,   182,   262,     0,     0,   185,     0,
       0,     0,   143,   144,     0,     0,     0,     0,     0,   145,
     146,     0,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   182,   276,     0,     0,   185,     0,     0,
       0,   143,   144,     0,     0,     0,     0,     0,   145,   146,
       0,   147,   148,   149,   150,   151,   152,   153,   154,   155,
     156,   157,   182,     0,   287,     0,   185,     0,     0,     0,
     143,   144,     0,     0,     0,     0,     0,   145,   146,     0,
     147,   148,   149,   150,   151,   152,   153,   154,   155,   156,
     157,   182,     0,   303,     0,   185,     0,     0,     0,   143,
     144,     0,     0,     0,     0,     0,   145,   146,     0,   147,
     148,   149,   150,   151,   152,   153,   154,   155,   156,   157,
     182,     0,   304,     0,   185,     0,     0,     0,   143,   144,
       0,     0,     0,     0,     0,   145,   146,     0,   147,   148,
     149,   150,   151,   152,   153,   154,   155,   156,   157,   182,
       0,   307,     0,   185,     0,     0,     0,   143,   144,     0,
       0,     0,     0,     0,   145,   146,     0,   147,   148,   149,
     150,   151,   152,   153,   154,   155,   156,   157,   182,     0,
     321,     0,   185,     0,     0,     0,   143,   144,     0,     0,
       0,     0,     0,   145,   146,     0,   147,   148,   149,   150,
     151,   152,   153,   154,   155,   156,   157,   182,     0,     0,
       0,   185,     0,     0,     0,   143,   144,     0,     0,     0,
       0,     0,   145,   146,     0,   147,   148,   149,   150,   151,
     152,   153,   154,   155,   156,   157,   182,     0,     0,     0,
     185,     0,     0,     0,   143,   144,     0,     0,     0,     0,
       0,   145,     0,     0,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156,   157,   182,     0,     0,     0,   185,
       0,     0,     0,   143,   144,     0,     0,     0,     0,     0,
       0,     0,     0,   147,   148,   149,   150,   151,   152,   153,
     154,   155,   156,   157,   182,     0,     0,     0,   185,     0,
       0,     0,   143,   144,     0,     0,     0,     0,     0,     0,
       0,     0,   147,   148,     0,     0,   151,   152,   153,   154,
     155,   156,   157
};

static const yytype_int16 yycheck[] =
{
       1,    95,    43,    89,    41,    36,   184,    41,    43,    34,
      11,    34,    43,    44,    45,   130,   194,    48,    49,    50,
      51,    52,    16,    60,    43,    36,    60,    40,   105,    30,
      31,    44,    73,    44,    41,    72,    38,    38,    73,    40,
      42,    42,    73,    53,    10,    11,    12,    13,     4,    50,
      44,    73,    62,    60,    73,    65,    66,    67,    68,    69,
      70,    36,    74,    37,    65,    61,    41,    41,    43,    38,
      36,   186,    38,    42,    40,    36,    35,    46,    47,    40,
      46,    47,    41,    61,    85,    86,   172,   164,    89,    55,
      73,    40,    37,    34,    95,    44,    41,    63,    40,    41,
     110,   111,    73,   104,   105,    41,   116,    73,    74,    75,
      76,    77,    40,    43,    42,    40,   117,    36,   236,    73,
     130,   131,   132,   133,   134,   135,   136,   137,    42,    34,
     140,    40,    36,    36,    73,   145,   146,   147,   148,   149,
     150,   151,   152,   153,   154,   155,   156,   157,   263,   226,
      36,   228,    36,   230,   272,    40,    73,   275,    40,    40,
      40,   279,    36,   164,    77,    77,    77,    37,    41,    36,
      36,   172,   182,   291,   184,    73,   186,    39,   179,    73,
      41,    73,    73,    73,   194,    36,   196,    36,    60,    60,
      73,   309,   269,   311,   312,    60,    37,   207,    34,   317,
      29,    30,    31,    32,    73,    38,   324,    36,    37,    42,
     328,    37,   330,    46,    47,    73,    73,    40,    40,   337,
      40,    37,    36,   341,    34,   226,    73,   228,   322,   230,
      34,    64,    65,    66,    36,   245,   246,    37,   248,    68,
      69,    70,   336,    34,    73,    16,   332,    44,    44,    34,
     260,    37,     8,   263,   264,    34,    38,    37,   344,    37,
      42,    37,    37,    44,    46,    47,    34,    34,   269,    34,
     280,    34,    34,   283,    34,    34,    37,    34,   288,     1,
      62,    63,    64,    65,    66,     7,   303,     9,    10,    11,
      12,    13,    14,    15,    82,    17,    18,    -1,    -1,    -1,
      22,    -1,   303,    -1,    -1,    -1,    -1,    29,    30,    31,
      32,    33,    -1,    35,    36,    -1,    38,    -1,    -1,    -1,
      -1,   322,    -1,    -1,    46,    47,    -1,    -1,    -1,    -1,
      -1,   332,    -1,    55,    -1,   336,    -1,    -1,    -1,    -1,
      -1,    63,    -1,   344,    -1,    -1,    68,    69,    70,    -1,
       1,    73,    74,    75,    76,    77,     7,    -1,     9,    10,
      11,    12,    13,    14,    15,    -1,    17,    18,    -1,    -1,
      -1,    22,     1,    -1,    -1,    -1,     5,     6,    29,    30,
      31,    32,    33,    -1,    35,    36,    -1,    38,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    46,    47,    -1,    -1,    -1,
      29,    30,    31,    32,    55,    -1,    35,    36,    -1,    -1,
      -1,    -1,    63,    -1,    -1,    -1,    -1,    68,    69,    70,
      -1,     1,    73,    74,    75,    76,    77,     7,    -1,     9,
      10,    11,    12,    13,    14,    15,    -1,    17,    18,    68,
      69,    70,    22,     1,    73,    -1,    -1,     5,     6,    29,
      30,    31,    32,    33,    -1,    35,    36,    -1,    38,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    46,    47,    -1,    -1,
      -1,    29,    30,    31,    32,    55,    -1,    35,    36,    -1,
      -1,    -1,    -1,    63,    -1,    -1,    -1,    -1,    68,    69,
      70,    -1,     1,    73,    74,    75,    76,    77,     7,    -1,
       9,    10,    11,    12,    13,    14,    15,    -1,    17,    18,
      68,    69,    70,    22,     1,    73,    -1,    -1,     5,     6,
      29,    30,    31,    32,    33,    -1,    35,    36,    -1,    38,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    46,    47,    -1,
      -1,    -1,    29,    30,    31,    32,    55,    -1,    35,    36,
      -1,    -1,    -1,    -1,    63,    -1,    -1,    -1,    -1,    68,
      69,    70,    -1,     1,    73,    74,    75,    76,    77,     7,
      -1,     9,    10,    11,    12,    13,    14,    15,    -1,    17,
      18,    68,    69,    70,    22,     1,    73,    -1,    -1,     5,
       6,    29,    30,    31,    32,    33,    -1,    35,    36,    -1,
      38,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    46,    47,
      -1,    -1,    -1,    29,    30,    31,    32,    55,    -1,    35,
      36,    -1,    -1,    -1,    -1,    63,    -1,    -1,    -1,    -1,
      68,    69,    70,    -1,     1,    73,    74,    75,    76,    77,
       7,    -1,     9,    10,    11,    12,    13,    14,    15,    -1,
      17,    18,    68,    69,    70,    22,     1,    73,    -1,    -1,
       5,     6,    29,    30,    31,    32,    33,    -1,    35,    36,
      -1,    38,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    46,
      47,    -1,    -1,    -1,    29,    30,    31,    32,    55,    -1,
      35,    36,    -1,    -1,    -1,    -1,    63,    -1,    -1,    -1,
      -1,    68,    69,    70,    -1,     1,    73,    74,    75,    76,
      77,     7,    -1,     9,    10,    11,    12,    13,    14,    15,
      -1,    17,    18,    68,    69,    70,    22,     1,    73,    -1,
      -1,     5,     6,    29,    30,    31,    32,    33,    -1,    35,
      36,    -1,    38,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      46,    47,    -1,    -1,    -1,    29,    30,    31,    32,    55,
      -1,    35,    36,    -1,    -1,    -1,    -1,    63,    -1,    -1,
      -1,    -1,    68,    69,    70,    -1,     1,    73,    74,    75,
      76,    77,     7,    -1,     9,    10,    11,    12,    13,    14,
      15,    -1,    17,    18,    68,    69,    70,    22,     1,    73,
      -1,    -1,     5,     6,    29,    30,    31,    32,    33,    -1,
      35,    36,    -1,    38,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    46,    47,    -1,    -1,    -1,    29,    30,    31,    32,
      55,    -1,    35,    36,    -1,    -1,    -1,    -1,    63,    -1,
      -1,    -1,    -1,    68,    69,    70,    -1,     1,    73,    74,
      75,    76,    77,     7,    -1,     9,    10,    11,    12,    13,
      14,    15,    -1,    17,    18,    68,    69,    70,    22,    -1,
      73,    -1,    -1,    -1,    -1,    29,    30,    31,    32,    33,
      -1,    35,    36,    -1,    38,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    46,    47,    -1,    -1,    28,    29,    30,    31,
      32,    55,    -1,    -1,    36,    -1,    -1,    -1,    -1,    63,
      -1,    -1,    -1,    -1,    68,    69,    70,    -1,     1,    73,
      74,    75,    76,    77,     7,    -1,     9,    10,    11,    12,
      13,    14,    15,    -1,    17,    18,    68,    69,    70,    22,
      -1,    73,    -1,    -1,    -1,    -1,    29,    30,    31,    32,
      33,    -1,    35,    36,    -1,    38,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    46,    47,    -1,    -1,    -1,    29,    30,
      31,    32,    55,    -1,    -1,    36,    -1,    -1,    -1,    -1,
      63,    -1,    -1,    -1,    -1,    68,    69,    70,    -1,     1,
      73,    74,    75,    76,    77,     7,    -1,     9,    10,    11,
      12,    13,    14,    15,    -1,    17,    18,    68,    69,    70,
      22,    -1,    73,    -1,    -1,    -1,    -1,    29,    30,    31,
      32,    33,    -1,    35,    36,    -1,    38,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    46,    47,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    55,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    63,    -1,    -1,    -1,    -1,    68,    69,    70,    -1,
       1,    73,    74,    75,    76,    77,     7,    -1,     9,    10,
      11,    12,    13,    14,    15,    -1,    17,    18,    -1,    -1,
      -1,    22,    -1,    -1,    -1,    -1,    -1,    -1,    29,    30,
      31,    32,    33,    -1,    35,    36,    -1,    38,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    46,    47,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    55,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    63,    -1,    -1,    -1,    -1,    68,    69,    70,
      -1,     1,    73,    74,    75,    76,    77,     7,    -1,     9,
      10,    11,    12,    13,    14,    15,    -1,    17,    18,    -1,
      -1,    -1,    22,    -1,    -1,    -1,    -1,    -1,    -1,    29,
      30,    31,    32,    33,    -1,    35,    36,    -1,    38,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    46,    47,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    55,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    63,    -1,    -1,    -1,    -1,    68,    69,
      70,    -1,     1,    73,    74,    75,    76,    77,     7,    -1,
       9,    10,    11,    12,    13,    14,    15,    -1,    17,    18,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    -1,    -1,    -1,
      29,    30,    31,    32,    33,    -1,    35,    36,    -1,    38,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    46,    47,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    55,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    63,    -1,    -1,    -1,    -1,    68,
      69,    70,    -1,     1,    73,    74,    75,    76,    77,     7,
      -1,     9,    10,    11,    12,    13,    14,    15,    -1,    17,
      18,    -1,     0,     1,    22,     3,     4,     5,    -1,    -1,
      -1,    29,    30,    31,    32,    33,    -1,    35,    36,    -1,
      38,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    46,    47,
      -1,    29,    30,    31,    32,    -1,    -1,    55,    36,    -1,
      -1,    -1,    -1,    -1,    -1,    63,    -1,    -1,    -1,    -1,
      68,    69,    70,    -1,    -1,    73,    74,    75,    76,    77,
      10,    11,    12,    13,    -1,    -1,    -1,    -1,    -1,    67,
      68,    69,    70,    -1,    -1,    73,    -1,    -1,    -1,    29,
      30,    31,    32,    -1,    -1,    -1,    36,    -1,    38,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    46,    47,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    55,    10,    11,    12,    13,
      -1,    -1,    -1,    63,    29,    30,    31,    32,    68,    69,
      70,    36,    -1,    73,    74,    75,    76,    77,    -1,    -1,
      -1,    -1,    36,    -1,    38,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    46,    47,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    55,    -1,    68,    69,    70,    71,    -1,    73,    63,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    73,
      74,    75,    76,    77,    37,    38,    -1,    -1,    41,    42,
      -1,    -1,    -1,    46,    47,    -1,    -1,    -1,    -1,    -1,
      53,    54,    -1,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    37,    38,    -1,    -1,    -1,    42,
      -1,    -1,    -1,    46,    47,    -1,    -1,    -1,    -1,    -1,
      53,    54,    -1,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    37,    38,    -1,    -1,    -1,    42,
      -1,    -1,    -1,    46,    47,    -1,    -1,    -1,    -1,    -1,
      53,    54,    -1,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    37,    38,    -1,    -1,    -1,    42,
      -1,    -1,    -1,    46,    47,    -1,    -1,    -1,    -1,    -1,
      53,    54,    -1,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    37,    38,    -1,    -1,    -1,    42,
      -1,    -1,    -1,    46,    47,    -1,    -1,    -1,    -1,    -1,
      53,    54,    -1,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    38,    -1,    40,    41,    42,    -1,
      -1,    -1,    46,    47,    -1,    -1,    -1,    -1,    -1,    53,
      54,    -1,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    38,    -1,    40,    -1,    42,    -1,    -1,
      -1,    46,    47,    -1,    -1,    -1,    -1,    -1,    53,    54,
      -1,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    38,    -1,    40,    -1,    42,    -1,    -1,    -1,
      46,    47,    -1,    -1,    -1,    -1,    -1,    53,    54,    -1,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    38,    -1,    40,    -1,    42,    -1,    -1,    -1,    46,
      47,    -1,    -1,    -1,    -1,    -1,    53,    54,    -1,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      38,    -1,    40,    -1,    42,    -1,    -1,    -1,    46,    47,
      -1,    -1,    -1,    -1,    -1,    53,    54,    -1,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    38,
      -1,    40,    -1,    42,    -1,    -1,    -1,    46,    47,    -1,
      -1,    -1,    -1,    -1,    53,    54,    -1,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    38,    -1,
      40,    -1,    42,    -1,    -1,    -1,    46,    47,    -1,    -1,
      -1,    -1,    -1,    53,    54,    -1,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    38,    -1,    40,
      -1,    42,    -1,    -1,    -1,    46,    47,    -1,    -1,    -1,
      -1,    -1,    53,    54,    -1,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    38,    -1,    40,    -1,
      42,    -1,    -1,    -1,    46,    47,    -1,    -1,    -1,    -1,
      -1,    53,    54,    -1,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    38,    -1,    40,    -1,    42,
      -1,    -1,    -1,    46,    47,    -1,    -1,    -1,    -1,    -1,
      53,    54,    -1,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    38,    39,    -1,    -1,    42,    -1,
      -1,    -1,    46,    47,    -1,    -1,    -1,    -1,    -1,    53,
      54,    -1,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    38,    39,    -1,    -1,    42,    -1,    -1,
      -1,    46,    47,    -1,    -1,    -1,    -1,    -1,    53,    54,
      -1,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    38,    -1,    40,    -1,    42,    -1,    -1,    -1,
      46,    47,    -1,    -1,    -1,    -1,    -1,    53,    54,    -1,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    38,    -1,    40,    -1,    42,    -1,    -1,    -1,    46,
      47,    -1,    -1,    -1,    -1,    -1,    53,    54,    -1,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      38,    -1,    40,    -1,    42,    -1,    -1,    -1,    46,    47,
      -1,    -1,    -1,    -1,    -1,    53,    54,    -1,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    38,
      -1,    40,    -1,    42,    -1,    -1,    -1,    46,    47,    -1,
      -1,    -1,    -1,    -1,    53,    54,    -1,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    38,    -1,
      40,    -1,    42,    -1,    -1,    -1,    46,    47,    -1,    -1,
      -1,    -1,    -1,    53,    54,    -1,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    38,    -1,    -1,
      -1,    42,    -1,    -1,    -1,    46,    47,    -1,    -1,    -1,
      -1,    -1,    53,    54,    -1,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    38,    -1,    -1,    -1,
      42,    -1,    -1,    -1,    46,    47,    -1,    -1,    -1,    -1,
      -1,    53,    -1,    -1,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    38,    -1,    -1,    -1,    42,
      -1,    -1,    -1,    46,    47,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    38,    -1,    -1,    -1,    42,    -1,
      -1,    -1,    46,    47,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    56,    57,    -1,    -1,    60,    61,    62,    63,
      64,    65,    66
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    80,     0,     1,     3,     4,     5,    29,    30,    31,
      32,    36,    67,    68,    69,    70,    73,    81,    82,    85,
      86,    99,    34,    34,    73,     4,    96,    97,    99,    74,
      61,    61,    43,    73,   100,    73,    83,    84,    34,    73,
      41,    37,    41,    43,    40,    99,    71,    98,    99,    73,
      36,     1,     7,     9,    10,    11,    12,    13,    14,    15,
      17,    18,    22,    33,    35,    36,    38,    46,    47,    55,
      63,    73,    74,    75,    76,    77,    99,   101,   103,   104,
     105,    35,    41,    42,     1,     5,     6,    35,    73,    87,
      88,    89,    90,    91,    99,    34,    96,    96,    41,    60,
      41,    60,    41,    60,    72,    36,    92,    93,    99,    40,
      36,    36,    40,    73,   105,    73,    36,    36,    40,    40,
     105,    73,   105,    94,    95,   105,   105,   105,   105,   105,
      36,    44,    45,    48,    49,    50,    51,    52,    43,    73,
      38,    40,    42,    46,    47,    53,    54,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    40,    84,
      73,    40,    99,    99,    36,    35,    73,    88,    43,    73,
      35,    73,    87,    77,    77,    77,    99,    92,    37,    41,
      73,   105,    38,    40,    41,    42,    36,   105,    28,    73,
      99,   102,    40,    37,    41,    39,    41,    94,   105,   105,
     105,   105,   105,   105,   105,    73,    40,    44,   105,    73,
     105,   105,   105,   105,   105,   105,   105,   105,   105,   105,
     105,   105,   105,    73,    73,    92,    36,    73,    36,    40,
      36,    35,    60,    60,    60,    37,    34,    99,    37,   105,
      95,    73,    94,    37,    73,    16,    44,    73,    40,    95,
     105,    37,    40,    40,    40,    40,    40,    40,    40,    40,
      44,   105,    39,    36,    44,    40,    40,    37,    92,    36,
      92,    92,    34,   100,    73,    34,    39,    40,    37,    34,
      16,   105,   105,    44,   105,    37,   105,    40,    44,    94,
     105,    34,    37,    92,    37,    37,   100,    35,   100,   100,
     105,    37,   105,    40,    40,   105,    37,    40,   100,    34,
      37,    34,    34,    35,    35,    35,    37,    34,    37,    73,
     102,    40,    35,   100,    34,   100,   100,     8,    34,   100,
      34,    37,    87,    35,   100,    35,    35,    34,   100,    35,
     100,    34,    35,    35,    87,   100,    35,    35,   100,    35,
      35,    35
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    79,    80,    80,    80,    80,    80,    81,    81,    82,
      82,    83,    83,    83,    84,    84,    85,    85,    85,    85,
      85,    85,    86,    86,    86,    87,    87,    88,    88,    88,
      88,    89,    90,    90,    90,    91,    91,    92,    92,    93,
      93,    94,    94,    95,    95,    96,    96,    97,    97,    98,
      98,    99,    99,    99,    99,    99,    99,    99,    99,    99,
      99,    99,    99,    99,   100,   100,   101,   101,   101,   101,
     101,   101,   101,   101,   101,   101,   101,   101,   101,   101,
     101,   101,   101,   101,   101,   101,   101,   101,   101,   101,
     102,   102,   102,   102,   103,   103,   103,   103,   104,   104,
     104,   105,   105,   105,   105,   105,   105,   105,   105,   105,
     105,   105,   105,   105,   105,   105,   105,   105,   105,   105,
     105,   105,   105,   105,   105,   105,   105,   105,   105,   105,
     105,   105,   105,   105,   105,   105
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     0,     2,     2,     2,     2,     4,     5,     3,
       2,     1,     3,     2,     1,     3,     6,     5,     5,     4,
      13,    12,     8,     9,     4,     1,     2,     1,     1,     1,
       2,     7,     4,     4,     3,     8,     9,     0,     1,     2,
       4,     0,     1,     1,     3,     1,     2,     3,     3,     1,
       3,     1,     1,     1,     1,     1,     1,     4,     6,     4,
       6,     4,     6,     3,     0,     2,     1,     1,     2,     2,
       2,     1,     3,     2,     4,     4,     4,     4,     4,     4,
       4,     6,     7,     7,    11,     7,    11,    10,     9,    10,
       4,     2,     3,     0,     5,     3,     6,     4,     3,     5,
       2,     1,     1,     1,     1,     1,     1,     1,     3,     4,
       4,     5,     1,     6,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     2,     2,
       2,     2,     2,     2,     3,     5
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = LSP_YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == LSP_YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use LSP_YYerror or LSP_YYUNDEF. */
#define YYERRCODE LSP_YYUNDEF


/* Enable debugging if requested.  */
#if LSP_YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !LSP_YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !LSP_YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


/* Context of a parse error.  */
typedef struct
{
  yy_state_t *yyssp;
  yysymbol_kind_t yytoken;
} yypcontext_t;

/* Put in YYARG at most YYARGN of the expected tokens given the
   current YYCTX, and return the number of tokens stored in YYARG.  If
   YYARG is null, return the number of expected tokens (guaranteed to
   be less than YYNTOKENS).  Return YYENOMEM on memory exhaustion.
   Return 0 if there are more than YYARGN expected tokens, yet fill
   YYARG up to YYARGN. */
static int
yypcontext_expected_tokens (const yypcontext_t *yyctx,
                            yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  int yyn = yypact[+*yyctx->yyssp];
  if (!yypact_value_is_default (yyn))
    {
      /* Start YYX at -YYN if negative to avoid negative indexes in
         YYCHECK.  In other words, skip the first -YYN actions for
         this state because they are default actions.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;
      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yyx;
      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
        if (yycheck[yyx + yyn] == yyx && yyx != YYSYMBOL_YYerror
            && !yytable_value_is_error (yytable[yyx + yyn]))
          {
            if (!yyarg)
              ++yycount;
            else if (yycount == yyargn)
              return 0;
            else
              yyarg[yycount++] = YY_CAST (yysymbol_kind_t, yyx);
          }
    }
  if (yyarg && yycount == 0 && 0 < yyargn)
    yyarg[0] = YYSYMBOL_YYEMPTY;
  return yycount;
}




#ifndef yystrlen
# if defined __GLIBC__ && defined _STRING_H
#  define yystrlen(S) (YY_CAST (YYPTRDIFF_T, strlen (S)))
# else
/* Return the length of YYSTR.  */
static YYPTRDIFF_T
yystrlen (const char *yystr)
{
  YYPTRDIFF_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
# endif
#endif

#ifndef yystpcpy
# if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#  define yystpcpy stpcpy
# else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
# endif
#endif

#ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYPTRDIFF_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYPTRDIFF_T yyn = 0;
      char const *yyp = yystr;
      for (;;)
        switch (*++yyp)
          {
          case '\'':
          case ',':
            goto do_not_strip_quotes;

          case '\\':
            if (*++yyp != '\\')
              goto do_not_strip_quotes;
            else
              goto append;

          append:
          default:
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (yyres)
    return yystpcpy (yyres, yystr) - yyres;
  else
    return yystrlen (yystr);
}
#endif


static int
yy_syntax_error_arguments (const yypcontext_t *yyctx,
                           yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yyctx->yytoken != YYSYMBOL_YYEMPTY)
    {
      int yyn;
      if (yyarg)
        yyarg[yycount] = yyctx->yytoken;
      ++yycount;
      yyn = yypcontext_expected_tokens (yyctx,
                                        yyarg ? yyarg + 1 : yyarg, yyargn - 1);
      if (yyn == YYENOMEM)
        return YYENOMEM;
      else
        yycount += yyn;
    }
  return yycount;
}

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return -1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return YYENOMEM if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYPTRDIFF_T *yymsg_alloc, char **yymsg,
                const yypcontext_t *yyctx)
{
  enum { YYARGS_MAX = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat: reported tokens (one for the "unexpected",
     one per "expected"). */
  yysymbol_kind_t yyarg[YYARGS_MAX];
  /* Cumulated lengths of YYARG.  */
  YYPTRDIFF_T yysize = 0;

  /* Actual size of YYARG. */
  int yycount = yy_syntax_error_arguments (yyctx, yyarg, YYARGS_MAX);
  if (yycount == YYENOMEM)
    return YYENOMEM;

  switch (yycount)
    {
#define YYCASE_(N, S)                       \
      case N:                               \
        yyformat = S;                       \
        break
    default: /* Avoid compiler warnings. */
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
    }

  /* Compute error message size.  Don't count the "%s"s, but reserve
     room for the terminator.  */
  yysize = yystrlen (yyformat) - 2 * yycount + 1;
  {
    int yyi;
    for (yyi = 0; yyi < yycount; ++yyi)
      {
        YYPTRDIFF_T yysize1
          = yysize + yytnamerr (YY_NULLPTR, yytname[yyarg[yyi]]);
        if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
          yysize = yysize1;
        else
          return YYENOMEM;
      }
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return -1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yytname[yyarg[yyi++]]);
          yyformat += 2;
        }
      else
        {
          ++yyp;
          ++yyformat;
        }
  }
  return 0;
}


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;

  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYPTRDIFF_T yymsg_alloc = sizeof yymsgbuf;

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = LSP_YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == LSP_YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= LSP_YYEOF)
    {
      yychar = LSP_YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == LSP_YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = LSP_YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = LSP_YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* program: %empty  */
#line 134 "parser_lsp.y"
                { root = make_program(); (yyval.program) = root; }
#line 2111 "parser_lsp.tab.c"
    break;

  case 3: /* program: program include_stmt  */
#line 135 "parser_lsp.y"
                                { (yyval.program) = (yyvsp[-1].program); }
#line 2117 "parser_lsp.tab.c"
    break;

  case 4: /* program: program ccpinclude_stmt  */
#line 136 "parser_lsp.y"
                                { (yyval.program) = (yyvsp[-1].program); }
#line 2123 "parser_lsp.tab.c"
    break;

  case 5: /* program: program class_decl  */
#line 137 "parser_lsp.y"
                         {
        (yyval.program) = (yyvsp[-1].program);
        (yyval.program)->declarations = realloc((yyval.program)->declarations, ((yyval.program)->decl_count+1)*sizeof(ASTNode*));
        (yyval.program)->declarations[(yyval.program)->decl_count++] = (ASTNode*)(yyvsp[0].class_node);
    }
#line 2133 "parser_lsp.tab.c"
    break;

  case 6: /* program: program func_decl  */
#line 142 "parser_lsp.y"
                        {
        (yyval.program) = (yyvsp[-1].program);
        (yyval.program)->declarations = realloc((yyval.program)->declarations, ((yyval.program)->decl_count+1)*sizeof(ASTNode*));
        (yyval.program)->declarations[(yyval.program)->decl_count++] = (ASTNode*)(yyvsp[0].func_node);
    }
#line 2143 "parser_lsp.tab.c"
    break;

  case 7: /* include_stmt: INCLUDE LBRACE include_list RBRACE  */
#line 150 "parser_lsp.y"
                                       {
        NodeList* l = (NodeList*)(yyvsp[-1].node_list);
        for (int i = 0; i < l->count; i++) {
            IdentifierNode* path = (IdentifierNode*)l->items[i];
            root->includes = realloc(root->includes, (root->include_count+1)*sizeof(char*));
            root->includes[root->include_count++] = path->name;
        }
        free(l->items); free(l);
    }
#line 2157 "parser_lsp.tab.c"
    break;

  case 8: /* include_stmt: INCLUDE LBRACE include_list RBRACE SEMICOLON  */
#line 159 "parser_lsp.y"
                                                   {
        NodeList* l = (NodeList*)(yyvsp[-2].node_list);
        for (int i = 0; i < l->count; i++) {
            IdentifierNode* path = (IdentifierNode*)l->items[i];
            root->includes = realloc(root->includes, (root->include_count+1)*sizeof(char*));
            root->includes[root->include_count++] = path->name;
        }
        free(l->items); free(l);
    }
#line 2171 "parser_lsp.tab.c"
    break;

  case 9: /* ccpinclude_stmt: CCPINCLUDE STRING_LITERAL SEMICOLON  */
#line 171 "parser_lsp.y"
                                        {
        /* strip surrounding quotes from the string literal */
        int len = strlen((yyvsp[-1].str));
        char *stripped = malloc(len - 1);
        strncpy(stripped, (yyvsp[-1].str) + 1, len - 2);
        stripped[len - 2] = '\0';
        root->cpp_includes = realloc(root->cpp_includes, (root->cpp_include_count+1)*sizeof(char*));
        root->cpp_includes[root->cpp_include_count++] = stripped;
        free((yyvsp[-1].str));
    }
#line 2186 "parser_lsp.tab.c"
    break;

  case 10: /* ccpinclude_stmt: CCPINCLUDE STRING_LITERAL  */
#line 181 "parser_lsp.y"
                                {
        int len = strlen((yyvsp[0].str));
        char *stripped = malloc(len - 1);
        strncpy(stripped, (yyvsp[0].str) + 1, len - 2);
        stripped[len - 2] = '\0';
        root->cpp_includes = realloc(root->cpp_includes, (root->cpp_include_count+1)*sizeof(char*));
        root->cpp_includes[root->cpp_include_count++] = stripped;
        free((yyvsp[0].str));
    }
#line 2200 "parser_lsp.tab.c"
    break;

  case 11: /* include_list: include_path  */
#line 193 "parser_lsp.y"
                                                { NodeList* l=list_new(); list_add(l,(yyvsp[0].node)); (yyval.node_list)=l; }
#line 2206 "parser_lsp.tab.c"
    break;

  case 12: /* include_list: include_list COMMA include_path  */
#line 194 "parser_lsp.y"
                                                { NodeList* l=(NodeList*)(yyvsp[-2].node_list); list_add(l,(yyvsp[0].node)); (yyval.node_list)=l; }
#line 2212 "parser_lsp.tab.c"
    break;

  case 13: /* include_list: include_list COMMA  */
#line 195 "parser_lsp.y"
                                                { (yyval.node_list) = (yyvsp[-1].node_list); }
#line 2218 "parser_lsp.tab.c"
    break;

  case 14: /* include_path: IDENTIFIER  */
#line 199 "parser_lsp.y"
               {
        /* build path string incrementally using an IdentifierNode to carry it */
        (yyval.node) = (ASTNode*)make_identifier((yyvsp[0].str));
    }
#line 2227 "parser_lsp.tab.c"
    break;

  case 15: /* include_path: include_path DOT IDENTIFIER  */
#line 203 "parser_lsp.y"
                                  {
        /* append .IDENTIFIER to the accumulated path string */
        IdentifierNode* prev = (IdentifierNode*)(yyvsp[-2].node);
        int len = strlen(prev->name) + 1 + strlen((yyvsp[0].str)) + 1;
        char* combined = malloc(len);
        snprintf(combined, len, "%s.%s", prev->name, (yyvsp[0].str));
        free(prev->name);
        prev->name = combined;
        (yyval.node) = (ASTNode*)prev;
    }
#line 2242 "parser_lsp.tab.c"
    break;

  case 16: /* class_decl: PUBLIC CLASS IDENTIFIER LBRACE class_body RBRACE  */
#line 216 "parser_lsp.y"
                                                     {
        (yyval.class_node) = make_class((yyvsp[-3].str), 1);
        NodeList* body = (NodeList*)(yyvsp[-1].node_list);
        for (int i = 0; i < body->count; i++) {
            ASTNode* m = body->items[i];
            if (m->type == NODE_FIELD) {
                (yyval.class_node)->fields = realloc((yyval.class_node)->fields, ((yyval.class_node)->field_count+1)*sizeof(FieldNode*));
                (yyval.class_node)->fields[(yyval.class_node)->field_count++] = (FieldNode*)m;
            } else if (m->type == NODE_METHOD) {
                MethodNode* mn = (MethodNode*)m;
                if (mn->return_type.name && strcmp(mn->return_type.name, "__ctor__") == 0) {
                    (yyval.class_node)->ctor_params = mn->params; (yyval.class_node)->ctor_param_count = mn->param_count;
                    (yyval.class_node)->ctor_body = mn->body; (yyval.class_node)->ctor_body_count = mn->body_count;
                    (yyval.class_node)->has_ctor = 1;
                } else {
                    (yyval.class_node)->methods = realloc((yyval.class_node)->methods, ((yyval.class_node)->method_count+1)*sizeof(MethodNode*));
                    (yyval.class_node)->methods[(yyval.class_node)->method_count++] = mn;
                }
            }
        }
        free(body->items); free(body);
    }
#line 2269 "parser_lsp.tab.c"
    break;

  case 17: /* class_decl: CLASS IDENTIFIER LBRACE class_body RBRACE  */
#line 238 "parser_lsp.y"
                                                {
        (yyval.class_node) = make_class((yyvsp[-3].str), 0);
        NodeList* body = (NodeList*)(yyvsp[-1].node_list);
        for (int i = 0; i < body->count; i++) {
            ASTNode* m = body->items[i];
            if (m->type == NODE_FIELD) {
                (yyval.class_node)->fields = realloc((yyval.class_node)->fields, ((yyval.class_node)->field_count+1)*sizeof(FieldNode*));
                (yyval.class_node)->fields[(yyval.class_node)->field_count++] = (FieldNode*)m;
            } else if (m->type == NODE_METHOD) {
                MethodNode* mn = (MethodNode*)m;
                if (mn->return_type.name && strcmp(mn->return_type.name, "__ctor__") == 0) {
                    (yyval.class_node)->ctor_params = mn->params; (yyval.class_node)->ctor_param_count = mn->param_count;
                    (yyval.class_node)->ctor_body = mn->body; (yyval.class_node)->ctor_body_count = mn->body_count;
                    (yyval.class_node)->has_ctor = 1;
                } else {
                    (yyval.class_node)->methods = realloc((yyval.class_node)->methods, ((yyval.class_node)->method_count+1)*sizeof(MethodNode*));
                    (yyval.class_node)->methods[(yyval.class_node)->method_count++] = mn;
                }
            }
        }
        free(body->items); free(body);
    }
#line 2296 "parser_lsp.tab.c"
    break;

  case 18: /* class_decl: PUBLIC CLASS IDENTIFIER LBRACE RBRACE  */
#line 260 "parser_lsp.y"
                                            { (yyval.class_node) = make_class((yyvsp[-2].str), 1); }
#line 2302 "parser_lsp.tab.c"
    break;

  case 19: /* class_decl: CLASS IDENTIFIER LBRACE RBRACE  */
#line 261 "parser_lsp.y"
                                            { (yyval.class_node) = make_class((yyvsp[-2].str), 0); }
#line 2308 "parser_lsp.tab.c"
    break;

  case 20: /* class_decl: PUBLIC CLASS IDENTIFIER LBRACE IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE class_body RBRACE  */
#line 263 "parser_lsp.y"
                                                                                                                   {
        (yyval.class_node) = make_class((yyvsp[-10].str), 1);
        if ((yyvsp[-6].node_list)) {
            NodeList* pl = (NodeList*)(yyvsp[-6].node_list);
            (yyval.class_node)->ctor_params = pl->items; (yyval.class_node)->ctor_param_count = pl->count; free(pl);
        }
        NodeList* cb = (NodeList*)(yyvsp[-3].node_list);
        (yyval.class_node)->ctor_body = cb->items; (yyval.class_node)->ctor_body_count = cb->count; free(cb);
        (yyval.class_node)->has_ctor = 1;
        NodeList* body = (NodeList*)(yyvsp[-1].node_list);
        for (int i = 0; i < body->count; i++) {
            ASTNode* m = body->items[i];
            if (m->type == NODE_FIELD) {
                (yyval.class_node)->fields = realloc((yyval.class_node)->fields, ((yyval.class_node)->field_count+1)*sizeof(FieldNode*));
                (yyval.class_node)->fields[(yyval.class_node)->field_count++] = (FieldNode*)m;
            } else if (m->type == NODE_METHOD) {
                (yyval.class_node)->methods = realloc((yyval.class_node)->methods, ((yyval.class_node)->method_count+1)*sizeof(MethodNode*));
                (yyval.class_node)->methods[(yyval.class_node)->method_count++] = (MethodNode*)m;
            }
        }
        free(body->items); free(body);
    }
#line 2335 "parser_lsp.tab.c"
    break;

  case 21: /* class_decl: CLASS IDENTIFIER LBRACE IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE class_body RBRACE  */
#line 285 "parser_lsp.y"
                                                                                                            {
        (yyval.class_node) = make_class((yyvsp[-10].str), 0);
        if ((yyvsp[-6].node_list)) {
            NodeList* pl = (NodeList*)(yyvsp[-6].node_list);
            (yyval.class_node)->ctor_params = pl->items; (yyval.class_node)->ctor_param_count = pl->count; free(pl);
        }
        NodeList* cb = (NodeList*)(yyvsp[-3].node_list);
        (yyval.class_node)->ctor_body = cb->items; (yyval.class_node)->ctor_body_count = cb->count; free(cb);
        (yyval.class_node)->has_ctor = 1;
        NodeList* body = (NodeList*)(yyvsp[-1].node_list);
        for (int i = 0; i < body->count; i++) {
            ASTNode* m = body->items[i];
            if (m->type == NODE_FIELD) {
                (yyval.class_node)->fields = realloc((yyval.class_node)->fields, ((yyval.class_node)->field_count+1)*sizeof(FieldNode*));
                (yyval.class_node)->fields[(yyval.class_node)->field_count++] = (FieldNode*)m;
            } else if (m->type == NODE_METHOD) {
                (yyval.class_node)->methods = realloc((yyval.class_node)->methods, ((yyval.class_node)->method_count+1)*sizeof(MethodNode*));
                (yyval.class_node)->methods[(yyval.class_node)->method_count++] = (MethodNode*)m;
            }
        }
        free(body->items); free(body);
    }
#line 2362 "parser_lsp.tab.c"
    break;

  case 22: /* func_decl: type IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE  */
#line 310 "parser_lsp.y"
                                                                     {
        (yyval.func_node) = make_func((yyvsp[-7].type_node), (yyvsp[-6].str));
        if ((yyvsp[-4].node_list)) { NodeList* pl=(NodeList*)(yyvsp[-4].node_list); (yyval.func_node)->params=pl->items; (yyval.func_node)->param_count=pl->count; free(pl); }
        NodeList* sl=(NodeList*)(yyvsp[-1].node_list); (yyval.func_node)->body=sl->items; (yyval.func_node)->body_count=sl->count; free(sl);
    }
#line 2372 "parser_lsp.tab.c"
    break;

  case 23: /* func_decl: type QUESTION IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE  */
#line 315 "parser_lsp.y"
                                                                                {
        Type t=(yyvsp[-8].type_node); t.nullable=1;
        (yyval.func_node) = make_func(t, (yyvsp[-6].str));
        if ((yyvsp[-4].node_list)) { NodeList* pl=(NodeList*)(yyvsp[-4].node_list); (yyval.func_node)->params=pl->items; (yyval.func_node)->param_count=pl->count; free(pl); }
        NodeList* sl=(NodeList*)(yyvsp[-1].node_list); (yyval.func_node)->body=sl->items; (yyval.func_node)->body_count=sl->count; free(sl);
    }
#line 2383 "parser_lsp.tab.c"
    break;

  case 24: /* func_decl: error LBRACE stmt_list RBRACE  */
#line 322 "parser_lsp.y"
                                    {
        (yyval.func_node) = make_func(make_simple_type("void", 0), "<error>");
        yyerrok;
    }
#line 2392 "parser_lsp.tab.c"
    break;

  case 25: /* class_body: member_decl  */
#line 329 "parser_lsp.y"
                { NodeList* l=list_new(); if ((yyvsp[0].node)) list_add(l,(yyvsp[0].node)); (yyval.node_list)=l; }
#line 2398 "parser_lsp.tab.c"
    break;

  case 26: /* class_body: class_body member_decl  */
#line 330 "parser_lsp.y"
                             { NodeList* l=(NodeList*)(yyvsp[-1].node_list); if ((yyvsp[0].node)) list_add(l,(yyvsp[0].node)); (yyval.node_list)=l; }
#line 2404 "parser_lsp.tab.c"
    break;

  case 27: /* member_decl: field_decl  */
#line 334 "parser_lsp.y"
                { (yyval.node) = (ASTNode*)(yyvsp[0].field_node); }
#line 2410 "parser_lsp.tab.c"
    break;

  case 28: /* member_decl: method_decl  */
#line 335 "parser_lsp.y"
                  { (yyval.node) = (ASTNode*)(yyvsp[0].method_node); }
#line 2416 "parser_lsp.tab.c"
    break;

  case 29: /* member_decl: ctor_decl  */
#line 336 "parser_lsp.y"
                { (yyval.node) = (ASTNode*)(yyvsp[0].node); }
#line 2422 "parser_lsp.tab.c"
    break;

  case 30: /* member_decl: error SEMICOLON  */
#line 338 "parser_lsp.y"
                      { (yyval.node) = NULL; yyerrok; }
#line 2428 "parser_lsp.tab.c"
    break;

  case 31: /* ctor_decl: IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE  */
#line 342 "parser_lsp.y"
                                                                {
        Type void_t; memset(&void_t, 0, sizeof(void_t)); void_t.kind = TYPE_SIMPLE; void_t.name = strdup("__ctor__");
        MethodNode* mn = make_method(void_t, (yyvsp[-6].str));
        if ((yyvsp[-4].node_list)) { NodeList* pl=(NodeList*)(yyvsp[-4].node_list); mn->params=pl->items; mn->param_count=pl->count; free(pl); }
        NodeList* sl=(NodeList*)(yyvsp[-1].node_list); mn->body=sl->items; mn->body_count=sl->count; free(sl);
        (yyval.node) = (ASTNode*)mn;
    }
#line 2440 "parser_lsp.tab.c"
    break;

  case 32: /* field_decl: PRIVATE type IDENTIFIER SEMICOLON  */
#line 352 "parser_lsp.y"
                                      { (yyval.field_node) = make_field((yyvsp[-2].type_node), (yyvsp[-1].str), 0); }
#line 2446 "parser_lsp.tab.c"
    break;

  case 33: /* field_decl: PUBLIC type IDENTIFIER SEMICOLON  */
#line 353 "parser_lsp.y"
                                        { (yyval.field_node) = make_field((yyvsp[-2].type_node), (yyvsp[-1].str), 1); }
#line 2452 "parser_lsp.tab.c"
    break;

  case 34: /* field_decl: type IDENTIFIER SEMICOLON  */
#line 354 "parser_lsp.y"
                                        { (yyval.field_node) = make_field((yyvsp[-2].type_node), (yyvsp[-1].str), 0); }
#line 2458 "parser_lsp.tab.c"
    break;

  case 35: /* method_decl: type IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE  */
#line 358 "parser_lsp.y"
                                                                     {
        (yyval.method_node) = make_method((yyvsp[-7].type_node), (yyvsp[-6].str));
        if ((yyvsp[-4].node_list)) { NodeList* pl=(NodeList*)(yyvsp[-4].node_list); (yyval.method_node)->params=pl->items; (yyval.method_node)->param_count=pl->count; free(pl); }
        NodeList* sl=(NodeList*)(yyvsp[-1].node_list); (yyval.method_node)->body=sl->items; (yyval.method_node)->body_count=sl->count; free(sl);
    }
#line 2468 "parser_lsp.tab.c"
    break;

  case 36: /* method_decl: type QUESTION IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE  */
#line 363 "parser_lsp.y"
                                                                                {
        Type t=(yyvsp[-8].type_node); t.nullable=1;
        (yyval.method_node) = make_method(t, (yyvsp[-6].str));
        if ((yyvsp[-4].node_list)) { NodeList* pl=(NodeList*)(yyvsp[-4].node_list); (yyval.method_node)->params=pl->items; (yyval.method_node)->param_count=pl->count; free(pl); }
        NodeList* sl=(NodeList*)(yyvsp[-1].node_list); (yyval.method_node)->body=sl->items; (yyval.method_node)->body_count=sl->count; free(sl);
    }
#line 2479 "parser_lsp.tab.c"
    break;

  case 37: /* param_list: %empty  */
#line 372 "parser_lsp.y"
                { (yyval.node_list) = NULL; }
#line 2485 "parser_lsp.tab.c"
    break;

  case 38: /* param_list: params  */
#line 373 "parser_lsp.y"
                { (yyval.node_list) = (yyvsp[0].node_list); }
#line 2491 "parser_lsp.tab.c"
    break;

  case 39: /* params: type IDENTIFIER  */
#line 377 "parser_lsp.y"
                    {
        NodeList* l=list_new();
        list_add(l,(ASTNode*)make_var_decl((yyvsp[-1].type_node),(yyvsp[0].str),NULL));
        (yyval.node_list)=l;
    }
#line 2501 "parser_lsp.tab.c"
    break;

  case 40: /* params: params COMMA type IDENTIFIER  */
#line 382 "parser_lsp.y"
                                   {
        NodeList* l=(NodeList*)(yyvsp[-3].node_list);
        list_add(l,(ASTNode*)make_var_decl((yyvsp[-1].type_node),(yyvsp[0].str),NULL));
        (yyval.node_list)=l;
    }
#line 2511 "parser_lsp.tab.c"
    break;

  case 41: /* arg_list: %empty  */
#line 390 "parser_lsp.y"
                { (yyval.node_list) = list_new(); }
#line 2517 "parser_lsp.tab.c"
    break;

  case 42: /* arg_list: args  */
#line 391 "parser_lsp.y"
                { (yyval.node_list) = (yyvsp[0].node_list); }
#line 2523 "parser_lsp.tab.c"
    break;

  case 43: /* args: expr  */
#line 395 "parser_lsp.y"
         { NodeList* l=list_new(); list_add(l,(yyvsp[0].node)); (yyval.node_list)=l; }
#line 2529 "parser_lsp.tab.c"
    break;

  case 44: /* args: args COMMA expr  */
#line 396 "parser_lsp.y"
                      { NodeList* l=(NodeList*)(yyvsp[-2].node_list); list_add(l,(yyvsp[0].node)); (yyval.node_list)=l; }
#line 2535 "parser_lsp.tab.c"
    break;

  case 45: /* nullable_type: type  */
#line 401 "parser_lsp.y"
                      { (yyval.type_node) = (yyvsp[0].type_node); }
#line 2541 "parser_lsp.tab.c"
    break;

  case 46: /* nullable_type: type QUESTION  */
#line 402 "parser_lsp.y"
                      { Type t = (yyvsp[-1].type_node); t.nullable = 1; (yyval.type_node) = t; }
#line 2547 "parser_lsp.tab.c"
    break;

  case 47: /* tuple_type_items: nullable_type COMMA nullable_type  */
#line 407 "parser_lsp.y"
                                      {
        TypeList* tl = typelist_new();
        typelist_add(tl, (yyvsp[-2].type_node));
        typelist_add(tl, (yyvsp[0].type_node));
        (yyval.node_list) = (void*)tl;
    }
#line 2558 "parser_lsp.tab.c"
    break;

  case 48: /* tuple_type_items: tuple_type_items COMMA nullable_type  */
#line 413 "parser_lsp.y"
                                           {
        TypeList* tl = (TypeList*)(yyvsp[-2].node_list);
        typelist_add(tl, (yyvsp[0].type_node));
        (yyval.node_list) = (void*)tl;
    }
#line 2568 "parser_lsp.tab.c"
    break;

  case 49: /* union_types: type  */
#line 423 "parser_lsp.y"
         {
        TypeList* tl = typelist_new();
        typelist_add(tl, (yyvsp[0].type_node));
        (yyval.node_list) = (void*)tl;
    }
#line 2578 "parser_lsp.tab.c"
    break;

  case 50: /* union_types: union_types PIPE type  */
#line 428 "parser_lsp.y"
                            {
        TypeList* tl = (TypeList*)(yyvsp[-2].node_list);
        typelist_add(tl, (yyvsp[0].type_node));
        (yyval.node_list) = (void*)tl;
    }
#line 2588 "parser_lsp.tab.c"
    break;

  case 51: /* type: INT  */
#line 436 "parser_lsp.y"
               { (yyval.type_node) = make_simple_type("int",   0); }
#line 2594 "parser_lsp.tab.c"
    break;

  case 52: /* type: STRING  */
#line 437 "parser_lsp.y"
               { (yyval.type_node) = make_simple_type("str",   0); }
#line 2600 "parser_lsp.tab.c"
    break;

  case 53: /* type: ERROR  */
#line 438 "parser_lsp.y"
               { (yyval.type_node) = make_simple_type("Error", 0); }
#line 2606 "parser_lsp.tab.c"
    break;

  case 54: /* type: VOID  */
#line 439 "parser_lsp.y"
               { (yyval.type_node) = make_simple_type("void",  0); }
#line 2612 "parser_lsp.tab.c"
    break;

  case 55: /* type: BOOL  */
#line 440 "parser_lsp.y"
               { (yyval.type_node) = make_simple_type("bool",  0); }
#line 2618 "parser_lsp.tab.c"
    break;

  case 56: /* type: IDENTIFIER  */
#line 441 "parser_lsp.y"
                 { (yyval.type_node) = make_simple_type((yyvsp[0].str),    0); free((yyvsp[0].str)); }
#line 2624 "parser_lsp.tab.c"
    break;

  case 57: /* type: ARRAY LT type GT  */
#line 443 "parser_lsp.y"
                                            { (yyval.type_node) = make_array_type((yyvsp[-1].type_node), 0); }
#line 2630 "parser_lsp.tab.c"
    break;

  case 58: /* type: ARRAY LT type COMMA NUMBER GT  */
#line 445 "parser_lsp.y"
                                            { (yyval.type_node) = make_array_type((yyvsp[-3].type_node), (yyvsp[-1].num)); }
#line 2636 "parser_lsp.tab.c"
    break;

  case 59: /* type: MULTI LT ANY_KW GT  */
#line 447 "parser_lsp.y"
                                            { (yyval.type_node) = make_multi_type(NULL, 0, 1, 0); }
#line 2642 "parser_lsp.tab.c"
    break;

  case 60: /* type: MULTI LT ANY_KW COMMA NUMBER GT  */
#line 449 "parser_lsp.y"
                                            { (yyval.type_node) = make_multi_type(NULL, 0, 1, (yyvsp[-1].num)); }
#line 2648 "parser_lsp.tab.c"
    break;

  case 61: /* type: MULTI LT union_types GT  */
#line 451 "parser_lsp.y"
                              {
        TypeList* tl = (TypeList*)(yyvsp[-1].node_list);
        (yyval.type_node) = make_multi_type(tl->items, tl->count, 0, 0);
        free(tl->items); free(tl);
    }
#line 2658 "parser_lsp.tab.c"
    break;

  case 62: /* type: MULTI LT union_types COMMA NUMBER GT  */
#line 457 "parser_lsp.y"
                                           {
        TypeList* tl = (TypeList*)(yyvsp[-3].node_list);
        (yyval.type_node) = make_multi_type(tl->items, tl->count, 0, (yyvsp[-1].num));
        free(tl->items); free(tl);
    }
#line 2668 "parser_lsp.tab.c"
    break;

  case 63: /* type: LPAREN tuple_type_items RPAREN  */
#line 463 "parser_lsp.y"
                                     {
        TypeList* tl = (TypeList*)(yyvsp[-1].node_list);
        (yyval.type_node) = make_tuple_type(tl->items, tl->count);
        free(tl->items); free(tl);
    }
#line 2678 "parser_lsp.tab.c"
    break;

  case 64: /* stmt_list: %empty  */
#line 471 "parser_lsp.y"
                { (yyval.node_list) = list_new(); }
#line 2684 "parser_lsp.tab.c"
    break;

  case 65: /* stmt_list: stmt_list stmt  */
#line 472 "parser_lsp.y"
                     {
        NodeList* l=(NodeList*)(yyvsp[-1].node_list);
        if ((yyvsp[0].node)) list_add(l,(yyvsp[0].node));
        (yyval.node_list)=l;
    }
#line 2694 "parser_lsp.tab.c"
    break;

  case 68: /* stmt: error SEMICOLON  */
#line 483 "parser_lsp.y"
                      { (yyval.node) = NULL; yyerrok; }
#line 2700 "parser_lsp.tab.c"
    break;

  case 69: /* stmt: BREAK SEMICOLON  */
#line 484 "parser_lsp.y"
                         { (yyval.node) = (ASTNode*)make_break(); SET_LINE((yyval.node)); }
#line 2706 "parser_lsp.tab.c"
    break;

  case 70: /* stmt: CONTINUE SEMICOLON  */
#line 485 "parser_lsp.y"
                         { (yyval.node) = (ASTNode*)make_continue(); SET_LINE((yyval.node)); }
#line 2712 "parser_lsp.tab.c"
    break;

  case 71: /* stmt: ASM_BLOCK  */
#line 486 "parser_lsp.y"
                {
        AsmBlockNode *ab = malloc(sizeof(AsmBlockNode));
        ab->base.type = NODE_ASM_BLOCK;
        ab->base.resolved_type.kind = TYPE_SIMPLE;
        ab->base.resolved_type.nullable = 0;
        ab->base.resolved_type.name = NULL;
        ab->base.resolved_type.elem_types = NULL;
        ab->base.resolved_type.elem_type_count = 0;
        ab->base.resolved_type.is_any = 0;
        ab->base.resolved_type.fixed_size = 0;
        ab->body = (yyvsp[0].str);
        (yyval.node) = (ASTNode*)ab;
    }
#line 2730 "parser_lsp.tab.c"
    break;

  case 72: /* stmt: DEFER expr SEMICOLON  */
#line 499 "parser_lsp.y"
                           { (yyval.node) = (ASTNode*)make_defer((yyvsp[-1].node)); SET_LINE((yyval.node)); }
#line 2736 "parser_lsp.tab.c"
    break;

  case 73: /* stmt: expr SEMICOLON  */
#line 500 "parser_lsp.y"
                     { (yyval.node) = (yyvsp[-1].node); }
#line 2742 "parser_lsp.tab.c"
    break;

  case 74: /* stmt: IDENTIFIER DECLARE_ASSIGN expr SEMICOLON  */
#line 502 "parser_lsp.y"
                                               {
        (yyval.node) = (ASTNode*)make_var_decl(make_simple_type("auto", 0), (yyvsp[-3].str), (yyvsp[-1].node)); SET_LINE((yyval.node));
    }
#line 2750 "parser_lsp.tab.c"
    break;

  case 75: /* stmt: IDENTIFIER ASSIGN expr SEMICOLON  */
#line 506 "parser_lsp.y"
                                       {
        (yyval.node) = (ASTNode*)make_assign((yyvsp[-3].str), (yyvsp[-1].node)); SET_LINE((yyval.node));
    }
#line 2758 "parser_lsp.tab.c"
    break;

  case 76: /* stmt: IDENTIFIER PLUS_ASSIGN expr SEMICOLON  */
#line 510 "parser_lsp.y"
                                             { (yyval.node) = (ASTNode*)make_compound_assign("+=", (yyvsp[-3].str), (yyvsp[-1].node)); SET_LINE((yyval.node)); }
#line 2764 "parser_lsp.tab.c"
    break;

  case 77: /* stmt: IDENTIFIER MINUS_ASSIGN expr SEMICOLON  */
#line 511 "parser_lsp.y"
                                             { (yyval.node) = (ASTNode*)make_compound_assign("-=", (yyvsp[-3].str), (yyvsp[-1].node)); SET_LINE((yyval.node)); }
#line 2770 "parser_lsp.tab.c"
    break;

  case 78: /* stmt: IDENTIFIER STAR_ASSIGN expr SEMICOLON  */
#line 512 "parser_lsp.y"
                                             { (yyval.node) = (ASTNode*)make_compound_assign("*=", (yyvsp[-3].str), (yyvsp[-1].node)); SET_LINE((yyval.node)); }
#line 2776 "parser_lsp.tab.c"
    break;

  case 79: /* stmt: IDENTIFIER SLASH_ASSIGN expr SEMICOLON  */
#line 513 "parser_lsp.y"
                                             { (yyval.node) = (ASTNode*)make_compound_assign("/=", (yyvsp[-3].str), (yyvsp[-1].node)); SET_LINE((yyval.node)); }
#line 2782 "parser_lsp.tab.c"
    break;

  case 80: /* stmt: IDENTIFIER MOD_ASSIGN expr SEMICOLON  */
#line 514 "parser_lsp.y"
                                             { (yyval.node) = (ASTNode*)make_compound_assign("%=", (yyvsp[-3].str), (yyvsp[-1].node)); SET_LINE((yyval.node)); }
#line 2788 "parser_lsp.tab.c"
    break;

  case 81: /* stmt: expr DOT IDENTIFIER ASSIGN expr SEMICOLON  */
#line 516 "parser_lsp.y"
                                                {
        (yyval.node) = (ASTNode*)make_member_assign((yyvsp[-5].node), (yyvsp[-3].str), (yyvsp[-1].node)); SET_LINE((yyval.node));
    }
#line 2796 "parser_lsp.tab.c"
    break;

  case 82: /* stmt: expr LBRACKET expr RBRACKET ASSIGN expr SEMICOLON  */
#line 520 "parser_lsp.y"
                                                        {
        (yyval.node) = (ASTNode*)make_index_assign((yyvsp[-6].node), (yyvsp[-4].node), (yyvsp[-1].node)); SET_LINE((yyval.node));
    }
#line 2804 "parser_lsp.tab.c"
    break;

  case 83: /* stmt: IF LPAREN expr RPAREN LBRACE stmt_list RBRACE  */
#line 524 "parser_lsp.y"
                                                    {
        IfNode* ifn = make_if((yyvsp[-4].node));
        NodeList* ts=(NodeList*)(yyvsp[-1].node_list); ifn->then_body=ts->items; ifn->then_count=ts->count; free(ts);
        (yyval.node) = (ASTNode*)ifn; SET_LINE((yyval.node));
    }
#line 2814 "parser_lsp.tab.c"
    break;

  case 84: /* stmt: IF LPAREN expr RPAREN LBRACE stmt_list RBRACE ELSE LBRACE stmt_list RBRACE  */
#line 529 "parser_lsp.y"
                                                                                 {
        IfNode* ifn = make_if((yyvsp[-8].node));
        NodeList* ts=(NodeList*)(yyvsp[-5].node_list); NodeList* es=(NodeList*)(yyvsp[-1].node_list);
        ifn->then_body=ts->items; ifn->then_count=ts->count;
        ifn->else_body=es->items; ifn->else_count=es->count;
        free(ts); free(es);
        (yyval.node) = (ASTNode*)ifn; SET_LINE((yyval.node));
    }
#line 2827 "parser_lsp.tab.c"
    break;

  case 85: /* stmt: WHILE LPAREN expr RPAREN LBRACE stmt_list RBRACE  */
#line 538 "parser_lsp.y"
                                                       {
        WhileNode* wn = make_while((yyvsp[-4].node));
        NodeList* sl=(NodeList*)(yyvsp[-1].node_list); wn->body=sl->items; wn->body_count=sl->count; free(sl);
        (yyval.node) = (ASTNode*)wn; SET_LINE((yyval.node));
    }
#line 2837 "parser_lsp.tab.c"
    break;

  case 86: /* stmt: FOR LPAREN for_init SEMICOLON expr SEMICOLON for_init RPAREN LBRACE stmt_list RBRACE  */
#line 544 "parser_lsp.y"
                                                                                           {
        ForNode* fn = make_for((yyvsp[-8].node), (yyvsp[-6].node), (yyvsp[-4].node));
        NodeList* sl=(NodeList*)(yyvsp[-1].node_list); fn->body=sl->items; fn->body_count=sl->count; free(sl);
        (yyval.node) = (ASTNode*)fn; SET_LINE((yyval.node));
    }
#line 2847 "parser_lsp.tab.c"
    break;

  case 87: /* stmt: FOR LPAREN for_init SEMICOLON expr SEMICOLON RPAREN LBRACE stmt_list RBRACE  */
#line 549 "parser_lsp.y"
                                                                                  {
        ForNode* fn = make_for((yyvsp[-7].node), (yyvsp[-5].node), NULL);
        NodeList* sl=(NodeList*)(yyvsp[-1].node_list); fn->body=sl->items; fn->body_count=sl->count; free(sl);
        (yyval.node) = (ASTNode*)fn; SET_LINE((yyval.node));
    }
#line 2857 "parser_lsp.tab.c"
    break;

  case 88: /* stmt: FOR LPAREN IDENTIFIER IN expr RPAREN LBRACE stmt_list RBRACE  */
#line 555 "parser_lsp.y"
                                                                   {
        ForInNode* fn = make_for_in((yyvsp[-6].str), 0, (yyvsp[-4].node));
        NodeList* sl=(NodeList*)(yyvsp[-1].node_list); fn->body=sl->items; fn->body_count=sl->count; free(sl);
        free((yyvsp[-6].str));
        (yyval.node) = (ASTNode*)fn; SET_LINE((yyval.node));
    }
#line 2868 "parser_lsp.tab.c"
    break;

  case 89: /* stmt: FOR LPAREN AMP IDENTIFIER IN expr RPAREN LBRACE stmt_list RBRACE  */
#line 562 "parser_lsp.y"
                                                                       {
        ForInNode* fn = make_for_in((yyvsp[-6].str), 1, (yyvsp[-4].node));
        NodeList* sl=(NodeList*)(yyvsp[-1].node_list); fn->body=sl->items; fn->body_count=sl->count; free(sl);
        free((yyvsp[-6].str));
        (yyval.node) = (ASTNode*)fn; SET_LINE((yyval.node));
    }
#line 2879 "parser_lsp.tab.c"
    break;

  case 90: /* for_init: type IDENTIFIER ASSIGN expr  */
#line 571 "parser_lsp.y"
                                { (yyval.node) = (ASTNode*)make_var_decl((yyvsp[-3].type_node), (yyvsp[-2].str), (yyvsp[0].node)); }
#line 2885 "parser_lsp.tab.c"
    break;

  case 91: /* for_init: type IDENTIFIER  */
#line 572 "parser_lsp.y"
                                { (yyval.node) = (ASTNode*)make_var_decl((yyvsp[-1].type_node), (yyvsp[0].str), NULL); }
#line 2891 "parser_lsp.tab.c"
    break;

  case 92: /* for_init: IDENTIFIER ASSIGN expr  */
#line 573 "parser_lsp.y"
                                { (yyval.node) = (ASTNode*)make_assign((yyvsp[-2].str), (yyvsp[0].node)); }
#line 2897 "parser_lsp.tab.c"
    break;

  case 93: /* for_init: %empty  */
#line 574 "parser_lsp.y"
                                { (yyval.node) = NULL; }
#line 2903 "parser_lsp.tab.c"
    break;

  case 94: /* var_decl: type IDENTIFIER ASSIGN expr SEMICOLON  */
#line 578 "parser_lsp.y"
                                          { (yyval.node) = (ASTNode*)make_var_decl((yyvsp[-4].type_node), (yyvsp[-3].str), (yyvsp[-1].node)); SET_LINE((yyval.node)); }
#line 2909 "parser_lsp.tab.c"
    break;

  case 95: /* var_decl: type IDENTIFIER SEMICOLON  */
#line 579 "parser_lsp.y"
                                          { (yyval.node) = (ASTNode*)make_var_decl((yyvsp[-2].type_node), (yyvsp[-1].str), NULL); SET_LINE((yyval.node)); }
#line 2915 "parser_lsp.tab.c"
    break;

  case 96: /* var_decl: type QUESTION IDENTIFIER ASSIGN expr SEMICOLON  */
#line 580 "parser_lsp.y"
                                                     {
        Type t = (yyvsp[-5].type_node); t.nullable = 1;
        (yyval.node) = (ASTNode*)make_var_decl(t, (yyvsp[-3].str), (yyvsp[-1].node)); SET_LINE((yyval.node));
    }
#line 2924 "parser_lsp.tab.c"
    break;

  case 97: /* var_decl: type QUESTION IDENTIFIER SEMICOLON  */
#line 584 "parser_lsp.y"
                                         {
        Type t = (yyvsp[-3].type_node); t.nullable = 1;
        (yyval.node) = (ASTNode*)make_var_decl(t, (yyvsp[-1].str), NULL); SET_LINE((yyval.node));
    }
#line 2933 "parser_lsp.tab.c"
    break;

  case 98: /* return_stmt: RETURN expr SEMICOLON  */
#line 591 "parser_lsp.y"
                          { (yyval.node) = (ASTNode*)make_return((yyvsp[-1].node)); SET_LINE((yyval.node)); }
#line 2939 "parser_lsp.tab.c"
    break;

  case 99: /* return_stmt: RETURN expr COMMA args SEMICOLON  */
#line 592 "parser_lsp.y"
                                       {
        NodeList* al = (NodeList*)(yyvsp[-1].node_list);
        int total = 1 + al->count;
        ASTNode **elems = malloc(total * sizeof(ASTNode*));
        elems[0] = (yyvsp[-3].node);
        for (int i = 0; i < al->count; i++) elems[i+1] = al->items[i];
        free(al->items); free(al);
        ASTNode* tup = (ASTNode*)make_tuple(elems, total);
        free(elems);
        (yyval.node) = (ASTNode*)make_return(tup); SET_LINE((yyval.node));
    }
#line 2955 "parser_lsp.tab.c"
    break;

  case 100: /* return_stmt: RETURN SEMICOLON  */
#line 603 "parser_lsp.y"
                          { (yyval.node) = (ASTNode*)make_return(NULL); SET_LINE((yyval.node)); }
#line 2961 "parser_lsp.tab.c"
    break;

  case 101: /* expr: NUMBER  */
#line 607 "parser_lsp.y"
           {
        char buf[32]; sprintf(buf, "%d", (yyvsp[0].num));
        (yyval.node) = (ASTNode*)make_literal(buf, LIT_INT); SET_LINE((yyval.node));
    }
#line 2970 "parser_lsp.tab.c"
    break;

  case 102: /* expr: FLOAT_LITERAL  */
#line 611 "parser_lsp.y"
                    { (yyval.node) = (ASTNode*)make_literal((yyvsp[0].str), LIT_FLOAT); SET_LINE((yyval.node)); }
#line 2976 "parser_lsp.tab.c"
    break;

  case 103: /* expr: STRING_LITERAL  */
#line 612 "parser_lsp.y"
                     { (yyval.node) = (ASTNode*)make_literal((yyvsp[0].str), LIT_STRING); SET_LINE((yyval.node)); }
#line 2982 "parser_lsp.tab.c"
    break;

  case 104: /* expr: INTERP_STRING  */
#line 613 "parser_lsp.y"
                     { (yyval.node) = (ASTNode*)make_interp_string((yyvsp[0].str)); SET_LINE((yyval.node)); }
#line 2988 "parser_lsp.tab.c"
    break;

  case 105: /* expr: NIL  */
#line 614 "parser_lsp.y"
             { (yyval.node) = (ASTNode*)make_literal("nullptr", LIT_NIL); SET_LINE((yyval.node)); }
#line 2994 "parser_lsp.tab.c"
    break;

  case 106: /* expr: TRUE_LIT  */
#line 615 "parser_lsp.y"
                { (yyval.node) = (ASTNode*)make_literal("true", LIT_BOOL); SET_LINE((yyval.node)); }
#line 3000 "parser_lsp.tab.c"
    break;

  case 107: /* expr: FALSE_LIT  */
#line 616 "parser_lsp.y"
                { (yyval.node) = (ASTNode*)make_literal("false", LIT_BOOL); SET_LINE((yyval.node)); }
#line 3006 "parser_lsp.tab.c"
    break;

  case 108: /* expr: LBRACKET arg_list RBRACKET  */
#line 618 "parser_lsp.y"
                                 {
        NodeList* al = (NodeList*)(yyvsp[-1].node_list);
        (yyval.node) = (ASTNode*)make_array_literal(al->items, al->count);
        free(al->items); free(al); SET_LINE((yyval.node));
    }
#line 3016 "parser_lsp.tab.c"
    break;

  case 109: /* expr: expr LBRACKET expr RBRACKET  */
#line 624 "parser_lsp.y"
                                  { (yyval.node) = (ASTNode*)make_index((yyvsp[-3].node), (yyvsp[-1].node)); SET_LINE((yyval.node)); }
#line 3022 "parser_lsp.tab.c"
    break;

  case 110: /* expr: IDENTIFIER LPAREN arg_list RPAREN  */
#line 626 "parser_lsp.y"
                                        {
        FuncCallNode* c = make_func_call((yyvsp[-3].str));
        NodeList* al=(NodeList*)(yyvsp[-1].node_list); c->args=al->items; c->arg_count=al->count; free(al);
        (yyval.node) = (ASTNode*)c; SET_LINE((yyval.node));
    }
#line 3032 "parser_lsp.tab.c"
    break;

  case 111: /* expr: NEW IDENTIFIER LPAREN arg_list RPAREN  */
#line 632 "parser_lsp.y"
                                            {
        NewNode* n = make_new((yyvsp[-3].str));
        NodeList* al=(NodeList*)(yyvsp[-1].node_list); n->args=al->items; n->arg_count=al->count; free(al);
        (yyval.node) = (ASTNode*)n; SET_LINE((yyval.node));
    }
#line 3042 "parser_lsp.tab.c"
    break;

  case 112: /* expr: IDENTIFIER  */
#line 637 "parser_lsp.y"
                 { (yyval.node) = (ASTNode*)make_identifier((yyvsp[0].str)); SET_LINE((yyval.node)); }
#line 3048 "parser_lsp.tab.c"
    break;

  case 113: /* expr: expr DOT IDENTIFIER LPAREN arg_list RPAREN  */
#line 639 "parser_lsp.y"
                                                 {
        MethodCallNode* c = make_method_call((yyvsp[-5].node), (yyvsp[-3].str));
        NodeList* al=(NodeList*)(yyvsp[-1].node_list); c->args=al->items; c->arg_count=al->count; free(al);
        (yyval.node) = (ASTNode*)c; SET_LINE((yyval.node));
    }
#line 3058 "parser_lsp.tab.c"
    break;

  case 114: /* expr: expr DOT IDENTIFIER  */
#line 645 "parser_lsp.y"
                          { (yyval.node) = (ASTNode*)make_member_access((yyvsp[-2].node), (yyvsp[0].str)); SET_LINE((yyval.node)); }
#line 3064 "parser_lsp.tab.c"
    break;

  case 115: /* expr: expr PLUS expr  */
#line 647 "parser_lsp.y"
                      { (yyval.node) = (ASTNode*)make_binary_op("+",  (yyvsp[-2].node), (yyvsp[0].node)); SET_LINE((yyval.node)); }
#line 3070 "parser_lsp.tab.c"
    break;

  case 116: /* expr: expr MINUS expr  */
#line 648 "parser_lsp.y"
                      { (yyval.node) = (ASTNode*)make_binary_op("-",  (yyvsp[-2].node), (yyvsp[0].node)); SET_LINE((yyval.node)); }
#line 3076 "parser_lsp.tab.c"
    break;

  case 117: /* expr: expr STAR expr  */
#line 649 "parser_lsp.y"
                      { (yyval.node) = (ASTNode*)make_binary_op("*",  (yyvsp[-2].node), (yyvsp[0].node)); SET_LINE((yyval.node)); }
#line 3082 "parser_lsp.tab.c"
    break;

  case 118: /* expr: expr SLASH expr  */
#line 650 "parser_lsp.y"
                      { (yyval.node) = (ASTNode*)make_binary_op("/",  (yyvsp[-2].node), (yyvsp[0].node)); SET_LINE((yyval.node)); }
#line 3088 "parser_lsp.tab.c"
    break;

  case 119: /* expr: expr MOD expr  */
#line 651 "parser_lsp.y"
                      { (yyval.node) = (ASTNode*)make_binary_op("%",  (yyvsp[-2].node), (yyvsp[0].node)); SET_LINE((yyval.node)); }
#line 3094 "parser_lsp.tab.c"
    break;

  case 120: /* expr: expr LT expr  */
#line 652 "parser_lsp.y"
                      { (yyval.node) = (ASTNode*)make_binary_op("<",  (yyvsp[-2].node), (yyvsp[0].node)); SET_LINE((yyval.node)); }
#line 3100 "parser_lsp.tab.c"
    break;

  case 121: /* expr: expr GT expr  */
#line 653 "parser_lsp.y"
                      { (yyval.node) = (ASTNode*)make_binary_op(">",  (yyvsp[-2].node), (yyvsp[0].node)); SET_LINE((yyval.node)); }
#line 3106 "parser_lsp.tab.c"
    break;

  case 122: /* expr: expr LE expr  */
#line 654 "parser_lsp.y"
                      { (yyval.node) = (ASTNode*)make_binary_op("<=", (yyvsp[-2].node), (yyvsp[0].node)); SET_LINE((yyval.node)); }
#line 3112 "parser_lsp.tab.c"
    break;

  case 123: /* expr: expr GE expr  */
#line 655 "parser_lsp.y"
                      { (yyval.node) = (ASTNode*)make_binary_op(">=", (yyvsp[-2].node), (yyvsp[0].node)); SET_LINE((yyval.node)); }
#line 3118 "parser_lsp.tab.c"
    break;

  case 124: /* expr: expr EQ expr  */
#line 656 "parser_lsp.y"
                      { (yyval.node) = (ASTNode*)make_binary_op("==", (yyvsp[-2].node), (yyvsp[0].node)); SET_LINE((yyval.node)); }
#line 3124 "parser_lsp.tab.c"
    break;

  case 125: /* expr: expr NE expr  */
#line 657 "parser_lsp.y"
                      { (yyval.node) = (ASTNode*)make_binary_op("!=", (yyvsp[-2].node), (yyvsp[0].node)); SET_LINE((yyval.node)); }
#line 3130 "parser_lsp.tab.c"
    break;

  case 126: /* expr: expr AND expr  */
#line 658 "parser_lsp.y"
                      { (yyval.node) = (ASTNode*)make_binary_op("&&", (yyvsp[-2].node), (yyvsp[0].node)); SET_LINE((yyval.node)); }
#line 3136 "parser_lsp.tab.c"
    break;

  case 127: /* expr: expr OR expr  */
#line 659 "parser_lsp.y"
                      { (yyval.node) = (ASTNode*)make_binary_op("||", (yyvsp[-2].node), (yyvsp[0].node)); SET_LINE((yyval.node)); }
#line 3142 "parser_lsp.tab.c"
    break;

  case 128: /* expr: NOT expr  */
#line 661 "parser_lsp.y"
                        { (yyval.node) = (ASTNode*)make_unary_op("!", (yyvsp[0].node), 0); SET_LINE((yyval.node)); }
#line 3148 "parser_lsp.tab.c"
    break;

  case 129: /* expr: MINUS expr  */
#line 662 "parser_lsp.y"
                              { (yyval.node) = (ASTNode*)make_unary_op("-", (yyvsp[0].node), 0); SET_LINE((yyval.node)); }
#line 3154 "parser_lsp.tab.c"
    break;

  case 130: /* expr: INC expr  */
#line 663 "parser_lsp.y"
                        { (yyval.node) = (ASTNode*)make_unary_op("++", (yyvsp[0].node), 0); SET_LINE((yyval.node)); }
#line 3160 "parser_lsp.tab.c"
    break;

  case 131: /* expr: DEC expr  */
#line 664 "parser_lsp.y"
                        { (yyval.node) = (ASTNode*)make_unary_op("--", (yyvsp[0].node), 0); SET_LINE((yyval.node)); }
#line 3166 "parser_lsp.tab.c"
    break;

  case 132: /* expr: expr INC  */
#line 665 "parser_lsp.y"
                        { (yyval.node) = (ASTNode*)make_unary_op("++", (yyvsp[-1].node), 1); SET_LINE((yyval.node)); }
#line 3172 "parser_lsp.tab.c"
    break;

  case 133: /* expr: expr DEC  */
#line 666 "parser_lsp.y"
                        { (yyval.node) = (ASTNode*)make_unary_op("--", (yyvsp[-1].node), 1); SET_LINE((yyval.node)); }
#line 3178 "parser_lsp.tab.c"
    break;

  case 134: /* expr: LPAREN expr RPAREN  */
#line 667 "parser_lsp.y"
                         { (yyval.node) = (yyvsp[-1].node); }
#line 3184 "parser_lsp.tab.c"
    break;

  case 135: /* expr: LPAREN expr COMMA args RPAREN  */
#line 669 "parser_lsp.y"
                                    {
        NodeList* al = (NodeList*)(yyvsp[-1].node_list);
        int total = 1 + al->count;
        ASTNode **elems = malloc(total * sizeof(ASTNode*));
        elems[0] = (yyvsp[-3].node);
        for (int i = 0; i < al->count; i++) elems[i+1] = al->items[i];
        free(al->items); free(al);
        (yyval.node) = (ASTNode*)make_tuple(elems, total);
        free(elems);
        SET_LINE((yyval.node));
    }
#line 3200 "parser_lsp.tab.c"
    break;


#line 3204 "parser_lsp.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == LSP_YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      {
        yypcontext_t yyctx
          = {yyssp, yytoken};
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == -1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = YY_CAST (char *,
                             YYSTACK_ALLOC (YY_CAST (YYSIZE_T, yymsg_alloc)));
            if (yymsg)
              {
                yysyntax_error_status
                  = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
                yymsgp = yymsg;
              }
            else
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = YYENOMEM;
              }
          }
        yyerror (yymsgp);
        if (yysyntax_error_status == YYENOMEM)
          YYNOMEM;
      }
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= LSP_YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == LSP_YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = LSP_YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != LSP_YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
  return yyresult;
}

#line 682 "parser_lsp.y"


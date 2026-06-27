/* Provides the definition of current_compile_target that compiler.c
   extern-declares and parser.y/lexer.l reference.  The generated
   parser.tab.c predates this variable being added to parser.y, so we
   supply it here to satisfy the linker. */
const char *current_compile_target = "linux";

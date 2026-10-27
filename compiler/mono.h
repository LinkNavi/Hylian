#ifndef MONO_H
#define MONO_H
#include "ast.h"
#include <stddef.h>

/* Generics and tuples. See mono.c. */

/* Called by the parser whenever it builds generic syntax, so programs that use
   none skip the whole pass. */
void mono_note_use(void);
/* `T: Ord`: the bounds of a type-parameter list (parallel to `names`, NULL = none),
   keyed by the names array the declaration keeps. */
void mono_set_bounds(char **names, char **bounds);
/* A use that only carries a type name (cast<Box<int>>, sizeof): make sure the
   instance exists. */
void mono_request(const Type *t);

/* "Box_of_int" for Box<int>; the same spelling the instance is given. */
char *mono_mangle_type(const Type *t);
char *mono_instance_name(const char *base, const Type *args, int nargs);

/* Expand every explicit use of a generic. Removes the templates from the
   program and appends the instances. Run before typecheck(). */
void mono_run(ProgramNode *prog, const char *filename);

/* Called for each class/function instance created after mono_run() returns,
   i.e. while typechecking, so the typechecker can register it. */
typedef struct {
    void (*on_class)(ClassNode *);
    void (*on_func)(FuncNode *);
    void (*on_method)(ClassNode *, MethodNode *);
} MonoHooks;
void mono_set_hooks(const MonoHooks *h);

/* `max(1, 2)`: instantiate a generic function by inferring its type arguments
   from the argument types. Returns the instance's name, or NULL (with a
   message in err) when a parameter cannot be inferred. */
int mono_is_generic_func(const char *name);
const char *mono_instantiate_inferred(const char *name, const Type *argtypes, int argc,
                                      char *err, size_t errn);

/* `obj.map<int>(f)` / `obj.map(f)`: instantiate a generic method of class
   `cls`, adding the instance to the class. Returns the method's new name. */
int mono_is_generic_method(const char *cls, const char *name);
const char *mono_instantiate_method(const char *cls, const char *name, const Type *targs, int ntargs,
                                    const Type *argtypes, int argc, char *err, size_t errn);

/* `new Box(41)`: the same for a generic class, from its constructor parameters. */
int mono_is_generic_class(const char *name);
const char *mono_instantiate_class_inferred(const char *name, const Type *argtypes, int argc,
                                            char *err, size_t errn);

/* Tuples: the struct class for (A, B, ...), and a way to recognise one. */
const char *mono_tuple_class(const Type *elems, int n);
int mono_tuple_info(const char *cls, int *count);
const Type *mono_tuple_elem(const char *cls, int i);

#endif

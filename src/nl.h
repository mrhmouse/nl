#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#define NL_HEAD(cell) cell.value.as_pair[0]
#define NL_TAIL(cell) cell.value.as_pair[1]
#define NL_NEXT(cell) (cell.value.as_pair+1)
#define NL_HEAD_AT(ref) ref->value.as_pair[0]
#define NL_TAIL_AT(ref) ref->value.as_pair[1]
#define NL_NEXT_AT(ref) (ref->value.as_pair+1)
#define NL_FOREACH(start, a) for (a = start; a->type == NL_PAIR; a = NL_NEXT_AT(a))
struct nl_cell {
  enum {
        NL_NIL,
        NL_INTEGER,
        NL_PAIR,
        NL_SYMBOL,
  } type;
  union {
    int64_t as_integer;
    char *as_symbol;
    struct nl_cell *as_pair;
  } value;
};
struct nl_scope_symbols {
  char *name;
  struct nl_cell value;
  struct nl_scope_symbols *next;
};
struct nl_scope {
  char *last_err;
  struct nl_scope_symbols *symbols;
  struct nl_scope *parent_scope;
};
typedef int (*nl_native_func)(struct nl_scope *, struct nl_cell, struct nl_cell *result);
struct nl_cell nl_cell_as_nil();
struct nl_cell nl_cell_as_int(int64_t);
struct nl_cell nl_cell_as_pair(struct nl_cell, struct nl_cell);
struct nl_cell nl_cell_as_symbol(char *);
int nl_compare(struct nl_cell, struct nl_cell);
int64_t nl_list_length(struct nl_cell);
char *nl_intern(char *);
int nl_read(struct nl_scope *, FILE *, struct nl_cell *);
void nl_scope_init(struct nl_scope *);
void nl_scope_put(struct nl_scope *, char *, struct nl_cell);
void nl_scope_get(struct nl_scope *, char *, struct nl_cell *);
void nl_scope_define_builtins(struct nl_scope *);
int nl_run_repl(int interactive, struct nl_scope *);
void nl_globals_init();

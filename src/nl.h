#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#define NL_HEAD(cell) cell.value.as_pair[0]
#define NL_TAIL(cell) cell.value.as_pair[1]
#define NL_NEXT(cell) (cell.value.as_pair+1)
#define NL_HEAD_AT(ref) ref->value.as_pair[0]
#define NL_TAIL_AT(ref) ref->value.as_pair[1]
#define NL_NEXT_AT(ref) (ref->value.as_pair+1)
#define NL_FOREACH(start, a) for (a = start; a->type == NL_PAIR; a = NL_NEXT_AT(a))
#define NL_BUILTIN(name) int nl_ ## name(struct nl_scope *scope, struct nl_cell cell, struct nl_cell *result)
#define NL_DEF_BUILTIN(sym, name) nl_scope_put(scope, nl_intern(strdup(sym)), nl_cell_as_int((int64_t)nl_ ## name))
/**
 * The cell is the smallest block of data in nl
 */
struct nl_cell {
  enum {
        NL_NIL,
        NL_INTEGER,
        NL_SYMBOL,
        NL_PAIR,
  } type;
  union {
    int64_t as_integer;
    char *as_symbol;
    /**
     * Should be a pointer to exactly 2 cells
     */
    struct nl_cell *as_pair;
  } value;
};
/**
 * Linked list of names (which should be interned) and values
 */
struct nl_scope_symbols {
  char *name;
  struct nl_cell value;
  struct nl_scope_symbols *next;
};
/**
 * Scopes hold a symbol list, and optionally have a parent scope
 */
struct nl_scope {
  // TODO move this inside the symbol list, like nl_in, and make it a stack
  char *last_err;
  struct nl_scope_symbols *symbols;
  struct nl_scope *parent_scope;
};
/**
 * Native functions accept a scope (for variable lookup) and a cell (which
 * is the tail of the call pair). They should leave the result value in the
 * result pointer, and return non-zero on error.
 */
typedef int (*nl_native_func)(struct nl_scope *, struct nl_cell, struct nl_cell *result);
NL_BUILTIN(evalq);
NL_BUILTIN(quote);
NL_BUILTIN(load);
NL_BUILTIN(loadnative);
struct nl_cell nl_cell_as_nil();
struct nl_cell nl_cell_as_int(int64_t);
/**
 * Create a new cell pointing to the given pair of values.
 * This function allocates memory, and may call the garbage-collector
 */
struct nl_cell nl_cell_as_pair(struct nl_cell, struct nl_cell);
/**
 * Create a new cell pointing to the given symbol.
 * The symbol should already be interned
 */
struct nl_cell nl_cell_as_symbol(char *);
/**
 * Compare the two cells, returning -1 if the first cell is smaller,
 * 0 if they are equal, or 1 if the first cell is larger.
 *
 * Values of the same type are compared normally: integers are compared
 * numerically, symbols are compared case-sensitively, and pairs are
 * compared first by their length and then element-wise
 *
 * Values of different types are ranked in the order: nil, integers,
 * symbols, pairs; from smallest to largest
 */
int nl_compare(struct nl_cell, struct nl_cell);
int64_t nl_list_length(struct nl_cell);
/**
 * Intern the given symbol, possibly freeing the memory it points to
 * if the same symbol has been interned before. Returns the interned
 * symbol
 */
char *nl_intern(char *);
/**
 * Read the next value from the given file, storing it into the given cell location.
 * Returns non-zero on error.
 */
int nl_read(struct nl_scope *, FILE *, struct nl_cell *);
/**
 * Initialize a scope struct. This should be called before using
 * a scope in any other way
 */
void nl_scope_init(struct nl_scope *);
/**
 * Bind the given value to the given symbol, which should be interned,
 * in the given scope. If the symbol is already bound in scope, that
 * value is updated. Otherwise, the symbol is bound in root scope.
 */
void nl_scope_put(struct nl_scope *, char *, struct nl_cell);
/**
 * Get the value for the given symbol in the given scope, storing it in the given cell location
 */
void nl_scope_get(struct nl_scope *, char *, struct nl_cell *);
/**
 * Initialize the root scope by binding native core functions.
 * This should be called once for the root scope of the program,
 * after calling nl_scope_init
 *
 * TODO rename nl_root_scope_init(...)
 */
void nl_scope_define_builtins(struct nl_scope *);
/**
 * Read each datum from the file bound at *In in the given scope, evaluating
 * each datum until end-of-file
 *
 * Optionally run interactively
 *
 * TODO move prompt symbols into scope
 */
int nl_run_repl(int interactive, struct nl_scope *);
/**
 * Initialize global values needed by the runtime.
 * This must be called once before running the program
 */
void nl_globals_init();

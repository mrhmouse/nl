// `nl` is an _unfinished_ interpreted Lisp-1
// with dynamic scope and PicoLisp-style "macros",
// built on top of a handful of datatypes and builtins
//
// TODO
// ====
// * no double-quoted symbols
//   we need a way to read & write double-quoted symbols
//   with spaces or other special characters
// * needs a set of core builtins
//   must decide on a minimal set of builtins to provide
// * needs a standard library
//   outside of the builtins, must provide a standard
//   library with functions for building real
//   applications, e.g. HTML generation libraries,
//   data structures built on cells, test frameworks, etc
// * needs unquote operator
//   unsure if this should work with the standard `quote`
//   or if it should require the use of `quasiquote`. need
//   both a single-item and a splicing version, e.g.
//   `'(1 2 ,(+ 1 2))` should produce the list `(1 2 3)`
//   while `'(1 2 ,.(range 3 5))` should produce `(1 2 3 4 5)`
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <gc.h>
#define NL_HEAD(cell) cell.value.as_pair[0]
#define NL_TAIL(cell) cell.value.as_pair[1]
#define NL_HEAD_AT(ref) ref->value.as_pair[0]
#define NL_TAIL_AT(ref) ref->value.as_pair[1]
//
// Structures and typedefs
// =======================
// the cell is the basic building block of all data
// in `nl`, and can be an integer, a symbol, the special
// value `nil` or a pair of two other cells.
//
struct nl_cell {
  enum {
        // the special value `nil` is the default value, and
        // the only value that is treated as "false" by comparisons
        NL_NIL,
        // integers are signed, 64-bit values
        NL_INTEGER,
        // pairs can be used to compose cells, and so are the building
        // blocks of all higher-level data structures
        NL_PAIR,
        // symbols are immutable strings, used for variable names
        // as well as plain data
        NL_SYMBOL,
  } type;
  union {
    int64_t as_integer;
    char *as_symbol;
    struct nl_cell *as_pair;
  } value;
};
// `nl` has dynamic scope, which means that the environment
// variables are evaluated in can change over time (multiple evaluations).
//
// variables are kept in a linked list along with their name, which
// we make sure is _interned_ for fast pointer comparisons on lookup
struct nl_scope_symbols {
  char *name;
  struct nl_cell value;
  struct nl_scope_symbols *next;
};
// scopes can be nested, with one parent scope having many potential
// child scopes. on lookup, if a symbol isn't found in the child scope,
// then the parent scope is consulted recursively until a match is found,
// or there are no more parent scopes. if no match is found, the value `nil`
// is returned by default
//
// currently, scopes also contain the traditional I/O streams plus
// a single slot for the last error in scope.. something TODO later
// will be to possibly move these to values inside the scope, so
// lookups work naturally and they can be shadowed
struct nl_scope {
  FILE *stdout, *stdin, *stderr;
  char *last_err;
  struct nl_scope_symbols *symbols;
  struct nl_scope *parent_scope;
};
// native functions (currently only builtins) all adhere to the same signature.
// all native functions return an error code: `0` for no error. they receive a scope
// in which to evaluate symbols, and a cell which holds the argument, unevaluated.
// the result of the function should be written to the `result` pointer before exit
//
// in most cases, the cell argument is a pair at the head of a linked list of arguments.
// in some cases though, such as with the `quote` builtin, it makes sense to accept
// arguments of other types than pairs.
typedef int (*nl_native_func)(struct nl_scope *, struct nl_cell, struct nl_cell *result);
// Constructors and Initializers
// =============================
// create a nil cell: the default value, and the only "false" value
struct nl_cell nl_cell_as_nil() {
  struct nl_cell c;
  c.type = NL_NIL;
  return c;
}
// create a cell containing the given integer
struct nl_cell nl_cell_as_int(int64_t value) {
  struct nl_cell c;
  c.type = NL_INTEGER;
  c.value.as_integer = value;
  return c;
}
// create a cell containing the given head and tail.
// *this function allocates*
//
// note that, unlike most Lisps, pairs consist of a "head"
// and a "tail" -- not a "car" and a "cdr". these arbitrary
// words could just as easily be "left" and "right", but in
// 2020 we can stop calling them "car" and "cdr" :^)
struct nl_cell nl_cell_as_pair(struct nl_cell head, struct nl_cell tail) {
  struct nl_cell c;
  c.type = NL_PAIR;
  c.value.as_pair = GC_malloc(sizeof(head) + sizeof(tail));
  NL_HEAD(c) = head;
  NL_TAIL(c) = tail;
  return c;
}
// create a cell referencing the given symbol, which should
// already have been interned
struct nl_cell nl_cell_as_symbol(char *interned_symbol) {
  struct nl_cell c;
  c.type = NL_SYMBOL;
  c.value.as_symbol = interned_symbol;
  return c;
}
// Globals
// =======
// these values are used internally
static struct nl_cell quote;
// initialize a scope, defining the first value: "nil"
void nl_scope_init(struct nl_scope *scope) {
  scope->stdout = stdout;
  scope->stdin = stdin;
  scope->stderr = stderr;
  scope->last_err = NULL;
  scope->parent_scope = NULL;
  scope->symbols = GC_malloc(sizeof(*scope->symbols));
  scope->symbols->name = "nil";
  scope->symbols->value.type = NL_NIL;
  scope->symbols->next = NULL;
}
// Reading Data from Input
// =======================
// read the next non-whitespace character from the stdin in scope
int nl_skip_whitespace(struct nl_scope *scope) {
  int ch;
  do {
    ch = fgetc(scope->stdin);
  } while (isspace(ch));
  return ch;
}
// symbols are permanently interned into the program memory.
// for long-running programs this is potentially an issue, so
// try to avoid creating symbols based on user input
struct nl_interned_symbols {
  char *sym;
  struct nl_interned_symbols *next;
};
static struct nl_interned_symbols *nl_interned_symbols;
char *nl_intern(char *sym) {
  if (!nl_interned_symbols) {
    nl_interned_symbols = malloc(sizeof(*nl_interned_symbols));
    nl_interned_symbols->sym = sym;
    nl_interned_symbols->next = NULL;
    return sym;
  }
  struct nl_interned_symbols *s;
  for (s = nl_interned_symbols; s != NULL; s = s->next) {
    if (0 == strcmp(sym, s->sym)) {
      free(sym);
      return s->sym;
    }
    if (s->next == NULL) {
      s->next = malloc(sizeof(*s->next));
      s->next->sym = sym;
      s->next->next = NULL;
      return sym;
    }
  }
  return NULL;
}
// read the next value from the stdin in scope.
int nl_read(struct nl_scope *scope, struct nl_cell *result) {
  struct nl_cell head, *tail;
  int sign = 1, ch = nl_skip_whitespace(scope);
  if (ch == '-') {
    // the hyphen can be used to indicate negative numbers, but
    // it could also be the start of a symbol
    int peek = fgetc(scope->stdin);
    if (isdigit(peek)) {
      sign = -1;
      ch = peek;
      goto NL_READ_DIGIT;
    }
    ungetc(peek, scope->stdin);
    goto NL_READ_SYMBOL;
  } else if (isdigit(ch)) {
    // otherwise, only numbers start with digits
  NL_READ_DIGIT:
    *result = nl_cell_as_int(ch - '0');
    while (isdigit(ch = fgetc(scope->stdin))) {
      result->value.as_integer *= 10;
      result->value.as_integer += ch - '0';
    }
    result->value.as_integer *= sign;
    ungetc(ch, scope->stdin);
    return 0;
  } else if ('"' == ch) {
    // TODO :^)
    scope->last_err = "quoted symbols not implemented";
    return 1;
  } else if ('\'' == ch) {
    // if the value is prefixed with the single-quote character,
    // then it is returned as the tail of a pair, with the symbol
    // `quote` as the head
    if (nl_read(scope, &head)) return 1;
    *result = nl_cell_as_pair(nl_cell_as_symbol(nl_intern(strdup("quote"))), head);
    return 0;
  } else if (',' == ch) {
    // if the value is prefixed with the single-quote character,
    // then it is returned as the tail of a pair, with the symbol
    // `unquote` as the head. TODO this doesn't currently do anything
    // at evaluation time, though. it might later interact with `quote`
    if (nl_read(scope, &head)) return 1;
    *result = nl_cell_as_pair(nl_cell_as_symbol(nl_intern(strdup("unquote"))), head);
    return 0;
  } else if ('(' == ch) {
    // lists are written enclosed in parentheses, and can have two or
    // more items separated by whitespace. they are encoded as chains
    // of pairs, where the last tail is any non-pair cell -- usually `nil`
    ch = nl_skip_whitespace(scope);
    if (ch == ')') {
      *result = nl_cell_as_nil();
      return 0;
    }
    ungetc(ch, scope->stdin);
    if (nl_read(scope, &head)) return 1;
    *result = nl_cell_as_pair(head, nl_cell_as_nil());
    tail = result->value.as_pair + 1;
    for (;;) {
      ch = nl_skip_whitespace(scope);
      if (ch == ')') return 0;
      if (ch == '.') {
        // if the last item in a list is separated from the other
        // items using a period, then it will be the last tail of
        // the chain of pairs composing the list
        if (nl_read(scope, tail)) return 1;
        if (nl_skip_whitespace(scope) != ')') {
          scope->last_err = "illegal list";
          return 1;
        }
        return 0;
      }
      // otherwise, the last tail of the chain is set to `nil`
      ungetc(ch, scope->stdin);
      if (nl_read(scope, &head)) return 1;
      *tail = nl_cell_as_pair(head, nl_cell_as_nil());
      tail = tail->value.as_pair + 1;
    }
  } else {
    // any other character that isn't matched can be used to start a symbol
    int used, allocated;
    char *buf;
  NL_READ_SYMBOL:
    used = 0;
    allocated = 16;
    buf = malloc(sizeof(char) * allocated);
    // symbols can contain any non-whitespace character besides parentheses,
    // since these are used to denote lists. this means that while a symbol
    // cannot normally _start_ with characters like the period or comma, it
    // *can* contain them. if you intend to use these characters as operators,
    // ensure they're separated from other symbols on the left by whitespace
    for (; !isspace(ch) && ch != '(' && ch != ')'; ch = fgetc(scope->stdin)) {
      buf[used++] = ch;
      if (used == allocated) {
        allocated *= 2;
        buf = realloc(buf, sizeof(char) * allocated);
      }
    }
    ungetc(ch, scope->stdin);
    buf[used] = '\0';
    buf = realloc(buf, sizeof(char) * used);
    // all symbols get interned as soon as they're read; only unique values
    // are kept in memory. symbols don't disappear from memory once read
    *result = nl_cell_as_symbol(nl_intern(buf));
    return 0;
  }
}
// Working with Scope
// ==================
// putting a value into a scope overwrites the first value with a matching
// name, including symbols inherited from the parent scopes. if no matching
// value is found, and there are no more parent scopes, then the value is added
// to the list of symbols in that scope.
//
// importantly, this means that defining a value for a new symbol in a child scope
// actually writes that value into the top-most parent scope. this construction
// allows the user to define new functions which define top-level values
void nl_scope_put(struct nl_scope *scope, char *name, struct nl_cell value) {
  struct nl_scope_symbols *s;
  for (; scope != NULL; scope = scope->parent_scope) {
    for (s = scope->symbols; s != NULL; s = s->next) {
      if (name == s->name) {
        s->value = value;
        return;
      }
      if (!s->next && !scope->parent_scope) {
        s->next = GC_malloc(sizeof(*s->next));
        s->next->name = name;
        s->next->value = value;
        return;
      }
    }
  }
}
// getting a value from a scope searches for an exact pointer match of the
// name through all symbols in the scope, and then through all parent scopes.
// if no matching symbol is found, and there are no more parent scopes,
// the default value `nil` is returned
void nl_scope_get(struct nl_scope *scope, char *name, struct nl_cell *result) {
  struct nl_scope_symbols *s;
  for (; scope != NULL; scope = scope->parent_scope) {
    for (s = scope->symbols; s != NULL; s = s->next)
      if (name == s->name) {
        *result = s->value;
        return;
      }
  }
  *result = nl_cell_as_nil();
}
// linking a scope to a parent scope also copies references to the scope's I/O streams.
// this will allow child scopes to temporarily redirect their input or output to files
void nl_scope_link(struct nl_scope *child, struct nl_scope *parent) {
  child->stdout = parent->stdout;
  child->stdin = parent->stdin;
  child->stderr = parent->stderr;
  child->parent_scope = parent;
}
// Language Builtins
// =================
// all builtins have the same signature as native functions
#define NL_BUILTIN(name) int nl_ ## name(struct nl_scope *scope, struct nl_cell cell, struct nl_cell *result)
#define NL_FOREACH(start, a) for (a = start; a->type == NL_PAIR; a = a->value.as_pair + 1)
#define NL_DEF_BUILTIN(sym, name) nl_scope_put(scope, nl_intern(strdup(sym)), nl_cell_as_int((int64_t)nl_ ## name))
NL_BUILTIN(evalq);
int nl_setqe(struct nl_scope *target_scope, struct nl_scope *eval_scope, struct nl_cell args, struct nl_cell *result) {
  struct nl_cell *tail;
  if (args.type != NL_PAIR) {
    target_scope->last_err = "illegal setq call: non-pair args";
    return 1;
  }
  for (tail = &args; tail->type == NL_PAIR; tail = NL_TAIL_AT(tail).value.as_pair + 1) {
    if (NL_HEAD_AT(tail).type != NL_SYMBOL) {
      target_scope->last_err = "illegal setq call: non-symbol var";
      return 1;
    }
    if (NL_TAIL_AT(tail).type != NL_PAIR) {
      if (nl_evalq(eval_scope, NL_TAIL_AT(tail), result)) return 1;
      nl_scope_put(target_scope, NL_HEAD_AT(tail).value.as_symbol, *result);
      return 0;
    }
    if (nl_evalq(eval_scope, NL_HEAD(NL_TAIL_AT(tail)), result)) return 1;
    nl_scope_put(target_scope, NL_HEAD_AT(tail).value.as_symbol, *result);
  }
  return 0;
}
/**
 * (letq (A 1 B 2
 *        C 3 D 4)
 *  (body ...)
 *  (more-body ...))
 *
 * Create a symbols list for the duration, based on the current symbols list.
 * Evaluate each value and set it to the quoted symbol, as with setq, in the first arg.
 * Evaluate each scopement in the remaining args.
 * Discard the symbols list.
 * Result is the last evaluated scopement.
 */
NL_BUILTIN(letq) {
  struct nl_scope body_scope;
  struct nl_cell vars, body, *tail;
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal letq: non-pair args";
    return 1;
  }
  vars = NL_HEAD(cell);
  body = NL_TAIL(cell);
  if (body.type != NL_PAIR) {
    scope->last_err = "illegal letq: non-pair body";
    return 1;
  }
  nl_scope_init(&body_scope);
  if (vars.type == NL_PAIR) {
    if (nl_setqe(&body_scope, scope, vars, result)) return 1;
  }
  nl_scope_link(&body_scope, scope);
  NL_FOREACH(&body, tail) {
    if (nl_evalq(&body_scope, NL_HEAD_AT(tail), result)) return 1;
  }
  if (tail->type != NL_NIL) return nl_evalq(&body_scope, *tail, result);
  return 0;
}
NL_BUILTIN(writeq);
NL_BUILTIN(call) {
  struct nl_cell *p, *a, v;
  struct nl_scope call_scope;
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal call: non-pair args";
    return 1;
  }
 retry:
  switch (NL_HEAD(cell).type) {
  case NL_SYMBOL:
    nl_scope_get(scope, NL_HEAD(cell).value.as_symbol, cell.value.as_pair);
    goto retry;
  case NL_INTEGER:
    return ((nl_native_func)NL_HEAD(cell).value.as_integer)(scope, NL_TAIL(cell), result);
  case NL_NIL:
    scope->last_err = "illegal call: cannot invoke nil";
    return 1;
  default:
    break;
  }
  nl_scope_init(&call_scope);
  switch (NL_HEAD(NL_HEAD(cell)).type) {
  case NL_SYMBOL:
    nl_scope_put(&call_scope, NL_HEAD(NL_HEAD(cell)).value.as_symbol, NL_TAIL(cell));
    break;
  case NL_PAIR:
    a = &NL_TAIL(cell);
    NL_FOREACH(&NL_HEAD(NL_HEAD(cell)), p) {
      if (NL_HEAD_AT(p).type != NL_SYMBOL) {
        scope->last_err = "illegal call: non-symbol parameter in lambda";
        return 1;
      }
      if (a->type == NL_PAIR) {
        if (nl_evalq(scope, NL_HEAD_AT(a), &v)) return 1;
        nl_scope_put(&call_scope, NL_HEAD_AT(p).value.as_symbol, v);
        a = a->value.as_pair + 1;
      } else if (a->type == NL_NIL) {
        nl_scope_put(&call_scope, NL_HEAD_AT(p).value.as_symbol, *a);
      } else {
        if (nl_evalq(scope, *a, &v)) return 1;
        nl_scope_put(&call_scope, NL_HEAD_AT(p).value.as_symbol, v);
        a->type = NL_NIL;
      }
    }
    break;
  case NL_NIL:
    break;
  default:
    scope->last_err = "illegal call: illegal parameter list in lambda";
    return 1;
  }
  nl_scope_link(&call_scope, scope);
  NL_FOREACH(&NL_TAIL(NL_HEAD(cell)), p) {
    if (nl_evalq(&call_scope, NL_HEAD_AT(p), result)) return 1;
  }
  return 0;
}
NL_BUILTIN(evalq) {
  switch (cell.type) {
  case NL_NIL:
  case NL_INTEGER:
    *result = cell;
    return 0;
  case NL_SYMBOL:
    nl_scope_get(scope, cell.value.as_symbol, result);
    return 0;
  case NL_PAIR:
    return nl_call(scope, cell, result);
  default:
    scope->last_err = "unknown cell type";
    return 1;
  }
}
NL_BUILTIN(quote) {
  *result = cell;
  return 0;
}
NL_BUILTIN(is_nil) {
  if (nl_evalq(scope, cell.type == NL_PAIR ? NL_HEAD(cell) : cell, result)) return 1;
  if (result->type == NL_NIL)
    *result = nl_cell_as_symbol(nl_intern(strdup("t")));
  else
    *result = nl_cell_as_nil();
  return 0;
}
NL_BUILTIN(is_integer) {
  if (nl_evalq(scope, cell.type == NL_PAIR ? NL_HEAD(cell) : cell, result)) return 1;
  if (result->type == NL_INTEGER)
    *result = nl_cell_as_symbol(nl_intern(strdup("t")));
  else
    *result = nl_cell_as_nil();
  return 0;
}
NL_BUILTIN(is_pair) {
  if (nl_evalq(scope, cell.type == NL_PAIR ? NL_HEAD(cell) : cell, result)) return 1;
  if (result->type == NL_PAIR)
    *result = nl_cell_as_symbol(nl_intern(strdup("t")));
  else
    *result = nl_cell_as_nil();
  return 0;
}
NL_BUILTIN(is_symbol) {
  if (nl_evalq(scope, cell.type == NL_PAIR ? NL_HEAD(cell) : cell, result)) return 1;
  if (result->type == NL_SYMBOL)
    *result = nl_cell_as_symbol(nl_intern(strdup("t")));
  else
    *result = nl_cell_as_nil();
  return 0;
}
int nl_cell_equal(struct nl_cell a, struct nl_cell b) {
  if (a.type != b.type) return 0;
  switch (a.type) {
  case NL_NIL: return 1;
  case NL_INTEGER: return a.value.as_integer == b.value.as_integer;
  case NL_SYMBOL: return a.value.as_symbol == b.value.as_symbol;
  case NL_PAIR: return nl_cell_equal(NL_HEAD(a), NL_HEAD(b))
      && nl_cell_equal(NL_TAIL(a), NL_TAIL(b));
  default: return 0;
  }
}
NL_BUILTIN(apply) {
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal apply call: non-pair args";
    return 1;
  }
  if (NL_HEAD(cell).type != NL_SYMBOL) {
    scope->last_err = "illegal apply call: first arg should be symbol";
    return 1;
  }
  if (NL_TAIL(cell).type != NL_PAIR) {
    scope->last_err = "illegal apply call: non-pair args tail";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(NL_TAIL(cell)), result)) return 1;
  cell = nl_cell_as_pair(NL_HEAD(cell), *result);
  return nl_evalq(scope, cell, result);
}
NL_BUILTIN(eval) {
  struct nl_cell *tail, form;
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal eval: non-pair args";
    return 1;
  }
  NL_FOREACH(&cell, tail) {
    if (nl_evalq(scope, NL_HEAD_AT(tail), &form)) return 1;
    if (nl_evalq(scope, form, result)) return 1;
  }
  return 0;
}
NL_BUILTIN(foreach) {
  struct nl_cell fun, list, *a, call;
  if (cell.type != NL_PAIR || NL_TAIL(cell).type != NL_PAIR) {
    scope->last_err = "illegal foreach: expected at least two args";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &fun)) return 1;
  if (nl_evalq(scope, NL_HEAD(NL_TAIL(cell)), &list)) return 1;
  if (list.type == NL_NIL) {
    *result = nl_cell_as_nil();
    return 0;
  }
  if (list.type != NL_PAIR) {
    scope->last_err = "illegal foreach: expected a pair";
    return 1;
  }
  quote = nl_cell_as_symbol(nl_intern(strdup("quote")));
  NL_FOREACH(&list, a) {
    call = nl_cell_as_pair(fun, nl_cell_as_pair(nl_cell_as_pair(quote, *a), nl_cell_as_nil()));
    if (nl_call(scope, call, result)) return 1;
  }
  return 0;
}
NL_BUILTIN(map) {
  struct nl_cell fun, list, *item, call;
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal map: non-pair args";
    return 1;
  }
  if (NL_TAIL(cell).type != NL_PAIR) {
    scope->last_err = "illegal map: expected at least two args in list";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &fun)) return 1;
  if (nl_evalq(scope, NL_HEAD(NL_TAIL(cell)), &list)) return 1;
  if (list.type == NL_NIL) {
    *result = list;
    return 0;
  }
  if (list.type != NL_PAIR) {
    scope->last_err = "illegal map: second argument should be a pair";
    return 1;
  }
  *result = nl_cell_as_pair(nl_cell_as_nil(), nl_cell_as_nil());
  NL_FOREACH(&list, item) {
    call = nl_cell_as_pair(fun, nl_cell_as_pair(nl_cell_as_pair(quote, NL_HEAD_AT(item)), nl_cell_as_nil()));
    if (nl_call(scope, call, result->value.as_pair)) return 1;
    switch (NL_TAIL_AT(item).type) {
    case NL_NIL:
      break;
    case NL_PAIR:
      NL_TAIL_AT(result) = nl_cell_as_pair(nl_cell_as_nil(), nl_cell_as_nil());
      result = result->value.as_pair + 1;
      break;
    default:
      call = nl_cell_as_pair(fun, nl_cell_as_pair(nl_cell_as_pair(quote, NL_TAIL_AT(item)), nl_cell_as_nil()));
      return nl_call(scope, call, result->value.as_pair + 1);
    }
  }
  return 0;
}
NL_BUILTIN(filter) {
  struct nl_cell fun, list, *item, call;
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal filter: non-pair args";
    return 1;
  }
  if (NL_TAIL(cell).type != NL_PAIR) {
    scope->last_err = "illegal filter: expected at least two args in list";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &fun)) return 1;
  if (nl_evalq(scope, NL_HEAD(NL_TAIL(cell)), &list)) return 1;
  if (list.type == NL_NIL) {
    *result = list;
    return 0;
  }
  if (list.type != NL_PAIR) {
    scope->last_err = "illegal filter: second argument should be a pair";
    return 1;
  }
  *result = nl_cell_as_pair(nl_cell_as_nil(), nl_cell_as_nil());
  NL_FOREACH(&list, item) {
    call = nl_cell_as_pair(fun, nl_cell_as_pair(nl_cell_as_pair(quote, NL_HEAD_AT(item)), nl_cell_as_nil()));
    if (nl_call(scope, call, result->value.as_pair)) return 1;
    if (NL_HEAD_AT(result).type != NL_NIL) {
      NL_HEAD_AT(result) = NL_HEAD_AT(item);
      NL_TAIL_AT(result) = nl_cell_as_pair(nl_cell_as_nil(), nl_cell_as_nil());
      result = result->value.as_pair + 1;
    }
  }
  *result = nl_cell_as_nil();
  return 0;
}
NL_BUILTIN(reduce) {
  struct nl_cell fun, list, *item, call;
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal reduce: non-pair args";
    return 1;
  }
  if (NL_TAIL(cell).type != NL_PAIR) {
    scope->last_err = "illegal reduce: expected at least three args in list";
    return 1;
  }
  if (NL_TAIL(NL_TAIL(cell)).type != NL_PAIR) {
    scope->last_err = "illegal reduce: expected at least three args in list";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &fun)) return 1;
  if (nl_evalq(scope, NL_HEAD(NL_TAIL(cell)), result)) return 1;
  if (nl_evalq(scope, NL_HEAD(NL_TAIL(NL_TAIL(cell))), &list)) return 1;
  if (list.type == NL_NIL) {
    return 0;
  }
  if (list.type != NL_PAIR) {
    scope->last_err = "illegal reduce: third argument should be a pair";
    return 1;
  }
  NL_FOREACH(&list, item) {
    call = nl_cell_as_pair(fun, nl_cell_as_pair(nl_cell_as_pair(quote, NL_HEAD_AT(item)), nl_cell_as_pair(nl_cell_as_pair(quote, *result), nl_cell_as_nil())));
    if (nl_call(scope, call, result)) return 1;
  }
  return 0;
}
NL_BUILTIN(equal) {
  struct nl_cell *tail, last, val;
  if (cell.type != NL_PAIR) {
    *result = nl_cell_as_symbol(nl_intern(strdup("t")));
    return 0;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &last)) return 1;
  NL_FOREACH(cell.value.as_pair + 1, tail) {
    if (nl_evalq(scope, NL_HEAD_AT(tail), &val)) return 1;
    if (!nl_cell_equal(last, val)) {
      *result = nl_cell_as_nil();
      return 0;
    }
  }
  *result = nl_cell_as_symbol(nl_intern(strdup("t")));
  return 0;
}
int64_t nl_list_length(struct nl_cell l) {
  int64_t len = 0;
  struct nl_cell *p;
  NL_FOREACH(&l, p) {
    ++len;
  }
  if (p->type != NL_NIL) ++len;
  return len;
}
int nl_compare(struct nl_cell a, struct nl_cell b) {
  struct nl_cell *i, *j;
  int item_result;
  int64_t a_len, b_len;
  // same-type comparisons
  if (a.type == b.type)
    switch (a.type) {
    case NL_NIL: return 0;
    case NL_SYMBOL: return strcmp(a.value.as_symbol, b.value.as_symbol);
    case NL_INTEGER:
      if (a.value.as_integer == b.value.as_integer) return 0;
      if (a.value.as_integer < b.value.as_integer) return -1;
      return 1;
    case NL_PAIR:
      a_len = nl_list_length(a);
      b_len = nl_list_length(b);
      if (a_len == b_len) {
        // item-by-item comparison
        for (i = &a, j = &b; i->type == NL_PAIR && j->type == NL_PAIR; i = i->value.as_pair + 1, j = j->value.as_pair + 1) {
          item_result = nl_compare(NL_HEAD_AT(i), NL_HEAD_AT(j));
          if (item_result) return item_result;
        }
        return nl_compare(*i, *j);
      }
      if (a_len < b_len) return -1;
      return 1;
    }
  // cross-type comparisons
  // nil is the smallest type
  if (a.type == NL_NIL) return -1;
  if (b.type == NL_NIL) return 1;
  // integers are bigger than nil, but smaller than other types
  if (a.type == NL_INTEGER) return -1;
  if (b.type == NL_INTEGER) return 1;
  // symbols are bigger than ints
  if (a.type == NL_SYMBOL) return -1;
  if (b.type == NL_SYMBOL) return 1;
  // pairs of any size are bigger than all other types
  if (a.type == NL_PAIR) return -1;
  return 1;
}
NL_BUILTIN(length) {
  int64_t n = 0;
  struct nl_cell *a;
  if (nl_evalq(scope, cell.type == NL_PAIR ? NL_HEAD(cell) : cell, result)) return 1;
  switch (result->type) {
  case NL_NIL:
    n = 0;
    break;
  case NL_INTEGER:
    n = 1;
    break;
  case NL_SYMBOL:
    n = strlen(result->value.as_symbol);
    break;
  case NL_PAIR:
    NL_FOREACH(result, a) {
      n += 1;
      switch (NL_TAIL_AT(a).type) {
      case NL_INTEGER:
      case NL_SYMBOL:
        n += 1;
      default:
        break;
      }
    }
    break;
  default:
    scope->last_err = "unknown cell type";
    return 1;
  }
  *result = nl_cell_as_int(n);
  return 0;
}
NL_BUILTIN(lt) {
  struct nl_cell *p, a, b;
  if (cell.type != NL_PAIR) {
    *result = nl_cell_as_nil();
    return 0;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &a)) return 1;
  NL_FOREACH(cell.value.as_pair + 1, p) {
    if (nl_evalq(scope, NL_HEAD_AT(p), &b)) return 1;
    if (nl_compare(a, b) != -1) {
      *result = nl_cell_as_nil();
      return 0;
    }
    a = b;
  }
  *result = nl_cell_as_symbol(nl_intern(strdup("t")));
  return 0;
}
NL_BUILTIN(gt) {
  struct nl_cell *p, a, b;
  if (cell.type != NL_PAIR) {
    *result = nl_cell_as_nil();
    return 0;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &a)) return 1;
  NL_FOREACH(cell.value.as_pair + 1, p) {
    if (nl_evalq(scope, NL_HEAD_AT(p), &b)) return 1;
    if (nl_compare(a, b) != 1) {
      *result = nl_cell_as_nil();
      return 0;
    }
    a = b;
  }
  *result = nl_cell_as_symbol(nl_intern(strdup("t")));
  return 0;
}
NL_BUILTIN(lte) {
  struct nl_cell *p, a, b;
  if (cell.type != NL_PAIR) {
    *result = nl_cell_as_nil();
    return 0;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &a)) return 1;
  NL_FOREACH(cell.value.as_pair + 1, p) {
    if (nl_evalq(scope, NL_HEAD_AT(p), &b)) return 1;
    if (nl_compare(a, b) == 1) {
      *result = nl_cell_as_nil();
      return 0;
    }
    a = b;
  }
  *result = nl_cell_as_symbol(nl_intern(strdup("t")));
  return 0;
}
NL_BUILTIN(gte) {
  struct nl_cell *p, a, b;
  if (cell.type != NL_PAIR) {
    *result = nl_cell_as_nil();
    return 0;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &a)) return 1;
  NL_FOREACH(cell.value.as_pair + 1, p) {
    if (nl_evalq(scope, NL_HEAD_AT(p), &b)) return 1;
    if (nl_compare(a, b) == -1) {
      *result = nl_cell_as_nil();
      return 0;
    }
    a = b;
  }
  *result = nl_cell_as_symbol(nl_intern(strdup("t")));
  return 0;
}
NL_BUILTIN(not) {
  if (nl_evalq(scope, cell.type == NL_PAIR ? NL_HEAD(cell) : cell, result)) return 1;
  if (result->type == NL_NIL)
    *result = nl_cell_as_symbol(nl_intern(strdup("t")));
  else
    *result = nl_cell_as_nil();
  return 0;
}
NL_BUILTIN(head) {
  if (cell.type != NL_PAIR) {
    scope->last_err = "invalid head: non-pair args";
    return 1;
  }
  if (NL_TAIL(cell).type != NL_NIL) {
    scope->last_err = "invalid head: too many args";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), result)) return 1;
  if (result->type == NL_PAIR) *result = NL_HEAD_AT(result);
  return 0;
}
NL_BUILTIN(tail) {
  if (cell.type != NL_PAIR) {
    scope->last_err = "invalid tail: non-pair args";
    return 1;
  }
  if (NL_TAIL(cell).type != NL_NIL) {
    scope->last_err = "invalid tail: too many args";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), result)) return 1;
  if (result->type == NL_PAIR)
    *result = NL_TAIL_AT(result);
  else
    *result = nl_cell_as_nil();
  return 0;
}
NL_BUILTIN(pair) {
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal pair: non-pair args";
    return 1;
  }
  *result = nl_cell_as_pair(nl_cell_as_nil(), nl_cell_as_nil());
  if (nl_evalq(scope, NL_HEAD(cell), result->value.as_pair)) return 1;
  if (NL_TAIL(cell).type == NL_PAIR) {
    if (nl_evalq(scope, NL_HEAD(NL_TAIL(cell)), result->value.as_pair + 1)) return 1;
  } else if (nl_evalq(scope, NL_TAIL(cell), result->value.as_pair + 1)) return 1;
  return 0;
}
NL_BUILTIN(list) {
  struct nl_cell *in_tail, *out_tail = result;
  if (cell.type == NL_NIL) {
    *result = nl_cell_as_nil();
    return 0;
  }
  *result = nl_cell_as_pair(nl_cell_as_nil(), nl_cell_as_nil());
  NL_FOREACH(&cell, in_tail) {
    if (nl_evalq(scope, NL_HEAD_AT(in_tail), out_tail->value.as_pair)) return 1;
    if (NL_TAIL_AT(in_tail).type != NL_PAIR) {
      return nl_evalq(scope, NL_TAIL_AT(in_tail), out_tail->value.as_pair + 1);
    }
    NL_TAIL_AT(out_tail) = nl_cell_as_pair(nl_cell_as_nil(), nl_cell_as_nil());
    out_tail = out_tail->value.as_pair + 1;
  }
  return 0;
}
NL_BUILTIN(add) {
  struct nl_cell *tail, val;
  if (cell.type == NL_NIL) {
    *result = nl_cell_as_int(0);
    return 0;
  }
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal add: non-pair args";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), result)) return 1;
  if (result->type != NL_INTEGER) {
    scope->last_err = "illegal add: non-integer arg";
    return 1;
  }
  NL_FOREACH(cell.value.as_pair + 1, tail) {
    if (nl_evalq(scope, NL_HEAD_AT(tail), &val)) return 1;
    if (val.type != NL_INTEGER) {
      scope->last_err = "illegal add: non-integer arg";
      return 1;
    }
    result->value.as_integer += val.value.as_integer;
  }
  return 0;
}
NL_BUILTIN(sub) {
  struct nl_cell *tail, val;
  if (cell.type == NL_NIL) {
    *result = nl_cell_as_int(0);
    return 0;
  }
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal sub: non-pair args";
    return 1;
  }
  if (NL_TAIL(cell).type == NL_NIL) {
    *result = nl_cell_as_int(-NL_HEAD(cell).value.as_integer);
    return 0;
  }
  if (nl_evalq(scope, NL_HEAD(cell), result)) return 1;
  if (result->type != NL_INTEGER) {
    scope->last_err = "illegal sub: non-integer arg";
    return 1;
  }
  NL_FOREACH(cell.value.as_pair + 1, tail) {
    if (nl_evalq(scope, NL_HEAD_AT(tail), &val)) return 1;
    if (val.type != NL_INTEGER) {
      scope->last_err = "illegal sub: non-integer arg";
      return 1;
    }
    result->value.as_integer -= val.value.as_integer;
  }
  return 0;
}
NL_BUILTIN(mul) {
  struct nl_cell *tail, val;
  int64_t sum = 1;
  if (cell.type == NL_NIL) {
    *result = nl_cell_as_int(1);
    return 0;
  }
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal mul: non-pair args";
    return 1;
  }
  NL_FOREACH(&cell, tail) {
    if (nl_evalq(scope, NL_HEAD_AT(tail), &val)) return 1;
    if (val.type != NL_INTEGER) {
      scope->last_err = "illegal mul: non-integer arg";
      return 1;
    }
    sum *= val.value.as_integer;
  }
  *result = nl_cell_as_int(sum);
  return 0;
}
NL_BUILTIN(div) {
  struct nl_cell *tail, val;
  if (cell.type == NL_NIL) {
    *result = nl_cell_as_int(1);
    return 0;
  }
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal div: non-pair args";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), result)) return 1;
  if (NL_TAIL(cell).type == NL_NIL) {
    result->value.as_integer = 1 / result->value.as_integer;
    return 0;
  }
  NL_FOREACH(cell.value.as_pair + 1, tail) {
    if (nl_evalq(scope, NL_HEAD_AT(tail), &val)) return 1;
    if (val.type != NL_INTEGER) {
      scope->last_err = "illegal div: non-integer arg";
      return 1;
    }
    result->value.as_integer /= val.value.as_integer;
  }
  return 0;
}
NL_BUILTIN(printq) {
  switch (cell.type) {
  case NL_NIL:
    fprintf(scope->stdout, "nil");
    *result = cell;
    return 0;
  case NL_INTEGER:
    fprintf(scope->stdout, "%li", cell.value.as_integer);
    *result = cell;
    return 0;
  case NL_PAIR:
    if (nl_printq(scope, NL_HEAD(cell), result)) return 1;
    if (NL_TAIL(cell).type == NL_NIL) return 0;
    fprintf(scope->stdout, NL_TAIL(cell).type == NL_PAIR ? " " : ", ");
    return nl_printq(scope, NL_TAIL(cell), result);
  case NL_SYMBOL:
    fprintf(scope->stdout, "%s", cell.value.as_symbol);
    *result = cell;
    return 0;
  default:
    scope->last_err = "unknown cell type";
    return 1;
  }
}
NL_BUILTIN(print) {
  struct nl_cell val, *tail;
  if (cell.type != NL_PAIR)
    return nl_evalq(scope, cell, result) || nl_printq(scope, cell, result);
  NL_FOREACH(&cell, tail) {
    if (nl_evalq(scope, NL_HEAD_AT(tail), &val)) return 1;
    if (nl_printq(scope, val, result)) return 1;
    if (NL_TAIL_AT(tail).type == NL_PAIR) fputc(' ', scope->stdout);
  }
  if (tail->type != NL_NIL) {
    fprintf(scope->stdout, ", ");
    nl_print(scope, *tail, result);
  }
  return 0;
}
NL_BUILTIN(defq) {
  struct nl_cell name, body;
  *result = nl_cell_as_nil();
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal defq: non-pair args";
    return 1;
  }
  name = NL_HEAD(cell);
  if (name.type != NL_SYMBOL) {
    scope->last_err = "illegal defq: non-symbol name";
    return 1;
  }
  body = NL_TAIL(cell);
  if (body.type != NL_PAIR) {
    scope->last_err = "illegal defq: non-pair body";
    return 1;
  }
  nl_scope_put(scope, name.value.as_symbol, body);
  return 0;
}
NL_BUILTIN(setq) {
  return nl_setqe(scope, scope, cell, result);
}
NL_BUILTIN(set) {
  struct nl_cell *tail, var;
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal set call: non-pair args";
    return 1;
  }
  for (tail = &cell; tail->type == NL_PAIR; tail = NL_TAIL_AT(tail).value.as_pair + 1) {
    if (nl_evalq(scope, NL_HEAD_AT(tail), &var)) return 1;
    if (var.type != NL_SYMBOL) {
      scope->last_err = "illegal set call: non-symbol var";
      return 1;
    }
    if (NL_TAIL_AT(tail).type != NL_PAIR) {
      if (nl_evalq(scope, NL_TAIL_AT(tail), result)) return 1;
      nl_scope_put(scope, var.value.as_symbol, *result);
      return 0;
    }
    if (nl_evalq(scope, NL_HEAD(NL_TAIL_AT(tail)), result)) return 1;
    nl_scope_put(scope, var.value.as_symbol, *result);
  }
  return 0;
}
NL_BUILTIN(writeq) {
  struct nl_cell *tail;
  switch (cell.type) {
  case NL_NIL:
    fprintf(scope->stdout, "nil");
    *result = cell;
    return 0;
  case NL_INTEGER:
    fprintf(scope->stdout, "%li", cell.value.as_integer);
    *result = cell;
    return 0;
  case NL_SYMBOL:
    fprintf(scope->stdout, "%s", cell.value.as_symbol);
    *result = cell;
    return 0;
  case NL_PAIR:
    fputc('(', scope->stdout);
    if (nl_writeq(scope, NL_HEAD(cell), result)) return 1;
    tail = cell.value.as_pair + 1;
    for (;;) {
      switch (tail->type) {
      case NL_NIL:
        fputc(')', scope->stdout);
        *result = cell;
        return 0;
      case NL_PAIR:
        fputc(' ', scope->stdout);
        if (nl_writeq(scope, NL_HEAD_AT(tail), result)) return 1;
        tail = tail->value.as_pair + 1;
        break;
      case NL_INTEGER:
        fprintf(scope->stdout, " . %li)", tail->value.as_integer);
        return 0;
      case NL_SYMBOL:
        // TODO write quoted symbols
        fprintf(scope->stdout, " . %s)", tail->value.as_symbol);
        return 0;
      }
    }
  }
  *result = cell;
  scope->last_err = "unhandled type";
  return 1;
}
NL_BUILTIN(write) {
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal write call: non-pair args";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), result)) return 1;
  return nl_writeq(scope, *result, result);
}
NL_BUILTIN(and) {
  struct nl_cell *tail;
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal and: non-pair args";
    return 1;
  }
  NL_FOREACH(&cell, tail) {
    if (nl_evalq(scope, NL_HEAD_AT(tail), result)) return 1;
    if (result->type == NL_NIL) return 0;
  }
  return 0;
}
NL_BUILTIN(or) {
  struct nl_cell *tail;
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal or: non-pair args";
    return 1;
  }
  NL_FOREACH(&cell, tail) {
    if (nl_evalq(scope, NL_HEAD_AT(tail), result)) return 1;
    if (result->type != NL_NIL) return 0;
  }
  return 0;
}
NL_BUILTIN(write_bytes) {
  struct nl_cell *a;
  switch (cell.type) {
  case NL_INTEGER:
    fputc((char)cell.value.as_integer, scope->stdout);
  case NL_NIL:
    *result = cell;
    return 0;
  case NL_SYMBOL:
    return nl_evalq(scope, cell, result)
      || nl_write_bytes(scope, *result, result);
  case NL_PAIR:
    NL_FOREACH(&cell, a) {
      if (nl_evalq(scope, NL_HEAD_AT(a), result)) return 1;
      if (nl_write_bytes(scope, *result, result)) return 1;
    }
    return nl_write_bytes(scope, *a, result);
  }
  scope->last_err = "unknown cell type";
  return 1;
}
NL_BUILTIN(write_wbytes) {
  struct nl_cell *a;
  switch (cell.type) {
  case NL_INTEGER:
    fputwc((wchar_t)cell.value.as_integer, scope->stdout);
  case NL_NIL:
    *result = cell;
    return 0;
  case NL_SYMBOL:
    return nl_evalq(scope, cell, result)
      || nl_write_wbytes(scope, *result, result);
  case NL_PAIR:
    NL_FOREACH(&cell, a) {
      if (nl_evalq(scope, NL_HEAD_AT(a), result)) return 1;
      if (nl_write_wbytes(scope, *result, result)) return 1;
    }
    return nl_write_wbytes(scope, *a, result);
  }
  scope->last_err = "unknown cell type";
  return 1;
}
void nl_scope_define_builtins(struct nl_scope *scope) {
  NL_DEF_BUILTIN("*", mul);
  NL_DEF_BUILTIN("+", add);
  NL_DEF_BUILTIN("-", sub);
  NL_DEF_BUILTIN("/", div);
  NL_DEF_BUILTIN("<", lt);
  NL_DEF_BUILTIN("<=", lte);
  NL_DEF_BUILTIN("=", equal);
  NL_DEF_BUILTIN(">", gt);
  NL_DEF_BUILTIN(">=", gte);
  NL_DEF_BUILTIN("and", and);
  NL_DEF_BUILTIN("apply", apply);
  NL_DEF_BUILTIN("pair", pair);
  NL_DEF_BUILTIN("defq", defq);
  NL_DEF_BUILTIN("eval", eval);
  NL_DEF_BUILTIN("filter", filter);
  NL_DEF_BUILTIN("foreach", foreach);
  NL_DEF_BUILTIN("head", head);
  NL_DEF_BUILTIN("integer?", is_integer);
  NL_DEF_BUILTIN("length", length);
  NL_DEF_BUILTIN("letq", letq);
  NL_DEF_BUILTIN("list", list);
  NL_DEF_BUILTIN("map", map);
  NL_DEF_BUILTIN("nil?", is_nil);
  NL_DEF_BUILTIN("not", not);
  NL_DEF_BUILTIN("or", or);
  NL_DEF_BUILTIN("pair?", is_pair);
  NL_DEF_BUILTIN("print", print);
  NL_DEF_BUILTIN("printq", printq);
  NL_DEF_BUILTIN("quote", quote);
  NL_DEF_BUILTIN("reduce", reduce);
  NL_DEF_BUILTIN("set", set);
  NL_DEF_BUILTIN("setq", setq);
  NL_DEF_BUILTIN("symbol?", is_symbol);
  NL_DEF_BUILTIN("tail", tail);
  NL_DEF_BUILTIN("write", write);
  NL_DEF_BUILTIN("write-bytes", write_bytes);
  NL_DEF_BUILTIN("write-wbytes", write_wbytes);
  NL_DEF_BUILTIN("writeq", writeq);
}
// Main REPL
// =========
int nl_run_repl(struct nl_scope *scope) {
  struct nl_cell last_read, last_eval;
  for (;;) {
    fprintf(scope->stdout, "\n> ");
    if (nl_read(scope, &last_read)) {
      if (scope->last_err)
        fprintf(scope->stderr, "ERROR read: %s\n", scope->last_err);
      else
        fputs("ERROR read\n", scope->stderr);
      return 1;
    }
    if (nl_evalq(scope, last_read, &last_eval)) {
      if (scope->last_err)
        fprintf(scope->stderr, "ERROR eval: %s\n", scope->last_err);
      else
        fputs("ERROR eval\n", scope->stderr);
      return 2;
    }
    fputs("; ", scope->stdout);
    nl_writeq(scope, last_eval, &last_read);
  }
}
void nl_globals_init() {
  quote = nl_cell_as_symbol(nl_intern(strdup("quote")));
}
int main() {
  struct nl_scope scope;
  nl_globals_init();
  nl_scope_init(&scope);
  nl_scope_define_builtins(&scope);
  return nl_run_repl(&scope);
}

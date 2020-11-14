// =nl= is an _unfinished_ interpreted Lisp-1
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
//   unsure if this should work with the standard =quote=
//   or if it should require the use of =quasiquote=. need
//   both a single-item and a splicing version, e.g.
//   ='(1 2 ,(+ 1 2))= should produce the list =(1 2 3)=
//   while ='(1 2 `(range 3 5))= should produce =(1 2 3 4 5)=
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <gc.h>

// Structures and typedefs
// =======================
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
struct nl_symbols {
  char *name;
  struct nl_cell value;
  struct nl_symbols *next;
};
struct nl_state {
  FILE *stdout, *stdin, *stderr;
  char *last_err;
  struct nl_symbols *symbols;
  struct nl_state *parent_state;
};
typedef int (*nl_native_func)(struct nl_state *, struct nl_cell, struct nl_cell *result);
// Constructors and Initializers
// =============================
struct nl_cell nl_cell_as_nil() {
  struct nl_cell c;
  c.type = NL_NIL;
  return c;
}
struct nl_cell nl_cell_as_int(int64_t value) {
  struct nl_cell c;
  c.type = NL_INTEGER;
  c.value.as_integer = value;
  return c;
}
struct nl_cell nl_cell_as_pair(struct nl_cell head, struct nl_cell tail) {
  struct nl_cell c;
  c.type = NL_PAIR;
  c.value.as_pair = GC_malloc(sizeof(head) + sizeof(tail));
  c.value.as_pair[0] = head;
  c.value.as_pair[1] = tail;
  return c;
}
struct nl_cell nl_cell_as_symbol(char *interned_symbol) {
  struct nl_cell c;
  c.type = NL_SYMBOL;
  c.value.as_symbol = interned_symbol;
  return c;
}
void nl_state_init(struct nl_state *state) {
  state->stdout = stdout;
  state->stdin = stdin;
  state->stderr = stderr;
  state->last_err = NULL;
  state->parent_state = NULL;
  state->symbols = GC_malloc(sizeof(*state->symbols));
  state->symbols->name = "nil";
  state->symbols->value.type = NL_NIL;
  state->symbols->next = NULL;
}
// Reading Data from Input
// =======================
int nl_skip_whitespace(struct nl_state *state) {
  int ch;
  do {
    ch = fgetc(state->stdin);
  } while (isspace(ch));
  return ch;
}
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
int nl_read(struct nl_state *state, struct nl_cell *result) {
  struct nl_cell head, *tail;
  int sign = 1, ch = nl_skip_whitespace(state);
  if (ch == '-') {
    int peek = fgetc(state->stdin);
    if (isdigit(peek)) {
      sign = -1;
      ch = peek;
      goto NL_READ_DIGIT;
    }
    ungetc(peek, state->stdin);
    goto NL_READ_SYMBOL;
  } else if (isdigit(ch)) {
  NL_READ_DIGIT:
    *result = nl_cell_as_int(ch - '0');
    while (isdigit(ch = fgetc(state->stdin))) {
      result->value.as_integer *= 10;
      result->value.as_integer += ch - '0';
    }
    result->value.as_integer *= sign;
    ungetc(ch, state->stdin);
    return 0;
  } else if ('"' == ch) {
    state->last_err = "quoted symbols not implemented";
    return 1;
  } else if ('\'' == ch) {
    if (nl_read(state, &head)) return 1;
    *result = nl_cell_as_pair(nl_cell_as_symbol(nl_intern(strdup("quote"))), head);
    return 0;
  } else if (',' == ch) {
    if (nl_read(state, &head)) return 1;
    *result = nl_cell_as_pair(nl_cell_as_symbol(nl_intern(strdup("unquote"))), head);
    return 0;
  } else if ('(' == ch) {
    ch = nl_skip_whitespace(state);
    if (ch == ')') {
      *result = nl_cell_as_nil();
      return 0;
    }
    ungetc(ch, state->stdin);
    if (nl_read(state, &head)) return 1;
    *result = nl_cell_as_pair(head, nl_cell_as_nil());
    tail = result->value.as_pair + 1;
    for (;;) {
      ch = nl_skip_whitespace(state);
      if (ch == ')') return 0;
      if (ch == '.') {
        if (nl_read(state, tail)) return 1;
        if (nl_skip_whitespace(state) != ')') {
          state->last_err = "illegal list";
          return 1;
        }
        return 0;
      }
      ungetc(ch, state->stdin);
      if (nl_read(state, &head)) return 1;
      *tail = nl_cell_as_pair(head, nl_cell_as_nil());
      tail = tail->value.as_pair + 1;
    }
  } else {
    int used, allocated;
    char *buf;
  NL_READ_SYMBOL:
    used = 0;
    allocated = 16;
    buf = malloc(sizeof(char) * allocated);
    for (; !isspace(ch) && ch != '(' && ch != ')'; ch = fgetc(state->stdin)) {
      buf[used++] = ch;
      if (used == allocated) {
        allocated *= 2;
        buf = realloc(buf, sizeof(char) * allocated);
      }
    }
    ungetc(ch, state->stdin);
    buf[used] = '\0';
    buf = realloc(buf, sizeof(char) * used);
    *result = nl_cell_as_symbol(nl_intern(buf));
    return 0;
  }
}
// Working with State
// ==================
void nl_state_put(struct nl_state *state, char *name, struct nl_cell value) {
  struct nl_symbols *s;
  for (; state != NULL; state = state->parent_state) {
    for (s = state->symbols; s != NULL; s = s->next) {
      if (name == s->name) {
        s->value = value;
        return;
      }
      if (!s->next && !state->parent_state) {
        s->next = GC_malloc(sizeof(*s->next));
        s->next->name = name;
        s->next->value = value;
        return;
      }
    }
  }
}
void nl_state_get(struct nl_state *state, char *name, struct nl_cell *result) {
  struct nl_symbols *s;
  for (; state != NULL; state = state->parent_state) {
    for (s = state->symbols; s != NULL; s = s->next)
      if (name == s->name) {
        *result = s->value;
        return;
      }
  }
  *result = nl_cell_as_nil();
}
void nl_state_link(struct nl_state *child, struct nl_state *parent) {
  child->stdout = parent->stdout;
  child->stdin = parent->stdin;
  child->stderr = parent->stderr;
  child->parent_state = parent;
}
// Language Builtins
// =================
#define NL_BUILTIN(name) int nl_ ## name(struct nl_state *state, struct nl_cell cell, struct nl_cell *result)
NL_BUILTIN(evalq);
NL_BUILTIN(quote) {
  *result = cell;
  return 0;
}
NL_BUILTIN(is_nil) {
  if (nl_evalq(state, cell.type == NL_PAIR ? cell.value.as_pair[0] : cell, result)) return 1;
  if (result->type == NL_NIL)
    *result = nl_cell_as_symbol(nl_intern(strdup("t")));
  else
    *result = nl_cell_as_nil();
  return 0;
}
NL_BUILTIN(is_integer) {
  if (nl_evalq(state, cell.type == NL_PAIR ? cell.value.as_pair[0] : cell, result)) return 1;
  if (result->type == NL_INTEGER)
    *result = nl_cell_as_symbol(nl_intern(strdup("t")));
  else
    *result = nl_cell_as_nil();
  return 0;
}
NL_BUILTIN(is_pair) {
  if (nl_evalq(state, cell.type == NL_PAIR ? cell.value.as_pair[0] : cell, result)) return 1;
  if (result->type == NL_PAIR)
    *result = nl_cell_as_symbol(nl_intern(strdup("t")));
  else
    *result = nl_cell_as_nil();
  return 0;
}
NL_BUILTIN(is_symbol) {
  if (nl_evalq(state, cell.type == NL_PAIR ? cell.value.as_pair[0] : cell, result)) return 1;
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
  case NL_PAIR: return nl_cell_equal(a.value.as_pair[0], b.value.as_pair[0])
      && nl_cell_equal(a.value.as_pair[1], b.value.as_pair[1]);
  default: return 0;
  }
}
NL_BUILTIN(letq);
NL_BUILTIN(call);
NL_BUILTIN(evalq) {
  switch (cell.type) {
  case NL_NIL:
  case NL_INTEGER:
    *result = cell;
    return 0;
  case NL_SYMBOL:
    nl_state_get(state, cell.value.as_symbol, result);
    return 0;
  case NL_PAIR:
    return nl_call(state, cell, result);
  default:
    state->last_err = "unknown cell type";
    return 1;
  }
}
NL_BUILTIN(call) {
  struct nl_cell head, letq_tag, *args, *vars, *params;
  if (nl_evalq(state, cell.value.as_pair[0], &head)) return 1;
 call_retry:
  switch (head.type) {
  case NL_NIL:
    state->last_err = "illegal function call: cannot invoke nil";
    return 1;
  case NL_INTEGER:
    return ((nl_native_func)head.value.as_integer)(state, cell.value.as_pair[1], result);
  case NL_SYMBOL:
    if (nl_evalq(state, head, &head)) return 1;
    goto call_retry;
  case NL_PAIR:
    if (head.value.as_pair[1].type != NL_PAIR) {
      state->last_err = "illegal lambda call: non-pair lambda body";
      return 1;
    }
    letq_tag = nl_cell_as_pair(nl_cell_as_nil(), head.value.as_pair[1]);
    vars = letq_tag.value.as_pair;
    switch (head.value.as_pair[0].type) {
    case NL_PAIR:
      args = cell.value.as_pair + 1;
      params = head.value.as_pair;
      *vars = nl_cell_as_pair(nl_cell_as_nil(), nl_cell_as_nil());
      for (;;) {
        if (args->type != NL_PAIR || params->type != NL_PAIR) {
          // out of args or params
          while (params->type == NL_PAIR) {
            vars->value.as_pair[0] = params->value.as_pair[0];
            vars->value.as_pair[1] = nl_cell_as_pair(nl_cell_as_nil(), nl_cell_as_pair(nl_cell_as_nil(), nl_cell_as_nil()));
            vars = vars->value.as_pair[1].value.as_pair + 1;
            params = params->value.as_pair + 1;
          }
          *vars = nl_cell_as_nil();
          return nl_letq(state, letq_tag, result);
        }
        vars->value.as_pair[0] = params->value.as_pair[0];
        vars->value.as_pair[1] = nl_cell_as_pair(args->value.as_pair[0], nl_cell_as_pair(nl_cell_as_nil(), nl_cell_as_nil()));
        vars = vars->value.as_pair[1].value.as_pair + 1;
        args = args->value.as_pair + 1;
        params = params->value.as_pair + 1;
      }
    case NL_SYMBOL:
      *vars = nl_cell_as_pair(head.value.as_pair[0],
                              nl_cell_as_pair(nl_cell_as_pair(nl_cell_as_symbol(nl_intern(strdup("quote"))),
                                                              cell.value.as_pair[1]),
                                              nl_cell_as_nil()));
    case NL_NIL:
      return nl_letq(state, letq_tag, result);
    default:
      state->last_err = "illegal lambda call: non-pair parameter list";
      return 1;
    }
  default:
    state->last_err = "unknown cell type";
    return 1;
  }
}
NL_BUILTIN(macro) {
  if (cell.type != NL_PAIR) {
    state->last_err = "illegal macro call: non-pair args";
    return 1;
  }
  if (cell.value.as_pair[0].type != NL_SYMBOL) {
    state->last_err = "illegal macro call: first arg should be symbol";
    return 1;
  }
  if (cell.value.as_pair[1].type != NL_PAIR) {
    state->last_err = "illegal macro call: non-pair args tail";
    return 1;
  }
  if (nl_evalq(state, cell.value.as_pair[1].value.as_pair[0], result)) return 1;
  cell = nl_cell_as_pair(cell.value.as_pair[0], *result);
  return nl_evalq(state, cell, result);
}
NL_BUILTIN(eval) {
  struct nl_cell *tail, form;
  if (cell.type != NL_PAIR) {
    state->last_err = "illegal eval: non-pair args";
    return 1;
  }
  for (tail = &cell; tail->type == NL_PAIR; tail = tail->value.as_pair + 1) {
    if (nl_evalq(state, tail->value.as_pair[0], &form)) return 1;
    if (nl_evalq(state, form, result)) return 1;
  }
  return 0;
}
NL_BUILTIN(equal) {
  struct nl_cell *tail, last, val;
  if (cell.type != NL_PAIR) {
    *result = nl_cell_as_symbol(nl_intern(strdup("t")));
    return 0;
  }
  if (nl_evalq(state, cell.value.as_pair[0], &last)) return 1;
  for (tail = cell.value.as_pair + 1; tail->type == NL_PAIR; tail = tail->value.as_pair + 1) {
    if (nl_evalq(state, tail->value.as_pair[0], &val)) return 1;
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
  for (p = &l; p->type == NL_PAIR; p = p->value.as_pair +1) {
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
          item_result = nl_compare(i->value.as_pair[0], j->value.as_pair[0]);
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
NL_BUILTIN(lt) {
  struct nl_cell *p, a, b;
  if (cell.type != NL_PAIR) {
    *result = nl_cell_as_nil();
    return 0;
  }
  if (nl_evalq(state, cell.value.as_pair[0], &a)) return 1;
  for (p = cell.value.as_pair + 1; p->type == NL_PAIR; p = p->value.as_pair + 1, a = b) {
    if (nl_evalq(state, p->value.as_pair[0], &b)) return 1;
    if (nl_compare(a, b) != -1) {
      *result = nl_cell_as_nil();
      return 0;
    }
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
  if (nl_evalq(state, cell.value.as_pair[0], &a)) return 1;
  for (p = cell.value.as_pair + 1; p->type == NL_PAIR; p = p->value.as_pair + 1, a = b) {
    if (nl_evalq(state, p->value.as_pair[0], &b)) return 1;
    if (nl_compare(a, b) != 1) {
      *result = nl_cell_as_nil();
      return 0;
    }
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
  if (nl_evalq(state, cell.value.as_pair[0], &a)) return 1;
  for (p = cell.value.as_pair + 1; p->type == NL_PAIR; p = p->value.as_pair + 1, a = b) {
    if (nl_evalq(state, p->value.as_pair[0], &b)) return 1;
    if (nl_compare(a, b) == 1) {
      *result = nl_cell_as_nil();
      return 0;
    }
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
  if (nl_evalq(state, cell.value.as_pair[0], &a)) return 1;
  for (p = cell.value.as_pair + 1; p->type == NL_PAIR; p = p->value.as_pair + 1, a = b) {
    if (nl_evalq(state, p->value.as_pair[0], &b)) return 1;
    if (nl_compare(a, b) == -1) {
      *result = nl_cell_as_nil();
      return 0;
    }
  }
  *result = nl_cell_as_symbol(nl_intern(strdup("t")));
  return 0;
}
NL_BUILTIN(not) {
  if (nl_evalq(state, cell.type == NL_PAIR ? cell.value.as_pair[0] : cell, result)) return 1;
  if (result->type == NL_NIL)
    *result = nl_cell_as_symbol(nl_intern(strdup("t")));
  else
    *result = nl_cell_as_nil();
  return 0;
}
NL_BUILTIN(head) {
  if (cell.type != NL_PAIR) {
    state->last_err = "invalid head: non-pair args";
    return 1;
  }
  if (cell.value.as_pair[1].type != NL_NIL) {
    state->last_err = "invalid head: too many args";
    return 1;
  }
  if (nl_evalq(state, cell.value.as_pair[0], result)) return 1;
  if (result->type == NL_PAIR) *result = result->value.as_pair[0];
  return 0;
}
NL_BUILTIN(tail) {
  if (cell.type != NL_PAIR) {
    state->last_err = "invalid tail: non-pair args";
    return 1;
  }
  if (cell.value.as_pair[1].type != NL_NIL) {
    state->last_err = "invalid tail: too many args";
    return 1;
  }
  if (nl_evalq(state, cell.value.as_pair[0], result)) return 1;
  if (result->type == NL_PAIR)
    *result = result->value.as_pair[1];
  else
    *result = nl_cell_as_nil();
  return 0;
}
NL_BUILTIN(cons) {
  if (cell.type != NL_PAIR) {
    state->last_err = "illegal cons: non-pair args";
    return 1;
  }
  *result = nl_cell_as_pair(nl_cell_as_nil(), nl_cell_as_nil());
  if (nl_evalq(state, cell.value.as_pair[0], result->value.as_pair)) return 1;
  if (cell.value.as_pair[1].type == NL_PAIR) {
    if (nl_evalq(state, cell.value.as_pair[1].value.as_pair[0], result->value.as_pair + 1)) return 1;
  } else if (nl_evalq(state, cell.value.as_pair[1], result->value.as_pair + 1)) return 1;
  return 0;
}
NL_BUILTIN(list) {
  struct nl_cell *in_tail, *out_tail = result;
  if (cell.type == NL_NIL) {
    *result = nl_cell_as_nil();
    return 0;
  }
  *result = nl_cell_as_pair(nl_cell_as_nil(), nl_cell_as_nil());
  for (in_tail = &cell; in_tail->type == NL_PAIR; in_tail = in_tail->value.as_pair + 1) {
    if (nl_evalq(state, in_tail->value.as_pair[0], out_tail->value.as_pair)) return 1;
    if (in_tail->value.as_pair[1].type != NL_PAIR) {
      return nl_evalq(state, in_tail->value.as_pair[1], out_tail->value.as_pair + 1);
    }
    out_tail->value.as_pair[1] = nl_cell_as_pair(nl_cell_as_nil(), nl_cell_as_nil());
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
    state->last_err = "illegal add: non-pair args";
    return 1;
  }
  if (nl_evalq(state, cell.value.as_pair[0], result)) return 1;
  if (result->type != NL_INTEGER) {
    state->last_err = "illegal add: non-integer arg";
    return 1;
  }
  for (tail = cell.value.as_pair + 1; tail->type == NL_PAIR; tail = tail->value.as_pair + 1) {
    if (nl_evalq(state, tail->value.as_pair[0], &val)) return 1;
    if (val.type != NL_INTEGER) {
      state->last_err = "illegal add: non-integer arg";
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
    state->last_err = "illegal sub: non-pair args";
    return 1;
  }
  if (cell.value.as_pair[1].type == NL_NIL) {
    *result = nl_cell_as_int(-cell.value.as_pair[0].value.as_integer);
    return 0;
  }
  if (nl_evalq(state, cell.value.as_pair[0], result)) return 1;
  if (result->type != NL_INTEGER) {
    state->last_err = "illegal sub: non-integer arg";
    return 1;
  }
  for (tail = cell.value.as_pair + 1; tail->type == NL_PAIR; tail = tail->value.as_pair + 1) {
    if (nl_evalq(state, tail->value.as_pair[0], &val)) return 1;
    if (val.type != NL_INTEGER) {
      state->last_err = "illegal sub: non-integer arg";
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
    state->last_err = "illegal mul: non-pair args";
    return 1;
  }
  for (tail = &cell; tail->type == NL_PAIR; tail = tail->value.as_pair + 1) {
    if (nl_evalq(state, tail->value.as_pair[0], &val)) return 1;
    if (val.type != NL_INTEGER) {
      state->last_err = "illegal mul: non-integer arg";
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
    state->last_err = "illegal div: non-pair args";
    return 1;
  }
  if (nl_evalq(state, cell.value.as_pair[0], result)) return 1;
  if (cell.value.as_pair[1].type == NL_NIL) {
    result->value.as_integer = 1 / result->value.as_integer;
    return 0;
  }
  for (tail = cell.value.as_pair + 1; tail->type == NL_PAIR; tail = tail->value.as_pair + 1) {
    if (nl_evalq(state, tail->value.as_pair[0], &val)) return 1;
    if (val.type != NL_INTEGER) {
      state->last_err = "illegal div: non-integer arg";
      return 1;
    }
    result->value.as_integer /= val.value.as_integer;
  }
  return 0;
}
NL_BUILTIN(printq) {
  switch (cell.type) {
  case NL_NIL:
    fprintf(state->stdout, "nil");
    *result = cell;
    return 0;
  case NL_INTEGER:
    fprintf(state->stdout, "%li", cell.value.as_integer);
    *result = cell;
    return 0;
  case NL_PAIR:
    if (nl_printq(state, cell.value.as_pair[0], result)) return 1;
    if (cell.value.as_pair[1].type == NL_NIL) return 0;
    fprintf(state->stdout, cell.value.as_pair[1].type == NL_PAIR ? " " : ", ");
    return nl_printq(state, cell.value.as_pair[1], result);
  case NL_SYMBOL:
    fprintf(state->stdout, "%s", cell.value.as_symbol);
    *result = cell;
    return 0;
  default:
    state->last_err = "unknown cell type";
    return 1;
  }
}
NL_BUILTIN(print) {
  struct nl_cell val, *tail;
  if (cell.type != NL_PAIR)
    return nl_evalq(state, cell, result) || nl_printq(state, cell, result);
  for (tail = &cell; tail->type == NL_PAIR; tail = tail->value.as_pair + 1) {
    if (nl_evalq(state, tail->value.as_pair[0], &val)) return 1;
    if (nl_printq(state, val, result)) return 1;
    if (tail->value.as_pair[1].type == NL_PAIR) fputc(' ', state->stdout);
  }
  if (tail->type != NL_NIL) {
    fprintf(state->stdout, ", ");
    nl_print(state, *tail, result);
  }
  return 0;
}
NL_BUILTIN(defq) {
  struct nl_cell name, body;
  *result = nl_cell_as_nil();
  if (cell.type != NL_PAIR) {
    state->last_err = "illegal defq: non-pair args";
    return 1;
  }
  name = cell.value.as_pair[0];
  if (name.type != NL_SYMBOL) {
    state->last_err = "illegal defq: non-symbol name";
    return 1;
  }
  body = cell.value.as_pair[1];
  if (body.type != NL_PAIR) {
    state->last_err = "illegal defq: non-pair body";
    return 1;
  }
  nl_state_put(state, name.value.as_symbol, body);
  return 0;
}
int nl_setqe(struct nl_state *target_state, struct nl_state *eval_state, struct nl_cell args, struct nl_cell *result) {
  struct nl_cell *tail;
  if (args.type != NL_PAIR) {
    target_state->last_err = "illegal setq call: non-pair args";
    return 1;
  }
  for (tail = &args; tail->type == NL_PAIR; tail = tail->value.as_pair[1].value.as_pair + 1) {
    if (tail->value.as_pair[0].type != NL_SYMBOL) {
      target_state->last_err = "illegal setq call: non-symbol var";
      return 1;
    }
    if (tail->value.as_pair[1].type != NL_PAIR) {
      if (nl_evalq(eval_state, tail->value.as_pair[1], result)) return 1;
      nl_state_put(target_state, tail->value.as_pair[0].value.as_symbol, *result);
      return 0;
    }
    if (nl_evalq(eval_state, tail->value.as_pair[1].value.as_pair[0], result)) return 1;
    nl_state_put(target_state, tail->value.as_pair[0].value.as_symbol, *result);
  }
  return 0;
}
NL_BUILTIN(setq) {
  return nl_setqe(state, state, cell, result);
}
NL_BUILTIN(set) {
  struct nl_cell *tail, var;
  if (cell.type != NL_PAIR) {
    state->last_err = "illegal set call: non-pair args";
    return 1;
  }
  for (tail = &cell; tail->type == NL_PAIR; tail = tail->value.as_pair[1].value.as_pair + 1) {
    if (nl_evalq(state, tail->value.as_pair[0], &var)) return 1;
    if (var.type != NL_SYMBOL) {
      state->last_err = "illegal set call: non-symbol var";
      return 1;
    }
    if (tail->value.as_pair[1].type != NL_PAIR) {
      if (nl_evalq(state, tail->value.as_pair[1], result)) return 1;
      nl_state_put(state, var.value.as_symbol, *result);
      return 0;
    }
    if (nl_evalq(state, tail->value.as_pair[1].value.as_pair[0], result)) return 1;
    nl_state_put(state, var.value.as_symbol, *result);
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
 * Evaluate each statement in the remaining args.
 * Discard the symbols list.
 * Result is the last evaluated statement.
 */
NL_BUILTIN(letq) {
  struct nl_state body_state;
  struct nl_cell vars, body, *tail;
  if (cell.type != NL_PAIR) {
    state->last_err = "illegal letq: non-pair args";
    return 1;
  }
  vars = cell.value.as_pair[0];
  body = cell.value.as_pair[1];
  if (body.type != NL_PAIR) {
    state->last_err = "illegal letq: non-pair body";
    return 1;
  }
  nl_state_init(&body_state);
  if (vars.type == NL_PAIR) {
    if (nl_setqe(&body_state, state, vars, result)) return 1;
  }
  nl_state_link(&body_state, state);
  for (tail = &body; tail->type == NL_PAIR; tail = tail->value.as_pair + 1) {
    if (nl_evalq(&body_state, tail->value.as_pair[0], result)) return 1;
  }
  if (tail->type != NL_NIL) return nl_evalq(&body_state, *tail, result);
  return 0;
}
NL_BUILTIN(writeq) {
  struct nl_cell *tail;
  switch (cell.type) {
  case NL_NIL:
    fprintf(state->stdout, "nil");
    *result = cell;
    return 0;
  case NL_INTEGER:
    fprintf(state->stdout, "%li", cell.value.as_integer);
    *result = cell;
    return 0;
  case NL_SYMBOL:
    fprintf(state->stdout, "%s", cell.value.as_symbol);
    *result = cell;
    return 0;
  case NL_PAIR:
    fputc('(', state->stdout);
    if (nl_writeq(state, cell.value.as_pair[0], result)) return 1;
    tail = cell.value.as_pair + 1;
    for (;;) {
      switch (tail->type) {
      case NL_NIL:
        fputc(')', state->stdout);
        *result = cell;
        return 0;
      case NL_PAIR:
        fputc(' ', state->stdout);
        if (nl_writeq(state, tail->value.as_pair[0], result)) return 1;
        tail = tail->value.as_pair + 1;
        break;
      case NL_INTEGER:
        fprintf(state->stdout, " . %li)", tail->value.as_integer);
        return 0;
      case NL_SYMBOL:
        // TODO write quoted symbols
        fprintf(state->stdout, " . %s)", tail->value.as_symbol);
        return 0;
      }
    }
  }
  *result = cell;
  state->last_err = "unhandled type";
  return 1;
}
NL_BUILTIN(write) {
  if (cell.type != NL_PAIR) {
    state->last_err = "illegal write call: non-pair args";
    return 1;
  }
  if (nl_evalq(state, cell.value.as_pair[0], result)) return 1;
  return nl_writeq(state, *result, result);
}
NL_BUILTIN(and) {
  struct nl_cell *tail;
  if (cell.type != NL_PAIR) {
    state->last_err = "illegal and: non-pair args";
    return 1;
  }
  for (tail = &cell; tail->type == NL_PAIR; tail = tail->value.as_pair + 1) {
    if (nl_evalq(state, tail->value.as_pair[0], result)) return 1;
    if (result->type == NL_NIL) return 0;
  }
  return 0;
}
NL_BUILTIN(or) {
  struct nl_cell *tail;
  if (cell.type != NL_PAIR) {
    state->last_err = "illegal or: non-pair args";
    return 1;
  }
  for (tail = &cell; tail->type == NL_PAIR; tail = tail->value.as_pair + 1) {
    if (nl_evalq(state, tail->value.as_pair[0], result)) return 1;
    if (result->type != NL_NIL) return 0;
  }
  return 0;
}
#define NL_FOREACH(start, a) for (a = start; a->type == NL_PAIR; a = a->value.as_pair + 1)
NL_BUILTIN(write_bytes) {
  struct nl_cell *a;
  switch (cell.type) {
  case NL_INTEGER:
    fputc((char)cell.value.as_integer, state->stdout);
  case NL_NIL:
    *result = cell;
    return 0;
  case NL_SYMBOL:
    return nl_evalq(state, cell, result)
      || nl_write_bytes(state, *result, result);
  case NL_PAIR:
    NL_FOREACH(&cell, a) {
      if (nl_evalq(state, a->value.as_pair[0], result)) return 1;
      if (nl_write_bytes(state, *result, result)) return 1;
    }
    return nl_write_bytes(state, *a, result);
  }
  state->last_err = "unknown cell type";
  return 1;
}
NL_BUILTIN(write_wbytes) {
  struct nl_cell *a;
  switch (cell.type) {
  case NL_INTEGER:
    fputwc((wchar_t)cell.value.as_integer, state->stdout);
  case NL_NIL:
    *result = cell;
    return 0;
  case NL_SYMBOL:
    return nl_evalq(state, cell, result)
      || nl_write_wbytes(state, *result, result);
  case NL_PAIR:
    NL_FOREACH(&cell, a) {
      if (nl_evalq(state, a->value.as_pair[0], result)) return 1;
      if (nl_write_wbytes(state, *result, result)) return 1;
    }
    return nl_write_wbytes(state, *a, result);
  }
  state->last_err = "unknown cell type";
  return 1;
}
#define NL_DEF_BUILTIN(sym, name) nl_state_put(state, nl_intern(strdup(sym)), nl_cell_as_int((int64_t)nl_ ## name))
void nl_state_define_builtins(struct nl_state *state) {
  NL_DEF_BUILTIN("pair?", is_pair);
  NL_DEF_BUILTIN("nil?", is_nil);
  NL_DEF_BUILTIN("symbol?", is_symbol);
  NL_DEF_BUILTIN("integer?", is_integer);
  NL_DEF_BUILTIN("quote", quote);
  NL_DEF_BUILTIN("printq", printq);
  NL_DEF_BUILTIN("print", print);
  NL_DEF_BUILTIN("setq", setq);
  NL_DEF_BUILTIN("set", set);
  NL_DEF_BUILTIN("letq", letq);
  NL_DEF_BUILTIN("defq", defq);
  NL_DEF_BUILTIN("eval", eval);
  NL_DEF_BUILTIN("list", list);
  NL_DEF_BUILTIN("head", head);
  NL_DEF_BUILTIN("tail", tail);
  NL_DEF_BUILTIN("macro", macro);
  NL_DEF_BUILTIN("cons", cons);
  NL_DEF_BUILTIN("and", and);
  NL_DEF_BUILTIN("or", or);
  NL_DEF_BUILTIN("writeq", writeq);
  NL_DEF_BUILTIN("write", write);
  NL_DEF_BUILTIN("not", not);
  NL_DEF_BUILTIN("=", equal);
  NL_DEF_BUILTIN(">", gt);
  NL_DEF_BUILTIN("<", lt);
  NL_DEF_BUILTIN(">=", gte);
  NL_DEF_BUILTIN("<=", lte);
  NL_DEF_BUILTIN("+", add);
  NL_DEF_BUILTIN("-", sub);
  NL_DEF_BUILTIN("*", mul);
  NL_DEF_BUILTIN("/", div);
  NL_DEF_BUILTIN("write-bytes", write_bytes);
  NL_DEF_BUILTIN("write-wbytes", write_wbytes);
}
// Main REPL
// =========
int nl_run_repl(struct nl_state *state) {
  struct nl_cell last_read, last_eval;
  for (;;) {
    fprintf(state->stdout, "\n> ");
    if (nl_read(state, &last_read)) {
      if (state->last_err)
        fprintf(state->stderr, "ERROR read: %s\n", state->last_err);
      else
        fputs("ERROR read\n", state->stderr);
      return 1;
    }
    if (nl_evalq(state, last_read, &last_eval)) {
      if (state->last_err)
        fprintf(state->stderr, "ERROR eval: %s\n", state->last_err);
      else
        fputs("ERROR eval\n", state->stderr);
      return 2;
    }
    fputs("; ", state->stdout);
    nl_writeq(state, last_eval, &last_read);
  }
}
int main() {
  struct nl_state state;
  nl_state_init(&state);
  nl_state_define_builtins(&state);
  return nl_run_repl(&state);
}

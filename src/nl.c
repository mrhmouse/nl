#include "nl.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <dlfcn.h>
#include <gc.h>
static struct nl_cell nil, t, quote, unquote, nl_in, nl_out, nl_err;
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
  NL_HEAD(c) = head;
  NL_TAIL(c) = tail;
  return c;
}
struct nl_cell nl_cell_as_symbol(char *interned_symbol) {
  struct nl_cell c;
  c.type = NL_SYMBOL;
  c.value.as_symbol = interned_symbol;
  return c;
}
int64_t nl_list_length(struct nl_cell l) {
  int64_t len = 0;
  struct nl_cell *p;
  if (l.type == NL_PAIR) {
    NL_FOREACH(&l, p) {
      ++len;
    }
    if (p->type != NL_NIL) ++len;
  }
  return len;
}
void nl_scope_init(struct nl_scope *scope) {
  scope->last_err = NULL;
  scope->parent_scope = NULL;
  scope->symbols = GC_malloc(sizeof(*scope->symbols));
  scope->symbols->name = nil.value.as_symbol;
  scope->symbols->value.type = NL_NIL;
  scope->symbols->next = NULL;
}
int nl_skip_whitespace(FILE *in) {
  int ch;
  do {
    ch = fgetc(in);
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
int nl_read(struct nl_scope *scope, FILE *s_in, struct nl_cell *result) {
  struct nl_cell head, *tail;
  int ch, sign = 1, used = 0, allocated = 16;
  char *buf;
 start:
  ch = nl_skip_whitespace(s_in);
  if (ch == EOF) {
    return EOF;
  } else if (ch == '#') {
    do { ch = fgetc(s_in); }
    while (ch != '\n');
    goto start;
  } else if (ch == '-') {
    int peek = fgetc(s_in);
    if (isdigit(peek)) {
      sign = -1;
      ch = peek;
      goto NL_READ_DIGIT;
    }
    ungetc(peek, s_in);
    goto NL_READ_SYMBOL;
  } else if (isdigit(ch)) {
  NL_READ_DIGIT:
    *result = nl_cell_as_int(ch - '0');
    while (isdigit(ch = fgetc(s_in))) {
      result->value.as_integer *= 10;
      result->value.as_integer += ch - '0';
    }
    result->value.as_integer *= sign;
    ungetc(ch, s_in);
    return 0;
  } else if ('"' == ch) {
    buf = malloc(sizeof(char) * allocated);
    for (ch = fgetc(s_in); ch != '"'; ch = fgetc(s_in)) {
      if (ch == '\\')
        buf[used++] = fgetc(s_in);
      else
        buf[used++] = ch;
      if (used == allocated) {
        allocated *= 2;
        buf = realloc(buf, sizeof(char) * allocated);
      }
    }
    buf[used] = '\0';
    buf = realloc(buf, sizeof(char) * used);
    if (used == 0) {
      free(buf);
      *result = nil;
    } else {
      *result = nl_cell_as_symbol(nl_intern(buf));
    }
    return 0;
  } else if ('\'' == ch) {
    if (nl_read(scope, s_in, &head)) return 1;
    *result = nl_cell_as_pair(quote, head);
    return 0;
  } else if (',' == ch) {
    if (nl_read(scope, s_in, &head)) return 1;
    *result = nl_cell_as_pair(unquote, head);
    return 0;
  } else if ('(' == ch) {
    ch = nl_skip_whitespace(s_in);
    if (ch == ')') {
      *result = nil;
      return 0;
    }
    ungetc(ch, s_in);
    if (nl_read(scope, s_in, &head)) return 1;
    *result = nl_cell_as_pair(head, nil);
    tail = NL_NEXT_AT(result);
    for (;;) {
      ch = nl_skip_whitespace(s_in);
      if (ch == ')') return 0;
      if (ch == '.') {
        if (nl_read(scope, s_in, tail)) return 1;
        if (nl_skip_whitespace(s_in) != ')') {
          scope->last_err = "illegal list";
          return 1;
        }
        return 0;
      }
      ungetc(ch, s_in);
      if (nl_read(scope, s_in, &head)) return 1;
      *tail = nl_cell_as_pair(head, nil);
      tail = NL_NEXT_AT(tail);
    }
  } else {
  NL_READ_SYMBOL:
    buf = malloc(sizeof(char) * allocated);
    for (; !isspace(ch) && ch != '(' && ch != ')'; ch = fgetc(s_in)) {
      buf[used++] = ch;
      if (used == allocated) {
        allocated *= 2;
        buf = realloc(buf, sizeof(char) * allocated);
      }
    }
    ungetc(ch, s_in);
    buf[used] = '\0';
    buf = realloc(buf, sizeof(char) * used);
    *result = nl_cell_as_symbol(nl_intern(buf));
    return 0;
  }
}
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
void nl_scope_get(struct nl_scope *scope, char *name, struct nl_cell *result) {
  struct nl_scope_symbols *s;
  for (; scope != NULL; scope = scope->parent_scope) {
    for (s = scope->symbols; s != NULL; s = s->next)
      if (name == s->name) {
        *result = s->value;
        return;
      }
  }
  *result = nil;
}
int nl_setqe(struct nl_scope *target_scope, struct nl_scope *eval_scope, struct nl_cell args, struct nl_cell *result) {
  struct nl_cell *tail;
  if (args.type != NL_PAIR) {
    target_scope->last_err = "illegal setq call: non-pair args";
    return 1;
  }
  for (tail = &args; tail->type == NL_PAIR; tail = NL_NEXT(NL_TAIL_AT(tail))) {
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
NL_BUILTIN(call) {
  struct nl_cell *p, *a, v, head;
  struct nl_scope call_scope;
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal call: non-pair args";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &head)) return 1;
 retry:
  switch (head.type) {
  case NL_SYMBOL:
    nl_scope_get(scope, head.value.as_symbol, &head);
    goto retry;
  case NL_INTEGER:
    return ((nl_native_func)head.value.as_integer)(scope, NL_TAIL(cell), result);
  case NL_NIL:
    scope->last_err = "illegal call: cannot invoke nil";
    return 1;
  default:
    break;
  }
  nl_scope_init(&call_scope);
  switch (NL_HEAD(head).type) {
  case NL_SYMBOL:
    nl_scope_put(&call_scope, NL_HEAD(head).value.as_symbol, NL_TAIL(cell));
    break;
  case NL_PAIR:
    a = &NL_TAIL(cell);
    NL_FOREACH(&NL_HEAD(head), p) {
      if (NL_HEAD_AT(p).type != NL_SYMBOL) {
        scope->last_err = "illegal call: non-symbol parameter in lambda";
        return 1;
      }
      if (a->type == NL_PAIR) {
        if (nl_evalq(scope, NL_HEAD_AT(a), &v)) return 1;
        nl_scope_put(&call_scope, NL_HEAD_AT(p).value.as_symbol, v);
        a = NL_NEXT_AT(a);
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
  call_scope.parent_scope = scope;
  NL_FOREACH(&NL_TAIL(head), p) {
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
int nl_compare(struct nl_cell a, struct nl_cell b) {
  struct nl_cell *i, *j;
  int item_result;
  int64_t a_len, b_len;
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
        for (i = &a, j = &b; i->type == NL_PAIR && j->type == NL_PAIR; i = NL_NEXT_AT(i), j = NL_NEXT_AT(j)) {
          item_result = nl_compare(NL_HEAD_AT(i), NL_HEAD_AT(j));
          if (item_result) return item_result;
        }
        return nl_compare(*i, *j);
      }
      if (a_len < b_len) return -1;
      return 1;
    }
  if (a.type == NL_NIL) return -1;
  if (b.type == NL_NIL) return 1;
  if (a.type == NL_INTEGER) return -1;
  if (b.type == NL_INTEGER) return 1;
  if (a.type == NL_SYMBOL) return -1;
  if (b.type == NL_SYMBOL) return 1;
  if (a.type == NL_PAIR) return -1;
  return 1;
}
void nl_scope_define_builtins(struct nl_scope *scope) {
  nl_scope_put(scope, nl_in.value.as_symbol, nl_cell_as_int((int64_t)stdin));
  nl_scope_put(scope, nl_out.value.as_symbol, nl_cell_as_int((int64_t)stdout));
  nl_scope_put(scope, nl_err.value.as_symbol, nl_cell_as_int((int64_t)stderr));
  NL_DEF_BUILTIN("load", load);
  NL_DEF_BUILTIN("load-native", loadnative);
  NL_DEF_BUILTIN("quote", quote);
}
NL_BUILTIN(quote) {
  *result = cell;
  return 0;
}
NL_BUILTIN(load) {
  struct nl_cell last_read, c_in;
  FILE *in;
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal load";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &c_in)) return 1;
  if (c_in.type != NL_SYMBOL) {
    scope->last_err = "illegal load: expected pathname";
    return 1;
  }
  in = fopen(c_in.value.as_symbol, "r");
  if (!in) in = stdin;
  for (;;) {
    if (nl_read(scope, in, &last_read) == EOF) break;
    if (nl_evalq(scope, last_read, result)) return 1;
  }
  return 0;
}
NL_BUILTIN(loadnative) {
  void *lib, *f;
  struct nl_cell name, *n;
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal load-native: need a list";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &name)) return 1;
  if (name.type != NL_SYMBOL) {
    scope->last_err = "illegal load-native: first arg should be a symbol";
    return 1;
  }
  lib = dlopen(name.value.as_symbol, RTLD_LAZY);
  if (!lib) {
    *result = nil;
    return 0;
  }
  NL_FOREACH(&NL_TAIL(cell), n) {
    if (NL_HEAD_AT(n).type != NL_PAIR
        || NL_HEAD(NL_HEAD_AT(n)).type != NL_SYMBOL
        || NL_TAIL(NL_HEAD_AT(n)).type != NL_SYMBOL) {
      scope->last_err = "illegal load-native: expected pair of symbols";
      return 1;
    }
    f = dlsym(lib, NL_HEAD(NL_HEAD_AT(n)).value.as_symbol);
    if (!f) {
      *result = nil;
      return 0;
    }
    nl_scope_put(scope, NL_TAIL(NL_HEAD_AT(n)).value.as_symbol, nl_cell_as_int((int64_t)f));
  }
  *result = t;
  return 0;
}
int nl_run_repl(int interactive, struct nl_scope *scope) {
  struct nl_cell last_read, last_eval, c_in, c_out, c_err;
  FILE *s_in = stdin, *s_out = stdout, *s_err = stderr;
  for (;;) {
    if (!nl_evalq(scope, nl_in, &c_in)
        && c_in.type == NL_INTEGER)
      s_in = (FILE *)c_in.value.as_integer;
    if (!nl_evalq(scope, nl_out, &c_out)
        && c_out.type == NL_INTEGER)
      s_out = (FILE *)c_out.value.as_integer;
    if (!nl_evalq(scope, nl_err, &c_err)
        && c_err.type == NL_INTEGER)
      s_err = (FILE *)c_err.value.as_integer;
    if (interactive)
      fprintf(s_out, "\n> ");
    if (nl_read(scope, s_in, &last_read)) {
      if (scope->last_err)
        fprintf(s_err, "ERROR read: %s\n", scope->last_err);
      else
        fputs("ERROR read\n", s_err);
      return 1;
    }
    if (nl_evalq(scope, last_read, &last_eval)) {
      if (scope->last_err)
        fprintf(s_err, "ERROR eval: %s\n", scope->last_err);
      else
        fputs("ERROR eval\n", s_err);
      return 2;
    }
    if (interactive) {
      fputs("; ", s_out);
      nl_writeq(scope, last_eval, &last_read);
    }
  }
}
void nl_globals_init() {
  nil = nl_cell_as_nil();
  t = nl_cell_as_symbol(nl_intern(strdup("t")));
  quote = nl_cell_as_symbol(nl_intern(strdup("quote")));
  unquote = nl_cell_as_symbol(nl_intern(strdup("unquote")));
  nl_in  = nl_cell_as_symbol(nl_intern(strdup("*In")));
  nl_out = nl_cell_as_symbol(nl_intern(strdup("*Out")));
  nl_err = nl_cell_as_symbol(nl_intern(strdup("*Err")));
}

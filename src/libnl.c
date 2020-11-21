#include "nl.h"
#include <dlfcn.h>
#include <gc.h>
#define NL_BUILTIN(name) int nl_ ## name(struct nl_scope *scope, struct nl_cell cell, struct nl_cell *result)
#define NL_DEF_BUILTIN(sym, name) nl_scope_put(scope, nl_intern(strdup(sym)), nl_cell_as_int((int64_t)nl_ ## name))
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
  NL_FOREACH(&l, p) {
    ++len;
  }
  if (p->type != NL_NIL) ++len;
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
NL_BUILTIN(evalq);
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
    nl_scope_get(scope, NL_HEAD(cell).value.as_symbol, &NL_HEAD(cell));
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
    *result = t;
  else
    *result = nil;
  return 0;
}
NL_BUILTIN(is_integer) {
  if (nl_evalq(scope, cell.type == NL_PAIR ? NL_HEAD(cell) : cell, result)) return 1;
  if (result->type == NL_INTEGER)
    *result = t;
  else
    *result = nil;
  return 0;
}
NL_BUILTIN(is_pair) {
  if (nl_evalq(scope, cell.type == NL_PAIR ? NL_HEAD(cell) : cell, result)) return 1;
  if (result->type == NL_PAIR)
    *result = t;
  else
    *result = nil;
  return 0;
}
NL_BUILTIN(is_symbol) {
  if (nl_evalq(scope, cell.type == NL_PAIR ? NL_HEAD(cell) : cell, result)) return 1;
  if (result->type == NL_SYMBOL)
    *result = t;
  else
    *result = nil;
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
    *result = nil;
    return 0;
  }
  if (list.type != NL_PAIR) {
    scope->last_err = "illegal foreach: expected a pair";
    return 1;
  }
  NL_FOREACH(&list, a) {
    call = nl_cell_as_pair(fun, nl_cell_as_pair(nl_cell_as_pair(quote, NL_HEAD_AT(a)), nil));
    if (nl_evalq(scope, call, result)) return 1;
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
  *result = nl_cell_as_pair(nil, nil);
  NL_FOREACH(&list, item) {
    call = nl_cell_as_pair(fun, nl_cell_as_pair(nl_cell_as_pair(quote, NL_HEAD_AT(item)), nil));
    if (nl_evalq(scope, call, result->value.as_pair)) return 1;
    switch (NL_TAIL_AT(item).type) {
    case NL_NIL:
      break;
    case NL_PAIR:
      NL_TAIL_AT(result) = nl_cell_as_pair(nil, nil);
      result = NL_NEXT_AT(result);
      break;
    default:
      call = nl_cell_as_pair(fun, nl_cell_as_pair(nl_cell_as_pair(quote, NL_TAIL_AT(item)), nil));
      return nl_evalq(scope, call, NL_NEXT_AT(result));
    }
  }
  return 0;
}
NL_BUILTIN(mappair) {
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
  *result = nl_cell_as_pair(nil, nil);
  NL_FOREACH(&list, item) {
    call = nl_cell_as_pair(fun, nl_cell_as_pair(nl_cell_as_pair(quote, *item), nil));
    if (nl_evalq(scope, call, result->value.as_pair)) return 1;
    if (NL_TAIL_AT(item).type == NL_PAIR) {
      NL_TAIL_AT(result) = nl_cell_as_pair(nil, nil);
      result = NL_NEXT_AT(result);
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
  *result = nl_cell_as_pair(nil, nil);
  NL_FOREACH(&list, item) {
    call = nl_cell_as_pair(fun, nl_cell_as_pair(nl_cell_as_pair(quote, NL_HEAD_AT(item)), nil));
    if (nl_evalq(scope, call, result->value.as_pair)) return 1;
    if (NL_HEAD_AT(result).type != NL_NIL) {
      NL_HEAD_AT(result) = NL_HEAD_AT(item);
      NL_TAIL_AT(result) = nl_cell_as_pair(nil, nil);
      result = NL_NEXT_AT(result);
    }
  }
  *result = nil;
  return 0;
}
NL_BUILTIN(fold) {
  struct nl_cell fun, list, *item, call;
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal fold: non-pair args";
    return 1;
  }
  if (NL_TAIL(cell).type != NL_PAIR) {
    scope->last_err = "illegal fold: expected at least three args in list";
    return 1;
  }
  if (NL_TAIL(NL_TAIL(cell)).type != NL_PAIR) {
    scope->last_err = "illegal fold: expected at least three args in list";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &fun)) return 1;
  if (nl_evalq(scope, NL_HEAD(NL_TAIL(cell)), result)) return 1;
  if (nl_evalq(scope, NL_HEAD(NL_TAIL(NL_TAIL(cell))), &list)) return 1;
  if (list.type == NL_NIL) {
    return 0;
  }
  if (list.type != NL_PAIR) {
    scope->last_err = "illegal fold: third argument should be a pair";
    return 1;
  }
  NL_FOREACH(&list, item) {
    call = nl_cell_as_pair(fun, nl_cell_as_pair(nl_cell_as_pair(quote, NL_HEAD_AT(item)), nl_cell_as_pair(nl_cell_as_pair(quote, *result), nil)));
    if (nl_evalq(scope, call, result)) return 1;
  }
  return 0;
}
NL_BUILTIN(unfold) {
  struct nl_cell seed, pair_f, continue_f, next_seed_f, call, v;
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal unfold: non-pair args";
    return 1;
  }
  if (nl_list_length(cell) != 5) {
    scope->last_err = "illegal unfold: expected exactly five args";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &seed)
      || nl_evalq(scope, NL_HEAD(NL_TAIL(cell)), result)
      || nl_evalq(scope, NL_HEAD(NL_TAIL(NL_TAIL(cell))), &pair_f)
      || nl_evalq(scope, NL_HEAD(NL_TAIL(NL_TAIL(NL_TAIL(cell)))), &continue_f)
      || nl_evalq(scope, NL_HEAD(NL_TAIL(NL_TAIL(NL_TAIL(NL_TAIL(cell))))), &next_seed_f))
    return 1;
  for (;;) {
    call = nl_cell_as_pair(continue_f, nl_cell_as_pair(nl_cell_as_pair(quote, seed), nil));
    if (nl_evalq(scope, call, &v)) return 1;
    if (v.type == NL_NIL) break;
    call = nl_cell_as_pair(pair_f, nl_cell_as_pair(nl_cell_as_pair(quote, seed), nl_cell_as_pair(nl_cell_as_pair(quote, *result), nil)));
    if (nl_evalq(scope, call, result)) return 1;
    call = nl_cell_as_pair(next_seed_f, nl_cell_as_pair(nl_cell_as_pair(quote, seed), nil));
    if (nl_evalq(scope, call, &seed)) return 1;
  }
  return 0;
}
NL_BUILTIN(equal) {
  struct nl_cell *tail, last, val;
  if (cell.type != NL_PAIR) {
    *result = t;
    return 0;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &last)) return 1;
  NL_FOREACH(NL_NEXT(cell), tail) {
    if (nl_evalq(scope, NL_HEAD_AT(tail), &val)) return 1;
    if (!nl_cell_equal(last, val)) {
      *result = nil;
      return 0;
    }
  }
  *result = t;
  return 0;
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
    *result = nil;
    return 0;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &a)) return 1;
  NL_FOREACH(NL_NEXT(cell), p) {
    if (nl_evalq(scope, NL_HEAD_AT(p), &b)) return 1;
    if (nl_compare(a, b) != -1) {
      *result = nil;
      return 0;
    }
    a = b;
  }
  *result = t;
  return 0;
}
NL_BUILTIN(gt) {
  struct nl_cell *p, a, b;
  if (cell.type != NL_PAIR) {
    *result = nil;
    return 0;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &a)) return 1;
  NL_FOREACH(NL_NEXT(cell), p) {
    if (nl_evalq(scope, NL_HEAD_AT(p), &b)) return 1;
    if (nl_compare(a, b) != 1) {
      *result = nil;
      return 0;
    }
    a = b;
  }
  *result = t;
  return 0;
}
NL_BUILTIN(lte) {
  struct nl_cell *p, a, b;
  if (cell.type != NL_PAIR) {
    *result = nil;
    return 0;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &a)) return 1;
  NL_FOREACH(NL_NEXT(cell), p) {
    if (nl_evalq(scope, NL_HEAD_AT(p), &b)) return 1;
    if (nl_compare(a, b) == 1) {
      *result = nil;
      return 0;
    }
    a = b;
  }
  *result = t;
  return 0;
}
NL_BUILTIN(gte) {
  struct nl_cell *p, a, b;
  if (cell.type != NL_PAIR) {
    *result = nil;
    return 0;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &a)) return 1;
  NL_FOREACH(NL_NEXT(cell), p) {
    if (nl_evalq(scope, NL_HEAD_AT(p), &b)) return 1;
    if (nl_compare(a, b) == -1) {
      *result = nil;
      return 0;
    }
    a = b;
  }
  *result = t;
  return 0;
}
NL_BUILTIN(not) {
  if (nl_evalq(scope, cell.type == NL_PAIR ? NL_HEAD(cell) : cell, result)) return 1;
  if (result->type == NL_NIL)
    *result = t;
  else
    *result = nil;
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
    *result = nil;
  return 0;
}
NL_BUILTIN(pair) {
  if (cell.type != NL_PAIR) {
    scope->last_err = "illegal pair: non-pair args";
    return 1;
  }
  *result = nl_cell_as_pair(nil, nil);
  if (nl_evalq(scope, NL_HEAD(cell), result->value.as_pair)) return 1;
  if (NL_TAIL(cell).type == NL_PAIR) {
    if (nl_evalq(scope, NL_HEAD(NL_TAIL(cell)), NL_NEXT_AT(result))) return 1;
  } else if (nl_evalq(scope, NL_TAIL(cell), NL_NEXT_AT(result))) return 1;
  return 0;
}
NL_BUILTIN(list) {
  struct nl_cell *in_tail, *out_tail = result;
  if (cell.type == NL_NIL) {
    *result = nil;
    return 0;
  }
  *result = nl_cell_as_pair(nil, nil);
  NL_FOREACH(&cell, in_tail) {
    if (nl_evalq(scope, NL_HEAD_AT(in_tail), out_tail->value.as_pair)) return 1;
    if (NL_TAIL_AT(in_tail).type != NL_PAIR) {
      return nl_evalq(scope, NL_TAIL_AT(in_tail), NL_NEXT_AT(out_tail));
    }
    NL_TAIL_AT(out_tail) = nl_cell_as_pair(nil, nil);
    out_tail = NL_NEXT_AT(out_tail);
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
  NL_FOREACH(NL_NEXT(cell), tail) {
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
  NL_FOREACH(NL_NEXT(cell), tail) {
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
  NL_FOREACH(NL_NEXT(cell), tail) {
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
  struct nl_cell s_out;
  FILE *out = stdout;
  if (!nl_evalq(scope, nl_out, &s_out)
      && s_out.type == NL_INTEGER)
    out = (FILE *)s_out.value.as_integer;
  switch (cell.type) {
  case NL_NIL:
    fprintf(out, "nil");
    *result = cell;
    return 0;
  case NL_INTEGER:
    fprintf(out, "%li", cell.value.as_integer);
    *result = cell;
    return 0;
  case NL_PAIR:
    if (nl_printq(scope, NL_HEAD(cell), result)) return 1;
    if (NL_TAIL(cell).type == NL_NIL) return 0;
    fprintf(out, NL_TAIL(cell).type == NL_PAIR ? " " : ", ");
    return nl_printq(scope, NL_TAIL(cell), result);
  case NL_SYMBOL:
    fprintf(out, "%s", cell.value.as_symbol);
    *result = cell;
    return 0;
  default:
    scope->last_err = "unknown cell type";
    return 1;
  }
}
NL_BUILTIN(print) {
  struct nl_cell val, *tail, s_out;
  FILE *out = stdout;
  if (!nl_evalq(scope, nl_out, &s_out)
      && s_out.type == NL_INTEGER)
    out = (FILE *)s_out.value.as_integer;
  if (cell.type != NL_PAIR)
    return nl_evalq(scope, cell, result) || nl_printq(scope, cell, result);
  NL_FOREACH(&cell, tail) {
    if (nl_evalq(scope, NL_HEAD_AT(tail), &val)) return 1;
    if (nl_printq(scope, val, result)) return 1;
    if (NL_TAIL_AT(tail).type == NL_PAIR) fputc(' ', out);
  }
  if (tail->type != NL_NIL) {
    fprintf(out, ", ");
    nl_print(scope, *tail, result);
  }
  return 0;
}
NL_BUILTIN(defq) {
  struct nl_cell name, body;
  *result = nil;
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
  for (tail = &cell; tail->type == NL_PAIR; tail = NL_NEXT(NL_TAIL_AT(tail))) {
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
void nl_write_symbol(FILE *out, const char *sym) {
  const char *s = sym;
  switch (*sym) {
  case '.':
  case ',':
  case '\'':
  case '"':
  case '#':
    goto quoted;
  default:
    if (isdigit(*s)) goto quoted;
    break;
  }
  for (; *s != '\0'; ++s) {
    if (isspace(*s) || *s == '(' || *s == ')')
      goto quoted;
  }
  fprintf(out, "%s", sym);
  return;
 quoted:
  fprintf(out, "\"%.*s", (int)(s - sym), sym);
  fputc(*s, out);
  for (sym = ++s; *s != '\0'; ++s) {
    if (*s == '\\' || *s == '"') {
      fprintf(out, "%.*s\\%c", (int)(s - sym), sym, *s);
      sym = s+1;
    }
  }
  fprintf(out, "%s\"", sym);
}
NL_BUILTIN(writeq) {
  struct nl_cell *tail, s_out;
  FILE *out = stdout;
  if (!nl_evalq(scope, nl_out, &s_out)
      && s_out.type == NL_INTEGER)
    out = (FILE *)s_out.value.as_integer;
  switch (cell.type) {
  case NL_NIL:
    fprintf(out, "nil");
    *result = cell;
    return 0;
  case NL_INTEGER:
    fprintf(out, "%li", cell.value.as_integer);
    *result = cell;
    return 0;
  case NL_SYMBOL:
    nl_write_symbol(out, cell.value.as_symbol);
    *result = cell;
    return 0;
  case NL_PAIR:
    fputc('(', out);
    if (nl_writeq(scope, NL_HEAD(cell), result)) return 1;
    tail = NL_NEXT(cell);
    for (;;) {
      switch (tail->type) {
      case NL_NIL:
        fputc(')', out);
        *result = cell;
        return 0;
      case NL_PAIR:
        fputc(' ', out);
        if (nl_writeq(scope, NL_HEAD_AT(tail), result)) return 1;
        tail = NL_NEXT_AT(tail);
        break;
      case NL_INTEGER:
        fprintf(out, " . %li)", tail->value.as_integer);
        return 0;
      case NL_SYMBOL:
        fprintf(out, " . ");
        nl_write_symbol(out, tail->value.as_symbol);
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
  struct nl_cell *a, s_out;
  FILE *out = stdout;
  if (!nl_evalq(scope, nl_out, &s_out)
      && s_out.type == NL_INTEGER)
    out = (FILE *)s_out.value.as_integer;
  switch (cell.type) {
  case NL_INTEGER:
    fputc((char)cell.value.as_integer, out);
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
NL_BUILTIN(exit) {
  struct nl_cell exit_code;
  if (cell.type == NL_NIL) exit(0);
  if (cell.type != NL_PAIR) {
    scope->last_err = "invalid exit: expected pair args";
    exit(1);
  }
  if (nl_evalq(scope, NL_HEAD(cell), &exit_code)) return 1;
  switch (exit_code.type) {
  case NL_NIL: exit(0);
  case NL_INTEGER: exit(exit_code.value.as_integer);
  default:
    scope->last_err = "invalid exit: expected integer exit code";
    exit(1);
  }
  return 1;
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
void nl_scope_define_builtins(struct nl_scope *scope) {
  nl_scope_put(scope, nl_in.value.as_symbol, nl_cell_as_int((int64_t)stdin));
  nl_scope_put(scope, nl_out.value.as_symbol, nl_cell_as_int((int64_t)stdout));
  nl_scope_put(scope, nl_err.value.as_symbol, nl_cell_as_int((int64_t)stderr));
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
  NL_DEF_BUILTIN("defq", defq);
  NL_DEF_BUILTIN("eval", eval);
  NL_DEF_BUILTIN("exit", exit);
  NL_DEF_BUILTIN("filter", filter);
  NL_DEF_BUILTIN("fold", fold);
  NL_DEF_BUILTIN("foreach", foreach);
  NL_DEF_BUILTIN("head", head);
  NL_DEF_BUILTIN("integer?", is_integer);
  NL_DEF_BUILTIN("length", length);
  NL_DEF_BUILTIN("list", list);
  NL_DEF_BUILTIN("load", load);
  NL_DEF_BUILTIN("load-native", loadnative);
  NL_DEF_BUILTIN("map", map);
  NL_DEF_BUILTIN("map-pair", mappair);
  NL_DEF_BUILTIN("nil?", is_nil);
  NL_DEF_BUILTIN("not", not);
  NL_DEF_BUILTIN("or", or);
  NL_DEF_BUILTIN("pair", pair);
  NL_DEF_BUILTIN("pair?", is_pair);
  NL_DEF_BUILTIN("print", print);
  NL_DEF_BUILTIN("printq", printq);
  NL_DEF_BUILTIN("quote", quote);
  NL_DEF_BUILTIN("set", set);
  NL_DEF_BUILTIN("setq", setq);
  NL_DEF_BUILTIN("symbol?", is_symbol);
  NL_DEF_BUILTIN("tail", tail);
  NL_DEF_BUILTIN("unfold", unfold);
  NL_DEF_BUILTIN("write", write);
  NL_DEF_BUILTIN("write-bytes", write_bytes);
  NL_DEF_BUILTIN("writeq", writeq);
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

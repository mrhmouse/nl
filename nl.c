#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// nl
// not lisp
// new lisp
// 'nother lisp
// nano lisp
// nil lisp

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
  c.value.as_pair = malloc(sizeof(head) + sizeof(tail));
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

void nl_state_put(struct nl_state *state, const char *name, struct nl_cell value) {
  int match = 0;
  struct nl_symbols *s, *l;
  for (; state != NULL; state = state->parent_state) {
    for (s = state->symbols; s != NULL; s = s->next) {
      l = s;
      if (0 == strcmp(name, s->name)) {
        match = 1;
        break;
      }
    }
    if (match) break;
  }
  if (!match) {
    l->next = malloc(sizeof(*l->next));
    l->next->name = strdup(name);
    l = l->next;
  }
  l->value = value;
}
void nl_state_get(struct nl_state *state, const char *name, struct nl_cell *result) {
  struct nl_symbols *s;
  for (s = state->symbols; s != NULL; s = s->next)
    if (0 == strcmp(name, s->name)) {
      *result = s->value;
      return;
    }
  if (state->parent_state) nl_state_get(state->parent_state, name, result);
  else *result = nl_cell_as_nil();
}
void nl_state_link(struct nl_state *child, struct nl_state *parent) {
  child->stdout = parent->stdout;
  child->stdin = parent->stdin;
  child->stderr = parent->stderr;
  child->parent_state = parent;
}
void nl_state_init(struct nl_state *state) {
  state->stdout = stdout;
  state->stdin = stdin;
  state->stderr = stderr;
  state->last_err = NULL;
  state->parent_state = NULL;
  state->symbols = malloc(sizeof(*state->symbols));
  state->symbols->name = "nil";
  state->symbols->value.type = NL_NIL;
  state->symbols->next = NULL;
}

#define NL_BUILTIN(name) int nl_ ## name(struct nl_state *state, struct nl_cell cell, struct nl_cell *result)
NL_BUILTIN(quote) {
  *result = cell;
  return 0;
}
NL_BUILTIN(print);
NL_BUILTIN(printq);
NL_BUILTIN(write);
NL_BUILTIN(setq);
NL_BUILTIN(letq);
NL_BUILTIN(defq);
NL_BUILTIN(eval);
void nl_state_define_builtins(struct nl_state *state) {
  nl_state_put(state, "quote", nl_cell_as_int((int64_t)nl_quote));
  nl_state_put(state, "printq", nl_cell_as_int((int64_t)nl_printq));
  nl_state_put(state, "print", nl_cell_as_int((int64_t)nl_print));
  nl_state_put(state, "setq", nl_cell_as_int((int64_t)nl_setq));
  nl_state_put(state, "letq", nl_cell_as_int((int64_t)nl_letq));
  nl_state_put(state, "defq", nl_cell_as_int((int64_t)nl_defq));
  nl_state_put(state, "eval", nl_cell_as_int((int64_t)nl_eval));
}

int nl_skip_whitespace(struct nl_state *state) {
  int ch;
  do {
    ch = fgetc(state->stdin);
  } while (isspace(ch));
  return ch;
}
int nl_read(struct nl_state *state, struct nl_cell *result) {
  int sign = 1;
  int ch = nl_skip_whitespace(state);
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
  } else if ('(' == ch) {
    struct nl_cell head, *tail;
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
    // TODO interning, uniqing
    *result = nl_cell_as_symbol(buf);
    return 0;
  }
}

NL_BUILTIN(evalq) {
  struct nl_cell head, letq_tag, *args, *vars, *params;
  switch (cell.type) {
  case NL_NIL:
  case NL_INTEGER:
    *result = cell;
    return 0;
  case NL_SYMBOL:
    nl_state_get(state, cell.value.as_symbol, result);
    return 0;
  case NL_PAIR:
    if (nl_evalq(state, cell.value.as_pair[0], &head)) return 1;
    switch (head.type) {
    case NL_NIL:
      state->last_err = "illegal function call: cannot invoke NIL";
      return 1;
    case NL_INTEGER:
      return ((nl_native_func)head.value.as_integer)(state, cell.value.as_pair[1], result);
    case NL_SYMBOL:
      state->last_err = "illegal function call: cannot invoke symbol";
      return 1;
    case NL_PAIR:
      if (head.value.as_pair[0].type != NL_PAIR) {
        state->last_err = "illegal lambda call: non-pair parameter list";
        return 1;
      }
      if (head.value.as_pair[1].type != NL_PAIR) {
        state->last_err = "illegal lambda call: non-pair lambda body";
        return 1;
      }
      letq_tag = nl_cell_as_pair(nl_cell_as_pair(nl_cell_as_nil(), nl_cell_as_nil()),
                                 head.value.as_pair[1]);
      vars = letq_tag.value.as_pair;
      for (args = cell.value.as_pair + 1, params = head.value.as_pair;
           args->type == NL_PAIR && params->type == NL_PAIR;
           args = args->value.as_pair + 1, params = params->value.as_pair + 1)
        {
          *vars = nl_cell_as_pair(params->value.as_pair[0], nl_cell_as_pair(args->value.as_pair[0], nl_cell_as_nil()));
          vars = vars->value.as_pair + 1;
        }
      return nl_letq(state, letq_tag, result);
    default:
      state->last_err = "unknown cell type";
      return 1;
    }
  default:
    state->last_err = "unknown cell type";
    return 1;
  }
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

NL_BUILTIN(printq) {
  switch (cell.type) {
  case NL_NIL:
    *result = cell;
    return 0;
  case NL_INTEGER:
    fprintf(state->stdout, "%li", cell.value.as_integer);
    *result = cell;
    return 0;
  case NL_PAIR:
    nl_printq(state, cell.value.as_pair[0], result);
    if (NL_NIL != cell.value.as_pair[1].type) {
      fputc(' ', state->stdout);
      nl_printq(state, cell.value.as_pair[1], result);
    }
    return 0;
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
  switch (cell.type) {
  case NL_NIL:
    *result = cell;
    return 0;
  case NL_INTEGER:
    fprintf(state->stdout, "%li", cell.value.as_integer);
    *result = cell;
    return 0;
  case NL_PAIR:
    if (nl_evalq(state, cell.value.as_pair[0], result)) return 1;
    for (tail = &cell; tail->type == NL_PAIR; tail = tail->value.as_pair + 1) {
      if (nl_evalq(state, tail->value.as_pair[0], &val)) return 1;
      fputc(' ', state->stdout);
      nl_print(state, val, result);
    }
    if (tail->type != NL_NIL) nl_print(state, *tail, result);
    return 0;
  case NL_SYMBOL:
    fprintf(state->stdout, "%s", cell.value.as_symbol);
    *result = cell;
    return 0;
  default:
    state->last_err = "unknown cell type";
    return 1;
  }
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


NL_BUILTIN(defq) {
  struct nl_cell name, body;
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

NL_BUILTIN(setq) {
  return nl_setqe(state, state, cell, result);
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
  if (vars.type != NL_PAIR) {
    state->last_err = "illegal letq: non-pair var list";
    return 1;
  }
  if (body.type != NL_PAIR) {
    state->last_err = "illegal letq: non-pair body";
    return 1;
  }
  nl_state_init(&body_state);
  if (nl_setqe(&body_state, state, vars, result)) return 1;
  nl_state_link(&body_state, state);
  for (tail = &body; tail->type == NL_PAIR; tail = tail->value.as_pair + 1) {
    if (nl_evalq(&body_state, tail->value.as_pair[0], result)) return 1;
  }
  if (tail->type != NL_NIL) return nl_evalq(&body_state, *tail, result);
  return 0;
}

NL_BUILTIN(write) {
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
    if (nl_write(state, cell.value.as_pair[0], result)) return 1;
    tail = cell.value.as_pair + 1;
    for (;;) {
      switch (tail->type) {
      case NL_NIL:
        fputc(')', state->stdout);
        *result = cell;
        return 0;
      case NL_PAIR:
        fputc(' ', state->stdout);
        if (nl_write(state, tail->value.as_pair[0], result)) return 1;
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
    fputs("\n", state->stdout);
    nl_write(state, last_eval, &last_read);
  }
}

int main() {
  struct nl_state state;
  nl_state_init(&state);
  nl_state_define_builtins(&state);
  return nl_run_repl(&state);
}

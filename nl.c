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
};

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
  struct nl_symbols *s;
  for (s = state->symbols; s->next != NULL; s = s->next)
    if (0 == strcmp(name, s->name)) {
      s->value.type = NL_NIL;
      return;
    }
  s->next = malloc(sizeof(*s->next));
  s->next->name = strdup(name);
  s->next->value = value;
}

void nl_state_init(struct nl_state *state) {
  state->stdout = stdout;
  state->stdin = stdin;
  state->stderr = stderr;
  state->last_err = NULL;
  state->symbols = malloc(sizeof(*state->symbols));
  state->symbols->name = strdup("NIL");
  state->symbols->value.type = NL_NIL;
  state->symbols->next = NULL;
}

int nl_skip_whitespace(struct nl_state *state) {
  int ch;
  do {
    ch = fgetc(state->stdin);
  } while (isspace(ch));
  return ch;
}

int nl_read(struct nl_state *state, struct nl_cell *result) {
  int ch = nl_skip_whitespace(state);
  if (isdigit(ch)) {
    *result = nl_cell_as_int(ch - '0');
    while (isdigit(ch = fgetc(state->stdin))) {
      result->value.as_integer *= 10;
      result->value.as_integer += ch - '0';
    }
    ungetc(ch, state->stdin);
    return 0;
  } else if ('"' == ch) {
    state->last_err = "quoted symbols not implemented";
    return 1;
  } else if ('(' == ch) {
    state->last_err = "lists not implemented";
    return 1;
  } else {
    state->last_err = "symbols not implemented";
    return 1;
  }
}

int nl_eval(struct nl_state *state, struct nl_cell value, struct nl_cell *result) {
  state->last_err = "not implemented";
  return 1;
}

void nl_print(struct nl_state *state, struct nl_cell cell) {
  switch (cell.type) {
  case NL_NIL:
    break;
  case NL_INTEGER:
    fprintf(state->stdout, "%li", cell.value.as_integer);
    break;
  case NL_PAIR:
    nl_print(state, cell.value.as_pair[0]);
    fputc(' ', state->stdout);
    nl_print(state, cell.value.as_pair[1]);
    break;
  case NL_SYMBOL:
    fprintf(state->stdout, "%s", cell.value.as_symbol);
    break;
  }
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
    if (nl_eval(state, last_read, &last_eval)) {
      if (state->last_err)
        fprintf(state->stderr, "ERROR eval: %s\n", state->last_err);
      else
        fputs("ERROR eval\n", state->stderr);
      return 2;
    }
    nl_print(state, last_eval);
  }
}

int main() {
  struct nl_state state;
  nl_state_init(&state);
  return nl_run_repl(&state);
}

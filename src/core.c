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
    call = nl_cell_as_pair(nl_cell_as_pair(quote, fun), nl_cell_as_pair(nl_cell_as_pair(quote, NL_HEAD_AT(a)), nil));
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
    call = nl_cell_as_pair(nl_cell_as_pair(quote, fun), nl_cell_as_pair(nl_cell_as_pair(quote, NL_HEAD_AT(item)), nil));
    if (nl_evalq(scope, call, result->value.as_pair)) return 1;
    switch (NL_TAIL_AT(item).type) {
    case NL_NIL:
      break;
    case NL_PAIR:
      NL_TAIL_AT(result) = nl_cell_as_pair(nil, nil);
      result = NL_NEXT_AT(result);
      break;
    default:
      call = nl_cell_as_pair(nl_cell_as_pair(quote, fun), nl_cell_as_pair(nl_cell_as_pair(quote, NL_TAIL_AT(item)), nil));
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
    call = nl_cell_as_pair(nl_cell_as_pair(quote, fun), nl_cell_as_pair(nl_cell_as_pair(quote, *item), nil));
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
    call = nl_cell_as_pair(nl_cell_as_pair(quote, fun), nl_cell_as_pair(nl_cell_as_pair(quote, NL_HEAD_AT(item)), nil));
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
    call = nl_cell_as_pair(nl_cell_as_pair(quote, fun), nl_cell_as_pair(nl_cell_as_pair(quote, NL_HEAD_AT(item)), nl_cell_as_pair(nl_cell_as_pair(quote, *result), nil)));
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
    call = nl_cell_as_pair(nl_cell_as_pair(quote, continue_f), nl_cell_as_pair(nl_cell_as_pair(quote, seed), nil));
    if (nl_evalq(scope, call, &v)) return 1;
    if (v.type == NL_NIL) break;
    call = nl_cell_as_pair(nl_cell_as_pair(quote, pair_f), nl_cell_as_pair(nl_cell_as_pair(quote, seed), nl_cell_as_pair(nl_cell_as_pair(quote, *result), nil)));
    if (nl_evalq(scope, call, result)) return 1;
    call = nl_cell_as_pair(nl_cell_as_pair(quote, next_seed_f), nl_cell_as_pair(nl_cell_as_pair(quote, seed), nil));
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
NL_BUILTIN(set_head) {
  struct nl_cell pair, new_head;
  if (2 != nl_list_length(cell)) {
    scope->last_err = "illegal set-head: expected 2 args";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &pair)
      || nl_evalq(scope, NL_HEAD(NL_TAIL(cell)), &new_head))
    return 1;
  if (pair.type != NL_PAIR) {
    scope->last_err = "illegal set-head: cannot set head of non-pair";
    return 1;
  }
  NL_HEAD(pair) = new_head;
  return 0;
}
NL_BUILTIN(set_tail) {
  struct nl_cell pair, new_tail;
  if (2 != nl_list_length(cell)) {
    scope->last_err = "illegal set-tail: expected 2 args";
    return 1;
  }
  if (nl_evalq(scope, NL_HEAD(cell), &pair)
      || nl_evalq(scope, NL_HEAD(NL_TAIL(cell)), &new_tail))
    return 1;
  if (pair.type != NL_PAIR) {
    scope->last_err = "illegal set-tail: cannot set tail of non-pair";
    return 1;
  }
  NL_TAIL(pair) = new_tail;
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

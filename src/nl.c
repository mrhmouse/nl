#include "nl.h"
int main() {
  struct nl_scope scope;
  nl_globals_init();
  nl_scope_init(&scope);
  nl_scope_define_builtins(&scope);
  return nl_run_repl(isatty(STDIN_FILENO), &scope);
}

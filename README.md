`nl`
======
This repository hosts an _in-progress_ interpreter
for a homebrewed Lisp-1 with dynamic scope and a
minimal set of datatypes.

The name stands for "nil lisp" or "not a lisp" and
might change at some point -- it was just the first
name to come to mind

```lisp
(defq hello (Name)
 (print 'Hello Name)
 (newline))
(hello 'world!)

(defq sum (List)
 (fold + 0 List))
(print '(The answer is:)
       (sum '(1 11 13 17)))
```

Overview: Data-types
--------------------
There are only four _data-types_ available in `nl`:
* _integers_, which are signed and 64-bit
* _symbols_, which are like immutable strings
* _pairs_, which are a combination of any two other values;
  unlike `cons` pairs in traditional Lisps, which are composed
  of a `car` and a `cdr`, these pairs are compose of a `head`
  and a `tail`. They're exactly the same thing, with a less
  awkward title
* _nil_, which is a special type with one value (itself);
  it represents the empty set, "nothing-ness", or "undefined",
  and is the only value which is treated as "false"

These data-types are composed together to build higher-level
data-structures. The most common data-structure is the list,
which is composed of nested pairs, ending with a final
tail of `nil` in the last pair.

Overview: Syntax
--------------------
Each data-type has its own syntax. Some data-types accept
more than one syntax for reading, but will print to the
same syntax for writing.

`nil` can be written as the symbol `nil`, or as `()`.
Technically, the symbol `nil` can be set to something else
at runtime, but this is not done in practice.

Integers are a series of digits, `0123456789`, optionally
prefixed with a hypen, `-`, to indicate a negative number.
Floating point numbers are not supported, but a syntax
may be added in the future for reading fixed-point numbers
with a configurable scale (a la _PicoLisp_).

Symbols start with any character besides `#',().` and can
contain any non-whitespace character besides `()`. _TODO_
To write a symbol that contains whitespace or characters from
these lists, you can surround it with double quotes `""`.
Any double quotes intended to be included as part of this
symbol must be escaped with a backslash `\`. To include a
literal backslash, escape it with a backslash like `\\`.
Any other backslashed character is included as-is.

Pairs are written as any two items, surrounded by parentheses
and separated by a period `.` character. For example, the
pair of the two numbers `42` and `1337` would be written
as `(42 . 1337)`. Other examples of pairs are:
* `(1 . 2)`
* `(nil . nil)`
* `(1 . (2 . 3))`
* `((A . nil) . (((+ . (1 . (A . nil)))) . nil))`

There is one data-structure which also gets a special syntax,
and that's the list. Instead of writing the mess of dots
required to build a list out of pairs, you can write the
last example as `((A) (+ 1 A))`. In general, the form `(A)`
is a pair with `A` in the head and `nil` in the tail; the
form `(A B)` is a pair with `A` in the head, and another
pair `(B)` in the tail. Further items add further nested
pairs. If you wish to explicitly end a list with something
other than `nil`, you can separate the last item from
the other items with a period `.` as in `(1 2 3 . 4)`

Overview: Evaluation
--------------------
The rules for evaluation are as follows:
* _nil_ evaluates to itself
* an integer evaluates to itself
* symbols evaluate to their value in the current _scope_
* lists are evaluated as _function calls_

Function calls are evaluated as follows:
* the head of the list is evaluated
* if it is `nil`, an error is raised
  (_TODO_: no error handling as of yet, so right now
  this means "the program halts")
* if it's a symbol, it is evaluated again and this
  process is repeated with the new value
* if it's an integer, it is interpreted as a pointer
  to a native function; the tail of the list is passed
  unevaluated to the native function, and the result
  of the native function is returned
* if it's a list, then it is interpreted as a lambda
  call. First, the tail of the list is optionally bound
  to symbols according the head of the lambda; then,
  each item in the tail of the lambda is evaluated in order;
  the result of the final item is returned

Lambda lists can bind their arguments in three ways:
* using `nil` as the head of the lambda will skip
  evaluation of any arguments and just evaluate the
  tail of the lambda (aka the "lambda body")
* using a list of symbols will evaluate each argument
  in turn, up to the number of symbols provided, and
  then bind the resulting values to those symbols
* using a single symbol will bind the list of arguments
  to that symbol, unevaluated; the function is then
  in charge of evaluating the arguments if and when
  it needs to. this allows "macros" to be written like
  regular functions

Overview: Scope
--------------------
When values are "bound" to a symbol, it means that the
value has been associated with that symbol in some _scope_.
By default, everything shares the same, global scope. When
a lambda is being called, though, its parameters are bound
in a new child scope, nested "inside" the parent scope.

Any values bound this way will _shadow_ previously-bound
values until the call is complete. Afterwards, the previous
values are accessible again.

Core Functions
====================
`nl` comes with some functions built-in as native functions;
these make up the _core functions_ and provide all of the
functionality of the language. The standard library is fully
implemented in terms of these core functions.

Core Functions: `*`, `+`, `-`, `/`
--------------------
These functions do math on zero or more integers at a time.

Core Functions: `<`, `<=`, `=`, `>`, `>=`
--------------------
These functions compare values, returning either `nil` or
the symbol `t` to indicate false or true. Comparisons across
types work as follows, in order:
* `nil` values are smaller than all other values, except `nil`
* `integer` values are smaller than values of other types,
  besides `nil`, and compare normally to other integers
* `symbol` values are smaller than pairs, but larger than
  other types, and compare case-sensitively to other `symbol`s
* `pair` values are the largest types, and are compared to
  other `pair` values by recursively comparing the heads and
  tails of each value

Core Functions: `and`
--------------------
Evaluates each argument in order. If it is `nil`, evaluation
stops and `nil` is returned. Otherwise, returns the value
of the last argument.

Core Functions: `apply`
--------------------
Evaluate the second argument, and pass it as the whole argument
list to the first argument. This can be used to pass evaluated
arguments to functions which don't normally evaluate their
arguments -- for example, to write recursive "macros".

Core Functions: `defq`
--------------------
This function doesn't evaluate any of its arguments.
Binds the head of the argument list (which should be a symbol)
to the tail of the argument list (which is usually a lambda).

Core Functions: `eval`
--------------------
Evaluates its first argument (actually, evaluates it twice).
Used for running code that has been collected as data, as
in a "macro"

Core Functions: `exit`
--------------------
Exits the program, halting execution. Optionally set the exit
code to the first argument, which should be an integer between
0 and 255.

Core Functions: `filter`
--------------------
Filter the items in a list, returning a new list consisting of
only the items for which the given predicate returns non-`nil`

Core Functions: `pair`
--------------------
Creates a pair from the values of its first and second arguments.

TODO
====================
There is always more to do :)
* more docs
* no double-quoted symbols
  we need a way to read & write double-quoted symbols
  with spaces or other special characters
* needs a set of core builtins
  must decide on a minimal set of builtins to provide
* needs a standard library
  outside of the builtins, must provide a standard
  library with functions for building real
  applications, e.g. HTML generation libraries,
  data structures built on cells, test frameworks, etc
* needs unquote operator
  unsure if this should work with the standard `quote`
  or if it should require the use of `quasiquote`. need
  both a single-item and a splicing version, e.g.
  `'(1 2 ,(+ 1 2))` should produce the list `(1 2 3)`
  while `'(1 2 ,.(range 3 5))` should produce `(1 2 3 4 5)`
* need tail-call optimization
* comments at between the last element of a list
  and the closing parentheses crash the reader

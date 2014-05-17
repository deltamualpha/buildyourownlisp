This is a repo for my own implementation of [Build Your Own Lisp](http://buildyourownlisp.com/). I'm learning the rudiments of C and what goes into devising your own programming language.

## Deviations from the book

* My version of the lispy syntax dispenses with the special `lispy` root and instead treats expressions as the root item. This means that all S-expressions must be wrapped in the [syntax of the gods](http://www.xkcd.com/224/), as opposed to the bare structures the default syntax allows. This only really presented problems at the very end, where I had a segfault on external library load related to how I had been defining what an `Expr` was in the `mpca_lang` call.
* I didn't bother keeping around the portable readline() implementation, as this was all done on one MacBook.
* I experimented with replacing the odd `{}` syntax with the more familiar `'()` style, but haven't fully implemented it. (There's some `putchar`->`print` switches to be made that I didn't feel like tracking down.)

## Known issues

* If you try and use a symbol that contains a character not in `[a-zA-Z0-9_+\-*\/\\=<>!&\.]` the repl (and probably the loader) hangs. That should probably be made more safe somehow.
* It's currently only loading the first expression from an external file; not sure why.

Relevant links:

* [Build Your Own Lisp](http://buildyourownlisp.com/)
* [MPC](https://github.com/orangeduck/mpc)
* [orangeduck/BuildYourOwnLisp](https://github.com/orangeduck/BuildYourOwnLisp)
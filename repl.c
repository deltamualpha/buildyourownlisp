#include <stdio.h>
#include <stdlib.h>
#include "mpc/mpc.h"

#include <editline/readline.h>

/* this macro ASSERTs a condition, then errors if it's NOT true */
#define LASSERT(args, cond, fmt, ...) \
	if (!(cond)) { \
		lval* err = lval_err(fmt, ##__VA_ARGS__); \
		lval_del(args); \
		return err; \
	}

/* forward declarations */

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

/*
 * lval setup
 */

enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_FUN, LVAL_SEXPR, LVAL_QEXPR };

typedef lval*(*lbuiltin)(lenv*, lval*);

struct lval {
	int type;

	long num;
	char* err;
	char* sym;
	lbuiltin fun;

	int count;
	struct lval** cell;
};

/* create a pointer to a new num lval */
lval* lval_num(long x) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_NUM;
	v->num = x;
	return v;
}

/* create a pointer to a new err lval */
lval* lval_err(char* fmt, ...) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_ERR;

	/* varable list */
	va_list va;
	va_start(va, fmt);

	/* allocate 512 bytes of space */
	v->err = malloc(512);

	/* print */
	vsnprintf(v->err, 511, fmt, va);

	/* realloc to our actual string length */
	v->err = realloc(v->err, strlen(v->err)+1);

	/* cleanup */
	va_end(va);

	return v;
}

/* create a pointer to a new symbol lval */
lval* lval_sym(char* s) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_SYM;
	v->sym = malloc(strlen(s)+1);
	strcpy(v->sym, s);
	return v;
}

/* a new pointer to a function */
lval* lval_fun(lbuiltin func) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_FUN;
	v->fun = func;
	return v;
}

/* a pointer to a new empty sexpr lval */
lval* lval_sexpr(void) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_SEXPR;
	v->count = 0;
	v->cell = NULL;
	return v;
}

/* Qexpr pointer construction */
lval* lval_qexpr(void) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_QEXPR;
	v->count = 0;
	v->cell = NULL;
	return v;
}

char* ltype_name (int t) {
	switch(t) {
		case LVAL_FUN: return "Function";
		case LVAL_NUM: return "Number";
		case LVAL_ERR: return "Error";
		case LVAL_SYM: return "Symbol";
		case LVAL_SEXPR: return "S-Expression";
		case LVAL_QEXPR: return "Q-Expression";
		default: return "Unknown";
	}
}

/* 
 * working with lvals
 */

void lval_del(lval* v) {
	switch (v->type) {
		/* nothing else to delete for nums or funcs */
		case LVAL_NUM: break;
		case LVAL_FUN: break;

		/* for err or sym free strings */
		case LVAL_ERR: free(v->err); break;
		case LVAL_SYM: free(v->sym); break;

		/* sexpr and qexpr delete everything, recursively */
		case LVAL_QEXPR:
		case LVAL_SEXPR:
			for (int i = 0; i < v->count; i++) {
				lval_del(v->cell[i]);
			}
			/* and the pointer itself */
			free(v->cell);
		break;
	}
	/* and now the actual lval struct itself */
	free(v);
}

lval* lval_add(lval* v, lval* x) {
	/* update our pointer count */
	v->count++;

	/* allocate more space for the pointer */
	v->cell = realloc(v->cell, sizeof(lval*) * v->count);

	/* put the new pointer into the cell list */
	v->cell[v->count-1] = x;

	/* return a pointer to the updated lval */
	return v;
}

lval* lval_pop(lval* v, int i) {
	/* Find the item at "i" */
	lval* x = v->cell[i];

	/* Shift the memory following the item at "i" over the top of it */
	memmove(&v->cell[i], &v->cell[i+1], sizeof(lval*) * (v->count-i-1));

	/* Decrease the count of items in the list */
	v->count--;

	/* Reallocate the memory used */
	v->cell = realloc(v->cell, sizeof(lval*) * v->count);

	/* return the pointer we asked for; v is MUTATED but still exists, sans pointer to x */
	return x;
}

lval* lval_take(lval* v, int i) {
	/* pop the lval pointer we want out of v... */
	lval* x = lval_pop(v, i);

	/* ...then delete v! */
	lval_del(v);

	/* and return the pointer to the value */
	return x;
}

lval* lval_copy(lval* v) {

	lval* x = malloc(sizeof(lval));
	x->type = v->type;

	switch (v->type) {
		/* nums and funcs copy straight across */
		case LVAL_FUN: x->fun = v->fun; break;
		case LVAL_NUM: x->num = v->num; break;

		/* strings copy using malloc and strcpy */
		case LVAL_ERR:
			x->err = malloc(strlen(v->err) + 1);
			strcpy(x->err, v->err); 
			break;
		case LVAL_SYM:
			x->sym = malloc(strlen(v->sym) + 1);
			strcpy(x->sym, v->sym);
			break;

		/* copy lists recursively */
		case LVAL_SEXPR:
		case LVAL_QEXPR:
			x->count = v->count;
			x->cell = malloc(sizeof(lval*) * x->count);
			for (int i = 0; i < x->count; i++) {
				x->cell[i] = lval_copy(v->cell[i]);
			}
		break;
	}

	return x;
}

/* forward declaring this for lval_expr_print() */
void lval_print(lval* v);

void lval_expr_print(lval* v, char open, char close) {
	putchar(open);
	for (int i = 0; i < v->count; i++) {
		/* print value contained within */
		lval_print(v->cell[i]);

		/* trailing space, skipped if last run */
		if (i != (v->count-1)) {
			putchar(' ');
		}
	}
	putchar(close);
}

/* this was forward-declared */
void lval_print(lval* v) {
	switch (v->type) {
		case LVAL_NUM: printf("%li", v->num); break;
		case LVAL_FUN: printf("<function>"); break;
		case LVAL_ERR: printf("Error: %s", v->err); break;
		case LVAL_SYM: printf("%s", v->sym); break;
		/* recurse if it's an sexpr */
		case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
		case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
	}	
}

void lval_println(lval* v) {
	/* little util for printing lines */
	lval_print(v);
	putchar('\n');
}

/*
 * lenv setup
 */
struct lenv {
	int count;
	/* list of strings */
	char** syms;
	/* list of pointers */
	lval** vals;
};

lenv* lenv_new(void) {
	lenv* e = malloc(sizeof(lenv));
	e->count = 0;
	e->syms = NULL;
	e->vals = NULL;
	return e;
}

/*
 * working with lenvs
 */

void lenv_del(lenv* e) {
	for (int i = 0; i < e->count; i++) {
		free(e->syms[i]);
		lval_del(e->vals[i]);
	}
	free(e->syms);
	free(e->vals);
	free(e);
}

/* get existing lval from lenv */
lval* lenv_get(lenv* e, lval* k) {
	for (int i = 0; i < e->count; i++) {
		if (strcmp(e->syms[i], k->sym) == 0) {
			return lval_copy(e->vals[i]);
		}
	}
	return lval_err("Unknown symbol '%s'", k->sym);
}

/* put new lval into lenv */
void lenv_put(lenv* e, lval* k, lval* v) {
	for (int i = 0; i < e->count; i++) {
		if (strcmp(e->syms[i], k->sym) == 0) {
			lval_del(e->vals[i]);
			e->vals[i] = lval_copy(v);
			return;
		}
	}

	e->count++;
	e->vals = realloc(e->vals, sizeof(lval*) * e->count);
	e->syms = realloc(e->syms, sizeof(char*) * e->count);

	e->vals[e->count-1] = lval_copy(v);
	e->syms[e->count-1] = malloc(strlen(k->sym) + 1);
	strcpy(e->syms[e->count-1], k->sym);
}

/* forward declaration */
lval* lval_eval(lenv* e, lval* v);

/*
 * builtin operations
 */

lval* builtin_op(lenv* e, lval* a, char* op) {
	/* Ensure all arguments are numbers */
	for (int i = 0; i < a->count; i++) {
		if (a->cell[i]->type != LVAL_NUM) {
			lval_del(a);
			return lval_err("Cannot operate on non number!");
		}
	}
	
	/* Pop the first element */
	lval* x = lval_pop(a, 0);

	/* If no arguments and sub then perform unary negation */
	if ((strcmp(op, "-") == 0) && a->count == 0) { x->num = -x->num; }

	/* While there are still elements remaining */
	while (a->count > 0) {

		/* Pop the next element */
		lval* y = lval_pop(a, 0);

		/* Perform operation */
		if (strcmp(op, "+") == 0) { x->num += y->num; }
		if (strcmp(op, "-") == 0) { x->num -= y->num; }
		if (strcmp(op, "*") == 0) { x->num *= y->num; }
		if (strcmp(op, "/") == 0) {
			if (y->num == 0) {
				lval_del(x); lval_del(y);
				x = lval_err("Division By Zero!"); break;
			} else {
				x->num /= y->num;
			}
		}

		/* Delete element now finished with */
		lval_del(y);
	}

	/* Delete input expression and return result */
	lval_del(a);
	return x;
}

lval* builtin_head(lenv* e, lval* a) {
	/* error check! Note the inversion of equality */
	LASSERT(a, (a->count == 1), "'head' passed too many arguments; '1' expected, '%i' given.", a->count);
	LASSERT(a, (a->cell[0]->type == LVAL_QEXPR), "'head' passed incorrect type; '%s' expected, '%s' given.", ltype_name(LVAL_QEXPR), ltype_name(a->cell[0]->type));
	LASSERT(a, (a->cell[0]->count != 0), "'head' passed {}!");

	/* otherwise take first and delete the rest */
	lval* v = lval_take(a, 0);

	/* is this loop needed? Don't we already do this with lval_take? */
	while(v->count > 1) {
		lval_del(lval_pop(v, 1));
	}
	return v;
}

lval* builtin_tail(lenv* e, lval* a) {
	/* error check! Note the inversion of equality */
	LASSERT(a, (a->count == 1), "'tail' passed too many arguments; '1' expected, '%i' given.", a->count);
	LASSERT(a, (a->cell[0]->type == LVAL_QEXPR), "'tail' passed incorrect type; '%s' expected, '%s' given.", ltype_name(LVAL_QEXPR), ltype_name(a->cell[0]->type));
	LASSERT(a, (a->cell[0]->count != 0), "'tail' passed {}!");

	/* take the first */
	lval* v = lval_take(a, 0);

	/* and delete the rest of it */
	lval_del(lval_pop(v, 0));

	return v;
}

lval* builtin_list(lenv* e, lval* a) {
	/* look ma, I'm a qexpr now! */
	a->type = LVAL_QEXPR;
	return a;
}

lval* builtin_eval(lenv* e, lval* a) {
	LASSERT(a, (a->count == 1), "'eval' passed too many arguments; '1' expected, '%i' given.", a->count);
	LASSERT(a, (a->cell[0]->type == LVAL_QEXPR), "'eval' passed incorrect type; '%s' expected, '%s' given.", ltype_name(LVAL_QEXPR), ltype_name(a->cell[0]->type));

	/* make it into an sexpr and evaluate it! */
	lval* x = lval_take(a, 0);
	x->type = LVAL_SEXPR;
	return lval_eval(e, x);
}

lval* lval_join(lval* x, lval* y) {
	/* for each cell in y add it to x */
	while(y->count) {
		x = lval_add(x, lval_pop(y, 0));
	}

	/* clean up after ourselves */
	lval_del(y);
	return x;
}

lval* builtin_join(lenv* e, lval* a) {
	for (int i = 0; i < a->count; i++) {
		LASSERT(a, (a->cell[i]->type == LVAL_QEXPR), "'join' passed incorrect type; '%s' expected, '%s' given.", ltype_name(LVAL_QEXPR), ltype_name(a->cell[0]->type));
	}

	lval* x = lval_pop(a, 0);

	while(a->count) {
		x = lval_join(x, lval_pop(a, 0));
	}

	lval_del(a);
	return x;
}

lval* builtin_cons(lenv* e, lval* a) {
	LASSERT(a, (a->count == 2), "Incorrect argument count passed to 'cons'; '%i' given, '2' expected.", a->count);
	LASSERT(a, (a->cell[1]->type == LVAL_QEXPR), "'cons' expected a '%s' as second argument; received '%s'.", ltype_name(LVAL_QEXPR), ltype_name(a->cell[1]->type));

	/* get the first item from the input
	 * coerce it to be a qexpr
	 * then join the new qexpr to the existing list */
	lval* z = lval_join(lval_add(lval_qexpr(), lval_pop(a, 0)), lval_take(a, 0));

	/* return a pointer to the new lval */
	return z;
}

lval* builtin_def(lenv* e, lval* a) {
	LASSERT(a, (a->cell[0]->type == LVAL_QEXPR), "'def' passed incorrect type; '%s' expected, '%s' given.", ltype_name(LVAL_QEXPR), ltype_name(a->cell[0]->type));

	lval* syms = a->cell[0];

	for (int i = 0; i < syms->count; ++i) {
		LASSERT(a, (syms->cell[i]->type == LVAL_SYM), "Function 'def' cannot define non-symbol; '%s' received in position %i.", syms->cell[i]->type, i);
	}

	LASSERT(a, (syms->count == a->count-1), "Function 'def' cannot define incorrect number of values to symbols. %i vs. %i.", syms->count, a->count-1);

	for (int i = 0; i < syms->count; ++i) {
		lenv_put(e, syms->cell[i], a->cell[i+1]);
	}

	lval_del(a);
	return lval_sexpr();
}


lval* builtin_add(lenv* e, lval* a) {
	return builtin_op(e, a, "+");
}

lval* builtin_sub(lenv* e, lval* a) {
	return builtin_op(e, a, "-");
}

lval* builtin_mul(lenv* e, lval* a) {
	return builtin_op(e, a, "*");
}

lval* builtin_div(lenv* e, lval* a) {
	return builtin_op(e, a, "/");
}

/* register a builtin function in lenv */
void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
	lval* k = lval_sym(name);
	lval* v = lval_fun(func);
	lenv_put(e, k, v);
	lval_del(k); lval_del(v);
}

void lenv_add_builtins(lenv* e) {
	/* list functions */
	lenv_add_builtin(e, "list", builtin_list);
	lenv_add_builtin(e, "head", builtin_head);
	lenv_add_builtin(e, "tail", builtin_tail);
	lenv_add_builtin(e, "eval", builtin_eval);
	lenv_add_builtin(e, "cons", builtin_cons);

	/* math functions */
	lenv_add_builtin(e, "+", builtin_add);
	lenv_add_builtin(e, "-", builtin_sub);
	lenv_add_builtin(e, "*", builtin_mul);
	lenv_add_builtin(e, "/", builtin_div);

	/* def function */
	lenv_add_builtin(e, "def", builtin_def);
}
lval* lval_eval_sexpr(lenv* e, lval* v) {
	/* eval the children first */
	for (int i = 0; i < v->count; i++) {
		v->cell[i] = lval_eval(e, v->cell[i]);
	}

	/* error checking */
	for (int i = 0; i < v->count; i++) {
		if (v->cell[i]->type == LVAL_ERR) {
			return lval_take(v, i);
		}
	}

	/* empty expr */
	if (v->count == 0) { return v; }

	/* single expression */
	if (v->count == 1) { return lval_take(v, 0); }

	/* ensure out first element is a symbol */
	lval* f = lval_pop(v, 0);
	if (f->type != LVAL_FUN) {
		lval_del(f); lval_del(v);
		return lval_err("First element is not a function");
	}

	/* call builtin with operator */
	lval* result = f->fun(e, v);
	lval_del(f);
	return result;
}

/* alreadt declared */
lval* lval_eval(lenv* e, lval* v) {
	/* look up sym in environment */
	if (v->type == LVAL_SYM) {
		lval* x = lenv_get(e, v);
		lval_del(v);
		return x;
	}

	/* evaluate sexpr */
	if (v->type == LVAL_SEXPR) {
		return lval_eval_sexpr(e, v);
	}

	/* else just return yourself */
	return v;
}


lval* lval_read_num(mpc_ast_t* t) {
	long x = strtol(t->contents, NULL, 10);
	return errno != ERANGE ? lval_num(x) : lval_err("invalid number");
}

lval* lval_read(mpc_ast_t* t) {
	/* if number of symbol return that */
	if (strstr(t->tag, "number")) { return lval_read_num(t); }
	if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }

	/* if root or sexpr then create empty list */
	lval* x = NULL;
	if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
	if (strstr(t->tag, "sexpr"))  { x = lval_sexpr(); }
	if (strstr(t->tag, "qexpr"))  { x = lval_qexpr(); }

	/* now fill that list with the contents of the sexpr */
	for (int i = 0; i < t->children_num; i++) {
		if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
		if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
		if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
		if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
		if (strcmp(t->children[i]->tag, "regex") == 0)  { continue; }
		x = lval_add(x, lval_read(t->children[i]));
	}
	/* then return the new list */
	return x;
}

int main(int argc, char** argv) {
	/* create some parsers */
	mpc_parser_t* Number = mpc_new("number");
	mpc_parser_t* Symbol = mpc_new("symbol");
	mpc_parser_t* Sexpr  = mpc_new("sexpr");
	mpc_parser_t* Qexpr  = mpc_new("qexpr");
	mpc_parser_t* Expr   = mpc_new("expr");

	/* Define them with this language */
	mpca_lang(MPC_LANG_DEFAULT,
	"                                                 \
		number   : /-?[0-9]+/ ;                       \
		symbol   : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/ ; \
		sexpr    : '(' <expr>* ')' | <number> ;       \
		qexpr    : '{' <expr>* '}' ;                  \
		expr     : <number> | <symbol> | <sexpr> | <qexpr> ; \
	",
	Number, Symbol, Sexpr, Qexpr, Expr);

	/* Print Exit Instructions */
	puts("Press Ctrl+c to Exit");

	lenv* e = lenv_new();
	lenv_add_builtins(e);

	while(1) {

		char* input = readline("lispy> ");
		add_history(input);

		mpc_result_t r;
		if (mpc_parse("<stdin>", input, Expr, &r)) {
			lval* x = lval_eval(e, lval_read(r.output));
			lval_println(x);
			lval_del(x);
			mpc_ast_delete(r.output);
		} else {
			/* otherwise print error */
			mpc_err_print(r.error);
			mpc_err_delete(r.error);
		}

		/* Free retrived input */
		free(input);
	}

	lenv_del(e);

	mpc_cleanup(5, Number, Symbol, Sexpr, Qexpr, Expr);
	return 0;
}
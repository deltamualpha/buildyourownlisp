#include <stdio.h>
#include <stdlib.h>
#include "mpc/mpc.h"

#include <editline/readline.h>

/* this macro ASSERTs a condition, then errors if it's NOT true */
#define LASSERT(args, cond, fmt, ...) \
    if (!(cond)) { \
        lval* err = lval_err(fmt, ##__VA_ARGS__); \
        lval_del(args); \
        return err; }
/* this macro ASSERTs an expected number of arguments */
#define LASSERT_NUM(func, args, num) \
    LASSERT(args, args->count == num, \
        "Function %s passed incorrect number of args. Got %i, expected %i.", \
        func, args->count, num);
/* this macro ASSERTs an expected type of a argument given an index */
#define LASSERT_TYPE(func, args, index, expect) \
    LASSERT(args, args->cell[index]->type == expect, \
        "Function %s passed bad type for arg %i. Got %s, expected %s.",\
        func, index, ltype_name(args->cell[index]->type), ltype_name(expect));

/* forward declarations */

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

mpc_parser_t* Number; 
mpc_parser_t* Symbol; 
mpc_parser_t* String; 
mpc_parser_t* Comment;
mpc_parser_t* Sexpr;  
mpc_parser_t* Qexpr;  
mpc_parser_t* Expr;

/*
 * lval setup
 */

enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_STR, LVAL_FUN, LVAL_SEXPR, LVAL_QEXPR };

typedef lval*(*lbuiltin)(lenv*, lval*);

struct lval {
    int type;

    /* Basic types */
    long num;
    char* err;
    char* sym;
    char* str;

    /* functions */
    lbuiltin builtin;
    lenv* env;
    lval* formals;
    lval* body;

    /* expression */
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
    v->builtin = func;
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

lval* lval_str(char* s) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_STR;
    v->str = malloc(sizeof(s) + 1);
    strcpy(v->str, s);
    return v;
}

lenv* lenv_new(void);
void lenv_del(lenv* e);

/* constructing a user-created function
 * 'formals' are required variables, 'body' is computation to perform */
lval* lval_lambda(lval* formals, lval* body) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_FUN;

    /* NULL for non-builtin functions */
    v->builtin = NULL;

    /* new environment to store actual calling data */
    v->env = lenv_new();

    /* set formals and body */
    v->formals = formals;
    v->body = body;
    return v;
}

char* ltype_name (int t) {
    switch(t) {
        case LVAL_FUN: return "Function";
        case LVAL_NUM: return "Number";
        case LVAL_ERR: return "Error";
        case LVAL_SYM: return "Symbol";
        case LVAL_STR: return "String";
        case LVAL_SEXPR: return "S-Expression";
        case LVAL_QEXPR: return "Q-Expression";
        default: return "Unknown";
    }
}

/*
 * lenv setup
 */
struct lenv {
    /* pointer to parent environment (NULL if global) */
    lenv* par;
    int count;
    /* list of strings */
    char** syms;
    /* list of pointers */
    lval** vals;
};

lenv* lenv_new(void) {
    lenv* e = malloc(sizeof(lenv));
    e->par = NULL;
    e->count = 0;
    e->syms = NULL;
    e->vals = NULL;
    return e;
}

/* 
 * working with lvals
 */

void lval_del(lval* v) {
    switch (v->type) {
        /* nothing else to delete for nums or funcs */
        case LVAL_NUM: break;

        /* for err or sym free strings */
        case LVAL_ERR: free(v->err); break;
        case LVAL_SYM: free(v->sym); break;
        case LVAL_STR: free(v->str); break;

        /* sexpr and qexpr delete everything, recursively */
        case LVAL_QEXPR:
        case LVAL_SEXPR:
            for (int i = 0; i < v->count; i++) {
                lval_del(v->cell[i]);
            }
            /* and the pointer itself */
            free(v->cell);
        break;
        /* delete user functions but not builtins */
        case LVAL_FUN:
            if (!v->builtin) {
                lenv_del(v->env);
                lval_del(v->formals);
                lval_del(v->body);
            }
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

    /* return the pointer we asked for;
     * v is MUTATED but still exists, sans pointer to x */
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

lenv* lenv_copy(lenv* e);

lval* lval_copy(lval* v) {
    lval* x = malloc(sizeof(lval));
    x->type = v->type;

    switch (v->type) {
        /* nums copy straight across */
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
        case LVAL_STR:
            x->str = malloc(strlen(v->str) + 1);
            strcpy(x->str, v->str);
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

        /* functions! */
        case LVAL_FUN:
            if (v->builtin) {
                x->builtin = v->builtin;
            } else {
                x->builtin = NULL;
                x->env = lenv_copy(v->env);
                x->formals = lval_copy(v->formals);
                x->body = lval_copy(v->body);
            }
        break;
    }

    return x;
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

/* forward declartion */
lval* builtin_eval(lenv* e, lval* a);
void lenv_put(lenv* e, lval* k, lval* v);
lval* builtin_list(lenv* e, lval* a);

lval* lval_call(lenv* e, lval* f, lval* a) {
    /* if a builtin, just do it */
    if (f->builtin) { return f->builtin(e, a); }

    /* record argument counts */
    int given = a->count;
    int total = f->formals->count;

    /* while there's still arguments to process */
    while(a->count) {
        /* we're only being clever if given < total */
        if (f->formals->count == 0) {
            lval_del(a);
            return lval_err(
                "Function passed too many arguments; got %i expected %i.",
                given, total);
        }

        /* Pop the first symbol off */
        lval* sym = lval_pop(f->formals, 0);

        /* Special Case to deal with '&' */
        if (strcmp(sym->sym, "&") == 0) {
            /* make sure something else follows it */
            if (f->formals->count != 1) {
                lval_del(a);
                return lval_err("Format invalid; & not followed by single symbol");
            }

            /* next formal is bound to remaining arguments */
            lval* nsym = lval_pop(f->formals, 0);
            lenv_put(f->env, nsym, builtin_list(e, a));
            lval_del(sym); lval_del(nsym);
            break;
        }

        /* Pop the next argument */
        lval* val = lval_pop(a, 0);
        /* Bind a copy into the function's environment */
        lenv_put(f->env, sym, val);
        /* delete symbol and value */
        lval_del(sym); lval_del(val);
    }

    /* argument list is now bound, so that can go */
    lval_del(a);

    /* more '&' handling */
    if (f->formals->count > 0 &&
        strcmp(f->formals->cell[0]->sym, "&") == 0) {

        /* check our validity */
        if (f->formals->count != 2) {
            return lval_err("Format invalid; & not followed by single symbol");
        }

        /* pop and delete & */
        lval_del(lval_pop(f->formals, 0));
        /* pop next symbol and create empty list */
        lval* sym = lval_pop(f->formals, 0);
        lval* val = lval_qexpr();

        /* bind to env and delete */
        lenv_put(f->env, sym, val);
        lval_del(sym); lval_del(val);
    }

    /* if we matched up, eval and return */
    if (f->formals->count == 0) {
        /* set env */
        f->env->par = e;

        /* evaluate */
        return builtin_eval(f->env, lval_add(lval_sexpr(), lval_copy(f->body)));
    } else {
        /* otherwise, return a function "partially" evaluated */
        return lval_copy(f);
    }
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

void lval_print_str(lval* v) {
    char* escaped = malloc(strlen(v->str) + 1);
    strcpy(escaped, v->str);
    escaped = mpcf_escape(escaped);
    printf("\"%s\"", escaped);
    free(escaped);
}

/* this was forward-declared */
void lval_print(lval* v) {
    switch (v->type) {
        case LVAL_NUM: printf("%li", v->num); break;
        case LVAL_ERR: printf("Error: %s", v->err); break;
        case LVAL_SYM: printf("%s", v->sym); break;
        case LVAL_STR: lval_print_str(v); break;
        /* recurse if it's an sexpr */
        case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
        case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
        /* functions are a little complicated */
        case LVAL_FUN:
            if (v->builtin) {
                printf("<builtin>");
            } else {
                printf("(\\ ");
                lval_print(v->formals);
                putchar(' ');
                lval_print(v->body);
                putchar(')');
            }
        break;
    }   
}

void lval_println(lval* v) {
    /* little util for printing lines */
    lval_print(v);
    putchar('\n');
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

    /* if we couldn't find anything at this level, check the parents */
    if (e->par) {
        return lenv_get(e->par, k);
    } else {
        return lval_err("Unknown symbol '%s'", k->sym);
    }
}

/* put new lval into the local lenv */
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

/* add an lval to the global environment */
void lenv_def(lenv* e, lval* k, lval* v) {
    /* iterate back up the chain */
    while(e->par) { 
        e = e->par;
    }
    lenv_put(e, k, v);
}

/* copy an lenv */
lenv* lenv_copy(lenv* e) {
    lenv* n = malloc(sizeof(lenv));
    n->par = e->par;
    n->count = e->count;
    n->syms = malloc(sizeof(char*) * n->count);
    n->vals = malloc(sizeof(lval*) * n->count);
    for (int i = 0; i < e->count; ++i) {
        n->syms[i] = malloc(strlen(e->syms[i]) + 1);
        strcpy(n->syms[i], e->syms[i]);
        n->vals[i] = lval_copy(e->vals[i]);
    }
    return n;
}

/* test if two lvals are equal 
 * works recursively, checking only relevant fields 
 * zero is falsy, everything else is truthy */
int lval_eq(lval* x, lval* y) {
    if (x->type != y->type) {
        return 0;
    }

    /* compare based on type */
    switch (x->type) {
        case LVAL_NUM: return (x->num == y->num);
        case LVAL_ERR: return (strcmp(x->err, y->err) == 0);
        case LVAL_SYM: return (strcmp(x->sym, y->sym) == 0);
        case LVAL_STR: return (strcmp(x->str, y->str) == 0);
        case LVAL_FUN: 
            if (x->builtin || y->builtin) {
                return x->builtin == y->builtin;
            } else {
                return lval_eq(x->formals, y->formals) &&
                    lval_eq(x->body, y->body);
            }
        case LVAL_QEXPR:
        case LVAL_SEXPR:
            if (x->count != y->count) { return 0; }
            for (int i = 0; i < x->count; ++i) {
                if (!lval_eq(x->cell[i], y->cell[i])) {
                    return 0;
                }
            }
            return 1;
        break;
    }
    return 0;
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

/* greater/less than? */
lval* builtin_ord(lenv* e, lval* a, char* op) {
    LASSERT_NUM(op, a, 2);
    LASSERT_TYPE(op, a, 0, LVAL_NUM);
    LASSERT_TYPE(op, a, 1, LVAL_NUM);

    int r;
    if (strcmp(op, ">")  == 0) { r = (a->cell[0]->num >  a->cell[1]->num); }
    if (strcmp(op, "<")  == 0) { r = (a->cell[0]->num <  a->cell[1]->num); }
    if (strcmp(op, ">=") == 0) { r = (a->cell[0]->num >= a->cell[1]->num); }
    if (strcmp(op, "<=") == 0) { r = (a->cell[0]->num <= a->cell[1]->num); }
    lval_del(a);
    return lval_num(r);
}

/* equal or not equal? */
lval* builtin_cmp(lenv* e, lval* a, char* op) {
    LASSERT_NUM(op, a, 2);
    int r;
    if (strcmp(op, "==") == 0) { r =  lval_eq(a->cell[0], a->cell[1]); }
    if (strcmp(op, "!=") == 0) { r = !lval_eq(a->cell[0], a->cell[1]); }
    lval_del(a);
    return lval_num(r);
}

/* if */
/* cell[0] is the check, cell[1,2] are expressions to eval on true/false */
lval* builtin_if(lenv* e, lval* a) {
    LASSERT_NUM("if", a, 3);
    LASSERT_TYPE("if", a, 0, LVAL_NUM);
    LASSERT_TYPE("if", a, 1, LVAL_QEXPR);
    LASSERT_TYPE("if", a, 2, LVAL_QEXPR);

    /* make the q-exprs into s-exprs, but don't eval yet */
    lval* x;
    a->cell[1]->type = LVAL_SEXPR;
    a->cell[2]->type = LVAL_SEXPR;

    if (a->cell[0]->num) {
        x = lval_eval(e, lval_pop(a, 1));
    } else {
        x = lval_eval(e, lval_pop(a, 2));
    }

    lval_del(a);
    return x;
}

/* anonymous functions */
lval* builtin_lambda(lenv* e, lval* a) {
    /* check we received two arguments */
    LASSERT_NUM("\\", a, 2);
    /* check arguments are q-expressions */
    LASSERT_TYPE("\\", a, 0, LVAL_QEXPR);
    LASSERT_TYPE("\\", a, 1, LVAL_QEXPR);

    /* check first q-expr contains only symbols */
    for (int i = 0; i < a->cell[0]->count; ++i) {
        LASSERT(a, (a->cell[0]->cell[i]->type == LVAL_SYM),
            "Cannot define a non-symbol. Got %s, expected %s.",
            ltype_name(a->cell[0]->cell[i]->type), ltype_name(LVAL_SYM));
    }

    /* pop the args and pass them to lval_lambda */
    lval* formals = lval_pop(a, 0);
    lval* body = lval_pop(a, 0);
    lval_del(a);

    return lval_lambda(formals, body);
}

lval* builtin_head(lenv* e, lval* a) {
    /* error check! Note the inversion of equality */
    LASSERT_NUM("head", a, 1);
    LASSERT_TYPE("head", a, 0, LVAL_QEXPR);
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
    LASSERT_NUM("tail", a, 1);
    LASSERT_TYPE("tail", a, 0, LVAL_QEXPR);
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
    LASSERT_NUM("eval", a, 1);
    LASSERT_TYPE("eval", a, 0, LVAL_QEXPR);

    /* make it into an sexpr and evaluate it! */
    lval* x = lval_take(a, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(e, x);
}

lval* builtin_join(lenv* e, lval* a) {
    for (int i = 0; i < a->count; i++) {
        LASSERT_TYPE("join", a, i, LVAL_QEXPR);
    }

    lval* x = lval_pop(a, 0);

    while(a->count) {
        x = lval_join(x, lval_pop(a, 0));
    }

    lval_del(a);
    return x;
}

lval* builtin_cons(lenv* e, lval* a) {
    LASSERT_NUM("cons", a, 2);
    LASSERT_TYPE("cons", a, 1, LVAL_QEXPR);

    /* get the first item from the input
     * coerce it to be a qexpr
     * then join the new qexpr to the existing list */
    lval* z = lval_join(lval_add(lval_qexpr(), lval_pop(a, 0)), lval_take(a, 0));

    /* return a pointer to the new lval */
    return z;
}

lval* builtin_var(lenv* e, lval* a, char* func) {
    LASSERT_TYPE(func, a, 0, LVAL_QEXPR);

    lval* syms = a->cell[0];

    for (int i = 0; i < syms->count; ++i) {
        LASSERT(a, (syms->cell[i]->type == LVAL_SYM),
            "Function %s cannot define non-symbol. Got %s, expected %s",
            func, ltype_name(syms->cell[i]->type), ltype_name(LVAL_SYM));
    }

    LASSERT(a, (syms->count == a->count-1),
        "Function %s cannot define incorrect number of values to symbols. %i vs. %i.",
        func, syms->count, a->count-1);

    for (int i = 0; i < syms->count; ++i) {
        /* def is global, put/= is local */
        if (strcmp(func, "def") == 0) {
            lenv_def(e, syms->cell[i], a->cell[i+1]);
        }
        if (strcmp(func, "put") == 0) {
            lenv_put(e, syms->cell[i], a->cell[i+1]);
        }
    }

    lval_del(a);
    return lval_sexpr();
}

lval* lval_read(mpc_ast_t* t);

lval* builtin_load(lenv* e, lval* a) {
    LASSERT_NUM("load", a, 1);
    LASSERT_TYPE("load", a, 0, LVAL_STR);

    mpc_result_t r;
    if (mpc_parse_contents(a->cell[0]->str, Expr, &r)) {
        /* read it */
        lval* expr = lval_read(r.output);
        mpc_ast_delete(r.output);

        while(expr->count) {
            lval* x = lval_eval(e, lval_pop(expr, 0));
            /* error check! */
            if (x->type == LVAL_ERR) { lval_println(x); }
            lval_del(x);
        }

        lval_del(expr);
        lval_del(a);

        return lval_sexpr();
    } else {
        /* parse error */
        char* err_msg = mpc_err_string(r.error);
        mpc_err_delete(r.error);

        lval* err = lval_err("Could not load library; %s", err_msg);
        free(err_msg);
        lval_del(a);
        return err;
    }
}

lval* builtin_print(lenv* e, lval* a) {

    /* Print each argument followed by a space */
    for (int i = 0; i < a->count; i++) {
        lval_print(a->cell[i]); putchar(' ');
    }

    /* Print a newline and delete arguments */
    putchar('\n');
    lval_del(a);

    return lval_sexpr();
}

lval* builtin_error(lenv* e, lval* a) {
    LASSERT_NUM("error", a, 1);
    LASSERT_TYPE("error", a, 0, LVAL_STR);

    /* Construct Error from first argument */
    lval* err = lval_err(a->cell[0]->str);

    /* Delete arguments and return */
    lval_del(a);
    return err;
}

lval* builtin_def(lenv* e, lval* a) {
    return builtin_var(e, a, "def");
}

lval* builtin_put(lenv* e, lval* a) {
    return builtin_var(e, a, "=");
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

lval* builtin_gt(lenv* e, lval* a) {
    return builtin_ord(e, a, ">");
}

lval* builtin_lt(lenv* e, lval* a) {
    return builtin_ord(e, a, "<");
}

lval* builtin_ge(lenv* e, lval* a) {
    return builtin_ord(e, a, ">=");
}

lval* builtin_le(lenv* e, lval* a) {
    return builtin_ord(e, a, "<=");
}

lval* builtin_eq(lenv* e, lval* a) {
    return builtin_cmp(e, a, "==");
}

lval* builtin_ne(lenv* e, lval* a) {
    return builtin_cmp(e, a, "!=");
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
    lenv_add_builtin(e, "list",  builtin_list);
    lenv_add_builtin(e, "head",  builtin_head);
    lenv_add_builtin(e, "tail",  builtin_tail);
    lenv_add_builtin(e, "eval",  builtin_eval);
    lenv_add_builtin(e, "cons",  builtin_cons);
    lenv_add_builtin(e, "join",  builtin_join);
    /* math functions */
    lenv_add_builtin(e, "+",     builtin_add);
    lenv_add_builtin(e, "-",     builtin_sub);
    lenv_add_builtin(e, "*",     builtin_mul);
    lenv_add_builtin(e, "/",     builtin_div);
    /* def/put functions */
    lenv_add_builtin(e, "def",   builtin_def);
    lenv_add_builtin(e, "=",     builtin_put);
    lenv_add_builtin(e, "\\",    builtin_lambda);
    lenv_add_builtin(e, "fun",   builtin_lambda);
    /* comparison functions */
    lenv_add_builtin(e, ">",     builtin_gt);
    lenv_add_builtin(e, "<",     builtin_lt);
    lenv_add_builtin(e, ">=",    builtin_ge);
    lenv_add_builtin(e, "<=",    builtin_le);
    lenv_add_builtin(e, "==",    builtin_eq);
    lenv_add_builtin(e, "!=",    builtin_ne);
    lenv_add_builtin(e, "if",    builtin_if);
    /* string functions */
    lenv_add_builtin(e, "load",  builtin_load);
    lenv_add_builtin(e, "error", builtin_error);
    lenv_add_builtin(e, "print", builtin_print);
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
    lval* result = lval_call(e, f, v);
    lval_del(f);
    return result;
}

/* already declared */
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

lval* lval_read_string(mpc_ast_t* t) {
    t->contents[strlen(t->contents)-1] = '\0'; //remove trailing quote
    char* unescaped = malloc(strlen(t->contents+1)+1);
    strcpy(unescaped, t->contents+1); // copy everything after leading quote
    unescaped = mpcf_escape(unescaped);
    lval* str = lval_str(unescaped);
    free(unescaped);
    return str;
}

lval* lval_read(mpc_ast_t* t) {
    /* if number of symbol return that */
    if (strstr(t->tag, "number")) { return lval_read_num(t); }
    if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }
    if (strstr(t->tag, "string")) { return lval_read_string(t); }

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
        if (strstr(t->children[i]->tag, "comment"))     { continue; }
        x = lval_add(x, lval_read(t->children[i]));
    }
    /* then return the new list */
    return x;
}

int main(int argc, char** argv) {
    /* create some parsers */
    Number  = mpc_new("number");
    Symbol  = mpc_new("symbol");
    Sexpr   = mpc_new("sexpr");
    String  = mpc_new("string");
    Comment = mpc_new("comment");
    Qexpr   = mpc_new("qexpr");
    Expr    = mpc_new("expr");

    /* Define them with this language */
    mpca_lang(MPC_LANG_DEFAULT,
    "   number  : /-?[0-9]+/ ;                          \
        symbol  : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&\\.]+/ ; \
        sexpr   : '(' <expr>* ')' | <number> ;          \
        qexpr   : '{' <expr>* '}' ;                     \
        string  : /\"(\\\\.|[^\"])*\"/ ;                \
        comment : /;[^\\r\\n]*/ ;                       \
        expr    : <number> | <symbol>  | <sexpr>        \
                | <qexpr>  | <comment> | <string>       \
                | /^/ <expr>* /$/ ;                     ",
    Number, Symbol, Sexpr, Qexpr, String, Comment, Expr);

    lenv* e = lenv_new();
    lenv_add_builtins(e);

    if (argc == 1) {
        /* Print Exit Instructions */
        puts("Press Ctrl+c to Exit");

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
    }

    if (argc >= 2) {
        /* loop through supplied filenames */
        for (int i = 1; i < argc; ++i) {

            /* build lval with filename */
            lval* args = lval_add(lval_sexpr(), lval_str(argv[i]));

            /* load the file */
            lval* x = builtin_load(e, args);

            /* output error */
            if (x->type == LVAL_ERR) {
                lval_println(x);
            }

            /* cleanup */
            lval_del(x);
        }
    }

    lenv_del(e);

    mpc_cleanup(7, Number, Symbol, Sexpr, Qexpr, Comment, Expr, String);
    return 0;
}
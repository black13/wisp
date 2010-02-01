#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <gmp.h>
#include "object.h"
#include "cons.h"
#include "symtab.h"
#include "common.h"
#include "eval.h"
#include "str.h"
#include "reader.h"
#include "number.h"
#include "vector.h"

/* From lisp_math.c */
void lisp_math_init ();
object_t *num_eq (object_t * lst);

/* Various basic stuff */

object_t *lisp_and (object_t * lst)
{
  object_t *r = T, *p = lst;
  while (CONSP (p))
    {
      obj_destroy (r);
      r = eval (CAR (p));
      CHECK (r);
      if (r == NIL)
	return NIL;
      p = CDR (p);
    }
  if (p != NIL)
    THROW (improper_list, UPREF (lst));
  return UPREF (r);
}

object_t *lisp_or (object_t * lst)
{
  object_t *r = NIL, *p = lst;
  while (CONSP (p))
    {
      r = eval (CAR (p));
      CHECK (r);
      if (r != NIL)
	return UPREF (r);
      p = CDR (p);
    }
  if (p != NIL)
    THROW (improper_list, UPREF (lst));
  return NIL;
}

object_t *lisp_cons (object_t * lst)
{
  REQ (lst, 2, c_sym ("cons"));
  return c_cons (UPREF (CAR (lst)), UPREF (CAR (CDR (lst))));
}

object_t *lisp_quote (object_t * lst)
{
  REQ (lst, 1, c_sym ("quote"));
  return UPREF (CAR (lst));
}

object_t *lambda_f (object_t * lst)
{
  if (!is_func_form (lst))
    THROW (c_sym ("bad-function-form"), UPREF (lst));
  return c_cons (lambda, UPREF (lst));
}

object_t *defun (object_t * lst)
{
  if (!SYMBOLP (CAR (lst)) || !is_func_form (CDR (lst)))
    THROW (c_sym ("bad-function-form"), UPREF (lst));
  object_t *f = c_cons (lambda, UPREF (CDR (lst)));
  SET (CAR (lst), f);
  return UPREF (CAR (lst));
}

object_t *defmacro (object_t * lst)
{
  if (!SYMBOLP (CAR (lst)) || !is_func_form (CDR (lst)))
    THROW (c_sym ("bad-function-form"), UPREF (lst));
  object_t *f = c_cons (macro, UPREF (CDR (lst)));
  SET (CAR (lst), f);
  return f;
}

object_t *lisp_cdr (object_t * lst)
{
  REQ (lst, 1, c_sym ("cdr"));
  if (CAR (lst) == NIL)
    return NIL;
  if (!LISTP (CAR (lst)))
    THROW (wrong_type, CAR (lst));
  return UPREF (CDR (CAR (lst)));
}

object_t *lisp_car (object_t * lst)
{
  REQ (lst, 1, c_sym ("car"));
  if (CAR (lst) == NIL)
    return NIL;
  if (!LISTP (CAR (lst)))
    THROW (wrong_type, CAR (lst));
  return UPREF (CAR (CAR (lst)));
}

object_t *lisp_list (object_t * lst)
{
  return UPREF (lst);
}

object_t *lisp_if (object_t * lst)
{
  REQM (lst, 2, wrong_number_of_arguments);
  object_t *r = eval (CAR (lst));
  CHECK (r);
  if (r != NIL)
    {
      obj_destroy (r);
      return eval (CAR (CDR (lst)));
    }
  return eval_body (CDR (CDR (lst)));
}

object_t *progn (object_t * lst)
{
  return eval_body (lst);
}

object_t *let (object_t * lst)
{
  /* verify structure */
  if (!LISTP (CAR (lst)))
    THROW (c_sym ("bad-let-form"), UPREF (lst));
  object_t *vlist = CAR (lst);
  while (vlist != NIL)
    {
      object_t *p = CAR (vlist);
      if (!LISTP (p))
	THROW (c_sym ("bad-let-form"), UPREF (lst));
      if (!SYMBOLP (CAR (p)))
	THROW (c_sym ("bad-let-form"), UPREF (lst));
      vlist = CDR (vlist);
    }

  object_t *p;
  p = vlist = CAR (lst);
  int cnt = 0;
  while (p != NIL)
    {
      object_t *pair = CAR (p);
      object_t *e = eval (CAR (CDR (pair)));
      if (e == err_symbol)
	{
	  /* Undo scoping */
	  p = vlist;
	  while (cnt)
	    {
	      sympop (CAR (CAR (p)));
	      p = CDR (p);
	      cnt--;
	    }
	  return err_symbol;
	}
      sympush (CAR (pair), e);
      obj_destroy (e);
      p = CDR (p);
      cnt++;
    }
  object_t *r = eval_body (CDR (lst));
  p = vlist;
  while (p != NIL)
    {
      object_t *pair = CAR (p);
      sympop (CAR (pair));
      p = CDR (p);
    }
  return r;
}

object_t *lisp_while (object_t * lst)
{
  REQM (lst, 1, c_sym ("while"));
  object_t *r = NIL, *cond = CAR (lst), *body = CDR (lst);
  object_t *condr;
  while ((condr = eval (cond)) != NIL)
    {
      obj_destroy (r);
      obj_destroy (condr);
      r = eval_body (body);
    }
  return r;
}

/* Equality */

object_t *eq (object_t * lst)
{
  REQ (lst, 2, c_sym ("eq"));
  object_t *a = CAR (lst);
  object_t *b = CAR (CDR (lst));
  if (a == b)
    return T;
  return NIL;
}

object_t *eql (object_t * lst)
{
  REQ (lst, 2, c_sym ("eql"));
  object_t *a = CAR (lst);
  object_t *b = CAR (CDR (lst));
  if (a->type != b->type)
    return NIL;
  switch (a->type)
    {
    case INT:
    case FLOAT:
      return num_eq (lst);
      break;
    case SYMBOL:
    case CONS:
      if (a == b)
	return T;
      break;
    case STRING:
      if (OSTRLEN (a) == OSTRLEN (b))
	if (memcmp (OSTR (a), OSTR (b), OSTRLEN (a)) == 0)
	  return T;
      break;
    case VECTOR:
      return NIL;
      break;
    case CFUNC:
    case SPECIAL:
      if (FVAL (a) == FVAL (b))
	return T;
      break;
    }
  return NIL;
}

object_t *lisp_hash (object_t * lst)
{
  REQ (lst, 1, c_sym ("hash"));
  return c_int (obj_hash (CAR (lst)));
}

object_t *lisp_print (object_t * lst)
{
  REQ (lst, 1, c_sym ("print"));
  obj_print (CAR (lst), 1);
  return NIL;
}

/* Symbol table */

object_t *lisp_set (object_t * lst)
{
  REQ (lst, 2, c_sym ("set"));
  if (!SYMBOLP (CAR (lst)))
    THROW (wrong_type, c_cons (c_sym ("set"), CAR (lst)));
  if (CONSTANTP (CAR (lst)))
    THROW (c_sym ("setting-constant"), CAR (lst));

  SET (CAR (lst), CAR (CDR (lst)));
  return UPREF (CAR (CDR (lst)));
}

object_t *lisp_value (object_t * lst)
{
  REQ (lst, 1, c_sym ("value"));
  if (!SYMBOLP (CAR (lst)))
    THROW (wrong_type, c_cons (c_sym ("value"), CAR (lst)));

  return UPREF (GET (CAR (lst)));
}

object_t *symbol_name (object_t * lst)
{
  REQ (lst, 1, c_sym ("symbol-name"));
  if (!SYMBOLP (CAR (lst)))
    THROW (wrong_type, UPREF (CAR (lst)));
  return c_strs (xstrdup (SYMNAME (CAR (lst))));
}

/* String */

object_t *lisp_concat (object_t * lst)
{
  REQ (lst, 2, c_sym ("concat"));
  object_t *a = CAR (lst);
  object_t *b = CAR (CDR (lst));
  if (!STRINGP (a))
    THROW (wrong_type, UPREF (a));
  if (!STRINGP (b))
    THROW (wrong_type, UPREF (b));
  return str_cat (a, b);
}

/* Predicates */

object_t *nullp (object_t * lst)
{
  REQ (lst, 1, c_sym ("nullp"));
  if (CAR (lst) == NIL)
    return T;
  return NIL;
}

object_t *funcp (object_t * lst)
{
  REQ (lst, 1, c_sym ("funcp"));
  if (FUNCP (CAR (lst)))
    return T;
  return NIL;
}

object_t *listp (object_t * lst)
{
  REQ (lst, 1, c_sym ("listp"));
  if (LISTP (CAR (lst)))
    return T;
  return NIL;
}

object_t *symbolp (object_t * lst)
{
  REQ (lst, 1, c_sym ("symbolp"));
  if (SYMBOLP (CAR (lst)))
    return T;
  return NIL;
}

object_t *numberp (object_t * lst)
{
  REQ (lst, 1, c_sym ("numberp"));
  if (NUMP (CAR (lst)))
    return T;
  return NIL;
}

object_t *stringp (object_t * lst)
{
  REQ (lst, 1, c_sym ("stringp"));
  if (STRINGP (CAR (lst)))
    return T;
  return NIL;
}

object_t *integerp (object_t * lst)
{
  REQ (lst, 1, c_sym ("integerp"));
  if (INTP (CAR (lst)))
    return T;
  return NIL;
}

object_t *floatp (object_t * lst)
{
  REQ (lst, 1, c_sym ("floatp"));
  if (FLOATP (CAR (lst)))
    return T;
  return NIL;
}

object_t *vectorp (object_t * lst)
{
  REQ (lst, 1, c_sym ("vectorp"));
  if (VECTORP (CAR (lst)))
    return T;
  return NIL;
}

/* Input/Output */

object_t *lisp_load (object_t * lst)
{
  REQ (lst, 1, c_sym ("load"));
  object_t *str = CAR (lst);
  if (!STRINGP (str))
    THROW (wrong_type, UPREF (str));
  char *filename = OSTR (str);
  int r = load_file (NULL, filename, 0);
  if (!r)
    THROW (c_sym ("load-file-error"), UPREF (str));
  return T;
}

object_t *lisp_eval_string (object_t * lst)
{
  REQ (lst, 1, c_sym ("eval-string"));
  object_t *stro = CAR (lst);
  if (!STRINGP (stro))
    THROW (wrong_type, UPREF (stro));
  char *str = OSTR (stro);
  reader_t *r = reader_create (NULL, str, "eval-string", 0);
  object_t *sexp = read_sexp (r);
  if (sexp == err_symbol)
    THROW (c_sym ("parse-error"), UPREF (stro));
  object_t *ret = eval (sexp);
  obj_destroy (sexp);
  return ret;
}

/* Error handling */

object_t *throw (object_t * lst)
{
  THROW (UPREF (CAR (lst)), UPREF (CAR (CDR (lst))));
}

object_t *catch (object_t * lst)
{
  object_t *csym = eval (CAR (lst));
  CHECK (csym);
  object_t *body = CDR (lst);
  object_t *r = eval_body (body);
  if (r == err_symbol)
    {
      if (csym == err_thrown)
	{
	  obj_destroy (csym);
	  obj_destroy (err_thrown);
	  return err_attach;
	}
      else
	return err_symbol;
    }
  return r;
}

/* Vectors */

object_t *lisp_vset (object_t * lst)
{
  REQ (lst, 3, c_sym ("vset"));
  object_t *vec = CAR (lst);
  object_t *ind = CAR (CDR (lst));
  object_t *val = CAR (CDR (CDR (lst)));
  if (!VECTORP (vec))
    THROW (wrong_type, UPREF (vec));
  if (!INTP (ind))
    THROW (wrong_type, UPREF (ind));
  return vset_check (vec, ind, val);
}

object_t *lisp_vget (object_t * lst)
{
  REQ (lst, 2, c_sym ("vget"));
  object_t *vec = CAR (lst);
  object_t *ind = CAR (CDR (lst));
  if (!VECTORP (vec))
    THROW (wrong_type, UPREF (vec));
  if (!INTP (ind))
    THROW (wrong_type, UPREF (ind));
  return vget_check (vec, ind);
}

object_t *lisp_vlength (object_t * lst)
{
  REQ (lst, 1, c_sym ("vlength"));
  object_t *vec = CAR (lst);
  if (!VECTORP (vec))
    THROW (wrong_type, UPREF (vec));
  return c_int (VLENGTH (vec));
}

object_t *make_vector (object_t * lst)
{
  REQ (lst, 2, c_sym ("make-vector"));
  object_t *len = CAR (lst);
  object_t *o = CAR (CDR (lst));
  if (!INTP (len))
    THROW (wrong_type, UPREF (len));
  return c_vec (into2int (len), o);
}

object_t *lisp_vconcat (object_t * lst)
{
  REQ (lst, 2, c_sym ("vconcat"));
  object_t *a = CAR (lst);
  object_t *b = CAR (CDR (lst));
  if (!VECTORP (a))
    THROW (wrong_type, UPREF (a));
  if (!VECTORP (b))
    THROW (wrong_type, UPREF (b));
  return vector_concat (a, b);
}

/* Internals */

object_t *lisp_refcount (object_t * lst)
{
  REQ (lst, 1, c_sym ("refcount"));
  return c_int (CAR (lst)->refs);
}

object_t *lisp_eval_depth (object_t * lst)
{
  REQ (lst, 0, c_sym ("eval-depth"));
  return c_int (stack_depth);
}

object_t *lisp_max_eval_depth (object_t * lst)
{
  REQX (lst, 1, c_sym ("max-eval-depth"));
  if (lst == NIL)
    return c_int (max_stack_depth);
  object_t *arg = CAR (lst);
  if (!INTP (arg))
    THROW (wrong_type, UPREF (arg));
  int new_depth = into2int (arg);
  if (new_depth < 10)
    return NIL;
  max_stack_depth = new_depth;
  return UPREF (arg);
}

/* Installs all of the above functions. */
void lisp_init ()
{
  /* Maths */
  lisp_math_init ();

  /* Various */
  SSET (c_sym ("and"), c_special (&lisp_and));
  SSET (c_sym ("or"), c_special (&lisp_or));
  SSET (c_sym ("quote"), c_special (&lisp_quote));
  SSET (c_sym ("lambda"), c_special (&lambda_f));
  SSET (c_sym ("defun"), c_special (&defun));
  SSET (c_sym ("defmacro"), c_special (&defmacro));
  SSET (c_sym ("car"), c_cfunc (&lisp_car));
  SSET (c_sym ("cdr"), c_cfunc (&lisp_cdr));
  SSET (c_sym ("list"), c_cfunc (&lisp_list));
  SSET (c_sym ("if"), c_special (&lisp_if));
  SSET (c_sym ("not"), c_cfunc (&nullp));
  SSET (c_sym ("progn"), c_special (&progn));
  SSET (c_sym ("let"), c_special (&let));
  SSET (c_sym ("while"), c_special (&lisp_while));
  SSET (c_sym ("eval"), c_cfunc (&eval_body));
  SSET (c_sym ("print"), c_cfunc (&lisp_print));
  SSET (c_sym ("cons"), c_cfunc (&lisp_cons));

  /* Symbol table */
  SSET (c_sym ("set"), c_cfunc (&lisp_set));
  SSET (c_sym ("value"), c_cfunc (&lisp_value));
  SSET (c_sym ("symbol-name"), c_cfunc (&symbol_name));

  /* Strings */
  SSET (c_sym ("concat2"), c_cfunc (&lisp_concat));

  /* Equality */
  SSET (c_sym ("eq"), c_cfunc (&eq));
  SSET (c_sym ("eql"), c_cfunc (&eql));
  SSET (c_sym ("hash"), c_cfunc (&lisp_hash));

  /* Predicates */
  SSET (c_sym ("nullp"), c_cfunc (&nullp));
  SSET (c_sym ("funcp"), c_cfunc (&funcp));
  SSET (c_sym ("listp"), c_cfunc (&listp));
  SSET (c_sym ("symbolp"), c_cfunc (&symbolp));
  SSET (c_sym ("stringp"), c_cfunc (&stringp));
  SSET (c_sym ("numberp"), c_cfunc (&numberp));
  SSET (c_sym ("integerp"), c_cfunc (&integerp));
  SSET (c_sym ("floatp"), c_cfunc (&floatp));
  SSET (c_sym ("vectorp"), c_cfunc (&vectorp));

  /* Input/Output */
  SSET (c_sym ("load"), c_cfunc (&lisp_load));
  SSET (c_sym ("eval-string"), c_cfunc (&lisp_eval_string));

  /* Error handling */
  SSET (c_sym ("throw"), c_cfunc (&throw));
  SSET (c_sym ("catch"), c_special (&catch));

  /* Vectors */
  SSET (c_sym ("vset"), c_cfunc (&lisp_vset));
  SSET (c_sym ("vget"), c_cfunc (&lisp_vget));
  SSET (c_sym ("vlength"), c_cfunc (&lisp_vlength));
  SSET (c_sym ("make-vector"), c_cfunc (&make_vector));
  SSET (c_sym ("vconcat"), c_cfunc (&lisp_vconcat));

  /* Internals */
  SSET (c_sym ("refcount"), c_cfunc (&lisp_refcount));
  SSET (c_sym ("eval-depth"), c_cfunc (&lisp_eval_depth));
  SSET (c_sym ("max-eval-depth"), c_cfunc (&lisp_max_eval_depth));
}
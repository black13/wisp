#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "object.h"
#include "symtab.h"
#include "common.h"
#include "cons.h"
#include "eval.h"
#include "str.h"
#include "reader.h"
#include "number.h"
#include "vector.h"

static void read_error (reader_t * r, char *str);
static void addpop (reader_t * r);
static void reset (reader_t * r);

char *wisproot = NULL;

char *atom_chars =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
  "0123456789!#$%^&*-_=+|\\/?.~<>";
char *prompt = "wisp> ";

reader_t *reader_create (FILE * fid, char *str, char *name, int interactive)
{
  reader_t *r = xmalloc (sizeof (reader_t));
  r->fid = fid;
  r->strp = r->str = str;
  r->name = name ? name : "<unknown>";
  r->interactive = interactive;
  r->prompt = prompt;
  r->linecnt = 1;
  r->eof = 0;
  r->error = 0;
  r->shebang = -1 + interactive;
  r->done = 0;

  /* read buffers */
  r->buflen = 1024;
  r->bufp = r->buf = xmalloc (r->buflen + 1);
  r->readbuflen = 8;
  r->readbufp = r->readbuf = xmalloc (r->readbuflen * sizeof (int));

  /* state stack */
  r->ssize = 32;
  r->base = r->state = xmalloc (r->ssize * sizeof (rstate_t));
  return r;
}

void reader_destroy (reader_t * r)
{
  reset (r);
  xfree (r->buf);
  xfree (r->readbuf);
  xfree (r->base);
  xfree (r);
}

/* Read next character in the stream. */
static int reader_getc (reader_t * r)
{
  int c;
  if (r->readbufp > r->readbuf)
    {
      c = *(r->readbufp);
      r->readbufp--;
      return c;
    }
  if (r->str != NULL)
    {
      c = *(r->strp);
      if (c != '\0')
	r->strp++;
      else
	return EOF;
    }
  else
    c = fgetc (r->fid);
  return c;
}

/* Unread a byte. */
static void reader_putc (reader_t * r, int c)
{
  r->readbufp++;
  if (r->readbufp == r->readbuf + r->readbuflen)
    {
      r->readbuflen *= 2;
      r->readbuf = xrealloc (r->readbuf, sizeof (int) * r->readbuflen);
      r->readbufp = r->readbuf + r->readbuflen / 2;
    }
  *(r->readbufp) = c;
}

/* Consume remaining whitespace on line, including linefeed. */
static void consume_whitespace (reader_t * r)
{
  int c = reader_getc (r);
  while (strchr (" \t\r", c) != NULL)
    c = reader_getc (r);
  if (c != '\n')
    reader_putc (r, c);
}

/* Consume remaining characters on line, including linefeed. */
static void consume_line (reader_t * r)
{
  int c = reader_getc (r);
  while (c != '\n' && c != EOF)
    c = reader_getc (r);
  if (c != '\n')
    reader_putc (r, c);
}

/* Return height of sexp stack. */
static size_t stack_height (reader_t * r)
{
  return (r->state - r->base);
}

/* Push new list onto the sexp stack. */
static void push (reader_t * r)
{
  r->state++;
  if (r->state == r->base + r->ssize)
    {
      r->ssize *= 2;
      r->base = xrealloc (r->base, sizeof (rstate_t *) * r->ssize);
      r->state = r->base + r->ssize / 2;
    }
  /* clear the state */
  r->state->quote_mode = 0;
  r->state->dotpair_mode = 0;
  r->state->vector_mode = 0;
  r->state->head = r->state->tail = c_cons (NIL, NIL);
}

/* Remove top object from the sexp stack. */
static object_t *pop (reader_t * r)
{
  if (!r->done && stack_height (r) <= 1)
    {
      read_error (r, "unbalanced parenthesis");
      return err_symbol;
    }
  if (!r->done && r->state->dotpair_mode == 1)
    {
      read_error (r, "missing cdr object for dotted pair");
      return err_symbol;
    }
  object_t *p = CDR (r->state->head);
  CDR (r->state->head) = NIL;
  obj_destroy (r->state->head);
  if (r->state->vector_mode)
    {
      r->state--;
      object_t *v = list2vector (p);
      obj_destroy (p);
      return v;
    }
  r->state--;
  return p;
}

static void reset_buf (reader_t * r)
{
  r->bufp = r->buf;
  *(r->bufp) = '\0';
}

/* Remove top object from the sexp stack. */
static void reset (reader_t * r)
{
  r->done = 1;
  while (r->state != r->base)
    obj_destroy (pop (r));
  reset_buf (r);
  r->readbufp = r->readbuf;
  r->done = 0;
}

/* Print an error message. */
static void read_error (reader_t * r, char *str)
{
  fprintf (stderr, "%s:%d: %s\n", r->name, r->linecnt, str);
  consume_line (r);
  reset (r);
  r->error = 1;
}

/* Determine if top list is empty. */
static int list_empty (reader_t * r)
{
  return CDR (r->state->head) == NIL;
}

static void print_prompt (reader_t * r)
{
  if (r->interactive && stack_height (r) == 1)
    printf ("%s", r->prompt);
}

/* Push a new object into the current list. */
static void add (reader_t * r, object_t * o)
{
  if (r->state->dotpair_mode == 2)
    {
      /* CDR already filled. Cannot add more. */
      read_error (r, "invalid dotted pair syntax - too many objects");
      return;
    }

  if (!r->state->dotpair_mode)
    {
      CDR (r->state->tail) = c_cons (o, NIL);
      r->state->tail = CDR (r->state->tail);
      if (r->state->quote_mode)
	addpop (r);
    }
  else
    {
      CDR (r->state->tail) = o;
      r->state->dotpair_mode = 2;
      if (r->state->quote_mode)
	addpop (r);
    }
}

/* Pop sexp stack and add it to the new top list. */
static void addpop (reader_t * r)
{
  object_t *o = pop (r);
  if (!r->error)
    add (r, o);
}

/* Append character to buffer. */
static void buf_append (reader_t * r, char c)
{
  if (r->bufp == r->buf + r->buflen)
    {
      r->buflen *= 2;
      r->buf = xrealloc (r->buf, r->buflen + 1);
      r->bufp = r->buf + r->buflen / 2;
    }
  *(r->bufp) = c;
  *(r->bufp + 1) = '\0';
  r->bufp++;
}

/* Load into buffer until character, ignoring escaped ones. */
static int buf_read (reader_t * r, char *halt)
{
  int esc = 0;
  int c = reader_getc (r);
  esc = 0;
  if (c == '\\')
    {
      c = reader_getc (r);
      esc = 1;
    }
  while ((esc || strchr (halt, c) == NULL) && (c != EOF))
    {
      buf_append (r, c);
      c = reader_getc (r);
      esc = 0;
      if (c == '\\')
	{
	  c = reader_getc (r);
	  esc = 1;
	}
    }
  reader_putc (r, c);
  return !esc;
}

/* Turn string in buffer into string object. */
static object_t *parse_str (reader_t * r)
{
  size_t size = r->bufp - r->buf;
  char *str = xstrdup (r->buf);
  reset_buf (r);
  return c_str (str, size);
}

/* Turn string in buffer into atom object. */
static object_t *parse_atom (reader_t * r)
{
  char *str = r->buf;
  char *end;

  /* Detect integer */
  int i = strtol (str, &end, 10);
  (void) i;
  if (end != str && *end == '\0')
    {
      object_t *o = c_ints (str);
      reset_buf (r);
      return o;
    }

  /* Detect float */
  int d = strtod (str, &end);
  (void) d;
  if (end != str && *end == '\0')
    {
      object_t *o = c_floats (str);
      reset_buf (r);
      return o;
    }

  /* Might be a symbol then */
  char *p = r->buf;
  while (p <= r->bufp)
    {
      if (strchr (atom_chars, *p) == NULL)
	{
	  char *errstr = xstrdup ("invalid symbol character: X");
	  errstr[strlen (errstr) - 1] = *p;
	  read_error (r, errstr);
	  xfree (errstr);
	  return NIL;
	}
      p++;
    }
  object_t *o = c_sym (r->buf);
  reset_buf (r);
  return o;
}

/* Read a single sexp from the reader. */
object_t *read_sexp (reader_t * r)
{
  /* Check for a shebang line. */
  if (r->shebang == -1)
    {
      char str[2];
      str[0] = reader_getc (r);
      str[1] = reader_getc (r);
      if (str[0] == '#' && str[1] == '!')
	{
	  /* Looks like a she-bang line. */
	  r->shebang = 1;
	  consume_line (r);
	}
      else
	{
	  r->shebang = 0;
	  reader_putc (r, str[1]);
	  reader_putc (r, str[0]);
	}
    }

  r->done = 0;
  r->error = 0;
  push (r);
  print_prompt (r);
  while (!r->eof && !r->error && (list_empty (r) || stack_height (r) > 1))
    {
      int nc, c = reader_getc (r);
      switch (c)
	{
	case EOF:
	  r->eof = 1;
	  break;

	  /* Comments */
	case ';':
	  consume_line (r);
	  break;

	  /* Dotted pair */
	case '.':
	  nc = reader_getc (r);
	  if (strchr (" \t\r\n()", nc) != NULL)
	    {
	      if (r->state->dotpair_mode > 0)
		read_error (r, "invalid dotted pair syntax");
	      else if (r->state->vector_mode > 0)
		read_error (r, "dotted pair not allowed in vector");
	      else
		{
		  r->state->dotpair_mode = 1;
		  reader_putc (r, nc);
		}
	    }
	  else
	    {
	      /* Turn it into a decimal point. */
	      reader_putc (r, nc);
	      reader_putc (r, '.');
	      reader_putc (r, '0');
	    }
	  break;

	  /* Whitespace */
	case '\n':
	  r->linecnt++;
	  print_prompt (r);
	case ' ':
	case '\t':
	case '\r':
	  break;

	  /* Parenthesis */
	case '(':
	  push (r);
	  break;
	case ')':
	  if (r->state->quote_mode)
	    read_error (r, "unbalanced parenthesis");
	  else if (r->state->vector_mode)
	    read_error (r, "unbalanced brackets");
	  else
	    addpop (r);
	  break;

	  /* Vectors */
	case '[':
	  push (r);
	  r->state->vector_mode = 1;
	  break;
	case ']':
	  if (r->state->quote_mode)
	    read_error (r, "unbalanced parenthesis");
	  else if (!r->state->vector_mode)
	    read_error (r, "unbalanced brackets");
	  else
	    addpop (r);
	  break;

	  /* Quoting */
	case '\'':
	  push (r);
	  add (r, quote);
	  if (!r->error)
	    r->state->quote_mode = 1;
	  break;

	  /* strings */
	case '"':
	  buf_read (r, "\"");
	  add (r, parse_str (r));
	  reader_getc (r);	/* Throw away other quote. */
	  break;

	  /* numbers and symbols */
	default:
	  buf_append (r, c);
	  buf_read (r, " \t\r\n()[];");
	  object_t *o = parse_atom (r);
	  if (!r->error)
	    add (r, o);
	  break;
	}
    }
  if (!r->eof && !r->error)
    consume_whitespace (r);
  if (r->error)
    return err_symbol;

  /* Check state */
  r->done = 1;
  if (stack_height (r) > 1 || r->state->quote_mode
      || r->state->dotpair_mode == 1)
    {
      read_error (r, "premature end of file");
      return err_symbol;
    }
  if (list_empty (r))
    {
      obj_destroy (pop (r));
      return NIL;
    }

  object_t *wrap = pop (r);
  object_t *sexp = UPREF (CAR (wrap));
  obj_destroy (wrap);
  return sexp;
}

/* Use the core functions above to eval each sexp in a file. */
int load_file (FILE * fid, char *filename, int interactive)
{
  if (fid == NULL)
    {
      fid = fopen (filename, "r");
      if (fid == NULL)
	return 0;
    }
  reader_t *r = reader_create (fid, NULL, filename, interactive);
  while (!r->eof)
    {
      object_t *sexp = read_sexp (r);
      if (sexp != err_symbol)
	{
	  object_t *ret = top_eval (sexp);
	  if (r->interactive && ret != err_symbol)
	    obj_print (ret, 1);
	  obj_destroy (sexp);
	  obj_destroy (ret);
	}
    }
  reader_destroy (r);
  return 1;
}

/* Convenience function for creating a REPL. */
void repl ()
{
  interactive_mode = 1;
  load_file (stdin, "<stdin>", 1);
}

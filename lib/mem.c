#include <stdint.h>
#include "common.h"
#include "mem.h"

void mm_fill_stack (mmanager_t * mm)
{
  uint8_t *p = xmalloc ((mm->size - (mm->stack - mm->base)) * mm->osize);
  for (; mm->stack < mm->base + mm->size; mm->stack++, p += mm->osize)
    {
      mm->clearf (p);
      *(mm->stack) = (void *) p;
    }
  mm->stack--;
}

void mm_resize_stack (mmanager_t * mm)
{
  mm->size *= 2;
  size_t count = mm->stack - mm->base;
  mm->base = xrealloc (mm->base, mm->size * sizeof (void *));
  mm->stack = mm->base + count;
}

mmanager_t *mm_create (size_t osize, void (*clear_func) (void *o))
{
  mmanager_t *mm = xmalloc (sizeof (mmanager_t));
  mm->osize = osize;
  mm->clearf = clear_func;
  mm->size = 1024;
  mm->stack = mm->base = xmalloc (sizeof (void *) * mm->size);
  mm_fill_stack (mm);
  return mm;
}

void mm_destroy (mmanager_t * mm)
{
  xfree (mm->stack);
  xfree (mm);
}

void *mm_alloc (mmanager_t * mm)
{
  if (mm->stack == mm->base)
    mm_fill_stack (mm);
  void *p = *(mm->stack);
  mm->stack--;
  return p;
}

void mm_free (mmanager_t * mm, void *o)
{
  mm->stack++;
  if (mm->stack == mm->base + mm->size)
    mm_resize_stack (mm);
  *(mm->stack) = o;
  mm->clearf (o);
}

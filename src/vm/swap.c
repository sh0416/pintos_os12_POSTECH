#include "vm/swap.h"
#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include <bitmap.h>

#define SWAP_SECTOR_SIZE ((PGSIZE)/(BLOCK_SECTOR_SIZE))

static struct list swap_table;
static struct bitmap *swap_bitmap;
static struct block *swap_block;

static struct lock swap_lock;

void
init_swap_table ()
{
  list_init (&swap_table);
  swap_block = block_get_role (BLOCK_SWAP);
  swap_bitmap = bitmap_create (block_size (swap_block) / SWAP_SECTOR_SIZE);
  lock_init (&swap_lock);
}

void
free_swap_slot_by_address (uint32_t *pd, void *upage, void *frame)
{
  struct list_elem *e;
  struct swap_entry *s = NULL;
  block_sector_t j;
  lock_acquire (&swap_lock);
  // find swap slot
  for (e = list_begin (&swap_table); e != list_end (&swap_table);
       e = list_next (e))
    {
      s = list_entry (e, struct swap_entry, elem);
      if (s->pd == pd && s->upage == upage)
        break;
    }
  // case: no swap slot
  if (e == list_end (&swap_table))
    {
      lock_release (&swap_lock);
      return;
    }
  // read from swap sector to page
  for (j=0; j<SWAP_SECTOR_SIZE; j++)
    {
      block_read (swap_block, 8*(s->sector)+j, frame);
      frame += BLOCK_SECTOR_SIZE;
    }
  // unmark bitmap
  bitmap_set (swap_bitmap, s->sector, false);
  // insert swap_entry into swap_table
  list_remove (&(s->elem));
  free (s);
  lock_release (&swap_lock);
}

void
free_swap_slot_by_pd (uint32_t *pd)
{
  struct list_elem *e;
  struct swap_entry *s;
  lock_acquire (&swap_lock);
  // find swap slot
  for (e = list_begin (&swap_table); e != list_end (&swap_table);
       e = list_next (e))
    {
      s = list_entry (e, struct swap_entry, elem);
      if (s->pd == pd)
        {
          // unmark bitmap
          bitmap_set (swap_bitmap, s->sector, false);
          // remove swap_entry into swap_table
          list_remove (&(s->elem));
          free (s);
        }
    }
  lock_release (&swap_lock);
}

bool
allocate_swap_slot (uint32_t *pd, void *upage, void *frame)
{
  struct swap_entry *s;
  size_t i, swap_bitmap_size;
  block_sector_t j;
  lock_acquire (&swap_lock);
  // find swap sector
  swap_bitmap_size = bitmap_size (swap_bitmap);
  for (i=0; i<swap_bitmap_size; i++)
    if (bitmap_test (swap_bitmap, i))
      break;
  // case: no swap sector
  if (i==swap_bitmap_size)
    {
      lock_release (&swap_lock);
      return false;
    }
  // write from page to swap sector
  for (j=0; j<SWAP_SECTOR_SIZE; j++)
    {
      block_write (swap_block, 8*i+j, frame);
      frame += BLOCK_SECTOR_SIZE;
    }
  // mark bitmap
  bitmap_set (swap_bitmap, i, true);
  // insert swap_entry into swap_table
  s = (struct swap_entry*) malloc (sizeof *s);
  s->sector = i;
  s->pd = pd;
  s->upage = upage;
  lock_init (&s->lock);
  list_push_back (&swap_table, &(s->elem));
  lock_release (&swap_lock);
  return true;
}

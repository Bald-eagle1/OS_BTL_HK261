/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Memory physical module mm/mm-memphy.c
 */

#include "mm.h"
#include "mm64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 *  MEMPHY_mv_csr - move MEMPHY cursor
 *  @mp: memphy struct
 *  @offset: offset
 */
static unsigned char frame_list_lock;

static void lock_frame_list(void) {
   while (__atomic_test_and_set(&frame_list_lock, __ATOMIC_ACQUIRE)) {}
}
static void unlock_frame_list(void) {
   __atomic_clear(&frame_list_lock, __ATOMIC_RELEASE);
}
void lock_ram_storage(void);
void unlock_ram_storage(void);

int MEMPHY_mv_csr(struct memphy_struct *mp, addr_t offset)
{
   int numstep = 0;

   mp->cursor = 0;
   while (numstep < offset && numstep < mp->maxsz)
   {
      /* Traverse sequentially */
      mp->cursor = (mp->cursor + 1) % mp->maxsz;
      numstep++;
   }

   return 0;
}

/*
 *  MEMPHY_seq_read - read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_seq_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
   if (mp == NULL)
      return -1;

   if (!mp->rdmflg)
      return -1; /* Not compatible mode for sequential read */

   lock_ram_storage();
   MEMPHY_mv_csr(mp, addr);
   *value = (BYTE)mp->storage[addr];
   unlock_ram_storage();

   return 0;
}

/*
 *  MEMPHY_read read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
   if (mp == NULL)
      return -1;

   if (mp->rdmflg)
   {
      lock_ram_storage();
      *value = mp->storage[addr];
      unlock_ram_storage();
   }
   else /* Sequential access device */
      return MEMPHY_seq_read(mp, addr, value);

   return 0;
}

/*
 *  MEMPHY_seq_write - write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_seq_write(struct memphy_struct *mp, addr_t addr, BYTE value)
{

   if (mp == NULL)
      return -1;

   if (!mp->rdmflg)
      return -1; /* Not compatible mode for sequential read */

   lock_ram_storage();
   MEMPHY_mv_csr(mp, addr);
   mp->storage[addr] = value;
   unlock_ram_storage();

   return 0;
}

/*
 *  MEMPHY_write-write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data)
{
   if (mp == NULL)
      return -1;

   if (mp->rdmflg)
   {
      lock_ram_storage();
      mp->storage[addr] = data;
      unlock_ram_storage();
   }
   else /* Sequential access device */
      return MEMPHY_seq_write(mp, addr, data);

   return 0;
}

/*
 *  MEMPHY_format-format MEMPHY device
 *  @mp: memphy struct
 */
int MEMPHY_format(struct memphy_struct *mp, int pagesz)
{
   /* This setting come with fixed constant PAGESZ */
   int numfp = mp->maxsz / pagesz;
   struct framephy_struct *newfst, *fst;
   int iter = 0;

   if (numfp <= 0)
      return -1;

   /* Init head of free framephy list */
   fst = malloc(sizeof(struct framephy_struct));
   fst->fp_next = NULL;
   fst->fpn = iter;
   mp->free_fp_list = fst;

   /* We have list with first element, fill in the rest num-1 element member*/
   for (iter = 1; iter < numfp; iter++)
   {
      newfst = malloc(sizeof(struct framephy_struct));
      newfst->fpn = iter;
      newfst->fp_next = NULL;
      fst->fp_next = newfst;
      fst = newfst;
   }

   return 0;
}

int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *retfpn)
{
   lock_frame_list();
   struct framephy_struct *fp = mp->free_fp_list;

   if (fp == NULL)
   {
      unlock_frame_list();
      return -1;
   }

   *retfpn = fp->fpn;
   mp->free_fp_list = fp->fp_next;
   unlock_frame_list();

   /* 
    * TODO manage free frame list
    */
   //*retfpn ..
   // cap nhat lai danh sach free frame

   /* 
    * TODO MEMPHY is iteratively used up until its exhausted
    * No garbage collector acting then it not been released
    */
   free(fp);

   return 0;
}

int MEMPHY_dump(struct memphy_struct *mp)
{
  /*
   * TODO dump memphy contnt mp->storage
   *     for tracing the memory content
   */

   return 0;
}

int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn)
{
   lock_frame_list();
   struct framephy_struct *fp = mp->free_fp_list;
   struct framephy_struct *newnode = malloc(sizeof(struct framephy_struct));

   if (newnode == NULL)
   {
      unlock_frame_list();
      return -1;
   }

   /* Create new node with value fpn */
   newnode->fpn = fpn;
   newnode->fp_next = fp;
   mp->free_fp_list = newnode;
   unlock_frame_list();

   return 0;
}

/*
 *  Init MEMPHY struct
 */
int init_memphy(struct memphy_struct *mp, addr_t max_size, int randomflg)
{
   mp->free_fp_list = NULL;
   mp->used_fp_list = NULL;
   mp->storage = (BYTE *)malloc(max_size * sizeof(BYTE));
   mp->maxsz = max_size;
   memset(mp->storage, 0, max_size * sizeof(BYTE));

#ifdef MM64
   MEMPHY_format(mp, PAGING64_PAGESZ);
#else
   MEMPHY_format(mp, PAGING_PAGESZ);
#endif

   mp->rdmflg = (randomflg != 0) ? 1 : 0;

   if (!mp->rdmflg) /* Not Ramdom acess device, then it serial device*/
      mp->cursor = 0;

   return 0;
}

// #endif

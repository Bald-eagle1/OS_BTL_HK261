/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(MM64)

/*
 * init_pte - Initialize PTE entry
 */
int init_pte(addr_t *pte,
             int pre,    // present
             addr_t fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             addr_t swpoff) // swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      if (fpn == 0)
        return -1;  // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}


/*
 * get_pd_from_pagenum - Parse address to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Extract page direactories */
	*pgd = (addr&PAGING64_ADDR_PGD_MASK)>>PAGING64_ADDR_PGD_LOBIT;
	*p4d = (addr&PAGING64_ADDR_P4D_MASK)>>PAGING64_ADDR_P4D_LOBIT;
	*pud = (addr&PAGING64_ADDR_PUD_MASK)>>PAGING64_ADDR_PUD_LOBIT;
	*pmd = (addr&PAGING64_ADDR_PMD_MASK)>>PAGING64_ADDR_PMD_LOBIT;
	*pt = (addr&PAGING64_ADDR_PT_MASK)>>PAGING64_ADDR_PT_LOBIT;

	/* TODO: implement the page direactories mapping */

	return 0;
}

/*
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Shift the address to get page num and perform the mapping*/
	return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                         pgd,p4d,pud,pmd,pt);
}

//helper
static addr_t* pte_walk(struct mm_struct *mm, addr_t pgn, int create)
{
  if (mm == NULL || mm->pgd == NULL)
  {
    return NULL;
  }

  addr_t idx[5] = {0};
  get_pd_from_pagenum(pgn, &idx[0], &idx[1], &idx[2], &idx[3], &idx[4]);

  addr_t *t = mm->pgd;
  for (int i = 0; i < 4; i++)
  {
    if (t[idx[i]] == 0)
    {
      if (!create) return NULL;

      addr_t *new_tbl = calloc(PAGING64_PTRS_PER_TABLE, sizeof(addr_t));

      if (new_tbl == NULL) return NULL;

      t[idx[i]] = (addr_t)(uintptr_t)new_tbl;
    }

    t = (addr_t*)(uintptr_t)t[idx[i]];
  }
  return &t[idx[4]];
}
/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  addr_t *pte;

  if (caller == NULL) return -1;

#ifdef MM64
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  pte = pte_walk(caller->mm, pgn, 1);   
  if (pte == NULL) return -1;
#else
  pte = &caller->krnl->mm->pgd[pgn];
#endif

  *pte = 0;                           
  CLRBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 * @caller: caller
 */
int pte_set_fpn(struct mm_struct *mm, addr_t pgn, addr_t fpn, struct pcb_t *caller)
{
  addr_t *pte;

#ifdef MM64	
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  pte = pte_walk(mm, pgn, 1);
  if (pte == NULL) return -1;
#else
  pte = &caller->krnl->mm->pgd[pgn];
#endif
  *pte = 0;   

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}


/* Get PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @caller: caller
 * @ret    : page table entry
 **/
addr_t pte_get_entry(struct mm_struct *mm, addr_t pgn, struct pcb_t *caller)
{
  addr_t *pte;
#ifdef MM64	
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  pte = pte_walk(mm, pgn, 0);
  if (pte == NULL) return 0;
#else	
  pte = &caller->krnl->mm->pgd[pgn];
#endif

  return *pte;
}

/* Set PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
int pte_set_entry(struct mm_struct *mm, addr_t pgn, uint64_t pte_val, struct pcb_t *caller)
{
  addr_t *pte;
#ifdef MM64	
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  pte = pte_walk(mm, pgn, 1);
  if (pte == NULL) return -1;
#else	
  pte = &caller->krnl->mm->pgd[pgn];
#endif
  *pte = pte_val;
	return 0;
}

/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum)                      // num of mapping page
{
  int pgit = 0;
  uint64_t pattern = 0xdeadbeef;

  /* TODO memset the page table with given pattern
   */
  if (caller == NULL)
    return -1;

  pattern = pattern & ~(PAGING_PTE_PRESENT_MASK | PAGING_PTE_SWAPPED_MASK);

  for (pgit = 0; pgit < pgnum; pgit++) {
    pte_set_entry(caller->mm, (addr >> 12) + pgit, pattern, caller);
  }

  return 0;
}

/*
 * vmap_page_range - map a range of page at aligned address
 */
addr_t vmap_page_range(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz // start
                    int pgnum,                      // num of mapping page
                    struct framephy_struct *frames, // list of the mapped frames
                    struct vm_rg_struct *ret_rg)    // return mapped region, the real mapped fp
{                                                   // no guarantee all given pages are mapped
//struct framephy_struct *fpit;
//int pgit = 0;
//addr_t pgn;

  /* TODO: update the rg_end and rg_start of ret_rg 
  //ret_rg->rg_end =  ....
  //ret_rg->rg_start = ...
  //ret_rg->vmaid = ...
  */

  /* TODO map range of frame to address space
   *      [addr to addr + pgnum*PAGING_PAGESZ
   *      in page table caller->krnl->mm->pgd,
   *                    caller->krnl->mm->pud...
   *                    ...
   */

  /* Tracking for later page replacement activities (if needed)
   * Enqueue new usage page */
  //enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn64 + pgit);

  return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  addr_t fpn;
  int pgit;
  struct framephy_struct *newfp_str = NULL;
  struct framephy_struct **cur = frm_lst;
  addr_t ret_alloc = 0;

  /* TODO: allocate the page
  //caller-> ...
  //frm_lst-> ...
  */
  if (frm_lst == NULL)
    return (addr_t)-1;
  *frm_lst = NULL;
  if (caller == NULL || caller->krnl == NULL ||
      caller->krnl->mram == NULL || req_pgnum <= 0)
    return (addr_t)-1;

  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    newfp_str = malloc(sizeof(struct framephy_struct));
    if (newfp_str == NULL)
    {
      ret_alloc = (addr_t)-1;
      break;
    }

    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) == -1)
    {
      /* TODO: ERROR CODE of obtaining somes but not enough frames */
      free(newfp_str);
      ret_alloc = (addr_t)-3000;
      break;
    }

    newfp_str->fpn = fpn;
    newfp_str->owner = caller->mm;
    newfp_str->fp_next = NULL;
    *cur = newfp_str;
    cur = &newfp_str->fp_next;
  }

  if (ret_alloc != 0 && *frm_lst != NULL)
  {
    *cur = caller->krnl->mram->free_fp_list;
    caller->krnl->mram->free_fp_list = *frm_lst;
    *frm_lst = NULL;
  }

  return ret_alloc;
}

addr_t vm_map_kernel(struct pcb_t *caller, addr_t astart, addr_t aend,
                     addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  /* TODO: allocate kernel frames and update krnl_pgd. */
  if (caller == NULL || caller->krnl == NULL || ret_rg == NULL || incpgnum <= 0)
    return (addr_t)-1;

  struct krnl_t *krnl = caller->krnl;
  if (krnl->mm == NULL || krnl->mram == NULL || krnl->krnl_pgd == NULL ||
      krnl->mm->p4d == NULL || krnl->mm->pud == NULL ||
      krnl->mm->pmd == NULL || krnl->mm->pt == NULL ||
      krnl->mram->storage == NULL || krnl->mram->maxsz <= 0)
    return (addr_t)-1;

  if (astart >= aend || mapstart < astart || mapstart > aend ||
      mapstart % PAGING64_PAGESZ != 0 ||
      (addr_t)incpgnum > (aend - mapstart) / PAGING64_PAGESZ ||
      mapstart / PAGING64_PAGESZ >= PAGING64_MAX_PGN)
    return (addr_t)-1;

  int pgn = mapstart / PAGING64_PAGESZ;
  if ((addr_t)incpgnum > PAGING64_MAX_PGN - pgn)
    return (addr_t)-1;

  addr_t *pt = krnl->krnl_pt;
  if (pt == NULL)
    pt = krnl->mm->pt;
  for (int i = 0; i < incpgnum; i++)
    if (pt[pgn + i] != 0)
      return (addr_t)-1;

  struct framephy_struct *fp = krnl->mram->free_fp_list;
  struct framephy_struct *scan;
  int firstfpn = 0;
  int numfp = krnl->mram->maxsz / PAGING64_PAGESZ;
  int i;
  while (fp != NULL)
  {
    if (fp->fpn < (addr_t)numfp && fp->fpn <= PAGING_PTE_FPN_MASK)
    {
      firstfpn = fp->fpn;
      if (incpgnum <= numfp - firstfpn &&
          (addr_t)(incpgnum - 1) <= PAGING_PTE_FPN_MASK - firstfpn)
      {
        for (i = 0; i < incpgnum; i++)
        {
          scan = krnl->mram->free_fp_list;
          while (scan != NULL && scan->fpn != (addr_t)(firstfpn + i))
            scan = scan->fp_next;
          if (scan == NULL)
            break;
        }
        if (i == incpgnum)
          break;
      }
    }
    fp = fp->fp_next;
  }
  if (fp == NULL)
    return (addr_t)-1;

  for (i = incpgnum - 1; i >= 0; i--)
  {
    struct framephy_struct *prev = NULL;
    fp = krnl->mram->free_fp_list;
    while (fp->fpn != (addr_t)(firstfpn + i))
    {
      prev = fp;
      fp = fp->fp_next;
    }
    if (prev == NULL)
      krnl->mram->free_fp_list = fp->fp_next;
    else
      prev->fp_next = fp->fp_next;
    fp->fp_next = krnl->mram->free_fp_list;
    krnl->mram->free_fp_list = fp;
  }

  struct framephy_struct *frm_lst = NULL;
  if (alloc_pages_range(caller, incpgnum, &frm_lst) != 0)
    return (addr_t)-1;

  if (krnl->krnl_pt == NULL)
  {
    memset(krnl->krnl_pgd, 0, PAGING64_MAX_PGN * sizeof(addr_t));
    krnl->krnl_p4d = krnl->mm->p4d;
    krnl->krnl_pud = krnl->mm->pud;
    krnl->krnl_pmd = krnl->mm->pmd;
    krnl->krnl_pt = krnl->mm->pt;
    krnl->krnl_pgd[0] = (addr_t)krnl->krnl_p4d;
    krnl->krnl_p4d[0] = (addr_t)krnl->krnl_pud;
    krnl->krnl_pud[0] = (addr_t)krnl->krnl_pmd;
    krnl->krnl_pmd[0] = (addr_t)krnl->krnl_pt;
  }
  for (i = 0; i < incpgnum; i++)
  {
    fp = frm_lst;
    int fpn = fp->fpn;
    krnl->krnl_pt[pgn + i] = PAGING_PTE_PRESENT_MASK | fpn;
    memset(krnl->mram->storage + fpn * PAGING64_PAGESZ, 0, PAGING64_PAGESZ);
    frm_lst = fp->fp_next;
    free(fp);
  }

  ret_rg->rg_start = mapstart;
  ret_rg->rg_end = mapstart + (addr_t)incpgnum * PAGING64_PAGESZ;
  ret_rg->vmaid = 0;
  ret_rg->rg_next = NULL;
  return 0;
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;

  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide
   *duplicate control mechanism, keep it simple
   */
  ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);

  if (ret_alloc != 0)
    return (addr_t)-1;

  /* it leaves the case of memory is enough but half in ram, half in swap
   * do the swaping all to swapper to get the all in ram */
   vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);

  return 0;
}

/* Swap copy content page from source frame to destination frame
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
#ifdef MM64
  for (cellidx = 0; cellidx < PAGING64_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING64_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING64_PAGESZ + cellidx;
#else
  for (cellidx = 0; cellidx < PAGING64_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING64_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING64_PAGESZ + cellidx;
#endif

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

/*
 *Initialize a empty Memory Management instance
 * @mm:     self mm
 * @caller: mm owner
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  memset(mm, 0, sizeof(struct mm_struct));

  /* TODO init page table directory */
  mm->pgd = calloc(PAGING64_PTRS_PER_TABLE, sizeof(addr_t));
  mm->p4d = calloc(PAGING64_PTRS_PER_TABLE, sizeof(addr_t));
  mm->pud = calloc(PAGING64_PTRS_PER_TABLE, sizeof(addr_t));
  mm->pmd = calloc(PAGING64_PTRS_PER_TABLE, sizeof(addr_t));
  mm->pt  = calloc(PAGING64_PTRS_PER_TABLE, sizeof(addr_t));
  if (mm->pgd == NULL || mm->p4d == NULL || mm->pud == NULL ||
      mm->pmd == NULL || mm->pt == NULL)
    return -1;

  mm->pgd[0] = (addr_t)mm->p4d;
  mm->p4d[0] = (addr_t)mm->pud;
  mm->pud[0] = (addr_t)mm->pmd;
  mm->pmd[0] = (addr_t)mm->pt;

  /* By default the owner comes with at least one vma */
  struct vm_area_struct *vma_malloc_used = malloc(sizeof(struct vm_area_struct));
  struct vm_area_struct *vma_valloc_used = malloc(sizeof(struct vm_area_struct));
  struct vm_area_struct *vma_stack0 = malloc(sizeof(struct vm_area_struct));

  vma_malloc_used->vm_id = 0;
  vma_malloc_used->vm_start = 0x5d000;
  vma_malloc_used->vm_end = vma_malloc_used->vm_start;
  vma_malloc_used->sbrk = vma_malloc_used->vm_start;
  vma_malloc_used->vm_next = vma_valloc_used;
  vma_malloc_used->vm_mm = mm;
  vma_malloc_used->vm_freerg_list = NULL;

  vma_valloc_used->vm_id = 1;
  vma_valloc_used->vm_start = 0xff000;
  vma_valloc_used->vm_end = vma_valloc_used->vm_start;
  vma_valloc_used->sbrk = vma_valloc_used->vm_start;
  vma_valloc_used->vm_next = vma_stack0;
  vma_valloc_used->vm_mm = mm;
  vma_valloc_used->vm_freerg_list = NULL;

  vma_stack0->vm_id = 2;
  vma_stack0->vm_start = 0xbfffe000;
  vma_stack0->vm_end = vma_stack0->vm_start;
  vma_stack0->sbrk = vma_stack0->vm_start;
  vma_stack0->vm_next = NULL;
  vma_stack0->vm_mm = mm;
  vma_stack0->vm_freerg_list = NULL;

  /* TODO: update mmap */
  mm->mmap = vma_malloc_used;
  mm->fifo_pgn = NULL;
  mm->kcpooltbl = calloc(PAGING_MAX_SYMTBL_SZ, sizeof(struct kcache_pool_struct));
  for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++) {
    mm->symrgtbl[i].vmaid = -1;
    mm->symrgtbl[i].rg_start = 0;
    mm->symrgtbl[i].rg_end = 0;
    mm->symrgtbl[i].rg_next = NULL;
  }

  return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[" FORMAT_ADDR "->"  FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (vma != NULL)
  {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (ip != NULL)
  {
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("\n");
  return 0;
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
  // addr_t pgn = start >> PAGING64_ADDR_PT_SHIFT;
  // addr_t pgit = 0;

  struct mm_struct *mm = caller->mm;
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t pt=0;

  get_pd_from_address(start, &pgd, &p4d, &pud, &pmd, &pt);

  /* TODO traverse the page map and dump the page directory entries */

  printf(" PDG=%16p P4g=%16p PUD=%16p PMD=%16p\n", (void*)mm->pgd, (void*)mm->p4d, (void*)mm->pud, (void*)mm->pmd);

  return 0;
}

#endif  //def MM64

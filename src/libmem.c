/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * System Library
 * Memory Module Library libmem.c
 */

// #ifdef MM_PAGING

#include "string.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include "dma.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t kmm_lock = PTHREAD_MUTEX_INITIALIZER;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = rg_elmt;

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /*Allocate at the toproof */
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  int inc_sz=0;

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
 
    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/

  /*Attempt to increate limit to get space */
#ifdef MM64
  inc_sz = (uint32_t)(size/(int)PAGING64_PAGESZ);
  inc_sz = inc_sz + 1;
#else
  inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif
  int old_sbrk;
  inc_sz = inc_sz + 1;

  old_sbrk = cur_vma->sbrk;

  /* TODO INCREASE THE LIMIT
   * SYSCALL 1 sys_memmap
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
#ifdef MM64
  regs.a3 = size;
#else
  regs.a3 = PAGING_PAGE_ALIGNSZ(size);
#endif  
  _syscall(caller->krnl, caller->pid, 17, &regs); /* SYSCALL 17 sys_memmap */
  /*Successful increase limit */
  caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->mm->symrgtbl[rgid].rg_end = old_sbrk + size;

  *alloc_addr = old_sbrk;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;

}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);

  if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t  addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  /* By default using vmaid = 0 */
  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  int val = __free(proc, 0, reg_index);
  if (val == -1)
  {
    return -1;
  }
printf("%s:%d\n",__func__,__LINE__);
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
  return 0;//val;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{

  uint64_t pte = pte_get_entry(mm, pgn, caller);

  if (!PAGING_PAGE_PRESENT(pte))
  { /* Page is not online, make it actively living */
    addr_t vicpgn;
    addr_t vicpte;
    addr_t vicfpn, swpfpn;
    /* The requested page must be stored in swap. */
    if (!(pte & PAGING_PTE_SWAPPED_MASK))
      return -1;

    addr_t tgtfpn = PAGING_SWP(pte);
    int swptyp = PAGING_PTE_SWPTYP(pte);

    /* Get free frame in MEMSWP */
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
    {
      return -1;
    }

    /* Choose an old page to make room for the requested page in RAM. */
    if (find_victim_page(mm, &vicpgn) == -1)
    {
      MEMPHY_put_freefp(caller->krnl->active_mswp, swpfpn);
      return -1;
    }

    vicpte = pte_get_entry(mm, vicpgn, caller);
    vicfpn = PAGING_FPN(vicpte);

    /* Copy the old page from RAM to a free swap frame. */
    __swap_cp_page(caller->krnl->mram, vicfpn,
                   caller->krnl->active_mswp, swpfpn);

    /* Load the requested page from swap into the reused RAM frame. */
    __swap_cp_page(caller->krnl->mswp[swptyp], tgtfpn,
                   caller->krnl->mram, vicfpn);

    /* Update the locations of both pages in the page table. */
    pte_set_swap(caller, vicpgn, caller->krnl->active_mswp_id, swpfpn);
    pte_set_fpn(mm, pgn, vicfpn, caller);

    /* Free the old swap frame and add the loaded page to the FIFO list. */
    MEMPHY_put_freefp(caller->krnl->mswp[swptyp], tgtfpn);
    enlist_pgn_node(&mm->fifo_pgn, pgn);
  }

  *fpn = PAGING_FPN(pte_get_entry(mm, pgn, caller));

  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
//int off = ...
#ifdef MM64
  pgn = addr >> 12;
  int off = addr & (PAGING64_PAGESZ - 1);
#else
  int off = PAGING_OFFST(addr);
#endif
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

//int phyaddr = ...
#ifdef MM64
  int phyaddr = fpn * PAGING64_PAGESZ + off;
#else
  int phyaddr = fpn * PAGING_PAGESZ + off;
#endif

  /* TODO
   *  MEMPHY_read(caller->krnl->mram, phyaddr, data);
   *  MEMPHY READ
   *  SYSCALL 17 sys_memmap with SYSMEM_IO_READ
   */
  struct sc_regs regs = {0};
  regs.a1 = SYSMEM_IO_READ;
  regs.a2 = phyaddr;

  if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0)
    return -1;

  *data = (BYTE)regs.a3;

  return 0;
}
/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  //int off = ...
  #ifdef MM64
  pgn = addr >> 12;
  int off = addr & (PAGING64_PAGESZ - 1);
  #else
  int off = PAGING_OFFST(addr);
  #endif
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  #ifdef MM64
  int phyaddr = fpn * PAGING64_PAGESZ + off;
  #else
  int phyaddr = fpn * PAGING_PAGESZ + off;
  #endif

  /* TODO
   *  MEMPHY_write(caller->krnl->mram, phyaddr, value);
   *  MEMPHY WRITE with SYSMEM_IO_WRITE
   * SYSCALL 17 sys_memmap
   */
  struct sc_regs regs = {0};
  regs.a1 = SYSMEM_IO_WRITE;
  regs.a2 = phyaddr;
  regs.a3 = value;

  if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0)
    return -1;

  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

//struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  /* TODO Invalid memory identify */

  pg_getval(caller->krnl->mm, currg->rg_start + offset, data, caller);

  return 0;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  if (destination == NULL)
    return -1;

  BYTE data;
printf("%s:%d\n",__func__,__LINE__);
  int val = __read(proc, 0, source, offset, &data);
  if (val != 0)
    return val;

  *destination = data;
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pg_setval(caller->krnl->mm, currg->rg_start + offset, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return val;
}


int libkmem_malloc(struct pcb_t * caller, uint32_t size, uint32_t reg_index)
{
  /*
   * TODO: provide OS level management
   *       and forward the request to helper
   */
  addr_t addr;
  addr_t val;

  /*
   * TODO: provide OS kmem allocation validation
   */

  if (caller == NULL || size == 0 || reg_index >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  val = __kmalloc(caller, 0, reg_index, size, &addr);
  if (val == (addr_t)-1)
    return -1;

  return 0;
}


addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /* TODO: provide OS kernel memory allocation
   *       update krnl_pgd for OS kernel level management */

  struct krnl_t *krnl = caller->krnl;  

#ifdef MM64
  addr_t pagesz = PAGING64_PAGESZ;
  addr_t maxpgn = PAGING64_MAX_PGN;
  if (krnl->mm->p4d == NULL || krnl->mm->pud == NULL ||
      krnl->mm->pmd == NULL || krnl->mm->pt == NULL)
    return (addr_t)-1;
#else
  addr_t pagesz = PAGING_PAGESZ;
  addr_t maxpgn = PAGING_MAX_PGN;
#endif

  pthread_mutex_lock(&kmm_lock);
  struct vm_area_struct *cur_vma = get_vma_by_num(krnl->mm, vmaid);
  struct vm_rg_struct *rgnode = &krnl->mm->symrgtbl[rgid];

  if (cur_vma == NULL || rgnode->rg_start != rgnode->rg_end)
  {
    pthread_mutex_unlock(&kmm_lock);
    return (addr_t)-1;
  }

  addr_t alignedsize = ((size + pagesz - 1) / pagesz) * pagesz;
  int incpgnum = alignedsize / pagesz;
  if (cur_vma->sbrk >= maxpgn * pagesz)
  {
    pthread_mutex_unlock(&kmm_lock);
    return (addr_t)-1;
  }
  addr_t mapstart = ((cur_vma->sbrk + pagesz - 1) / pagesz) * pagesz;
  addr_t pgn = mapstart / pagesz;
  if ((addr_t)incpgnum > maxpgn - pgn)
  {
    pthread_mutex_unlock(&kmm_lock);
    return (addr_t)-1;
  }

struct vm_rg_struct mapped_rg;

addr_t val = vm_map_kernel(caller,
                          mapstart,
                          mapstart + alignedsize,
                          mapstart,
                          incpgnum,
                          &mapped_rg);

if (val != 0)
{
  pthread_mutex_unlock(&kmm_lock);
  return (addr_t)-1;
}

  rgnode->vmaid = vmaid;
  rgnode->rg_start = mapstart;
  rgnode->rg_end = mapstart + size;
  rgnode->rg_next = NULL;
  cur_vma->sbrk = mapstart + alignedsize;
  cur_vma->vm_end = cur_vma->sbrk;
  *alloc_addr = mapstart;

  pthread_mutex_unlock(&kmm_lock);
  return 0;

}



/*libmem_dma_memcpy - DMA memcpy
 *@caller: caller
 *@src: source region
 *@dst: destination region
 *@offset: memory offset
 *@size: copied data size
 */
int libmem_dma_memcpy(struct pcb_t *caller, uint32_t src, uint32_t dst, uint32_t offset, uint32_t size)
{
  struct vm_rg_struct *currg_src = NULL;
  struct vm_rg_struct *currg_dst = NULL;

  if (caller->mm != NULL) {
    currg_src = get_symrg_byid(caller->mm, src);
    currg_dst = get_symrg_byid(caller->mm, dst);
  }

  if ((currg_src == NULL || (currg_src->rg_start == 0 && currg_src->rg_end == 0)) &&
      caller->krnl != NULL && caller->krnl->mm != NULL) {
    currg_src = get_symrg_byid(caller->krnl->mm, src);
  }

  if ((currg_dst == NULL || (currg_dst->rg_start == 0 && currg_dst->rg_end == 0)) &&
      caller->krnl != NULL && caller->krnl->mm != NULL) {
    currg_dst = get_symrg_byid(caller->krnl->mm, dst);
  }

  addr_t vaddr_src = (currg_src != NULL ? currg_src->rg_start : 0) + offset;
  addr_t vaddr_dst = (currg_dst != NULL ? currg_dst->rg_start : 0) + offset;

  addr_t bus_src = vaddr_src;
  addr_t bus_dst = vaddr_dst;

#ifdef MM_PAGING
#ifdef MM64
  int fpn_src = -1, fpn_dst = -1;
  addr_t pgn_src = vaddr_src / PAGING64_PAGESZ;
  addr_t pgn_dst = vaddr_dst / PAGING64_PAGESZ;
  if (caller->mm != NULL && pg_getpage(caller->mm, pgn_src, &fpn_src, caller) == 0 && fpn_src >= 0) {
    bus_src = ((uint64_t)fpn_src * PAGING64_PAGESZ) + (vaddr_src % PAGING64_PAGESZ);
  }
  if (caller->mm != NULL && pg_getpage(caller->mm, pgn_dst, &fpn_dst, caller) == 0 && fpn_dst >= 0) {
    bus_dst = ((uint64_t)fpn_dst * PAGING64_PAGESZ) + (vaddr_dst % PAGING64_PAGESZ);
  }
#else
  int fpn_src = -1, fpn_dst = -1;
  addr_t pgn_src = PAGING_PGN(vaddr_src);
  addr_t pgn_dst = PAGING_PGN(vaddr_dst);
  if (caller->mm != NULL && pg_getpage(caller->mm, pgn_src, &fpn_src, caller) == 0 && fpn_src >= 0) {
    bus_src = ((uint64_t)fpn_src * PAGING_PAGESZ) + PAGING_OFFST(vaddr_src);
  }
  if (caller->mm != NULL && pg_getpage(caller->mm, pgn_dst, &fpn_dst, caller) == 0 && fpn_dst >= 0) {
    bus_dst = ((uint64_t)fpn_dst * PAGING_PAGESZ) + PAGING_OFFST(vaddr_dst);
  }
#endif
#endif

  /*
   * Asynchronous DMA: Submit transfer request to co-processor worker queue.
   * If queuing fails, fall back to synchronous do_dma().
   */
  if (dma_async_submit(caller, (uint64_t)bus_src, (uint64_t)bus_dst, (uint64_t)size) != 0) {
    do_dma(caller, (uint64_t)bus_src, (uint64_t)bus_dst, (uint64_t)size);
  }

  return 0;
}


int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */

  struct vm_rg_struct *dest_rg = get_symrg_byid(caller->mm, destination);
  if (dest_rg == NULL || (dest_rg->rg_start == 0 && dest_rg->rg_end == 0)) {
    addr_t addr = 0;
    __kmalloc(caller, 0, destination, offset + size, &addr);
  }

  /*
   * TODO: Map kernel address range
   */
  for (uint32_t i = 0; i < size; i++) {
    BYTE data = 0;

    __read_user_mem(caller, 0, source, offset + i, &data);
    __write_kernel_mem(caller, 0, destination, offset + i, data);
  }

  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */

  struct vm_rg_struct *dest_rg = get_symrg_byid(caller->mm, destination);
  if (dest_rg == NULL || (dest_rg->rg_start == 0 && dest_rg->rg_end == 0)) {
    addr_t addr = 0;
    __alloc(caller, 0, destination, offset + size, &addr);
  }

  /*
   * TODO: Map kernel address range
   */
  for (uint32_t i = 0; i < size; i++) {
    BYTE data = 0;
      __read_kernel_mem(caller, 0, source, offset + i, &data);
      __write_user_mem(caller, 0, destination, offset + i, data);
  }
  return 0;
}


int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* TODO: provide OS memory operator for kernel memory region */

  if (caller == NULL || caller->krnl == NULL || data == NULL ||
      rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  struct krnl_t *krnl = caller->krnl;
  if (krnl->mm == NULL || krnl->mram == NULL || krnl->krnl_pgd == NULL ||
      krnl->mram->storage == NULL || krnl->mram->maxsz <= 0)
    return -1;

  pthread_mutex_lock(&kmm_lock);

  struct vm_rg_struct *currg = get_symrg_byid(krnl->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(krnl->mm, vmaid);
  if (currg == NULL || cur_vma == NULL || currg->vmaid != vmaid ||
      currg->rg_start >= currg->rg_end ||
      currg->rg_start < cur_vma->vm_start || currg->rg_end > cur_vma->sbrk ||
      offset >= currg->rg_end - currg->rg_start)
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  addr_t addr = currg->rg_start + offset;
#ifdef MM64
  addr_t pagesz = PAGING64_PAGESZ;
#else
  addr_t pagesz = PAGING_PAGESZ;
#endif
  addr_t pte;

#ifdef MM64
  if (krnl->krnl_pt == NULL || addr / pagesz >= PAGING64_MAX_PGN)
#else
  if (addr / pagesz >= PAGING_MAX_PGN)
#endif
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }
  int pgn = addr / pagesz;
  int off = addr % pagesz;
#ifdef MM64
  pte = krnl->krnl_pt[pgn];
#else
  pte = krnl->krnl_pgd[pgn];
#endif
  if (!PAGING_PAGE_PRESENT(pte) || (pte & PAGING_PTE_SWAPPED_MASK))
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  int fpn = PAGING_FPN(pte);
  int phyaddr = fpn * pagesz + off;
  if (phyaddr >= krnl->mram->maxsz)
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  int val = MEMPHY_read(krnl->mram, phyaddr, data);
  pthread_mutex_unlock(&kmm_lock);
  return val;
}

int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* TODO: provide OS memory operator for kernel memory region */

  if (caller == NULL || caller->krnl == NULL ||
      rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  struct krnl_t *krnl = caller->krnl;
  if (krnl->mm == NULL || krnl->mram == NULL || krnl->krnl_pgd == NULL ||
      krnl->mram->storage == NULL || krnl->mram->maxsz <= 0)
    return -1;

  pthread_mutex_lock(&kmm_lock);

  struct vm_rg_struct *currg = get_symrg_byid(krnl->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(krnl->mm, vmaid);
  if (currg == NULL || cur_vma == NULL || currg->vmaid != vmaid ||
      currg->rg_start >= currg->rg_end ||
      currg->rg_start < cur_vma->vm_start || currg->rg_end > cur_vma->sbrk ||
      offset >= currg->rg_end - currg->rg_start)
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  addr_t addr = currg->rg_start + offset;
#ifdef MM64
  addr_t pagesz = PAGING64_PAGESZ;
#else
  addr_t pagesz = PAGING_PAGESZ;
#endif
  addr_t pte;

#ifdef MM64
  if (krnl->krnl_pt == NULL || addr / pagesz >= PAGING64_MAX_PGN)
#else
  if (addr / pagesz >= PAGING_MAX_PGN)
#endif
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }
  int pgn = addr / pagesz;
  int off = addr % pagesz;
#ifdef MM64
  pte = krnl->krnl_pt[pgn];
#else
  pte = krnl->krnl_pgd[pgn];
#endif
  if (!PAGING_PAGE_PRESENT(pte) || (pte & PAGING_PTE_SWAPPED_MASK))
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  int fpn = PAGING_FPN(pte);
  int phyaddr = fpn * pagesz + off;
  if (phyaddr >= krnl->mram->maxsz)
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  int val = MEMPHY_write(krnl->mram, phyaddr, value);
  pthread_mutex_unlock(&kmm_lock);
  return val;
}

int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* TODO: provide OS level management user memory access */

  if (caller == NULL || caller->mm == NULL || caller->krnl == NULL ||
      data == NULL || rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  if (caller->mm == caller->krnl->mm)
    return -1;

  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL ||
      currg->rg_start >= currg->rg_end ||
      currg->rg_start < cur_vma->vm_start || currg->rg_end > cur_vma->sbrk ||
      offset >= currg->rg_end - currg->rg_start)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  addr_t addr = currg->rg_start + offset;
  if (addr > INT_MAX)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  int val = pg_getval(caller->mm, (int)addr, data, caller);
  pthread_mutex_unlock(&mmvm_lock);
  return val;
}


int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* TODO: provide OS level management user memory access */

  if (caller == NULL || caller->mm == NULL || caller->krnl == NULL ||
      rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  if (caller->mm == caller->krnl->mm)
    return -1;

  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL ||
      currg->rg_start >= currg->rg_end ||
      currg->rg_start < cur_vma->vm_start || currg->rg_end > cur_vma->sbrk ||
      offset >= currg->rg_end - currg->rg_start)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  addr_t addr = currg->rg_start + offset;
  if (addr > INT_MAX)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  int val = pg_setval(caller->mm, (int)addr, value, caller);
  pthread_mutex_unlock(&mmvm_lock);
  return val;
}


/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pagenum, fpn;
  uint32_t pte;

  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = caller->mm->pgd[pagenum];

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else
    {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */
  if (!pg)
  {
    return -1;
  }
  struct pgn_t *prev = NULL;
  while (pg->pg_next)
  {
    prev = pg;
    pg = pg->pg_next;
  }
  *retpgn = pg->pgn;
  if (prev == NULL)
    mm->fifo_pgn = NULL;
  else
    prev->pg_next = NULL;

  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    { /* Current region has enough space */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      { /*Use up all space, remove current node */
        /*Clone next rg node */
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;

          rgit->rg_next = nextrg->rg_next;

          free(nextrg);
        }
        else
        {                                /*End of free list */
          rgit->rg_start = rgit->rg_end; // dummy, size 0 region
          rgit->rg_next = NULL;
        }
      }
      break;
    }
    else
    {
      rgit = rgit->rg_next; // Traverse next rg
    }
  }

  if (newrg->rg_start == -1) // new region not found
    return -1;

  return 0;
}

// #endif

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
static int cur_heap_vmaid[32] = {0};

void dma_async_wait_caller(struct pcb_t *caller);
void dma_async_wait_all(void);

static int is_canonical_address(addr_t addr)
{
    uint64_t msb7 = (addr >> 57) & 0x7FULL;
    uint64_t bit56 = (addr >> 56) & 1ULL;

    if ((bit56 == 0 && msb7 == 0) || (bit56 == 1 && msb7 == 0x7FULL))
        return 1;
    return 0;
}

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
  dma_async_wait_all();
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  if (cur_vma == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
    caller->mm->symrgtbl[rgid].vmaid = rgnode.vmaid;
 
    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/

  addr_t old_sbrk = cur_vma->sbrk;

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
  if (_syscall(caller->krnl, caller->pid, 17, &regs) < 0) { /* SYSCALL 17 sys_memmap */
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  /*Successful increase limit */
  caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->mm->symrgtbl[rgid].rg_end = old_sbrk + size;
  caller->mm->symrgtbl[rgid].vmaid = vmaid;

  *alloc_addr = old_sbrk;

  if (old_sbrk + size < cur_vma->sbrk) {
    struct vm_rg_struct *free_rg = malloc(sizeof(struct vm_rg_struct));
    if (free_rg != NULL) {
      free_rg->rg_start = old_sbrk + size;
      free_rg->rg_end = cur_vma->sbrk;
      free_rg->vmaid = vmaid;
      free_rg->rg_next = NULL;
      enlist_vm_freerg_list(caller->mm, free_rg);
    }
  }

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
  dma_async_wait_all();

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
  freerg_node->vmaid = rgnode->vmaid;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->vmaid = -1;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*
 * alloc_large_vma_macos - allocate a new vma for rg >= 4096 bytes
 */
int alloc_large_vma_macos(struct pcb_t *caller, addr_t size, int rgid, addr_t *alloc_addr)
{
  pthread_mutex_lock(&mmvm_lock);
  dma_async_wait_all();
  addr_t pagesz = PAGING64_PAGESZ;
  addr_t aligned_size = ((size + pagesz - 1) / pagesz) * pagesz;
  int incpgnum = (int)(aligned_size / pagesz);

  int next_id = 3;
  struct vm_area_struct *p = caller->mm->mmap;
  while (p != NULL) {
    if ((int)p->vm_id >= next_id) {
      next_id = p->vm_id + 1;
    }
    p = p->vm_next;
  }

  addr_t hole_start = 0;
  addr_t cur_candidate = 0xff000;
  struct vm_area_struct *cur = caller->mm->mmap;
  while (cur != NULL) {
    if (cur->vm_end <= cur_candidate) {
      cur = cur->vm_next;
      continue;
    }
    if (cur->vm_start > cur_candidate) {
      if (cur->vm_start - cur_candidate >= aligned_size) {
        hole_start = cur_candidate;
        break;
      }
    }
    if (cur->vm_end > cur_candidate) {
      cur_candidate = ((cur->vm_end + pagesz - 1) / pagesz) * pagesz;
    }
    cur = cur->vm_next;
  }

  if (hole_start == 0 && (0xbfffe000 > cur_candidate) && (0xbfffe000 - cur_candidate >= aligned_size)) {
    hole_start = cur_candidate;
  }
  if (hole_start == 0) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  struct vm_area_struct *large_vma = malloc(sizeof(struct vm_area_struct));
  if (large_vma == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  large_vma->vm_id = next_id;
  large_vma->vm_start = hole_start;
  large_vma->vm_end = hole_start + aligned_size;
  large_vma->sbrk = large_vma->vm_end;
  large_vma->vm_mm = caller->mm;
  large_vma->vm_freerg_list = NULL;
  large_vma->vm_next = NULL;

  insert_vma_sorted(caller->mm, large_vma);

  struct vm_rg_struct mapped_rg;
  if (vm_map_ram(caller, large_vma->vm_start, large_vma->vm_end, large_vma->vm_start, incpgnum, &mapped_rg) < 0) {
    struct vm_area_struct *prev = NULL, *scan = caller->mm->mmap;
    while (scan != NULL && scan != large_vma) {
      prev = scan;
      scan = scan->vm_next;
    }
    if (scan == large_vma) {
      if (prev == NULL) caller->mm->mmap = scan->vm_next;
      else prev->vm_next = scan->vm_next;
    }
    free(large_vma);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  caller->mm->symrgtbl[rgid].rg_start = large_vma->vm_start;
  caller->mm->symrgtbl[rgid].rg_end   = large_vma->vm_start + size;
  caller->mm->symrgtbl[rgid].vmaid    = large_vma->vm_id;
  *alloc_addr = large_vma->vm_start;
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*
 * free_large_vma_macos - free a vma and return frame
 */
int free_large_vma_macos(struct pcb_t *caller, int rgid)
{
  if (caller == NULL || caller->mm == NULL || rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return -1;
  pthread_mutex_lock(&mmvm_lock);
  dma_async_wait_all();
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);
  if (rgnode == NULL || (rgnode->rg_start == 0 && rgnode->rg_end == 0)) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  int target_vmaid = rgnode->vmaid;

  struct vm_area_struct *prev = NULL;
  struct vm_area_struct *cur = caller->mm->mmap;
  while (cur != NULL && (int)cur->vm_id != target_vmaid) {
    prev = cur;
    cur = cur->vm_next;
  }
  if (cur == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if (prev == NULL) {
    caller->mm->mmap = cur->vm_next;
  } else {
    prev->vm_next = cur->vm_next;
  }

  addr_t pagesz = PAGING64_PAGESZ;
  int pgnum = (int)((cur->vm_end - cur->vm_start) / pagesz);
  for (int pgit = 0; pgit < pgnum; pgit++) {
    addr_t pgn = (cur->vm_start + (addr_t)pgit * pagesz) >> PAGING64_ADDR_PT_SHIFT;
    uint64_t pte = pte_get_entry(caller->mm, pgn, caller);
    if (PAGING_PAGE_PRESENT(pte)) {
      addr_t fpn = PAGING_PTE_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
  }

  rgnode->rg_start = 0;
  rgnode->rg_end   = 0;
  rgnode->vmaid    = -1;

  free(cur);
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

int alloc_chunk_vma(struct pcb_t *caller, addr_t size, int rgid, addr_t *alloc_addr)
{
  pthread_mutex_lock(&mmvm_lock);
  dma_async_wait_all();
  addr_t pagesz = PAGING64_PAGESZ;
  addr_t aligned_size = pagesz;
  int incpgnum = 1;

  int next_id = 3;
  struct vm_area_struct *p = caller->mm->mmap;
  while (p != NULL) {
    if ((int)p->vm_id >= next_id) next_id = p->vm_id + 1;
    p = p->vm_next;
  }

  addr_t hole_start = 0;
  addr_t cur_candidate = 0xff000;
  struct vm_area_struct *cur = caller->mm->mmap;
  while (cur != NULL) {
    if (cur->vm_end <= cur_candidate) {
      cur = cur->vm_next;
      continue;
    }
    if (cur->vm_start > cur_candidate) {
      if (cur->vm_start - cur_candidate >= aligned_size) {
        hole_start = cur_candidate;
        break;
      }
    }
    if (cur->vm_end > cur_candidate) {
      cur_candidate = ((cur->vm_end + pagesz - 1) / pagesz) * pagesz;
    }
    cur = cur->vm_next;
  }
  if (hole_start == 0 && (0xbfffe000 > cur_candidate) && (0xbfffe000 - cur_candidate >= aligned_size)) {
    hole_start = cur_candidate;
  }
  if (hole_start == 0) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  struct vm_area_struct *chunk_vma = malloc(sizeof(struct vm_area_struct));
  if (chunk_vma == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  chunk_vma->vm_id = next_id;
  chunk_vma->vm_start = hole_start;
  chunk_vma->vm_end = hole_start + aligned_size;
  chunk_vma->sbrk = chunk_vma->vm_end;
  chunk_vma->vm_mm = caller->mm;
  chunk_vma->vm_freerg_list = NULL;
  chunk_vma->vm_next = NULL;

  insert_vma_sorted(caller->mm, chunk_vma);

  struct vm_rg_struct mapped_rg;
  if (vm_map_ram(caller, chunk_vma->vm_start, chunk_vma->vm_end, chunk_vma->vm_start, incpgnum, &mapped_rg) < 0) {
    struct vm_area_struct *prev = NULL, *scan = caller->mm->mmap;
    while (scan != NULL && scan != chunk_vma) {
      prev = scan;
      scan = scan->vm_next;
    }
    if (scan == chunk_vma) {
      if (prev == NULL) caller->mm->mmap = scan->vm_next;
      else prev->vm_next = scan->vm_next;
    }
    free(chunk_vma);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  caller->mm->symrgtbl[rgid].rg_start = chunk_vma->vm_start;
  caller->mm->symrgtbl[rgid].rg_end   = chunk_vma->vm_start + size;
  caller->mm->symrgtbl[rgid].vmaid    = chunk_vma->vm_id;
  *alloc_addr = chunk_vma->vm_start;

  if (size < aligned_size) {
    struct vm_rg_struct *free_rg = malloc(sizeof(struct vm_rg_struct));
    if (free_rg != NULL) {
      free_rg->rg_start = chunk_vma->vm_start + size;
      free_rg->rg_end   = chunk_vma->vm_end;
      free_rg->vmaid    = chunk_vma->vm_id;
      free_rg->rg_next  = NULL;
      enlist_vm_freerg_list(caller->mm, free_rg);
    }
  }

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
  int val;

  if (size >= PAGING64_PAGESZ) {
    val = alloc_large_vma_macos(proc, size, reg_index, &addr);
  }
  else {
    int active_vmaid = (proc->pid < 32) ? cur_heap_vmaid[proc->pid] : 0;
    val = __alloc(proc, active_vmaid, reg_index, size, &addr);

    if (val < 0) {
      val = alloc_chunk_vma(proc, size, reg_index, &addr);
      if (val == 0 && proc->pid < 32) {
        cur_heap_vmaid[proc->pid] = proc->mm->symrgtbl[reg_index].vmaid;
      }
    }
  }
  if (val == -1)
  {
    return -1;
  }
  if (reg_index < 10)
  {
    proc->regs[reg_index] = addr;
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
  if (proc == NULL || proc->mm == NULL || reg_index >= PAGING_MAX_SYMTBL_SZ)
    return -1;
  struct vm_rg_struct *rgnode = get_symrg_byid(proc->mm, reg_index);
  if (rgnode == NULL || (rgnode->rg_start == 0 && rgnode->rg_end == 0)) {
    return -1;
  }

  if (rgnode->rg_start >= 0xbfffe000) {
    pthread_mutex_lock(&kmm_lock);
    if (proc->krnl != NULL && proc->krnl->mm != NULL) {
      proc->krnl->mm->symrgtbl[reg_index].rg_start = 0;
      proc->krnl->mm->symrgtbl[reg_index].rg_end = 0;
      proc->krnl->mm->symrgtbl[reg_index].vmaid = -1;
    }
    rgnode->rg_start = 0;
    rgnode->rg_end = 0;
    rgnode->vmaid = -1;
    pthread_mutex_unlock(&kmm_lock);
    return 0; 
  }

  int val;
  addr_t size = rgnode->rg_end - rgnode->rg_start;
  if (size >= PAGING64_PAGESZ) {
    val = free_large_vma_macos(proc, reg_index);
  }
  else {
    val = __free(proc, rgnode->vmaid, reg_index);
  }
  if (val == -1)
  {
    return -1;
  }

// printf("%s:%d\n",__func__,__LINE__);
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
  pthread_mutex_lock(&mmvm_lock);
  dma_async_wait_all();
  if (caller == NULL || caller->mm == NULL || caller->krnl == NULL ||
      data == NULL || rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  /* TODO Invalid memory identify */
  if (currg == NULL || currg->rg_end <= currg->rg_start ||
      offset >= currg->rg_end - currg->rg_start) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  addr_t access_addr = currg->rg_start + offset;

  if (!is_canonical_address(access_addr)) {
    printf("[INVALID ADDR] Process %d tried to access non-canonical address at %llx!\n", 
           caller->pid, (unsigned long long)access_addr);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (((access_addr >> 56) & 1ULL) == 1 || access_addr >= 0xbfffe000ULL) {
      printf("[SECURITY FAULT] Process %d denied accessing kernel space at %llx!\n", 
            caller->pid, (unsigned long long)access_addr);
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
  }

  pg_getval(caller->mm, currg->rg_start + offset, data, caller);

  pthread_mutex_unlock(&mmvm_lock);
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
  struct vm_rg_struct *currg = get_symrg_byid(proc->mm, source);
  int vmaid = (currg != NULL && currg->vmaid >= 0) ? currg->vmaid : 0;
  int val = __read(proc, vmaid, source, offset, &data);
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
  dma_async_wait_all();
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  addr_t access_addr = currg->rg_start + offset;

  if (!is_canonical_address(access_addr)) {
    printf("[INVALID ADDR] Process %d tried to access non-canonical address at %llx!\n", 
           caller->pid, (unsigned long long)access_addr);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (((access_addr >> 56) & 1ULL) == 1 || access_addr >= 0xbfffe000ULL) {
      printf("[SECURITY FAULT] Process %d denied accessing kernel space at %llx!\n", 
            caller->pid, (unsigned long long)access_addr);
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
  }

  pg_setval(caller->mm, currg->rg_start + offset, value, caller);

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
  struct vm_rg_struct *currg = get_symrg_byid(proc->mm, destination);
  int vmaid = (currg != NULL && currg->vmaid >= 0) ? currg->vmaid : 0;
  int val = __write(proc, vmaid, destination, offset, data);
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
  if (caller == NULL || reg_index >= 10)
    return -1;
  pthread_mutex_lock(&kmm_lock);
  addr_t addr = 0;
  int val = __kmalloc(caller, 0, reg_index, size, &addr);
  if (val == -1) {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }
  caller->regs[reg_index] = addr;
  printf("\tProcess %2d: kmalloc %u bytes successfully at address %lu (stored in reg %u)\n", 
        caller->pid, size, (unsigned long)addr, reg_index);
  pthread_mutex_unlock(&kmm_lock);
  return 0;
}



addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return -1;
  struct krnl_t *krnl = caller->krnl;
  struct vm_area_struct *vma = get_vma_by_num(krnl->mm, vmaid);
  if (vma == NULL)
    return -1;
  addr_t old_sbrk = vma->sbrk;
  vma->sbrk += size;
  vma->vm_end = vma->sbrk;

  krnl->mm->symrgtbl[rgid].vmaid = vmaid;
  krnl->mm->symrgtbl[rgid].rg_start = old_sbrk;
  krnl->mm->symrgtbl[rgid].rg_end = old_sbrk + size;

  if (caller->mm != NULL) {
    caller->mm->symrgtbl[rgid].vmaid = vmaid;
    caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
    caller->mm->symrgtbl[rgid].rg_end = old_sbrk + size;
  }
  *alloc_addr = old_sbrk;
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
  int is_krnl_src = 0, is_krnl_dst = 0;

  if (caller->mm != NULL) {
    currg_src = get_symrg_byid(caller->mm, src);
    currg_dst = get_symrg_byid(caller->mm, dst);
  }

  if ((currg_src == NULL || (currg_src->rg_start == 0 && currg_src->rg_end == 0)) &&
      caller->krnl != NULL && caller->krnl->mm != NULL) {
    currg_src = get_symrg_byid(caller->krnl->mm, src);
    is_krnl_src = 1;
  }

  if ((currg_dst == NULL || (currg_dst->rg_start == 0 && currg_dst->rg_end == 0)) &&
      caller->krnl != NULL && caller->krnl->mm != NULL) {
    currg_dst = get_symrg_byid(caller->krnl->mm, dst);
    is_krnl_dst = 1;
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
  if (is_krnl_src && caller->krnl != NULL && caller->krnl->krnl_pt != NULL &&
      pgn_src < PAGING64_MAX_PGN && PAGING_PAGE_PRESENT(caller->krnl->krnl_pt[pgn_src])) {
    fpn_src = PAGING_FPN(caller->krnl->krnl_pt[pgn_src]);
    bus_src = ((uint64_t)fpn_src * PAGING64_PAGESZ) + (vaddr_src % PAGING64_PAGESZ);
  } else if (caller->mm != NULL && pg_getpage(caller->mm, pgn_src, &fpn_src, caller) == 0 && fpn_src >= 0) {
    bus_src = ((uint64_t)fpn_src * PAGING64_PAGESZ) + (vaddr_src % PAGING64_PAGESZ);
  }

  if (is_krnl_dst && caller->krnl != NULL && caller->krnl->krnl_pt != NULL &&
      pgn_dst < PAGING64_MAX_PGN && PAGING_PAGE_PRESENT(caller->krnl->krnl_pt[pgn_dst])) {
    fpn_dst = PAGING_FPN(caller->krnl->krnl_pt[pgn_dst]);
    bus_dst = ((uint64_t)fpn_dst * PAGING64_PAGESZ) + (vaddr_dst % PAGING64_PAGESZ);
  } else if (caller->mm != NULL && pg_getpage(caller->mm, pgn_dst, &fpn_dst, caller) == 0 && fpn_dst >= 0) {
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
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;
  uint32_t u_src = source;
  uint32_t k_dest = destination;

  struct vm_rg_struct *k_rg_src = get_symrg_byid(caller->krnl->mm, source);
  struct vm_rg_struct *k_rg_dest = get_symrg_byid(caller->krnl->mm, destination);
  if (k_rg_src != NULL && k_rg_src->rg_start != 0 && (k_rg_dest == NULL || k_rg_dest->rg_start == 0)) {
    u_src = destination;
    k_dest = source;
  }

  struct vm_rg_struct *dest_rg = get_symrg_byid(caller->krnl->mm, k_dest);
  if (dest_rg == NULL || (dest_rg->rg_start == 0 && dest_rg->rg_end == 0)) {
    addr_t addr = 0;
    __kmalloc(caller, 0, k_dest, offset + size, &addr);
  }

  for (uint32_t i = 0; i < size; i++) {
    BYTE data = 0;
    __read_user_mem(caller, 0, u_src, offset + i, &data);
    __write_kernel_mem(caller, 0, k_dest, offset + i, data);
  }
  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;
  uint32_t k_src = source;
  uint32_t u_dest = destination;

  struct vm_rg_struct *k_rg_src = get_symrg_byid(caller->krnl->mm, source);
  struct vm_rg_struct *k_rg_dest = get_symrg_byid(caller->krnl->mm, destination);

  if (k_rg_dest != NULL && k_rg_dest->rg_start != 0 && (k_rg_src == NULL || k_rg_src->rg_start == 0)) {
    k_src = destination;
    u_dest = source;
  }

  struct vm_rg_struct *dest_rg = get_symrg_byid(caller->mm, u_dest);
  if (dest_rg == NULL || (dest_rg->rg_start == 0 && dest_rg->rg_end == 0)) {
    addr_t addr = 0;
    __alloc(caller, 0, u_dest, offset + size, &addr);
  }

  for (uint32_t i = 0; i < size; i++) {
    BYTE data = 0;
    if (__read_kernel_mem(caller, 0, k_src, offset + i, &data) != 0)
      break;
    __write_user_mem(caller, 0, u_dest, offset + i, data);
  }
  return 0;
}


int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL || data == NULL)
    return -1;
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);
  struct vm_area_struct *vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (currg == NULL || vma == NULL)
    return -1;

  if (currg->rg_start == 0 && currg->rg_end == 0)
    return -1;

  addr_t k_offset = (currg->rg_start - vma->vm_start) + offset;
  return MEMPHY_read(caller->krnl->mram, k_offset, data);
}

int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);
  struct vm_area_struct *vma = get_vma_by_num(caller->krnl->mm, vmaid);
  
  if (currg == NULL || vma == NULL)
    return -1;

  if (currg->rg_start == 0 && currg->rg_end == 0)
    return -1;

  addr_t k_offset = (currg->rg_start - vma->vm_start) + offset;
  return MEMPHY_write(caller->krnl->mram, k_offset, value);
}

/*__read_user_mem - read value in user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* TODO: provide OS level management user memory access */
  //krnl->pgd ... or krnl->krnl_pgd  ... based on kmem implementation strategy

  return __read(caller, vmaid, rgid, offset, data);
}


/*__write_user_mem - write a user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* TODO: provide OS level management user memory access */
  //krnl->pgd ... or krnl->krnl_pgd  ... based on kmem implementation strategy

  return __write(caller, vmaid, rgid, offset, value);
}


/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
// int free_pcb_memph(struct pcb_t *caller)
// {
//   pthread_mutex_lock(&mmvm_lock);
//   int pagenum, fpn;
//   uint32_t pte;

//   for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
//   {
//     pte = caller->mm->pgd[pagenum];

//     if (PAGING_PAGE_PRESENT(pte))
//     {
//       fpn = PAGING_FPN(pte);
//       MEMPHY_put_freefp(caller->krnl->mram, fpn);
//     }
//     else
//     {
//       fpn = PAGING_SWP(pte);
//       MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
//     }
//   }

//   pthread_mutex_unlock(&mmvm_lock);
//   return 0;
// }


#ifdef MM64
static void free_level(addr_t *table, int level, struct krnl_t *krnl)
{
  for (int i = 0; i < PAGING64_PTRS_PER_TABLE; i++) {
    addr_t e = table[i];
    if (e == 0)
      continue;

    if (level < 4) {
      addr_t *child = (addr_t *)(uintptr_t)e;
      free_level(child, level + 1, krnl);
      free(child);
    } else {
      if (PAGING_PAGE_PRESENT(e)) {
        MEMPHY_put_freefp(krnl->mram, PAGING_FPN(e));
      } else if (e & PAGING_PTE_SWAPPED_MASK) {
        MEMPHY_put_freefp(krnl->mswp[PAGING_PTE_SWPTYP(e)], PAGING_SWP(e));
      }
    }
  }
}
#endif

/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
#ifdef MM64
  if (caller == NULL || caller->mm == NULL || caller->mm->pgd == NULL)
    return -1;

  pthread_mutex_lock(&mmvm_lock);
  free_level(caller->mm->pgd, 0, caller->krnl);
  free(caller->mm->pgd);
  caller->mm->pgd = NULL;
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
#else
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
#endif
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
  (void)vmaid;
  if (caller == NULL || caller->mm == NULL || caller->mm->mmap == NULL)
    return -1;

  struct vm_rg_struct *rgit = caller->mm->mmap->vm_freerg_list;

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
      newrg->vmaid = rgit->vmaid;

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
          rgit->vmaid = nextrg->vmaid;

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

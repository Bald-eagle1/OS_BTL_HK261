/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  if (mm == NULL) {
    return NULL;
  }

  struct vm_area_struct *pvma = mm->mmap;
  while (pvma != NULL) {
    if ((int)pvma->vm_id == vmaid) {
      return pvma;
    }
    pvma = pvma->vm_next;
  }

  return NULL;
}

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn , addr_t swpfpn)
{
    __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);
    return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t alignedsz)
{
  struct vm_rg_struct * newrg;
  /* TODO retrive current vma to obtain newrg, current comment out due to compiler redundant warning*/
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  newrg = malloc(sizeof(struct vm_rg_struct));

  // TODO: update the newrg boundary
  newrg->vmaid = vmaid;
  newrg->rg_start = cur_vma->sbrk;
  newrg->rg_end = newrg->rg_start + alignedsz;
  newrg->rg_next = NULL;

  /* END TODO */

  return newrg;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, addr_t vmastart, addr_t vmaend)
{
  struct vm_area_struct *vma = caller->mm->mmap;

  /* TODO validate the planned memory area is not overlapped */
  if (vmastart >= vmaend)
  {
    return -1;
  }

  if (vma == NULL)
  {
    return -1;
  }

  /* TODO validate the planned memory area is not overlapped */

  struct vm_area_struct *cur_area = get_vma_by_num(caller->mm, vmaid);
  if (cur_area == NULL)
  {
    return -1;
  }

  while (vma != NULL)
  {
    if (vma != cur_area && OVERLAP(vmastart, vmaend, vma->vm_start, vma->vm_end))
    {
      return -1;
    }
    vma = vma->vm_next;
  }
  /* End TODO*/

  return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size
 *
 */
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  /* TOTO with new address scheme, the size need tobe aligned 
   *      the raw inc_sz maybe not fit pagesize
   */ 

#ifdef MM64
  addr_t alignedsize = PAGING64_PAGE_ALIGNSZ(inc_sz);
  int incpgnum = (int)(alignedsize / PAGING64_PAGESZ);
#else
  addr_t alignedsize = PAGING_PAGE_ALIGNSZ(inc_sz);
  int incpgnum = (int)(alignedsize / PAGING_PAGESZ);
#endif

  struct vm_rg_struct *area = get_vm_area_node_at_brk(caller, vmaid, alignedsize);
  

  /* 
   * TODO Validate overlap of obtained region 
   *if (validate_overlap_vm_area(caller, vmaid, area->rg_start, area->rg_end) < 0)
   *   return -1;  //Overlap and failed allocation 
   */

  if (validate_overlap_vm_area(caller, vmaid, area->rg_start, area->rg_end) < 0){
    free(area);
    return -1;
  }

  struct vm_rg_struct mapped_rg;
  if (vm_map_ram(caller, area->rg_start, area->rg_end, area->rg_start, incpgnum, &mapped_rg) < 0) {
    free(area);
    return -1;
  }

  /* TODO: Obtain the new vm area based on vmaid */
  cur_vma->sbrk = area->rg_end;
  cur_vma->vm_end = area->rg_end;
  free(area);

  return 0;
}

/*
 * insert_vma_sorted - insert new vma to mmap list sorted by vm_start
 */
int insert_vma_sorted(struct mm_struct *mm, struct vm_area_struct *new_vma) {
  if (mm->mmap == NULL || new_vma->vm_start < mm->mmap->vm_start) {
    new_vma->vm_next = mm->mmap;
    mm->mmap = new_vma;
    return 0;
  }

  struct vm_area_struct *cur = mm->mmap;
  while (cur->vm_next != NULL && cur->vm_next->vm_start < new_vma->vm_start) {
    cur = cur->vm_next;
  }

  new_vma->vm_next = cur->vm_next;
  cur->vm_next = new_vma;
  return 0;
}

// #endif

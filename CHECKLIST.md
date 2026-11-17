# Assignment Implementation Checklist
> **CO2018 Operating Systems Assignment (HK261) — Simple Operating System Simulation**  
> Tracks module progress, open tasks, critical bug fixes, and verification test suites.

---

## Current Status Summary

| Module | Status | Progress | Notes |
| :--- | :---: | :---: | :--- |
| **1. Scheduler & MLQ Policy** | 🟢 Completed | 100% | Priority queues, MLQ slot engine, and `retire_proc` queue synchronization complete. |
| **2. Memory Management (64-bit Paging)** | 🟢 Completed | 100% | 5-level page table walk, page-aligned `alloc_large_vma_macos`, swap-in/out, and 5-level `free_pcb_memph` verified. |
| **3. Co-processor & DMA** | 🟢 Completed | 100% | Standalone testbench and live OS multi-CPU asynchronous DMA verified (`os_2_mlq_paging`). |
| **4. System Call Interface** | 🟢 Completed | 100% | Syscall table generation, `listsyscall`, and PID-to-PCB resolution in `sys_mem.c` verified. |
| **5. Synchronization & Locking** | 🟢 Completed | 100% | DMA queue/hardware locks, `mmvm_lock`, `kmm_lock`, `memphy_lock`, `ram_storage_lock`, and `queue_lock` verified. |
| **6. Report & Verification** | 🟢 Completed | 95% | All standalone, paging, DMA, and syscall test suites passing without errors; final report packaging remaining. |

---

## 1. Scheduler Subsystem (Section 2.1)

### Priority Queues (`include/queue.h`, `src/queue.c`)
- [x] Implement `empty(struct queue_t *q)` check.
- [x] Implement `enqueue(struct queue_t *q, struct pcb_t *proc)` with capacity boundary check (`MAX_QUEUE_SIZE`).
- [x] Implement `dequeue(struct queue_t *q)`:
  - [x] FIFO order for Multi-Level Queue (MLQ) mode.
  - [x] Highest-priority extraction for non-MLQ fallback mode.
- [x] Implement `purgequeue(struct queue_t *q, struct pcb_t *proc)` to remove a specific PCB.
- [x] Implement element shifting in `purgequeue()` to maintain contiguous array order.
- [x] Merged into `main` via PR #5.

### Multi-Level Queue (MLQ) Engine (`src/sched.c`)
- [x] Initialize MLQ ready queues `mlq_ready_queue[MAX_PRIO]` and slot quotas `slot[i] = MAX_PRIO - i`.
- [x] Implement `get_mlq_proc()`:
  - [x] Scan priority levels `0 .. MAX_PRIO-1`.
  - [x] Select process from non-empty queue with `slot[prio] > 0`.
  - [x] Decrement `slot[prio]--` on each dispatch.
  - [x] When all active queues exhaust their slot quota (`slot[prio] == 0`), reset all slots to `MAX_PRIO - i`.
  - [x] Add dispatched process to `running_list`.
  - [x] Ensure `queue_lock` is properly unlocked on all exit paths.
- [x] Implement `put_mlq_proc()`:
  - [x] Re-enqueue pre-empted process back to `mlq_ready_queue[proc->prio]` under `queue_lock`.
  - [x] Remove process from `running_list` via `purgequeue()` under `queue_lock`.
- [x] Implement `add_mlq_proc()`:
  - [x] Add newly loaded process to `mlq_ready_queue[proc->prio]` under `queue_lock`.
- [x] Implement `retire_proc()` to purge finished processes from `running_list` safely under `queue_lock`.
- [x] Implement non-MLQ fallback `get_proc()` in `#else` block of `src/sched.c`.

---

## 2. Memory Management Subsystem (Section 2.2)

### 64-bit 5-Level Paging (`src/mm64.c`, `include/mm64.h`)
- [x] Fix compilation errors in `mm64.c` (resolved undeclared macros and added `<string.h>` for `memset`).
- [x] Implement multi-level page directory allocation and linking in `init_mm()`:
  - [x] Initialize 64-bit page table directories (`pgd` $\rightarrow$ `p4d` $\rightarrow$ `pud` $\rightarrow$ `pmd` $\rightarrow$ `pt`).
  - [x] Initialize process default VMAs: `vma_malloc_used` (`0x5d000`), `vma_valloc_used` (`0xff000`), and `vma_stack0` (`0xbfffe000`).
  - [x] Initialize symbol table (`symrgtbl`) and kernel cache pool (`kcpooltbl`).
- [x] Implement `print_pgtbl()` page table dump format.
- [x] Implement 5-level page table walk in `pte_set_swap()`, `pte_set_fpn()`, `pte_get_entry()`, and `pte_set_entry()`.
- [x] Complete `alloc_pages_range()` and `vmap_page_range()`.
- [x] Complete `vmap_pgd_memset()` with page directory mapping pattern.
- [x] Unify `caller->mm` vs `caller->krnl->mm` usage across `os.c`, `mm64.c`, `mm-vm.c`, and `libmem.c`.

### Physical Memory Management (`src/mm-memphy.c`)
- [x] Fix free frame list tracking in `MEMPHY_get_freefp()`:
  - Retrieve head frame number `*retfpn = fp->fpn`.
  - Advance `mp->free_fp_list = fp->fp_next` before calling `free(fp)`.
- [x] Implement frame recycling in `MEMPHY_put_freefp()`.
- [x] Implement physical read and write in `MEMPHY_read()` and `MEMPHY_write()` with storage synchronization.
- [x] Implement memory dump in `MEMPHY_dump()`.

### Virtual Memory Areas & Swapping (`src/mm-vm.c`, `src/libmem.c`)
- [x] Implement `get_vma_by_num()` in `src/mm-vm.c` with iterative VMA traversal.
- [x] Implement `get_vm_area_node_at_brk()` in `src/mm-vm.c` to allocate and initialize new region nodes.
- [x] Implement `validate_overlap_vm_area()` in `src/mm-vm.c` to check boundaries against all existing VMAs.
- [x] Implement `inc_vma_limit()` in `src/mm-vm.c` with 64-bit/32-bit page alignment, overlap validation, and `vm_map_ram()`.
- [x] Implement `insert_vma_sorted()`, page-aligned `alloc_large_vma_macos()`, `free_large_vma_macos()`, and `alloc_chunk_vma()`.
- [x] Implement free region allocation & deallocation helpers: `get_free_vmrg_area()`, `__alloc()`, and `__free()` in `src/libmem.c`.
- [x] Define `OVERLAP(x1, x2, y1, y2)` and `INCLUDE(x1, x2, y1, y2)` range macros in `include/mm.h`.
- [x] Fix single-node list boundary check in `find_victim_page()` (`src/libmem.c`).
- [x] Implement recursive 5-level `free_level()` walk in `free_pcb_memph()` (`src/libmem.c`).
- [x] Complete `pg_getpage()` in `src/libmem.c` to handle page faults, swapping out victim frames (`RAM -> SWAP`) and swapping in target frames (`SWAP -> RAM`).
- [x] Connect `pg_getval()` and `pg_setval()` to physical I/O system calls (`SYSMEM_IO_READ`, `SYSMEM_IO_WRITE`).

### Kernel Space & Dual-Mode Isolation (`src/libmem.c`)
- [x] Implement `libkmem_copy_from_user()` (safe data transfer from userspace region to kernelspace region).
- [x] Implement `libkmem_copy_to_user()` (safe data transfer from kernelspace region to userspace region).
- [x] Implement `__kmalloc()` and `libkmem_malloc()` for kernel contiguous memory allocation.
- [x] Implement canonical 57-bit address validation and privileged memory access boundaries (block user `read`/`write` to kernel space).
- [x] Implement `__read_kernel_mem()` and `__write_kernel_mem()` mapping logic.

---

## 3. Co-Processor & DMA Subsystem (Section 2.3)

### System Bus & Physical Memory Access (`src/sysbus.c`, `include/sysbus.h`)
- [x] Implement `sysbus_set_os_memory()` to attach OS physical RAM (`mram.storage`) to the system bus.
- [x] Implement fallback in `memory_bank_get_ptr()` to route DMA transfers to OS physical RAM during `./os` execution.
- [x] Implement bounds-checking and `ram_storage_lock` synchronization for 32-bit hardware reads and writes on the bus.
- [x] Implement DPI hardware callbacks: `dpi_bank_read32_legacy()` and `dpi_bank_write32_legacy()`.
- [x] Implement interrupt notification in `dma_done_callback()` using condition variable broadcast.

### Asynchronous Co-Processor Driver (`src/dma_c_testbench.cpp`, `include/dma.h`)
- [x] Implement thread-safe request queue: `dma_async_submit()`.
- [x] Implement worker consumer loop: `dma_async_worker_loop()`.
- [x] Implement completion barriers: `dma_async_wait_all()` and `dma_async_wait_caller()`.
- [x] Implement hardware mutex protection (`g_dma_hw_lock`) in `do_dma()`.
- [x] Add interrupt validation and completion flag checking.
- [x] Separate standalone testbench pattern generation (`DMA_MAIN=1`) from live OS memory access (`DMA_MAIN=0`).

### OS Co-Processor Thread Integration (`src/os.c`)
- [x] Spawn background co-processor thread executing `dma_async_worker_loop()`.
- [x] Initialize Verilator RTL simulation via `rtl_dma_init_simulation()`.
- [x] Bind `mram.storage` to system bus at boot time.
- [x] Implement clean shutdown sequence (`dma_async_shutdown()`, thread join, cleanup) and fix POSIX `write` symbol collision in `src/cpu.c`.

### DMA Instruction Execution (`src/libmem.c`)
- [x] Implement `libmem_dma_memcpy()`:
  - [x] Resolve source and destination region IDs in process/kernel symbol tables.
  - [x] Translate virtual addresses (`rg_start + offset`) to physical bus addresses (`fpn * PAGE_SZ + offset`).
  - [x] Asynchronously submit transfer request to the co-processor queue.

---

## 4. System Call Interface (Section 1.6)

### System Call Dispatcher (`src/syscall.c`, `src/sys_mem.c`, `src/sys_listsyscall.c`)
- [x] Generate system call table (`src/syscall.tbl` $\rightarrow$ `src/syscalltbl.lst`).
- [x] Implement syscall listing in `__sys_listsyscall()`.
- [x] Refactor `__sys_memmap()` in `src/sys_mem.c`:
  - [x] Remove temporary dummy `malloc(sizeof(struct pcb_t))`.
  - [x] Resolve calling process `pcb_t *` by matching `pid` against kernel `running_list`.
  - [x] Enforce dual-mode access (prevent direct PCB pointer passing across user-kernel boundary).
- [x] Verify `listsyscall` (syscall 0) and `memmap` (syscall 17) handlers.

---

## 5. Synchronization & Concurrency (Section 2.4)

### Resource Lock Audit
- [x] Audit `queue_lock` in `sched.c` to prevent deadlock and race conditions during multi-CPU dispatch.
- [x] Protect DMA hardware and request queue via `g_dma_hw_lock` and `g_dma_q_lock`.
- [x] Audit `mmvm_lock` and `kmm_lock` in `libmem.c`.
- [x] Protect physical memory allocator (`free_fp_list` in `mm-memphy.c`) via `memphy_lock` against concurrent allocations from multiple CPUs.

---

## 6. Verification & Test Plan (Section 3)

### Unit & Standalone Tests
- [x] **DMA Hardware Testbench:**
  ```bash
  make dma && ./dma_testbench 0x1000 0x2000 64
  ```
- [x] **Paging & Virtual Memory Test Cases:**
  ```bash
  ./os os_0_mlq_paging
  ./os os_1_singleCPU_mlq_paging
  ./os os_1_mlq_paging
  ./os os_1_mlq_paging_small_1K
  ./os os_1_mlq_paging_small_4K
  ```
- [x] **Multi-CPU & DMA Co-Processor Test Cases:**
  ```bash
  ./os os_2_singleCPU_mlq_paging
  ./os os_2_mlq_paging
  ```
- [x] **System Call Test Cases:**
  ```bash
  ./os os_syscall
  ./os os_syscall_list
  ./os os_sc
  ```
- [ ] **Pure Scheduler Legacy Configs** (requires uncommenting `#define MM_FIXED_MEMSZ` in `include/os-cfg.h`):
  ```bash
  ./os sched_0
  ./os sched_1
  ./os os_1_singleCPU_mlq
  ```

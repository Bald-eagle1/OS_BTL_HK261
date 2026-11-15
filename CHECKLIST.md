# Assignment Implementation Checklist
> **CO2018 Operating Systems Assignment (HK261) — Simple Operating System Simulation**  
> Tracks module progress, open tasks, critical bug fixes, and verification test suites.

---

## Current Status Summary

| Module | Status | Progress | Notes |
| :--- | :---: | :---: | :--- |
| **1. Scheduler & MLQ Policy** | 🟡 In Progress | ~40% | Queue operations implemented; MLQ slot algorithm in `sched.c` pending. |
| **2. Memory Management (64-bit Paging)** | 🔴 Action Required | ~35% | Compilation error in `mm64.c` (`PAGING64_PAGESZ_LOBIT`); page table linking needs repair. |
| **3. Co-processor & DMA** | 🟢 Completed | 100% | Standalone testbench verified; asynchronous worker and bus addressing connected. |
| **4. System Call Interface** | ⚪ Pending | ~10% | PID-to-PCB resolution in `sys_mem.c` pending. |
| **5. Synchronization & Locking** | 🟡 In Progress | ~50% | DMA and memory locks active; queue and physical memory locks need audit. |
| **6. Report & Verification** | ⚪ Pending | ~15% | DMA testbench verified; full OS test runs and report pending other modules. |

---

## 1. Scheduler Subsystem (Section 2.1)

### Priority Queues (`include/queue.h`, `src/queue.c`)
- [x] Implement `empty(struct queue_t *q)` check.
- [x] Implement `enqueue(struct queue_t *q, struct pcb_t *proc)` with capacity boundary check (`MAX_QUEUE_SIZE`).
- [x] Implement `dequeue(struct queue_t *q)`:
  - [x] FIFO order for Multi-Level Queue (MLQ) mode.
  - [x] Highest-priority extraction for non-MLQ fallback mode.
- [x] Implement `purgequeue(struct queue_t *q, struct pcb_t *proc)` to remove a specific PCB.
- [x] Implement element shifting in `removeFromQueue()` to maintain contiguous array order.

### Multi-Level Queue (MLQ) Engine (`src/sched.c`)
- [x] Initialize MLQ ready queues `mlq_ready_queue[MAX_PRIO]` and slot quotas `slot[i] = MAX_PRIO - i`.
- [ ] Implement `get_mlq_proc()`:
  - [ ] Scan priority levels `0 .. MAX_PRIO-1`.
  - [ ] Select process from non-empty queue with `slot[prio] > 0`.
  - [ ] Decrement `slot[prio]--` on each dispatch.
  - [ ] When all active queues exhaust their slot quota (`slot[prio] == 0`), reset all slots to `MAX_PRIO - i`.
  - [ ] Add dispatched process to `running_list`.
  - [ ] Ensure `queue_lock` is properly unlocked on all exit paths.
- [ ] Implement `put_mlq_proc()`:
  - [ ] Re-enqueue pre-empted process back to `mlq_ready_queue[proc->prio]`.
  - [ ] Remove from `running_list` under mutex lock.
- [ ] Implement `add_mlq_proc()`:
  - [ ] Add newly loaded process to `mlq_ready_queue[proc->prio]`.
- [ ] Merge scheduler feature branch into `main`.

---

## 2. Memory Management Subsystem (Section 2.2)

### 64-bit 5-Level Paging (`src/mm64.c`, `include/mm64.h`)
- [ ] **[CRITICAL]** Fix compilation error in `mm64.c` (replace undeclared `PAGING64_PAGESZ_LOBIT` with `PAGING64_ADDR_PT_SHIFT` / `12`).
- [ ] **[CRITICAL]** Fix multi-level page directory linking in `init_mm()`:
  - Allocate and link directory hierarchy pointers (`pgd` $\rightarrow$ `p4d` $\rightarrow$ `pud` $\rightarrow$ `pmd` $\rightarrow$ `pt`) to prevent null-pointer dereferences in `pte_get_entry()`, `pte_set_fpn()`, `pte_set_swap()`, and `pte_set_entry()`.
- [ ] Fix syntax and structure in `vmap_page_range()` (remove premature closing brace and complete VMA tracking).
- [ ] Complete `alloc_pages_range()`:
  - [x] Request free frames from `mram` via `MEMPHY_get_freefp()`.
  - [ ] Handle page eviction/swap when physical RAM is exhausted.
- [ ] Complete `vmap_pgd_memset()` with page directory mapping pattern.
- [ ] Implement `print_pgtbl()` to match reference dump format (`PDG=... P4D=... PUD=... PMD=... PT=...`).

### Physical Memory Management (`src/mm-memphy.c`)
- [ ] Fix free frame list tracking in `MEMPHY_get_freefp()`:
  - Retrieve head frame number `*retfpn = fp->fpn`.
  - Advance `mp->free_fp_list = fp->fp_next` before freeing node.
- [x] Implement frame recycling in `MEMPHY_put_freefp()`.
- [x] Implement physical read and write in `MEMPHY_read()` and `MEMPHY_write()`.
- [ ] Implement memory dump in `MEMPHY_dump()`.

### Virtual Memory Areas & Swapping (`src/mm-vm.c`, `src/libmem.c`)
- [ ] Define `OVERLAP(x1, x2, y1, y2)` and `INCLUDE(x1, x2, y1, y2)` range macros in `include/mm.h`.
- [ ] Implement `inc_vma_limit()` in `mm-vm.c` to expand process virtual memory boundary (`sbrk`).
- [ ] Implement `get_vm_area_node_at_brk()` in `mm-vm.c`.
- [ ] Implement victim page selection in `find_victim_page()` (`src/libmem.c`) with boundary check for single-node FIFO list.
- [ ] Complete `pg_getpage()` in `src/libmem.c` to handle page faults, swapping out victim frames (`RAM -> SWAP`) and swapping in target frames (`SWAP -> RAM`).
- [ ] Connect `pg_getval()` and `pg_setval()` to physical I/O system calls (`SYSMEM_IO_READ`, `SYSMEM_IO_WRITE`).

### Kernel Space & Dual-Mode Isolation (`src/libmem.c`)
- [ ] Implement `__kmalloc()` and `libkmem_malloc()` for kernel contiguous memory allocation.
- [ ] Implement privileged memory access boundaries (block user `read`/`write` to kernel space).
- [ ] Implement `libkmem_copy_from_user()` (safe data transfer from userspace region to kernelspace region).
- [ ] Implement `libkmem_copy_to_user()` (safe data transfer from kernelspace region to userspace region).

---

## 3. Co-Processor & DMA Subsystem (Section 2.3)

### System Bus & Physical Memory Access (`src/sysbus.c`, `include/sysbus.h`)
- [x] Implement `sysbus_set_os_memory()` to attach OS physical RAM (`mram.storage`) to the system bus.
- [x] Implement fallback in `memory_bank_get_ptr()` to route DMA transfers to OS physical RAM during `./os` execution.
- [x] Implement bounds-checking for 32-bit hardware reads and writes on the bus.
- [x] Implement DPI hardware callbacks: `dpi_bank_read32_legacy()` and `dpi_bank_write32_legacy()`.
- [x] Implement interrupt notification in `dma_done_callback()` using condition variable broadcast.

### Asynchronous Co-Processor Driver (`src/dma_c_testbench.cpp`, `include/dma.h`)
- [x] Implement thread-safe request queue: `dma_async_submit()`.
- [x] Implement worker consumer loop: `dma_async_worker_loop()`.
- [x] Implement hardware mutex protection (`g_dma_hw_lock`) in `do_dma()`.
- [x] Add interrupt validation and completion flag checking.
- [x] Separate standalone testbench pattern generation (`DMA_MAIN=1`) from live OS memory access (`DMA_MAIN=0`).

### OS Co-Processor Thread Integration (`src/os.c`)
- [x] Spawn background co-processor thread executing `dma_async_worker_loop()`.
- [x] Initialize Verilator RTL simulation via `rtl_dma_init_simulation()`.
- [x] Bind `mram.storage` to system bus at boot time.
- [x] Implement clean shutdown sequence (`dma_async_shutdown()`, thread join, cleanup).

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
- [ ] Refactor `__sys_memmap()` in `src/sys_mem.c`:
  - [ ] Remove temporary dummy `malloc(sizeof(struct pcb_t))`.
  - [ ] Resolve calling process `pcb_t *` by matching `pid` against kernel queue lists (`running_list` / ready queues).
  - [ ] Enforce dual-mode access (prevent direct PCB pointer passing across user-kernel boundary).
- [ ] Verify `listsyscall` (syscall 0) and `memmap` (syscall 17) handlers.

---

## 5. Synchronization & Concurrency (Section 2.4)

### Resource Lock Audit
- [ ] Audit `queue_lock` in `sched.c` to prevent deadlock and race conditions during multi-CPU dispatch.
- [x] Protect DMA hardware and request queue via `g_dma_hw_lock` and `g_dma_q_lock`.
- [x] Audit `mmvm_lock` and `kmm_lock` in `libmem.c`.
- [ ] Protect physical memory allocator (`free_fp_list` in `mm-memphy.c`) against concurrent allocations from multiple CPUs.

---

## 6. Verification & Test Plan (Section 3)

### Unit & Standalone Tests
- [x] **DMA Hardware Testbench:**
  ```bash
  make dma && ./dma_testbench 0x1000 0x2000 64
  ```
- [ ] **Scheduler Test Cases:**
  ```bash
  ./os sched_0
  ./os sched_1
  ./os os_1_singleCPU_mlq
  ```
- [ ] **Paging & Virtual Memory Test Cases:**
  ```bash
  ./os os_0_mlq_paging
  ./os os_1_singleCPU_mlq_paging
  ./os os_1_mlq_paging
  ./os os_1_mlq_paging_small_1K
  ./os os_1_mlq_paging_small_4K
  ```
- [ ] **Multi-CPU & DMA Co-Processor Test Cases:**
  ```bash
  ./os os_2_singleCPU_mlq_paging
  ./os os_2_mlq_paging
  ```
- [ ] **System Call Test Cases:**
  ```bash
  ./os os_syscall
  ./os os_syscall_list
  ./os os_sc
  ```

---

## 7. Submission & Report Deliverables (Section 3.3)

- [ ] Complete answers to theoretical questions in specification PDF:
  - [ ] Unified system call interface advantages & disadvantages (p. 9).
  - [ ] Handling runaway/long-running system calls (p. 11).
  - [ ] Benefits of MLQ detailed scheduling policies (p. 13).
  - [ ] Motivation for combining segmentation with paging (p. 17).
  - [ ] Benefits of extending hierarchical paging to N levels (p. 18).
  - [ ] Trade-offs of paging vs. contiguous memory allocation (p. 19).
  - [ ] Concurrency and race conditions in Simple OS (p. 25).
- [ ] Gantt charts for CPU scheduling execution traces.
- [ ] Memory allocation status and multi-level address translation documentation.
- [ ] Final package compression: `assignment_STUDENTID.zip`.

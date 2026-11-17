# demacOprx — Operating System Simulation with DMA Subsystem
> **Course:** CO2018 Operating Systems — Ho Chi Minh City University of Technology (HCMUT)

A multi-tasking operating system simulator that integrates 64-bit virtual memory management, multi-level queue scheduling, and a cycle-accurate hardware DMA co-processor powered by Verilator RTL and DPI-C.

---

## 🚀 Key Modules & What We Built

### 1. Asynchronous Co-processor & DMA Subsystem (`Module 2.3`)
* **Physical System Bus Interface (`src/sysbus.c`):**
  * Flat physical linear address space ($0 \to \text{MaxSize} - 1$).
  * Dual-mode address router: safely routes bus offsets to either a 1 MB test memory bank (standalone mode) or the OS kernel's real physical RAM array `krnl->mram->storage` (live OS mode).
  * Boundary protection preventing out-of-bounds reads/writes.
* **Verilator RTL Integration via DPI-C (`src/dpi_rtl.cpp`, `src/dma_c_testbench.cpp`):**
  * Cycle-accurate 32-bit word transfers (`dpi_bank_read32_legacy`, `dpi_bank_write32_legacy`).
  * Completion signaling via hardware interrupts (`dma_done_callback`).
* **Virtual-to-Physical Address Translation (`src/libmem.c`):**
  * Translates logical region IDs and byte offsets into physical bus offsets before passing transfer descriptors to hardware.
  * Walks the 5-level page table for user pages (`FPN * 4096 + offset`).
  * Direct arithmetic subtraction for kernel direct-mapped addresses (`VA - 0xff00000000000000ULL`).
* **Non-Blocking Asynchronous Queue:**
  * Background worker thread with thread-safe FIFO request queue (`dma_async_submit`).
  * Non-blocking execution so the CPU continues running processes while the DMA hardware transfers data in parallel.

### 2. 64-bit Memory Management (`MM64` & VMAs)
* **5-Level Paging Hierarchy (`src/mm64.c`):** PGD $\to$ P4D $\to$ PUD $\to$ PMD $\to$ PT directory structures with recursive cleanup (`free_level`).
* **VMA Allocation (`src/libmem.c`, `src/mm-vm.c`):** First-fit search (`alloc_large_vma_macos`, `alloc_chunk_vma`), sorted VMA linked list insertion, and frame reclamation.
* **Kernel Heap Allocator:** Kernel memory allocation (`kmalloc`) and user-to-kernel memory transfer routines.

### 3. Multi-Level Queue Scheduler & Concurrency
* **MLQ Scheduler (`src/sched.c`, `src/queue.c`):** Priority-based scheduling (priorities 0–139) with preemption and CPU time slicing.
* **Thread-Safe Synchronization:** Concurrency locks on physical RAM storage (`lock_ram_storage`), virtual memory operations (`mmvm_lock`), and DMA completion barriers (`dma_async_wait_all`).

---

## 🛠️ Prerequisites

* **Operating System:** Linux (Ubuntu 20.04/22.04 or WSL2 on Windows)
* **Packages:**
  ```bash
  sudo apt update
  sudo apt install -y build-essential gcc g++ make verilator
  ```

---

## 🔨 How to Build

Compile everything using `make`:

```bash
# Clean previous build artifacts
make clean

# Build the complete OS simulator (output: ./os)
make os

# Build the standalone DMA hardware testbench (output: ./dma_testbench)
make dma
```

---

## 🏃 How to Run

### 1. Standalone DMA Testbench
Tests the Verilator RTL DMA engine in isolation using the temporary 1 MB memory bank:

```bash
# Syntax: ./dma_testbench <src_offset> <dst_offset> <length_bytes>
./dma_testbench 0x1000 0x2000 64
```
*Copies 64 bytes from offset `0x1000` to `0x2000` and validates the transferred pattern.*

### 2. Full OS Simulation
Run the OS simulator with any test configuration from the `input/` folder:

```bash
# Multi-tasking OS with DMA memcpy transfers:
./os os_2_mlq_paging

# Single-CPU and MLQ paging test suites:
./os os_0_mlq_paging
./os os_1_mlq_paging
./os os_1_singleCPU_mlq_paging
./os os_1_mlq_paging_small_4K

# System call test suite:
./os os_syscall
./os os_syscall_list
```

---

## 📁 Repository Structure

```
├── include/              # Header files (sysbus.h, mm.h, common.h, os-mm.h, etc.)
├── src/
│   ├── sysbus.c          # Physical System Bus interface & dual-mode memory routing
│   ├── dma_c_testbench.cpp # Async DMA worker loop, queue, and RTL testbench
│   ├── dpi_rtl.cpp       # DPI-C bridge to Verilator simulation
│   ├── libmem.c          # Memory syscalls, VMA allocators, and DMA address translation
│   ├── mm64.c            # 64-bit 5-level paging directories and table operations
│   ├── cpu.c             # CPU instruction execution and dispatch loop
│   ├── os.c              # OS boot, timer, and initialization
│   └── sched.c           # Multi-level queue scheduler
├── input/                # Test configuration files and process instruction scripts
│   ├── os_2_mlq_paging   # Multi-process configuration with DMA instructions
│   └── proc/             # Process instruction scripts (m2s, s0, p0s, etc.)
└── Makefile              # Build automation for ./os and ./dma_testbench
```

---

## 📜 License
Educational use only for Course CO2018 — Ho Chi Minh City University of Technology (HCMUT).

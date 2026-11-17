# demacOprx — Operating System Simulation with DMA Subsystem
> **Course:** CO2018 Operating Systems — Ho Chi Minh City University of Technology (HCMUT)
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

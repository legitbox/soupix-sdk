# Linux 6.12 LTS Migration Assessment

## ION Backport Strategy (Option A)

### What ION is
ION is the buffer allocator underpinning the entire multimedia pipeline. Every frame of video, every audio buffer, every TPU inference input flows through ION-allocated memory. It was removed from upstream Linux in 5.11 but the CVITEK code is self-contained and can be dropped back in.

### ION source files to backport (17 files total)

**Core ION framework** (from `drivers/staging/android/ion/`):
- `ion.c`, `ion.h` — core device, ioctl handler, buffer management
- `ion_heap.c` — common heap operations
- `ion_carveout_heap.c` — reserved memory heap (used by CVITEK for VPP/NPU carveouts)
- `ion_cma_heap.c` — CMA-backed heap
- `ion_chunk_heap.c` — chunked heap
- `ion_system_heap.c` — system memory heap
- `ion_page_pool.c` — page pool for system heap
- `compat_ion.c`, `compat_ion.h` — 32/64-bit compat ioctl layer
- `Kconfig`, `Makefile`

**CVITEK ION extensions** (from `cvitek/` subdirectory):
- `cvitek_ion.c` — platform driver, DT parsing, heap setup, custom ioctls (cache flush/invalidate, heap info)
- `cvitek_ion_alloc.c` — kernel-side allocation helpers (`cvi_ion_alloc`, `cvi_ion_free`, etc.)
- `cvitek_ion_alloc.h` — header
- `cvitek_ion_debugfs.c` — debug filesystem entries
- `Makefile`

**UAPI headers** (from `drivers/staging/android/uapi/`):
- `ion.h` — ioctl definitions, heap types, allocation structs
- `ion_cvitek.h` — CVITEK custom ioctls (flush range, invalidate range, heap info)

### Heap types used
| Compatible string | Heap type | Purpose |
|---|---|---|
| `cvitek,carveout_vpp` | CARVEOUT | Video processing (ISP, VPSS, codec) |
| `cvitek,cma_vpp` | DMA/CMA | Fallback VPP allocation |
| `cvitek,carveout_npu` | CARVEOUT | TPU inference buffers |
| `cvitek,carveout` | CARVEOUT | Generic reserved memory |
| `cvitek,sys_contig` | SYSTEM_CONTIG | Contiguous system memory |

### Custom ioctls
- `ION_IOC_CVITEK_FLUSH_RANGE` — flush CPU cache for virtual address range
- `ION_IOC_CVITEK_INVALIDATE_RANGE` — invalidate cache (VA-based)
- `ION_IOC_CVITEK_GET_HEAP_INFO` — query heap total/available size
- `ION_IOC_CVITEK_FLUSH_PHY_RANGE` — flush cache by physical address
- `ION_IOC_CVITEK_INVALIDATE_PHY_RANGE` — invalidate cache by physical address

### ION consumers in osdrv (21 files across 11 modules)
| Module | Files | Usage |
|---|---|---|
| `base/` (vb.c) | 1 | Video buffer management — ION alloc/free core |
| `sys/` | 3 | System module — ION memory management |
| `vi/` | 4 | Video input — frame buffer allocation |
| `vpss/` | 3 | Video processing — buffer alloc/mapping |
| `cvi_vc_drv/` | 3 | Video codec + JPEG — bitstream buffers |
| `vo/` | 1 | Video output — display buffers |
| `rgn/` | 2 | Region/OSD — overlay buffers |
| `dwa/` | 2 | Dewarp — frame buffers |
| `ive/` | 1 | Intelligent video — processing buffers |
| `fast_image/` | 1 | Fast boot image capture |
| `tpu/` | — | Uses ION indirectly through sys module |

### API surface that must be preserved
```c
// Kernel-side allocation (used by osdrv drivers)
int cvi_ion_alloc(enum ion_heap_type type, size_t len, bool mmap_cache);
void cvi_ion_free(pid_t fd_pid, int fd);
struct ion_buffer *cvi_ion_alloc_nofd(enum ion_heap_type type, size_t len, bool mmap_cache);
void cvi_ion_free_nofd(struct ion_buffer *buffer);
int cvi_ion_get_memory_state(uint64_t *total, uint64_t *free, uint64_t *max_avail);

// Core ION (used internally)
int ion_alloc(size_t len, unsigned int heap_id_mask, unsigned int flags, struct ion_buffer **buf);
struct ion_buffer *ion_alloc_nofd(size_t len, unsigned int heap_id_mask, unsigned int flags);
void ion_free(pid_t fd_pid, int fd);
void ion_free_nofd(struct ion_buffer *buffer);
void ion_device_add_heap(struct ion_heap *heap);
int ion_query_heaps(struct ion_heap_query *query, int is_kernel);

// Buffer fields accessed by drivers
buffer->paddr   // physical address (CVITEK extension)
buffer->name    // buffer name (CVITEK extension)
buffer->size
buffer->sg_table
```

---

## Kernel API Changes: 5.10 -> 6.12

### Critical changes affecting ION backport

| Change | Impact | Fix |
|---|---|---|
| `drivers/staging/android/` removed entirely | ION directory gone | Re-create directory, add to staging Kconfig/Makefile |
| `struct shrinker` API changed (6.0+) | `ion_heap.shrinker` field | Use `shrinker_alloc()`/`shrinker_register()` instead of `register_shrinker()` |
| `set_fs()`/`get_fs()` removed (5.18+) | Used in `cvitek_ion_alloc.c` for ARM | Remove — RISC-V path already uses `force_uaccess_begin()`, extend to all arches |
| `pte_offset_map()` returns NULL on failure (6.5+) | Used in `get_user_pa()` | Add NULL check |
| `__inval_dcache_area()` renamed/removed | Used in cache invalidate ioctl | Use `dcache_inval_poc()` or arch-specific equivalent |
| `__dma_map_area()` removed (ARM64) | Used in physical cache flush/invalidate | Use `arch_sync_dma_for_device()` (already has fallback path) |
| `misc_register` / `miscdevice` changes | Minor | Mechanical |

### Critical changes affecting ALL drivers (osdrv + in-tree)

| Change | Scope | Fix type |
|---|---|---|
| `platform_driver.remove` returns `void` (was `int`) | ALL platform drivers (~30+) | Mechanical: change return type, remove `return 0` |
| Timer API: `init_timer`/`setup_timer` gone | Audit all timer usage | Use `timer_setup()` |
| `ioremap()` changes | All MMIO drivers | Verify calls, may need `ioremap_wc()` |
| `of_device_is_compatible()` signature | DT matching code | Verify |
| Proc filesystem API | Drivers using `/proc` | Use `proc_create_single()` etc. |
| `class_create()` lost owner arg (6.4+) | Char device drivers | Remove `THIS_MODULE` arg |
| `__user` annotation enforcement stricter | Many drivers | Fix sparse warnings |

### RISC-V architecture changes (HIGH complexity)

| Change | Notes |
|---|---|
| Upstream T-HEAD support in 6.x | `arch/riscv/` has official `ERRATA_THEAD_*` configs — must reconcile with `ARCH_CVITEK` |
| Vector extension handling | Upstream 6.x has proper V-extension support — `CONFIG_VECTOR_0_7` is CVITEK-specific |
| `march-cvitek/` custom code | Must verify if upstream now covers C906 quirks |
| SBI interface changes | OpenSBI compatibility |
| Memory model changes | `CONFIG_RISCV_ISA_THEAD` may conflict with upstream |

---

## Toolchain Summary

| Target | Current | Notes |
|---|---|---|
| RISC-V (C906) | GCC 10, musl | Uses `-march=rv64imafdcv0p7xthead -mcpu=c906fdv` — requires T-HEAD aware toolchain |
| ARM64 (A53) | GCC 7.3.1, glibc | Very old, may need upgrade for 6.12 kernel headers |
| FreeRTOS (C906) | GCC bare-metal | `-march=rv64imafdc` — no v0p7/xthead, should be fine |
| osdrv | Uses `-Wall -Wextra -Werror` | Will break on new warnings from GCC 11+ or 6.12 kernel |
| cvi_mpi | Uses `-Wall -Wextra -Werror` | Same risk |

**Recommendation**: Use Xuantie (T-HEAD) GCC 13.x toolchain — maintains v0p7 support while getting modern C23 and better optimization.

---

## Migration Order

```
Phase A: ION Backport (THIS FIRST)
  1. Create drivers/staging/android/ion/ in 6.12 tree
  2. Drop in all 17 files
  3. Fix shrinker API, remove set_fs(), fix cache ops
  4. Wire into staging Kconfig/Makefile
  5. Test: /dev/ion appears, heap query works

Phase B: In-tree CVITEK Drivers (~148 files)
  1. Port arch/riscv/ CVITEK additions (Kconfig.socs, march-cvitek/)
  2. Port clk, pinctrl, thermal drivers
  3. Port STMMAC ethernet glue + PHY
  4. Port crypto, DMA, SoC integration
  5. Port ASoC audio (27 files)
  6. Port fbdev, efuse, power/reset
  7. Port AIC8800 WiFi (large, relatively independent)
  8. Port device trees

Phase C: Out-of-tree Drivers (osdrv, 232K LOC)
  Port in dependency order:
  base -> sys -> rtos_cmdqu -> vi -> vpss -> vcodec+jpeg ->
  vo -> rgn -> dwa -> cif -> tpu -> ive -> peripherals

Phase D: Userspace (cvi_mpi)
  - Update KERNEL_PATH to 6.12 source
  - Fix any UAPI header changes
  - With ION backported: no userspace changes needed
  - Rebuild all 18 modules

Phase E: Integration Testing
  - Full module load order validation
  - Camera capture test
  - Encode/decode test
  - FreeRTOS mailbox echo
  - TPU inference
  - 72-hour stability soak
```

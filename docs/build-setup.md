# soupix-sdk Build Setup

## Prerequisites

```bash
# Ubuntu/Debian build dependencies
sudo apt install -y pkg-config build-essential ninja-build automake autoconf \
    libtool wget curl git gcc libssl-dev bc slib squashfs-tools jq \
    python3-distutils scons parallel python3-dev python3-pip device-tree-compiler \
    ssh cpio fakeroot libncurses5 flex bison libncurses5-dev genext2fs rsync unzip \
    dosfstools mtools tcl openssh-client cmake expect python-is-python3 python3-jinja2 zstd

# RISC-V cross-compiler (stock, for initial compile testing)
sudo apt install -y gcc-riscv64-linux-gnu

# For production builds, use T-HEAD Xuantie GCC with v0p7 vector support:
# Download from https://www.xrvm.com/community/download?id=4224193099938988032
# or use the host-tools from the original SDK
```

## Host Tools (T-HEAD Toolchain)

The SDK expects a T-HEAD-aware RISC-V toolchain at `host-tools/gcc/`:
```bash
git clone https://github.com/milkv-duo/host-tools.git host-tools
```

## Kernel Build (6.12 LTS)

```bash
cd linux

# Configure for Milk-V Duo
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-musl- milkv_duo_defconfig

# Or use stock GCC for testing (won't support v0p7 extensions)
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- milkv_duo_defconfig

# Build
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-musl- -j$(nproc) Image modules dtbs
```

## Full SDK Build

```bash
# Build everything (uses build/Makefile orchestration)
./build.sh milkv-duo-musl-riscv64-sd
```

## Current Status

### What works
- ION backported to 6.12 with all API fixes
- All CVITEK in-tree drivers overlaid and wired into Kconfig
- Mechanical API fixups done (.remove_new, class_create, stmmac)
- Defconfig created

### What needs compile testing
- CVITEK clock drivers may need CCF API updates
- CVITEK pinctrl may need pinctrl subsystem API updates
- MMC/SDHCI drivers may need SDHCI core API updates
- AIC8800 WiFi driver (83K LOC, large)
- ASoC audio drivers may need DAPM/codec API updates
- arch/riscv ARCH_CVITEK Kconfig reconciliation with upstream T-HEAD

### Known remaining issues
- `CONFIG_VECTOR_0_7` and `CONFIG_RISCV_ISA_THEAD` are CVITEK-custom Kconfig
  options that don't exist in upstream 6.12 RISC-V arch code
- The `march-cvitek/` code may conflict with upstream T-HEAD errata support
- The `SCHED_CVITEK` config (selected by ARCH_CVITEK) doesn't exist upstream
- `ION_OF` config (selected by ION_CVITEK) doesn't exist — removed from Kconfig

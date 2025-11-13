# 规范遵从（Spec conformance）

本 runc 分支对 `linux` 平台**实现了** [OCI Runtime Spec v1.3.0](https://github.com/opencontainers/runtime-spec/tree/v1.3.0)。

以下特性尚未实现：

| 规范版本 | 特性                                     | PR                                                        |
| -------- | ---------------------------------------- | --------------------------------------------------------- |
| v1.1.0   | `SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV` | [#3862](https://github.com/opencontainers/runc/pull/3862) |
| v1.3.0   | 对 `linux.intelRdt` 的解释进行澄清       | [#3832](https://github.com/opencontainers/runc/pull/3832) |
| v1.3.0   | 在 poststart hook 失败时使运行失败       | [#4348](https://github.com/opencontainers/runc/pull/4348) |

## 架构

支持以下架构：

| runc 二进制 | seccomp                                              |
| ----------- | ---------------------------------------------------- |
| `amd64`     | `SCMP_ARCH_X86`, `SCMP_ARCH_X86_64`, `SCMP_ARCH_X32` |
| `arm64`     | `SCMP_ARCH_ARM`, `SCMP_ARCH_AARCH64`                 |
| `armel`     | `SCMP_ARCH_ARM`                                      |
| `armhf`     | `SCMP_ARCH_ARM`                                      |
| `ppc64le`   | `SCMP_ARCH_PPC64LE`                                  |
| `riscv64`   | `SCMP_ARCH_RISCV64`                                  |
| `s390x`     | `SCMP_ARCH_S390`, `SCMP_ARCH_S390X`                  |
| `loong64`   | `SCMP_ARCH_LOONGARCH64`                              |

runc 二进制也可能可编译为 i386、大端序的 PPC64 以及若干 MIPS 变体，但这些架构**并非官方支持**。

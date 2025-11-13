## systemd cgroup 驱动

默认情况下，`runc` 自行创建 cgroup 并设置 cgroup 限制（此模式称为 **fs cgroup 驱动**）。当传入全局选项 `--systemd-cgroup`（例如 `runc --systemd-cgroup run ...`）时，`runc` 切换为 **systemd cgroup 驱动**。本文档描述该驱动的特性与一些注意事项。

### systemd 单元名称与放置位置

创建容器时，`runc` 会通过 dbus 向 systemd 请求为该容器创建一个临时单元（transient unit），并将其放入指定的 `slice` 中。

单元名和所属 slice 的生成方式根据容器运行时规范如下决定：

1. 如果设置了 `Linux.CgroupsPath`，其格式应为 `[slice]:[prefix]:[name]`。

   这里 `slice` 是容器将要放入的 systemd slice。如果为空，默认是 `system.slice`；但当使用 cgroup v2 且创建的是无 root 容器（rootless）时，默认值为 `user.slice`。

   注意 `slice` 可以包含连字符以表示子 slice（例如 `user-1000.slice` 是合法表示，表示 `user.slice` 的子 slice），但不得包含斜杠（例如 `user.slice/user-1000.slice` 为无效写法）。

   当 `slice` 为 `-` 时表示根 slice。

   接着，`prefix` 和 `name` 用于组成单元名，格式为 `<prefix>-<name>.scope`；若 `name` 以 `.slice` 为后缀，则忽略 `prefix` 并直接使用 `name`（作为 slice 单元）。`prefix` 和 `name` 的默认值均为空字符串。

2. 如果未设置或将 `Linux.CgroupsPath` 置为空，则行为等同于将其设置为 `:runc:<container-id>`。参见上文说明该设置将如何转换。

如上所述，被创建的单元可以是 scope 或 slice。对于 scope，`runc` 通过 systemd 属性 `Slice=` 指定其父 slice，并同时设置 `Delegate=true`。对于 slice，`runc` 通过 `Wants=` 属性指定对父 slice 的弱依赖。

### 资源限制

`runc` 始终为所有控制器启用统计（accounting），无论是否设置了具体限制。这意味着它会无条件为被创建的 systemd 单元设置如下属性：

- `CPUAccounting=true`
- `IOAccounting=true`（cgroup v1 时为 `BlockIOAccounting`）
- `MemoryAccounting=true`
- `TasksAccounting=true`

`runc` 将运行时规范中的资源限制翻译为 systemd 单元属性，从而为该单元设置资源限制。

这种翻译并不完全覆盖所有 cgroup 属性，因为有些 cgroup 属性无法通过 systemd 设置。因此，`runc` 的 systemd cgroup 驱动由 fs 驱动作后备（换言之，cgroup 限制会先通过 systemd 单元属性设置，必要时还会通过写入 cgroupfs 文件来设置）。

`runc` 将哪些运行时规范资源翻译为 systemd 单元属性，取决于所用内核的 cgroup 版本（v1 或 v2）以及运行的 systemd 版本。如果所用的 systemd 版本较旧（不支持某些资源），`runc` 将不会设置那些资源。

下表汇总了受支持的翻译。

#### cgroup v1

| 运行时规范资源   | systemd 属性名       | 最低 systemd 版本 |
| ---------------- | -------------------- | ----------------- |
| `memory.limit`   | `MemoryLimit`        |                   |
| `cpu.shares`     | `CPUShares`          |                   |
| `blockIO.weight` | `BlockIOWeight`      |                   |
| `pids.limit`     | `TasksMax`           |                   |
| `cpu.cpus`       | `AllowedCPUs`        | v244              |
| `cpu.mems`       | `AllowedMemoryNodes` | v244              |

#### cgroup v2

| 运行时规范资源            | systemd 属性名                  | 最低 systemd 版本 |
| ------------------------- | ------------------------------- | ----------------- |
| `memory.limit`            | `MemoryMax`                     |                   |
| `memory.reservation`      | `MemoryLow`                     |                   |
| `memory.swap`             | `MemorySwapMax`                 |                   |
| `cpu.shares`              | `CPUWeight`                     |                   |
| `pids.limit`              | `TasksMax`                      |                   |
| `cpu.cpus`                | `AllowedCPUs`                   | v244              |
| `cpu.mems`                | `AllowedMemoryNodes`            | v244              |
| `unified.cpu.max`         | `CPUQuota`, `CPUQuotaPeriodSec` | v242              |
| `unified.cpu.weight`      | `CPUWeight`                     |                   |
| `unified.cpu.idle`        | `CPUWeight`                     | v252              |
| `unified.cpuset.cpus`     | `AllowedCPUs`                   | v244              |
| `unified.cpuset.mems`     | `AllowedMemoryNodes`            | v244              |
| `unified.memory.high`     | `MemoryHigh`                    |                   |
| `unified.memory.low`      | `MemoryLow`                     |                   |
| `unified.memory.min`      | `MemoryMin`                     |                   |
| `unified.memory.max`      | `MemoryMax`                     |                   |
| `unified.memory.swap.max` | `MemorySwapMax`                 |                   |
| `unified.pids.max`        | `TasksMax`                      |                   |

有关 systemd 单元资源属性的文档，请参阅 `systemd.resource-control(5)` 手册页。

### 辅助属性

systemd 单元的辅助属性（容器创建后通过 `systemctl show <unit-name>` 可见）可以通过在容器运行时规范（`config.json`）中添加注解来设置或覆盖。

例如：

```json
        "annotations": {
                "org.systemd.property.TimeoutStopUSec": "uint64 123456789",
                "org.systemd.property.CollectMode":"'inactive-or-failed'"
        },
```

上述注解将设置如下属性：

- `TimeoutStopSec` 为 2 分 3 秒；
- `CollectMode` 为 `"inactive-or-failed"`。

这些值必须采用 gvariant 文本格式，详见 [gvariant 文档](https://docs.gtk.org/glib/gvariant-text-format.html)。

要确定 systemd 对特定参数期望的类型，请查阅 systemd 源码或相应文档。

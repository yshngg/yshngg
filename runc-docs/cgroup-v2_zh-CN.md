# cgroup v2

自 **v1.0.0-rc93** 起，`runc` 已完全支持 **cgroup v2（统一模式）**。

要使用 cgroup v2，可能需要修改宿主机的 init 系统配置。以下发行版已知默认使用 cgroup v2：

<!-- the list should be kept in sync with https://github.com/rootless-containers/rootlesscontaine.rs/blob/master/content/getting-started/common/cgroup2.md -->

- Fedora（自 31 起）
- Arch Linux（自 2021 年 4 月起）
- openSUSE Tumbleweed（自大约 2021 年起）
- Debian GNU/Linux（自 11 起）
- Ubuntu（自 21.10 起）
- RHEL 及 RHEL 类发行版（自 9 起）

在其他基于 systemd 的发行版上，可以通过在内核命令行添加 `systemd.unified_cgroup_hierarchy=1` 来启用 cgroup v2。

## 我在使用 cgroup v2 吗？

如果存在 `/sys/fs/cgroup/cgroup.controllers` 则表示是。

## 主机要求

### 内核（Kernel）

- 推荐版本：5.2 或更高
- 最低版本：4.15

不建议使用早于 5.2 的内核，因为缺少 freezer（冻结）功能。

特别地，**严禁** 使用早于 4.15 的内核（除非你在使用带用户命名空间的容器），因为这些内核不支持对设备权限的控制。

### systemd

在 cgroup v2 主机上，**强烈建议** 使用 systemd cgroup 驱动运行 `runc`（`runc --systemd-cgroup`），但这不是强制性的。

推荐的 systemd 版本是 244 或更高；较旧的 systemd 不支持将 `cpuset` 控制器委派出去。

确保已安装 `dbus-user-session`（Debian/Ubuntu）或 `dbus-daemon`（CentOS/Fedora）包，并且 `dbus` 正在运行。在 Debian 系发行版上可以按如下操作：

```bash
sudo apt install -y dbus-user-session
systemctl --user start dbus
```

## Rootless

在 cgroup v2 主机上，rootless 模式下的 `runc` 可以与 systemd 通信以获取要被委派的 cgroup 权限。

```bash
runc spec --rootless
jq '.linux.cgroupsPath="user.slice:runc:foo"' config.json | sponge config.json
runc --systemd-cgroup run foo
```

容器进程会在类似如下的 cgroup 中运行：
`/user.slice/user-$(id -u).slice/user@$(id -u).service/user.slice/runc-foo.scope`。

### 配置委派

通常，默认情况下只会将 `memory` 和 `pids` 控制器委派给非 root 用户。

```console
$ cat /sys/fs/cgroup/user.slice/user-$(id -u).slice/user@$(id -u).service/cgroup.controllers
memory pids
```

要允许委派其他控制器，需要按如下修改 systemd 配置：

```bash
sudo mkdir -p /etc/systemd/system/user@.service.d
cat <<EOF | sudo tee /etc/systemd/system/user@.service.d/delegate.conf
[Service]
Delegate=cpu cpuset io memory pids
EOF
sudo systemctl daemon-reload
```

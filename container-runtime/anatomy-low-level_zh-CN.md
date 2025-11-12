# 容器运行时 第2部分：底层容器运行时的结构

这是关于容器运行时的四部分系列中的第二篇。在[第1部分](https://www.ianlewis.org/en/container-runtimes-part-1-introduction-container-r)里，我对容器运行时做了概览并讨论了低层与高层运行时的区别。本文将详细讲解**低层容器运行时**。

低层运行时具有有限的功能集，通常只执行运行容器所需的底层任务。大多数开发者在日常工作中不需要直接使用它们。低层运行时通常以简单的工具或库形式实现，供高层运行时和容器工具的开发者在实现底层功能时调用。虽然多数开发者不会直接使用低层运行时，但了解它们的工作方式对于故障排查和调试是很有帮助的。

正如我在第1部分所述，容器是通过 [Linux namespaces](https://en.wikipedia.org/wiki/Linux_namespaces) 和 [cgroups](https://en.wikipedia.org/wiki/Cgroups) 实现的。namespaces 让你为每个容器虚拟化系统资源（比如文件系统或网络）；而 cgroups 则提供限制容器可用资源（如 CPU、内存）的方法。在核心层面，低层容器运行时负责为容器设置这些 namespaces 和 cgroups，然后在这些隔离的命名空间与控制组中运行命令。大多数容器运行时会实现更多功能，但上面描述的是最基本的部分。

一定要看 Liz Rice 的精彩演讲「[用 Go 从零构建一个容器](https://www.youtube.com/watch?v=Utf-A4rODH8)」。她的演讲是理解低层容器运行时如何实现的绝佳入门。Liz 在演讲中走过了许多步骤，但你能想象出的最简单的、仍然可以称为“容器运行时”的实现，可能只做像下面这样的事情：

- 创建 cgroup
- 在 cgroup 中运行命令
- 使用 [unshare](http://man7.org/linux/man-pages/man2/unshare.2.html) 切换到独立的 namespaces
- 命令执行完毕后清理 cgroup（当没有进程引用某个 namespace 时，该 namespace 会自动被删除）

不过，一个健壮的低层容器运行时会做更多事情，例如允许在 cgroup 上设置资源限制、搭建根文件系统，并将容器进程 chroot 到该根文件系统。

## 构建示例运行时

我们来演示如何运行一个简单的临时运行时来创建一个容器。我们可以使用标准的 Linux 命令：
`cgcreate`、`cgset`、`cgexec`、`chroot` 和 `unshare` 来完成这些步骤。下面大多数命令需要以 root 身份运行。

首先为容器准备一个根文件系统。这里我们使用 BusyBox Docker 镜像作为基础。创建一个临时目录并把 BusyBox 导出到该目录。大部分命令需要 root 权限。

```shell
CID=$(docker create busybox)
ROOTFS=$(mktemp -d)
docker export $CID | tar -xf - -C $ROOTFS
```

接着创建 cgroup 并对内存和 CPU 做限制。内存限制以字节为单位，这里我们把限制设置为 100MB。

```shell
UUID=$(uuidgen)
cgcreate -g cpu,memory:$UUID
cgset -r memory.limit_in_bytes=100000000 $UUID
cgset -r cpu.shares=512 $UUID
```

CPU 使用限制有两种常见方式。这里我们使用 CPU “shares” 来设置限制。shares 表示相对于同一时间运行的其他进程的 CPU 时间权重。单独运行时，容器可以使用整个 CPU；但如果有其他容器同时运行，它们将根据各自的 shares 按比例分配 CPU。

基于 CPU 核心的硬性限制要复杂一点。它允许你对容器可使用的 CPU 核心数设置硬性上限。限制 CPU 核心需要在 cgroup 上设置两个选项：`cfs_period_us` 和 `cfs_quota_us`。`cfs_period_us` 指定多长时间检查一次 CPU 使用，`cfs_quota_us` 指定在一个 period 内一个任务在单核上可运行的时间，两者单位都是微秒（microseconds）。

例如，如果我们想把容器限制为使用 2 个核，可以将 period 设为 1 秒，quota 设为 2 秒（1 秒 = 1,000,000 微秒），这样在一个 1 秒的周期内，进程实际上可以使用等同于 2 个核心的时间。[这篇文章](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/6/html/resource_management_guide/sec-cpu) 对这个概念有详细解释。

```shell
cgset -r cpu.cfs_period_us=1000000 $UUID
cgset -r cpu.cfs_quota_us=2000000 $UUID
```

接下来我们在容器中执行命令。下面的命令会在我们创建的 cgroup 中执行命令，unshare 指定的命名空间，设置主机名，并 chroot 到我们的文件系统。

```shell
$ cgexec -g cpu,memory:$UUID \
>     unshare -uinpUrf --mount-proc \
>     sh -c "/bin/hostname $UUID && chroot $ROOTFS /bin/sh"
/ # echo "Hello from in a container"
Hello from in a container
/ # exit
```

最后，在命令执行完成后，我们可以删除创建的 cgroup 和临时目录进行清理。

```shell
cgdelete -r -g cpu,memory:$UUID
rm -r $ROOTFS
```

为了进一步演示这个流程，我写了一个用 bash 实现的简单运行时项目 [execc](https://github.com/ianlewis/execc)。它支持挂载、用户、PID、IPC、UTS 和网络命名空间；设置内存限制；按核数设置 CPU 限制；挂载 proc 文件系统；并在独立的根文件系统中运行容器。

## 低层容器运行时示例

为了更好地理解低层容器运行时，看看一些实例很有帮助。不同的运行时实现了不同的功能，并强调容器化的不同方面。

### lmctfy

虽然并未广泛使用，但值得一提的低层运行时是 [`lmctfy`](https://github.com/google/lmctfy)。`lmctfy` 是 Google 的一个项目，基于 Borg（Google 内部使用的调度/容器系统）使用的内部运行时。它一个很有趣的特性是支持基于容器名称的 cgroup 层级结构。例如，根容器名为 `busybox`，它可以创建名为 `busybox/sub1` 或 `busybox/sub2` 的子容器，名称形成一种路径结构。因此每个子容器可以有自己的 cgroup，而这些 cgroup 又受父容器 cgroup 的限制。这种设计受 Borg 启发，使得 `lmctfy` 中的容器能够在服务器上按预先分配的资源运行子任务容器，从而比运行时本身更容易实现严格的 SLO（服务级别目标）。

尽管 `lmctfy` 提供了一些有趣的特性和想法，但其他运行时更易于使用，因此 Google 最终决定社区应更多地把精力放在 Docker 的 `libcontainer`（后来发展为 runc）上，而不是继续把重点放在 `lmctfy`。

### runc

`runc` 目前是最广泛使用的容器运行时。它最初作为 Docker 的一部分开发，后来被拆分为独立的工具与库。

内部上，`runc` 运行容器的方式与上文描述的类似，但 `runc` 实现了 OCI runtime 规范。这意味着它会从特定的 “OCI bundle” 格式运行容器。bundle 格式包含一个 `config.json` 用于配置，以及一个容器的根文件系统。你可以在 GitHub 上阅读 [OCI runtime spec](https://github.com/opencontainers/runtime-spec) 了解更多。你也可以在 [runc 的 GitHub 项目](https://github.com/opencontainers/runc) 上学习如何安装 runc。

先创建根文件系统。此处我们再一次使用 BusyBox。

```shell
mkdir rootfs
docker export $(docker create busybox) | tar -xf - -C rootfs
```

接着创建一个 `config.json` 文件。

```shell
runc spec
```

该命令会为容器生成一个模板 `config.json`，看起来类似于：

```shell
$ cat config.json
{
    "ociVersion": "1.0.0",
    "process": {
        "terminal": true,
        "user": {
            "uid": 0,
            "gid": 0
        },
        "args": [
            "sh"
        ],
        "env": [
            "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
            "TERM=xterm"
        ],
        "cwd": "/",
        "capabilities": {
...
```

默认情况下，它会在 `./rootfs` 的根文件系统中运行 `sh`。既然这正是我们想要的设置，就可以直接运行容器了。

```shell
$ sudo runc run mycontainerid
/ # echo "Hello from in a container"
Hello from in a container
```

## rkt

`rkt` 是 CoreOS 开发的一个流行替代方案（相对于 Docker/runc）。`rkt` 有点难以归类，因为它既提供了像 `runc` 那样的低层功能，也提供一些典型的高层运行时特性。这里我将侧重描述 `rkt` 的低层特性，把高层特性留到下一篇讨论。

`rkt` 最初使用 [Application Container (`appc`)](https://coreos.com/rkt/docs/latest/app-container.html) 标准，`appc` 是作为对 Docker 容器格式的一个开源替代标准提出的。Appc 从未被广泛采用为主流镜像格式，且现在已不再积极维护，但它确实达到过为社区提供开放标准的目标。`rkt` 将在未来使用 OCI 容器格式替代 `appc`。

Application Container Image (ACI) 是 Appc 的镜像格式。镜像是一个 tar.gz，里面包含一个 manifest 目录和 rootfs 目录作为根文件系统。你可以在 [`appc` 的 GitHub 仓库](https://github.com/appc/spec/blob/master/spec/aci.md) 中阅读关于 ACI 的更多信息。

你可以使用 `acbuild` 工具来构建容器镜像。`acbuild` 可以写进 shell 脚本，类似于 Dockerfile 的使用方式。

```shell
acbuild begin
acbuild set-name example.com/hello
acbuild dep add quay.io/coreos/alpine-sh
acbuild copy hello /bin/hello
acbuild set-exec /bin/hello
acbuild port add www tcp 5000
acbuild label add version 0.0.1
acbuild label add arch amd64
acbuild label add os linux
acbuild annotation add authors "Carly Container <carly@example.com>"
acbuild write hello-0.0.1-linux-amd64.aci
acbuild end
```

## 再见！

希望这篇文章能帮助你了解低层容器运行时是什么。尽管大多数容器用户会使用更高层的运行时，但知道容器在底层如何工作对于排查问题和调试是非常有用的。

下一篇我将上移栈层，讨论**高层容器运行时**：我会讲它们提供了哪些功能、为什么更适合想使用容器的应用开发者，以及谈谈像 Docker 和 rkt 在高层方面的功能。记得订阅我的 RSS 或 [关注我的 Twitter](https://twitter.com/IanMLewis)，以便在下一篇文章发布时收到通知。

> 更新：请继续阅读 [容器运行时 第3部分：高层运行时（Container Runtimes Part 3: High-Level Runtimes）](https://www.ianlewis.org/en/container-runtimes-part-3-high-level-runtimes)

在此之前，你可以通过下列方式更多参与 Kubernetes 社区：

- 在 [Stack Overflow](http://stackoverflow.com/questions/tagged/kubernetes) 提问或回答问题
- 在 Twitter 上关注 [@Kubernetesio](https://twitter.com/kubernetesio)
- 加入 Kubernetes [Slack](http://slack.k8s.io/) 并与我们聊天。（我是 ianlewis，来打个招呼吧！）
- 在 [GitHub](https://github.com/kubernetes/kubernetes) 为 Kubernetes 项目贡献代码

> _感谢 [Craig Box](https://twitter.com/craigbox)、Jack Wilbur、Philip Mallory、[David Gageot](https://twitter.com/dgageot)、Jonathan MacMillan 和 [Maya Kaczorowski](https://twitter.com/MayaKaczorowski) 对本文草稿的审阅。_

# Kubernetes Pods 到底是什么？

最近我看到一条来自很酷的 Amy Codes（真心希望那是她真名）的推文，内容关于 Kubernetes 的 Pod：

> 你知道为什么 pod 里的容器总是一起调度吗？因为它们是嵌套容器。
>
> 头脑炸裂。
>
> — Amy Codes (@TheAmyCode) [2017 年 8 月 21 日](https://twitter.com/TheAmyCode/status/899462049184350208)

虽然这条推文并不是百分之百准确（容器并不是真正的“事物”。稍后我们会讲到），但它指出了一个事实：Pod 非常妙。值得来看看 Pod 和容器到底是什么。

[Kubernetes 文档中关于 Pod 的章节](https://kubernetes.io/docs/concepts/workloads/pods/pod/) 对 Pod 有最完整、最权威的解释，但写得比较通用、术语也多。我仍然建议你去读那篇文档——它比我写的更准确。这篇文章希望能做一个更接地气的补充说明。

## 容器并不是真正的“容器”

很多人已经知道这一点，但 Linux 的 “容器” 并不是真正存在的独立实体。Linux 中没有一个叫做 “container” 的事物。大家所说的容器，其实就是正常进程，它们借助 Linux 内核的两个特性来运行：命名空间（namespaces）和控制组（cgroups）。命名空间允许你给进程一个“视图”，将视图之外的一切隐藏起来，从而为进程提供一个独立的运行环境。这样进程就无法看到或干扰其他进程。

命名空间包括：

- 主机名（Hostname）
- 进程 ID（Process IDs）
- 文件系统（File System）
- 网络接口（Network interfaces）
- 进程间通信（IPC）

虽然我上面说运行在命名空间里的进程不能干扰其他进程，但这并不完全正确。进程仍能使用宿主机上的所有资源，从而可能会耗尽资源、使其他进程饥饿。为限制这种行为，Linux 有一个叫做 cgroups（控制组）的特性。进程可以被放到一个 cgroup 中（类似命名空间的概念），但 cgroup 会限制该进程可以使用的资源。这些资源包括 CPU、内存、块设备 I/O、网络 I/O 等。CPU 通常以 milli-core（千分之一核）为单位限制，内存以字节为单位限制。如果进程超出 cgroup 设置的内存限制，就会遇到 OOM（内存不足）错误；CPU 使用则只限于 cgroup 允许的额度。

关于命名空间和控制组的学习资料很多，只需 Google 一下就能找到，这里推荐两篇不错的文章 / 视频：

- [What even is a container: namespaces and cgroups](https://jvns.ca/blog/2016/10/10/what-even-is-a-container/)
- [Cgroups, namespaces, and beyond: what are containers made from?](https://www.youtube.com/watch?v=sK5i-N34im8)

我想强调的是，cgroups 和每种命名空间类型是**独立的特性**。上面列出的一些命名空间可以选择性地使用，也可以完全不使用。你可以只使用 cgroups，或者采用两者的某种组合（严格来说底层仍在用命名空间和 cgroups，只是使用的是 root 命名空间而已）。命名空间和 cgroups 也可以作用于一**组**进程：你可以让多个进程共享同一个命名空间，这样它们可以相互看到并交互；也可以把它们放在同一个 cgroup 中，使得这组进程共同受限于特定的 CPU / 内存配额。

## 组合的组合

当你使用 Docker 正常运行一个容器时，Docker 会为每个容器创建命名空间和 cgroups，使两者一一对应——这就是开发者通常所理解的“容器”。

<img class="align-center" src="containers.png">

这些容器本质上是独立的“筒仓”，除非你把卷（volume）或端口映射到宿主机，否则它们之间不会直接通信。

不过，通过一些额外的命令行参数，你可以让多个 Docker 容器共享同一套命名空间。下面先创建一个 nginx 容器：

```shell
$ cat <<EOF >> nginx.conf
> error_log stderr;
> events { worker_connections  1024; }
> http {
>     access_log /dev/stdout combined;
>     server {
>         listen 80 default_server;
>         server_name example.com www.example.com;
>         location / {
>             proxy_pass http://127.0.0.1:2368;
>         }
>     }
> }
> EOF
$ docker run -d --name nginx -v `pwd`/nginx.conf:/etc/nginx/nginx.conf -p 8080:80 nginx
```

接着我们运行一个包含 [ghost](https://github.com/TryGhost/Ghost) 的容器，但这次我们通过额外参数让它加入到 nginx 容器的命名空间中：

```shell
docker run -d --name ghost --net=container:nginx --ipc=container:nginx --pid=container:nginx ghost
```

现在我们的 nginx 可以直接通过 localhost 将请求代理到 ghost 容器。如果访问 `http://localhost:8080/`，你应该能看到通过 nginx 代理的 ghost。上面的命令创建了一组运行在同一套命名空间里的容器，这些命名空间允许 Docker 容器互相发现并通信。

<img class="align-center" src="ghost_.png">

## Pod（在某种意义上）就是容器

既然我们已经看到可以把多个进程组合到同一套命名空间和 cgroup 中，那么这正是 Kubernetes Pod 的本质。Pod 允许你声明想要运行的容器，Kubernetes 会自动以正确的方式为它们设置命名空间和 cgroup。实际情况比这复杂一点，因为 Kubernetes 不使用 Docker 的网络实现（它使用 [CNI](https://github.com/containernetworking/cni)），但大体思想就是这样。

<img class="align-center" src="pods.png">

当容器以这种方式被组织后，每个进程“感觉”自己在同一台机器上运行。它们可以通过 localhost 互相通信、使用共享卷，甚至可以通过 IPC 或发送信号（如 HUP、TERM）来相互交互（在启用共享 PID 命名空间的 Kubernetes 1.7 与 Docker >=1.13 的组合中可以实现）。

现在假设你想运行 nginx，并让 [confd](https://github.com/kelseyhightower/confd) 监视 etcd，当后端应用服务器的列表变化时由 confd 更新 nginx 配置并重启 nginx。比如你的 etcd 存着后端应用的 IP 地址列表，当列表变化时 confd 会收到通知、写出新的 nginx 配置并给 nginx 发送 HUP，促使 nginx 重载配置。

<img class="align-center" src="nginx.png">

用 Docker 来做这件事的传统方法是把 nginx 和 confd 放到同一个容器里。因为 Docker 容器只有一个 entrypoint，你需要用像 supervisord 之类的进程管理程序来保持两个进程都在运行。这并不理想，因为你需要为每个 nginx 副本运行一个 supervisord。更重要的是，Docker 只“知道” supervisord，因为那是 entrypoint。Docker 无法看到各个子进程的状态，这意味着你或其他工具无法通过 Docker API 得到这些进程的详细信息。比如 nginx 可能崩溃了，但 Docker 可能并不知道。

<img class="align-center" src="supervisord.png">

而使用 Pod，Kubernetes 会管理每个进程，因此可以洞察它们的状态。它可以通过 API 向用户报告状态信息，并能在进程崩溃时重启它，或提供自动化日志等服务。

<img class="align-center" src="kubernetes.png">

## Pod 作为一种容器的“API”

通过以这种方式将容器组合到 Pod，我们可以把可以被加入 Pod 的容器当成一种“API”供他人使用。这里的“API”并不是指 Web API，而是一种抽象，其他 Pod 可以重用它。

举例来说，回到 nginx + confd 的例子。confd 本身并不需要知道 nginx 的实现细节。它只知道需要在 etcd 中监视某个值，然后对某个进程发送 HUP 或执行一个命令。它运行的目标程序不必一定是 nginx，可以是任何类型的应用。这样，你可以把 confd 这个容器镜像和它的配置拿来在许多不同类型的 Pod 中复用。能够这样做的 Pod 通常被称为 “sidecar containers”（侧车容器），名字来自摩托车旁边的侧车（sidecar）。

你还可以想象其他类型的抽象。像 [Istio](https://istio.io/) 这样的服务网格可以作为 sidecar 附加进来，提供服务路由、遥测、策略执行等功能，而无需改动主应用。

你也可以同时使用多个 sidecar。没有什么阻止你同时使用 confd sidecar 和 istio sidecar。通过这种方式可以把应用组合起来，构建更复杂、更可靠的系统，同时保持每个单独应用的相对简单。

希望这能让你对 Pod 有一个清晰的理解，并明白为什么它们会成为未来部署容器时的关键组成部分。如果你想了解更多 Kubernetes，可以加入 [Kubernetes Slack](http://slack.kubernetes.io/)。那里汇集了优秀的 Kubernetes 开发者，讨论各种 Kubernetes 相关话题。建议关注 `#kubernetes-users` 频道进行一般讨论。

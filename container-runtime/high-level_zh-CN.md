# 容器运行时 第3部分：高层运行时

> _这是关于容器运行时四部分系列的第三篇。自从[第1部分](https://www.ianlewis.org/en/container-runtimes-part-1-introduction-container-r)发布已经有一段时间了，第一篇对容器运行时做了概览并讨论了低层与高层运行时的差异。在[第2部分](https://www.ianlewis.org/en/container-runtimes-part-2-anatomy-low-level-contai)我详细介绍了低层容器运行时并构建了一个简单的低层运行时。_

高层运行时位于栈的更高层。低层运行时负责实际运行容器的机制，而高层运行时负责容器镜像的传输与管理、解包镜像，并将运行交给低层运行时去执行。通常，高层运行时提供一个守护进程（daemon）和一个 API，供远端应用程序以逻辑方式运行并监控容器；但它们在实际工作上会坐在低层运行时之上并将具体工作委托给低层运行时或其它高层运行时。

高层运行时也可能提供一些听起来像“低层”的功能，但这些功能是跨机上多个容器共享使用的。例如，一项功能可能是网络命名空间的管理，并允许容器加入另一个容器的网络命名空间。

下面是一张概念图，帮助理解各组件如何组合在一起：

<img src="runtime-architecture.png" alt="Runtime architecture diagram" class="align-center" />

## 高层运行时示例

为了更好地理解高层运行时，看看几个例子会有帮助。与低层运行时一样，每个高层运行时实现的功能有所不同。

### Docker

Docker 是最早期的开源容器运行时之一。它由平台即服务公司 dotCloud 开发，用来在容器中运行其用户的 Web 应用。

Docker 是一个集镜像构建、打包、共享与运行于一体的容器运行时。Docker 采用客户端/服务器架构，最初以单体守护进程 `dockerd` 和 `docker` 客户端形式存在。守护进程提供了构建镜像、管理镜像和运行容器的大部分逻辑，并暴露了 API。命令行客户端可以向守护进程发送命令并获取信息。

它是首个将容器构建与运行生命周期所需功能整合起来的流行运行时。

最初 Docker 同时实现了高层和低层的运行时功能，但这些部分后来被拆分成独立项目：`runc`（低层）和 `containerd`（高层）。现在 Docker 由 `dockerd` 守护进程组成，以及打包后的 `docker-containerd` 和 `docker-runc`。`docker-containerd` 与 `docker-runc` 实际上是 Docker 打包的标准 `containerd` 和 `runc` 版本。

<img src="docker.png" alt="Docker architecture diagram" class="align-center" />

`dockerd` 提供诸如构建镜像等功能，`dockerd` 使用 `docker-containerd` 来提供镜像管理和运行容器等功能。例如，Docker 的构建步骤实际上是一些解释 Dockerfile 的逻辑，它在一个临时容器中运行必要的命令（通过 `containerd`），然后将得到的容器文件系统保存成镜像。

### containerd

[containerd](https://containerd.io/) 是从 Docker 中拆分出来的高层运行时。就像 `runc` 被拆出来作为低层运行时组件一样，`containerd` 被拆出来作为 Docker 的高层运行时组件。`containerd` 实现了下载镜像、管理镜像以及从镜像运行容器等功能。当需要运行容器时，它会把镜像解包成 OCI 运行时 bundle，然后调用 `runc` 来实际运行容器。

containerd 还提供 API 和客户端工具用于与之交互。containerd 的命令行客户端是 `ctr`。

你可以用 `ctr` 命令让 `containerd` 拉取一个镜像：

```shell
sudo ctr images pull docker.io/library/redis:latest
```

列出本地镜像：

```shell
sudo ctr images list
```

基于镜像创建容器：

```shell
sudo ctr container create docker.io/library/redis:latest redis
```

列出运行中的容器：

```shell
sudo ctr container list
```

停止（删除）容器：

```shell
sudo ctr container delete redis
```

这些命令与用户使用 Docker 的交互方式类似。但与 Docker 不同，containerd 专注于运行容器，因此它并不提供构建容器的机制。Docker 更关注最终用户和开发者用例，而 containerd 更关注运维场景，例如在服务器上运行容器。像构建镜像这种任务则留给其它工具来完成。

## rkt

在上一篇中我提到过，`rkt` 是一个同时具有低层与高层特性的运行时。例如，像 Docker 一样，rkt 允许你构建镜像、在本地仓库中获取和管理镜像，并通过单一命令运行它们。不过 rkt 与 Docker 的差别在于它不提供长期运行的守护进程和远程 API。

你可以拉取远端镜像：

```shell
sudo rkt fetch coreos.com/etcd:v3.3.10
```

然后列出本地安装的镜像：

```shell
$ sudo rkt image list
ID                      NAME                                    SIZE    IMPORT TIME     LAST USED
sha512-07738c36c639     coreos.com/rkt/stage1-fly:1.30.0        44MiB   2 minutes ago   2 minutes ago
sha512-51ea8f513d06     coreos.com/oem-gce:1855.5.0             591MiB  2 minutes ago   2 minutes ago
sha512-2ba519594e47     coreos.com/etcd:v3.3.10                 69MiB   25 seconds ago  24 seconds ago
```

并删除镜像：

```shell
$ sudo rkt image rm coreos.com/etcd:v3.3.10
successfully removed aci for image: "sha512-2ba519594e4783330ae14e7691caabfb839b5f57c0384310a7ad5fa2966d85e3"
rm: 1 image(s) successfully removed
```

虽然 rkt 现在看起来不再被非常积极地维护，但它仍然是一个有趣的工具，并且是容器技术历史上的重要一环。

## 展望

在下一篇文章中，我将从 Kubernetes 的视角讨论运行时以及它们的工作方式。记得订阅[我的 RSS 订阅](https://www.ianlewis.org/feed/enfeed)或在 Twitter 上关注我，以便在下一篇博客发布时收到通知。

> 更新：请继续阅读 [容器运行时 第4部分：Kubernetes 容器运行时与 CRI（Container Runtimes Part 4: Kubernetes Container Runtimes & CRI）](https://www.ianlewis.org/en/container-runtimes-part-4-kubernetes-container-run)

在此之前，你可以通过以下渠道更多参与 Kubernetes 社区：

- 在 [Stack Overflow](http://stackoverflow.com/questions/tagged/kubernetes) 提问或回答问题
- 在 Twitter 上关注 [@Kubernetesio](https://twitter.com/kubernetesio)
- 加入 Kubernetes [Slack](http://slack.k8s.io/) 并与我们聊天。（我是 ianlewis，来打个招呼吧！）
- 在 [GitHub](https://github.com/kubernetes/kubernetes) 为 Kubernetes 项目贡献代码

如果你对博客主题有任何建议或想法，可以通过 Twitter 私信或回复给我（[@IanMLewis](https://twitter.com/IanMLewis)）。谢谢！

_> 感谢 [Craig Box](https://twitter.com/craigbox)、[Marcus Johansson](https://twitter.com/marcjoha)、Steve Perry 和 Nicolas Lacasse 对本文草稿的审阅。_

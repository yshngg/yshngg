# 容器运行时 第4部分：Kubernetes 容器运行时与 CRI

_这是关于容器运行时四部分系列的第四篇也是最后一篇。自从[第1部分](https://www.ianlewis.org/en/container-runtimes-part-1-introduction-container-r)发布已经有一段时间了，第一篇对容器运行时做了概览并讨论了低层与高层运行时的差异。在[第2部分](https://www.ianlewis.org/en/container-runtimes-part-2-anatomy-low-level-contai)我详细介绍了低层容器运行时并构建了一个简单的低层运行时。在[第3部分](https://www.ianlewis.org/en/container-runtimes-part-3-high-level-runtimes)我上移了栈并讨论了高层容器运行时。_

Kubernetes 运行时是支持 [Container Runtime Interface（CRI）](https://github.com/kubernetes/community/blob/master/contributors/devel/sig-node/container-runtime-interface.md) 的高层容器运行时。CRI 在 Kubernetes 1.5 中引入，充当了 [kubelet](https://kubernetes.io/docs/concepts/overview/components/#kubelet) 与容器运行时之间的桥梁。想要与 Kubernetes 集成的高层容器运行时需要实现 CRI。按照我们在第3部分的定义，Kubernetes 运行时需要管理镜像，支持 [Kubernetes pods](https://www.ianlewis.org/en/what-are-kubernetes-pods-anyway)，并管理各个容器，因此 **必须** 是高层运行时——底层运行时不具备这些必要功能。第3部分已经详细解释了高层容器运行时的内容，这篇文章我会侧重于 CRI 并介绍一些支持 CRI 的运行时。

要更好地理解 CRI，先看看 Kubernetes 的整体架构是很有帮助的。kubelet 是运行在每个工作节点上的代理，负责管理该节点上的容器工作负载。实际运行工作负载时，kubelet 使用 CRI 与运行在同一节点上的容器运行时通信。通过这种方式，CRI 本质上是一个抽象层或 API，使得你可以替换不同的容器运行时实现，而不用把它们内嵌在 kubelet 中。

<img src="CRI.png" alt="Kubernetes architecture diagram" class="align-center" />

## 支持 CRI 的运行时示例

下面列出一些可以与 Kubernetes 一起使用的 CRI 运行时。

### containerd

[containerd](https://containerd.io/) 是第3部分提到的高层运行时。`containerd` 可能是目前最流行的 CRI 运行时。它通过一个 [插件](https://github.com/containerd/cri) 实现 CRI，默认启用。它默认监听一个 Unix socket，因此你可以这么配置 `crictl` 去连接 containerd：

```shell
cat <<EOF | sudo tee /etc/crictl.yaml
runtime-endpoint: unix:///run/containerd/containerd.sock
EOF
```

containerd 的一个有趣点是，从 1.2 版本开始它通过所谓的 “runtime handler” 支持多个底层运行时。runtime handler 在 CRI 的字段中传递，根据该 handler，`containerd` 会运行一个叫做 shim 的应用来启动容器。借助这个机制，可以使用除了 runc 之外的低层运行时来运行容器，例如 [gVisor](https://github.com/google/gvisor)、[Kata Containers](https://katacontainers.io/) 或 [Nabla Containers](https://nabla-containers.github.io/)。在 Kubernetes API 中，runtime handler 通过 [`RuntimeClass` 对象](https://kubernetes.io/docs/concepts/containers/runtime-class/) 对外暴露（在 Kubernetes 1.12 时处于 alpha 状态）。关于 shim 概念的更多内容可以参考 [PR #2434](https://github.com/containerd/containerd/pull/2434)。

### Docker

对 CRI 的支持最早是为 Docker 开发的，当时以 kubelet 与 Docker 之间的 shim 形式实现。后来 Docker 将许多功能拆分到 `containerd`，现在通过 `containerd` 支持 CRI。在现代版本的 Docker 中，安装 Docker 会一并安装 `containerd`，CRI 直接与 `containerd` 通信。因此，Docker 本身并非必须用于支持 CRI——你可以根据需要直接安装 `containerd` 或通过 Docker 安装。

### cri-o

`cri-o` 是为 Kubernetes 专门设计的轻量级 CRI 运行时，属于高层运行时。它支持管理[与 OCI 兼容的镜像](https://github.com/opencontainers/image-spec)，并能从任何 OCI 兼容的镜像仓库拉取镜像。它支持 `runc` 和 Clear Containers 作为底层运行时。理论上它也支持其他与 OCI 兼容的低层运行时，但它依赖与 `runc` 的[OCI 命令行接口](https://github.com/opencontainers/runtime-tools/blob/master/docs/command-line-interface.md) 的兼容性，因此实际上在灵活性上不如 `containerd` 的 shim API。

`cri-o` 的默认 endpoint 位于 `/var/run/crio/crio.sock`，因此可以像下面这样配置 `crictl`：

```shell
cat <<EOF | sudo tee /etc/crictl.yaml
runtime-endpoint: unix:///var/run/crio/crio.sock
EOF
```

## CRI 规范

CRI 是基于 [protocol buffers](https://developers.google.com/protocol-buffers/) 和 [gRPC](https://grpc.io/) 的 API。规范以一个 [protobuf 文件](https://github.com/kubernetes/kubernetes/blob/master/staging/src/k8s.io/cri-api/pkg/apis/runtime/v1alpha2/api.proto) 的形式定义在 Kubernetes 仓库中（位于 kubelet 相关目录）。CRI 定义了若干远程过程调用（RPC）和消息类型，这些 RPC 用于诸如“拉取镜像”（`ImageService.PullImage`）、“创建 Pod”（`RuntimeService.RunPodSandbox`）、“创建容器”（`RuntimeService.CreateContainer`）、“启动容器”（`RuntimeService.StartContainer`）、“停止容器”（`RuntimeService.StopContainer`）等操作。

例如，启动一个新的 Kubernetes Pod 的典型 CRI 交互大致如下（用我自己简化的伪 gRPC 表示；每个 RPC 实际上会有更大的请求对象，这里为简洁起见做了简化）。`RunPodSandbox` 和 `CreateContainer` RPC 在响应中返回 ID，后续请求会使用这些 ID：

```text
ImageService.PullImage({image: "image1"})
ImageService.PullImage({image: "image2"})
podID = RuntimeService.RunPodSandbox({name: "mypod"})
id1 = RuntimeService.CreateContainer({
  pod: podID,
  name: "container1",
  image: "image1",
})
id2 = RuntimeService.CreateContainer({
  pod: podID,
  name: "container2",
  image: "image2",
})
RuntimeService.StartContainer({id: id1})
RuntimeService.StartContainer({id: id2})
```

我们可以使用 [`crictl`](https://github.com/kubernetes-sigs/cri-tools) 工具直接与 CRI 运行时交互。`crictl` 允许我们从命令行向 CRI 运行时发送 gRPC 消息，用于调试和测试 CRI 实现，而无需启动完整的 `kubelet` 或 Kubernetes 集群。你可以从 cri-tools 的 GitHub [releases 页面](https://github.com/kubernetes-sigs/cri-tools/releases) 下载 `crictl` 二进制。

你可以在 `/etc/crictl.yaml` 下创建配置文件来配置 `crictl`，在该文件中指定运行时的 gRPC endpoint（可以是 Unix socket：`unix:///path/to/file`，或 TCP：`tcp://<host>:<port>`）。下面示例使用 containerd：

```shell
cat <<EOF | sudo tee /etc/crictl.yaml
runtime-endpoint: unix:///run/containerd/containerd.sock
EOF
```

或者你也可以在每次命令行调用时指定运行时 endpoint：

```shell
crictl --runtime-endpoint unix:///run/containerd/containerd.sock …
```

下面用 `crictl` 运行一个包含单个容器的 Pod。首先告诉运行时拉取 `nginx` 镜像（因为不能在本地没有镜像的情况下启动容器）：

```shell
sudo crictl pull nginx
```

接着创建一个 Pod 创建请求，作为 JSON 文件保存：

```shell
cat <<EOF | tee sandbox.json
{
    "metadata": {
        "name": "nginx-sandbox",
        "namespace": "default",
        "attempt": 1,
        "uid": "hdishd83djaidwnduwk28bcsb"
    },
    "linux": {
    },
    "log_directory": "/tmp"
}
EOF
```

然后创建 pod sandbox，并把 sandbox 的 ID 存为 `SANDBOX_ID`：

```shell
SANDBOX_ID=$(sudo crictl runp --runtime runsc sandbox.json)
```

接下来我们创建一个容器创建请求，写入 JSON 文件：

```shell
cat <<EOF | tee container.json
{
  "metadata": {
      "name": "nginx"
    },
  "image":{
      "image": "nginx"
    },
  "log_path":"nginx.0.log",
  "linux": {
  }
}
EOF
```

然后在之前创建的 Pod 中创建并启动容器：

```shell
{
  CONTAINER_ID=$(sudo crictl create ${SANDBOX_ID} container.json sandbox.json)
  sudo crictl start ${CONTAINER_ID}
}
```

你可以查看正在运行的 Pod：

```shell
sudo crictl inspectp ${SANDBOX_ID}
```

以及正在运行的容器：

```shell
sudo crictl inspect ${CONTAINER_ID}
```

清理步骤：先停止并删除容器：

```shell
{
  sudo crictl stop ${CONTAINER_ID}
  sudo crictl rm ${CONTAINER_ID}
}
```

然后停止并删除 Pod（原文命令中为 `stopp` 与 `rmp`，实际应为 `stopp` → `stopp` 可能为笔误，请使用 `stop`/`rm` 或 `stopp`/`rmp` 所在的正确 `crictl` 命令）：

```shell
{
  sudo crictl stopp ${SANDBOX_ID}
  sudo crictl rmp ${SANDBOX_ID}
}
```

> 注：上面最后两条命令在原文里有拼写（或示例）错误。实际使用时请参考你安装的 `crictl` 版本文档，正确的清理命令通常是 `crictl stopp` / `crictl rmp`（某些实现可能是 `crictl stopp` 与 `crictl rmp`，也可能是 `crictl stop` 与 `crictl rmp` 或其它子命令，请以你的 `crictl --help` 输出为准）。

## 感谢关注本系列

这是容器运行时系列的最后一篇，但别担心——未来还会有更多关于容器和 Kubernetes 的文章。记得添加[我的 RSS 订阅](https://www.ianlewis.org/feed/enfeed)或在 Twitter 上关注我，以便在下一篇博客发布时收到通知。

在此期间，你也可以通过以下渠道更多参与 Kubernetes 社区：

- 在 [Stack Overflow](http://stackoverflow.com/questions/tagged/kubernetes) 提问或回答问题
- 在 Twitter 上关注 [@Kubernetesio](https://twitter.com/kubernetesio)
- 加入 Kubernetes [Slack](http://slack.k8s.io/) 并与我们聊天。（我是 ianlewis，来打个招呼吧！）
- 在 [GitHub](https://github.com/kubernetes/kubernetes) 为 Kubernetes 项目贡献代码

如果你对博客主题有任何建议或想法，可以通过 Twitter（[@IanMLewis](https://twitter.com/IanMLewis)）给我发回复或私信。谢谢！

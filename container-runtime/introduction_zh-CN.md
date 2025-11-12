# 容器运行时 第1部分：容器运行时简介

在处理容器时，你经常会听到一个术语——“**容器运行时（container runtime）**”。不同的人对“容器运行时”可能有不同的理解，所以这个术语令人困惑且模糊——即便在容器社区内部也常常如此。

本文是一个四篇系列的第一篇：

1. 第1部分：容器运行时简介——为什么它们这么令人困惑？
2. 第2部分：深入底层运行时
3. 第3部分：深入高层运行时
4. 第4部分：Kubernetes 运行时与 CRI

本文将解释什么是容器运行时以及为什么会有这么多困惑。之后我会深入不同类型的容器运行时、它们的功能以及彼此之间的区别。

![不确定这是指 Kubernetes 的容器运行时、底层容器运行时，还是电影片长](notsure.png){: .align-center }

传统上，程序员对“runtime（运行时）”的理解可能有两种：要么是程序运行时的生命周期阶段，要么是某种语言的具体实现（支持该语言执行的运行环境）。例如 Java HotSpot 运行时。后者的含义最接近“容器运行时”。**容器运行时负责运行容器时除实际运行程序之外的所有部分**。正如我们在本系列中会看到的，运行时会实现不同层次的功能，但只要能运行容器，就足以被称为“容器运行时”。

如果你对容器不是很熟悉，先看下面这些链接再回来阅读会有帮助：

- [什么是容器：namespaces 和 cgroups](https://jvns.ca/blog/2016/10/10/what-even-is-a-container/)
- [Cgroups、namespaces 以及更多：容器由什么组成？（视频）](https://www.youtube.com/watch?v=sK5i-N34im8)

## 为什么容器运行时这么令人困惑？

Docker 于 2013 年发布，解决了很多开发者在端到端运行容器时遇到的问题。Docker 拥有以下这些功能：

- 一种容器镜像格式
- 构建容器镜像的方法（`Dockerfile` / `docker build`）
- 管理容器镜像的方式（`docker images`、`docker rm <image>` 等）
- 管理容器实例的方式（`docker ps`、`docker rm <container>` 等）
- 共享容器镜像的方式（`docker push` / `docker pull`）
- 运行容器的方式（`docker run`）

当时 Docker 是一个单体系统（monolithic）。不过，这些功能彼此并非强耦合，每一项都可以用更小、更专注的工具实现并协同工作——只要这些工具使用共同的格式（即某种容器标准）。

因此，Docker、Google、CoreOS 及其它厂商创建了 [Open Container Initiative（OCI）](https://www.opencontainers.org/)。他们把用于“运行容器”的代码拆出来，做成名为 [`runc`](https://github.com/opencontainers/runc) 的工具和库，并将其捐赠给 OCI，作为 [OCI runtime 规范](https://github.com/opencontainers/runtime-spec) 的参考实现。

最初大家不太清楚 Docker 实际向 OCI 贡献了什么。事实是，他们贡献的是一种**“运行”容器的标准方式**，仅此而已。他们并没有把镜像格式或镜像推/拉的规范一起贡献进去。当你运行一个 Docker 容器时，Docker 实际上会执行以下步骤：

1. 下载镜像
2. 将镜像解包成一个“bundle”，把各层展平为单一文件系统
3. 从该 bundle 运行容器

Docker 标准化的只是第 3 步。在这一点澄清之前，很多人都以为“容器运行时”应该包含 Docker 提供的所有功能。后来 Docker 社区澄清了最初的 [规范](https://github.com/opencontainers/runtime-spec/commit/77d44b10d5df53ee63f0768cd0a29ef49bad56b6#diff-b84a8d65d8ed53f4794cd2db7e8ea731R45) 实际上只声明了“运行容器”这一部分属于运行时。这个认知上的断层直到今天仍然存在，使得“容器运行时”成为一个令人困惑的话题。我希望能在本文中展示双方观点各有道理，因此我会比较宽泛地使用“容器运行时”这一术语。

## 底层与高层容器运行时

当人们想起容器运行时时，可能会想到这样的例子：`runc`、`lxc`、`lmctfy`、Docker（`containerd`）、`rkt`、`cri-o`。这些项目为不同场景设计，实现的功能也不同。其中一些（比如 `containerd` 和 `cri-o`）实际上会借助 `runc` 来运行容器，但在其之上实现了镜像管理和 API。你可以把这些功能——包括镜像传输、镜像管理、镜像解包和 API——看作是**高层**功能，而把 `runc` 的实现看作**低层**实现。

由此可见，容器运行时生态相当复杂。不同运行时覆盖了从“低层”到“高层”光谱的不同部分。下面是一张非常主观的示意图：

![一张示意图，显示容器运行时在从“低层”到“高层”的光谱上的分布。lxc 和 runc 覆盖从低层到约中间的位置；lmctfy 更偏向中高层；CRI-O 从中间延伸到高层；containerd 稍高于 CRI-O；rkt 覆盖从低层到高层的大部分范围。](runtimes.png){: .align-center }

因此，实际专注于**仅运行容器**的实现通常被称为“**底层容器运行时（low-level container runtimes）**”。而那些支持更多高层功能（如镜像管理、gRPC/Web API）的实现，通常被称为“**高层容器工具**”或“**高层容器运行时（high-level container runtimes）**”，有时也简称为“容器运行时”。下文我会称它们为“高层容器运行时”。需要注意的是，底层运行时和高层运行时本质上是不同的，它们解决的是不同的问题。

容器是通过 [Linux namespaces](https://en.wikipedia.org/wiki/Linux_namespaces) 和 [cgroups](https://en.wikipedia.org/wiki/Cgroups) 实现的：

- namespaces 允许你为每个容器虚拟化系统资源（比如文件系统或网络）；
- cgroups 提供限制容器可用资源（如 CPU 和内存）的手段。

在最低层面，容器运行时负责为容器设置这些 namespaces 和 cgroups，然后在这些隔离环境里运行命令。底层运行时直接使用这些操作系统特性。

通常，想把应用放进容器运行的开发者需要的不仅仅是底层运行时提供的功能，他们还需要关于镜像格式、镜像管理和镜像共享的 API 与功能——这些是高层运行时提供的。底层运行时不够“日常使用”，因此真正会直接使用底层运行时的人，通常是那些实现高层运行时或构建容器工具的开发者。

实现底层运行时的开发者会说，像 `containerd` 和 `cri-o` 这样的高层运行时不是真正的运行时，因为它们把“运行容器”这个职责外包给了 `runc`。但从用户角度看，它们是提供“运行容器”能力的单一组件；底层实现可以替换掉，因此从用户角度仍然有理由把它们称为“运行时”。尽管 `containerd` 和 `cri-o` 都调用 `runc`，但它们是功能和目标都很不同的独立项目。

## 下次再见

希望这篇文章能帮助你理解什么是容器运行时以及为什么它们难以理解。如果你有疑问，欢迎在下方留言或在 [Twitter](https://twitter.com/IanMLewis) 上告诉我你觉得容器运行时最难理解的部分。

下一篇我将深入讲解**底层容器运行时**：具体介绍底层运行时做了什么，讨论流行的底层运行时如 `runc`、`rkt`，以及那些不太流行但很重要的比如 `lmctfy`，我还会演示如何实现一个简单的底层运行时。记得订阅我的 RSS 或 [关注我的 Twitter](https://twitter.com/IanMLewis)，以便在下一篇发布时收到通知。

> 更新：请继续阅读 [容器运行时 第2部分：底层容器运行时的结构（Container Runtimes Part 2: Anatomy of a Low-Level Container Runtime）](https://www.ianlewis.org/en/container-runtimes-part-2-anatomy-low-level-contai)

在此之前，你可以通过下列渠道更多地参与 Kubernetes 社区：

- 在 [Stack Overflow](http://stackoverflow.com/questions/tagged/kubernetes) 提问或回答问题
- 在 Twitter 上关注 [@Kubernetesio](https://twitter.com/kubernetesio)
- 加入 Kubernetes [Slack](http://slack.k8s.io/) 并与我们聊天。（我是 ianlewis，可以来打个招呼！）
- 在 [GitHub](https://github.com/kubernetes/kubernetes) 为 Kubernetes 项目贡献代码

> 感谢 [Sandeep Dinesh](https://twitter.com/SandeepDinesh)、[Mark Mandel](https://twitter.com/neurotic)、[Craig Box](https://twitter.com/craigbox)、[Maya Kaczorowski](https://twitter.com/mayakaczorowski) 和 Joe Burnett 对本文草稿的审阅。

---

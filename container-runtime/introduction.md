---
Title: "Container Runtimes Part 1: An Introduction to Container Runtimes"
Author: Ian Lewis
Date: 2017-12-06
Link: https://www.ianlewis.org/en/container-runtimes-part-1-introduction-container-r
---

<details open>

<summary>Additional languages</summary>

- [Simplified Chinese](introduction_zh-CN.md)

</details>

# Container Runtimes Part 1: An Introduction to Container Runtimes

One of the terms you hear a lot when dealing with containers is "container
runtime". "Container runtime" can have different meanings to different people so
it's no wonder that it's such a confusing and vaguely understood term, even
within the container community.

This post is the first in a series that will be in four parts:

1. Part 1: Intro to Container Runtimes: why are they so confusing?
2. Part 2: Deep Dive into Low-Level Runtimes
3. Part 3: Deep Dive into High-Level Runtimes
4. Part 4: Kubernetes Runtimes and the CRI

This post will explain what container runtimes are and why there is so much
confusion. I will then dive into different types of container runtimes, what
they do, and how they are different from each other.

![Not sure if this refers to Kubernetes container runtime, low-level container runtime, or movie runtime](notsure.png){: .align-center }

Traditionally, a computer programmer might know "runtime" as either the
lifecycle phase when a program is running, or the specific implementation of a
language that supports its execution. An example might be the Java HotSpot
runtime. This latter meaning is the closest to "container runtime". A container
runtime is responsible for all the parts of running a container that isn't
actually running the program itself. As we will see throughout this series,
runtimes implement varying levels of features, but running a container is
actually all that's required to call something a container runtime.

If you're not super familiar with containers, check out these links first and
come back:

- [What even is a container: namespaces and cgroups](https://jvns.ca/blog/2016/10/10/what-even-is-a-container/)
- [Cgroups, namespaces, and beyond: what are containers made from?](https://www.youtube.com/watch?v=sK5i-N34im8)

## Why are Container Runtimes so Confusing?

Docker was released in 2013 and solved many of the problems that developers had
running containers end-to-end. It had all these things:

- A container image format
- A method for building container images (`Dockerfile`/`docker build`)
- A way to manage container images (`docker images`, `docker rm <image>`, etc.)
- A way to manage instances of containers (`docker ps`, `docker rm <container>`,
  etc.)
- A way to share container images (`docker push`/`docker pull`)
- A way to run containers (`docker run`)

At the time, Docker was a monolithic system. However, none of these features
were really dependent on each other. Each of these could be implemented in
smaller and more focused tools that could be used together. Each of the tools
could work together by using a common format, a container standard.

Because of that, Docker, Google, CoreOS, and other vendors created the [Open
Container Initiative (OCI)](https://www.opencontainers.org/). They then broke
out their code for running containers as a tool and library called
[`runc`](https://github.com/opencontainers/runc) and donated it to OCI as a
reference implementation of the [OCI runtime
specification](https://github.com/opencontainers/runtime-spec).

It was initially confusing what Docker had contributed to OCI. What they
contributed was a standard way to "run" containers but nothing more. They didn't
include the image format or registry push/pull formats. When you run a Docker
container, these are the steps Docker actually goes through:

1. Download the image
2. Unpack the image into a "bundle". This flattens the layers into a single
   filesystem.
3. Run the container from the bundle

What Docker standardized was only #3. Until that was clarified, everyone had
thought of a container runtime as supporting all of the features Docker
supported. Eventually, Docker folks clarified that the [original
spec](https://github.com/opencontainers/runtime-spec/commit/77d44b10d5df53ee63f0768cd0a29ef49bad56b6#diff-b84a8d65d8ed53f4794cd2db7e8ea731R45)
stated that only the "running the container" part that made up the runtime. This
is a disconnect that continues even today, and makes "container runtimes" such a
confusing topic. I'll hopefully show that neither side is totally wrong and I'll
use the term pretty broadly in this blog post.

## Low-Level and High-Level Container Runtimes

When folks think of container runtimes, a list of examples might come to mind;
runc, lxc, lmctfy, Docker (containerd), rkt, cri-o. Each of these is built for
different situations and implements different features. Some, like containerd
and cri-o, actually use runc to run the container but implement image management
and APIs on top. You can think of these features -- which include image
transport, image management, image unpacking, and APIs -- as high-level features
as compared to runc's low-level implementation.

With that in mind you can see that the container runtime space is fairly
complicated. Each runtime covers different parts of this low-level to high-level
spectrum. Here is a very subjective diagram:

![A diagram of showing container runtimes on a spectrum from "low level" on the
left, to "high level" on the right. lxc and runc spans from "low level" to about
half way to "high level". lmctfy spans slightly farther right towards "high
level". CRI-O spans from mid-way to "high-level". containerd is slightly higher
level than CRI-O. rkt spans most of the way from "low level" to "high level".
](runtimes.png){: .align-center }

So for practical purposes, actual container runtimes that focus on just running
containers are usually referred to as "low-level container runtimes". Runtimes
that support more high-level features, like image management and gRPC/Web APIs,
are usually referred to as "high-level container tools", "high-level container
runtimes" or usually just "container runtimes". I'll refer to them as
"high-level container runtimes". It's important to note that low-level runtimes
and high-level runtimes are fundamentally different things that solve different
problems.

Containers are implemented using [Linux
namespaces](https://en.wikipedia.org/wiki/Linux_namespaces) and
[cgroups](https://en.wikipedia.org/wiki/Cgroups). Namespaces let you virtualize
system resources, like the file system or networking, for each container.
Cgroups provide a way to limit the amount of resources like CPU and memory that
each container can use. At the lowest level, container runtimes are responsible
for setting up these namespaces and cgroups for containers, and then running
commands inside those namespaces and cgroups. Low-level runtimes support using
these operating system features.

Typically, developers who want to run apps in containers will need more than
just the features that low-level runtimes provide. They need APIs and features
around image formats, image management, and sharing images. These features are
provided by high-level runtimes. Low-level runtimes just don't provide enough
features for this everyday use. For that reason the only folks that will
actually use low-level runtimes would be developers who implement higher level
runtimes, and tools for containers.

Developers who implement low-level runtimes will say that higher level runtimes
like containerd and cri-o are not actually container runtimes, as from their
perspective they outsource the implementation of running a container to runc.
But, from the user's perspective, they are a singular component that provides
the ability to run containers. One implementation can be swapped out for
another, so it still makes sense to call it a runtime from that perspective.
Even though containerd and cri-o both use runc, they are very different projects
that have very different feature support.

## Until Next Time

I hope that helped explain container runtimes and why they are so hard to
understand. Feel free to leave me comments below or [on
Twitter](https://twitter.com/IanMLewis) and let me know what about container
runtimes was hardest for you to understand.

In the next post I'll do a deep dive into low-level container runtimes. In that
post I'll talk about exactly what low-level container runtimes do. I'll talk
about popular low-level runtimes like runc and rkt, as well as
unpopular-but-important ones like lmctfy. I'll even walk through how to
implement a simple low-level runtime. Be sure to add my RSS feed or [follow me
on Twitter](https://twitter.com/IanMLewis) to get notified when the next blog
post comes out.

> Update: Please continue on and check out [Container Runtimes Part 2: Anatomy
> of a Low-Level Container
> Runtime](https://www.ianlewis.org/en/container-runtimes-part-2-anatomy-low-level-contai)

Until then, you can get more involved with the Kubernetes community via these
channels:

- Post and answer questions on [Stack
  Overflow](http://stackoverflow.com/questions/tagged/kubernetes)
- Follow [@Kubernetesio](https://twitter.com/kubernetesio) on Twitter
- Join the Kubernetes [Slack](http://slack.k8s.io/) and chat with us. (I'm
  ianlewis so say Hi!)
- Contribute to the Kubernetes project on
  [GitHub](https://github.com/kubernetes/kubernetes)

> Thanks to [Sandeep Dinesh](https://twitter.com/SandeepDinesh), [Mark
> Mandel](https://twitter.com/neurotic), [Craig
> Box](https://twitter.com/craigbox), [Maya
> Kaczorowski](https://twitter.com/mayakaczorowski), and Joe Burnett for
> reviewing drafts of this post.

---
Title: "Container Runtimes Part 2: Anatomy of a Low-Level Container Runtime"
Author: Ian Lewis
Date: 2018-02-26
Link: https://www.ianlewis.org/en/container-runtimes-part-2-anatomy-low-level-contai
---

<details open>

<summary>Additional languages</summary>

- [Simplified Chinese](anatomy-low-level_zh-CN.md)

</details>

# Container Runtimes Part 2: Anatomy of a Low-Level Container Runtime

This is the second in a four-part series on container runtimes. In [part
1](https://www.ianlewis.org/en/container-runtimes-part-1-introduction-container-r),
I gave an overview of container runtimes and discussed the differences between
low-level and high-level runtimes. In this post I will go into detail on
low-level container runtimes.

Low-level runtimes have a limited feature set and typically perform the
low-level tasks for running a container. Most developers shouldn't use them for
their day-to-day work. Low-level runtimes are usually implemented as simple
tools or libraries that developers of higher level runtimes and tools can use
for the low-level features. While most developers won't use low-level runtimes
directly, it's good to know how they work for troubleshooting and debugging
purposes.

As I explained in part 1, containers are implemented using [Linux
namespaces](https://en.wikipedia.org/wiki/Linux_namespaces) and
[cgroups](https://en.wikipedia.org/wiki/Cgroups). Namespaces let you virtualize
system resources, like the file system or networking for each container. On the
other hand, cgroups provide a way to limit the amount of resources, such as CPU
and memory, that each container can use. At their core, low-level container
runtimes are responsible for setting up these namespaces and cgroups for
containers, and then running commands inside those namespaces and cgroups. Most
container runtimes implement more features, but those are the essential bits.

Be sure to check out the amazing talk ["Building a container from scratch in
Go"](https://www.youtube.com/watch?v=Utf-A4rODH8) by Liz Rice. Her talk is a
great introduction to how low-level container runtimes are implemented. Liz goes
through many of these steps, but the most trivial runtime you can imagine that
you could still call a "container runtime" might do something like the
following:

- Create cgroup
- Run command(s) in cgroup
- [Unshare](http://man7.org/linux/man-pages/man2/unshare.2.html) to move to its
  own namespaces
- Clean up cgroup after command completes (namespaces are deleted automatically
  when not referenced by a running process)

A robust low-level container runtime, however, would do a lot more, like allow
for setting resource limits on the cgroup, setting up a root filesystem, and
chrooting the container's process to the root file system.

## Building a Sample Runtime

Let's walk through running a simple ad hoc runtime to set up a container. We can
perform the steps using the standard Linux
[cgcreate](https://linux.die.net/man/1/cgcreate),
[cgset](https://linux.die.net/man/1/cgset),
[cgexec](https://linux.die.net/man/1/cgexec),
[chroot](http://man7.org/linux/man-pages/man2/chroot.2.html) and
[unshare](http://man7.org/linux/man-pages/man1/unshare.1.html) commands. You'll
need to run most of the commands below as root.

First let's set up a root filesystem for our container. We'll use the BusyBox
Docker container as our base. Here we create a temporary directory and extract
BusyBox into it. Most of these commands need to be run as root.

```shell
CID=$(docker create busybox)
ROOTFS=$(mktemp -d)
docker export $CID | tar -xf - -C $ROOTFS
```

Now let's create our cgroup and set restrictions on the memory and CPU. Memory
limits are set in bytes. Here we are setting the limit to 100MB.

```shell
UUID=$(uuidgen)
cgcreate -g cpu,memory:$UUID
cgset -r memory.limit_in_bytes=100000000 $UUID
cgset -r cpu.shares=512 $UUID
```

CPU usage can be restricted in one of two ways. Here we set our CPU limit using
CPU "shares". Shares are an amount of CPU time relative to other processes
running at the same time. Containers running by themselves can use the whole
CPU, but if other containers are running, they can use a proportional amount of
CPU to their CPU shares.

CPU limits based on CPU cores are a bit more complicated. They let you set hard
limits on the amount of CPU cores that a container can use. Limiting CPU cores
requires you set two options on the cgroup, `cfs_period_us` and `cfs_quota_us`.
`cfs_period_us` specifies how often CPU usage is checked and `cfs_quota_us`
specifies the amount of time that a task can run on one core in one period. Both
are specified in microseconds.

For instance, if we wanted to limit our container to two cores we could specify
a period of one second and a quota of two seconds (one second is 1,000,000
microseconds) and this would effectively allow our process to use two cores
during a one-second period. [This
article](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/6/html/resource_management_guide/sec-cpu)
explains this concept in depth.

```shell
cgset -r cpu.cfs_period_us=1000000 $UUID
cgset -r cpu.cfs_quota_us=2000000 $UUID
```

Next we can execute a command in the container. This will execute the command
within the cgroup we created, unshare the specified namespaces, set the
hostname, and chroot to our filesystem.

```shell
$ cgexec -g cpu,memory:$UUID \
>     unshare -uinpUrf --mount-proc \
>     sh -c "/bin/hostname $UUID && chroot $ROOTFS /bin/sh"
/ # echo "Hello from in a container"
Hello from in a container
/ # exit
```

Finally, after our command has completed, we can clean up by deleting the cgroup
and temporary directory that we created.

```shell
cgdelete -r -g cpu,memory:$UUID
rm -r $ROOTFS
```

To further demonstrate how this works, I wrote a simple runtime in bash called
[execc](https://github.com/ianlewis/execc). It supports mount, user, PID, IPC,
UTS, and network namespaces; setting memory limits; setting CPU limits by number
of cores; mounting the proc file system; and running the container in its own
root file system.

## Examples of Low-Level Container Runtimes

In order to better understand low-level container runtimes it's useful to look
at some examples. These runtimes implement different features and emphasize
different aspects of containerization.

### lmctfy

Though not in wide use, one container runtime of note is
[`lmctfy`](https://github.com/google/lmctfy). `lmctfy` is a project by Google,
based on the internal container runtime that
[Borg](https://research.google.com/pubs/pub43438.html) uses. One of its most
interesting features is that it supports container hierarchies that use cgroup
hierarchies via the container names. For example, a root container called
`busybox` could create sub-containers under the name `busybox/sub1` or
`busybox/sub2` where the names form a kind of path structure. As a result each
sub-container can have its own cgroups that are then limited by the parent
container's cgroup. This is inspired by Borg and gives containers in `lmctfy`
the ability to run sub-task containers under a pre-allocated set of resources on
a server, and thus achieve more stringent SLOs than could be provided by the
runtime itself.

While `lmctfy` provides some interesting features and ideas, other runtimes were
more usable so Google decided it would be better for the community to focus
worked on Docker's `libcontainer` instead of `lmctfy`.

### runc

runc is currently the most widely used container runtime. It was originally
developed as part of Docker and was later extracted out as a separate tool and
library.

Internally, runc runs containers similarly to how I described it above, but runc
implements the OCI runtime spec. That means that it runs containers from a
specific "OCI bundle" format. The format of the bundle has a config.json file
for some configuration and a root file system for the container. You can find
out more by reading the [OCI runtime
spec](https://github.com/opencontainers/runtime-spec) on GitHub. You can learn
how to install runc from the [runc GitHub
project](https://github.com/opencontainers/runc).

First create the root filesystem. Here we'll use BusyBox again.

```shell
mkdir rootfs
docker export $(docker create busybox) | tar -xf - -C rootfs
```

Next create a config.json file.

```shell
runc spec
```

This command creates a template config.json for our container. It should look
something like this:

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

By default it runs the sh command in a container with a root filesystem at
./rootfs. Since that's exactly the setup we want we can just go ahead and run
the container.

```shell
$ sudo runc run mycontainerid
/ # echo "Hello from in a container"
Hello from in a container
```

## rkt

rkt is a popular alternative to Docker/runc that was developed by CoreOS. rkt is
a bit hard to categorize because it provides all the features that other
low-level runtimes like runc provide, but also provides features typical of
high-level runtimes. Here I'll describe the low-level features of rkt and leave
the high-level features for the next post.

rkt originally used the [Application
Container](https://coreos.com/rkt/docs/latest/app-container.html) (`appc`)
standard, which was developed as an open alternative standard to alternative to
Docker's container format. Appc never achieved widespread adoption as a
container format and is no longer being actively developed bit achieved its
goals to ensure open standards are available to the community. Instead of
`appc`, `rkt` will use OCI container formats in the future.

Application Container Image (ACI) is the image format for Appc. Images are a
tar.gz containing a manifest directory and a rootfs directory for the root
filesystem. You can read more about ACI in the [`appc` GitHub
repository](https://github.com/appc/spec/blob/master/spec/aci.md).

You can build a container image using the `acbuild` tool. You can use `acbuild`
in shell scripts that can be executed much like Dockerfiles are run.

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

<!-- markdownlint-disable no-trailing-punctuation -->

## Adiós!

<!-- markdownlint-enable no-trailing-punctuation -->

I hope this helped you get an idea of what low-level container runtimes are.
While most users of containers will use higher-level runtimes, it's good to know
how containers are working underneath the covers for troubleshooting issues and
debugging.

In the next post I'll move up the stack and talk about high-level container
runtimes. I'll talk about what high-level runtimes provide and how they are much
better for app developers who want to use containers. I'll also talk about
popular runtimes like Docker and rkt's high-level features. Be sure to add my
RSS feed or [follow me on Twitter](https://twitter.com/IanMLewis) to get
notified when the next blog post comes out.

> Update: Please continue on and check out [Container Runtimes Part 3:
> High-Level
> Runtimes](https://www.ianlewis.org/en/container-runtimes-part-3-high-level-runtimes)

Until then, you can get more involved with the Kubernetes community via these
channels:

- Post and answer questions on [Stack
  Overflow](http://stackoverflow.com/questions/tagged/kubernetes)
- Follow [@Kubernetesio](https://twitter.com/kubernetesio) on Twitter
- Join the Kubernetes [Slack](http://slack.k8s.io/) and chat with us. (I'm
  ianlewis so say Hi!)
- Contribute to the Kubernetes project on
  [GitHub](https://github.com/kubernetes/kubernetes)

> _Thanks to [Craig Box](https://twitter.com/craigbox), Jack Wilbur, Philip
> Mallory, [David Gageot](https://twitter.com/dgageot), Jonathan MacMillan, and
> [Maya Kaczorowski](https://twitter.com/MayaKaczorowski) for reviewing drafts
> of this post._

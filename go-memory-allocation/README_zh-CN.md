# Go 语言中的内存分配

## 免责声明

本文主要关注在 [Linux](https://en.wikipedia.org/wiki/Linux) 上运行的 [Go 1.24](https://tip.golang.org/doc/go1.24) 语言，目标架构为 [ARM](https://en.wikipedia.org/wiki/ARM_architecture_family)。
可能不会覆盖其它操作系统或硬件架构的特定细节。

内容基于其他资料与我对 Go 的理解，因此可能并非完全准确。欢迎在文末评论区指正或提出建议 😄。

## 引言

内存分配是每种编程语言运行时的核心问题，Go 也不例外。高效的内存分配与管理直接影响 Go 应用的性能、可扩展性与响应性。虽然 Go 通过简单的 API（`new(T)`、`&T{}` 和 `make`）屏蔽了大部分复杂性，但了解底层实现可以让我们看到运行时如何实现高效，及其可能的瓶颈所在。

在本文中，我们将深入探讨 Go 的内存分配器。我们会查看其核心组件、如何协同为不同大小的分配服务，以及栈如何与堆对象一起管理。并穿插一些案例研究，让你理解 Go 内存分配策略的实际影响。读完后，你应能更清晰地看到 Go 是如何在屏蔽内存管理细节的同时仍保持高性能的。

在深入了解 Go 如何分配内存之前，先理解典型操作系统中内存工作的基础概念是很重要的。我建议先读一遍 [虚拟内存基础（Fundamental of Virtual Memory）](https://nghiant3223.github.io/2025/05/29/fundamental_of_virtual_memory.html) 那篇文章。如果你已经熟悉相关概念，可以跳过那篇。现在我们进入 Go 对虚拟内存的视角。

## Go 对虚拟内存的视角

由于一个 Go 进程本质上是用户态应用程序，它遵循在那篇基础文章中描述的标准虚拟内存布局。特别地，进程的 _Stack_ 段是与 Go 运行时主线程 `m0` 关联的 `g0` 栈（即系统栈）。已初始化的全局变量存放在 _Data_ 段，而未初始化的则位于 _BSS_ 段。

传统意义上的 _Heap_ 段（位于程序断点 program break 之下）并不是 Go 运行时用于分配堆对象的位置。相反，Go 运行时大量依赖**内存映射段（memory-mapped segments）**来为堆对象与 goroutine（协程）栈分配内存。今后我把 Go 用于动态分配的这些内存映射段称为“堆”，这与传统的程序断点下的堆不要混淆。

| <img src="go_virtual_memory_view.png" width=300> |
| :----------------------------------------------: |
|         从 Go 运行时视角看的虚拟内存布局         |

### Arena 与 Page

为高效管理这些内存，Go 运行时把这些内存映射段按层次划分，粗到细。最粗的单位称为 **arena**（arena），固定大小为 64 MB。Go 会尽量让 arena 的地址是连续的，但由于 `mmap` 系统调用的行为（`mmap` 可能返回与请求地址不同的地址），arena 并不总是连续。

每个 arena 进一步被细分为更小的固定单元称为 **page**，每页大小为 8 KB。注意，这里的运行时管理的页面与基础文章中讨论的典型 OS 页面（通常为 4 KB）不同。若对象小于 8 KB，则每页会放多个**相同大小**的对象；若对象正好为 8 KB，则一页放一个对象；大于 8 KB 的对象会跨多页。

| <img src="go_memory_pages.png" width=900> |
| :---------------------------------------: |
|         Go 运行时的内存页面示意图         |

这些页面也用于为 goroutine（协程）栈分配内存。正如我在之前的 [Go Scheduler](https://nghiant3223.github.io/2025/04/15/go-scheduler.html) 的博文中讨论的，每个 goroutine（协程）栈初始化时占用 2 KB，因此一个 8 KB 的页面最多可以放 4 个 goroutine（协程）栈。

### Span 与 Size Class

Go 内存管理的另一个关键概念是 **span**。span 是由一个或多个**连续**页面组成的内存单元。每个 span 被划分为多个大小相同的对象。通过把 span 划分成多个等大小对象，Go 实际上采用了**分隔适配（segregated fit）**的内存分配策略。这使 Go 能高效地为不同大小的对象分配内存，同时尽量减少碎片。

Go 使用结构体 `mspan` 来保存 span 的元数据，例如首页地址、页数、已分配对象数量等等。在本文中，当我说 “span” 时指该内存区域；当我说 `mspan` 时指描述该区域的结构体。

运行时将对象大小组织为一组预定义的 **size class（大小类）**。每个 span 精确属于一个大小类，由其所包含对象的大小决定。Go 定义了 68 个不同的大小类，编号从 0 到 67（见 `sizeclasses.go`）。其中 size class 0 用于处理 _大对象_（大于 32 KB），而 1 到 67 则用于 _tiny（微小）对象_ 和 _small（小）对象_。

| <img src="span_with_size_class.png" width=900> |
| :--------------------------------------------: |
|          两个不同大小类的 span 示意图          |

某些大小类的 span 包含固定页数与对象数，这由表中的 `bytes/span` 和 `objects` 列决定。上图示例：size class 38（每对象 2048 字节）与 size class 55（每对象 10880 字节）。因为单个 8 KB 页面正好可放四个 2048 字节对象，所以 size class 38 的 span 在一页内含 4 个对象；而 10880 字节对象超出一页，所以 size class 55 的 span 跨 4 页并放 3 个对象。

那为什么 size class 55 的 span 不是只跨 2 页而放 1 个对象，而是跨 4 页放 3 个对象呢？这是为了减少内存碎片。因为 span 内的对象是连续的，最后一个对象到 span 末尾之间可能存在一段空间，称为 _tail waste（尾部浪费）_，可用公式计算：`(页数)*8192 - (对象数)*(对象大小)`。若 span 跨 2 页并仅含 1 个 10880 字节对象，则尾部浪费是 `2*8192 - 10880*1 = 5504` 字节；若跨 4 页含 3 个对象，则尾部浪费是 `4*8192 - 10880*3 = 128` 字节——明显更小。

| <img src="span_tail_waste.png" width=900> |
| :---------------------------------------: |
|           span 的尾部浪费示意图           |

既然用户的 Go 应用可能分配任意大小的对象，为什么 Go 只设置 67 个小对象大小类？比如应用分配了一个 300 字节的小对象，表中并没有直接对应项，该怎么办？在这种情况下，Go 运行时会把对象大小向上舍入到下一个大小类——例如 320 字节。到目前为止图中所示的绿色对象并不是用户实际分配的对象，而是运行时管理的“大小类对象”。

| <img src="user_objects_and_size_class_objects.png" width=500> |
| :-----------------------------------------------------------: |
|               用户对象与大小类对象的关系示意图                |

用户分配的对象（下文简称 _user objects_）被包含在大小类对象中。user objects 的大小可以各不相同，但必须小于其所属大小类对象的大小。因此，user object 的实际大小与其所属大小类对象大小之间会存在浪费。这类在所有大小类对象中的浪费加上 span 的尾部浪费构成了该 span 的 _总浪费_。

> ⚠️ 一个大小类对象并不总是只包含一个 user object。对于 small 与 large 的 user object，通常每个大小类对象放一个 user object；但对于 tiny 的 user object，会把多个 user object 填打包到一个大小类对象内（见后文 [Tiny Objects Allocator](#tiny-objects-allocator)）。

举例：考虑一个 size class 55 的 span，在最坏情况内它放了三个 user object，每个大小为 10241 字节（即 size class 55 的最小用户对象大小）。三个大小类对象的浪费是 `3*(10880-10241) = 3*639 = 1917` 字节；尾部浪费是 `4*8192 - 10880*3 = 128` 字节。因此 span 的总浪费是 `1917 + 128 = 2045` 字节；span 总大小 `4*8192 = 32768` 字节。最大总浪费比为 `2045/32768 = 6.24%`，这与 Go 大小类表中该 size class 的第 6 列一致。

尽管 Go 使用了旨在减少碎片的分隔适配策略，但内存仍然有一定浪费。span 的总浪费反映了每个 span 的外部碎片程度。

### Span Class

Go 的垃圾回收器采用追踪（tracing）式回收，这意味着在一次回收周期中需要遍历对象图来识别所有可达对象。然而，如果某个类型在编译时就确定**不包含任何指针**（既不直接包含，也不通过字段间接包含），例如一个只含基本类型字段的 struct，那么垃圾回收器可以安全地跳过扫描该类型的对象，从而降低开销并提升性能。指针的有无在编译时即可确定，因此该优化没有额外运行时成本。

为支持这种行为，Go 引入了 **span class** 的概念。span class 基于两个属性对 span 进行分类：其包含对象的大小类（size class）以及这些对象是否包含指针（是否需要扫描）。若对象包含指针，span 属于 _scan_ 类；若不包含指针，则为 _noscan_ 类。

因为是否包含指针是二值属性，span class 的总数等于大小类数的两倍。Go 定义了 `68*2 = 136` 个 span class，编号从 0 到 135。如果编号是偶数则为 _scan_ 类；若为奇数则为 _noscan_ 类。

更准确地说：每个 span 属于且仅属于一个 span class。通过将 span class 编号除以 2 即可得到它对应的 size class。span class 的奇偶性决定了它是 scan 还是 noscan。

### Span Set

为了高效管理 span，Go 将它们组织到称为 **span set** 的数据结构里。span set 是一组属于相同 span class 的 `mspan` 对象的集合，结构如下面图示。

| <img src="span_set.png" width=500> |
| :--------------------------------: |
|       span set 的布局示意图        |

本质上，span set 是一个由多个数组组成的切片。切片按需增长，每个数组大小固定为 512 个条目。数组的每个元素是一个 `mspan`（可以为空）。图中紫色表示非空元素，白色表示空元素。

span set 还有两个额外字段：`head` 与 `tail`，用于跟踪第一个与最后一个非空元素。pop（弹出）操作从 `head` 开始，按数组自上而下、每个数组内从左到右遍历；push（推入）操作从 `tail` 开始，也按自上而下、每数组从左到右填充。如果 push 或 pop 导致某个数组变为空，则该数组会被从 span set 中移除并放入空闲数组池以便复用。

注意 `head` 与 `tail` 是原子变量，因此多个 goroutine（协程）可以并发地向 span set 添加或移除 span，而无需额外加锁。

### Heap Bits 与 Malloc Header

假设有一个包含 1000 个字段的大结构体，其中某些字段是指针，Go 的垃圾回收器如何知道哪些字段是指针以便正确遍历对象图？如果回收器在运行时检查每个对象的每个字段，那将非常低效，尤其是对于大型或深度嵌套的数据结构。

为了解决这个问题，Go 使用元数据来高效标记指针位置，从而避免扫描所有字段。该机制基于两个关键结构：**heap bits（堆位图）**与 **malloc header（malloc 头）**。

对于小于 512 字节的对象，Go 在 span 中分配内存并使用堆位图来跟踪 span 中哪些字（word，通常 8 字节）包含指针。位图中的每一位对应一个字：1 表示该字是指针，0 表示非指针数据。该位图存放在 span 的尾部并被该 span 中所有对象共享。当创建 span 时，Go 会为位图保留空间，剩余空间用于放置尽可能多的对象。

| <img id="heap-bits" src="heap_bits.png" width=500> |
| :------------------------------------------------: |
|               span 中的堆位图示意图                |

对于大于 512 字节的对象，维护大位图效率不高。此时，每个对象会附带一个 8 字节的 malloc header——一个指向对象类型信息的指针。该类型元数据包含 `GCData` 字段，用来编码类型的指针布局。垃圾回收器利用这些信息在遍历对象图时精准地定位包含指针的字段，从而高效扫描。

| <img id="malloc-header" src="malloc_header.png" width=500> |
| :--------------------------------------------------------: |
|                对象的 malloc header 示意图                 |

下面是你提供内容的 **中文（简体，zh-CN）翻译**，保留原文的 Markdown 结构、代码/链接和图片引用（术语与代码名如 `mheap`、`mspan`、`mcentral`、`mcache` 等保持英文原文形式以便查阅源码或文档）。如你希望把某些术语（例如 `goroutine`、`GOMAXPROCS`、`sysmon`）在文档中每次出现时都加注中文解释，我可以在下一次把整篇合并并统一处理。

## 堆管理

Go 在内存映射段之上构建了自己的堆抽象，由全局的 `mheap` 对象负责管理（见 `mheap` 源码）。
`mheap` 负责分配新的 span、回收未使用的 span，甚至管理 goroutine（协程）的栈。

### Span 分配：`mheap.alloc`

由于 Go 运行时运行在巨大的虚拟地址空间内，`mheap` 在为 span 分配连续空闲页时（尤其在高并发下）可能很难高效地找到连续的空闲页。早期 Go 的每个 `mheap` 操作都是全局同步的（参见 “Scaling the Go Page Allocator” 提案），这种设计在高并发分配负载下会导致**严重的吞吐量下降和尾延迟增大**。现代 Go 的内存分配器实现了该提案中的可扩展设计。下面我们来看看它如何克服这些瓶颈并在高度并发环境中高效管理内存。

#### 跟踪空闲页

因为虚拟地址空间很大，并且每一页的状态（空闲或使用中）是二值的，所以用位图来存储这些信息是高效的：`1` 表示使用中，`0` 表示空闲。这里的“使用中/空闲”是指页面是否被某个 span 拥有，而不是被用户程序直接访问。每个位图由 8 个 `uint64` 组成，总共 64 字节，可表示 512 个连续页面的状态。

一个 arena 大小为 64 MB，页面大小为 8 KB，则每个 arena 包含 `64MB/8KB = 8192` 页。因为每个位图覆盖 512 页，故每个 arena 需要 `8192/512 = 16` 个位图。每个位图 64 字节，因此所有位图共 `16 × 64 = 1024` 字节，即 1 KB。

然而，遍历位图以寻找连续空闲页仍然低效，若位图中没有空闲页则更是浪费。因此需要某种**缓存**机制让我们能快速定位连续空闲区块。Go 引入了位图的**summary（摘要）** 概念，包含三项字段：`start`、`end` 和 `max`。`start` 表示该位图开头连续的 0 位数；`end` 表示位图末尾连续的 0 位数；`max` 表示位图中最长的连续 0 位序列长度。位图一旦被修改（分配或释放页面），对应的摘要会被及时更新。

下图示意了一个位图摘要：开头有 3 个连续空闲页，末尾有 7 个连续空闲页，最长连续空闲段为 10。箭头表示地址增长方向（低地址端的 3 页，高地址端的 7 页）。

| <img src="bitmap_summary.png" width=500> |
| :--------------------------------------: |
|          位图摘要的可视化示意图          |

有了这三项字段，Go 可以通过合并相邻内存块的摘要，在单个 arena 内或跨相邻 arena 迅速找到足够大的连续空闲块。举例：两个相邻的 512 页块 `S1` 与 `S2`，`S1` 的摘要为 `start=3, end=7, max=10`，`S2` 的摘要为 `start=5, end=2, max=8`。合并后覆盖 1024 页的摘要为 `start = S1.start = 3`，`end = S2.end = 2`，`max = max(S1.max, S2.max, S1.end + S2.start) = max(10, 8, 7+5) = 12`。

| <img src="merging_summary.png" width=900> |
| :---------------------------------------: |
|        合并相邻内存块摘要的示意图         |

通过自底向上合并更低层的摘要，Go 隐式构建了一棵分层结构以高效跟踪连续空闲页。运行时使用一棵全局的摘要 **radix 树** 来管理整个虚拟地址空间（下图所示）。每个蓝色方框表示一个连续内存块的摘要，虚线显示它在下一层覆盖的范围；绿色方框表示叶子节点对应的 512 页的位图。

| <img src="summary_radix_tree.png" width=900> |
| :------------------------------------------: |
|      整个地址空间的摘要 radix 树示意图       |

在 linux/amd64 架构下，Go 使用 48 位虚拟地址空间（`2^48` 字节 = 256 TB），这使得 radix 树高度为 5。内部节点（0 到 3 层）通过合并 8 个子节点的摘要得到其摘要；叶子节点（第 4 层）对应单个位图（覆盖 512 页）。

各层的条目数按 8 的幂增长；每个叶子条目汇总 512 页，因此 level 0 的每个条目汇总 `512 * 8^4 = 2097152` 连续页，等于 `2097152 * 8KB = 16GB` 的内存量。注意这些数字是最大可能项数，实际条目数量会随着堆增长逐步扩展。

| <img src="radix_tree_zoom.png" width=900> |
| :---------------------------------------: |
|      summary radix 树的更深层次视图       |

如前所述，level 0 的每个条目汇总 `2^21` 连续页，因此 `start`、`end`、`max` 的最大值可达 `2^21`。把这三项一起存储需要 `21 * 3 = 63` 位，因此可以将摘要打包进单个 `uint64`（类型名 `pallocSum`）：低 21 位存 `start`，接下来的 21 位存 `end`，再接下来的 21 位存 `max`。

有一个特殊情况：若 `max = 2^21`，意味着整块内存都空闲，此时 `start` 和 `end` 也为 `2^21`，摘要可以编码为 `2^63`。相反，若块内无任何空闲页（`start=end=max=0`），摘要的值为 `0`。

摘要 radix 树以“数组的切片数组”实现（每个切片对应一层）。数组固定树的层数，而切片根据堆的扩张动态增长。切片中低地址对应的摘要保存在开头，高地址摘要追加到尾部。由于某层的摘要切片覆盖整个**已保留**的地址空间，摘要在切片中的索引直接决定其代表的内存区域。

#### 查找空闲页：`pageAlloc.find`

Go 使用深度优先搜索来定位满足请求的连续空闲页段。它从 radix 树的 level 0（最多 16,384 个条目）开始扫描；若某个摘要为 `0`（无空闲页），则跳过。如果在两个相邻条目的边界上或某条目的开头/末尾找到了满足条件的连续段，则可根据该摘要所指的地址立即返回空闲段的地址。

若当前摘要的 `max` 字段满足分配请求，则搜索会下探到其 8 个子节点继续查找。如果搜索到叶子层仍未找到足够的连续段，则在 `max` 足够大的条目中扫描其位图以找出确切的连续页段。遍历完 level 0 的所有条目仍未找到时，返回 `0`，表示没有可用的连续页。

该算法的一个缺点是：如果 level 0 开头的许多页已被占用，分配器可能会在 radix 树中重复走相同路径进行多次分配，效率不高。为此，Go 维护了一个称为 `searchAddr` 的**hint**，标记在该地址之前没有空闲页。这样分配器可直接从 hint 开始搜索，而不是每次从头开始。

由于分配通常从低地址向高地址推进，hint 会在每次搜索后前移，从而缩小搜索范围，直到有内存被释放为止。实际运行中，大部分分配都发生在接近当前 hint 的位置。

#### 扩展堆：`mheap.grow`

如果 radix 树中没有足够的空闲页（即 `pageAlloc.find` 返回 0），Go 运行时必须通过调用 `mmap` 向内核请求扩展虚拟地址空间。堆的增长通常不会精确按请求的页数增长，而是按 arena 大小（64 MB）向上取整。即使只请求 1 页，虚拟内存空间也会以 64 MB 的 arena 单位扩展（物理内存方面仍受需求分页影响）。

为此，运行时维护了一组 **arenaHints**（首选地址列表），这些地址是运行时希望内核用于新分配的地址。该列表在 `main` 函数运行前初始化，具体值可查看源码链接。堆增长时，Go 会遍历这些 hints，向 `mmap` 的第一个参数传入建议地址以请求内存。

内核可能会选择不同的位置；若发生这种情况，Go 会尝试下一个 hint。若所有 hint 都失败，Go 会退回到请求一个按 arena 大小对齐的随机地址，然后更新 hint 列表以便未来的增长与新分配的 arena 保持连续。

此过程将内存段状态从 _None_ 过渡到 _Reserved_。一旦 arena 被注册到运行时（加入到所有 arena 的列表），该段从 _Reserved_ 转为 _Prepared_。此时，摘要 radix 树会更新包含新 arena，扩展每层的摘要切片，标记新页的位图为空闲，并更新摘要。这段新内存也被标为**已使用**供运行时追踪。

#### 建立 Span：`mheap.haveSpan`

一旦找到满足需求的连续页段，运行时会设置一个 `mspan` 对象来管理该内存范围。像其他 Go 对象一样，`mspan` 本身也需要内存。因此这些 `mspan` 对象由 `fixalloc` slab 分配器分配，`fixalloc` 直接通过 `mmap` 向内核请求内存。

随后为该 span 配置其大小类、页数与首页地址。相关的内存段状态从 _Prepared_ 转为 _Ready_，表示可以供 `mcentral` 使用。

#### 缓存空闲页：`mheap.allocToCache`

不幸的是，`pageAlloc.find` 与 `mheap.grow` 都依赖全局锁，在高并发分配负载下可能成为瓶颈。因为 Go 程序的并发度与处理器 `P` 的数量相关，将空闲页在每个 `P` 本地缓存有助于避免全局锁竞争。

Go 为此实现了每个 `P` 的 `pageCache`。`pageCache` 包含一个 64 页对齐内存块的基地址和一个 64 位位图，用于跟踪这些页中哪些是空闲的。因为每页 8 KB，单个 `P` 的 `pageCache` 最多能缓存 512 KB 的空闲内存。

当 goroutine 请求 `mheap` 分配 span 时，运行时首先检查当前 `P` 的 `pageCache`。若缓存中有足够空闲页，则直接使用这些页来建立 span；否则回退调用 `pageAlloc.find` 来定位足够的连续页。

若 `pageCache` 为空，运行时会分配一个新的 `pageCache`。它会尝试在当前 `searchAddr` 附近获取页（如前文“查找空闲页”所述）。因为 hint 可能不精确，有时需要遍历 radix 树来查找空闲页。

需要注意的是，当请求的页数 `N` 接近 64 时，获得恰好 `N` 页的概率会下降（`pageCache` 最多 64 页），此时缓存未命中的频率会升高，运行时会频繁回退到 `pageAlloc.find`。因此当 `N >= 16` 时，运行时直接绕过缓存，直接调用 `pageAlloc.find`。

下图概括了为 span 分配查找空闲页的逻辑。灰色框“Find pages”对应“查找空闲页”部分；绿色框“Grow the heap”对应“扩展堆”；蓝色框“Set up a span”对应“建立 span”。

（原文中包含流程图的 Mermaid 源码，此处保留该图表结构）

一旦获取到新页，它们会在摘要 radix 树中被标记为已使用，以防其他 `P` 抢占，并确保分配器在下次堆增长时不会重用这些页。摘要树的 hint 也会更新以跳过这些已使用页。

#### 缓存 Span 对象：`mheap.allocMSpanLocked`

如前文“建立 Span”所述，需要为每个 span 分配一个 `mspan` 对象进行管理。如果直接从 `mheap` 获取 `mspan`，则需要获取全局锁，这可能成为性能瓶颈。为避免该瓶颈，Go 在每个 `P` 也缓存空闲的 `mspan` 对象，类似于页面的本地缓存。

当在 `pageCache` 中找到空闲页时，运行时会先检查当前 `P` 是否已有缓存的 `mspan`；若有则可立即复用，避免全局锁争用。否则，运行时会从 `mheap` 批量分配多个 `mspan`，将它们缓存到该 `P` 的空闲链表以备将来使用，然后用其中一个来管理新分配的页段。

### 中央 Span 管理器：`mcentral`

由于 `mheap` 主要管理粗粒度的内存（如页面和大 span），它并不适合高效地分配和释放 tiny 或 small 对象。这个工作由 `mcentral` 承担，`mcentral` 也是 `mheap` 与每个 `P` 的分配器 `mcache` 之间的桥梁。

#### 内部数据结构

每个 `mcentral` 管理属于某个特定 span class 的 spans。总体上，`mheap` 维护 136 个 `mcentral` 实例（每个 span class 一个）。在一个 `mcentral` 内部，有两类 span 集合：`full`（没有空闲对象的 spans）和 `partial`（有部分空闲对象的 spans）。每类又细分为 `swept` 与 `unswept` 两个集合，取决于这些 spans 是否已被 sweep 过。

（原文包含一个 Mermaid 示意图，表示 `mcentral` 中的四类 span set）

什么是被 `swept`？Go 的 GC 是标记-清扫（mark-and-sweep）式：首先标记所有可达对象，然后清扫不可达对象，回收其内存以供运行时重用或在某些情况下释放回内核以减小进程占用。sweep 的核心步骤包括：从 unswept 集合弹出一个 span，释放其中被标记为不可达的对象，然后把 span 推入 swept 集合。

span 在 partial 与 full 集合间的转换由分配或 sweep 决定，取决于该 span 的空闲对象数是否增减。若某 span 的空闲对象数变为零，则从 partial 移到 full；反之若空闲对象数为正，则从 full 移到 partial。

由于 span set 是线程安全的（见上文“Span Set”），`mcentral` 可以被多个 goroutine 并发访问而无需额外锁，从而提高 span 分配的吞吐量。

#### 准备 Span：`mcentral.cacheSpan`

作为 `mheap` 与 `mcache` 之间的中间层，`mcentral` 负责将其 span 集合中已有的 span（或向 `mheap` 请求到的新 span）准备好以供请求的 `mcache` 使用。下图（Mermaid 流程图）展示了详细逻辑：若需要，为了分配会先进行 sweep 以准备可用内存；优先从 partial swept 集合中返回 span；若没有，则从 partial unswept 中 sweep 一个；若仍没有则考虑 full unswept（sweep 后检查是否有空闲对象）；若都没有则请求 `mheap` 分配新的 span。

#### 回收 Span：`mcentral.uncacheSpan`

当 `mcache` 需要把一个 span 归还给 `mcentral` 时，会调用 `mcentral.uncacheSpan`。若该 span 尚未被 sweep，则先进行 sweep 回收不可达对象。无论是否进行了 sweep，最终该 span 会根据其空闲对象数被放入 full 或 partial 的 swept 集合中。

### 处理器的内存分配器：`mcache`

如在 [Go Scheduler](https://nghiant3223.github.io/2025/04/15/go-scheduler.html) 一文中讨论的，每个处理器 `P` 是 goroutine 的执行上下文。由于 goroutine 会分配内存，每个 `P` 也维护着自己的内存分配器 `mcache`，它针对 tiny 与 small 堆分配做了优化，同时也用于为 goroutine 分配栈段。

#### 缓存空闲 Span

`mcache` 的名称来自它在其 `alloc` 字段中为每个 span class 缓存带有空闲对象的 span。每当 `mcache` 实例初始化时，所有大小类都会被初始化为 `emptymspan`（不含空闲对象）。当某个 goroutine 需要为特定 span class 分配 user object 时，它会向 `mcache` 请求一个空闲的 size-class 对象：要么从缓存的 span 直接获得，要么当缓存 span 无空闲对象时，向 `mcentral` 请求新的 span。下面的 Mermaid 流程图说明了这一逻辑。

（原文含 Mermaid 流程图：查看缓存 span 中是否有空闲大小类对象；若无则将非空的缓存 span 返还给 mcentral，并请求新的 span；缓存新 span；返回对象等）

绿色框“Return the cached span to `mcentral`”对应“回收 span”；蓝色框“Request `mcentral` for a new span”对应“请求 mcentral 准备 span”。

#### Tiny Objects 分配器

所有 user tiny 对象（小于 16 字节）都从 size class 5（或 size class 2，取决源码表述）中分配，该大小类的每个 size-class 对象占 16 字节。每个 `mcache` 实例通过三个字段追踪微小分配：

1. `tiny`：当前 size-class 对象的起始地址（该对象尚有可分配空间）。
2. `tinyoffset`：相对于 `tiny` 的上一次已分配用户对象的结束位置。
3. `tinyalloc`：当前 span 中已分配的微对象总数。

| <img id="tiny-span" src="tiny_allocation_span.png" width=400> |
| :-----------------------------------------------------------: |
|             示例：用于 tiny 对象分配的 span 示例              |

上图中 `0x30` 是示例性的 size-class 对象起始地址。详细的分配逻辑见后文“Tiny Objects: `mallocgcTiny`”。

#### 总结

正如前文所述，Go 的内存分配器是一个由三部分协同工作的复杂系统：`mheap`（管理粗粒度页面/arena）、`mcentral`（管理同一 span class 的 spans 并与 mheap/mcache 协调）以及每个 `P` 的 `mcache`（用于快速分配 tiny/small 对象）。下图汇总了这些组件如何交互以为 Go 程序分配内存。建议在进入后续的堆分配细节之前先回顾此图。

| <img id="tiny-span" src="memory_allocator_recap.png"> |
| :---------------------------------------------------: |
|                Go 内存分配器的工作总结                |

下面是你提供内容的 **中文（简体，zh-CN）翻译**，保留原文的 Markdown 结构、代码/链接与图示引用（函数名、类型名与源码标识如 `mallocgc`、`mcache` 等保留英文以便查源码）。如需我把这些翻译合并成一个 Markdown 文件导出（例如 `memory_allocation_in_go_zh.md`），或者在文档中每次出现某些术语都添加注释（例如 `goroutine（协程）`、`GOMAXPROCS`、`sysmon`），告诉我你的偏好我可以生成并提供下载链接。

## 堆分配

在 Go 中，常有一种误解认为把对象分配到堆上必须使用 `new(T)` 或 `&T{}`。事实并非总是如此，原因有三：

1. 如果对象足够小、仅在某个函数作用域内存在，且不会被作用域外引用，编译器可能会将其分配到栈上而不是堆上。
2. 即便是用 `var n int` 声明的原始类型变量，也可能因为 **逃逸分析（escape analysis）** 的结果而被放到堆上。
3. 使用 `make` 创建的复合类型（slice、map、channel 等）通常会将其底层数据结构放到堆上。

对象是否放到堆上的决定由编译器做出；相关讨论见本文后文的 **[栈还是堆？](#stack-or-heap)** 部分。本节仅聚焦运行时用于在堆上分配对象的方法 `mallocgc`（会被 `new`、`make`、`&T{}` 等内建操作间接调用）。

`mallocgc` 会根据对象大小把对象分为三类：**tiny**（小于 16 字节）、**small**（16 字节 到 32760 字节）和 **large**（大于 32760 字节）。它还会考虑对象类型是否包含指针（影响垃圾回收）。基于这些条件，`mallocgc` 会走不同的分配路径以优化内存使用和性能，下面的流程图展示了这些路径选择。

（原文的 Mermaid 流程图保留）

### Tiny 对象：`mallocgcTiny`

微小对象由每个处理器 `P` 上的 `mcache` 提供，使用在前文 **Tiny Objects Allocator** 部分描述的三个字段来跟踪分配。其分配逻辑如下图所示。

（原文的 Mermaid 流程图保留）

`tinyoffset` 会根据请求的大小做对齐：若大小能被 8 整除则按 8 字节对齐，能被 4 整除则按 4 字节对齐，能被 2 整除则按 2 字节对齐，否则不做对齐。蓝色菱形处的检查表示：以当前 `tinyoffset` 为起点、长度为请求 `size` 的用户对象能否放入当前的 size-class 对象；若能，则直接在该 size-class 对象内分配新用户对象。绿色框“向 `mcentral` 请求 size class 5 的新 span”的逻辑见前文 **Preparing a Span** 一节。

| <img id="tiny-span" src="tiny_allocation.png"> |
| :--------------------------------------------: |
|              微小对象的分配示意图              |

注意微小对象的分配由每个处理器局部的 `mcache` 提供，这使得分配在绝大多数情况下无锁且线程安全，只有在需要向 `mcentral`/`mheap` 请求新 span 时才可能涉及全局同步。

用于微小对象分配的 spans 属于 span class 5（或 size class 2）。依据 [size class 表](https://github.com/golang/go/blob/go1.24.0/src/runtime/sizeclasses.go#L6-L73)，size class 2 的一个 span 可容纳 512 个 size-class 对象。由于每个 size-class 对象可以在微小分配下容纳多个用户对象，因此单个 span 在无锁情形下就能服务至少 512 次微小对象分配。

### Small 对象：`mallocgcSmall*`

为了让垃圾回收器高效识别可达对象并跳过不含指针的对象，Go 将 small 对象根据其类型是否包含指针分为 **scan** 与 **noscan** 两类 span class（详见 [Span Class](#span-class)）。对含指针的 scan 类，又有两种布局：一种在 span 末端保存 heap bits，另一种为每个对象前置 malloc header（详见 [Heap Bits and Malloc Header](#heap-bits-and-malloc-header)）。Go 根据这些分类实现了不同的 small 对象分配函数。

#### _Noscan_ 小对象：`mallocgcSmallNoscan`

不包含指针的小对象由 `mallocgcSmallNoscan` 分配。首先将请求的 `size` 向上舍入以精确匹配某个 size-class。因为是 noscan，其对应的 span class 计算为 `2*sizeclass+1`（奇数表示 noscan）。例如，请求 365 字节会被向上舍入到 384 字节（size class 22），对应的 span class 为 `2*22+1 = 45`。

函数会检查当前处理器 `P` 的 `mcache` 中该 span class 的缓存 span 是否有空闲对象；若没有，则向 `mcentral` 请求一个新 span 并缓存到 `mcache`。拿到空闲 size-class 对象后，更新 GC 与分析器相关信息并返回分配地址。

#### _Scan_ 小对象：`mallocgcSmallScanNoHeader` 与 `mallocgcSmallScanHeader`

含指针的小对象根据大小由 `mallocgcSmallScanNoHeader`（当 `size ≤ 512` 字节时使用）或 `mallocgcSmallScanHeader`（当 `size > 512` 字节时使用）分配。这两者与 `mallocgcSmallNoscan` 的总体逻辑相似，但在 span class、span 内部布局和 size-class 对象的具体布局上有所不同。

用于 `mallocgcSmallScanNoHeader` 的 spans 在末尾保留专门的数据区域用于 heap bits（参见 [Heap Bits and Malloc Header]），因此可放置的 size-class 对象数量会少于 size class 表上未保留 bits 时的值；这由 `mheap.initSpan` 在创建 span 时处理空间保留逻辑。

用于 `mallocgcSmallScanHeader` 的 span 则在每个 size-class 对象前置 8 字节的 malloc header（指向类型信息）。因此在为这类对象匹配 size-class 时，需要先将请求的 `size` 增加 8 字节再向上舍入到最近的 size-class。例如，请求 636 字节且对象包含指针：原本 636 可匹配 size class 28（640 字节），但加上 8 字节 malloc header 后变为 644 字节，因而要提升到 size class 29（704 字节）。

### 大对象：`mallocgcLarge`

由于 `mcache` 与 `mcentral` 只管理不超过 32 KB 的 size class，超过 32760 字节的大对象会直接由 `mheap` 分配（参见 **Span Allocation** 部分），不会通过 `mcache` 或 `mcentral`。用于大对象的 spans 也分为 scan 或 noscan；不同于 small 对象，大对象的 span class 固定：scan spans 编号为 0，noscan spans 编号为 1。

当分配大对象（例如包含一百万个大结构体的 slice）时，内核通常不会立即提交物理内存；运行时只是保留虚拟地址空间。物理页只会在程序首次写入该区域时实际分配（即依赖于**按需分页（demand paging）**）。

## 栈管理

如在 [Go Scheduler](https://nghiant3223.github.io/2025/04/15/go-scheduler.html) 一文中所述，Go 运行时代码与用户代码都运行在由内核管理的线程上。每个线程有自己的栈——一段连续的内存块，用于保存栈帧，而栈帧又存放函数参数、局部变量和返回地址。由于把变量分配到栈上本质上只是调整栈指针（参见 [Stack Allocation](https://nghiant3223.github.io/2025/05/29/fundamental_of_virtual_memory.html#stack-allocation)），我们关注的重点是 Go 中栈如何被分配和管理。

在 Go 中，线程的栈称为 **系统栈（system stack）**，而 goroutine 的栈简称为 **栈（stack）**。为管理执行上下文，运行时引入了 `m`（线程）和 `g`（goroutine）抽象。每个 `g` 都有一个 `stack` 字段记录其栈的起始与结束地址。每个 `m` 有一个特殊的 `g0` goroutine，其栈就是系统栈。运行时在执行必须运行在系统栈上的操作（例如增长或收缩某个 goroutine 的栈）时会切换到 `g0`。

主线程的系统栈由内核在 Go 进程启动时分配。对于非主线程，系统栈由内核或 Go 运行时分配，具体取决于操作系统以及是否使用了 [CGO](https://go.dev/wiki/cgo)。在 Darwin（macOS）和 Windows 上，内核总是为非主线程分配系统栈；但在 Linux 上，除非启用了 CGO，否则 Go 运行时会为非主线程分配系统栈。

| <img src="darwin_windows_memory_layout.png" width=200/> | <img src="linux_memory_layout.png" width=200/> |
| :-----------------------------------------------------: | :--------------------------------------------: |
|            Darwin/Windows 进程的虚拟内存布局            |            Linux 进程的虚拟内存布局            |

内核分配的系统栈位于 Go 管理的虚拟内存空间之外，而运行时分配的系统栈则创建在 Go 管理的地址空间内。内核会确保其系统栈不会与 Go 管理的内存冲突。内核分配的系统栈通常在 512 KB 到几 MB 之间，而由 Go 分配的系统栈固定为 [16 KB](https://github.com/golang/go/blob/go1.24.0/src/runtime/proc.go#L2242-L2242)。相比之下，goroutine 的栈起始为 [2 KB](https://github.com/golang/go/blob/go1.24.0/src/runtime/proc.go#L5044-L5044)，并可根据需要动态增长或收缩。

### 分配栈：`stackalloc`

由 Go 运行时管理的栈——无论是系统栈还是 goroutine 栈——都存放在与堆对象相同的 [span](#span-and-size-class) 中。可以把栈看作一种特殊的堆对象，用于在执行运行时或用户代码时保存局部变量和调用帧。

栈通常从当前 `P` 的 `mcache` 中分配。如果垃圾回收正在进行、处理器 `P` 数量发生变化，或当前线程在系统调用期间与其 `P` 分离，则栈会从全局池中分配。全局池分为两类：用于小于 32 KB 的 [*small*] 池，以及用于等于或大于 32 KB 的 [*large*] 池。

goroutine 的栈以较小的尺寸开始，因此最初由 small 栈池分配；当栈因调用更多函数或分配更多栈变量而增长超过 32 KB 时，会改为使用 large 栈池。该行为将在 [栈增长](#stack-growth-morestack) 一节详细说明。

#### 从池中分配栈

small 栈池是一个包含四项的数组，每项是双向链表，链表中的元素是 `mspan`（每个 `mspan` 保存一块虚拟内存的元数据）。该池中的所有 span 都属于 span class 0，并覆盖四个连续页面，因此每个 span 占用 32 KB。数组的每个条目对应一个栈阶（order），决定栈的大小：order 0 → 每个栈为 2 KB，order 1 → 4 KB，order 2 → 8 KB，order 3 → 16 KB。

之所以把栈按阶和大小这样分类，是因为 goroutine 栈以连续内存区域形式存在，且扩展时大小会成倍增长。关于这点在 [栈增长](#stack-growth-morestack) 中会有更详细解释。

| <img id="tiny-span" src="small_stack_pool.png" width=500> |
| :-------------------------------------------------------: |
|                       小栈池示意图                        |

当需要小于 32 KB 的栈时，运行时首先根据请求大小确定合适的阶（order），然后检查该阶链表的头部以寻找可用的 span。若没有可用 span，则向 `mheap` 请求一个 span（见 [Span Allocation](#span-allocation)），并把该 span 切割成所需阶的若干个栈。一旦某个 span 就绪，运行时取出第一个可用栈，更新 span 的元数据，并返回该栈。

large 栈池实现上更简单，是一个包含不同大小栈的链表；每个栈都存放在 span class 0 的某个 `mspan` 中。当请求等于或大于 32 KB 的栈时，从链表中弹出第一个栈并返回；若链表为空，则向 `mheap` 请求新的 span。

注意：由于栈池是全局的、可被多个线程并发访问，因此这些池被互斥锁保护以保证线程安全，但这带来了锁竞争导致吞吐下降的折衷。

#### 从缓存分配栈

为减少分配栈时的锁竞争，每个处理器 `P` 在其 `mcache` 中维护自己的栈缓存。与 small 栈池类似，栈缓存是一个四项的数组，但每项是单向链表，存放空闲栈，且每项对应一个栈阶（order）。

在处理小栈分配请求时，运行时会先检查当前 `P` 的栈缓存是否有可用栈；若没有，则会从 small 栈池补充若干栈到缓存中，然后返回第一个。大栈不会从栈缓存中分配，而是始终直接从 large 栈池分配。

### 栈增长：`morestack`

#### 分段栈（segmented stack）

历史上，Go 使用过分段栈（segmented stack）策略。每个 goroutine 从一个小栈开始；若某次函数调用需要比当前栈更多的空间，就会分配一个新的栈并把它链到前一个栈上。函数返回时，会释放新栈并继续在前一个栈上执行。这个过程称为 **stack split（栈分裂）**。

下面的代码片段与示意图说明了一个场景：函数 `ingest` 一行行处理文件，当 `read` 的栈帧跨越两个栈时，若栈指针达到某个限制（所谓的 _stack guard_，稍后讨论），对 `read` 或 `process` 的调用可能会触发栈分裂。请注意，在该方案下 goroutine 的栈可能不是连续的内存区域。

```go
func ingest(path string) {
  ...
  for {
    line, err := read(file) // Causes stack split.
    if err == io.EOF {
      break
    }
    process(line)
  }
  ...
}
```

| <img id="tiny-span" src="stack_split.png"> |
| :----------------------------------------: |
|         分段栈策略中的栈分裂示意图         |

但分段栈会遇到一个性能问题，称为 **hot stack split**（热栈分裂）问题：如果某个函数在紧密的循环里频繁需要分配和释放栈，整个过程会付出显著性能开销。函数返回时新分配的栈被释放。由于每次栈分裂大约需要 60 纳秒，这在循环中反复发生时会产生显著开销。

一种避免该问题的技巧是在频繁被调用的函数的栈帧中加入**填充（padding）**，通过分配一些哑变量来增大栈帧，从而降低发生栈分裂的概率。但从程序员角度这既容易出错又降低可读性。

#### 连续栈（contiguous stack）

为缓解 hot stack split 问题，自 Go 1.4 之后 Go 改用了称为 **contiguous stacks（连续栈）** 的策略。当 goroutine 的栈需要增长时，会分配一个比当前栈大两倍的新栈。将当前栈的内容复制到新栈后，goroutine 切换到使用新栈。

| <img src="copy_stack.png" width=600> |
| :----------------------------------: |
|      连续栈策略中的栈复制示意图      |

上图展示了连续栈在增长后的行为：在某些情况下（如第一轮迭代结束后）栈并不会立即缩小，这有助于缓解热分裂问题。

不过如果 goroutine 的栈在高峰期显著增长但随后大部分空间长时间未使用，则长久不缩会浪费内存。实际上，在连续栈方案下，goroutine 的栈会在垃圾回收周期中被收缩（而非函数返回时）。如果当前栈的实际使用总量小于当前栈大小的四分之一，就会分配一个大小为当前栈一半的新栈，把内容复制过去并切换到新栈。更多细节见 `shrinkstack` 的实现。

如在 [Go Scheduler](https://nghiant3223.github.io/2025/04/15/go-scheduler.html#cooperative-preemption-since-go-114) 一文中提到，为了让 goroutine 栈能够增长，编译器需要在函数序言（prologue）处插入检查。这个检查是 CPU 指令，会消耗若干 CPU 周期。对被频繁调用的小函数而言，这个开销可能比较明显。为减小该开销，小函数可以使用 `//go:nosplit` 指令告诉编译器**不要**在其序言插入栈增长检查。

> ⚠️ 别混淆：
> `//go:nosplit` 中的 “split” 一词看起来像是分段栈时代的“栈分裂”，但在连续栈方案中它实际上也表示“不要插入栈增长检查”。

#### 栈保护（Stack Guard）

当调用函数时，栈指针会根据函数栈帧大小向下移动（在栈增长方向上减少），随后与 goroutine 的 **stack guard** 比较，以决定是否需要增长栈。stack guard 由两部分组成：`StackNosplitBase` 和 `StackSmall`。在 Linux 上，这会把守护位置放在栈底上方 928 字节处——其中 `StackNosplitBase` 为 800 字节，`StackSmall` 为 128 字节。

| <img src="/assets/2025-06-03_MEMORY_ALLOCATION_IN_GO/stack_guard.png" width=500> |
| :------------------------------------------------------------------------------: |
|                     goroutine 栈中 stack guard 的位置示意图                      |

（注：图片路径在原文中为 `stack_guard.png`。）

既然溢出意味着栈指针越过栈底，为什么要把栈指针与 stack guard 而不是与栈底比较？Go 运行时源码中的注释对此已有解释，简单总结如下：

第一，允许函数使用 `//go:nosplit` 跳过栈增长检查，所以必须为这类函数预留等同于 `StackNosplitBase` 的空间，以便它们能在不触发增长的情况下安全执行。例如，`morestack` 本身用来处理栈增长，它的整个栈帧必须能适配在当前分配的栈上。

第二，这对小栈帧是一个优化：对于栈帧小于 `StackSmall` 的小函数，Go 在调用时不一定每次都调整栈指针并与 guard 比较；它可以仅检查当前栈指针是否已低于 guard，从而每次调用节省一次对栈指针的调整操作，节省一条 CPU 指令。

### 回收/重用栈：`stackfree`

当 goroutine 执行完毕、栈因可用空间过多而被收缩，或由 Go 运行时管理的系统线程退出时，它们的栈会被标记为可重用。如果该 goroutine 此时仍附着在某个 `P` 上，且该 `P` 的栈缓存未满，则栈会返回到该 `P` 的栈缓存；否则栈会被放回全局池（small 或 large），具体取决于栈的大小。

当栈被返回到全局池时，如果垃圾回收未在进行，相应的内存页可能会被退回给内核。更多实现细节见运行时源码中的注释。

## 栈还是堆？

有人可能认为 `var n T` 总是在栈上分配，而 `new(T)` 或 `&T{}` 总是在堆上分配类型为 `T` 的对象。但在 Go 中事实并非总是如此。下面通过一些示例说明问题的本质以及 Go 如何处理。

考虑下面的程序：函数 `getUserByID` 在栈上分配一个 `User` 结构体，从数据库读取数据并返回该结构体的地址（也就是返回指向该栈上结构体的指针）。

```go
func getUserByID(id int64) *User {
  var user User
  user = db.FindUserByID(id)
  return &user
}

func main() {
  var userID int64 = 1
  var user *User = getUserByID(userID)
  var userAge = user.age
  user.age = userAge + 1
}
```

| <img src="dangling_pointer.png" width=500> |
| :----------------------------------------: |
|   悬空指针（dangling pointer）问题示意图   |

当 `getUserByID` 被调用时，变量 `user` 被放在其栈帧中的地址（例如 `0xe0`）。函数返回后，`user` 变量仍然保存着 `0xe0`，但该地址不再有效，因为 `getUserByID` 的栈帧已经被弹出。随后 `main` 尝试访问 `user.age` 时会解引用一个无效地址，导致 **悬空指针** 和未定义行为。

为防止此类问题，Go 在编译期使用一种称为 **逃逸分析（escape analysis）** 的技术来判断变量是否应该分配到栈上还是必须“逃逸”到堆上。逃逸分析会检查用 `var n T`、`new(T)`、`&T{}` 或 `make(T)` 声明的变量是否会在声明函数外被引用；如果会被引用，则必须分配到堆上，以保证函数返回后仍能安全访问。

在上述示例中，`user` 的地址被返回并在 `main` 中使用，因此编译器会判定 `user` 需要逃逸到堆上，进而把它分配到堆以避免悬空指针问题。

逃逸分析也**尽力把变量保留在栈上**，即便这些变量在语义上本可分配到堆（例如通过 `new(T)`、`&T{}` 或 `make(T)` 创建的对象），只要编译器能证明它们仅在函数作用域内使用且其大小不超过 `MaxImplicitStackVarSize`，就会把它们放在栈上。

你可以通过 `-gcflags="-m"` 选项编译下面的程序来观察编译器的优化决策（包括逃逸分析的结果）：

```go
package main

type User struct { ID int64 }

func newUser(id int64) *User {
  user := User{ID: id}
  return &user
}

func main() {
  _ = newUser(20250603)
  _ = make([]User, 100)
}

// $ go build -gcflags="-m" main.go
// ./main.go:6:2: moved to heap: user
// ./main.go:12:10: make([]User, 100) does not escape
```

可以看到 `user` 被移动到了堆（因为它会逃逸），而 `make([]User, 100)` 没有逃逸，因为它仅在 `main` 中使用且大小少于 `MaxImplicitStackVarSize`。

如果把 slice 的长度改为 1,000,000 并再次编译：

```text
// $ go build -gcflags="-m" main.go
// ./main.go:6:2: moved to heap: user
// ./main.go:12:10: make([]User, 1000000) escapes to heap
```

这次除了 `user` 逃逸外，`make([]User, 1000000)` 也会逃逸到堆上，因为其大小超过了 `MaxImplicitStackVarSize`，因此无法安全地放在栈上。

## 案例分析

既然你已经更清楚 Go 如何为堆对象和栈分配内存，下面我们用几个真实世界的例子来看看 Go 在实际场景中的分配行为，以及如何优化堆分配。

### 案例 1：重用 slice 的底层数组

如你所知，Go 中的 slice 是一个描述符，包含指向底层数组的指针、长度和容量。当我们用 `make([]T, length, capacity)` 创建新 slice 时，编译器会把 `make` 替换为运行时的 [`makeslice`](https://github.com/golang/go/blob/go1.24.0/src/runtime/slice.go#L92-L117) 调用。`makeslice` 会调用 [`mallocgc`](#heap-allocation) ，按 `capacity * sizeof(T)` 的大小在堆上分配底层数组。也就是说，`capacity` 表示底层数组的大小，而 `length` 表示数组中正在使用的元素数。

`append` 会在 slice 末尾加入元素并增加 length。如果新 length 超过当前 capacity，`append` 会调用 [`mallocgc`](https://github.com/golang/go/blob/go1.24.0/src/runtime/malloc.go#L992-L1096) 分配一个翻倍的新底层数组，将已有元素拷贝过去，并更新 slice 描述符指向新数组。

slice 可以用 `[start:end]` 语法重切片（reslice）。重切片会创建一个新的 slice 头，指向原底层数组的某个子范围。重要的是 —— 这个操作不会拷贝数据或分配额外内存，新的 slice 只是重用已存在的数组。详见 [Slice Intro](https://go.dev/blog/slices-intro)。

我们可以利用重切片来优化堆分配：复用 slice 的底层数组而不是每次都分配新数组。看下面这个逐行解析 CSV 并处理每行的示例程序（每行可能有大量字段）：

```go
package main

import (
  "bufio"
  "os"
)

func parse(line string) []string {
  start := 0
  var row []string
  for i := 0; i < len(line); i++ {
    if line[i] == ',' {
      row = append(row, line[start:i])
      start = i + 1
    }
  }
  row = append(row, line[start:])
  return row
}

func process(row []string) {
  // Process the line.
}

func main() {
  file, _ := os.Open("input.csv")
  defer file.Close()

  scanner := bufio.NewScanner(file)
  for scanner.Scan() {
    line := scanner.Text()
    row := parse(line)
    process(row)
  }
}
```

因为 `parse` 每次都会创建一个空的 slice，所以每次调用都会在堆上分配新的底层数组；而且对每个字段调用 `append`，当字段数超过初始 capacity 时会触发多次堆分配。这样会多次走相同的 `mallocgc` 路径，产生大量不必要的堆分配。

我们可以通过重切片复用 `row` 的底层数组来优化。用 `row[:0]` 将 slice 的长度重置为 0，但保持其 capacity 不变。这样只有第一次解析时（首次分配）会产生堆分配。举例：对于每行有 1024 个字段、总行数为 1,000,000 的 CSV 文件，简单地复用底层数组可以把堆分配次数从 `1000000 * log₂(1024) = 10⁷` 降到仅 `log₂(1024) = 10`。

优化后的代码：

```go
package main

import (
  "bufio"
  "os"
)

func parse(line string, row []string) []string {
  start := 0
  for i := 0; i < len(line); i++ {
    if line[i] == ',' {
      row = append(row, line[start:i])
      start = i + 1
    }
  }
  row = append(row, line[start:])
  return row
}

func process(row []string) {
  // Process the line.
}

func main() {
  file, _ := os.Open("input.csv")
  defer file.Close()

  var row []string
  scanner := bufio.NewScanner(file)
  for scanner.Scan() {
    line := scanner.Text()
    row = row[:0] // Reuse the underlying array.
    row = parse(line, row)
    process(row)
  }
}
```

### 案例 2：把多个变量合并到一个 struct

最近在 `iter` 包中有一次提交把多个标量变量合并到了一个 struct 中（提交见链接）。
原来这 7 个变量都超出了函数作用域，因此它们各自被单独分配到堆上，导致 7 次对 [`mallocgc`](https://github.com/golang/go/blob/go1.24.0/src/runtime/malloc.go#L992-L1096) 的调用。尽管其中有些变量小于 16 字节，可由 Tiny Allocator 分配，但频繁调用 `Pull` 时 7 次 `mallocgc` 的开销仍然明显。

将这些变量组合到一个 struct 中，只需一次对 `mallocgc` 的调用以在堆上分配该 struct，从而提高分配效率。这种方法的缺点是把原本不相关的对象耦合在一起，可能导致垃圾回收器无法单独回收已经不再需要的其中某些对象。但在该场景中这些变量大多数是一起使用的，这种折衷是可以接受的。

原 PR 中的基准结果显示堆分配次数从 11 次减少到了 5 次，内存占用与分配时间约减少三分之一，符合把 7 个变量合并为一个 struct 所节省的 `mallocgc` 次数。

```
         │ /tmp/bench.old │           /tmp/bench.new           │
         │     sec/op     │   sec/op     vs base               │
Pull-12       218.6n ± 7%   146.1n ± 0%  -33.19% (p=0.000 n=10)

         │ /tmp/bench.old │           /tmp/bench.new           │
         │      B/op      │    B/op     vs base                │
Pull-12        288.0 ± 0%   176.0 ± 0%  -38.89% (p=0.000 n=10)

         │ /tmp/bench.old │           /tmp/bench.new           │
         │   allocs/op    │ allocs/op   vs base                │
Pull-12       11.000 ± 0%   5.000 ± 0%  -54.55% (p=0.000 n=10)
```

### 案例 3：用 `sync.Pool` 重用对象

在一些应用中，会频繁创建并丢弃大量短生命周期、无状态且类型相同的对象。一个典型例子是 `fmt` 包中用于格式化的打印器对象 `pp`，它在 `Fprintf`、`Sprintf` 等函数中被大量使用。

如果这些函数每次都分配一个新的 `pp`，使用后丢弃，那么在每秒写入 10,000 条日志的场景下，会导致每秒有 10,000 个 `pp` 被分配并被 GC 扫描，产生巨大的开销。

为减少这类开销，Go 提供了 `sync.Pool`，用于缓存和重用相同类型的对象。`sync.Pool` 在处理 `Get` 请求时会先在池中查找可用对象；若没有，则调用用户提供的 `New` 创建一个（最终会调用 `mallocgc` 在堆上分配）。客户端使用完对象后可用 `Put` 将对象放回池中。通过回收对象，`sync.Pool` 能减少堆分配次数以及 GC 需扫描的对象数量，从而提升性能。

实际上，`Fprintf` 与 `Sprintf` 会从池中取出 `pp`，用于格式化，使用完再放回池中复用。`ppFree` 池在 `fmt` 包初始化时建立。

`sync.Pool` 在高并发下设计为尽可能无锁与高效。它依赖于一种称为 pinning 的技术，防止在 `Get`/`Put` 操作时 goroutine 被抢占，从而保证 goroutine 在操作期间留在同一处理器 `P` 上（`sync.Pool` 在每个 `P` 上有本地结构，pinning 有利于局部性与无锁设计）。

## 结论

Go 的内存分配器有一个明确目标：在高度并发的应用中提供高效的分配能力。通过在 `mheap`、`mcentral`、`mcache` 间分层设计，运行时在全局协同与每个 `P` 的本地缓存之间取得平衡，最小化锁竞争并保持分配快速。栈的管理与堆对象不同，但也遵循高效分配与自适应增长的原则。

对大多数 Go 开发者来说，上述细节被 `&T{}`、`new(T)`、`make(T)` 等简单构造所屏蔽。但理解这些内部机制有助于明白为何某些模式性能更好、垃圾回收如何与分配交互、以及运行时为实现低延迟并发做出的权衡。在构建与优化 Go 应用时，请记住你创建的每个变量或 goroutine 最终都由这些机制支撑。

希望这些内容能帮助你写出更高效、更可靠的 Go 程序。如有疑问，欢迎留言讨论。 <span>
如果你喜欢我的内容，欢迎请我喝杯咖啡支持我： <span> <a href="https://buymeacoffee.com/nghiant3221" target="_blank"> <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 2em;"> </a> </span>! 😄 </span>

## 参考资料

- Ankur Anand. [_A Visual Guide to Go Memory Allocator_](https://blog.ankuranand.com/2019/02/20/a-visual-guide-to-golang-memory-allocator-from-ground-up/).
- sobyte.net. [_Go Memory Allocation_](https://www.sobyte.net/post/2022-01/go-memory-allocation/), [Go Stack Management](https://www.sobyte.net/post/2021-12/golang-stack-management/).
- Michael Knyszek, Austin Clements. [_Scaling the Go Page Allocator_](https://go.googlesource.com/proposal/+/master/design/35112-scaling-the-page-allocator.md).
- Dmitry Vyukov. [_Go Scheduler: Implementing Language with Lightweight Concurrency_](https://www.youtube.com/watch?v=-K11rY57K7k).

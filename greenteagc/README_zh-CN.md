---
title: The Green Tea Garbage Collector
date: 2025-10-29
by:
- Michael Knyszek
- Austin Clements
tags:
- garbage collection
- performance
summary: Go 1.25 includes a new experimental garbage collector, Green Tea.
---

<style type="text/css" scoped>
  .centered {
	position: relative;
	display: flex;
	flex-direction: column;
	align-items: center;
  }
  div.carousel {
	display: flex;
	width: 100%;
	height: auto;
	overflow-x: auto;
	scroll-snap-type: x mandatory;
	padding-bottom: 1.1em;
  }
  .hide-overflow {
	overflow-x: hidden !important;
  }
  button.scroll-button-left {
	left: 0;
	bottom: 0;
  }
  button.scroll-button-right {
	right: 0;
	bottom: 0;
  }
  button.scroll-button {
	position: absolute;
	font-size: 1em;
	font-family: inherit;
	font-style: oblique;
  }
  figure.carouselitem {
	display: flex;
	flex-direction: column;
	align-items: center;
	margin: 0;
	padding: 0;
	width: 100%;
	flex-shrink: 0;
	scroll-snap-align: start;
  }
  figure.carouselitem figcaption {
	display: table-caption;
	caption-side: top;
	text-align: left;
	width: 80%;
	height: auto;
	padding: 8px;
  }
  figure.captioned {
	display: flex;
	flex-direction: column;
	align-items: center;
	margin: 0 auto;
	padding: 0;
	width: 95%;
  }
  figure.captioned figcaption {
	display: table-caption;
	caption-side: top;
	text-align: center;
	font-style: oblique;
	height: auto;
	padding: 8px;
  }
  div.row {
	display: flex;
	flex-direction: row;
	justify-content: center;
	align-items: center;
	width: 100%;
  }
</style>

<noscript>
    <center>
    <i>为了获得最佳体验，请在启用 JavaScript 的浏览器中查看 <a href="https://go.dev/blog/greenteagc">这篇博客文章</a>。</i>
    </center>
</noscript>

Go 1.25 包含一个名为 Green Tea 的新的实验性垃圾回收器，构建时通过设置 `GOEXPERIMENT=greenteagc` 可用。
许多工作负载在垃圾回收上所花费的时间减少了约 10%，但有些工作负载的降低幅度可达 40%！

它已达到可在生产环境使用的水平并已在 Google 内部投入使用，因此我们鼓励你去试试。
我们也知道有些工作负载受益不大，甚至没有受益，所以你的反馈对我们推进这项工作十分关键。
基于当前数据，我们计划在 Go 1.26 中将其设为默认。

要报告问题，请[提交新 issue](https://github.com/golang/go/issues/new)。

要报告成功案例，请回复[现有的 Green Tea issue](https://github.com/golang/go/issues/73581)。

下文为基于 Michael Knyszek 在 GopherCon 2025 的演讲改写的博客文章。

{{video "[https://www.youtube.com/embed/gPJkM95KpKo"}}](https://www.youtube.com/embed/gPJkM95KpKo%22}})

## 跟踪式垃圾回收

在讨论 Green Tea 之前，让我们先把垃圾回收的基础讲清楚。

### 对象与指针

垃圾回收的目的是自动回收并重用程序不再使用的内存。

为此，Go 的垃圾回收器关注的是 *对象* 和 *指针*。

在 Go 运行时的上下文中，*对象* 是其底层内存从堆上分配的 Go 值。
当 Go 编译器无法找出其他方式为一个值分配内存时，就会创建堆对象。
例如，下面这段代码会分配一个堆对象：一个指针切片的底层存储。

```
var x = make([]*int, 10) // global
```

Go 编译器无法将该切片的底层存储分配到堆以外的地方，因为它很难，甚至可能不可能，判断 `x` 会引用该对象多久。

*指针* 只是表示 Go 值在内存中位置的数字，它们是 Go 程序引用对象的方式。
例如，要获得上面代码片段中分配的对象起始处的指针，我们可以写：

```
&x[0] // 0xc000104000
```

### 标记-清除算法

Go 的垃圾回收器遵循一种通常称为 *跟踪式垃圾回收* 的策略，这意味着垃圾回收器会沿着程序中的指针进行遍历或“跟踪”来识别程序仍在使用的对象。

更具体地说，Go 的垃圾回收器实现了标记-清除（mark-sweep）算法。
这并不像听起来那样复杂。
把对象和指针想象成一种图（graph），按计算机科学的意义来理解。
对象是节点，指针是边。

标记-清除算法在这个图上运行，顾名思义，它分为两个阶段。

第一阶段是标记阶段（mark），它从称为 *根* 的明确定义的起始边走起。
想想全局变量和局部变量。
然后，它会将沿途发现的所有东西 *标记* 为 *已访问*，以避免循环往复。
这类似于典型的图的泛洪算法，如深度优先或广度优先搜索。

接下来是清除阶段（sweep）。
在我们的图遍历中没有被访问到的对象就是未使用的，或称为 *不可达*。
我们称其为不可达，因为通过正常的安全 Go 代码语义不可能再访问那段内存。
在清除阶段，算法会简单地迭代所有未被访问的节点并将其内存标记为可用，以便内存分配器重用。

### 就是这样吗？

你可能会觉得我有点过于简化了。
垃圾回收器经常被称为 *魔法* 或 *黑盒子*。
你在某种程度上是对的，实际上还有更多复杂之处。

例如，在实践中该算法是与常规 Go 代码并发执行的。
在底层正在变化的图上进行遍历带来了挑战。
我们还对该算法进行了并行化，这将是后面会再次提到的细节。

但请相信我，这些细节大多与核心算法是分离的。
核心之处确实只是一个简单的图泛洪。

### 图泛洪示例

让我们通过一个示例来演示。
通过下面的幻灯片逐步查看以跟上讲解。

<noscript>
<i>水平滚动以查看幻灯片！</i>
<br />
<br />
建议在启用 JavaScript 的环境下查看，这样会显示“Previous”和“Next”按钮。
这将允许你通过点击翻阅幻灯片，而不是滚动查看，从而更好地突出图表之间的差异。
<br />
<br />
</noscript>

<div class="centered">
<button type="button" id="marksweep-prev" class="scroll-button scroll-button-left" hidden disabled>← Prev</button>
<button type="button" id="marksweep-next" class="scroll-button scroll-button-right" hidden>Next →</button>
<div id="marksweep" class="carousel">
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-007.png" />
		<figcaption>
		这里是一些全局变量和 Go 堆的示意图。
		让我们逐块拆解。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-008.png" />
		<figcaption>
		左边是我们的根。
		这是全局变量 x 和 y。
		它们将作为图遍历的起点。
		根据左下角的图例，它们被标为蓝色，表示它们当前在我们的工作列表上。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-009.png" />
		<figcaption>
		右侧是我们的堆。
		目前堆中的所有内容都呈灰色，因为我们还没有访问任何东西。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-010.png" />
		<figcaption>
		这些矩形中的每一个都代表一个对象。
		每个对象都标有其类型。
		此处的对象是类型 T 的对象，其类型定义在左上角。
		它有一个指向子项数组的指针，以及一些值。
		我们可以推断这是某种递归的树状数据结构。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-011.png" />
		<figcaption>
		除了类型为 T 的对象外，你还会注意到我们有包含 *T 的数组对象。
		这些数组对象由类型为 T 的对象的 "children" 字段指向。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-012.png" />
		<figcaption>
		矩形内部的每个方格代表 8 字节的内存。
		带点的方格代表指针。
		如果有箭头，则表示是非 nil 指针，指向某个对象。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-013.png" />
		<figcaption>
		如果没有对应的箭头，则表示它是 nil 指针。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-014.png" />
		<figcaption>
		接下来，这些虚线矩形代表空闲空间，我称之为空闲“槽”。我们可以把对象放在那里，但目前没有对象。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-015.png" />
		<figcaption>
		你还会注意到对象被这些带标签的虚线圆角矩形分组。
		每一个都代表一页（<i>page</i>），即一块连续的固定大小、对齐的内存。
		在 Go 中，页大小为 8 KiB（不论硬件虚拟内存页面大小如何）。
		这些页被标记为 A、B、C 和 D，下面我会这样称呼它们。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-015.png" />
		<figcaption>
		在这个示意图中，每个对象都作为某一页的一部分被分配。
		就像真实实现中一样，每一页只包含某一特定大小的对象。
		这正是 Go 堆的组织方式。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-016.png" />
		<figcaption>
		页也是我们组织每个对象元数据的方式。
		这里你可以看到七个盒子，每个对应页面 A 中七个对象槽之一。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-016.png" />
		<figcaption>
		每个盒子代表一位信息：我们是否已经见过该对象。
		这实际上就是真实运行时管理对象是否被访问过的方式，这在后面会成为重要细节。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-017.png" />
		<figcaption>
		信息量有点大，感谢你跟着看完。
		这些内容稍后都会派上用场。
		现在，让我们看看图泛洪如何应用于这张图。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-018.png" />
		<figcaption>
		我们先从工作列表取出一个根。
		我们把它标为红色以示它现在是活动的。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-019.png" />
		<figcaption>
		沿着该根的指针，我们找到一个类型为 T 的对象，将其加入工作列表。
		根据图例，我们将该对象绘为蓝色，表示它在工作列表上。
		注意我们也为该对象在元数据中设置了 seen 位。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-020.png" />
		<figcaption>
		另一个根也是同样的操作。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-021.png" />
		<figcaption>
		现在我们处理完所有根，工作列表上有两个对象。
		我们把一个对象从工作列表中取出。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-022.png" />
		<figcaption>
		接下来我们要做的是遍历对象的指针，以发现更多对象。
		顺便说一下，我们把遍历对象的指针称为“扫描”对象。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-023.png" />
		<figcaption>
		我们发现了这个有效的数组对象…
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-024.png" />
		<figcaption>
		…并将它加入工作列表。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-025.png" />
		<figcaption>
		从这里，我们递归地继续。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-026.png" />
		<figcaption>
		我们遍历数组的指针。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-027.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-028.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-029.png" />
		<figcaption>
		找到更多对象…
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-030.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-031.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-032.png" />
		<figcaption>
		然后我们遍历数组对象所引用的对象！
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-033.png" />
		<figcaption>
		注意我们仍然必须遍历所有指针，即使它们是 nil。
		我们事先不能知道它们是否为 nil。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-034.png" />
		<figcaption>
		沿着这条分支又走完一个对象…
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-035.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-036.png" />
		<figcaption>
		现在我们到达了另一条分支，从我们之前从某个根在页 A 中找到的那个对象开始。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-036.png" />
		<figcaption>
		你可能注意到我们的工作列表呈现后进先出（LIFO）的纪律，这表明我们的工作列表是一个栈，因此我们的图泛洪近似为深度优先。
		这是有意为之，并反映了 Go 运行时中实际的图泛洪算法。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-037.png" />
		<figcaption>
		继续前进…
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-038.png" />
		<figcaption>
		接下来我们找到另一个数组对象…
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-039.png" />
		<figcaption>
		并遍历它…
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-040.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-041.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-042.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-043.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-044.png" />
		<figcaption>
		工作列表上只剩下一个对象了…
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-045.png" />
		<figcaption>
		我们来扫描它…
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-046.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-047.png" />
		<figcaption>
		现在我们完成了标记阶段！没有任何活动项，工作列表也为空。
		所有以黑色绘制的对象都是可达的，灰色的是不可达的。
		让我们一口气清除所有不可达对象。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/marksweep-048.png" />
		<figcaption>
		我们已将那些对象转换为空闲槽，准备放置新对象。
		</figcaption>
	</figure>
</div>
</div>

## 问题所在

在了解了这些之后，我想我们已经掌握了 Go 垃圾回收器到底在做什么。
这个过程今天看起来已经运行得相当好，那问题是什么呢？

事实是，在某些程序中我们在执行该算法上会花费 *大量* 时间，这给几乎每个 Go 程序增加了实质性的开销。
在 Go 程序中看到 20% 或更多的 CPU 时间耗费在垃圾回收上并不罕见。

让我们拆解这些时间都花在了哪里。

### 垃圾回收成本

宏观来看，垃圾回收器的成本有两部分。
第一是它运行的频率，第二是每次运行时所做的工作量。
把这两者相乘，就得到垃圾回收器的总体成本。

<figure class="captioned">
	<figcaption>
	总 GC 成本 = GC 周期数 &times; 每次 GC 周期的平均成本
	</figcaption>
</figure>

多年来我们在两项工作上都有所推进，若要了解垃圾回收器运行频率的更多信息，请参见 [Michael 在 2022 年 GopherCon EU 的演讲](https://www.youtube.com/watch?v=07wduWyWx8M)（关于内存限制）。
[Go 垃圾回收指南](/doc/gc-guide) 也包含大量相关内容，如果你想深入可读一读。

但现在我们只关注第二项，即每次周期的成本。

通过多年查看 CPU 性能剖面来改进性能，我们知道关于 Go 垃圾回收器有两个重要结论。

其一，大约 90% 的垃圾回收开销都花在标记阶段，而只有大约 10% 用于清除。
清除实际上比标记更容易优化，Go 多年来一直有一个非常高效的清除器。

其二，在标记耗时中，有相当一部分时间，通常至少 35%，只是因为在访问堆内存时被 *阻塞*。
这本身已经很糟糕，但它完全阻碍了现代 CPU 真正高速运作的能力。

### “微架构灾难”

在这里“阻塞”的确切含义是什么？
现代 CPU 的细节可能会很复杂，所以让我们用个类比。

想象 CPU 正在沿着一条路行驶，这条路就是你的程序。
CPU 想要加速到高速行驶，为此它需要能看到远方，并且道路要畅通。
但图泛洪算法就像在城市街道中行驶。
CPU 无法看到转弯处，也无法预测接下来会发生什么。
为了前进，它不断不得不减速转弯、在红绿灯处停下，并躲避行人。
无论发动机多快，都很难真正提速。

把这个更具体地反映到我们之前的示例上。
我在堆上叠加了我们走过的路径。
每一条从左到右的箭头代表我们做的一段扫描工作，而虚线箭头显示我们在不同扫描工作之间如何跳转。

<figure class="captioned">
	<img src="greenteagc/graphflood-path.png" />
	<figcaption>
	示例中垃圾回收器在堆上所走的路径。
	</figcaption>
</figure>

注意我们在内存中四处跳转，只做每个地方很小的一点工作。
特别是，我们经常在页之间、页的不同部分之间跳转。

现代 CPU 做了大量缓存工作。
访问主内存可能比访问缓存中的内存慢多达 100 倍。
CPU 缓存会被最近访问过且邻近的内存填充。
但没有保证任意两个互指的对象在内存中也会彼此接近。
图泛洪并没有考虑这一点。

顺便说一下：如果我们只是因为等待主内存而被阻塞，情况可能并不那么糟糕。
CPU 可以异步发出内存请求，因此即使慢的请求也能重叠执行（如果 CPU 能看到足够的指令）。
但在图泛洪中，每一小段工作都很小、不可预测，并且高度依赖于上一步，因此 CPU 被迫在几乎每次单独的内存访问上等待。

更不幸的是，这个问题只会变得更严重。
业界有一句话：“等两年，你的代码会变快。”

但对于采用标记-清除算法的垃圾回收语言 Go 来说，风险相反。
“等两年，你的代码会变慢。”
现代 CPU 硬件的趋势给垃圾回收器性能带来了新挑战：

**非一致性内存访问（NUMA）。**
首先，现在内存往往与 CPU 核心的子集相关联。
其他 CPU 核心访问该内存的速度比以前更慢。
换句话说，主内存访问的成本[取决于访问它的 CPU 核心是哪一个](https://jprahman.substack.com/p/sapphire-rapids-core-to-core-latency)。
这是非一致的，因此我们称之为 NUMA。

**内存带宽减少。**
每个 CPU 可用的内存带宽随时间减少。
这意味着尽管我们有更多的 CPU 核心，但每个核心能向主内存提交的请求相对减少，导致非缓存请求比以前等待更久。

**CPU 核心持续增加。**
上面我们看的是顺序标记算法，但真实的垃圾回收器是并行执行该算法的。
这在有限数量的 CPU 核心上能很好扩展，但共享的对象扫描队列会成为瓶颈，即使设计得很用心也是如此。

**现代硬件特性。**
新硬件有一些花哨的功能，比如向量指令，允许我们一次处理大量数据。
虽然这可能带来巨大的加速潜力，但要把它用于标记并不容易，因为标记的大部分工作是不规则并且通常很小的。

## Green Tea

这就把我们带到了 Green Tea，我们在标记-清除算法上的新方法。
Green Tea 的关键思想惊人地简单：

*以页为单位工作，而不是以对象为单位。*

听起来很平凡，对吗？
然而，要弄清如何对对象图的遍历进行调度以及为实践中的良好运作需要追踪哪些信息，还是花了大量工作。

更具体地说，这意味着：

* 我们不是扫描对象，而是扫描整页。
* 我们不是在工作列表上追踪对象，而是追踪页。
* 最终我们仍需要标记对象，但我们会将已标记对象的信息局部维护在每一页上，而不是跨整个堆去维护。

### Green Tea 示例

通过再次查看我们的示例堆（不过这次运行 Green Tea 而不是简单的图泛洪），我们来看看这在实践中意味着什么。

同样地，通过注释幻灯片逐步查看以跟上说明。

<noscript>
<i>水平滚动以查看幻灯片！</i>
<br />
<br />
建议在启用 JavaScript 的环境下查看，这样会显示“Previous”和“Next”按钮。
这将允许你通过点击翻阅幻灯片，而不是滚动查看，从而更好地突出图表之间的差异。
<br />
<br />
</noscript>

<div class="centered">
<button type="button" id="greentea-prev" class="scroll-button scroll-button-left" hidden disabled>← Prev</button>
<button type="button" id="greentea-next" class="scroll-button scroll-button-right" hidden>Next →</button>
<div id="greentea" class="carousel">
	<figure class="carouselitem">
		<img src="greenteagc/greentea-060.png" />
		<figcaption>
		这是之前相同的堆，但现在每个对象有两个元数据位而不是一个。
		同样，每一位或盒子对应页面中的一个对象槽。
		总计我们现在有十四个位，对应页面 A 中的七个槽。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-060.png" />
		<figcaption>
		顶部的位与之前相同：表示我们是否看到过指向该对象的指针。
		我称这些为“seen” 位。
		底部的一组位是新的。
		这些“scanned” 位追踪我们是否已经对该对象进行了<i>扫描</i>。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-060.png" />
		<figcaption>
		这个新的元数据是必要的，因为在 Green Tea 中，<b>工作列表追踪的是页面，而不是对象</b>。
		我们仍然需要在某种层面追踪对象，这些位就是为此而设。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-062.png" />
		<figcaption>
		我们一开始仍与之前相同，从根遍历对象。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-063.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-064.png" />
		<figcaption>
		但这次，我们不是把对象放到工作列表上，而是把整页——在本例中是页 A——放到工作列表上，用蓝色阴影表示整页。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-066.png" />
		<figcaption>
		我们找到的对象也被渲染为蓝色，表示当我们真的把该页从工作列表上取下时，需要查看那个对象。
		注意对象的蓝色色调直接反映了页面 A 中的元数据。
		其对应的 seen 位被设置，但 scanned 位尚未设置。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-069.png" />
		<figcaption>
		我们沿着下一个根继续，找到另一个对象，再次把整页——页 C——放到工作列表并设置该对象的 seen 位。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-071.png" />
		<figcaption>
		我们完成根的遍历，所以转向工作列表并取下页 A。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-072.png" />
		<figcaption>
		利用 seen 和 scanned 位，我们可以判断出页 A 上有一个对象需要扫描。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-074.png" />
		<figcaption>
		我们扫描该对象，沿着它的指针继续。
		因此我们把页 B 加入工作列表，因为页 A 中的第一个对象指向页 B 中的一个对象。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-075.png" />
		<figcaption>
		我们处理完页 A。
		接下来我们取下工作列表上的页 C。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-076.png" />
		<figcaption>
		与页 A 相似，页 C 上也只有一个对象需要扫描。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-078.png" />
		<figcaption>
		我们找到一个指向页 B 中另一个对象的指针。
		页 B 已经在工作列表上，因此我们无需再次将它加入列表。
		我们只需要为目标对象设置 seen 位。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-079.png" />
		<figcaption>
		现在轮到页 B 了。
		我们已经在页 B 上积累了两个要扫描的对象，
		可以连续按内存顺序处理这两个对象！
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-081.png" />
		<figcaption>
		我们遍历第一个对象的指针…
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-082.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-083.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-084.png" />
		<figcaption>
		我们发现一个指向页 A 中对象的指针。
		页 A 之前在工作列表上，但现在不在了，所以我们把它重新放回工作列表。
		与原始的标记-清除算法不同，在那里任意给定对象在整个标记阶段最多被加入工作列表一次，而在 Green Tea 中，给定的页面可以在一个标记阶段中多次出现在工作列表上。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-085.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-086.png" />
		<figcaption>
		我们紧接着扫描页面中第二个被标记的对象。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-087.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-088.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-089.png" />
		<figcaption>
		我们在页 A 中找到了更多对象…
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-090.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-091.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-092.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-093.png" />
		<figcaption>
		我们完成了页 B 的扫描，所以我们从工作列表中取下页 A。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-094.png" />
		<figcaption>
		这次我们只需要扫描三个对象，而不是四个，
		因为我们已经扫描了第一个对象。
		我们通过查看 "seen" 和 "scanned" 位的差异来知道哪些对象需要扫描。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-095.png" />
		<figcaption>
		我们将按顺序扫描这些对象。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-096.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-097.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-098.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-099.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-100.png" />
		<figcaption>
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-101.png" />
		<figcaption>
		完成了！工作列表中不再有页面，也没有任何活动项。
		注意所有元数据现在都对齐了，因为所有可达对象既被 seen 又被 scanned。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-101.png" />
		<figcaption>
		你可能还注意到，在我们的遍历过程中，工作列表的顺序与图泛洪有所不同。
		图泛洪采用后进先出（栈式）顺序，而这里我们对页面使用先进先出（队列式）顺序。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-101.png" />
		<figcaption>
		这是有意为之。
		我们让 seen 对象在页面停留在队列上时积累起来，这样我们就能尽可能多地一次性处理它们。
		这就是我们能够在页 A 上一次性命中这么多对象的原因。
		有时候，懒惰也是一种美德。
		</figcaption>
	</figure>
	<figure class="carouselitem">
		<img src="greenteagc/greentea-102.png" />
		<figcaption>
		最后，我们像以前一样清除未访问的对象。
		</figcaption>
	</figure>
</div>
</div>

### 上高速公路

回到我们的驾驶类比。
我们终于上高速了吗？

让我们回顾之前的图泛洪图片。

<figure class="captioned">
	<img src="greenteagc/graphflood-path2.png" />
	<figcaption>
		原始图泛洪在堆上所走的路径需要 7 次独立的扫描。
	</figcaption>
</figure>

我们在很多地方跳来跳去，只做各处很小的一点工作。
Green Tea 所走的路径看起来则大不相同。

<figure class="captioned">
	<img src="greenteagc/greentea-path.png" />
	<figcaption>
		Green Tea 所走的路径只需要 4 次扫描。
	</figcaption>
</figure>

与之相比，Green Tea 在页 A 和页 B 上做更少但更长的从左到右的遍历。
这些箭头越长越好，而且对于更大的堆，这种效果会更明显。
*这就是 Green Tea 的魔力。*

这也是我们能够“上高速公路”的机会。

所有这些加起来，使得与微架构的适配更好。
我们现在可以更紧密地扫描邻近的对象，从而更有可能利用缓存并避免主内存访问。
同样，按页维护的元数据也更可能驻留在缓存中。
以页为单位而不是以对象为单位追踪工作，意味着工作列表更小，
对工作列表的压力减小也就意味着争用减少和 CPU 阻塞更少。

说到高速公路，我们也可以把隐喻中的发动机挂入以往无法触及的档位，因为现在我们可以使用向量硬件！

### 向量加速

如果你只对向量硬件有模糊的了解，可能会对我们如何在这里使用它感到困惑。
但除了常见的算术和三角运算外，最近的向量硬件支持两件对 Green Tea 很有价值的事情：
非常宽的寄存器，以及复杂的按位操作。

大多数现代 x86 CPU 支持 AVX-512，它具有 512 位宽的向量寄存器。
这足以把整页的元数据在两个寄存器中装下，直接放在 CPU 寄存器里，从而使 Green Tea 能够仅用几条直线指令就处理整页。
向量硬件长期以来就支持对整个向量寄存器进行基本的按位操作，但从 AMD Zen 4 和 Intel Ice Lake 开始，它还支持一种新的按位向量“瑞士军刀”指令，使 Green Tea 扫描过程中的关键步骤可以在几个 CPU 周期内完成。
这些结合在一起让我们能够给 Green Tea 的扫描循环加速。

在图泛洪中并没有这种选项，因为我们会在尺寸各异的对象之间跳转扫描。
有时你需要两个元数据位，有时需要一万位。
根本没有足够的可预测性或规则性来使用向量硬件。

如果你想深入了解细节，可以继续读下去！
否则可以直接跳到 [评估](#evaluation)。

#### AVX-512 扫描内核

要感受 AVX-512 垃圾回收扫描的样子，请看下面的示意图。

<figure class="captioned">
	<img src="greenteagc/avx512.svg" />
	<figcaption>
		用于扫描的 AVX-512 向量内核。
	</figcaption>
</figure>

这里面有很多内容，我们可能可以单独写一整篇博客来讲它如何工作。
现在我们先做一个高层次的拆解：

1. 首先我们获取页面的 "seen" 和 "scanned" 位。
   记住，这些是每页每个对象一个位，页面中所有对象大小相同。

2. 接着我们比较这两个位集合。
   它们的并集成为新的 "scanned" 位，而它们的差集就是“活动对象”位图，
   告诉我们在本次页面遍历中需要扫描哪些对象（相比之前的遍历）。

3. 我们对位图做差并把它“展开”，使得我们不再是一位对应一个对象，
   而是一位对应页面的每个字（8 字节）。
   我们称之为“活动字”位图。
   例如，如果页面保存 6 字（48 字节）对象，活动对象位图中的每一位都会被复制为活动字位图中的 6 位。
   如下所示：

<figure class="captioned">
	<div class="row"><pre>0 0 1 1 ...</pre> &rarr; <pre>000000 000000 111111 111111 ...</pre></div>
</figure>

4. 接着我们获取页面的指针/标量位图（pointer/scalar bitmap）。
   同样，这里的每一位对应页面的一个字（8 字节），它告诉我们该字是否存储一个指针。
   这部分数据由内存分配器管理。

5. 现在，我们取指针/标量位图和活动字位图的交集。
   结果就是“活动指针位图”：一个位图，告诉我们整页中属于任何未扫描可达对象的每个指针的位置。

6. 最后，我们可以遍历页面内存并收集所有指针。
   逻辑上，我们遍历活动指针位图中的每个置位位，
   在该字位置加载指针值，并将其写回到一个缓冲区，
   该缓冲区稍后会被用来标记对象为 seen 并将页面加入工作列表。
   使用向量指令，我们能够以 64 字节为单位进行处理，
   只需几条指令。

这之所以快速，部分原因是 `VGF2P8AFFINEQB` 指令，
它是 x86 的 “Galois Field New Instructions” 扩展的一部分，
以及我们之前提到的按位操作瑞士军刀指令。
它是整场表演的明星，因为它使我们可以在扫描内核中非常高效地执行步骤 (3)。
它执行按位的[仿射变换](https://en.wikipedia.org/wiki/Affine_transformation)，
将向量中每个字节视为自身的 8 位数学向量，并用一个 8x8 的位矩阵去乘它。
这一切都在 Galois 域（GF(2)）上进行，这意味着乘法是 AND，加法是 XOR。
这样我们就可以为每个对象大小定义一些 8x8 的位矩阵，精确地执行我们需要的 1:n 位展开。

如需完整汇编代码，请参见 [此文件](https://cs.opensource.google/go/go/+/master:src/internal/runtime/gc/scan/scan_amd64.s;l=23;drc=041f564b3e6fa3f4af13a01b94db14c1ee8a42e0)。
“展开器”针对每个尺寸类使用不同的矩阵和不同的置换，因此它们存在于由[代码生成器](https://cs.opensource.google/go/go/+/master:src/internal/runtime/gc/scan/mkasm.go;drc=041f564b3e6fa3f4af13a01b94db14c1ee8a42e0)生成的[单独文件](https://cs.opensource.google/go/go/+/master:src/internal/runtime/gc/scan/expand_amd64.s;drc=041f564b3e6fa3f4af13a01b94db14c1ee8a42e0)中。
除了展开函数外，代码量其实不多。
大部分实现被寄存器中直接操作数据的事实极大地简化了。
希望很快这些汇编代码[能被 Go 代码替代](https://github.com/golang/go/issues/73787)！

致谢 Austin Clements 提出这一过程。
这真是既酷又快！

### 评估

这就是它的工作原理。
它究竟能带来多大帮助？

答案是可能非常可观。
即便没有向量优化，我们在基准套件中也看到垃圾回收 CPU 成本降低在 10% 到 40% 之间。
例如，如果某个应用在垃圾回收上花费 10% 的时间，那么这将转化为整体 CPU 使用率下降 1% 到 4% 不等，具体取决于工作负载的细节。
10% 的 GC CPU 时间减少大约是常见的改进幅度。
（有关部分细节，请参见 [GitHub issue](https://github.com/golang/go/issues/73581)。）

我们已在 Google 内部推广 Green Tea，并在规模上观察到类似结果。

我们仍在逐步推出向量增强，但基准和早期结果表明这将再带来约 10% 的 GC CPU 降低。

虽然大多数工作负载在某种程度上会受益，但也有一些不会。

Green Tea 的假设基础是我们能够在单页上积累足够多要扫描的对象，以抵消积累过程的开销。
如果堆具有非常规则的结构：相同大小的对象位于对象图中相似的深度，那么这种假设显然成立。
但有些工作负载常常导致我们每页一次只需扫描单个对象。
这可能比图泛洪更糟，因为我们可能在尝试在页上积累对象而失败的同时做了更多工作。

Green Tea 的实现对只在页上有单个对象需要扫描的页面做了特判。
这有助于减少回退，但不能完全消除它们。

然而，要超过图泛洪并不需要太多的每页积累——比你想象的要少。
这一工作的一个意外结果是：每页只扫描 2% 的对象时，就可能比图泛洪有改进。

### 可用性

Green Tea 已作为实验包含在近期的 Go 1.25 版本中，构建时通过设置环境变量 `GOEXPERIMENT=greenteagc` 可启用。
这不包含前述的向量加速。

我们预计在 Go 1.26 中将其设为默认垃圾回收器，但你仍然可以在构建时用 `GOEXPERIMENT=nogreenteagc` 选择禁用。
Go 1.26 还会在较新的 x86 硬件上加入向量加速，并包含基于我们迄今收集反馈的一系列调整和改进。

如果可以的话，我们鼓励你在 Go 的 tip-of-tree 上尝试！
如果你更愿意使用 Go 1.25，我们也非常希望收到你的反馈。
参见[这个 GitHub 评论](https://github.com/golang/go/issues/73581#issuecomment-2847696497)，里面有一些关于我们感兴趣的诊断信息的细节（如果你能共享）以及首选的反馈渠道。

## 旅程

在结束本文之前，让我们花一点时间谈谈让我们走到这一步的旅程。
也就是这项技术背后的人性因素。

Green Tea 的核心看起来像是一个单一而简单的想法。
像是某个人灵光一闪的火花。

但事实并非如此。
Green Tea 是多年间许多人共同努力与思想的结果。
Go 团队的若干成员为这些想法做出了贡献，包括 Michael Pratt、Cherry Mui、David Chase 和 Keith Randall。
当年在 Intel 的 Yves Vandriessche 提供的微架构见解也极大地帮助了设计探索的方向。
有许多想法行不通，也有许多细节需要弄清楚。
只是为了让这个看似单一且简单的想法变得可行，付出了很多努力。

<figure class="captioned">
	<img src="greenteagc/timeline.png" />
	<figcaption>
		描绘我们在到达今天这一阶段之前所尝试的一部分想法的时间线。
	</figcaption>
</figure>

这一想法的萌芽可以追溯到 2018 年。
有趣的是，团队中的每个人都觉得最初的想法是别人先想到的。

Green Tea 的名字来自 2024 年，当时 Austin 在日本喝了大量抹茶并在咖啡馆中走访时推敲出早期版本的原型！
该原型表明 Green Tea 的核心思想是可行的。
从那里我们便一路竞速前进。

在 2025 年，随着 Michael 将 Green Tea 实现并投入生产，想法不断演进和变化。

之所以需要如此多的协作探索，是因为 Green Tea 不仅仅是一个算法，而是一个完整的设计空间。
这是我们认为单靠任何一个人都无法独立走完的过程。
仅仅有想法还不够，你需要敲定细节并证明其可行性。
现在我们做到了，我们可以继续迭代改进。

Green Tea 的未来充满光明。

再次提醒，请通过设置 `GOEXPERIMENT=greenteagc` 试用并告诉我们你的体验！
我们对这项工作非常兴奋，并希望听到你的反馈！

<script src="greenteagc/carousel.js"></script>

# Go 的内存模型

<sub>版本：2022 年 6 月 6 日</sub>

## 介绍

Go 内存模型规定了在何种条件下，一个 goroutine 中对某个变量的读取可以保证看到在另一个 goroutine 中对同一变量的写入所产生的值。

### 建议

对被多个 goroutine 同时访问并修改的数据，程序必须对这些访问进行序列化。

要序列化访问，应使用通道操作或其他同步原语保护数据，例如 `sync` 和 `sync/atomic` 包中提供的那些原语。

如果你必须阅读本文档其余部分才能理解你的程序行为，那你就是太聪明了。

别太聪明。

### 非正式概述

Go 对内存模型的处理与语言的其他部分类似，目标是保持语义简单、易于理解且有用。本节给出该方法的一般性概述，对大多数程序员来说已足够。内存模型在下一节中以更形式化的方式给出。

**数据竞争（data race）** 定义为对同一内存位置的写操作与另一个对该位置的读或写操作并发发生，除非涉及的所有访问都是 `sync/atomic` 包提供的原子数据访问。如前所述，强烈建议程序员使用适当的同步来避免数据竞争。如果没有数据竞争，Go 程序的行为就好像所有 goroutine 被复用到单个处理器上执行一样。此性质有时称为 DRF-SC：数据竞争自由（Data-Race-Free）的程序按顺序一致（sequentially consistent）的方式执行。

虽然程序员应尽量避免数据竞争，但 Go 实现对数据竞争的响应能力有一定限制。实现总是可以通过报告竞态并终止程序来响应数据竞争。除此之外，对单字（machine-word）或小于机器字大小的内存位置的每次读取，必须观察到该位置上实际写入过且尚未被覆盖的某个值（该写操作可能来自并发执行的 goroutine）。这些实现上的约束使得 Go 更像 Java 或 JavaScript —— 大多数竞态的结果是有限且可预期的 —— 而不像 C/C++ 那样，后者对含竞态的程序的语义完全未定义，编译器可以做任意变换。Go 的做法旨在使出错的程序更可靠、便于调试，同时坚持认为竞态是错误并且工具可以诊断并报告它们。

## 内存模型

下面对 Go 内存模型的形式化定义紧随 Hans-J. Boehm 和 Sarita V. Adve 在 PLDI 2008 上发表的论文 “[Foundations of the C++ Concurrency Memory Model](https://dl.acm.org/doi/10.1145/1375581.1375591)” 的方法。对数据竞争自由程序的定义以及对无竞态程序给出的顺序一致性保证，与该工作中的等价。

内存模型描述了程序执行的要求。程序执行由若干 goroutine 执行组成，而每个 goroutine 执行又由若干内存操作组成。

一个**内存操作（memory operation）**用四个要素建模：

- 其种类，表明它是普通的数据读取、普通的数据写入，还是诸如原子数据访问、互斥锁操作或通道操作这样的**同步操作（synchronizing operation）**；
- 它在程序中的位置；
- 被访问的内存位置或变量；
- 该操作读取或写入的值。

有些内存操作是**类读（read-like）**的，包括读取、原子读取、互斥锁锁定（mutex lock）和通道接收。其他内存操作是**类写（write-like）**的，包括写入、原子写入、互斥锁解锁（mutex unlock）、通道发送和通道关闭。某些操作，例如原子比较并交换（compare-and-swap），既是类读又是类写。

一个 **goroutine 执行** 被建模为单个 goroutine 执行的一组内存操作。

**要求 1**：每个 goroutine 中的内存操作必须与该 goroutine 的一个正确的顺序执行相对应，给定从内存读取和写入的值。该执行必须与由 Go 语言规范为控制流构造以及表达式求值顺序所规定的 **sequenced before（先后顺序）** 关系列一致。

一个 Go **程序执行** 被建模为一组 goroutine 执行，并带有一个映射 _W_，该映射指定每个类读操作（read-like operation）从哪个类写操作（write-like operation）读取。 （同一程序的不同运行可能对应不同的程序执行。）

**要求 2**：对某一给定的程序执行，当将映射 _W_ 限制到同步操作时，必须能够由某个与序列关系及这些操作读取/写入的值一致的隐式同步操作的全序来解释。

**synchronized before（同步之前）** 关系是对同步内存操作的偏序，由 _W_ 导出。如果一个同步类读操作 _r_ 观察到了一个同步类写操作 _w_（即 _W_(_r_) = _w_），则称 _w_ 在 _r_ 之前被同步（_w_ synchronized before _r_）。非正式地说，synchronized before 关系是前述隐式全序的一个子集，限于 _W_ 直接观察到的信息。

**happens before（发生在之前）** 关系定义为 sequenced before 与 synchronized before 二者并集的传递闭包。

**要求 3**：对于内存位置 _x_ 上的一个普通（非同步）数据读取 _r_，_W_(_r_) 必须是对 _r_ 可**见的（visible）**写 _w_，其中“可见”意味着同时满足：

1. _w_ happens before _r_（_w_ 在 _r_ 之前发生）；
2. 在任何在 _r_ 之前发生的对 _x_ 的其他写 _w'_ 之前，_w_ 不在它们之前发生（也就是说不存在写 _w'_，使得 _w_ happens before _w'_ 且 _w'_ happens before _r_）。

对于内存位置 _x_ 上的**读-写数据竞争（read-write data race）**，指的是一个对 _x_ 的类读操作 _r_ 与一个对 _x_ 的类写操作 _w_（至少其中之一为非同步操作），两者在 happens before 关系上无序（即既不是 _r_ happens before _w_，也不是 _w_ happens before _r_）。

对于内存位置 _x_ 上的**写-写数据竞争（write-write data race）**，指的是两个对 _x_ 的类写操作 _w_ 和 _w'_（至少其中之一为非同步操作），两者在 happens before 上无序。

注意，如果内存位置 _x_ 上不存在读-写或写-写的数据竞争，那么对 _x_ 的任一读取 _r_ 的 _W_(_r_) 只有一个可能值：即在 happens before 顺序中紧接其前面的那个写 _w_。

更一般地可以证明：任何数据竞争自由（data-race-free）的 Go 程序（即不存在读写或写写数据竞争的程序执行）其结果只能由某种按顺序一致的 goroutine 执行交错来解释。（证明与 Boehm 和 Adve 的论文第 7 节相同。）这一性质称为 DRF-SC（Data-Race-Free Sequential Consistency）。

形式定义的目的在于与其他语言对无竞态程序所提供的 DRF-SC 保证相匹配，包括 C、C++、Java、JavaScript、Rust 和 Swift。

某些 Go 语言操作（例如 goroutine 的创建和内存分配）也作用为同步操作。这些操作对 synchronized-before 偏序的影响在下面的“同步（Synchronization）”一节中有记录。各个包应负责为其自身的操作提供类似的文档说明。

## 含数据竞争程序的实现限制

前一节给出了数据竞争自由程序执行的形式化定义。本节以非正式方式描述对包含竞态的程序，Go 实现必须提供的语义。

任何实现都可以在检测到数据竞争时报告竞态并中止程序。使用 ThreadSanitizer（通过 `go build -race` 访问）的实现正是这样做的。

对数组、结构体或复数的读取可能被实现为分别读取每个子值（数组元素、结构体字段或实/虚部），且读取的顺序可以为任意。同样，对数组、结构体或复数的写入也可能被实现为对每个子值的写入，顺序为任意。

对存放在不大于机器字大小（machine word）的内存位置 _x_ 的一次读取 _r_，必须观察到某个写 _w_，其满足：_r_ 不在 _w_ 之前发生，且不存在写 _w'_ 使得 _w_ happens before _w'_ 且 _w'_ happens before _r_。换言之，每次读取必须观察到由先前或并发写所写的某个值。

此外，禁止观察非因果和“凭空出现（out of thin air）”的写入。

对大于单个机器字的内存位置的读取，鼓励但不要求实现满足与字大小内存位置相同的语义，即观察单个允许的写 _w_。出于性能原因，实现在这些较大操作上可以将其视为一组按不确定顺序的机器字大小的独立操作。这意味着对多字（multiword）数据结构上的竞态可能导致观察到不对应于单个写的非一致值。当值依赖于内部（指针，长度）对或（指针，类型）对的一致性时（例如 interface 值、map、slice 和 string 在多数 Go 实现中），这样的竞态可能进一步导致任意的内存损坏。

“错误的同步”一节给出了一些不正确同步的示例。

“错误的编译”一节给出了一些对实现限制的示例。

## 同步

### 初始化

程序初始化在单个 goroutine 中运行，但该 goroutine 可能创建其他 goroutine，而这些 goroutine 会并发运行。

如果包 `p` 导入包 `q`，则包 `q` 的 `init` 函数执行完成发生在包 `p` 的任何 `init` 的开始之前。

所有 `init` 函数的完成在函数 `main.main` 的开始之前被同步。

### Goroutine 的创建

启动新 goroutine 的 `go` 语句在该 goroutine 的执行开始之前是同步发生的。

例如，在此程序中：

```go
var a string

func f() {
	print(a)
}

func hello() {
	a = "hello, world"
	go f()
}
```

调用 `hello` 将在未来某个时刻（可能在 `hello` 返回之后）打印 `"hello, world"`。

### Goroutine 的销毁

一个 goroutine 的退出并不保证在程序中的任何事件之前被同步。例如，在此程序中：

```go
var a string

func hello() {
	go func() { a = "hello" }()
	print(a)
}
```

对 `a` 的赋值后没有随之发生的同步事件，因此它不保证能被任何其他 goroutine 观察到。事实上，激进的编译器甚至可能删除整个 `go` 语句。

如果必须让另一个 goroutine 观察到某个 goroutine 的效果，应使用锁或通道通信等同步机制来建立相对顺序。

### 通道通信

通道通信是 goroutine 之间主要的同步方法。特定通道上的每一次发送通常会与该通道上的对应接收相配对（通常在不同的 goroutine 中）。

对通道的发送在对应的从该通道接收完成之前被视为已同步发生。

例如程序：

```go
var c = make(chan int, 10)
var a string

func f() {
	a = "hello, world"
	c <- 0
}

func main() {
	go f()
	<-c
	print(a)
}
```

保证会打印 `"hello, world"`。对 `a` 的写在对 `c` 的发送之前被序列化，发送在对应的接收完成之前被同步，而接收又在 `print` 之前序列化，所以 `print` 会看到写入的值。

_关闭通道在接收返回零值之前是被同步的。_

在前例中，将 `c <- 0` 替换为 `close(c)` 会得到具有相同保证行为的程序。

对无缓冲通道的接收在对应发送完成之前被同步。

下面的程序（与上例相同，但发送与接收语句互换并使用无缓冲通道）：

```go
var c = make(chan int)
var a string

func f() {
	a = "hello, world"
	<-c
}

func main() {
	go f()
	c <- 0
	print(a)
}
```

也保证会打印 `"hello, world"`。对 `a` 的写在对 `c` 的接收之前被序列化，接收在对应的发送完成之前被同步，发送又在 `print` 之前序列化。

如果通道是有缓冲的（例如 `c = make(chan int, 1)`），则程序不再被保证打印 `"hello, world"`。（它可能打印空字符串、崩溃或执行其它操作。）

_对于容量为 C 的通道，第 k 个接收在第 k+C 个发送完成之前被同步。_

该规则将前述规则推广到有缓冲通道。它允许用有缓冲通道模拟计数信号量：通道中项目的数量对应于活动使用的数量，通道的容量对应于最大并发使用数，发送一个项目获取（acquire）信号量，接收一个项目释放（release）信号量。这是限制并发的常见惯用法。

下面的程序对工作列表中的每一项都启动一个 goroutine，但 goroutine 使用 `limit` 通道进行协调，确保最多有三个 goroutine 同时运行工作函数。

```go
var limit = make(chan int, 3)

func main() {
	for _, w := range work {
		go func(w func()) {
			limit <- 1
			w()
			<-limit
		}(w)
	}
	select{}
}
```

### 锁

`sync` 包实现了两种锁数据类型：`sync.Mutex` 和 `sync.RWMutex`。

*对于任意 `sync.Mutex` 或 `sync.RWMutex` 变量 `l`，若 *n* < *m*，则第 *n* 次对 `l.Unlock()` 的调用在第 *m* 次 `l.Lock()` 返回之前被同步。*

例如程序：

```go
var l sync.Mutex
var a string

func f() {
	a = "hello, world"
	l.Unlock()
}

func main() {
	l.Lock()
	go f()
	l.Lock()
	print(a)
}
```

保证会打印 `"hello, world"`。在 `f` 中的第一次 `l.Unlock()` 在 `main` 中的第二次 `l.Lock()` 返回之前被同步，而第二次 `l.Lock()` 的返回又在 `print` 之前序列化。

*对于任意对 `sync.RWMutex` 变量 `l` 的 `l.RLock` 调用，存在一个 *n*，使得第 *n* 次对 `l.Unlock` 的调用在 `l.RLock` 返回之前被同步，且匹配的 `l.RUnlock` 调用在第 *n*+1 次 `l.Lock` 返回之前被同步。*

_成功的 `l.TryLock`（或 `l.TryRLock`）相当于一次 `l.Lock`（或 `l.RLock`）调用。失败的调用对内存模型没有任何同步效果。从内存模型的角度看，`l.TryLock`（或 `l.TryRLock`）可以被视为即便互斥量 `l` 是解锁的也可能返回 false。_

### Once

`sync` 包通过 `Once` 类型为多 goroutine 下的初始化提供了安全机制。多个线程可以对同一个函数 `f` 调用 `once.Do(f)`，但只有一个会运行 `f()`，其他调用会阻塞直到 `f()` 返回。

_来自 `once.Do(f)` 的一次 `f()` 调用的完成在任何对同一 `once.Do(f)` 的返回之前被同步。_

在下列程序中：

```go
var a string
var once sync.Once

func setup() {
	a = "hello, world"
}

func doprint() {
	once.Do(setup)
	print(a)
}

func twoprint() {
	go doprint()
	go doprint()
}
```

调用 `twoprint` 将仅调用一次 `setup`。`setup` 函数会在任意一次 `print` 之前完成。结果是 `"hello, world"` 会被打印两次。

### 原子值

`sync/atomic` 包中的 API 统称为“原子操作”，可用于同步不同 goroutine 的执行。如果原子操作 _A_ 的效果被原子操作 _B_ 观察到，则 _A_ 在 _B_ 之前被同步。程序中执行的所有原子操作都表现得好像它们按某个顺序以顺序一致的方式执行。

上述定义与 C++ 的顺序一致原子（sequentially consistent atomics）和 Java 的 `volatile` 变量具有相同语义。

### 终结器（Finalizers）

`runtime` 包提供 `SetFinalizer`，用于在某个对象不再被程序可达时添加一个终结器（finalizer）。调用 `SetFinalizer(x, f)` 在终结调用 `f(x)` 之前被同步。

### 其他机制

`sync` 包还提供额外的同步抽象，包括 [条件变量（Cond）](https://pkg.go.dev/sync/#Cond)、[无锁映射（Map）](https://pkg.go.dev/sync/#Map)、[对象池（Pool）](https://pkg.go.dev/sync/#Pool) 和 [等待组（WaitGroup）](https://pkg.go.dev/sync/#WaitGroup)。每个这些类型的文档都说明了它们关于同步所作的保证。

提供同步抽象的其他包也应当记录它们所做的保证。

## 错误的同步

有竞态的程序是错误的，可能表现出非顺序一致的执行。特别要注意的是，读取 _r_ 可能观察到任意与 _r_ 并发执行的写 _w_ 所写的值。即使发生这种情况，也并不意味着在 _r_ 之后发生的读取会观察到发生在 _w_ 之前的写。

在此程序中：

```go
var a, b int

func f() {
	a = 1
	b = 2
}

func g() {
	print(b)
	print(a)
}

func main() {
	go f()
	g()
}
```

可能出现 `g` 打印 `2` 然后 `0` 的情况。

这一事实使得一些常见的惯用法失效。

双重检查锁（double-checked locking）试图避免同步开销。例如，之前的 `twoprint` 程序可能被错误地写成：

```go
var a string
var done bool

func setup() {
	a = "hello, world"
	done = true
}

func doprint() {
	if !done {
		once.Do(setup)
	}
	print(a)
}

func twoprint() {
	go doprint()
	go doprint()
}
```

但不能保证在 `doprint` 中观察到对 `done` 的写会隐含观察到对 `a` 的写。此版本可能（错误地）打印空字符串而不是 `"hello, world"`。

另一种不正确的惯用法是忙等待（busy waiting）某个值，例如：

```go
var a string
var done bool

func setup() {
	a = "hello, world"
	done = true
}

func main() {
	go setup()
	for !done {
	}
	print(a)
}
```

同前文所述，在 `main` 中观察到对 `done` 的写并不保证能观察到对 `a` 的写，因此该程序也可能打印空字符串。更糟的是，也不能保证 `main` 会观察到对 `done` 的写，因为两线程之间没有任何同步事件。`main` 中的循环并不保证会结束。

还有更微妙的变体，例如：

```go
type T struct {
	msg string
}

var g *T

func setup() {
	t := new(T)
	t.msg = "hello, world"
	g = t
}

func main() {
	go setup()
	for g == nil {
	}
	print(g.msg)
}
```

即便 `main` 观察到 `g != nil` 并退出循环，也不能保证它会观察到 `g.msg` 的已初始化值。

在所有这些示例中，解决方法相同：使用显式的同步。

## 错误的编译

Go 内存模型对编译器优化做了与对 Go 程序类似的限制。某些在单线程程序中有效的编译器优化在所有 Go 程序中并不总是有效。尤其是，编译器不得引入程序中不存在的写操作，不得允许单次读取观察到多个值，也不得允许单次写写入多重值。

下面所有示例均假设 `*p` 和 `*q` 指向可被多个 goroutine 访问的内存位置。

不向无竞态程序引入数据竞争意味着不得将写操作从包含它们的条件语句中移出。例如，编译器不得将下列程序的条件倒置：

```go
*p = 1
if cond {
	*p = 2
}
```

即，编译器不得将其改写为：

```go
*p = 2
if !cond {
	*p = 1
}
```

若 `cond` 为 false 且另一个 goroutine 正在读取 `*p`，则在原始程序中，另一个 goroutine 只能观察到 `*p` 的先前值或 `1`。在被改写的程序中，另一个 goroutine 可能观察到 `2`，这是原程序中不可能观察到的。

不向无竞态程序引入数据竞争还意味着不得假定循环会终止。例如，编译器通常不得将对 `*p` 或 `*q` 的访问移到下列程序的循环之前：

```go
n := 0
for e := list; e != nil; e = e.next {
	n++
}
i := *p
*q = 1
```

如果 `list` 指向一个循环链表，则原程序可能永远不会访问 `*p` 或 `*q`，而被改写的程序会访问它们。（如果编译器能证明 `*p` 的访问不会导致 panic，则将 `*p` 提前是安全的；将 `*q` 提前还需要编译器证明没有其他 goroutine 能访问 `*q`。）

不向无竞态程序引入数据竞争还意味着不得假定被调用的函数总是返回或不包含同步操作。例如，编译器不得将对 `*p` 或 `*q` 的访问移到下列函数调用之前（至少在不了解 `f` 的精确行为之前不得如此移动）：

```go
f()
i := *p
*q = 1
```

如果该调用永不返回，则原程序永远不会访问 `*p` 或 `*q`，而被改写的程序会访问它们。而如果该调用包含同步操作，原程序可能建立在访问 `*p` 和 `*q` 之前的 happens before 边，但被改写的程序不会。

不允许单次读取观察多个值还意味着不得从共享内存重新加载局部变量。例如，编译器不得丢弃 `i` 并在下列程序中在后面再次从 `*p` 重新加载它：

```go
i := *p
if i < 0 || i >= len(funcs) {
	panic("invalid function index")
}
... complex code ...
// compiler must NOT reload i = *p here
funcs[i]()
```

如果复杂代码需要许多寄存器，一个面向单线程程序的编译器可能会丢弃 `i` 而在 `funcs[i]()` 之前重新加载 `i = *p`。Go 编译器不得这样做，因为 `*p` 的值可能已改变。（编译器可以将 `i` 溢出到栈上以保存它。）

不允许单次写写入多重值也意味着不得使用将要被写入的局部变量所在的内存作为写之前的临时存储。例如，编译器不得使用 `*p` 作为临时存储来改写下列程序：

```go
*p = i + *p/2
```

即，不得将其重写为：

```go
*p /= 2
*p += i
```

如果 `i` 和 `*p` 初始都为 2，原始代码执行 `*p = 3`，所以并发线程只能从 `*p` 读到 2 或 3。被改写的代码会先做 `*p = 1` 然后 `*p = 3`，使并发线程也能读到 1。

请注意：以上这些优化在 C/C++ 编译器中是允许的：如果 Go 编译器与 C/C++ 编译器共用后端，则它必须注意禁用那些对 Go 无效的优化。

注意，禁止引入数据竞争的限制在编译器能够证明这些竞态不会影响目标平台上正确执行的情况下不适用。例如，在几乎所有 CPU 上，可以将

```go
n := 0
for i := 0; i < m; i++ {
	n += *shared
}
```

重写为：

```go
n := 0
local := *shared
for i := 0; i < m; i++ {
	n += local
}
```

前提是可以证明对 `*shared` 的访问不会导致故障，因为额外的读取不会影响任何现有的并发读取。另一方面，这种重写在源到源的转换器中并不总是有效。

## 结论

编写数据竞争自由程序的 Go 程序员可以依赖于这些程序按顺序一致地执行，这与几乎所有其他现代编程语言相同。

对于含有竞态的程序，程序员和编译器都应记住那句建议：别太聪明。

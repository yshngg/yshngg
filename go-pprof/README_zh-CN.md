# pprof

pprof 是一个用于可视化和分析性能剖析（profiling）数据的工具。

pprof 读取以 `profile.proto` 格式存储的一组剖析样本，并生成报告以便可视化和帮助分析数据。它既能生成文本报告，也能生成图形报告（通过 dot 可视化包）。

`profile.proto` 是一个描述一组调用栈和符号化信息的 protocol buffer。一个常见用途是表示通过统计采样得到的一组调用栈。该格式在 `proto/profile.proto` 文件中有描述。有关 protocol buffers 的详细信息，请参见 [https://developers.google.com/protocol-buffers](https://developers.google.com/protocol-buffers) 。

剖析（Profiles）可以从本地文件读取，也可以通过 http 获取。相同类型的多个剖析可以聚合或比较。

如果剖析样本包含机器地址，pprof 可以通过本地 binutils 工具（`addr2line` 和 `nm`）对地址进行符号化（symbolize）。

# pprof 剖析（profiles）

pprof 针对 `profile.proto` 格式的数据进行操作。每个剖析是若干样本的集合，其中每个样本关联到一个位于位置层次结构中的位置点（一或多个数值），以及一组标签。通常这些剖析表示通过对程序进行统计采样收集到的数据，因此每个样本描述了程序的调用栈以及在某位置收集到的样本数量或取值。pprof 对剖析语义保持中立（agnostic），因此也可以用于其他用途。pprof 生成的报告的解释取决于剖析来源定义的语义。

# 使用模式

使用 `pprof` 有几种不同方式。

## 报告生成

如果在命令行上指定了报告格式：

```
pprof <format> [options] source
```

pprof 将生成指定格式的报告并退出。格式可以是文本或图形。下面会详细说明支持的格式、选项和来源（source）。

## 交互式终端使用

如果未指定格式：

```
pprof [options] source
```

pprof 会启动一个交互式 shell，用户可以在其中输入命令。输入 `help` 可获得在线帮助。

## Web 界面

如果在命令行上指定了 `host:port`：

```
pprof -http=[host]:[port] [options] source
```

pprof 会在指定端口上开始提供 HTTP 服务。在浏览器中访问对应端口的 HTTP URL（通常为 `http://<host>:<port>/`）以查看界面。

# 细节

pprof 的目标是为某个剖析生成报告。报告由从剖析样本重建的位置层次结构（location hierarchy）生成。每个位置（location）包含两个值：

- _flat_：该位置本身的值。
- _cum_：该位置及其所有子孙位置的值之和（累积值）。

在一个样本中如果某位置出现多次（例如递归函数），每个位置只计数一次。

## 选项

_options_ 配置报告的内容。每个选项有一个值，值可以是布尔、数值或字符串。虽然一次只能指定一种格式，但大多数选项可以相互独立地选择。

一些常见的 pprof 选项有：

- **-flat** [默认], **-cum**：在文本报告中分别根据 flat 或 cum 值对条目进行排序。
- **-functions** [默认], **-filefunctions**, **-files**, **-lines**, **-addresses**：以指定的粒度生成报告。
- **-noinlines**：将内联函数的属性归因于它们的第一个非内联调用者。例如，像 `pprof -list foo -noinlines profile.pb.gz` 这样的命令可以用来生成注释过的源代码清单，并将内联函数的度量归因到非内联调用行。
- **-nodecount=_int_：** 报告中最大的条目数。pprof 只会打印这么多条目，并使用启发式方法选择要剪裁的条目。
- **-focus=_regex_：** 仅包含包含匹配 _regex_ 的报告条目的样本。
- **-ignore=_regex_：** 不包含包含匹配 _regex_ 的报告条目的样本。
- **-show_from=_regex_：** 不显示第一个匹配 _regex_ 之上的条目。
- **-show=_regex_：** 仅显示匹配 _regex_ 的条目。
- **-hide=_regex_：** 不显示匹配 _regex_ 的条目。

剖析中的每个样本可能包含多个值，表示与该样本相关的不同实体。pprof 报告包含单一的样本值，按惯例是报告中最后指定的那个值。`sample_index=` 选项选择要使用的值，可以设置为一个数字（从 0 到值的数量-1）或样本值的名称。

样本值是与单位关联的数值。如果 pprof 能识别这些单位，它会尝试将值缩放到适合可视化的单位。`unit=` 选项将强制使用特定单位。例如，`unit=sec` 会强制将任何时间值以秒为单位报告。pprof 能识别大多数常见的时间和内存大小单位。

## 标签（Tags）

剖析里的样本可能带有标签（tags）。这些标签有名称和值。值可以是数值或字符串；数值可以关联单位。标签被用作对样本值进行拆分的额外维度。标签最常见的用途是基于标签值从剖析中选择样本。pprof 也支持在可视化时使用标签。

### 标签过滤（Tag filtering）

`-tagfocus` 选项是基于标签值从剖析中选择数据时最常用的选项。它的语法为 **-tagfocus=_regex_** 或 **-tagfocus=_range_：**，将数据限制为具有与正则表达式匹配或在范围内的标签的样本。`-tagignore` 选项具有相同语法，可用来过滤掉具有匹配标签的样本。如果同时指定 `-tagignore` 和 `-tagfocus` 且两者都匹配某个样本，那么该样本将被丢弃。

当使用 `-tagfocus=regex` 和 `-tagignore=regex` 时，正则表达式会与每个标签的每个值进行比较。如果指定的值像 `regex1,regex2`，则只有具有匹配 `regex1` 且具有匹配 `regex2` 的标签值的样本才会被保留。

除了能够对标签值进行过滤外，还可以使用 `-tagfocus=tagName=value` 的表示法来指定某个值必须关联到特定的标签名。在这里，`tagName` 必须与标签的名称精确匹配，值可以是正则表达式或范围。如果指定的值像 `regex1,regex2`，则当两个标签值之一存在时样本匹配。

下面是一些说明如何使用 `-tagfocus` 的例子：

- `-tagfocus 128kb:512kb` 接受一个样本当且仅当它有任何数值标签的内存值在该范围内。
- `-tagfocus mytag=128kb:512kb` 接受一个样本当且仅当它有一个数值标签 `mytag`，其内存值在指定范围内。不能表示 `-tagfocus mytag=128kb:512kb,16kb:32kb` 或 `-tagfocus mytag=128kb:512kb,mytag2=128kb:512kb`。仅支持单一值或范围用于数值标签。
- `-tagfocus someregex` 接受一个样本当且仅当它有任意字符串标签的 `tagName:tagValue` 字符串匹配指定的正则表达式。将来，这会改为接受当且仅当它有任意字符串标签的 `tagValue` 字符串匹配指定 regexp。
- `-tagfocus mytag=myvalue1,myvalue2` 当两个标签值中的任一存在时匹配。

### 标签可视化（Tag visualization）

要列出剖析中可用的标签及其值，使用 **-tags** 选项。它会输出可用的标签及其值，以及按每个标签值对样本值的拆分（breakdown）。

pprof 的调用图（callgraph）报告，例如 `-web` 或原始 `-dot`，会自动将所有标签的值可视化为图中的伪节点。使用 `-tagshow` 和 `-taghide` 选项来限制显示哪些标签。这些选项接受与标签名匹配的正则表达式，分别用于显示或隐藏标签。

选项 `-tagroot` 和 `-tagleaf` 可以用于为剖析样本创建伪栈帧。例如，`-tagroot=mytag` 会在配置文件调用树的根部为相应样本添加以该标签值为名的栈帧。类似地，`-tagleaf=mytag` 会在每个样本的叶节点处添加这样的栈帧。这些选项在以树格式（例如 `-http` 模式 web UI 中的树视图）可视化剖析时很有用。

## 文本报告

pprof 文本报告以文本格式显示位置层次结构。

- **-text：** 打印位置条目，每行一个，包括 flat 和 cum 值。
- **-tree：** 打印每个位置条目及其前驱和后继。
- **-peek=_regex_：** 打印位置条目及其所有前驱和后继，不剪裁任何条目。
- **-traces：** 打印每个样本，每行一个位置。

## 图形报告

pprof 可以生成 DOT 格式的图形报告，并使用 graphviz 包将其转换为多种格式。

这些报告将位置层次结构表示为图，报告条目表示为节点。节点会使用启发式方法删除以限制图的大小，受 `nodecount` 选项控制。

- **-dot：** 生成 .dot 格式的报告。所有其它格式都由此生成。
- **-svg：** 生成 SVG 格式的报告。
- **-web：** 在临时文件中生成 SVG 格式的报告，并启动网页浏览器查看它。
- **-png, -jpg, -gif, -pdf：** 生成这些格式的报告。

### 解释调用图（Interpreting the Callgraph）

- **节点颜色（Node Color）**：
  - 大的正向累积值为红色。
  - 大的负向累积值为绿色；负值最有可能在剖析比较时出现，详见[比较剖析](#comparing-profiles)部分。
  - 接近零的累积值为灰色。

- **节点字体大小（Node Font Size）**：
  - 字体越大表示 flat 绝对值越大。
  - 字体越小表示 flat 绝对值越小。

- **边权重（Edge Weight）**：
  - 较粗的边表示沿该路径使用的资源更多。
  - 较细的边表示沿该路径使用的资源较少。

- **边颜色（Edge Color）**：
  - 大的正值为红色。
  - 大的负值为绿色。
  - 接近零的值为灰色。

- **虚线边（Dashed Edges）**：连接的两个位置之间有一些位置被移除。

- **实线边（Solid Edges）**：一个位置直接调用另一个位置。

- **“(inline)” 边标记：** 调用已被内联到调用者中。

考虑以下示例图：

![callgraph](images/callgraph.png)

- 对于节点：
  - `(*Rand).Read` 的 flat 值小且 cum 值小，因为字体小且节点为灰色。
  - `(*compressor).deflate` 的 flat 值大且 cum 值大，因为字体大且节点为红色。
  - `(*Writer).Flush` 的 flat 值小但 cum 值大，因为字体小但节点为红色。

- 对于边：
  - `(*Writer).Write` 与 `(*compressor).write` 之间的边：
    - 由于是虚线边，表示在这两个节点之间有节点被移除。
    - 由于边粗且为红色，表示在这些两个节点之间的调用栈中使用了更多资源。

  - `(*Rand).Read` 与 `read` 之间的边：
    - 由于是虚线边，表示在这两个节点之间有节点被移除。
    - 由于边细且为灰色，表示在这些调用栈之间使用的资源较少。

  - `read` 与 `(*rngSource).Int63` 之间的边：
    - 由于是实线边，表示两个节点之间没有其他节点（即为直接调用）。
    - 由于边细且为灰色，表示在这些调用栈之间使用的资源较少。

## 注释代码（Annotated code）

pprof 还可以生成带有样本注释的源代码报告。为此，源代码或二进制文件必须在本地可用，并且剖析必须包含具有适当细节级别的数据。

pprof 会在其当前工作目录及其所有祖先目录中查找源文件。pprof 会在环境变量 `$PPROF_BINARY_PATH` 指定的目录中查找二进制文件（默认 `$HOME/pprof/binaries`，在 Windows 上为 `%USERPROFILE%\pprof\binaries`）。它会按名称查找二进制文件，如果剖析包含链接器的 build id，它还会在以 build id 命名的目录中查找它们。

pprof 使用 binutils 工具来检查和反汇编二进制文件。默认情况下它会在当前路径中查找这些工具，但也可以通过环境变量 `$PPROF_TOOLS` 指定要查找的目录。

- **-list=_regex_：** 为匹配 _regex_ 的函数生成注释源代码清单，每行带有 flat/cum 值。
- **-disasm=_regex_：** 为匹配 _regex_ 的函数生成注释反汇编清单。
- **-weblist=_regex_：** 为匹配 _regex_ 的函数生成源代码/汇编结合的注释清单，并启动浏览器显示它。

## 比较剖析（Comparing profiles）

pprof 可以从另一个剖析中减去一个剖析，前提是剖析类型兼容（例如两个堆剖析）。pprof 有两个选项可以用来指定要从源剖析中减去的剖析文件名或 URL：

- **-diff_base=_profile_：** 对比两个剖析时很有用。输出中的百分比相对于 diff base 剖析中样本总数。
- **-base=_profile_：** 有助于从另一个累积剖析（例如 Go 的 block profile）中减去当前剖析，通常用于在稍后的时间点从同一程序收集到的累积剖析。当比较在同一程序上采集的累积剖析时，输出中的百分比相对于源剖析与基剖析之差的总量。

当使用 `-normalize` 标志并与 `-diff_base` 或 `-base` 一起指定基剖析时，此标志会在从源剖析中减去基剖析之前，对源剖析进行缩放，使得源剖析中的样本总和等于基剖析中的样本总和。该标志对于确定剖析之间相对差异很有用，例如哪个剖析在某个函数上占用了更大的 CPU 时间百分比。

使用 **-diff_base** 时，某些报告条目可能会出现负值。如果合并后的剖析以 protocol buffer 输出，diff base 剖析中的所有样本将带有键为 `"pprof::base"` 且值为 `"true"` 的标签。如果之后使用 pprof 查看合并的剖析，它将表现得像分别传入源和基剖析文件一样。

使用 **-base** 选项从稍后时间点在同一程序上收集的一个累积剖析中减去另一个累积剖析时，输出中的百分比将相对于源剖析和基剖析之差的总量，并且所有值将为正值。一般情况下，某些报告条目可能具有负值，并且在地址级别聚合时，百分比将相对于所有样本绝对值之和的总量计算。

# 获取剖析（Fetching profiles）

pprof 可以从文件或通过 http/https 的 URL 直接读取剖析。其原生格式是 gzip 压缩的 `profile.proto` 文件，但也可以接受由 [gperftools](https://github.com/gperftools/gperftools) 生成的一些遗留格式。

当从 URL 处理器获取时，pprof 接受一些选项以指示等待剖析的时长。

- **-seconds=_int_：** 让 pprof 请求持续指定秒数的剖析。仅对基于经过时间的剖析（如 CPU 剖析）有意义。
- **-timeout=_int_：** 让 pprof 在通过 http 获取剖析时等待指定的超时时间。如果未指定，pprof 会使用启发式方法来确定一个合理的超时。

pprof 还接受允许用户在从受保护端点获取或符号化剖析时指定 TLS 证书的选项。有关生成这些证书的更多信息，请参见 [https://docs.docker.com/engine/security/https/](https://docs.docker.com/engine/security/https/) 。

- **-tls_cert=_/path/to/cert_：** 用于获取和符号化剖析的 TLS 客户端证书文件路径。
- **-tls_key=_/path/to/key_：** 用于获取和符号化剖析的 TLS 私钥文件路径。
- **-tls_ca=_/path/to/ca_：** 用于获取和符号化剖析的证书颁发机构（CA）文件路径。

pprof 还支持在从受保护或符号化服务器收集剖析时跳过对服务器证书链和主机名的验证。要跳过此验证，请在 URL 中使用 `"https+insecure"` 代替 `"https"`。

如果指定了多个剖析，pprof 会获取它们并合并。这对于合并分布式作业中多个进程的剖析很有用。剖析可以来自不同的程序，但必须兼容（例如，CPU 剖析不能与堆剖析合并）。

## 符号化（Symbolization）

pprof 可以为仅包含地址信息的剖析添加符号信息。这对于编译型语言的剖析很有用，因为剖析来源可能无法或不容易包含函数名或源代码坐标。

pprof 可以通过检查二进制文件并使用 binutils 工具在本地提取符号信息，或者联系提供符号化接口的运行作业来获取符号信息。

pprof 默认会尝试符号化剖析，其 `-symbolize` 选项对符号化提供了一些控制：

- **-symbolize=none：** 禁用 pprof 的任何符号化。
- **-symbolize=local：** 仅尝试使用本地二进制和 binutils 工具进行符号化。
- **-symbolize=remote：** 仅尝试通过联系其符号化处理程序来符号化正在运行的作业。

对于本地符号化，pprof 会在剖析指定的路径中查找二进制文件，然后在环境变量 `$PPROF_BINARY_PATH` 指定的路径中查找。也可以将主二进制文件的名称直接作为 pprof 的第一个参数传入，以覆盖剖析主二进制文件的名称或位置，例如：

```
pprof /path/to/binary profile.pb.gz
```

默认情况下，pprof 会尝试对 C++ 名称进行去重整（demangle）和简化，以为 C++ 符号提供可读名称。它会积极丢弃模板和函数参数。可以使用 `-symbolize=demangle` 选项控制此行为。注意，对于远程符号化，符号化处理程序可能不会提供被混淆（mangled）的名称。

- **-symbolize=demangle=none：** 不执行任何去重整。若可用则显示混淆后的名称（mangled names）。
- **-symbolize=demangle=full：** 去重整，但不执行任何简化。若可用则显示完整的去重整名称。
- **-symbolize=demangle=templates：** 去重整，并修剪函数参数，但不修剪模板参数。

# Web 界面

当用户在命令行上提供 `-http=[host]:[port]` 参数请求 Web 界面时，pprof 会启动一个 Web 服务器并打开一个指向该服务器的浏览器窗口。服务器提供的 Web 界面允许用户以多种格式交互式查看剖析数据。

## 视图（Views）

显示顶部是包含若干按钮和菜单的头部。`View` 菜单允许用户在不同的可视化之间切换。可用视图如下所述：

### Graph

本地 Web 界面中的默认视图显示一个图，节点为函数，边表示调用者/被调用者关系。

注意：可以按住鼠标拖动显示，或使用滚轮缩放，或使用触控手势捏合/放大。

![Graph view](images/webui/graph.png)

例如，`FormatPack` 有一条指向 `FormatUntyped` 的出边，表示前者调用后者。边上的数值（5.72s）表示从 `FormatPack` 调用 `FormatUntyped`（及其被调用者）时消耗的时间。

详见[解释调用图](#interpreting-the-callgraph)节获取更多细节。

### 火焰图（Flame graph）

通过 `View` 菜单切换到 `Flame graph` 视图将显示一个 [flame graph](https://www.brendangregg.com/flamegraphs.html)。该视图提供了调用者/被调用者关系的紧凑表示：

![Flame graph](images/webui/flame.png)

此视图中的方框对应剖析中的栈帧。调用者方框位于被调用者方框之上。每个方框的宽度与在调用栈中出现该栈帧的样本值之和成正比。某个方框的子框从左到右按大小降序排列。

例如，这里可以看到 `FormatPack` 在 `FormatUntyped` 之上，表示前者调用后者。`FormatUntyped` 的宽度对应于该调用所占时间的比例。

不同方框中显示的名字可能具有不同字体大小。这些大小差异是为了尽可能多地将名字适配到方框内；不应对字体大小做其它解读。

#### 查看调用者（Viewing callers）

传统火焰图提供自上而下的视图：容易看到某函数调用了哪些函数，但难以找到某个函数被哪些调用者调用。例如，在示例中 `FormatUntyped` 有多个出现，因为它有多个调用者。

pprof 的火焰图扩展了传统模型：当选中某个函数时，图会改变以显示通向该函数的调用栈。因此，点击任何 `FormatUntyped` 的方框会显示以该函数为结束点的调用栈：

![Flame graph showing multiple callers](images/webui/flame-multi.png)

#### 差异模式（Diff mode）

当使用 **--diff_base** 选项时，方框宽度与以该方框为根的子树中增加和减少的总和成比例。例如，如果某子节点的成本减少了 150，而另一个子节点增加了 200，则方框宽度与 150+200 成比例。净增加或净减少（在前例中净增加为 200-150，即 50）由一个阴影区域表示。阴影区域的大小与净增加或净减少成比例。阴影为红色表示净增加，为绿色表示净减少。

#### 内联（Inlining）

内联通过调用者与被调用者之间缺失水平边界来表示。例如，假设 X 调用 Y，Y 调用 Z，而 Y 到 Z 的调用被内联到 Y 中。则在 X 与 Y 之间会有一条黑色边，但在 Y 与 Z 之间不会有边界。

### 注释源代码（Annotated Source Code）

尝试查看 `FormatUntyped` 内部的具体情况，可以通过注释了性能数据的源代码来查看。首先，右键点击该函数的方框以打开上下文菜单。

![Flame menu](images/webui/flame-menu.png)

选择 `Show source in new tab`。这会创建一个新标签，显示该函数的源代码。

注意：也可以通过从 `View` 菜单选择 `Source` 来显示源代码，但仅在关注单个或少数例程时这样做，因为显示源代码在查看多个函数时可能非常慢且输出庞大。

![Source listing](images/webui/source.png)

每行源代码都注有该行所消耗的时间。有两个数字（例如屏幕截图中第 207 行的 840ms 和 6.17s）。第一个数字不计入从该行调用的函数所花时间，第二个数字则包括这些时间。

通过点击第 207 行，可以展开显示内联函数的源代码以及对应的汇编：

![Expanded source listing](images/webui/source-expanded.png)

汇编代码以绿色显示。内联函数的源代码以蓝色显示，并按内联层级缩进。例如，缩进表示第 207 行的 `ConvertAll` 调用被内联，它又内联了对 `has_parsed_conversion` 的调用，而该调用又展开为一条 `cmpq` 指令。

### 反汇编（Disassembly）

有时只按指令顺序查看反汇编而不与源代码交织显示会很有帮助。可以通过从 `View` 菜单选择 `Disassemble` 来实现此目的。

注意：除非只关注单个或少数例程，否则不要选择 `Disassemble`，因为反汇编在查看多个例程时可能非常慢且输出庞大。

![Disassembly](images/webui/disasm.png)

### Top Functions

有时会发现显示剖析中顶级函数的表格很有用。

![Top functions](images/webui/top.png)

表格显示两种不同度量的数值（及百分比）：

- `flat`：该函数中的剖析样本数
- `cum`（累积）：该函数及其被调用者中的剖析样本数

表格默认按 `flat` 的降序排序。点击 `Cum` 表头会按该函数及其被调用者中的样本数降序排序。

### Peek

该视图以简单的文本格式显示每个函数的调用者 / 被调用者。通常火焰图视图更有帮助。

## 配置（Config）

`Config` 菜单允许用户将当前的精炼设置（例如 focus 和 hide 列表）保存为命名配置。已保存的配置可在以后重新应用以恢复保存的精炼。`Config` 菜单包含：

**Save as ...**：显示一个对话框，用户可以在其中输入配置名。当前的精炼设置将以指定名称保存。

**Default**：通过移除所有精炼切换回默认视图。

`Config` 菜单还包含每个命名配置的条目。选择该条目会应用该配置。当前选中的条目右侧会标有 ✓。点击该条目右侧的 🗙 会删除该配置（会先提示用户确认）。

## 待办（TODO）：覆盖以下问题：

- 整体布局
- 其它菜单项

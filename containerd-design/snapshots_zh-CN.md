# 快照（Snapshots）

从一开始，Docker 容器长期以来就是基于一种称为 _layers_（层）的快照方法构建的。_Layers_ 提供了派生文件系统、进行修改然后将变更集保存为新层的能力。

历史上，这些机制被紧密集成在 Docker 守护进程中，作为名为 `graphdriver` 的组件。`graphdriver` 使得在多种操作系统上运行 docker 守护进程成为可能，同时仍能在提交和分发镜像更改时保持大致相似的快照语义。

`graphdriver` 深度参与镜像的导入与导出，包括管理层关系和容器运行时的文件系统。`graphdriver` 的行为会影响镜像格式的传输方式。

在本文档中，我们提出一种更灵活的管理层的模型。该模型侧重于为基础快照功能提供 API，而不与镜像结构及其标识紧密耦合。最小化的 API 在不牺牲能力的前提下简化了行为。这使得驱动实现的表面更小，保证不同实现间的行为更一致。

与 `graphdriver` 的概念不同，_Snapshotter_ 并不关心镜像或容器。用户仅需准备并提交目录。我们也避免将 graph driver 与用于表示变更集的 tar 格式耦合。

最好的方面是，我们可以通过重构现有的 graphdrivers 达到该模型，从而最小化新增代码和庞大测试的需求。

## 范围

过去，`graphdriver` 组件在 Docker 中提供了大量功能，包括序列化、哈希计算、解包、打包和挂载。

_Snapshotter_ 只会提供面向挂载的快照访问，并带有最小的元数据。序列化、哈希、解包、打包和（额外的）挂载功能不包含在该设计内，优选在 graphdrivers 之间使用通用实现，而非各自专用实现。由于接口提供对变更集的直接访问，这对性能的影响较小。

## 架构

_Snapshotter_ 提供用于分配、快照和挂载基于层的抽象文件系统的 API。该模型通过构建具有父子关系的一组目录来工作，这些目录称为 _Snapshots_。

_Snapshot_ 表示一种文件系统状态。每个 snapshot 都有一个父级，其中空父级由空字符串表示。可以在父级与其 snapshot 之间取差异以创建经典的层（layer）。

通过生命周期可以最好地理解 snapshots。_Active_（活动）快照总是由对一个 _Committed_（已提交）快照（包括空快照）调用 `Prepare` 或 `View` 创建。_Committed_ 快照总是由对一个 _Active_ 快照调用 `Commit` 创建。活动快照永远不会变成已提交快照，反之亦然。所有快照都可以被移除。

在挂载一个 _Active_ 快照后，可以对该快照进行修改。提交（commit）操作会创建一个 _Committed_ 快照。已提交快照将继承活动快照的父级。已提交快照随后可以被用作父级。活动快照永远不能被用作父级。

下图演示了快照之间的关系：

![snapshot model diagram, showing active snapshots on the left and
committed snapshots on the right](snapshot_model.png)

在该图中，可以看到活动快照 _a_ 通过以已提交快照 `P<sub>0</sub>` 为父调用 `Prepare` 创建。修改后，_a_ 变成 _a'_，通过调用 `Commit` 创建了已提交快照 `P<sub>1</sub>`。_a'_ 可以进一步修改为 _a''_，再次调用 `Commit` 可以创建第二个已提交快照 `P<sub>2</sub>`。注意这里 `P<sub>2</sub>` 的父级是 `P<sub>0</sub>` 而不是 `P<sub>1</sub>`。

### 操作

_Snapshots_ 的体现由 `Mount` 对象和用于不透明数据存储的用户定义目录来协助实现。创建新的活动快照时，调用方提供一个称为 _key_ 的标识符。此操作返回一组挂载点（mounts），如果将它们挂载，则在挂载路径上会有完全准备好的快照。我们称此操作为 _prepare_。

一旦快照被 _prepared_ 并且挂载，调用方就可以向快照写入新数据。根据应用的不同，用户可能希望保留这些更改，也可能不想保留。

对于只读视图，可以使用 _view_ 操作。与 _prepare_ 类似，_view_ 会返回一组挂载点，如果将它们挂载则在挂载路径上会有完全准备好的快照。

如果用户希望保留更改，则使用 _commit_ 操作。_commit_ 操作接收表示活动快照的 _key_ 标识符以及一个 _name_ 标识符。成功后会创建一个 _committed_ 快照，当通过该 _name_ 引用时，可用作新 _active_ 快照的父级。

如果用户希望放弃活动快照中的更改，可调用 _remove_ 操作来释放与该快照相关的任何资源。`prepare` 或 `view` 返回的挂载点应在调用该方法前先卸载。

如果用户希望丢弃已提交的快照，也可使用 _remove_ 操作，但必须先删除其所有子项后才能继续。

有关详细使用信息，请参阅 [GoDoc](https://godoc.org/github.com/containerd/containerd/snapshots#Snapshotter)。

### 图形元数据（Graph metadata）

当快照被导入到容器系统时，会形成一个快照及其父关系的“图”。对该图的查询必须作为受支持的操作。

## 快照如何工作

为具体化 _Snapshots_ 术语，我们将从导入层的角度演示 _Snapshotter_ 的使用。我们将使用 Go API 来表示该过程。

### 导入一层（Importing a Layer）

要导入一层，我们只需让 _Snapshotter_ 提供一组挂载以便目标位置能够捕获变更集。我们先获取层 tar 文件的路径并创建一个临时位置用于解包：

```go
layerPath, tmpDir := getLayerPath(), mkTmpDir() // just a path to layer tar file.
```

我们首先使用 _Snapshotter_ 对一个新快照事务调用 `Prepare`，使用一个 _key_ 并以空父级 `""` 下降：

```go
mounts, err := snapshotter.Prepare(key, "")
if err != nil { ... }
```

`Snapshotter.Prepare` 会返回一组挂载点，`key` 标识该活动快照。将其挂载到临时位置：

```go
if err := mount.All(mounts, tmpDir); err != nil { ... }
```

一旦执行了挂载，我们的临时位置就准备好捕获差异了。实际上，这类似于文件系统事务。下一步是解包该层。我们有一个特殊函数 `unpackLayer`，它会将层的内容应用到目标位置并计算解包后层的 `DiffID`（这是 docker 实现的要求）：

```go
layer, err := os.Open(layerPath)
if err != nil { ... }
digest, err := unpackLayer(tmpLocation, layer) // unpack into layer location
if err != nil { ... }
```

完成上述操作后，我们应该得到一个表示该层内容的文件系统。稳健的实现应当验证 digest 是否与期望的 `DiffID` 匹配。完成后，我们卸载挂载点：

```go
unmount(mounts) // optional, for now
```

现在我们已验证并解包层，接着将活动快照提交到一个 _name_。在此示例中，我们仅使用层摘要（digest），但在实际中，这很可能是 `ChainID`：

```go
if err := snapshotter.Commit(digest.String(), key); err != nil { ... }
```

现在，我们在 _Snapshotter_ 中有了一个可通过提交时提供的 digest 访问的层。提交快照后，可以使用如下方式移除活动快照：

```go
snapshotter.Remove(key)
```

### 导入下一层

使新层依赖于上述层的过程与上文相同，不同之处在于在调用 `Snapshotter.Prepare` 时提供父级 `parent`，假定 `tmpLocation` 是干净的：

```go
mounts, err := snapshotter.Prepare(tmpLocation, parentDigest)
```

然后像之前一样挂载、应用并提交。新的快照将基于之前快照的内容。

### 运行容器

要运行容器，我们只需将已提交的镜像快照作为父级传入 `Snapshotter.Prepare`。挂载后，准备好的路径可以直接用作容器的文件系统：

```go
mounts, err := snapshotter.Prepare(containerKey, imageRootFSChainID)
```

返回的挂载点可以直接传递给容器运行时。如果想从该文件系统创建新镜像，则调用 `Snapshotter.Commit`：

```go
if err := snapshotter.Commit(newImageSnapshot, containerKey); err != nil { ... }
```

或者，对于大多数容器运行场景，会调用 `Snapshotter.Remove` 来通知 Snapshotter 放弃这些更改。

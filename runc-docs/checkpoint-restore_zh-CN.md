# 检查点与恢复（Checkpoint and Restore）

关于使用 `runc` 对容器进行**检查点（checkpoint）与恢复（restore）**的基本说明，请参阅以下手册页：
[runc-checkpoint(8)](../man/runc-checkpoint.8.md) 和 [runc-restore(8)](../man/runc-restore.8.md)。

---

## 检查点/恢复的注解（Annotations）

除了在命令行中指定选项（如上述手册页中描述的那样），还可以通过 **CRIU 的配置文件** 来影响 CRIU 的行为。
关于 CRIU 配置文件支持的详细信息，请参阅 [CRIU 官方 Wiki](https://criu.org/Configuration_files)。

除了 CRIU 默认的配置文件之外，`runc` 还会指示 CRIU 额外加载 `/etc/criu/runc.conf` 文件。
不过，可以通过注解 `org.criu.config` 来更改这个额外的 CRIU 配置文件路径。

---

### 禁用额外 CRIU 配置文件

如果将注解 `org.criu.config` 设置为空字符串，`runc` 将不会向 CRIU 传递任何额外的配置文件。
也就是说，通过设置为空字符串，可以禁用额外的 CRIU 配置文件。
这种方式可以确保不会因为额外配置文件而意外改变 CRIU 的行为。

**示例：禁用额外的 CRIU 配置文件**

```json
{
	"ociVersion": "1.0.0",
	"annotations": {
		"org.criu.config": ""
	},
	"process": {
```

---

### 指定自定义 CRIU 配置文件

如果将注解 `org.criu.config` 设置为一个非空字符串，`runc` 将把该字符串传递给 CRIU，并作为额外的配置文件进行加载。
如果 CRIU 无法打开该配置文件，它会忽略该文件并继续执行。

**示例：指定特定的 CRIU 配置文件**

```json
{
	"ociVersion": "1.0.0",
	"annotations": {
		"org.criu.config": "/etc/special-runc-criu-options"
	},
	"process": {
```

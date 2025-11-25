---
title: Faster Go maps with Swiss Tables
date: 2025-02-26
by:
- Michael Pratt
summary: Go 1.24 improves map performance with a brand new map implementation
---

哈希表是计算机科学中的一个核心数据结构，它为许多语言（包括 Go）中的 map 类型提供实现。

哈希表的概念最早由 Hans Peter Luhn 在 1953 年的一份 IBM 内部备忘录中[首次描述](https://spectrum.ieee.org/hans-peter-luhn-and-the-birth-of-the-hashing-algorithm)，该备忘录建议通过把条目放入“桶（bucket）”并在桶已包含条目时使用链表作为溢出，以加速查找。
今天我们会把这种称为[使用链地址法的哈希表](https://en.wikipedia.org/wiki/Hash_table#Separate_chaining)。

1954 年，Gene M. Amdahl、Elaine M. McGraw 和 Arthur L. Samuel 在为 IBM 701 编程时首次使用了“开放寻址（open addressing）”方案。
当一个桶已经含有条目时，新条目会放到下一个空桶中。这个想法在 1957 年由 W. Wesley Peterson 在《Addressing for Random-Access Storage》中形式化并发表。今天我们会把这称为[使用线性探测的开放寻址哈希表](https://en.wikipedia.org/wiki/Hash_table#Open_addressing)。

对于存在已久的数据结构，人们很容易认为它们已经“完成”了；我们知道了关于它们的一切，并且无法再改进。但事实并非如此！
计算机科学的研究仍在基础算法上取得进展，既包括算法复杂度方面，也包括利用现代 CPU 硬件的能力。例如，Go 1.19 将 `sort` 包从传统快速排序[切换为](/doc/go1.19#sortpkgsort) Orson R. L. Peters 在 2015 年首次描述的[Pattern-Defeating Quicksort（PDQ）](https://arxiv.org/pdf/2106.05123.pdf)。

像排序算法一样，哈希表数据结构也在不断改进。2017 年，Google 的 Sam Benzaquen、Alkis Evlogimenos、Matt Kulukundis 和 Roman Perepelitsa 提出了一种新的 C++ 哈希表设计，称为 “Swiss Tables”，并在 2018 年将其实现在 Abseil C++ 库中开源（见其[实现与介绍](https://abseil.io/blog/20180927-swisstables)）。

Go 1.24 包含了基于 Swiss Table 设计的全新内建 map 类型实现。在这篇博客中，我们将看看 Swiss Tables 如何优于传统哈希表，并讨论将 Swiss Table 设计引入 Go 的 map 时遇到的一些独特挑战。

## Open-addressed hash table

Swiss Tables 属于一种开放寻址（open-addressed）的哈希表形式，所以我们先快速概述基本的开放寻址哈希表是如何工作的。

在开放寻址哈希表中，所有条目都存储在单个底层数组中。我们把数组中的每个位置称为一个 *slot（槽）*。一个键属于哪个槽主要由 *哈希函数* `hash(key)` 决定。哈希函数把每个键映射为一个整数，其中相同的键总是映射到相同的整数，而不同的键理想情况下在整数上服从均匀随机分布。开放寻址哈希表的决定性特征是：它们通过在底层数组的其他位置存储键来解决冲突。因此，如果目标槽已经被占用（发生*冲突*），就会使用一个*探测序列*去考虑其他槽，直到找到一个空槽为止。下面来看一个示例哈希表来观察这一过程。

### Example

下面可以看到一个具有 16 个槽的哈希表底层数组，以及每个槽中存储的键（如果有的话）。值未显示，因为在本例中它们并不相关。

<style>
/*
go.dev .Article max-width is 55em. Only enable horizontal scrolling if the
screen is narrow enough to require scrolling (narrower than article width)
because otherwise some platforms (e.g., Chrome on macOS) display a scrollbar
even when the screen is wide enough.
*/
@media screen and (max-width: 55em) {
    .swisstable-table-container {
        /* Scroll horizontally on overflow (likely on mobile) */
        overflow: scroll;
    }
}

.swisstable-table {
    /* Combine table inner borders (1px total rather than 2px, one for cell above and one for cell below. */
    border-collapse: collapse;
    /* All column widths equal. */
    table-layout: fixed;
    /* Center table within container div */
    margin: 0 auto;
}

.swisstable-table-cell {
    /* Black border between cells. */
    border: 1px solid;
    /* Add visual spacing around contents. */
    padding: 0.5em 1em 0.5em 1em;
    /* Center within cell. */
    text-align: center;
}
</style>

<div class="swisstable-table-container">
    <table class="swisstable-table">
        <thead>
            <tr>
                <th class="swisstable-table-cell">Slot</th>
                <th class="swisstable-table-cell">0</th>
                <th class="swisstable-table-cell">1</th>
                <th class="swisstable-table-cell">2</th>
                <th class="swisstable-table-cell">3</th>
                <th class="swisstable-table-cell">4</th>
                <th class="swisstable-table-cell">5</th>
                <th class="swisstable-table-cell">6</th>
                <th class="swisstable-table-cell">7</th>
                <th class="swisstable-table-cell">8</th>
                <th class="swisstable-table-cell">9</th>
                <th class="swisstable-table-cell">10</th>
                <th class="swisstable-table-cell">11</th>
                <th class="swisstable-table-cell">12</th>
                <th class="swisstable-table-cell">13</th>
                <th class="swisstable-table-cell">14</th>
                <th class="swisstable-table-cell">15</th>
            </tr>
        </thead>
        <tbody>
            <tr>
                <td class="swisstable-table-cell">Key</td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell">56</td>
                <td class="swisstable-table-cell">32</td>
                <td class="swisstable-table-cell">21</td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell">78</td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
            </tr>
        </tbody>
    </table>
</div>

要插入一个新键，我们使用哈希函数选择一个槽。因为只有 16 个槽，我们需要将哈希值限制在该范围内，所以我们使用 `hash(key) % 16` 作为目标槽。假设我们要插入键 `98`，且 `hash(98) % 16 = 7`。槽 7 为空，因此我们直接把 98 插入到那里。另一方面，假设我们要插入键 `25` 且 `hash(25) % 16 = 3`。槽 3 已被键 56 占用，这是一次冲突，因此不能在此插入。

我们使用探测序列来寻找另一个槽。有多种已知的探测序列。最原始且最直接的探测序列是*线性探测（linear probing）*，它简单地依次尝试后续槽。

所以，在 `hash(25) % 16 = 3` 的例子中，因为槽 3 正在使用，我们会接着考虑槽 4，槽 4 也在使用，槽 5 也是如此。最后，我们会到达空槽 6，在那里存放键 25。

查找遵循相同的方法。对键 25 的查找会从槽 3 开始，检查它是否包含键 25（不包含），然后继续线性探测直到在槽 6 找到键 25。

此示例使用了一个具有 16 个槽的底层数组。如果我们插入超过 16 个元素会怎样？如果哈希表耗尽空间，通常会扩展，通常通过将底层数组大小加倍来实现。所有现有的条目都会被重新插入到新的底层数组中。

开放寻址哈希表实际上不会等到底层数组完全满了才扩展，因为随着数组变得更满，每次探测序列的平均长度会增加。在上面的键 25 例子中，我们必须探测 4 个不同的槽才能找到空槽。如果数组只有一个空槽，最坏情况的探测长度为 O(n)，也就是说你可能需要扫描整个数组。已使用槽的比例称为*负载因子（load factor）*，大多数哈希表定义了一个*最大负载因子*（通常为 70–90%），当达到该点时它们会扩展以避免非常满的哈希表带来的极长探测序列。

## Swiss Table

Swiss Table 的[设计](https://abseil.io/about/design/swisstables)也是一种开放寻址哈希表。让我们看它如何相较于传统开放寻址哈希表改进。我们仍然有一个用于存储的单个底层数组，但我们会把数组划分为每组 8 个槽的逻辑*组*。（也可以使用更大的组大小，下面会详述。）

另外，每个组有一个 64 位的*控制字（control word）*用于元数据。控制字中的每个字节对应组中的一个槽。每个字节的值表示该槽是空的、已删除还是正在使用。如果该槽正在使用，字节包含该槽键的哈希的低 7 位（称为 `h2`）。

<!-- Group table followed by control word table. Both are in the same container so they scroll together on mobile. -->

<div class="swisstable-table-container">
    <table class="swisstable-table">
        <thead>
            <tr>
                <th class="swisstable-table-cell"></th>
                <th class="swisstable-table-cell" colspan="8">Group 0</th>
                <th class="swisstable-table-cell" colspan="8">Group 1</th>
            </tr>
            <tr>
                <th class="swisstable-table-cell">Slot</th>
                <th class="swisstable-table-cell">0</th>
                <th class="swisstable-table-cell">1</th>
                <th class="swisstable-table-cell">2</th>
                <th class="swisstable-table-cell">3</th>
                <th class="swisstable-table-cell">4</th>
                <th class="swisstable-table-cell">5</th>
                <th class="swisstable-table-cell">6</th>
                <th class="swisstable-table-cell">7</th>
                <th class="swisstable-table-cell">0</th>
                <th class="swisstable-table-cell">1</th>
                <th class="swisstable-table-cell">2</th>
                <th class="swisstable-table-cell">3</th>
                <th class="swisstable-table-cell">4</th>
                <th class="swisstable-table-cell">5</th>
                <th class="swisstable-table-cell">6</th>
                <th class="swisstable-table-cell">7</th>
            </tr>
        </thead>
        <tbody>
            <tr>
                <td class="swisstable-table-cell">Key</td>
                <td class="swisstable-table-cell">56</td>
                <td class="swisstable-table-cell">32</td>
                <td class="swisstable-table-cell">21</td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell">78</td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
            </tr>
        </tbody>
    </table>
    <br/> <!-- Visual space between the tables -->
    <table class="swisstable-table">
        <thead>
            <tr>
                <th class="swisstable-table-cell"></th>
                <th class="swisstable-table-cell" colspan="8">64-bit control word 0</th>
                <th class="swisstable-table-cell" colspan="8">64-bit control word 1</th>
            </tr>
            <tr>
                <th class="swisstable-table-cell">Slot</th>
                <th class="swisstable-table-cell">0</th>
                <th class="swisstable-table-cell">1</th>
                <th class="swisstable-table-cell">2</th>
                <th class="swisstable-table-cell">3</th>
                <th class="swisstable-table-cell">4</th>
                <th class="swisstable-table-cell">5</th>
                <th class="swisstable-table-cell">6</th>
                <th class="swisstable-table-cell">7</th>
                <th class="swisstable-table-cell">0</th>
                <th class="swisstable-table-cell">1</th>
                <th class="swisstable-table-cell">2</th>
                <th class="swisstable-table-cell">3</th>
                <th class="swisstable-table-cell">4</th>
                <th class="swisstable-table-cell">5</th>
                <th class="swisstable-table-cell">6</th>
                <th class="swisstable-table-cell">7</th>
            </tr>
        </thead>
        <tbody>
            <tr>
                <td class="swisstable-table-cell">h2</td>
                <td class="swisstable-table-cell">23</td>
                <td class="swisstable-table-cell">89</td>
                <td class="swisstable-table-cell">50</td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell">47</td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
                <td class="swisstable-table-cell"></td>
            </tr>
        </tbody>
    </table>
</div>

插入的工作流程如下：

1. 计算 `hash(key)` 并把哈希拆成两部分：高 57 位（称为 `h1`）和低 7 位（称为 `h2`）。
2. 使用高位（`h1`）选择要考虑的第一个组：在此示例中由于只有 2 个组，所以为 `h1 % 2`。
3. 在组内，所有槽都同样有资格保存该键。我们必须首先判断是否有槽已经包含该键，如果包含则这是一次更新而不是新插入。
4. 如果没有槽包含该键，那么我们寻找一个空槽来放置该键。
5. 如果没有空槽，则继续探测序列，搜索下一个组。

查找遵循相同的基本流程。如果在步骤 4 中找到一个空槽，则我们知道插入本应使用该槽，因此可以停止搜索。

第 3 步就是 Swiss Table 的魔法所在。我们需要检查组内是否有任何槽包含期望的键。表面上我们可以简单做一次线性扫描并比较所有 8 个键，然而控制字让我们可以更高效地完成这一点。每个字节包含该槽哈希的低 7 位（`h2`）。如果我们能确定控制字中哪些字节包含我们要查找的 `h2`，就可以得到一组候选匹配。

换句话说，我们希望在控制字内做逐字节的相等比较。例如，如果我们在查找键 32，其 `h2 = 89`，我们希望执行如下操作。

<!-- Visualization of SIMD comparison -->

<div class="swisstable-table-container">
    <table class="swisstable-table">
        <tbody>
            <tr>
                <td class="swisstable-table-cell"><strong>Test word</strong></td>
                <td class="swisstable-table-cell">89</td>
                <td class="swisstable-table-cell">89</td>
                <td class="swisstable-table-cell">89</td>
                <td class="swisstable-table-cell">89</td>
                <td class="swisstable-table-cell">89</td>
                <td class="swisstable-table-cell">89</td>
                <td class="swisstable-table-cell">89</td>
                <td class="swisstable-table-cell">89</td>
            </tr>
            <tr>
                <td class="swisstable-table-cell"><strong>Comparison</strong></td>
                <td class="swisstable-table-cell">==</td>
                <td class="swisstable-table-cell">==</td>
                <td class="swisstable-table-cell">==</td>
                <td class="swisstable-table-cell">==</td>
                <td class="swisstable-table-cell">==</td>
                <td class="swisstable-table-cell">==</td>
                <td class="swisstable-table-cell">==</td>
                <td class="swisstable-table-cell">==</td>
            </tr>
            <tr>
                <td class="swisstable-table-cell"><strong>Control word</strong></td>
                <td class="swisstable-table-cell">23</td>
                <td class="swisstable-table-cell">89</td>
                <td class="swisstable-table-cell">50</td>
                <td class="swisstable-table-cell">-</td>
                <td class="swisstable-table-cell">-</td>
                <td class="swisstable-table-cell">-</td>
                <td class="swisstable-table-cell">-</td>
                <td class="swisstable-table-cell">-</td>
            </tr>
            <tr>
                <td class="swisstable-table-cell"><strong>Result</strong></td>
                <td class="swisstable-table-cell">0</td>
                <td class="swisstable-table-cell">1</td>
                <td class="swisstable-table-cell">0</td>
                <td class="swisstable-table-cell">0</td>
                <td class="swisstable-table-cell">0</td>
                <td class="swisstable-table-cell">0</td>
                <td class="swisstable-table-cell">0</td>
                <td class="swisstable-table-cell">0</td>
            </tr>
        </tbody>
    </table>
</div>

这是由 [SIMD](https://en.wikipedia.org/wiki/Single_instruction,_multiple_data) 硬件支持的一种操作：单条指令可以对更大值（*向量*）中的独立子值并行执行操作。在没有专门 SIMD 硬件的情况下，我们[可以使用一组标准的算术和按位运算来实现这个操作](https://cs.opensource.google/go/go/+/master:src/internal/runtime/maps/group.go;drc=a08984bc8f2acacebeeadf7445ecfb67b7e7d7b1;l=155?ss=go)。

结果是一组候选槽。`h2` 不匹配的槽不会有匹配键，因此可以跳过。`h2` 匹配的槽是潜在匹配项，但我们仍必须检查整个键，因为存在碰撞的可能性（使用 7 位哈希时碰撞概率为 1/128，仍然相当低）。

这个操作非常强大，因为我们实际上一次并行地执行了 8 步探测序列。这通过减少我们需要执行的平均比较次数，加速了查找和插入。对探测行为的这一改进允许 Abseil 和 Go 的实现相比以往的映射实现提高 Swiss Table 映射的最大负载因子，从而降低了平均内存占用。

## Go challenges

Go 的内建 map 类型有一些不同寻常的属性，这给采用新映射设计带来了额外挑战。其中两个尤其棘手。

### Incremental growth

当哈希表达到其最大负载因子时，需要扩展底层数组。通常这意味着下一次插入会把数组大小翻倍，并把所有条目复制到新数组中。想象向一个有 1GB 条目的 map 插入元素。大多数插入非常快，但那一次需要把 map 从 1GB 扩展到 2GB 的插入就必须复制 1GB 的条目，这会花很长时间。

Go 常被用于对延迟敏感的服务器，所以我们不希望内建类型的操作对尾延迟产生任意大的影响。相反，Go 的 map 以增量方式增长，使得每次插入在其必须执行的扩展工作量上有一个上界。这样可以限制单次插入的延迟影响。

不幸的是，Abseil（C++）的 Swiss Table 设计假设一次性扩展，并且探测序列依赖于总的组数，这使得将扩展工作拆分开来变得困难。

Go 的内建 map 通过另一层间接性来解决这一点——将每个 map 拆分成多个 Swiss Table。与其让单个 Swiss Table 实现整个 map，不如让每个 map 由一个或多个独立的表组成，这些表覆盖键空间的子集。单个表最多存储 1024 个条目。哈希中的若干高位用于选择一个键所属的表。此为一种[*可扩展哈希（extendible hashing）*](https://en.wikipedia.org/wiki/Extendible_hashing) 的形式，其中随着所需表数量的增多，所使用的位数也相应增加。

在插入期间，如果某个单独的表需要扩展，它会一次性完成扩展，但其他表不受影响。因此，单次插入的上界就是把一个 1024 条目的表扩展为两个 1024 条目的表并复制 1024 条目的延迟。

### Modification during iteration

许多哈希表设计（包括 Abseil 的 Swiss Tables）禁止在迭代期间修改映射。Go 语言规范[明确允许](/ref/spec#For_statements:~:text=The%20iteration%20order,iterations%20is%200.)在迭代期间修改映射，并给出如下语义：

* 如果在元素被产生之前就将其删除，则该元素不会被产生。
* 如果在元素被产生之前将其更新，将会产生更新后的值。
* 如果添加了新元素，则该元素可能会被产生也可能不会被产生。

一种典型的哈希表迭代方法是简单地遍历底层数组并按内存中布局的顺序产生值。这种方法会与上述语义冲突，尤其是因为插入可能会触发扩展，从而改变内存布局。

我们可以通过让迭代器保留对其当前正在迭代的表的引用来避免增长期间的布局变动影响。如果该表在迭代期间增长，我们仍使用旧版本的表，从而继续按旧的内存布局返回键。

这能否满足上述语义？在增长之后添加的新条目将被完全忽略，因为它们只添加到增长后的表而不在旧表中，这没问题，因为语义允许新条目不被产生。但更新和删除会成为问题：使用旧表可能会产生已过时或已删除的条目。

这个边缘情况通过仅使用旧表来确定迭代顺序来解决。在实际返回条目之前，我们会查询已增长的表以确定该条目是否仍然存在，并检索最新的值。

这覆盖了所有核心语义，尽管还有更多未在此处详述的小边缘情况。最终，Go map 在迭代时的宽松语义导致迭代成为 Go map 实现中最复杂的部分。

## Future work

在[微基准测试](https://github.com/golang/go/issues/54766#issuecomment-2542444404)中，map 操作在某些情况下相比 Go 1.23 提速高达 60%。由于 map 操作和使用方式多种多样，具体性能提升差异较大，有些边缘情况相比 Go 1.23 会有所回退。总体上，在完整应用基准中，我们发现几何平均的 CPU 时间改进约为 1.5%。

我们希望在未来的 Go 版本中继续研究更多的 map 改进。例如，我们可能能够[提高对不在 CPU 缓存中的 map 的操作的局部性](https://github.com/golang/go/issues/70835)。

我们也可以进一步改进控制字比较。如上所述，我们有一个使用标准算术与按位运算的可移植实现。然而，某些架构具有直接执行此类比较的 SIMD 指令。Go 1.24 已经在 amd64 上使用了 8 字节的 SIMD 指令，但我们可以考虑将支持扩展到其他架构。更重要的是，虽然标准指令在最多 8 字节字上操作，SIMD 指令通常至少支持 16 字节。这意味着我们可以将组大小增加到 16 个槽，并行执行 16 个哈希比较而不是 8 个。这将进一步减少查找所需的平均探测次数。

## Acknowledgements

基于 Swiss Table 的 Go map 实现筹备已久，涉及许多贡献者。我要感谢 YunHao Zhang ([@zhangyunhao116](https://github.com/zhangyunhao116))、PJ Malloy ([@thepudds](https://github.com/thepudds)) 和 [@andy-wm-arthur](https://github.com/andy-wm-arthur) 为构建 Go Swiss Table 实现的初始版本所做的工作。Peter Mattis ([@petermattis](https://github.com/petermattis)) 将这些想法与上文提到的 Go 特殊挑战的解决方案结合，构建了 [`github.com/cockroachdb/swiss`](https://pkg.go.dev/github.com/cockroachdb/swiss)，这是一个符合 Go 规范的 Swiss Table 实现。Go 1.24 的内建 map 实现很大程度上基于 Peter 的工作。感谢所有为此贡献的社区成员！

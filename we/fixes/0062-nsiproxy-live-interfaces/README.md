# 0062：过滤失效的 Unix 网络接口缓存身份

本提案新增；历史测试日期 2026-11-01，公开整理 2026-11-03。补丁只修改 Wine `dlls/nsiproxy.sys/ndis.c`，与 [0063](../0063-route-interface-cache/README.md)配套使用。[补丁](../../../patches/wine/0062-nsiproxy-Ignore-stale-Unix-network-interface-entries.patch)保留原始作者及 AI 协作署名。

## 现象与根因

实际加速器在一个 Wine 服务生命周期内反复删除、同名重建虚拟网卡。Linux 中只存在当前接口，但 Windows 接口枚举仍可返回多个旧的同名 Up 身份，按名称查找可能指向已经删除的索引。该错误会影响通过 Windows API 获取当前虚拟接口的程序。

`nsiproxy` 缓存的接口记录被保留以保证返回字符串指针的生命周期；原来的枚举和名称、索引、LUID 查找却没有统一排除已经失效的记录。保留内存和继续把该记录视为当前接口是两件事。

## 修复

使用 `if_nametoindex(cached_name) == cached_index` 校验缓存记录仍对应当前 Unix 接口，枚举与相关转换均应用该判断。旧记录仍保留分配，避免返回指针悬空；已有的 synthetic Ethernet 兼容项按原语义保留。最后一张真实 Ethernet 消失后重新计算是否需要兼容回退。

这修复的是已观察到的缓存身份问题，不声称它单独解释所有加速器转圈、节点不支持或连接失败。

## Windows 行为与验证

接口名称、索引和 LUID 应对应实际存在的接口；依据公开 Windows API 契约和隔离 Linux/Wine 删除重建观察。**本次新增生命周期和性能探针没有在原生 Windows 上执行**；不把 Wine 自身结果作为 Windows 对照。

候选运行器完成：PE64 生命周期 61/61、PE32 WoW64 生命周期 61/61、改名/兼容接口边界 PE64 90/90，合计 212 项。源码包括 [生命周期探针](../../tests/ndis_interface_lifecycle_probe.c)。测试使用临时接口和独立前缀，不运行真实游戏。

PE32 WoW64 使用 x86_64 Unix 后端；不是 legacy i386 Unix 后端的实际执行结果。没有证明同名同索引再利用、任意并发热插拔或所有客户端流程都正确。公开材料保留方法和匿名化结果，完整个人诊断日志不发布。

历史生命周期原始前后输出及 SHA256 见[接口生命周期证据](../../network-integration/VALIDATION.md#接口生命周期的前后输出)：每种 PE 架构的基准为 26/61 通过（35 项失败），0062 候选为 61/61，通过后未删除基准失败记录。

## 性能回退与最终状态

单独加入 0062 后，逐路由全表扫描会重复调用同一接口的实时查询，3000 条合成路由创建/删除明显变慢。该回退由 [0063](../0063-route-interface-cache/README.md)修复；不能只应用 0062 后声称获得最终网络修正版。

历史实机随后有成功的多目标切换复测，但也保留更早同版本仍卡住的记录。准确结果、失败与未解决项见[验证说明](../../network-integration/VALIDATION.md)。

## 构建与许可

按[构建说明](../../network-integration/BUILD.md)在独立源码目录内准备运行器；不要直接替换正在使用的模块。Wine 补丁及明确带 LGPL 头的探针沿用 LGPL-2.1-or-later。上游网络能力来源及本地贡献边界见[来源说明](../../network-integration/PROVENANCE.md)。

# wine 0013：私有命名空间（Private Namespaces）

## 现象

一些客户端在启动时用 Windows 的"私有命名空间"做单实例检测：`CreateBoundaryDescriptorW` 建边界描述符，`CreatePrivateNamespaceW`/`OpenPrivateNamespaceW` 打开命名空间，再用 `<别名>\<对象名>` 创建互斥体等具名对象。Wine 完全没有这组功能（边界描述符和命名空间都没有实现，`<别名>\<名字>` 这种对象名也解析不了），程序一调用就崩，客户端（以及它的加速引擎）根本起不来。

## 依据

`we/tests/private_ns_probe.c`（每个 API 都用 `GetProcAddress` 查找，找不到就跳过）在 Windows 测试机和 Wine 里各跑一遍，逐行比较输出（`private_ns_probe.windows.txt`/`windows32.txt` 对 `private_ns_probe.wine.txt`/`wine32.txt`）：

- 边界描述符的内存布局（`version`、条目数、总长度、`flags` 和每个条目的结构）与 Windows 一致；`AddSIDToBoundaryDescriptor` 会重新分配并搬动描述符，`AddIntegrityLabelToBoundaryDescriptor` 追加一个完整性级别条目；
- 重复建同一个命名空间/别名 → 183；别名是空串 → 52；别名里可以带反斜杠（`a\b`）；
- 别名大小写不敏感（`P` 和 `p` 找到同一个命名空间）；
- 子进程能 `OpenPrivateNamespaceW` 父进程建的命名空间并在里面建对象，父进程也能打开子进程建的对象；别名只在建它的进程里有效（子进程拿父进程的别名打开失败）；
- 命名空间里的 Mutex/Event/Semaphore/Timer/Section/Job 都能建、能打开；
- `<别名>` 单独用作对象名时不进私有命名空间（走 `BaseNamedObjects`）；别名不存在（`Z\m`）或多一级路径（`P\sub\m`）失败；
- `ClosePrivateNamespace(handle, DESTROY)` 之后命名空间销毁、里面的对象随之消失；不销毁则其它句柄不受影响。

## 修复

- ntdll：实现边界描述符的构建、加 SID/完整性级别、删除，以及描述符到命名空间路径名的转换（`\PrivateNamespaces\<描述符名>[;SID...]`，SID 按加入顺序列出）。
- kernelbase/kernel32：实现 `CreateBoundaryDescriptorA/W`、`AddSIDToBoundaryDescriptor`、`AddIntegrityLabelToBoundaryDescriptor`、`DeleteBoundaryDescriptor`、`CreatePrivateNamespaceA/W`、`OpenPrivateNamespaceA/W`、`ClosePrivateNamespace`；A 版转成 W 版。
- 对象名的解析放在 kernelbase 的 `BasepAdjustObjectAttributesForPrivateNamespace`：`<别名>\<名字>` 改写成真实路径；别名单独出现或找不到时改写（交给后面的对象函数给出原来的错误）。这样 `CreateMutex`/`OpenEvent` 等函数不用各改一遍。

## 验证

`private_ns_probe` 的 64 位和 32 位输出逐行比对；所有语义（183/52/3/2、跨进程、销毁）一致。输出里的差异只有：

- `elevated`：测试机上是管理员，Wine 里不是；
- `NtQueryObject` 查到的对象名字：Windows 给 `\...\名字`（命名空间目录本身是空名），Wine 给完整路径 `\PrivateNamespaces\...\名字`；
- 别名单独用作对象名时：Windows `\BaseNamedObjects\P`，Wine `\Sessions\1\BaseNamedObjects\P`；
- 子进程拿父进程别名打开对象：Windows 6、Wine 2（都失败，错误码不同）；
- `Close(..., DESTROY)` 之后 `GetLastError` 的残留值不同（Windows 87，Wine 没设置）。

## 未验证

- 完整性级别/权限控制只测了 World、BUILTIN\Administrators 和 medium 完整性级别的组合，没测低完整性进程、AppContainer 等；
- 多会话/多用户下的隔离（Wine 里只有一个会话）。

## 客户端验证（2026-10-08）

用带 0013 的 runner（we-proton-11.0-14-15）在一个 pasta 网络命名空间里跑客户端的启动器和引擎：单实例检测没有再崩，两者都正常启动。

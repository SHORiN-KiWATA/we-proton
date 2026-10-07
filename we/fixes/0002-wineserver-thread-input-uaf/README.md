# 0002：wineserver 在 attach_thread_input 里释放后使用

补丁：`patches/wine/0002-server-Free-thread-input-attachments-when-either-que.patch`（`server/queue.c`）。

## 现象

wineserver 段错误，前缀里所有进程一起消失。wineserver 崩溃后还会留下一批孤儿 Wine 进程，它们继续占着 X11 连接；多来几次，Xwayland 报 `Maximum number of clients reached`，之后新进程连不上显示，winex11 初始化失败退出。

coredumpctl 里有 41 次崩溃，栈都一样，在 `attach_thread_input` / `assign_thread_input`。频繁创建窗口、跨线程 `AttachThreadInput` 的程序容易触发。

## 根因

一个 attachment 在创建时挂在 `queue_from` 当时的 thread input 的 attachment 链表上。`msg_queue_destroy()` 只遍历队列**当前** input 的链表；队列换过 input 之后，旧链表上的 attachment 不会被清掉，留着一个指向已释放队列的指针。下一次 `attach_thread_input()` / `assign_thread_input()` 遍历到它就访问了已释放的内存。

## 修复

所有 attachment 同时挂在一个全局链表上；队列销毁时删除所有引用它的 attachment，释放统一走 `free_attachment()`。

## 验证

装上之后 wineserver 没有再在这里崩溃。

## 弯路

这个补丁写好后曾记录为「已装进 runner」，实际从没装上：runner 里的 wineserver 和官方发布包逐字节相同，之后照样崩了几十次。教训：装二进制之后要比对校验和，不能只看操作记录。

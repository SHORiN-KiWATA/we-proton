# 0049：托盘图标交给 explorer（0011），不用 GE-Proton 在 win32u 里的实现

补丁：`patches/wine/0049-win32u-Leave-notification-icons-to-explorer.patch`（`dlls/win32u/systray.c`）。

## 现象

换到 GE-Proton 底座后，WeGame 的托盘图标在 waybar 里透明的地方是不透明的黑色，图标变成一个黑底方块。DW-Proton 底座的 WE-Proton（托盘走 0011）显示正常。

## 根因

GE-Proton 在 win32u 里另有一套 StatusNotifierItem 托盘实现（`WINE_SYSTRAY_NOTIFY_ICON_SNI`、`WINE_SYSTRAY_NOTIFY_BALLOON_SNI` → `sni_notify_icon()`、`sni_notify_balloon()`）。`Shell_NotifyIcon` 先到 win32u：它接手了，explorer 里 0011 的实现就收不到图标。

它把图标转成 StatusNotifierItem 要的 ARGB 时，只要颜色平面里有任何一个像素的 alpha 不是 0，就认定这是带 alpha 的图标，整张图的透明度都取自颜色平面，不再看 AND 掩码。WeGame 的图标颜色平面带了一些 alpha，形状却是靠 AND 掩码挖出来的：掩码挖掉、颜色平面里 alpha 为 255 的黑色像素，就成了不透明的黑色。

0011 的实现（explorer 里）按掩码处理这种图标，显示正常。用户也要求托盘继续用 WE-Proton 自己的实现。

## 修复

`system_tray_call()`：`WINE_SYSTRAY_NOTIFY_ICON_SNI` 返回 -1（不接手，`Shell_NotifyIcon` 接着交给 explorer），`WINE_SYSTRAY_NOTIFY_BALLOON_SNI` 返回 FALSE。GE-Proton 的 DBus 循环和其他托盘消息不动。0011 照原样保留（换底座时一度丢掉，又恢复了）。

## 验证

- 2026-10-12 用户在 WeGame 里确认托盘修复生效，图标正常显示（we-proton-ge11-7-25-test2）
- 修复前用户的截图：同一个图标透明处全黑

## 未验证

- 不靠掩码、纯 alpha 的图标在两种实现下的差别（0011 原来就处理这种图标，DW-Proton 底座上一直在用）

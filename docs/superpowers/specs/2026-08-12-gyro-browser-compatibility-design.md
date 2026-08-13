# 移动端陀螺仪浏览器兼容修复设计

## 目标

在不新增页面按钮和操作入口的前提下，让支持方向传感器的移动设备尽可能自动启用 krpano Gyro2 控制，同时对需要显式授权、不支持传感器、权限被拒绝或插件加载失败的环境安全降级为手指拖动。

本设计不承诺所有手机和浏览器均可使用陀螺仪。硬件缺失、系统传感器关闭、浏览器站点权限关闭、跨域 iframe 权限策略或非安全上下文仍可能使功能不可用。

## 已确认根因

1. Gyro2 1.20.7 的 `isavailable` 只有在插件触发 `onavailable` 或 `onunavailable` 后才可靠；现有代码只在全景预览或高清图加载完成时尝试启用，时机与插件可用事件没有因果关系。
2. 现有 `requestFromGesture()` 在申请浏览器权限前先检查 `isavailable`，会在需要显式授权的浏览器形成“尚未授权所以不可用、因为不可用所以不申请授权”的循环依赖。
3. 现有适配器调用 `gyro.enable()` 和 `gyro.disable()`；Gyro2 1.20.7 官方公开的启停接口是修改 `plugin[gyro].enabled` 属性。
4. Android Chrome 通常不会显示 iOS 风格的方向权限弹窗，因此“没有弹窗”不能作为故障依据；必须以插件可用事件和真实启用状态为准。

## 方案比较

### 方案 A：Gyro2 生命周期驱动（采用）

把 Gyro2 的 `onavailable`、`onunavailable`、`onenable`、`ondisable` 事件桥接到页面，按浏览器是否提供 `requestPermission()` 选择自动启用或首次触摸授权。优点是遵循插件真实生命周期、无需轮询、跨设备行为清晰；改动集中在现有三个模块。

### 方案 B：场景加载后定时轮询

反复读取 `isavailable`，成功后启用。代码表面较少，但轮询时间难以确定，不能解决权限调用必须发生在用户手势中的问题，也无法可靠区分加载失败和硬件不支持。

### 方案 C：绕过 Gyro2 自行处理方向事件

直接监听 `deviceorientation`/`devicemotion` 并更新 krpano 视角。可完全掌控行为，但需要自行实现屏幕方向换算、滤波、漂移补偿、触控协调和浏览器差异，重复 Gyro2 的核心能力，范围和风险过大。

## 架构与事件流

### KrpanoAdapter

- 在动态场景 XML 的 Gyro2 插件节点注册 `onavailable`、`onunavailable`、`onenable`、`ondisable`。
- 通过稳定的全局桥接函数把状态传给适配器，再由 `onGyroStateChange` 回调通知 `MuseumApp`。
- `enableGyro()` 改为执行 `set(plugin[gyro].enabled,true);`，`disableGyro()` 改为执行 `set(plugin[gyro].enabled,false);`。
- 保留 `isGyroAvailable()` 作为状态读取，但只有收到插件可用事件后才用它决定最终状态。

### GyroController

- 分离“申请浏览器权限”和“插件是否已就绪”两个条件。
- 若浏览器不存在 `requestPermission()`（典型 Android Chrome），权限条件视为允许；收到插件 `available` 后自动启用。
- 若 `DeviceOrientationEvent` 或 `DeviceMotionEvent` 提供 `requestPermission()`，仅在首次真实 `pointerdown` 用户手势内请求；若两者都提供，则同时请求，两者均通过才视为授权。
- 权限请求不再以前置 `isavailable` 为条件。权限先通过而插件尚未就绪时记录意图，等待 `available` 后启用；插件先就绪而权限尚未申请时等待首次触摸。
- `suspend`、`resume` 与 `destroy` 继续复用现有语义。弹窗或临时 UI 打开时关闭插件，恢复时仅在权限与插件均允许时重新启用。

### MuseumApp

- 接收 Gyro2 生命周期状态并转交 `GyroController`。
- 删除以场景图片加载事件作为唯一启用依据的竞态逻辑；场景加载完成只能表示播放器画面就绪，不能替代插件 `available`。
- 保留全景区域首次 `pointerdown` 作为需要显式权限浏览器的授权入口，不新增 UI。
- 权限拒绝或插件不可用时最多提示一次，网站仍可拖动浏览。

## 兼容与降级行为

| 环境 | 行为 |
| --- | --- |
| Android Chrome/Edge/Samsung Internet，插件可用 | 收到 `available` 后自动启用，无权限弹窗 |
| iOS Safari/Chrome 等实现 `requestPermission()` 的浏览器 | 首次触摸全景时申请所需权限，通过后启用 |
| 权限先通过、插件稍后可用 | 保存启用意图，收到 `available` 后启用 |
| 插件先可用、权限尚未申请 | 等待首次触摸，不在非用户手势中弹权限 |
| 权限拒绝、传感器缺失或插件不可用 | 提示一次并降级为手指拖动 |
| 非 HTTPS 或被 Permissions Policy 阻止 | 不启用，保留手指拖动并提供可诊断状态 |

### 桌面端提示策略

PC 浏览器缺少方向传感器属于正常能力边界。桌面端收到 Gyro2 `unavailable` 时静默降级为手指拖动，不显示“未能启用陀螺仪”提示；移动端收到同一事件时仍最多提示一次，以便用户检查系统传感器或站点权限。浏览器明确拒绝或异常终止方向/运动权限请求时仍提示一次。

移动设备判定由 `MuseumApp` 注入 `GyroController`，优先读取 `navigator.userAgentData.mobile`；该字段不可用时，仅回退识别 Android、iPhone、iPad 和 iPod 的 User-Agent。不得使用视口宽度或触摸能力单独判定，以免窄窗口 PC 或触屏笔记本误报。

页面本身是顶层同源页面，不新增 iframe 逻辑。生产 Nginx 应允许本站使用 `accelerometer` 和 `gyroscope`，部署文档补充相应 `Permissions-Policy` 响应头与真机检查命令。

## 测试与验收

自动测试至少覆盖：

1. XML 注册四个 Gyro2 生命周期回调。
2. 适配器使用官方 `plugin[gyro].enabled` 启停方式。
3. Android 类环境在插件延迟 `available` 后自动启用。
4. 需要权限的环境在插件尚未 available 时仍会于用户手势内申请权限，并在稍后 available 时启用。
5. `DeviceMotionEvent` 与 `DeviceOrientationEvent` 双权限请求、拒绝和异常分支。
6. suspend/resume、场景切换与 modal 生命周期不回归。
7. 不支持设备不会抛出未处理异常，且不会重复提示。
8. 桌面端 Gyro2 `unavailable` 静默降级，移动端 `unavailable` 与权限拒绝仍最多提示一次。

真机验收：使用 `https://jingjiear.cn` 在 Android Chrome 与至少一台 iOS Safari 测试；进入后转动手机应改变全景视角，拖动仍可调整偏移，打开作品弹窗时陀螺仪暂停，关闭后恢复。Android 站点设置中的“动作传感器”被禁用时应正常降级。

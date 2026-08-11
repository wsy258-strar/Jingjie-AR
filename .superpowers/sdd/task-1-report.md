# Task 1 报告：krpano Gyro2 适配与页面级权限控制

## 实现与文件

- `WebApps/ARServer/www/js/krpano-adapter.js`：场景 XML 注册默认关闭的 Gyro2 插件（`keep=true`、`enabled=false`、`camroll=true`、`friction=0.5`）；新增 `enableGyro()`、`disableGyro()`、`isGyroAvailable()`。
- `WebApps/ARServer/www/js/gyro-controller.js`：新增 `GyroController`，实现手势权限申请、iOS 页面级一次性申请、非 iOS 自动启用、基于 `Set` 的暂停/恢复和销毁。
- `WebApps/ARServer/www/assets/krp/plugins/gyro2.js`：从主工作区的 `pano/html/assets/krp/1.20.7/plugins/gyro2.js` 原样复制；源路径在此 worktree 中未检出，因此读取指定主工作区路径后复制到当前 worktree 目标路径。
- `tests/frontend/krpano-adapter.test.mjs`：新增 Gyro2 XML 与适配器调用/可用性断言。
- `tests/frontend/gyro-controller.test.mjs`：新增 iOS 单次授权、嵌套暂停原因恢复测试。

## RED

精确命令：

```bash
/tmp/node-v20.19.5-linux-x64/bin/node --test tests/frontend/krpano-adapter.test.mjs tests/frontend/gyro-controller.test.mjs
```

结果：退出码 `1`；`gyro-controller.test.mjs` 报 `ENOENT`（`WebApps/ARServer/www/js/gyro-controller.js` 不存在），`krpano-adapter.test.mjs` 报测试文件失败。为取得 Node `--test` 子进程未展开的适配器失败详情，补充运行：

```bash
/tmp/node-v20.19.5-linux-x64/bin/node tests/frontend/krpano-adapter.test.mjs
```

结果：退出码 `1`，25 项中 23 通过、2 失败；失败原因为场景 XML 不含 Gyro2 插件节点，且 `adapter.enableGyro is not a function`。

## GREEN

精确命令：

```bash
/tmp/node-v20.19.5-linux-x64/bin/node --test tests/frontend/krpano-adapter.test.mjs tests/frontend/gyro-controller.test.mjs
```

结果：退出码 `0`，2 个测试文件通过，`pass 2`、`fail 0`。

## 自审

- iOS 路径通过 `requestPermission.call(deviceOrientation)` 保持浏览器 API 的接收者；同一控制器实例以 `permissionRequested` 阻止重复弹窗。
- iOS `autoEnable()` 不调用权限弹窗；无该 API 的设备通过 `requestFromGesture()` 按已授权处理。
- 只有最后一个暂停原因解除时才重启；重复 suspend 同一 reason 不会额外禁用。
- XML URL 与插件安装目标一致，且资源文件已用 `cmp -s` 与指定源文件核对相同。
- `git diff --check` 退出码为 `0`；仅任务允许的五个实现/测试/资源文件和本报告将被提交。

## 顾虑

- 当前任务仅实现控制器与适配器接口；页面把真实手势、抽屉和模态事件接入控制器属于后续页面整合任务，未越界修改。

## Critical 修复：Gyro2 可用性字段

- `KrpanoAdapter.isGyroAvailable()` 已从错误的 `plugin[gyro].available` 改为 krpano Gyro2 的只读字段 `plugin[gyro].isavailable`。
- `tests/frontend/krpano-adapter.test.mjs` 的假 player 同步断言 `plugin[gyro].isavailable`，防止该字段回归。

### RED

先仅更新测试桩，然后运行：

```bash
/tmp/node-v20.19.5-linux-x64/bin/node tests/frontend/krpano-adapter.test.mjs
```

结果：退出码 `1`；Gyro2 测试明确显示实际读取 `plugin[gyro].available`，预期为 `plugin[gyro].isavailable`（24 通过、1 失败）。随后将生产代码改为正确字段。

### GREEN

```bash
/tmp/node-v20.19.5-linux-x64/bin/node --test tests/frontend/krpano-adapter.test.mjs tests/frontend/gyro-controller.test.mjs
```

结果：退出码 `0`；`pass 2`、`fail 0`。

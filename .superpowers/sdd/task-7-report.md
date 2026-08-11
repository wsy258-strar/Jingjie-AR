### Task 7 Report: 前进热点和展品热点动效

#### 实现

- `buildSceneXml()` 的第四参数现在是 `reducedMotion`（默认 `false`），第五参数保留 scene-event generation；`loadScene()` 同时传入适配器的无障碍设置和 generation。
- `scene` 热点在加载后执行 0.75 秒的缩放、透明度和纵向位移提示，完成后复位并仅在热点仍存在时继续循环。
- `artwork` 热点以 1.1 秒放大/还原和透明度呼吸循环；动作每轮先检查 `caller` 对应热点仍在。
- 两类热点点击都会先停止 caller 的 scale、alpha、oy tween，再调用 `JingjieARHotspotBridge`。reduced motion 时不输出脉冲动作或 `onloaded` 循环，点击仍有效。

#### TDD 记录

- RED：新增 scene/artwork XML、点击停动效、reduced-motion 无循环测试后，Node 20 运行得到 26 通过、2 失败；失败原因为脉冲 action 和 `stoptween` 尚未生成。
- GREEN：实现动态 XML 后，使用 `/tmp/node-v20.19.5-linux-x64/bin/node --test --test-reporter=spec tests/frontend/krpano-adapter.test.mjs`，结果为 1 个测试文件通过（其中 28 个子测试通过）。

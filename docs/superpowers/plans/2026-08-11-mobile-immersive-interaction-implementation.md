# 移动端沉浸交互与全景反馈优化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** 修复移动端陀螺仪并加入触屏大图、横屏全屏、自适应 1.5 秒叠化、热点动效、准确的音乐状态与公安备案图标。

**Architecture:** 保持 C++ 后端和接口不变，将设备方向、全屏方向和音频状态拆成小型前端控制器，由 museum-app.js 负责协调；krpano 适配层只负责插件、热点和场景可见事件。作品大图在现有 ArtworkGallery 上增加独立顶层查看状态，转场控制器用旧画面、启动预览和 generation 事件完成加载与动画并行。

**Tech Stack:** 原生 ES Modules、Node.js 20 node:test、krpano 1.20.7 Gyro2/WebVR、Pointer Events、Fullscreen API、Screen Orientation API、CSS3。

## Global Constraints

- 不修改点赞、评论、收藏、分享、登录接口或场景 JSON 字段。
- PC 端现有放大、缩小和重置按钮必须保留。
- 陀螺仪申请状态只保存在当前页面内存；刷新或重新进入后再次执行申请流程。
- 场景加载与转场并行，目标总时长为 1500ms，慢网时旧画面不得完全消失成黑屏。
- iOS Safari 不支持强制横屏时必须保留全屏并提示手动旋转。
- prefers-reduced-motion: reduce 下停用热点循环动效并缩短叠化。
- 只提交本任务涉及的文件；保留现有 README.md 用户改动。
- 前端单元测试使用 Node.js 20；当前系统 /usr/bin/node 为 v12 时不得用它解释测试结果。

---

## File Structure

- Create: WebApps/ARServer/www/js/gyro-controller.js — 页面级方向权限、一次申请、暂停和恢复。
- Create: WebApps/ARServer/www/js/fullscreen-orientation.js — 全屏与横屏锁定的兼容协调。
- Create: WebApps/ARServer/www/js/audio-control-state.js — 从 audio 真实事件派生按钮状态。
- Create: tests/frontend/gyro-controller.test.mjs
- Create: tests/frontend/fullscreen-orientation.test.mjs
- Create: tests/frontend/audio-control-state.test.mjs
- Copy: pano/html/assets/krp/1.20.7/plugins/gyro2.js → WebApps/ARServer/www/assets/krp/plugins/gyro2.js
- Copy: img/beian_icon.png → WebApps/ARServer/www/assets/filing/beian_icon.png
- Modify: WebApps/ARServer/www/js/krpano-adapter.js — Gyro2、热点动作和场景事件桥。
- Modify: WebApps/ARServer/www/js/scene-dissolve.js — 自适应交叉叠化状态机。
- Modify: WebApps/ARServer/www/js/artwork-gallery.js — 移动端顶层图片手势。
- Modify: WebApps/ARServer/www/js/artwork-modal.js — 顶层查看器生命周期接线。
- Modify: WebApps/ARServer/www/js/museum-app.js — 统一协调新控制器。
- Modify: WebApps/ARServer/www/index.html — 图片查看层、横屏提示和备案图标节点。
- Modify: WebApps/ARServer/www/css/museum.css — 移动端查看层、触摸状态、转场和备案图标。
- Modify: tests/frontend/krpano-adapter.test.mjs
- Modify: tests/frontend/scene-dissolve.test.mjs
- Modify: tests/frontend/artwork-gallery.test.mjs
- Modify: tests/frontend/artwork-modal.test.mjs
- Modify: tests/frontend/museum-app-wiring.test.mjs
- Modify: tests/integration/museum_frontend_static_test.sh
- Modify: tests/integration/assets_manifest_test.sh

### Task 1: krpano Gyro2 适配与页面级权限控制

**Files:**
- Create: WebApps/ARServer/www/js/gyro-controller.js
- Create: tests/frontend/gyro-controller.test.mjs
- Copy: pano/html/assets/krp/1.20.7/plugins/gyro2.js
- Modify: tests/frontend/krpano-adapter.test.mjs
- Modify: WebApps/ARServer/www/js/krpano-adapter.js

**Interfaces:**
- Produces: GyroController 构造参数 adapter、deviceOrientation、onDenied。
- Produces: requestFromGesture(): Promise<boolean>、autoEnable(): Promise<boolean>、suspend(reason)、resume(reason)、destroy()。
- Produces: KrpanoAdapter.enableGyro()、disableGyro()、isGyroAvailable()。
- Produces: XML 插件 gyro，URL 为 /assets/krp/plugins/gyro2.js。

- [ ] **Step 1: 写入失败的适配器测试**

在 krpano-adapter.test.mjs 增加以下断言：

    test("场景 XML 注册 Gyro2 且默认由页面控制启用", () => {
      const xml = buildSceneXml(scene);
      assert.match(xml, /<plugin name="gyro" devices="html5" keep="true"/);
      assert.match(xml, /url="\/assets\/krp\/plugins\/gyro2\.js"/);
      assert.match(xml, /enabled="false"/);
    });

再使用记录 call 命令的假 player，断言 enableGyro() 和 disableGyro() 依次产生 gyro.enable(); 与 gyro.disable();，isGyroAvailable() 读取 plugin[gyro].available。

- [ ] **Step 2: 写入失败的权限控制器测试**

创建 gyro-controller.test.mjs，覆盖以下行为：

    test("iOS 同一页面只申请一次", async () => {
      let requests = 0;
      let enabled = 0;
      const controller = new GyroController({
        adapter: {
          isGyroAvailable: () => true,
          enableGyro: () => { enabled += 1; },
          disableGyro() {}
        },
        deviceOrientation: {
          requestPermission: async () => {
            requests += 1;
            return "granted";
          }
        }
      });
      assert.equal(await controller.requestFromGesture(), true);
      assert.equal(await controller.requestFromGesture(), true);
      assert.equal(requests, 1);
      assert.equal(enabled, 1);
    });

另一个测试先 autoEnable，再依次 suspend("modal")、suspend("drawer")、resume("modal")、resume("drawer")，断言最后一个原因解除后才再次 enable。

- [ ] **Step 3: 运行测试确认失败**

Run:

    node --test tests/frontend/krpano-adapter.test.mjs tests/frontend/gyro-controller.test.mjs

Expected: gyro-controller.js 不存在，且 Gyro2 XML 断言失败。

- [ ] **Step 4: 复制插件并实现最小接口**

Run:

    mkdir -p WebApps/ARServer/www/assets/krp/plugins
    cp pano/html/assets/krp/1.20.7/plugins/gyro2.js WebApps/ARServer/www/assets/krp/plugins/gyro2.js

gyro-controller.js 使用 permissionRequested、permissionGranted 和 Set 类型 suspensions。requestFromGesture() 用 Function.call 调用 iOS requestPermission；没有该函数时视为 granted。autoEnable() 声明为 async：存在 requestPermission 时返回 false 且不主动弹权限框，其他设备返回 requestFromGesture() 的结果。启停调用 adapter 的公开方法。

buildSceneXml() 加入 keep=true、enabled=false、camroll=true、friction=0.5 的 gyro 插件节点。适配器三个方法只读插件 available 或调用 gyro.enable()/gyro.disable()。

- [ ] **Step 5: 运行测试并提交**

    node --test tests/frontend/krpano-adapter.test.mjs tests/frontend/gyro-controller.test.mjs
    git add WebApps/ARServer/www/assets/krp/plugins/gyro2.js WebApps/ARServer/www/js/gyro-controller.js WebApps/ARServer/www/js/krpano-adapter.js tests/frontend/gyro-controller.test.mjs tests/frontend/krpano-adapter.test.mjs
    git commit -m "修复移动端陀螺仪控制"

Expected: 所有相关子测试通过。

### Task 2: 页面接入陀螺仪并随临时层暂停

**Files:**
- Modify: tests/frontend/museum-app-wiring.test.mjs
- Modify: WebApps/ARServer/www/js/museum-ui-state.js
- Modify: WebApps/ARServer/www/js/museum-app.js

**Interfaces:**
- Consumes: Task 1 的 GyroController。
- Produces: MuseumUiState.hasTransientLayer()，用于判断抽屉或视角面板是否打开。

- [ ] **Step 1: 写入失败的页面接线测试**

扩展 MuseumApp 测试桩以记录 gyroEnableCalls 和 gyroDisableCalls。首次向 panorama 分发 pointerdown 后断言启用一次；打开 scene-drawer 后断言禁用一次；关闭抽屉后断言恢复。再验证打开作品模态框时暂停。

- [ ] **Step 2: 运行测试确认失败**

    node --test tests/frontend/museum-app-wiring.test.mjs tests/frontend/museum-ui-state.test.mjs

Expected: 页面尚未创建或调用 GyroController。

- [ ] **Step 3: 实现页面接线**

museum-app.js 导入 GyroController，在 adapter 创建后实例化。onDenied 只显示一次“未能启用陀螺仪，仍可拖动浏览”。播放器初始化后调用 autoEnable()，全景第一次 pointerdown 调用 requestFromGesture()。

renderUiState() 使用 transient-ui 原因暂停或恢复；作品、登录和简介模态框使用 modal 原因。pagehide 调用 destroy()。状态只在对象内存中，不写存储。

- [ ] **Step 4: 运行测试并提交**

    node --test tests/frontend/gyro-controller.test.mjs tests/frontend/museum-ui-state.test.mjs tests/frontend/museum-app-wiring.test.mjs
    git add WebApps/ARServer/www/js/museum-app.js WebApps/ARServer/www/js/museum-ui-state.js tests/frontend/museum-app-wiring.test.mjs tests/frontend/museum-ui-state.test.mjs
    git commit -m "接入展馆陀螺仪权限流程"

Expected: 权限只申请一次，临时层关闭后才恢复。

### Task 3: 自适应 1.5 秒交叉叠化与首次预览

**Files:**
- Modify: tests/frontend/scene-dissolve.test.mjs
- Modify: tests/frontend/krpano-adapter.test.mjs
- Modify: WebApps/ARServer/www/js/scene-dissolve.js
- Modify: WebApps/ARServer/www/js/krpano-adapter.js
- Modify: WebApps/ARServer/www/js/museum-app.js
- Modify: WebApps/ARServer/www/css/museum.css

**Interfaces:**
- Produces: SceneDissolve.begin({ generation, fallbackUrl }): boolean。
- Produces: markPreviewVisible(generation)、complete(generation)、cancel(generation)。
- Produces: adapter 回调 onSceneEvent({ type, generation })，type 为 preview-visible 或 complete。

- [ ] **Step 1: 写入失败的慢网、首次进入和竞态测试**

更新现有 begin 调用为对象参数。新增测试必须断言：

1. begin({ generation: 4 }) 后立即进入 is-releasing。
2. 900ms 时尚无预览则进入 is-waiting，旧图仍有 src。
3. markPreviewVisible(3) 返回 false，markPreviewVisible(4) 返回 true 并进入 is-completing。
4. viewer 没有 canvas 时，begin({ generation: 1, fallbackUrl: "/preview.jpg" }) 使用该 URL。
5. cancel 当前 generation 后清空所有类、src 和计时器。

- [ ] **Step 2: 运行测试确认失败**

    node --test tests/frontend/scene-dissolve.test.mjs tests/frontend/krpano-adapter.test.mjs

Expected: 新状态和场景事件接口不存在。

- [ ] **Step 3: 实现状态机和事件桥**

SceneDissolve 使用 DEFAULT_DURATION_MS=1500、RELEASE_MS=900 和 COMPLETE_MIN_MS=300。begin() 优先捕获旧 canvas，无快照时使用 fallbackUrl；立刻进入 is-releasing。900ms 尚未收到预览事件时进入 is-waiting。

markPreviewVisible() 只接受当前 generation，从 is-releasing 或 is-waiting 进入 is-completing，并按已消耗时长选择剩余时间，但不得短于 300ms。CSS 中 is-waiting 的旧画面透明度固定为 .18，不能变为 0。

动态 XML 增加带 generation 的预览可见和完整加载 JS bridge。KrpanoAdapter 只转发 latestGeneration。switchScene() 拿到场景详情后立刻 begin({ generation, fallbackUrl: scene.previewUrl })，随后调用 loadScene()；不在 loadScene 完成后才启动。

- [ ] **Step 4: 运行测试并提交**

    node --test tests/frontend/scene-dissolve.test.mjs tests/frontend/krpano-adapter.test.mjs tests/frontend/museum-app-wiring.test.mjs
    git add WebApps/ARServer/www/js/scene-dissolve.js WebApps/ARServer/www/js/krpano-adapter.js WebApps/ARServer/www/js/museum-app.js WebApps/ARServer/www/css/museum.css tests/frontend/scene-dissolve.test.mjs tests/frontend/krpano-adapter.test.mjs tests/frontend/museum-app-wiring.test.mjs
    git commit -m "优化场景自适应叠化转场"

Expected: 正常、慢网、首次进入、快速切换和失败恢复测试均通过。

### Task 4: 移动端顶层作品图片手势

**Files:**
- Modify: WebApps/ARServer/www/index.html
- Modify: WebApps/ARServer/www/css/museum.css
- Modify: WebApps/ARServer/www/js/artwork-gallery.js
- Modify: WebApps/ARServer/www/js/artwork-modal.js
- Modify: tests/frontend/artwork-gallery.test.mjs
- Modify: tests/frontend/artwork-modal.test.mjs
- Modify: tests/integration/museum_frontend_static_test.sh

**Interfaces:**
- Produces: ArtworkGallery.openImmersive()、closeImmersive()、isImmersive()。
- Consumes: artwork-image-viewer、artwork-image-viewer-stage、artwork-image-viewer-image、artwork-image-viewer-close。

- [ ] **Step 1: 写入失败的手势测试**

扩展假元素支持多个 pointer 和计时器，新增断言：

- openImmersive() 后根层 aria-hidden=false，图片源等于当前图片。
- 两次间隔不超过 280ms、位置差不超过 24px 的点击将比例从 1 改为 2，且不退出。
- 单击计时器到期后在 scale=1 时退出。
- 两指初始距离 100、更新距离 500 时比例被限制为 4。
- scale>1 时单指拖动只改变偏移，不切换图片。
- scale=1 时超过 50px 横滑切换相邻图片。
- closeImmersive() 重置比例、偏移、活动 pointers 和待执行单击计时器。

- [ ] **Step 2: 运行测试确认失败**

    node --test tests/frontend/artwork-gallery.test.mjs tests/frontend/artwork-modal.test.mjs

Expected: 顶层查看器接口不存在。

- [ ] **Step 3: 增加结构和状态实现**

在作品弹窗后增加 role=dialog、aria-modal=true 的 artwork-image-viewer，内部包含关闭按钮、stage 和 img。查看层层级设为 80，使用固定定位、深色背景、安全区域 padding 和 touch-action:none。

ArtworkGallery 增加 immersiveScale、immersiveOffsetX/Y、Map 类型 pointers、singleTapTimer 和 savedScrollTop。缩放范围固定为 1–4，双击目标为 2。移动端点击普通图片打开查看层；PC 端现有工具继续工作。ArtworkModal.close() 和切换作品前调用 closeImmersive()。

在 max-width:820px 媒体查询隐藏 artwork-gallery-tools；PC 规则不隐藏。退出后把作品布局 scrollTop 恢复为 savedScrollTop。

- [ ] **Step 4: 运行测试并提交**

    node --test tests/frontend/artwork-gallery.test.mjs tests/frontend/artwork-modal.test.mjs
    bash tests/integration/museum_frontend_static_test.sh
    git add WebApps/ARServer/www/index.html WebApps/ARServer/www/css/museum.css WebApps/ARServer/www/js/artwork-gallery.js WebApps/ARServer/www/js/artwork-modal.js tests/frontend/artwork-gallery.test.mjs tests/frontend/artwork-modal.test.mjs tests/integration/museum_frontend_static_test.sh
    git commit -m "实现移动端作品大图手势浏览"

Expected: PC 工具仍存在，移动端顶层查看和手势测试通过。

### Task 5: 全屏后横屏锁定及兼容降级

**Files:**
- Create: WebApps/ARServer/www/js/fullscreen-orientation.js
- Create: tests/frontend/fullscreen-orientation.test.mjs
- Modify: WebApps/ARServer/www/index.html
- Modify: WebApps/ARServer/www/js/museum-app.js
- Modify: WebApps/ARServer/www/css/museum.css
- Modify: tests/frontend/museum-app-wiring.test.mjs

**Interfaces:**
- Produces: FullscreenOrientation({ documentObject, screenObject, onLandscapeFallback })。
- Produces: toggle(target): Promise<boolean> 和 handleFullscreenChange(target): boolean。

- [ ] **Step 1: 写入失败的兼容测试**

创建测试分别验证：

1. target.requestFullscreen() 成功后调用 screen.orientation.lock("landscape")。
2. 再次 toggle 时调用 document.exitFullscreen() 和 unlock()。
3. lock 抛错时 onLandscapeFallback 只调用一次，document.fullscreenElement 仍是 target。
4. requestFullscreen 抛错时返回 false，且不调用 lock。

- [ ] **Step 2: 运行测试确认失败**

    node --test tests/frontend/fullscreen-orientation.test.mjs tests/frontend/museum-app-wiring.test.mjs

Expected: 控制器不存在。

- [ ] **Step 3: 实现控制器和提示层**

控制器只在全屏成功后尝试 lock("landscape")；锁定失败不退出全屏。退出全屏后安全调用 unlock()。

index.html 增加 id=landscape-hint、role=status、hidden 的“请旋转手机横屏浏览”。museum-app.js 的回退回调显示提示，2500ms 后隐藏。fullscreenchange 继续负责按钮 is-fullscreen、title 和 aria-label。

- [ ] **Step 4: 运行测试并提交**

    node --test tests/frontend/fullscreen-orientation.test.mjs tests/frontend/museum-app-wiring.test.mjs
    bash tests/integration/museum_frontend_static_test.sh
    git add WebApps/ARServer/www/js/fullscreen-orientation.js WebApps/ARServer/www/js/museum-app.js WebApps/ARServer/www/index.html WebApps/ARServer/www/css/museum.css tests/frontend/fullscreen-orientation.test.mjs tests/frontend/museum-app-wiring.test.mjs tests/integration/museum_frontend_static_test.sh
    git commit -m "完善移动端横屏全屏体验"

Expected: 横屏成功、失败降级和退出解锁测试均通过。

### Task 6: 音乐真实状态同步与触屏样式修复

**Files:**
- Create: WebApps/ARServer/www/js/audio-control-state.js
- Create: tests/frontend/audio-control-state.test.mjs
- Modify: WebApps/ARServer/www/js/museum-app.js
- Modify: WebApps/ARServer/www/css/museum.css
- Modify: tests/frontend/museum-app-wiring.test.mjs

**Interfaces:**
- Produces: AudioControlState({ audio, button })。
- Produces: sync()、setUnavailable()、destroy()。

- [ ] **Step 1: 写入失败的音频事件测试**

创建假 audio 和 button，验证 play 事件且 paused=false 时才加入 is-playing；pause、ended、error、emptied 均移除 is-playing；无 src 时按钮 disabled=true。验证相同音乐 URL 的 configureMusic 不清空 src 或 currentTime。

- [ ] **Step 2: 运行测试确认失败**

    node --test tests/frontend/audio-control-state.test.mjs tests/frontend/museum-app-wiring.test.mjs

Expected: 音频状态控制器不存在。

- [ ] **Step 3: 实现统一状态并修正 CSS**

控制器监听 play、pause、ended、error 和 emptied。sync() 以“存在 src、paused=false、ended=false”为唯一播放判据，并同步 label/title。页面点击只调用 play() 或 pause()，不直接猜测按钮状态。

将 .icon-button:hover 从通用活动态选择器移出，仅放在以下媒体查询中：

    @media (hover: hover) and (pointer: fine) {
      .icon-button:hover {
        color: var(--accent-strong);
        border-color: var(--accent);
        background: var(--glass-strong);
      }
    }

is-playing、is-active 和 is-fullscreen 继续拥有金色状态。

- [ ] **Step 4: 运行测试并提交**

    node --test tests/frontend/audio-control-state.test.mjs tests/frontend/museum-app-wiring.test.mjs
    git add WebApps/ARServer/www/js/audio-control-state.js WebApps/ARServer/www/js/museum-app.js WebApps/ARServer/www/css/museum.css tests/frontend/audio-control-state.test.mjs tests/frontend/museum-app-wiring.test.mjs
    git commit -m "修复音乐播放按钮状态"

Expected: 触屏暂停后不再保留金色，音乐相同时持续播放。

### Task 7: 前进热点和展品热点动效

**Files:**
- Modify: tests/frontend/krpano-adapter.test.mjs
- Modify: WebApps/ARServer/www/js/krpano-adapter.js

**Interfaces:**
- Produces: buildSceneXml 第四参数 reducedMotion，默认 false。
- Consumes: hotspot.type 的 scene、artwork、text。

- [ ] **Step 1: 写入失败的热点 XML 测试**

构造同时含 scene 与 artwork 热点的场景，断言普通模式 XML：

- scene 热点 onloaded 调用 scene_hotspot_pulse。
- artwork 热点 onloaded 调用 artwork_hotspot_pulse。
- XML 含 scale、alpha、oy 的 tween。
- 两类 onclick 均先 stoptween 再调用 JingjieARHotspotBridge。

调用 buildSceneXml(scene, null, VIEW_MODES.NORMAL, true)，断言不含 hotspot_pulse。

- [ ] **Step 2: 运行测试确认失败**

    node --test tests/frontend/krpano-adapter.test.mjs

Expected: 动效动作不存在。

- [ ] **Step 3: 实现 krpano 动作**

前进热点在 0.75s 内做 scale 1→1.14、alpha 1→.65、oy 0→-12，再复位并循环。展品热点在 1.1s 内做 scale 1→1.12→1 和 alpha .85→1→.85。动作每轮先确认 caller 仍存在；点击先停止 caller 的 tween，再进入 JS bridge。reducedMotion 为真时不输出 onloaded 循环，只保留点击。

- [ ] **Step 4: 运行测试并提交**

    node --test tests/frontend/krpano-adapter.test.mjs
    git add WebApps/ARServer/www/js/krpano-adapter.js tests/frontend/krpano-adapter.test.mjs
    git commit -m "增强全景热点引导动效"

Expected: 两类热点动效可区分，减少动态效果时无循环。

### Task 8: 公安备案图标接口与静态资源检查

**Files:**
- Copy: img/beian_icon.png
- Modify: WebApps/ARServer/www/index.html
- Modify: WebApps/ARServer/www/css/museum.css
- Modify: tests/integration/museum_frontend_static_test.sh
- Modify: tests/integration/assets_manifest_test.sh

**Interfaces:**
- Produces: #police-filing-icon 和 /assets/filing/beian_icon.png。

- [ ] **Step 1: 写入失败的静态检查**

在静态测试加入：

    grep -Fq 'id="police-filing-icon"' "$index"
    grep -Fq 'src="/assets/filing/beian_icon.png"' "$index"
    grep -Fq 'class="police-filing-icon"' "$index"
    test -s WebApps/ARServer/www/assets/filing/beian_icon.png

- [ ] **Step 2: 运行测试确认失败**

    bash tests/integration/museum_frontend_static_test.sh
    bash tests/integration/assets_manifest_test.sh

Expected: 图标节点和网站静态资源不存在。

- [ ] **Step 3: 复制资源并修改链接**

Run:

    mkdir -p WebApps/ARServer/www/assets/filing
    cp img/beian_icon.png WebApps/ARServer/www/assets/filing/beian_icon.png

公安备案链接内部依次放替换说明注释、img 和文字 span。CSS 将链接设为 inline-flex，图标宽 .9rem、高 1rem、object-fit:contain。页面为图标注册一次 error 监听并设置 hidden=true，不能隐藏链接文字。

- [ ] **Step 4: 运行测试并提交**

    bash tests/integration/museum_frontend_static_test.sh
    bash tests/integration/assets_manifest_test.sh
    git add img/beian_icon.png WebApps/ARServer/www/assets/filing/beian_icon.png WebApps/ARServer/www/index.html WebApps/ARServer/www/css/museum.css tests/integration/museum_frontend_static_test.sh tests/integration/assets_manifest_test.sh
    git commit -m "增加公安联网备案图标接口"

Expected: 图标文件可访问，链接文字在图标失败时仍可见。

### Task 9: 完整回归与移动端人工验收

**Files:**
- Modify: 无。

- [ ] **Step 1: 运行全部前端单元测试**

    node --test tests/frontend/*.test.mjs

Expected: 全部子测试通过，无未处理 Promise 拒绝。

- [ ] **Step 2: 运行静态和资源测试**

    bash tests/integration/museum_frontend_static_test.sh
    bash tests/integration/assets_manifest_test.sh
    bash tests/integration/pano_migration_test.sh

Expected: 三个脚本均输出 PASS。

- [ ] **Step 3: 构建 ARServer 并执行相关 CTest**

    cmake -S . -B build-full -DCMAKE_BUILD_TYPE=Debug
    cmake --build build-full --target ar_server -j2
    ctest --test-dir build-full --output-on-failure -R 'museum_frontend|assets_manifest|pano_migration'

Expected: 构建完成且所选测试 100% 通过。

- [ ] **Step 4: 本地人工验证**

    set -a
    . ./.env.arserver
    set +a
    ./build-full/bin/ar_server

在同一局域网手机通过 HTTPS 测试地址打开站点并验证：iOS 每次刷新重新走权限流程；Android 转动设备可改变视角；全屏优先横屏；作品大图支持双击、双指和退出；慢速网络切换无完整黑屏；暂停音乐后按钮立即恢复灰白；两类热点动效可区分；备案图标可见。

- [ ] **Step 5: 检查差异和工作树**

    git diff --check
    git status --short
    git log --oneline -9

Expected: 无空白错误；README.md 仍保持用户原有改动，任务文件均已由各任务提交。

## Self-Review

- Spec coverage: Task 1–2 覆盖陀螺仪、每次页面重新申请及临时层暂停；Task 3 覆盖加载并行、首次预览、慢网防黑屏和 generation；Task 4 覆盖移动端顶层图片全部手势；Task 5 覆盖横屏及 iOS 降级；Task 6 覆盖音乐真实状态；Task 7 覆盖两类热点与减少动态效果；Task 8 覆盖备案图标；Task 9 覆盖回归和真机验收。
- Placeholder scan: 计划不含未定义的占位实现；所有新增模块、接口、测试命令、常量和提交边界均已明确。
- Type consistency: GyroController、FullscreenOrientation、AudioControlState、SceneDissolve、ArtworkGallery 的方法名在生产代码与测试任务中保持一致。
- Scope: 未引入新后端接口或框架，未替换 krpano，不改变 PC 端图片工具。

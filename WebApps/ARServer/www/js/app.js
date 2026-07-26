(function () {
    'use strict';
    var currentToken = localStorage.getItem('jingjie.token') || '';
    var currentUser = localStorage.getItem('jingjie.user') || '';
    var currentScene = null;
    var heartbeatTimer = null;
    var membersTimer = null;
    var musicPlaying = false;
    var sceneEpoch = 0;
    var commentCursor = 0;
    var MIN_PANORAMA_FOV = 35, MAX_PANORAMA_FOV = 100, DEFAULT_PANORAMA_FOV = 80;
    var panoramaZoomCleanup = null;
    var $ = function (id) { return document.getElementById(id); };

    function request(url, options) {
        options = options || {};
        options.headers = options.headers || {};
        return fetch(url, options).then(function (response) {
            return response.json().catch(function () { return {}; }).then(function (body) {
                if (!response.ok) { var error = new Error(body.error || ('HTTP ' + response.status)); error.status = response.status; throw error; }
                return body;
            });
        });
    }
    function authHeaders() { return currentToken ? { 'Authorization': 'Bearer ' + currentToken } : {}; }
    function showToast(message) { var toast = $('toast'); toast.textContent = message; toast.classList.remove('hidden'); window.setTimeout(function () { toast.classList.add('hidden'); }, 2500); }
    function setAuthButton() { $('auth-open').textContent = currentToken ? (currentUser + ' · 已登录') : '注册 / 登录'; }
    function openAuthModal() { $('auth-modal').classList.remove('hidden'); $('auth-username').focus(); }
    function closeAuthModal() { $('auth-modal').classList.add('hidden'); }
    function requireLogin() { if (currentToken) return true; showToast('请先登录后再进行点赞/评论'); openAuthModal(); return false; }

    function renderScenes(scenes) {
        var grid = $('scene-grid'); grid.textContent = '';
        scenes.forEach(function (scene) {
            var card = document.createElement('button'); card.type = 'button'; card.className = 'scene-card';
            var image = document.createElement('img'); image.src = scene.thumbnail_url || scene.panorama_url; image.alt = scene.name;
            var label = document.createElement('span'); label.textContent = scene.name;
            card.appendChild(image); card.appendChild(label); card.addEventListener('click', function () { window.openScene(scene); }); grid.appendChild(card);
        });
        $('scene-total').textContent = scenes.length + ' 个场景';
    }
    function loadScenes() { request('/api/scenes').then(function (data) { renderScenes(data.scenes || []); }).catch(function () { $('scene-grid').textContent = '场景加载失败，请检查服务。'; }); }
    function enterSession(scene, epoch) {
        if (!currentToken) return;
        request('/api/session/enter?token=' + encodeURIComponent(currentToken) + '&scene=' + encodeURIComponent(scene.id), { method: 'POST' })
            .then(function () { if (currentScene === scene && sceneEpoch === epoch) startPresence(); }).catch(function (error) { if (sceneEpoch === epoch) showToast(error.message); });
    }
    function heartbeat() {
        if (!currentToken || !currentScene) return;
        request('/api/session/heartbeat?token=' + encodeURIComponent(currentToken) + '&scene=' + encodeURIComponent(currentScene.id), { method: 'POST' }).catch(function () {});
    }
    function updateMembers() {
        if (!currentScene) return;
        request('/api/scenes/' + encodeURIComponent(currentScene.id) + '/members').then(function (data) { $('online-count').textContent = (data.members || []).length; }).catch(function () { $('online-count').textContent = '—'; });
    }
    function startPresence() { stopHeartbeat(); heartbeat(); updateMembers(); heartbeatTimer = window.setInterval(heartbeat, 10000); membersTimer = window.setInterval(updateMembers, 10000); }
    function stopHeartbeat() { if (heartbeatTimer) window.clearInterval(heartbeatTimer); if (membersTimer) window.clearInterval(membersTimer); heartbeatTimer = null; membersTimer = null; }
    function clearPanoramaZoom() { if (panoramaZoomCleanup) panoramaZoomCleanup(); panoramaZoomCleanup = null; }
    function bindPanoramaZoom(aframeScene) {
        clearPanoramaZoom();
        var host = $('aframe-host'), lastDistance = 0, camera = aframeScene.camera;
        function updateFov(delta) { if (!camera) return; camera.fov = Math.max(MIN_PANORAMA_FOV, Math.min(MAX_PANORAMA_FOV, camera.fov + delta)); camera.updateProjectionMatrix(); }
        function onWheel(event) { event.preventDefault(); updateFov(event.deltaY * 0.04); }
        function distance(touches) { var x = touches[0].clientX - touches[1].clientX, y = touches[0].clientY - touches[1].clientY; return Math.sqrt(x * x + y * y); }
        function onTouchStart(event) { if (event.touches.length === 2) lastDistance = distance(event.touches); }
        function onTouchMove(event) { if (event.touches.length !== 2 || !lastDistance) return; var current = distance(event.touches); updateFov((lastDistance - current) * 0.12); lastDistance = current; event.preventDefault(); }
        function onTouchEnd(event) { if (event.touches.length < 2) lastDistance = 0; }
        host.addEventListener('wheel', onWheel, { passive: false });
        host.addEventListener('touchstart', onTouchStart, { passive: true });
        host.addEventListener('touchmove', onTouchMove, { passive: false });
        host.addEventListener('touchend', onTouchEnd);
        panoramaZoomCleanup = function () { host.removeEventListener('wheel', onWheel); host.removeEventListener('touchstart', onTouchStart); host.removeEventListener('touchmove', onTouchMove); host.removeEventListener('touchend', onTouchEnd); };
    }
    function destroyAFrameScene() { clearPanoramaZoom(); $('aframe-host').textContent = ''; }
    function loadHighResolutionPanorama(scene, sky, epoch) {
        var previewUrl = scene.preview_url || scene.panorama_url;
        if (!scene.panorama_url || scene.panorama_url === previewUrl) return;
        var highResolution = new Image();
        highResolution.onload = function () {
            if (sceneEpoch !== epoch || currentScene !== scene || !sky.parentNode) return;
            sky.setAttribute('src', scene.panorama_url);
        };
        highResolution.onerror = function () {};
        highResolution.src = scene.panorama_url;
    }
    function createAFrameScene(scene, epoch) {
        destroyAFrameScene();
        var host = $('aframe-host');
        if (!window.AFRAME) { showToast('全景播放器加载失败'); return; }
        var aframeScene = document.createElement('a-scene'); aframeScene.setAttribute('embedded', ''); aframeScene.setAttribute('xr-mode-ui', 'enabled: false'); aframeScene.setAttribute('device-orientation-permission-ui', 'enabled: true');
        var previewUrl = scene.preview_url || scene.panorama_url;
        var sky = document.createElement('a-sky'); sky.setAttribute('src', previewUrl); sky.setAttribute('rotation', '0 -90 0');
        sky.addEventListener('materialtextureloaded', function () { loadHighResolutionPanorama(scene, sky, epoch); }, { once: true });
        aframeScene.appendChild(sky); host.appendChild(aframeScene);
        aframeScene.addEventListener('loaded', function () { if (aframeScene.camera) { aframeScene.camera.el.setAttribute('look-controls', 'reverseMouseDrag: true'); aframeScene.camera.fov = DEFAULT_PANORAMA_FOV; aframeScene.camera.updateProjectionMatrix(); bindPanoramaZoom(aframeScene); } });
    }
    function updateDetail(scene, epoch) {
        if (!currentScene) return;
        request('/api/scenes/' + encodeURIComponent(scene.id)).then(function (detail) { if (currentScene !== scene || sceneEpoch !== epoch) return; $('like-count').textContent = detail.like_count || 0; scene.music_url = detail.music_url; $('music-button').disabled = !scene.music_url; $('music-button').textContent = scene.music_url ? '播放音乐' : '音乐未配置'; if (scene.music_url) prepareMusic(scene.music_url); }).catch(function () {});
    }
    function prepareMusic(url) { var audio = $('scene-audio'); audio.src = url; audio.play().then(function () { musicPlaying = true; $('music-button').textContent = '暂停音乐'; }).catch(function () { musicPlaying = false; $('music-button').textContent = '播放音乐'; }); }
    function stopMusic() { var audio = $('scene-audio'); audio.pause(); audio.removeAttribute('src'); audio.load(); musicPlaying = false; }

    window.openScene = function (scene) {
        var epoch = ++sceneEpoch; scene.liked = false; currentScene = scene; commentCursor = 0; $('viewer-name').textContent = scene.name; $('viewer-kicker').textContent = '360° PANORAMA'; $('online-count').textContent = '0'; $('like-count').textContent = '0'; $('music-button').disabled = true; $('music-button').textContent = '音乐未配置'; $('viewer').classList.remove('hidden'); createAFrameScene(scene, epoch); updateDetail(scene, epoch); window.history.pushState({ scene: scene.id }, '', '#scene=' + encodeURIComponent(scene.id)); enterSession(scene, epoch);
    };
    window.closeScene = function () {
        var scene = currentScene; ++sceneEpoch; stopHeartbeat(); stopMusic(); $('comments-drawer').classList.add('hidden'); destroyAFrameScene(); $('viewer').classList.add('hidden'); currentScene = null;
        if (currentToken && scene) request('/api/session/exit?token=' + encodeURIComponent(currentToken), { method: 'POST' }).catch(function () {});
        if (location.hash.indexOf('#scene=') === 0) history.replaceState({}, '', location.pathname);
    };
    window.toggleLike = function () {
        if (!requireLogin() || !currentScene) return;
        var method = currentScene.liked ? 'DELETE' : 'POST';
        request('/api/scenes/' + encodeURIComponent(currentScene.id) + '/likes', { method: method, headers: authHeaders() }).then(function (data) { currentScene.liked = data.liked; $('like-count').textContent = data.like_count; $('like-button').firstChild.nodeValue = data.liked ? '♥ ' : '♡ '; }).catch(function (error) { showToast(error.message); });
    };
    window.openComments = function () {
        if (!currentScene || !requireLogin()) return; $('comments-drawer').classList.remove('hidden'); commentCursor = 0; $('comment-list').textContent = ''; loadComments(false);
    };
    function loadComments(append) {
        if (!currentScene) return;
        var scene = currentScene; var cursor = commentCursor;
        request('/api/scenes/' + encodeURIComponent(scene.id) + '/comments' + (cursor ? '?before=' + encodeURIComponent(cursor) : '')).then(function (data) {
            if (currentScene !== scene) return; var list = $('comment-list'); if (!append) list.textContent = ''; var comments = data.comments || [];
            if (!comments.length) { if (!append) list.textContent = '还没有评论，来留下第一句话吧。'; $('comments-more').classList.add('hidden'); return; }
            comments.forEach(function (comment) { var item = document.createElement('article'); item.className = 'comment-item'; var user = document.createElement('strong'); user.className = 'comment-user'; user.textContent = comment.username; var content = document.createElement('p'); content.textContent = comment.content; item.appendChild(user); item.appendChild(content); list.appendChild(item); }); commentCursor = data.next_before || 0; $('comments-more').classList.toggle('hidden', !commentCursor);
        }).catch(function (error) { showToast(error.message); });
    }
    window.submitComment = function () {
        if (!requireLogin() || !currentScene) return;
        var input = $('comment-input'); var content = input.value.trim(); if (!content) { showToast('评论不能为空'); return; }
        var headers = authHeaders(); headers['Content-Type'] = 'application/json';
        request('/api/scenes/' + encodeURIComponent(currentScene.id) + '/comments', { method: 'POST', headers: headers, body: JSON.stringify({ content: content }) }).then(function () { input.value = ''; commentCursor = 0; loadComments(false); }).catch(function (error) { showToast(error.message); });
    };
    window.toggleMusic = function () { var audio = $('scene-audio'); if (!currentScene || !currentScene.music_url) { showToast('音乐未配置'); return; } if (musicPlaying) { audio.pause(); musicPlaying = false; $('music-button').textContent = '播放音乐'; } else { audio.play().then(function () { musicPlaying = true; $('music-button').textContent = '暂停音乐'; }).catch(function () { showToast('浏览器需要手动允许播放音乐'); }); } };
    window.toggleFullscreen = function () { var element = $('viewer'); if (!document.fullscreenElement && element.requestFullscreen) element.requestFullscreen(); else if (document.exitFullscreen) document.exitFullscreen(); };
    function login() { var username = $('auth-username').value.trim(), password = $('auth-password').value; $('auth-message').textContent = ''; if (!username || !password) { $('auth-message').textContent = '请输入用户名和密码'; return; } request('/api/auth', { method: 'POST', headers: { 'Content-Type':'application/json' }, body: JSON.stringify({ username:username, password:password }) }).then(function (data) { currentToken=data.session_token; currentUser=data.username; localStorage.setItem('jingjie.token',currentToken); localStorage.setItem('jingjie.user',currentUser); setAuthButton(); closeAuthModal(); showToast(data.is_new ? '注册并登录成功' : '登录成功'); if (currentScene) enterSession(currentScene, sceneEpoch); }).catch(function (error) { $('auth-message').textContent=error.message || '认证失败'; }); }
    $('scene-audio').addEventListener('ended', function () { musicPlaying = false; if (currentScene && currentScene.music_url) $('music-button').textContent = '播放音乐'; }); $('auth-open').addEventListener('click', openAuthModal); $('auth-close').addEventListener('click', closeAuthModal); $('auth-submit').addEventListener('click', login); $('auth-password').addEventListener('keydown', function (event) { if (event.key === 'Enter') login(); }); $('exit-button').addEventListener('click', window.closeScene); $('fullscreen-button').addEventListener('click', window.toggleFullscreen); $('music-button').addEventListener('click', window.toggleMusic); $('like-button').addEventListener('click', window.toggleLike); $('comments-button').addEventListener('click', window.openComments); $('comments-close').addEventListener('click', function () { $('comments-drawer').classList.add('hidden'); }); $('comments-more').addEventListener('click', function () { loadComments(true); }); $('comment-submit').addEventListener('click', window.submitComment); $('comment-input').addEventListener('keydown', function (event) { if (event.key === 'Enter') window.submitComment(); }); window.addEventListener('popstate', function () { if (currentScene) window.closeScene(); });
    setAuthButton(); loadScenes();
})();

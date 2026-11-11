// Runs inside the already selected document. No player or rendering surface is created here.
(() => {
  const bindingSlot = Symbol.for('Aliyat.HibernationAgent.VideoPage');

  function safeUrl(raw) {
    try {
      const url = new URL(raw, location.href);
      if (!/^https?:$/.test(url.protocol) || url.username || url.password) return '';
      for (const key of url.searchParams.keys()) {
        if (/token|csrf|password|session|ticket|auth/i.test(key)) return '';
      }
      url.hash = '';
      return url.href.length <= 768 ? url.href : '';
    } catch {
      return '';
    }
  }

  function candidates(visibleOnly = false) {
    const entries = [];
    function visit(root, depth) {
      if (depth > 4) return;
      for (const video of root.querySelectorAll('video,bwp-video')) {
        const visible = video.getBoundingClientRect().width > 0;
        if (visibleOnly && !visible) continue;
        const primary = video.closest('.bpx-player-video-wrap,#bilibili-player,.bpx-player-container');
        const score = (primary ? 2e9 : 0) + (visible ? 1e9 : 0)
          + (video.videoWidth || 0) * (video.videoHeight || 0) + (video.paused ? 0 : 1000);
        entries.push({video, score});
      }
      for (const node of root.querySelectorAll('*')) {
        if (node.shadowRoot) visit(node.shadowRoot, depth + 1);
      }
    }
    visit(document, 0);
    return entries.sort((a,b) => b.score - a.score).map(entry => entry.video);
  }

  const sourceOf = video => String(video.currentSrc || video.src || '');
  function mediaId(video) {
    let hash = 2166136261;
    for (const ch of sourceOf(video)) hash = Math.imul(hash ^ ch.charCodeAt(0), 16777619);
    return (hash >>> 0).toString(16);
  }

  function media(video) {
    if (!video) return null;
    return {
      paused: !!video.paused, ended: !!video.ended, seeking: !!video.seeking,
      playback_rate: Number.isFinite(video.playbackRate) ? video.playbackRate : 1,
      current_time: Number.isFinite(video.currentTime) ? video.currentTime : 0,
      duration: Number.isFinite(video.duration) ? video.duration : null,
      ready_state: video.readyState || 0, width: video.videoWidth || 0,
      height: video.videoHeight || 0, media_id: mediaId(video),
      source_kind: sourceOf(video).startsWith('blob:') ? 'browser_media' : 'element',
      error: video.error ? String(video.error.code) : null
    };
  }

  const hasFrame = video => !!video && video.videoWidth > 0 && video.videoHeight > 0 && video.readyState >= 2;
  function target() {
    const videos = candidates();
    return videos.find(hasFrame) || videos[0];
  }

  function inspect(video = target()) {
    return {source_url: safeUrl(location.href), title: document.title.slice(0,240), video: media(video)};
  }

  function frame(video = target()) {
    const info = inspect(video);
    if (!hasFrame(video)) return {ok:false,reason:'video_not_ready',video:info.video};
    if (video.mediaKeys) return {ok:false,reason:'protected_video',video:info.video};
    const scale = Math.min(1, 480 / video.videoWidth, 270 / video.videoHeight);
    const canvas = document.createElement('canvas');
    canvas.width = Math.max(1, Math.floor(video.videoWidth * scale));
    canvas.height = Math.max(1, Math.floor(video.videoHeight * scale));
    try {
      canvas.getContext('2d').drawImage(video, 0, 0, canvas.width, canvas.height);
      const encoded = canvas.toDataURL('image/jpeg', .5);
      const base64 = encoded.slice(encoded.indexOf(',') + 1);
      if (!encoded.startsWith('data:image/jpeg;base64,') || !base64) {
        return {ok:false,reason:'video_surface_unsupported',video:info.video};
      }
      if (base64.length > 44000) return {ok:false,reason:'video_frame_too_large'};
      return {
        ok:true, ...info, frame_time_seconds:info.video.current_time,
        width:canvas.width, height:canvas.height, image:{mime_type:'image/jpeg',base64},
        observation_kind:'video_element_frame', screenshot_used:false
      };
    } catch (error) {
      return {ok:false,reason:error.name === 'SecurityError' ? 'video_canvas_cross_origin' : 'video_surface_unsupported',video:info.video};
    }
  }

  function bind(expectedUrl, selector) {
    let expected;
    try { expected = new URL(expectedUrl); } catch { return {ok:false,reason:'invalid_page_url'}; }
    if (!safeUrl(expected.href) || expected.href !== location.href) return {ok:false,reason:'page_mismatch'};
    let video;
    if (selector) {
      if (typeof selector !== 'string' || selector.length > 256) return {ok:false,reason:'invalid_video_selector'};
      try {
        const matches = document.querySelectorAll(selector);
        if (matches.length !== 1 || !matches[0].matches('video,bwp-video')) {
          return {ok:false,reason:'video_selector_not_unique'};
        }
        video = matches[0];
      } catch {
        return {ok:false,reason:'invalid_video_selector'};
      }
    } else video = target();
    if (!hasFrame(video)) return {ok:false,reason:'video_not_ready'};
    const bytes = crypto.getRandomValues(new Uint8Array(16));
    const id = Array.from(bytes, byte => byte.toString(16).padStart(2,'0')).join('');
    window[bindingSlot] = {id,video,url:location.href,source:sourceOf(video),stream:video.srcObject};
    return {ok:true,binding_id:id,...inspect(video)};
  }

  function boundFrame(id) {
    const binding = window[bindingSlot];
    if (!binding || binding.id !== id || binding.url !== location.href) return {ok:false,reason:'page_binding_expired'};
    const video = binding.video;
    if (!video.isConnected || sourceOf(video) !== binding.source || video.srcObject !== binding.stream) {
      return {ok:false,reason:'bound_video_changed'};
    }
    return frame(video);
  }

  return {candidates,media,safeUrl,inspect,frame,bind,boundFrame};
})()

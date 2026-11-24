import { vSub, vLen } from './math3d.js';
import { Camera, ViewPreset } from './camera.js';
import { MatcapKind, matcapName } from './matcap.js';
import { parseStlBuffer } from './stl_parser.js';
import { DEMO_MODELS, generateDemoStl } from './demo_models.js';
import { Renderer } from './renderer.js';

const $ = (id) => document.getElementById(id);

function formatCount(n) {
  if (n >= 1e6) return `${(n / 1e6).toFixed(2)}M`;
  if (n >= 1e3) return `${(n / 1e3).toFixed(1)}K`;
  return `${n}`;
}

const isLowSpecDevice =
  (typeof navigator !== 'undefined' && navigator.hardwareConcurrency && navigator.hardwareConcurrency <= 4) ||
  (typeof window !== 'undefined' && window.matchMedia && window.matchMedia('(pointer: coarse)').matches) ||
  (typeof window !== 'undefined' && window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches);

const state = {
  darkTheme: true,
  matcap: MatcapKind.Clay,
  wireframe: false,
  xray: false,
  twoSided: false,
  showGrid: true,
  showHud: true,
  showHelp: false,
  ecoMode: !!isLowSpecDevice,

  sliceEnabled: false,
  sliceAxis: 2,
  sliceFlip: false,
  sliceT: 0.5,

  measureMode: false,
  measureHasA: false,
  measureHasB: false,
  measureA: { x: 0, y: 0, z: 0 },
  measureB: { x: 0, y: 0, z: 0 },

  activeModelKey: 'demo:flange-housing',
  loadedFiles: [], // { name, file }
  loadedIndex: -1,
};

let currentMesh = null;
let lastStats = null;
let cam = new Camera();
let renderer = null;
let worker = null;
let parseSeq = 0;
let rafId = 0;
let lastAnimTime = 0;
let statusTimer = 0;
let transientStatus = '';

const canvas = $('gl-canvas');

function getEffectiveDpr() {
  const raw = window.devicePixelRatio || 1;
  return state.ecoMode ? Math.min(raw, 1.25) : Math.min(raw, 2.0);
}

function getTargetSamples() {
  return state.ecoMode ? 4 : 16;
}

function initWorker() {
  try {
    worker = new Worker('./js/stl_worker.js', { type: 'module' });
  } catch {
    worker = null;
  }
}

function parseStlAsync(buffer, name) {
  if (!worker) {
    return Promise.resolve(parseStlBuffer(buffer, name));
  }
  const id = ++parseSeq;
  return new Promise((resolve) => {
    const onMsg = (ev) => {
      if (!ev.data || ev.data.id !== id) return;
      worker.removeEventListener('message', onMsg);
      worker.removeEventListener('error', onErr);
      resolve(ev.data.result);
    };
    const onErr = () => {
      worker.removeEventListener('message', onMsg);
      worker.removeEventListener('error', onErr);
      resolve(parseStlBuffer(buffer, name));
    };
    worker.addEventListener('message', onMsg);
    worker.addEventListener('error', onErr);
    worker.postMessage({ id, buffer, name }, [buffer]);
  });
}

function showStatus(msg, durationMs = 2000) {
  transientStatus = msg;
  if (statusTimer) clearTimeout(statusTimer);
  updateStatusPill();
  if (durationMs > 0) {
    statusTimer = setTimeout(() => {
      transientStatus = '';
      updateStatusPill();
    }, durationMs);
  }
}

function updateStatusPill() {
  const dot = $('idle-dot');
  const txt = $('status-text');
  const ecoBtn = $('btn-eco');
  ecoBtn.setAttribute('aria-pressed', String(state.ecoMode));
  ecoBtn.textContent = state.ecoMode ? 'Eco' : 'Crisp';

  if (transientStatus) {
    dot.classList.remove('settled');
    txt.textContent = transientStatus;
    return;
  }

  const samples = renderer ? renderer.accumCount : 0;
  const target = getTargetSamples();
  const settled = renderer && !renderer.needsMoreSamples() && !cam.isAnimating();
  dot.classList.toggle('settled', !!settled);
  if (settled) {
    txt.textContent = `Idle · ${samples}× SSAA`;
  } else {
    txt.textContent = `SSAA ${samples}/${target}`;
  }
}

function updateInfoPill() {
  const nameEl = $('info-name');
  const metricsEl = $('info-metrics');
  if (!currentMesh || !currentMesh.ok) {
    nameEl.textContent = 'No model';
    metricsEl.textContent = '';
    return;
  }

  const sib =
    state.loadedFiles.length > 1 && state.loadedIndex >= 0
      ? ` [${state.loadedIndex + 1}/${state.loadedFiles.length}]`
      : '';
  nameEl.textContent = `${currentMesh.name}${sib}`;

  const tags = [];
  if (cam.isOrtho()) tags.push('Ortho');
  if (cam.getUpAxis() === 'yup') tags.push('Y-Up');
  const tagStr = tags.length ? ` · ${tags.join(' · ')}` : '';
  const vramMb = (currentMesh.vramBytes / (1024 * 1024)).toFixed(2);
  const ext = currentMesh.bboxExtent;

  metricsEl.textContent =
    `${formatCount(currentMesh.triCount)} tri · ` +
    `${ext.x.toFixed(1)}×${ext.y.toFixed(1)}×${ext.z.toFixed(1)} mm · ` +
    `${(currentMesh.volumeMm3 * 0.001).toFixed(1)} cm³ · ` +
    `${currentMesh.loadMs.toFixed(0)} ms · ${vramMb} MB VRAM${tagStr}`;
}

function updateSliceUi() {
  const pill = $('slice-pill');
  pill.classList.toggle('hidden', !(state.sliceEnabled && currentMesh && currentMesh.ok));
  if (!state.sliceEnabled || !currentMesh || !currentMesh.ok) return;

  [$('btn-slice-x'), $('btn-slice-y'), $('btn-slice-z')].forEach((b, idx) => {
    b.classList.toggle('active', state.sliceAxis === idx);
  });
  $('btn-slice-flip').setAttribute('aria-pressed', String(state.sliceFlip));
  $('slice-slider').value = String(state.sliceT);

  const ax = state.sliceAxis;
  const minV = ax === 0 ? currentMesh.bboxMin.x : ax === 1 ? currentMesh.bboxMin.y : currentMesh.bboxMin.z;
  const extV = ax === 0 ? currentMesh.bboxExtent.x : ax === 1 ? currentMesh.bboxExtent.y : currentMesh.bboxExtent.z;
  $('slice-mm').textContent = `${(minV + state.sliceT * extV).toFixed(1)} mm`;
}

function updateMeasureUi() {
  const pill = $('measure-pill');
  const txt = $('measure-text');
  canvas.classList.toggle('measure-cursor', state.measureMode);
  pill.classList.toggle('hidden', !state.measureMode);
  if (!state.measureMode) return;

  if (!state.measureHasA) {
    txt.textContent = 'Click surface for Point A · Esc cancel';
  } else if (!state.measureHasB) {
    txt.textContent = 'Point A set · Click surface for Point B';
  } else {
    const d = vSub(state.measureB, state.measureA);
    const dist = vLen(d);
    txt.textContent = `Dist: ${dist.toFixed(2)} mm (ΔX ${Math.abs(d.x).toFixed(2)} · ΔY ${Math.abs(d.y).toFixed(2)} · ΔZ ${Math.abs(d.z).toFixed(2)})`;
  }
}

function updateToolbarUi() {
  document.body.classList.toggle('theme-light', !state.darkTheme);
  document.body.classList.toggle('theme-dark', state.darkTheme);

  $('lbl-matcap').textContent = matcapName(state.matcap);
  $('lbl-theme').textContent = state.darkTheme ? 'Dark' : 'Light';
  $('btn-wire').setAttribute('aria-pressed', String(state.wireframe));
  $('btn-slice').setAttribute('aria-pressed', String(state.sliceEnabled));
  $('btn-measure').setAttribute('aria-pressed', String(state.measureMode));
  $('btn-xray').setAttribute('aria-pressed', String(state.xray));
  $('btn-twosided').setAttribute('aria-pressed', String(state.twoSided));
  $('btn-help').setAttribute('aria-pressed', String(state.showHelp));

  $('btn-ortho').setAttribute('aria-pressed', String(cam.isOrtho()));
  $('btn-yup').setAttribute('aria-pressed', String(cam.getUpAxis() === 'yup'));
  $('btn-grid').setAttribute('aria-pressed', String(state.showGrid));

  $('hud-layer').classList.toggle('hidden', !state.showHud);
  $('btn-restore-hud').classList.toggle('hidden', state.showHud);
  $('help-modal').classList.toggle('hidden', !state.showHelp);

  if (state.showHelp && renderer) {
    const chunkInfo = lastStats ? `${lastStats.visibleChunks}/${lastStats.totalChunks} chunks (${lastStats.drawCalls} draws)` : '—';
    $('engine-diag').textContent =
      `8 B/v UNORM16 · ${chunkInfo} · ${renderer.fp16Fbo ? 'RGBA16F' : 'RGBA8'} · ${renderer.glInfo}`;
  }

  updateInfoPill();
  updateSliceUi();
  updateMeasureUi();
  updateStatusPill();
}

function updateModelPopover() {
  const demoList = $('demo-list');
  demoList.innerHTML = '';
  for (const dm of DEMO_MODELS) {
    const key = `demo:${dm.id}`;
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = `pop-item${state.activeModelKey === key ? ' active' : ''}`;
    btn.textContent = dm.label;
    btn.addEventListener('click', () => {
      closePopovers();
      loadDemoModel(dm.id);
    });
    demoList.appendChild(btn);
  }

  const loadedSec = $('loaded-section');
  const loadedList = $('loaded-list');
  if (state.loadedFiles.length > 0) {
    loadedSec.classList.remove('hidden');
    loadedList.innerHTML = '';
    state.loadedFiles.forEach((entry, idx) => {
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = `pop-item${state.activeModelKey === `file:${idx}` ? ' active' : ''}`;
      btn.textContent = entry.name;
      btn.addEventListener('click', () => {
        closePopovers();
        loadLocalFileAt(idx);
      });
      loadedList.appendChild(btn);
    });
  } else {
    loadedSec.classList.add('hidden');
  }
}

function closePopovers() {
  $('model-popover').classList.add('hidden');
  $('btn-model-menu').setAttribute('aria-expanded', 'false');
  $('view-popover').classList.add('hidden');
  $('triad-btn').setAttribute('aria-expanded', 'false');
}

function updateAxisLabels(axisTipsCss) {
  const lx = $('axis-lbl-x');
  const ly = $('axis-lbl-y');
  const lz = $('axis-lbl-z');
  if (!axisTipsCss || !state.showHud) return;
  lx.style.left = `${axisTipsCss[0].x}px`;
  lx.style.top = `${axisTipsCss[0].y}px`;
  ly.style.left = `${axisTipsCss[1].x}px`;
  ly.style.top = `${axisTipsCss[1].y}px`;
  lz.style.left = `${axisTipsCss[2].x}px`;
  lz.style.top = `${axisTipsCss[2].y}px`;
}

function resizeCanvas() {
  if (!renderer) return;
  const dpr = getEffectiveDpr();
  const cssW = Math.max(1, window.innerWidth);
  const cssH = Math.max(1, window.innerHeight);
  const pxW = Math.max(1, Math.round(cssW * dpr));
  const pxH = Math.max(1, Math.round(cssH * dpr));
  if (canvas.width !== pxW || canvas.height !== pxH) {
    canvas.width = pxW;
    canvas.height = pxH;
  }
  renderer.resize(pxW, pxH, dpr);
}

function requestFrame(resetAccum = false) {
  if (!renderer) return;
  if (resetAccum) renderer.resetAccumulation();
  if (document.hidden) return;
  if (!rafId) {
    lastAnimTime = performance.now();
    rafId = requestAnimationFrame(onAnimationFrame);
  }
}

function onAnimationFrame(now) {
  rafId = 0;
  if (document.hidden || !renderer) return;

  const dt = Math.min(0.1, Math.max(0.0, (now - lastAnimTime) * 0.001));
  lastAnimTime = now;

  if (cam.isAnimating()) {
    cam.step(dt);
    renderer.resetAccumulation();
  }

  const { cstats, axisTipsCss } = renderer.renderFrame(currentMesh, cam, state);
  lastStats = cstats;
  updateAxisLabels(axisTipsCss);
  updateStatusPill();

  if (cam.isAnimating() || renderer.needsMoreSamples()) {
    rafId = requestAnimationFrame(onAnimationFrame);
  }
}

async function applyParsedBuffer(buffer, name, modelKey) {
  showStatus('Parsing STL…', 0);
  const parsed = await parseStlAsync(buffer, name);
  if (!parsed.ok) {
    showStatus(parsed.error || 'Failed to parse STL', 3500);
    return;
  }

  renderer.uploadMesh(parsed);
  // Release CPU-side vertex copy immediately after GPU VBO upload to minimize RAM
  parsed.vertices = null;
  currentMesh = parsed;
  state.activeModelKey = modelKey;
  state.measureHasA = false;
  state.measureHasB = false;

  cam.frameBounds(parsed.bboxMin, parsed.bboxMax, renderer.width, renderer.height, false);
  cam.setPreset(ViewPreset.Iso, false);

  transientStatus = '';
  updateModelPopover();
  updateToolbarUi();
  requestFrame(true);
}

function loadDemoModel(id) {
  const dm = DEMO_MODELS.find((m) => m.id === id) || DEMO_MODELS[0];
  const buf = generateDemoStl(dm.id);
  applyParsedBuffer(buf, dm.name, `demo:${dm.id}`);
}

async function loadLocalFileAt(idx) {
  const entry = state.loadedFiles[idx];
  if (!entry) return;
  state.loadedIndex = idx;
  const buf = await entry.file.arrayBuffer();
  await applyParsedBuffer(buf, entry.name, `file:${idx}`);
}

async function handleSelectedFiles(fileList) {
  const files = Array.from(fileList || []).filter((f) => /\.stl$/i.test(f.name) || fileList.length === 1);
  if (files.length === 0) {
    showStatus('Please select an .stl file', 2500);
    return;
  }
  state.loadedFiles = files.map((f) => ({ name: f.name, file: f }));
  await loadLocalFileAt(0);
}

async function loadFromUrl(url) {
  try {
    showStatus('Fetching STL…', 0);
    const resp = await fetch(url);
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const buf = await resp.arrayBuffer();
    const cleanName = url.split('/').pop()?.split('?')[0] || 'remote.stl';
    await applyParsedBuffer(buf, cleanName, `url:${url}`);
  } catch (err) {
    showStatus(`URL load failed: ${err.message}`, 3500);
    loadDemoModel('flange-housing');
  }
}

function resetView(animate = true) {
  if (!currentMesh || !currentMesh.ok) return;
  cam.frameBounds(currentMesh.bboxMin, currentMesh.bboxMax, renderer.width, renderer.height, animate);
  cam.setPreset(ViewPreset.Iso, animate);
  requestFrame(true);
}

function handleDoublePick(cssX, cssY) {
  if (!currentMesh || !currentMesh.ok) return;
  const pt = renderer.pickWorldPoint(cssX, cssY, cam, currentMesh, state);
  if (pt) {
    cam.setPivot(pt, true);
    showStatus('Pivot set', 1400);
    requestFrame(true);
  } else {
    resetView(true);
    showStatus('View reset', 1200);
  }
}

function handleMeasurePick(cssX, cssY) {
  if (!currentMesh || !currentMesh.ok) return;
  const pt = renderer.pickWorldPoint(cssX, cssY, cam, currentMesh, state);
  if (!pt) {
    showStatus('Click on model surface to measure', 1600);
    return;
  }
  if (!state.measureHasA || state.measureHasB) {
    state.measureA = pt;
    state.measureHasA = true;
    state.measureHasB = false;
  } else {
    state.measureB = pt;
    state.measureHasB = true;
  }
  updateMeasureUi();
  requestFrame(true);
}

function wireControls() {
  // Pointer & Mouse + Multi-touch gestures
  const pointers = new Map();
  let dragMode = null; // 'orbit' | 'pan' | 'pinch'
  let moved = false;
  let downX = 0, downY = 0;
  let lastX = 0, lastY = 0;
  let lastPinchDist = 0;
  let lastTapTime = 0, lastTapX = 0, lastTapY = 0;

  canvas.addEventListener('contextmenu', (e) => e.preventDefault());

  canvas.addEventListener('pointerdown', (e) => {
    closePopovers();
    canvas.setPointerCapture(e.pointerId);
    pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });

    if (pointers.size === 1) {
      moved = false;
      downX = lastX = e.clientX;
      downY = lastY = e.clientY;
      if (e.button === 1 || e.button === 2 || e.shiftKey) {
        dragMode = 'pan';
      } else {
        dragMode = 'orbit';
      }
    } else if (pointers.size === 2) {
      dragMode = 'pinch';
      moved = true;
      const pts = Array.from(pointers.values());
      lastPinchDist = Math.hypot(pts[1].x - pts[0].x, pts[1].y - pts[0].y);
      lastX = (pts[0].x + pts[1].x) * 0.5;
      lastY = (pts[0].y + pts[1].y) * 0.5;
    }
  });

  canvas.addEventListener('pointermove', (e) => {
    if (!pointers.has(e.pointerId)) return;
    pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });

    if (pointers.size === 2 && dragMode === 'pinch') {
      const pts = Array.from(pointers.values());
      const cx = (pts[0].x + pts[1].x) * 0.5;
      const cy = (pts[0].y + pts[1].y) * 0.5;
      const dist = Math.hypot(pts[1].x - pts[0].x, pts[1].y - pts[0].y);

      const dpr = getEffectiveDpr();
      cam.pan((cx - lastX) * dpr, (cy - lastY) * dpr, renderer.width, renderer.height);
      if (lastPinchDist > 1) {
        const steps = Math.log(dist / lastPinchDist) / Math.log(1 / 0.86);
        cam.zoom(steps, cx * dpr, cy * dpr, renderer.width, renderer.height);
      }
      lastX = cx;
      lastY = cy;
      lastPinchDist = dist;
      requestFrame(true);
      return;
    }

    if (pointers.size === 1 && dragMode) {
      const dx = e.clientX - lastX;
      const dy = e.clientY - lastY;
      if (Math.hypot(e.clientX - downX, e.clientY - downY) > 4) {
        moved = true;
      }
      lastX = e.clientX;
      lastY = e.clientY;
      const dpr = getEffectiveDpr();
      if (dragMode === 'pan') {
        cam.pan(dx * dpr, dy * dpr, renderer.width, renderer.height);
      } else {
        cam.orbit(dx * dpr, dy * dpr, renderer.width, renderer.height);
      }
      requestFrame(true);
    }
  });

  const endPointer = (e) => {
    const wasSingleTap = pointers.size === 1 && !moved && e.button === 0;
    pointers.delete(e.pointerId);
    if (pointers.size < 2) {
      dragMode = pointers.size === 1 ? 'orbit' : null;
      if (pointers.size === 1) {
        const rem = Array.from(pointers.values())[0];
        lastX = rem.x;
        lastY = rem.y;
      }
    }

    if (wasSingleTap) {
      if (state.measureMode) {
        handleMeasurePick(e.clientX, e.clientY);
        return;
      }
      if (e.pointerType === 'touch') {
        const now = performance.now();
        if (now - lastTapTime < 320 && Math.hypot(e.clientX - lastTapX, e.clientY - lastTapY) < 28) {
          lastTapTime = 0;
          handleDoublePick(e.clientX, e.clientY);
        } else {
          lastTapTime = now;
          lastTapX = e.clientX;
          lastTapY = e.clientY;
        }
      }
    }
  };

  canvas.addEventListener('pointerup', endPointer);
  canvas.addEventListener('pointercancel', endPointer);

  canvas.addEventListener('dblclick', (e) => {
    if (state.measureMode) return;
    handleDoublePick(e.clientX, e.clientY);
  });

  canvas.addEventListener(
    'wheel',
    (e) => {
      e.preventDefault();
      let delta = -e.deltaY;
      if (e.deltaMode === 1) delta *= 32;
      else if (e.deltaMode === 2) delta *= 240;
      const steps = Math.max(-3, Math.min(3, delta / 120));
      const dpr = getEffectiveDpr();
      cam.zoom(steps, e.clientX * dpr, e.clientY * dpr, renderer.width, renderer.height);
      requestFrame(true);
    },
    { passive: false }
  );

  // Toolbar & Popover bindings
  $('btn-model-menu').addEventListener('click', (e) => {
    e.stopPropagation();
    const pop = $('model-popover');
    const open = pop.classList.contains('hidden');
    closePopovers();
    if (open) {
      updateModelPopover();
      pop.classList.remove('hidden');
      $('btn-model-menu').setAttribute('aria-expanded', 'true');
    }
  });

  $('triad-btn').addEventListener('click', (e) => {
    e.stopPropagation();
    const pop = $('view-popover');
    const open = pop.classList.contains('hidden');
    closePopovers();
    if (open) {
      pop.classList.remove('hidden');
      $('triad-btn').setAttribute('aria-expanded', 'true');
    }
  });

  document.addEventListener('click', (e) => {
    if (!e.target.closest('#model-popover') && !e.target.closest('#btn-model-menu') &&
        !e.target.closest('#view-popover') && !e.target.closest('#triad-btn')) {
      closePopovers();
    }
  });

  $('btn-pop-open').addEventListener('click', () => { closePopovers(); $('file-input').click(); });
  $('btn-open').addEventListener('click', () => $('file-input').click());
  $('file-input').addEventListener('change', (e) => {
    if (e.target.files?.length) handleSelectedFiles(e.target.files);
    e.target.value = '';
  });

  $('btn-matcap').addEventListener('click', () => {
    state.matcap = (state.matcap + 1) % MatcapKind.Count;
    updateToolbarUi();
    requestFrame(true);
  });
  $('btn-wire').addEventListener('click', () => {
    state.wireframe = !state.wireframe;
    updateToolbarUi();
    requestFrame(true);
  });
  $('btn-slice').addEventListener('click', () => {
    state.sliceEnabled = !state.sliceEnabled;
    updateToolbarUi();
    requestFrame(true);
  });
  $('btn-measure').addEventListener('click', () => {
    state.measureMode = !state.measureMode;
    if (!state.measureMode) {
      state.measureHasA = false;
      state.measureHasB = false;
    }
    updateToolbarUi();
    requestFrame(true);
  });
  $('btn-measure-clear').addEventListener('click', () => {
    state.measureHasA = false;
    state.measureHasB = false;
    updateMeasureUi();
    requestFrame(true);
  });

  [$('btn-slice-x'), $('btn-slice-y'), $('btn-slice-z')].forEach((btn) => {
    btn.addEventListener('click', () => {
      state.sliceAxis = Number(btn.dataset.axis);
      updateSliceUi();
      requestFrame(true);
    });
  });
  $('btn-slice-flip').addEventListener('click', () => {
    state.sliceFlip = !state.sliceFlip;
    updateSliceUi();
    requestFrame(true);
  });
  $('slice-slider').addEventListener('input', (e) => {
    state.sliceT = parseFloat(e.target.value);
    updateSliceUi();
    requestFrame(true);
  });

  $('btn-xray').addEventListener('click', () => {
    state.xray = !state.xray;
    updateToolbarUi();
    requestFrame(true);
  });
  $('btn-twosided').addEventListener('click', () => {
    state.twoSided = !state.twoSided;
    updateToolbarUi();
    requestFrame(true);
  });
  $('btn-theme').addEventListener('click', () => {
    state.darkTheme = !state.darkTheme;
    updateToolbarUi();
    requestFrame(true);
  });
  $('btn-eco').addEventListener('click', () => {
    state.ecoMode = !state.ecoMode;
    renderer.setTargetSamples(getTargetSamples());
    resizeCanvas();
    updateToolbarUi();
    showStatus(state.ecoMode ? 'Eco mode (low power)' : 'Crisp 16× SSAA', 1600);
    requestFrame(true);
  });
  $('btn-reset').addEventListener('click', () => resetView(true));
  $('btn-help').addEventListener('click', () => {
    state.showHelp = !state.showHelp;
    updateToolbarUi();
  });
  $('btn-help-close').addEventListener('click', () => {
    state.showHelp = false;
    updateToolbarUi();
  });
  $('help-modal').addEventListener('click', (e) => {
    if (e.target === $('help-modal')) {
      state.showHelp = false;
      updateToolbarUi();
    }
  });

  document.querySelectorAll('#view-popover [data-view]').forEach((btn) => {
    btn.addEventListener('click', () => {
      cam.setPreset(btn.dataset.view, true);
      closePopovers();
      requestFrame(true);
    });
  });
  $('btn-ortho').addEventListener('click', () => {
    cam.toggleOrtho();
    updateToolbarUi();
    requestFrame(true);
  });
  $('btn-yup').addEventListener('click', () => {
    cam.toggleUpAxis();
    updateToolbarUi();
    requestFrame(true);
  });
  $('btn-grid').addEventListener('click', () => {
    state.showGrid = !state.showGrid;
    updateToolbarUi();
    requestFrame(true);
  });
  $('btn-hide-hud').addEventListener('click', () => {
    closePopovers();
    state.showHud = false;
    updateToolbarUi();
    requestFrame(false);
  });
  $('btn-restore-hud').addEventListener('click', () => {
    state.showHud = true;
    updateToolbarUi();
    requestFrame(false);
  });

  // Drag and drop STL files
  let dragDepth = 0;
  window.addEventListener('dragenter', (e) => {
    if (e.dataTransfer?.types?.includes('Files')) {
      dragDepth++;
      $('drop-overlay').classList.remove('hidden');
    }
  });
  window.addEventListener('dragleave', () => {
    dragDepth = Math.max(0, dragDepth - 1);
    if (dragDepth === 0) $('drop-overlay').classList.add('hidden');
  });
  window.addEventListener('dragover', (e) => {
    e.preventDefault();
  });
  window.addEventListener('drop', (e) => {
    e.preventDefault();
    dragDepth = 0;
    $('drop-overlay').classList.add('hidden');
    if (e.dataTransfer?.files?.length) {
      handleSelectedFiles(e.dataTransfer.files);
    }
  });

  // Keyboard shortcuts matching native faset
  window.addEventListener('keydown', (e) => {
    if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA')) {
      if (e.key === 'Tab' && state.sliceEnabled) {
        e.preventDefault();
        state.sliceAxis = (state.sliceAxis + 1) % 3;
        updateSliceUi();
        requestFrame(true);
      }
      return;
    }

    const k = e.key;
    if (k === 'F1' || k === '?') {
      e.preventDefault();
      state.showHelp = !state.showHelp;
      updateToolbarUi();
      return;
    }
    if (k === 'Escape') {
      closePopovers();
      if (state.showHelp) { state.showHelp = false; updateToolbarUi(); return; }
      if (state.measureMode) {
        if (state.measureHasA || state.measureHasB) {
          state.measureHasA = false;
          state.measureHasB = false;
        } else {
          state.measureMode = false;
        }
        updateToolbarUi();
        requestFrame(true);
        return;
      }
      if (state.sliceEnabled) {
        state.sliceEnabled = false;
        updateToolbarUi();
        requestFrame(true);
      }
      return;
    }
    if (k === 'Tab' && state.sliceEnabled) {
      e.preventDefault();
      state.sliceAxis = (state.sliceAxis + 1) % 3;
      updateSliceUi();
      requestFrame(true);
      return;
    }

    switch (k.toLowerCase()) {
      case '1': cam.setPreset(ViewPreset.Front, true); requestFrame(true); break;
      case '2': cam.setPreset(ViewPreset.Back, true); requestFrame(true); break;
      case '3': cam.setPreset(ViewPreset.Right, true); requestFrame(true); break;
      case '4': cam.setPreset(ViewPreset.Left, true); requestFrame(true); break;
      case '5': cam.setPreset(ViewPreset.Top, true); requestFrame(true); break;
      case '6': cam.setPreset(ViewPreset.Bottom, true); requestFrame(true); break;
      case '0': cam.setPreset(ViewPreset.Iso, true); requestFrame(true); break;
      case 'p': cam.toggleOrtho(); updateToolbarUi(); requestFrame(true); break;
      case 'u': cam.toggleUpAxis(); updateToolbarUi(); requestFrame(true); break;
      case 'g': state.showGrid = !state.showGrid; updateToolbarUi(); requestFrame(true); break;
      case 'm': state.matcap = (state.matcap + 1) % MatcapKind.Count; updateToolbarUi(); requestFrame(true); break;
      case 'w': state.wireframe = !state.wireframe; updateToolbarUi(); requestFrame(true); break;
      case 's': state.sliceEnabled = !state.sliceEnabled; updateToolbarUi(); requestFrame(true); break;
      case 'i': if (state.sliceEnabled) { state.sliceFlip = !state.sliceFlip; updateSliceUi(); requestFrame(true); } break;
      case 'd':
        state.measureMode = !state.measureMode;
        if (!state.measureMode) { state.measureHasA = false; state.measureHasB = false; }
        updateToolbarUi();
        requestFrame(true);
        break;
      case 'x': state.xray = !state.xray; updateToolbarUi(); requestFrame(true); break;
      case 'b': state.twoSided = !state.twoSided; updateToolbarUi(); requestFrame(true); break;
      case 'e': $('btn-eco').click(); break;
      case 't': state.darkTheme = !state.darkTheme; updateToolbarUi(); requestFrame(true); break;
      case 'o': $('file-input').click(); break;
      case 'r':
      case 'f': resetView(true); break;
      case 'h':
        state.showHud = !state.showHud;
        updateToolbarUi();
        requestFrame(false);
        break;
      case '[':
        if (state.loadedFiles.length > 1) {
          const prev = (state.loadedIndex - 1 + state.loadedFiles.length) % state.loadedFiles.length;
          loadLocalFileAt(prev);
        }
        break;
      case ']':
        if (state.loadedFiles.length > 1) {
          const next = (state.loadedIndex + 1) % state.loadedFiles.length;
          loadLocalFileAt(next);
        }
        break;
    }
  });

  window.addEventListener('resize', () => {
    resizeCanvas();
    requestFrame(true);
  });

  document.addEventListener('visibilitychange', () => {
    if (document.hidden && rafId) {
      cancelAnimationFrame(rafId);
      rafId = 0;
    } else if (!document.hidden && renderer && (cam.isAnimating() || renderer.needsMoreSamples())) {
      requestFrame(false);
    }
  });
}

function boot() {
  initWorker();
  try {
    renderer = new Renderer(canvas);
    const dpr = getEffectiveDpr();
    const w = Math.max(1, Math.round(window.innerWidth * dpr));
    const h = Math.max(1, Math.round(window.innerHeight * dpr));
    canvas.width = w;
    canvas.height = h;
    renderer.init(w, h, dpr);
    renderer.setTargetSamples(getTargetSamples());
  } catch (err) {
    $('error-msg').textContent = err?.message || 'WebGL2 initialization failed.';
    $('error-banner').classList.remove('hidden');
    return;
  }

  canvas.addEventListener('webglcontextlost', (e) => {
    e.preventDefault();
    if (rafId) {
      cancelAnimationFrame(rafId);
      rafId = 0;
    }
    showStatus('WebGL context paused', 0);
  });

  wireControls();
  updateToolbarUi();

  const params = new URLSearchParams(window.location.search);
  const urlParam = params.get('url');
  if (urlParam) {
    loadFromUrl(urlParam);
  } else {
    loadDemoModel('flange-housing');
  }
}

boot();

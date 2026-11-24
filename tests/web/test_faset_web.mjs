import test from 'node:test';
import assert from 'node:assert/strict';

import { Mat4, Frustum, halton, subpixelJitterNdc, vLen, vSub } from '../../docs/js/math3d.js';
import { Camera, ViewPreset, UpAxis } from '../../docs/js/camera.js';
import { CHUNK_TRIS, parseStlBuffer } from '../../docs/js/stl_parser.js';
import { cullChunks } from '../../docs/js/culling.js';
import { MATCAP_SIZE, MatcapKind, generateMatcapRgba8 } from '../../docs/js/matcap.js';
import { DEMO_MODELS, buildBinaryStl, generateDemoStl } from '../../docs/js/demo_models.js';
import { pickMeshCpu } from '../../docs/js/picking.js';
import { RendererCanvas2D } from '../../docs/js/renderer_canvas2d.js';

function approx(a, b, eps = 1e-4) {
  assert.ok(Math.abs(a - b) <= eps, `Expected ${a} ≈ ${b} (eps=${eps})`);
}

function makeCubeTriangles(x0, y0, z0, x1, y1, z1) {
  const tris = [];
  const q = (ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz) => {
    tris.push(ax, ay, az, bx, by, bz, cx, cy, cz);
    tris.push(ax, ay, az, cx, cy, cz, dx, dy, dz);
  };
  q(x0, y0, z0, x0, y1, z0, x1, y1, z0, x1, y0, z0); // -Z
  q(x0, y0, z1, x1, y0, z1, x1, y1, z1, x0, y1, z1); // +Z
  q(x0, y0, z0, x1, y0, z0, x1, y0, z1, x0, y0, z1); // -Y
  q(x1, y1, z0, x0, y1, z0, x0, y1, z1, x1, y1, z1); // +Y
  q(x0, y1, z0, x0, y0, z0, x0, y0, z1, x0, y1, z1); // -X
  q(x1, y0, z0, x1, y1, z0, x1, y1, z1, x1, y0, z1); // +X
  return tris;
}

test('math3d: Mat4 inverse, project/unproject, frustum and halton', () => {
  const proj = Mat4.perspective(Math.PI / 4, 16 / 9, 0.5, 500);
  const view = Mat4.lookAt({ x: 30, y: -40, z: 25 }, { x: 0, y: 0, z: 0 }, { x: 0, y: 0, z: 1 });
  const vp = Mat4.multiply(proj, view);
  const invVp = Mat4.inverse(vp);
  assert.ok(invVp);

  const world = { x: 4.25, y: -3.5, z: 7.75 };
  const win = Mat4.project(world, vp, 1280, 720);
  assert.ok(win && win.visible);
  const back = Mat4.unproject(win, invVp, 1280, 720);
  assert.ok(back);
  approx(back.x, world.x, 1e-3);
  approx(back.y, world.y, 1e-3);
  approx(back.z, world.z, 1e-3);

  const fr = Frustum.fromViewProj(vp);
  assert.equal(Frustum.intersectsSphere(fr, 0, 0, 0, 1.0), true);
  assert.equal(Frustum.intersectsSphere(fr, 0, 0, 5000, 1.0), false);

  assert.equal(halton(1, 2), 0.5);
  assert.equal(halton(2, 2), 0.25);
  const j0 = subpixelJitterNdc(0, 1280, 720);
  assert.equal(j0.jx, 0);
  assert.equal(j0.jy, 0);
  const j1 = subpixelJitterNdc(1, 1280, 720);
  assert.ok(Math.abs(j1.jx) <= 1.0 / 1280 + 1e-9);
});

test('camera: presets, orbit, pan, zoom and step convergence', () => {
  const cam = new Camera();
  cam.frameBounds({ x: -10, y: -10, z: -10 }, { x: 10, y: 10, z: 10 }, 1280, 720, false);
  const d0 = cam.distance();
  assert.ok(d0 > 20 && d0 < 120);

  cam.orbit(50, -25, 1280, 720);
  assert.equal(cam.isAnimating(), false);

  const beforePan = { ...cam.target() };
  cam.pan(40, 20, 1280, 720);
  assert.ok(vLen(vSub(cam.target(), beforePan)) > 0.1);

  cam.zoom(2, 640, 360, 1280, 720);
  assert.ok(cam.distance() < d0);

  for (const axis of [UpAxis.ZUp, UpAxis.YUp]) {
    cam.setUpAxis(axis);
    for (const preset of Object.values(ViewPreset)) {
      cam.setPreset(preset, true);
      assert.equal(cam.isAnimating(), true);
      let steps = 0;
      while (cam.step(0.016) && steps < 300) steps++;
      assert.equal(cam.isAnimating(), false);
      const vp = cam.viewProj(1280, 720);
      assert.ok(Number.isFinite(vp[0]) && Number.isFinite(vp[15]));
    }
  }
});

test('stl_parser: binary cube, solid-header trap, ASCII, multi-chunk & edge cases', () => {
  // 1. Binary cube 10x20x30 mm
  const cube = makeCubeTriangles(0, 0, 0, 10, 20, 30);
  const binBuf = buildBinaryStl(cube, 'solid binary_trap_header');
  const m = parseStlBuffer(binBuf, 'cube.stl');
  assert.equal(m.ok, true);
  assert.equal(m.isBinary, true);
  assert.equal(m.triCount, 12);
  assert.equal(m.vramBytes, 12 * 3 * 8);
  assert.equal(m.vertices.length, 12 * 3 * 4);
  approx(m.bboxExtent.x, 10, 1e-4);
  approx(m.bboxExtent.y, 20, 1e-4);
  approx(m.bboxExtent.z, 30, 1e-4);
  approx(m.volumeMm3, 6000, 1e-1);
  approx(m.areaMm2, 2200, 1e-1);

  // 2. ASCII STL with messy whitespace + scientific notation
  const ascii = `solid   test_part
  facet normal 0 0 1
    outer loop
      vertex   0.0e0   0.0   0.0
      vertex   1.0e1   0     0
      vertex   0       2e1   0.0
    endloop
  endfacet
endsolid test_part
`;
  const asciiBuf = new TextEncoder().encode(ascii).buffer;
  const ma = parseStlBuffer(asciiBuf, 'tri.stl');
  assert.equal(ma.ok, true);
  assert.equal(ma.isBinary, false);
  assert.equal(ma.triCount, 1);
  assert.equal(ma.quantScale.z, 0);
  approx(ma.areaMm2, 100, 1e-3);

  // 3. Multi-chunk (> 4096 tris)
  const manyTris = [];
  for (let i = 0; i < CHUNK_TRIS + 50; i++) {
    const x = i * 0.1;
    manyTris.push(x, 0, 0, x + 0.05, 0, 0, x, 1, 0);
  }
  const mm = parseStlBuffer(buildBinaryStl(manyTris), 'many.stl');
  assert.equal(mm.ok, true);
  assert.equal(mm.chunks.length, 2);
  assert.equal(mm.chunks[0].triCount, CHUNK_TRIS);
  assert.equal(mm.chunks[1].triCount, 50);

  // 4. Error cases
  assert.equal(parseStlBuffer(new ArrayBuffer(0), 'empty.stl').ok, false);
  assert.equal(parseStlBuffer(new ArrayBuffer(42), 'bad.stl').ok, false);
});

test('culling: frustum cull, normal-cone cull & adjacent span merge', () => {
  const cam = new Camera();
  cam.frameBounds({ x: 0, y: 0, z: 0 }, { x: 10, y: 10, z: 10 }, 1280, 720, false);
  const fr = Frustum.fromViewProj(cam.viewProj(1280, 720));

  const fwd = cam.forward();
  const chunks = [
    { firstVert: 0, vertCount: 30, triCount: 10, cx: 5, cy: 5, cz: 5, radius: 1, ax: -fwd.x, ay: -fwd.y, az: -fwd.z, cutoff: 0.2, cullable: true },
    { firstVert: 30, vertCount: 30, triCount: 10, cx: 5, cy: 5, cz: 5, radius: 1, ax: -fwd.x, ay: -fwd.y, az: -fwd.z, cutoff: 0.2, cullable: true },
    { firstVert: 60, vertCount: 30, triCount: 10, cx: 90000, cy: 90000, cz: 90000, radius: 1, ax: 0, ay: 0, az: 1, cutoff: 0.5, cullable: true },
    { firstVert: 90, vertCount: 30, triCount: 10, cx: 5, cy: 5, cz: 5, radius: 1, ax: fwd.x, ay: fwd.y, az: fwd.z, cutoff: 0.1, cullable: true },
  ];

  const { spans, stats } = cullChunks(chunks, fr, {
    backfaceConeCull: true,
    isOrtho: false,
    eye: cam.eye(),
    camForward: fwd,
  });
  assert.equal(stats.frustumCulled, 1);
  assert.equal(stats.coneCulled, 1);
  assert.equal(stats.visibleChunks, 2);
  assert.equal(spans.length, 1);
  assert.equal(spans[0].firstVert, 0);
  assert.equal(spans[0].vertCount, 60);

  const orthoRes = cullChunks(chunks, fr, {
    backfaceConeCull: true,
    isOrtho: true,
    eye: cam.eye(),
    camForward: fwd,
  });
  assert.equal(orthoRes.stats.coneCulled, 1);
});

test('matcap: generates 4 distinct 256x256 RGBA8 textures', () => {
  const sums = [];
  for (let k = 0; k < MatcapKind.Count; k++) {
    const tex = generateMatcapRgba8(k);
    assert.equal(tex.length, MATCAP_SIZE * MATCAP_SIZE * 4);
    assert.equal(tex[3], 255);
    const mid = ((MATCAP_SIZE >> 1) * MATCAP_SIZE + (MATCAP_SIZE >> 1)) * 4;
    assert.equal(tex[mid + 3], 255);
    sums.push(tex[mid] + tex[mid + 1] * 256 + tex[mid + 2] * 65536);
  }
  assert.equal(new Set(sums).size, MatcapKind.Count);
});

test('demo_models: all 3 procedural CAD models produce valid watertight-sized STLs', () => {
  assert.equal(DEMO_MODELS.length, 3);
  for (const dm of DEMO_MODELS) {
    const buf = generateDemoStl(dm.id);
    const parsed = parseStlBuffer(buf, dm.name);
    assert.equal(parsed.ok, true, `${dm.name} failed: ${parsed.error}`);
    assert.equal(parsed.isBinary, true);
    assert.ok(parsed.triCount > 100);
    assert.ok(parsed.volumeMm3 > 100);
    assert.ok(Number.isFinite(parsed.bboxExtent.x) && parsed.bboxExtent.x > 0);
  }
});

test('picking: CPU ray-triangle picker handles persp, ortho, miss & slice clipping', () => {
  const cube = makeCubeTriangles(-10, -10, -10, 10, 10, 10);
  const mesh = parseStlBuffer(buildBinaryStl(cube), 'cube.stl');
  const cam = new Camera();
  cam.frameBounds(mesh.bboxMin, mesh.bboxMax, 800, 600, false);
  cam.setPreset(ViewPreset.Front, false);

  const baseState = {
    twoSided: false,
    sliceEnabled: false,
    sliceAxis: 1,
    sliceFlip: false,
    sliceT: 0.5,
    xray: false,
  };

  // Center pixel hits front face y = -10
  const hitPersp = pickMeshCpu(400, 300, 800, 600, 1, cam, mesh, baseState);
  assert.ok(hitPersp);
  approx(hitPersp.y, -10, 1e-2);

  // Corner pixel misses cube
  const miss = pickMeshCpu(5, 5, 800, 600, 1, cam, mesh, baseState);
  assert.equal(miss, null);

  // Ortho pick hits front face y = -10
  cam.setOrtho(true);
  const hitOrtho = pickMeshCpu(400, 300, 800, 600, 1, cam, mesh, baseState);
  assert.ok(hitOrtho);
  approx(hitOrtho.y, -10, 1e-2);

  // Slice along Y at 0.5 (y <= 0 kept, y > 0 clipped OR y >= 0 kept):
  // sliceFlip=false -> sign=-1, keeps y <= 0, so front face y=-10 stays visible;
  // sliceFlip=true -> sign=+1, keeps y >= 0, so front face y=-10 is clipped and back interior y=+10 is hit!
  const slicedState = { ...baseState, sliceEnabled: true, sliceFlip: true, sliceT: 0.5 };
  const hitSliced = pickMeshCpu(400, 300, 800, 600, 1, cam, mesh, slicedState);
  assert.ok(hitSliced);
  approx(hitSliced.y, 10, 1e-2);
});

test('renderer_canvas2d: renders flange-housing across modes and returns axisTipsCss + pick', () => {
  let fillCount = 0;
  let strokeCount = 0;
  const mockCtx = {
    fillStyle: '',
    strokeStyle: '',
    lineWidth: 1,
    lineJoin: 'round',
    createLinearGradient: () => ({ addColorStop() {} }),
    fillRect() {},
    beginPath() {},
    moveTo() {},
    lineTo() {},
    closePath() {},
    fill() { fillCount++; },
    stroke() { strokeCount++; },
  };
  const mockCanvas = { getContext: (kind) => (kind === '2d' ? mockCtx : null) };

  const r = new RendererCanvas2D(mockCanvas);
  assert.equal(r.backend, 'Canvas 2D');
  assert.equal(r.needsCpuVertices, true);
  r.init(800, 600, 1);

  const mesh = parseStlBuffer(generateDemoStl('flange-housing'), 'flange-housing.stl');
  r.uploadMesh(mesh);

  const cam = new Camera();
  cam.frameBounds(mesh.bboxMin, mesh.bboxMax, 800, 600, false);

  for (let mc = 0; mc < MatcapKind.Count; mc++) {
    r.resetAccumulation();
    const state = {
      darkTheme: mc % 2 === 0,
      matcap: mc,
      wireframe: mc === 1,
      xray: mc === 2,
      twoSided: mc === 3,
      showGrid: true,
      showHud: true,
      sliceEnabled: mc === 3,
      sliceAxis: 2,
      sliceFlip: false,
      sliceT: 0.5,
      measureHasA: true,
      measureHasB: true,
      measureA: { x: 0, y: 0, z: 0 },
      measureB: { x: 10, y: 0, z: 0 },
    };
    const res = r.renderFrame(mesh, cam, state);
    assert.ok(res.axisTipsCss && res.axisTipsCss.length === 3);
    assert.ok(Number.isFinite(res.axisTipsCss[0].x));
  }
  assert.ok(fillCount > 100);
  assert.ok(strokeCount > 100);

  const hit = r.pickWorldPoint(400, 300, cam, mesh, {
    twoSided: false, sliceEnabled: false, sliceAxis: 2, sliceFlip: false, sliceT: 0.5, xray: false,
  });
  assert.ok(hit && Number.isFinite(hit.x));
});

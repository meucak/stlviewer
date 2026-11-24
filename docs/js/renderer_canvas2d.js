import { Mat4, Frustum } from './math3d.js';
import { UpAxis } from './camera.js';
import { MATCAP_SIZE, MatcapKind, generateMatcapRgba8 } from './matcap.js';
import { cullChunks } from './culling.js';
import { pickMeshCpu } from './picking.js';

const INV_65535 = 1.0 / 65535.0;
const FAST_TRI_LIMIT = 12000;

function clipPolygonPlane(pts, plane) {
  const out = [];
  const n = pts.length;
  for (let i = 0; i < n; i++) {
    const a = pts[i];
    const b = pts[(i + 1) % n];
    const da = a.x * plane[0] + a.y * plane[1] + a.z * plane[2] + plane[3];
    const db = b.x * plane[0] + b.y * plane[1] + b.z * plane[2] + plane[3];
    if (da >= 0) {
      out.push(a);
      if (db < 0) {
        const t = da / (da - db);
        out.push({ x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, z: a.z + (b.z - a.z) * t });
      }
    } else if (db >= 0) {
      const t = da / (da - db);
      out.push({ x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, z: a.z + (b.z - a.z) * t });
    }
  }
  return out;
}

export class RendererCanvas2D {
  constructor(canvas) {
    this.canvas = canvas;
    this.backend = 'Canvas 2D';
    this.needsCpuVertices = true;
    this.ctx = null;
    this.width = 1;
    this.height = 1;
    this.dpr = 1;
    this.fp16Fbo = false;
    this.accumCount = 0;
    this.targetSamples = 1;
    this.hasMesh = false;
    this.glInfo = 'Software 2D Rasterizer';
    this.matcapTables = [];
    this.triList = [];
  }

  init(width, height, dpr) {
    const ctx = this.canvas.getContext('2d', { alpha: false }) || this.canvas.getContext('2d');
    if (!ctx) {
      throw new Error('Canvas 2D context is unavailable.');
    }
    this.ctx = ctx;
    this.width = Math.max(1, width | 0);
    this.height = Math.max(1, height | 0);
    this.dpr = dpr || 1;
    for (let i = 0; i < MatcapKind.Count; i++) {
      this.matcapTables.push(generateMatcapRgba8(i));
    }
    this.accumCount = 0;
  }

  resize(width, height, dpr) {
    const w = Math.max(1, width | 0);
    const h = Math.max(1, height | 0);
    this.dpr = dpr || 1;
    if (w === this.width && h === this.height) return;
    this.width = w;
    this.height = h;
    this.accumCount = 0;
  }

  setTargetSamples() {
    this.targetSamples = 1;
  }

  resetAccumulation() {
    this.accumCount = 0;
  }

  needsMoreSamples() {
    return this.accumCount < this.targetSamples;
  }

  uploadMesh(mesh) {
    this.hasMesh = !!(mesh && mesh.ok && mesh.vertices && mesh.vertices.length > 0);
    this.targetSamples = this.hasMesh && mesh.triCount > FAST_TRI_LIMIT ? 2 : 1;
    this.accumCount = 0;
  }

  _computeClipPlane(mesh, state) {
    const plane = [0, 0, 0, 1];
    if (state.sliceEnabled && mesh && mesh.ok) {
      const axis = state.sliceAxis;
      const bmin = axis === 0 ? mesh.bboxMin.x : axis === 1 ? mesh.bboxMin.y : mesh.bboxMin.z;
      const bext = axis === 0 ? mesh.bboxExtent.x : axis === 1 ? mesh.bboxExtent.y : mesh.bboxExtent.z;
      const pos = bmin + state.sliceT * bext;
      const sign = state.sliceFlip ? 1.0 : -1.0;
      plane[axis] = sign;
      plane[3] = -sign * pos;
    }
    return plane;
  }

  _projectPoint(vp, x, y, z) {
    const cx = vp[0] * x + vp[4] * y + vp[8] * z + vp[12];
    const cy = vp[1] * x + vp[5] * y + vp[9] * z + vp[13];
    const cz = vp[2] * x + vp[6] * y + vp[10] * z + vp[14];
    const cw = vp[3] * x + vp[7] * y + vp[11] * z + vp[15];
    if (cw <= 1e-5) return null;
    const invW = 1.0 / cw;
    const ndcZ = cz * invW;
    return {
      sx: (cx * invW * 0.5 + 0.5) * this.width,
      sy: (1.0 - (cy * invW * 0.5 + 0.5)) * this.height,
      z: ndcZ,
    };
  }

  _drawLine3D(ctx, vp, ax, ay, az, bx, by, bz, color, widthPx = 1) {
    const pa = this._projectPoint(vp, ax, ay, az);
    const pb = this._projectPoint(vp, bx, by, bz);
    if (!pa || !pb) return;
    if (pa.z > 1.05 && pb.z > 1.05) return;
    ctx.strokeStyle = color;
    ctx.lineWidth = widthPx;
    ctx.beginPath();
    ctx.moveTo(pa.sx, pa.sy);
    ctx.lineTo(pb.sx, pb.sy);
    ctx.stroke();
  }

  renderFrame(mesh, cam, state) {
    const ctx = this.ctx;
    const w = this.width;
    const h = this.height;
    let cstats = {
      totalChunks: 0, frustumCulled: 0, coneCulled: 0,
      visibleChunks: 0, visibleTris: 0, drawCalls: 1,
    };

    // 1. Background vertical studio gradient
    const grad = ctx.createLinearGradient(0, 0, 0, h);
    if (state.darkTheme) {
      grad.addColorStop(0, '#07090c');
      grad.addColorStop(1, '#030406');
    } else {
      grad.addColorStop(0, '#d6dde8');
      grad.addColorStop(1, '#949eae');
    }
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, w, h);

    const view = cam.viewMatrix();
    const proj = cam.projMatrix(w, h);
    const vp = Mat4.multiply(proj, view);
    const modelValid = this.hasMesh && mesh && mesh.ok && mesh.vertices;

    // 2. Ground Grid
    if (state.showGrid) {
      const gc = modelValid
        ? {
            x: (mesh.bboxMin.x + mesh.bboxMax.x) * 0.5,
            y: (mesh.bboxMin.y + mesh.bboxMax.y) * 0.5,
            z: (mesh.bboxMin.z + mesh.bboxMax.z) * 0.5,
          }
        : { x: 0, y: 0, z: 0 };
      const mr = modelValid
        ? Math.max(Math.hypot(mesh.bboxExtent.x, mesh.bboxExtent.y, mesh.bboxExtent.z) * 0.5, 1.0)
        : 50.0;
      const logS = Math.floor(Math.log10(Math.max(mr * 0.45, 0.05)));
      const step = Math.pow(10.0, logS);
      const count = 10;
      const halfSpan = count * step;

      const minorCss = state.darkTheme ? 'rgba(62,72,92,0.34)' : 'rgba(65,75,92,0.30)';
      const majorCss = state.darkTheme ? 'rgba(92,106,132,0.52)' : 'rgba(50,60,76,0.46)';
      const xCss = state.darkTheme ? 'rgba(200,72,72,0.72)' : 'rgba(184,46,46,0.72)';
      const yCss = cam.getUpAxis() === UpAxis.YUp
        ? (state.darkTheme ? 'rgba(72,134,218,0.72)' : 'rgba(42,92,184,0.72)')
        : (state.darkTheme ? 'rgba(68,178,92,0.72)' : 'rgba(40,138,56,0.72)');

      if (cam.getUpAxis() === UpAxis.ZUp) {
        const gz = modelValid ? mesh.bboxMin.z - mr * 0.001 : 0.0;
        const cx = Math.round(gc.x / step) * step;
        const cy = Math.round(gc.y / step) * step;
        for (let i = -count; i <= count; i++) {
          const x = cx + i * step;
          const y = cy + i * step;
          const colX = Math.abs(y) < step * 0.01 ? xCss : (Math.round(y / step) % 5 === 0 ? majorCss : minorCss);
          const colY = Math.abs(x) < step * 0.01 ? yCss : (Math.round(x / step) % 5 === 0 ? majorCss : minorCss);
          this._drawLine3D(ctx, vp, cx - halfSpan, y, gz, cx + halfSpan, y, gz, colX, this.dpr);
          this._drawLine3D(ctx, vp, x, cy - halfSpan, gz, x, cy + halfSpan, gz, colY, this.dpr);
        }
      } else {
        const gy = modelValid ? mesh.bboxMin.y - mr * 0.001 : 0.0;
        const cx = Math.round(gc.x / step) * step;
        const cz = Math.round(gc.z / step) * step;
        for (let i = -count; i <= count; i++) {
          const x = cx + i * step;
          const z = cz + i * step;
          const colX = Math.abs(z) < step * 0.01 ? xCss : (Math.round(z / step) % 5 === 0 ? majorCss : minorCss);
          const colZ = Math.abs(x) < step * 0.01 ? yCss : (Math.round(x / step) % 5 === 0 ? majorCss : minorCss);
          this._drawLine3D(ctx, vp, cx - halfSpan, gy, z, cx + halfSpan, gy, z, colX, this.dpr);
          this._drawLine3D(ctx, vp, x, gy, cz - halfSpan, x, gy, cz + halfSpan, colZ, this.dpr);
        }
      }
    }

    // 3. Mesh rasterization (Painter's algorithm + MatCap LUT)
    if (modelValid) {
      const useCull = !state.twoSided && !state.sliceEnabled && !state.xray;
      const fr = Frustum.fromViewProj(vp);
      const { spans, stats } = cullChunks(mesh.chunks, fr, {
        backfaceConeCull: useCull,
        isOrtho: cam.isOrtho(),
        eye: cam.eye(),
        camForward: cam.forward(),
      });
      cstats = stats;

      const stride = this.targetSamples > 1 && this.accumCount === 0
        ? Math.max(2, Math.ceil(mesh.triCount / FAST_TRI_LIMIT))
        : 1;

      const verts = mesh.vertices;
      const bminX = mesh.bboxMin.x, bminY = mesh.bboxMin.y, bminZ = mesh.bboxMin.z;
      const sx = mesh.quantScale.x * INV_65535;
      const sy = mesh.quantScale.y * INV_65535;
      const sz = mesh.quantScale.z * INV_65535;
      const clipPlane = this._computeClipPlane(mesh, state);

      const mc = Math.max(0, Math.min(MatcapKind.Count - 1, state.matcap | 0));
      const lut = this.matcapTables[mc];

      const triList = this.triList;
      triList.length = 0;

      const emitTri = (x0, y0, z0, x1, y1, z1, x2, y2, z2) => {
        const e1x = x1 - x0, e1y = y1 - y0, e1z = z1 - z0;
        const e2x = x2 - x0, e2y = y2 - y0, e2z = z2 - z0;
        const wnx = e1y * e2z - e1z * e2y;
        const wny = e1z * e2x - e1x * e2z;
        const wnz = e1x * e2y - e1y * e2x;

        let vnx = view[0] * wnx + view[4] * wny + view[8] * wnz;
        let vny = view[1] * wnx + view[5] * wny + view[9] * wnz;
        let vnz = view[2] * wnx + view[6] * wny + view[10] * wnz;
        const nLen = Math.hypot(vnx, vny, vnz);
        if (nLen > 1e-12) {
          const inv = 1.0 / nLen;
          vnx *= inv; vny *= inv; vnz *= inv;
        } else {
          vnx = 0; vny = 0; vnz = 1;
        }

        const backFace = vnz < 0;
        if (useCull && backFace) return;

        const p0 = this._projectPoint(vp, x0, y0, z0);
        const p1 = this._projectPoint(vp, x1, y1, z1);
        const p2 = this._projectPoint(vp, x2, y2, z2);
        if (!p0 || !p1 || !p2) return;

        if (Math.max(p0.sx, p1.sx, p2.sx) < 0 || Math.min(p0.sx, p1.sx, p2.sx) > w) return;
        if (Math.max(p0.sy, p1.sy, p2.sy) < 0 || Math.min(p0.sy, p1.sy, p2.sy) > h) return;
        if (Math.min(p0.z, p1.z, p2.z) > 1.0 || Math.max(p0.z, p1.z, p2.z) < -1.0) return;

        if (backFace) {
          vnx = -vnx; vny = -vny; vnz = -vnz;
        }

        let r, g, b, a = 1.0;
        if (state.xray) {
          const rim = Math.pow(Math.max(0, Math.min(1, 1.0 - Math.abs(vnz))), 1.85);
          const br = state.darkTheme ? 41 : 10;
          const bg = state.darkTheme ? 143 : 61;
          const bb = state.darkTheme ? 245 : 138;
          const mult = 0.28 + 1.35 * rim;
          r = Math.min(255, Math.round(br * mult));
          g = Math.min(255, Math.round(bg * mult));
          b = Math.min(255, Math.round(bb * mult));
          a = Math.max(0.14, Math.min(0.88, 0.14 + 0.72 * rim));
        } else {
          const tx = Math.max(0, Math.min(MATCAP_SIZE - 1, Math.round((vnx * 0.492 + 0.5) * (MATCAP_SIZE - 1))));
          const ty = Math.max(0, Math.min(MATCAP_SIZE - 1, Math.round((vny * 0.492 + 0.5) * (MATCAP_SIZE - 1))));
          const idx = (ty * MATCAP_SIZE + tx) * 4;
          r = lut[idx];
          g = lut[idx + 1];
          b = lut[idx + 2];
          if (backFace) {
            const bt = 0.45 + 0.65 * Math.max(0, Math.min(1, vnz));
            r = Math.round(r * 0.75 * 0.32 + 140 * bt * 0.68);
            g = Math.round(g * 0.75 * 0.32 + 56 * bt * 0.68);
            b = Math.round(b * 0.75 * 0.32 + 36 * bt * 0.68);
          }
        }

        const avgZ = (p0.z + p1.z + p2.z) * 0.3333333;
        triList.push({
          x0: p0.sx, y0: p0.sy, x1: p1.sx, y1: p1.sy, x2: p2.sx, y2: p2.sy,
          z: avgZ,
          fill: a < 0.99 ? `rgba(${r},${g},${b},${a.toFixed(2)})` : `rgb(${r},${g},${b})`,
        });
      };

      for (let s = 0; s < spans.length; s++) {
        const sp = spans[s];
        const vEnd = sp.firstVert + sp.vertCount;
        for (let vi = sp.firstVert; vi < vEnd; vi += 3 * stride) {
          const i0 = vi * 4;
          const x0 = bminX + verts[i0] * sx, y0 = bminY + verts[i0 + 1] * sy, z0 = bminZ + verts[i0 + 2] * sz;
          const x1 = bminX + verts[i0 + 4] * sx, y1 = bminY + verts[i0 + 5] * sy, z1 = bminZ + verts[i0 + 6] * sz;
          const x2 = bminX + verts[i0 + 8] * sx, y2 = bminY + verts[i0 + 9] * sy, z2 = bminZ + verts[i0 + 10] * sz;

          if (state.sliceEnabled) {
            const d0 = x0 * clipPlane[0] + y0 * clipPlane[1] + z0 * clipPlane[2] + clipPlane[3];
            const d1 = x1 * clipPlane[0] + y1 * clipPlane[1] + z1 * clipPlane[2] + clipPlane[3];
            const d2 = x2 * clipPlane[0] + y2 * clipPlane[1] + z2 * clipPlane[2] + clipPlane[3];
            if (d0 < 0 && d1 < 0 && d2 < 0) continue;
            if (d0 >= 0 && d1 >= 0 && d2 >= 0) {
              emitTri(x0, y0, z0, x1, y1, z1, x2, y2, z2);
            } else {
              const clipped = clipPolygonPlane(
                [{ x: x0, y: y0, z: z0 }, { x: x1, y: y1, z: z1 }, { x: x2, y: y2, z: z2 }],
                clipPlane
              );
              for (let k = 1; k + 1 < clipped.length; k++) {
                emitTri(
                  clipped[0].x, clipped[0].y, clipped[0].z,
                  clipped[k].x, clipped[k].y, clipped[k].z,
                  clipped[k + 1].x, clipped[k + 1].y, clipped[k + 1].z
                );
              }
            }
          } else {
            emitTri(x0, y0, z0, x1, y1, z1, x2, y2, z2);
          }
        }
      }

      // Back-to-front Painter's sort
      triList.sort((a, b) => b.z - a.z);

      const wireStroke = state.wireframe
        ? (state.darkTheme ? 'rgba(62,148,245,0.65)' : 'rgba(13,46,102,0.65)')
        : null;
      ctx.lineWidth = Math.max(0.8, 0.9 * this.dpr);
      ctx.lineJoin = 'round';

      for (let i = 0; i < triList.length; i++) {
        const t = triList[i];
        ctx.beginPath();
        ctx.moveTo(t.x0, t.y0);
        ctx.lineTo(t.x1, t.y1);
        ctx.lineTo(t.x2, t.y2);
        ctx.closePath();
        ctx.fillStyle = t.fill;
        ctx.fill();
        if (wireStroke) {
          ctx.strokeStyle = wireStroke;
          ctx.stroke();
        }
      }
    }

    // 4. Measurement pins & line
    if (state.measureHasA) {
      const rPin = Math.max(cam.distance() * 0.012, 0.1);
      const drawDia = (p) => {
        const pts = [
          [p.x + rPin, p.y, p.z], [p.x - rPin, p.y, p.z],
          [p.x, p.y + rPin, p.z], [p.x, p.y - rPin, p.z],
          [p.x, p.y, p.z + rPin], [p.x, p.y, p.z - rPin],
        ];
        const pairs = [[0,2],[2,1],[1,3],[3,0],[0,4],[4,1],[1,5],[5,0],[2,4],[4,3],[3,5],[5,2]];
        for (let i = 0; i < pairs.length; i++) {
          const a = pts[pairs[i][0]], b = pts[pairs[i][1]];
          this._drawLine3D(ctx, vp, a[0], a[1], a[2], b[0], b[1], b[2], '#fa9e26', 1.6 * this.dpr);
        }
      };
      drawDia(state.measureA);
      if (state.measureHasB) {
        drawDia(state.measureB);
        this._drawLine3D(
          ctx, vp,
          state.measureA.x, state.measureA.y, state.measureA.z,
          state.measureB.x, state.measureB.y, state.measureB.z,
          '#59c7ff', 1.8 * this.dpr
        );
      }
    }

    // 5. Bottom-right 3D Axis Triad
    let axisTipsCss = null;
    if (state.showHud) {
      const cssW = w / this.dpr;
      const cssH = h / this.dpr;
      const cxCss = cssW - 54.0;
      const cyCss = cssH - 54.0;
      const armCss = 24.0;
      const labelArmCss = 35.0;
      const dirs = [
        [view[0], view[1], view[2]],
        [view[4], view[5], view[6]],
        [view[8], view[9], view[10]],
      ];
      const cols = state.darkTheme
        ? ['#f55c52', '#57e06b', '#5ca3fa']
        : ['#d12e29', '#249933', '#1f66d6'];

      axisTipsCss = new Array(3);
      ctx.lineWidth = Math.max(1.2, 1.5 * this.dpr);
      for (let i = 0; i < 3; i++) {
        const sxCss = cxCss + dirs[i][0] * armCss;
        const syCss = cyCss - dirs[i][1] * armCss;
        axisTipsCss[i] = {
          x: cxCss + dirs[i][0] * labelArmCss,
          y: cyCss - dirs[i][1] * labelArmCss,
        };
        ctx.strokeStyle = cols[i];
        ctx.beginPath();
        ctx.moveTo(cxCss * this.dpr, cyCss * this.dpr);
        ctx.lineTo(sxCss * this.dpr, syCss * this.dpr);
        ctx.stroke();
      }
    }

    this.accumCount = Math.min(this.targetSamples, this.accumCount + 1);
    return { cstats, axisTipsCss, accumCount: this.accumCount };
  }

  pickWorldPoint(cssX, cssY, cam, mesh, state) {
    return pickMeshCpu(cssX, cssY, this.width, this.height, this.dpr, cam, mesh, state);
  }
}

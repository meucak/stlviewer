import { Mat4, vSub, vCross, vDot, vNorm } from './math3d.js';

const INV_65535 = 1.0 / 65535.0;

function raySphereHit(ox, oy, oz, dx, dy, dz, cx, cy, cz, r, maxT) {
  const mx = ox - cx;
  const my = oy - cy;
  const mz = oz - cz;
  const b = mx * dx + my * dy + mz * dz;
  const c = mx * mx + my * my + mz * mz - r * r;
  if (c > 0.0 && b > 0.0) return false;
  const disc = b * b - c;
  if (disc < 0.0) return false;
  const tNear = -b - Math.sqrt(disc);
  return tNear < maxT;
}

export function pickMeshCpu(cssX, cssY, vpW, vpH, dpr, cam, mesh, state) {
  if (!mesh || !mesh.ok || !mesh.vertices || vpW <= 0 || vpH <= 0) return null;

  const px = Math.max(0, Math.min(vpW - 1, Math.round(cssX * dpr)));
  const py = Math.max(0, Math.min(vpH - 1, Math.round(cssY * dpr)));
  const glY = vpH - 1 - py;

  const vp = cam.viewProj(vpW, vpH);
  const invVP = Mat4.inverse(vp);
  if (!invVP) return null;

  const p0 = Mat4.unproject({ x: px + 0.5, y: glY + 0.5, z: 0.0 }, invVP, vpW, vpH);
  const p1 = Mat4.unproject({ x: px + 0.5, y: glY + 0.5, z: 1.0 }, invVP, vpW, vpH);
  if (!p0 || !p1) return null;

  const rayDir = vNorm(vSub(p1, p0));
  const ox = p0.x, oy = p0.y, oz = p0.z;
  const dx = rayDir.x, dy = rayDir.y, dz = rayDir.z;

  const cullBack = !state.twoSided && !state.sliceEnabled && !state.xray;

  let sliceOn = false;
  let cp0 = 0, cp1 = 0, cp2 = 0, cp3 = 1;
  if (state.sliceEnabled) {
    sliceOn = true;
    const ax = state.sliceAxis;
    const bminAx = ax === 0 ? mesh.bboxMin.x : ax === 1 ? mesh.bboxMin.y : mesh.bboxMin.z;
    const bextAx = ax === 0 ? mesh.bboxExtent.x : ax === 1 ? mesh.bboxExtent.y : mesh.bboxExtent.z;
    const pos = bminAx + state.sliceT * bextAx;
    const sign = state.sliceFlip ? 1.0 : -1.0;
    if (ax === 0) cp0 = sign;
    else if (ax === 1) cp1 = sign;
    else cp2 = sign;
    cp3 = -sign * pos;
  }

  const verts = mesh.vertices;
  const bminX = mesh.bboxMin.x, bminY = mesh.bboxMin.y, bminZ = mesh.bboxMin.z;
  const sx = mesh.quantScale.x * INV_65535;
  const sy = mesh.quantScale.y * INV_65535;
  const sz = mesh.quantScale.z * INV_65535;

  let bestT = Infinity;
  let bestHit = null;

  const chunks = mesh.chunks;
  for (let c = 0; c < chunks.length; c++) {
    const ch = chunks[c];
    if (ch.vertCount === 0) continue;
    if (!raySphereHit(ox, oy, oz, dx, dy, dz, ch.cx, ch.cy, ch.cz, ch.radius + 1e-3, bestT)) {
      continue;
    }

    const vStart = ch.firstVert;
    const vEnd = vStart + ch.vertCount;
    for (let vi = vStart; vi < vEnd; vi += 3) {
      const i0 = vi * 4;
      const i1 = i0 + 4;
      const i2 = i0 + 8;

      const x0 = bminX + verts[i0] * sx;
      const y0 = bminY + verts[i0 + 1] * sy;
      const z0 = bminZ + verts[i0 + 2] * sz;

      const x1 = bminX + verts[i1] * sx;
      const y1 = bminY + verts[i1 + 1] * sy;
      const z1 = bminZ + verts[i1 + 2] * sz;

      const x2 = bminX + verts[i2] * sx;
      const y2 = bminY + verts[i2 + 1] * sy;
      const z2 = bminZ + verts[i2 + 2] * sz;

      const e1x = x1 - x0, e1y = y1 - y0, e1z = z1 - z0;
      const e2x = x2 - x0, e2y = y2 - y0, e2z = z2 - z0;

      // pvec = cross(rayDir, e2)
      const pxv = dy * e2z - dz * e2y;
      const pyv = dz * e2x - dx * e2z;
      const pzv = dx * e2y - dy * e2x;

      const det = e1x * pxv + e1y * pyv + e1z * pzv;
      if (cullBack) {
        if (det <= 1e-9) continue;
      } else {
        if (Math.abs(det) <= 1e-9) continue;
      }

      const invDet = 1.0 / det;
      const tx = ox - x0, ty = oy - y0, tz = oz - z0;
      const u = (tx * pxv + ty * pyv + tz * pzv) * invDet;
      if (u < 0.0 || u > 1.0) continue;

      // qvec = cross(tvec, e1)
      const qx = ty * e1z - tz * e1y;
      const qy = tz * e1x - tx * e1z;
      const qz = tx * e1y - ty * e1x;

      const v = (dx * qx + dy * qy + dz * qz) * invDet;
      if (v < 0.0 || u + v > 1.0) continue;

      const t = (e2x * qx + e2y * qy + e2z * qz) * invDet;
      if (t < 0.0 || t >= bestT) continue;

      const hx = ox + dx * t;
      const hy = oy + dy * t;
      const hz = oz + dz * t;
      if (sliceOn && (hx * cp0 + hy * cp1 + hz * cp2 + cp3) < 0.0) {
        continue;
      }

      bestT = t;
      bestHit = { x: hx, y: hy, z: hz };
    }
  }

  return bestHit;
}

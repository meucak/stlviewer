import { Frustum } from './math3d.js';

export function cullChunks(chunks, frustum, opts) {
  const spans = [];
  const stats = {
    totalChunks: chunks.length,
    frustumCulled: 0,
    coneCulled: 0,
    visibleChunks: 0,
    visibleTris: 0,
    drawCalls: 0,
  };

  let hasOpen = false;
  let curFirst = 0;
  let curCount = 0;

  const eyeX = opts.eye.x;
  const eyeY = opts.eye.y;
  const eyeZ = opts.eye.z;
  const fwdX = opts.camForward.x;
  const fwdY = opts.camForward.y;
  const fwdZ = opts.camForward.z;
  const coneEnabled = !!opts.backfaceConeCull;
  const isOrtho = !!opts.isOrtho;

  for (let i = 0; i < chunks.length; i++) {
    const ch = chunks[i];
    if (ch.vertCount === 0) continue;

    if (!Frustum.intersectsSphere(frustum, ch.cx, ch.cy, ch.cz, ch.radius)) {
      stats.frustumCulled++;
      continue;
    }

    if (coneEnabled && ch.cullable) {
      if (isOrtho) {
        const d = fwdX * ch.ax + fwdY * ch.ay + fwdZ * ch.az;
        if (d > ch.cutoff + 1e-4) {
          stats.coneCulled++;
          continue;
        }
      } else {
        const dx = ch.cx - eyeX;
        const dy = ch.cy - eyeY;
        const dz = ch.cz - eyeZ;
        const dist = Math.hypot(dx, dy, dz);
        if (dist > ch.radius) {
          const d = dx * ch.ax + dy * ch.ay + dz * ch.az;
          if (d > dist * ch.cutoff + 1.001 * ch.radius) {
            stats.coneCulled++;
            continue;
          }
        }
      }
    }

    stats.visibleChunks++;
    stats.visibleTris += ch.triCount;

    if (hasOpen && curFirst + curCount === ch.firstVert) {
      curCount += ch.vertCount;
    } else {
      if (hasOpen) spans.push({ firstVert: curFirst, vertCount: curCount });
      curFirst = ch.firstVert;
      curCount = ch.vertCount;
      hasOpen = true;
    }
  }

  if (hasOpen) spans.push({ firstVert: curFirst, vertCount: curCount });
  stats.drawCalls = spans.length;
  return { spans, stats };
}

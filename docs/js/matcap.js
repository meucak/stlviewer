import { vAdd, vDot, vLerp, vNorm, vScale, vSub } from './math3d.js';

export const MATCAP_SIZE = 256;

export const MatcapKind = Object.freeze({
  Clay: 0,
  Aluminum: 1,
  Slate: 2,
  Normals: 3,
  Count: 4,
});

export const MATCAP_NAMES = Object.freeze(['Clay', 'Aluminum', 'Slate', 'Normals']);

function clamp(v, lo, hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

function linearToSrgbU8(x) {
  const c = clamp(x, 0.0, 1.0);
  const s = c <= 0.0031308 ? c * 12.92 : 1.055 * Math.pow(c, 1.0 / 2.4) - 0.055;
  return clamp((s * 255.0 + 0.5) | 0, 0, 255);
}

function acesFilmic(c) {
  const f = (x) => clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
  return { x: f(c.x), y: f(c.y), z: f(c.z) };
}

function smoothstep(e0, e1, x) {
  const t = clamp((x - e0) / (e1 - e0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

export function matcapName(kind) {
  return MATCAP_NAMES[kind] || 'Clay';
}

export function generateMatcapRgba8(kind) {
  const buf = new Uint8Array(MATCAP_SIZE * MATCAP_SIZE * 4);
  const keyDir = vNorm({ x: 0.46, y: 0.62, z: 0.64 });
  const fillDir = vNorm({ x: -0.62, y: 0.28, z: 0.73 });
  const rimDir = vNorm({ x: -0.25, y: -0.78, z: 0.57 });
  const viewDir = { x: 0.0, y: 0.0, z: 1.0 };
  const halfKey = vNorm(vAdd(keyDir, viewDir));
  const halfFill = vNorm(vAdd(fillDir, viewDir));

  for (let y = 0; y < MATCAP_SIZE; y++) {
    const ny = 1.0 - (y + 0.5) / (MATCAP_SIZE * 0.5);
    for (let x = 0; x < MATCAP_SIZE; x++) {
      const nx = (x + 0.5) / (MATCAP_SIZE * 0.5) - 1.0;
      let r2 = nx * nx + ny * ny;
      let ux = nx, uy = ny;
      if (r2 > 0.992) {
        const s = Math.sqrt(0.992 / r2);
        ux *= s;
        uy *= s;
        r2 = 0.992;
      }
      const nz = Math.sqrt(Math.max(0.0, 1.0 - r2));
      const n = { x: ux, y: uy, z: nz };

      const idx = (y * MATCAP_SIZE + x) * 4;
      if (kind === MatcapKind.Normals) {
        buf[idx + 0] = clamp(((n.x * 0.5 + 0.5) * 255.0 + 0.5) | 0, 0, 255);
        buf[idx + 1] = clamp(((n.y * 0.5 + 0.5) * 255.0 + 0.5) | 0, 0, 255);
        buf[idx + 2] = clamp(((n.z * 0.5 + 0.5) * 255.0 + 0.5) | 0, 0, 255);
        buf[idx + 3] = 255;
        continue;
      }

      const fresnel = Math.pow(1.0 - clamp(n.z, 0.0, 1.0), 3.0);
      const nDotK = Math.max(0.0, vDot(n, keyDir));
      const nDotF = Math.max(0.0, vDot(n, fillDir));
      const nDotR = Math.max(0.0, vDot(n, rimDir));
      const wrapK = clamp((vDot(n, keyDir) + 0.42) / 1.42, 0.0, 1.0);

      let lin;
      if (kind === MatcapKind.Clay) {
        const spec = Math.pow(Math.max(0.0, vDot(n, halfKey)), 18.0) * 0.06;
        const bounce = Math.max(0.0, -n.y * 0.7 + 0.3) * 0.07;
        const lightR = 0.11 + 1.05 * (wrapK * 0.85) + 0.26 * (nDotF * 0.65);
        const lightG = 0.115 + 0.99 * (wrapK * 0.85) + 0.32 * (nDotF * 0.65);
        const lightB = 0.125 + 0.91 * (wrapK * 0.85) + 0.40 * (nDotF * 0.65);
        const rimT = fresnel * 0.16 + nDotR * 0.08;
        lin = {
          x: 0.62 * lightR + bounce * 0.75 + spec + 0.45 * rimT,
          y: 0.58 * lightG + bounce * 0.68 + 0.97 * spec + 0.48 * rimT,
          z: 0.53 * lightB + bounce * 0.60 + 0.92 * spec + 0.54 * rimT,
        };
      } else if (kind === MatcapKind.Aluminum) {
        const r = vSub(vScale(viewDir, 2.0 * n.z), n);
        const ry = -r.y;
        const rx = -r.x;
        const horizon = smoothstep(-0.18, 0.24, ry);
        const envSky = vLerp({ x: 0.16, y: 0.18, z: 0.22 }, { x: 0.58, y: 0.65, z: 0.74 }, smoothstep(0.0, 0.85, ry));
        const envGnd = vLerp({ x: 0.07, y: 0.08, z: 0.10 }, { x: 0.16, y: 0.17, z: 0.19 }, smoothstep(-1.0, -0.05, ry));
        const env = vLerp(envGnd, envSky, horizon);
        const strip = smoothstep(0.25, 0.70, ry) * smoothstep(0.55, 0.10, Math.abs(rx - 0.18 * ry));
        env.x += 0.85 * strip;
        env.y += 0.88 * strip;
        env.z += 0.92 * strip;
        const specK = Math.pow(Math.max(0.0, vDot(n, halfKey)), 36.0) * 0.55;
        const specF = Math.pow(Math.max(0.0, vDot(n, halfFill)), 20.0) * 0.22;
        const sp = specK + specF + fresnel * 0.28;
        lin = {
          x: 0.76 * (env.x * 0.82 + 0.18 * nDotK) + 0.96 * sp,
          y: 0.80 * (env.y * 0.82 + 0.19 * nDotK) + 0.98 * sp,
          z: 0.85 * (env.z * 0.82 + 0.21 * nDotK) + 1.00 * sp,
        };
      } else {
        const spec1 = Math.pow(Math.max(0.0, vDot(n, halfKey)), 48.0) * 0.42;
        const spec2 = Math.pow(Math.max(0.0, vDot(n, halfFill)), 16.0) * 0.12;
        const sp = spec1 + spec2;
        const rimT = fresnel * 0.30 + nDotR * 0.12;
        lin = {
          x: 0.16 * (0.06 + 0.95 * nDotK * 0.95 + 0.35 * nDotF * 0.65) + 0.92 * sp + 0.36 * rimT,
          y: 0.21 * (0.075 + 0.97 * nDotK * 0.95 + 0.45 * nDotF * 0.65) + 0.96 * sp + 0.52 * rimT,
          z: 0.28 * (0.095 + 1.00 * nDotK * 0.95 + 0.60 * nDotF * 0.65) + 1.00 * sp + 0.72 * rimT,
        };
      }

      const tone = acesFilmic(lin);
      buf[idx + 0] = linearToSrgbU8(tone.x);
      buf[idx + 1] = linearToSrgbU8(tone.y);
      buf[idx + 2] = linearToSrgbU8(tone.z);
      buf[idx + 3] = 255;
    }
  }
  return buf;
}

export const PI = 3.14159265358979323846;
export const DEG2RAD = PI / 180.0;

export function vec3(x = 0, y = 0, z = 0) {
  return { x, y, z };
}

export function vAdd(a, b) {
  return { x: a.x + b.x, y: a.y + b.y, z: a.z + b.z };
}

export function vSub(a, b) {
  return { x: a.x - b.x, y: a.y - b.y, z: a.z - b.z };
}

export function vNeg(a) {
  return { x: -a.x, y: -a.y, z: -a.z };
}

export function vScale(a, s) {
  return { x: a.x * s, y: a.y * s, z: a.z * s };
}

export function vDot(a, b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

export function vCross(a, b) {
  return {
    x: a.y * b.z - a.z * b.y,
    y: a.z * b.x - a.x * b.z,
    z: a.x * b.y - a.y * b.x,
  };
}

export function vLenSq(v) {
  return v.x * v.x + v.y * v.y + v.z * v.z;
}

export function vLen(v) {
  return Math.hypot(v.x, v.y, v.z);
}

export function vNorm(v) {
  const l = vLen(v);
  if (l > 1e-12) {
    const inv = 1.0 / l;
    return { x: v.x * inv, y: v.y * inv, z: v.z * inv };
  }
  return { x: 0, y: 0, z: 0 };
}

export function vMin(a, b) {
  return {
    x: Math.min(a.x, b.x),
    y: Math.min(a.y, b.y),
    z: Math.min(a.z, b.z),
  };
}

export function vMax(a, b) {
  return {
    x: Math.max(a.x, b.x),
    y: Math.max(a.y, b.y),
    z: Math.max(a.z, b.z),
  };
}

export function vLerp(a, b, t) {
  return {
    x: a.x + (b.x - a.x) * t,
    y: a.y + (b.y - a.y) * t,
    z: a.z + (b.z - a.z) * t,
  };
}

// Column-major 4x4 matrix stored in Float32Array(16)
export const Mat4 = {
  identity() {
    const r = new Float32Array(16);
    r[0] = 1;
    r[5] = 1;
    r[10] = 1;
    r[15] = 1;
    return r;
  },

  perspective(fovYRad, aspect, zNear, zFar) {
    const r = new Float32Array(16);
    const f = 1.0 / Math.tan(fovYRad * 0.5);
    const safeAspect = Math.max(aspect, 1e-6);
    r[0] = f / safeAspect;
    r[5] = f;
    r[10] = -(zFar + zNear) / (zFar - zNear);
    r[11] = -1.0;
    r[14] = -(2.0 * zFar * zNear) / (zFar - zNear);
    return r;
  },

  ortho(left, right, bottom, top, zNear, zFar) {
    const r = Mat4.identity();
    r[0] = 2.0 / (right - left);
    r[5] = 2.0 / (top - bottom);
    r[10] = -2.0 / (zFar - zNear);
    r[12] = -(right + left) / (right - left);
    r[13] = -(top + bottom) / (top - bottom);
    r[14] = -(zFar + zNear) / (zFar - zNear);
    return r;
  },

  lookAt(eye, target, up) {
    const f = vNorm(vSub(target, eye));
    const s = vNorm(vCross(f, up));
    const u = vCross(s, f);
    const r = Mat4.identity();
    r[0] = s.x; r[4] = s.y; r[8] = s.z; r[12] = -vDot(s, eye);
    r[1] = u.x; r[5] = u.y; r[9] = u.z; r[13] = -vDot(u, eye);
    r[2] = -f.x; r[6] = -f.y; r[10] = -f.z; r[14] = vDot(f, eye);
    return r;
  },

  multiply(a, b) {
    const r = new Float32Array(16);
    for (let col = 0; col < 4; col++) {
      const c4 = col * 4;
      const b0 = b[c4], b1 = b[c4 + 1], b2 = b[c4 + 2], b3 = b[c4 + 3];
      for (let row = 0; row < 4; row++) {
        r[c4 + row] =
          a[row] * b0 +
          a[4 + row] * b1 +
          a[8 + row] * b2 +
          a[12 + row] * b3;
      }
    }
    return r;
  },

  mulVec4(a, x, y, z, w) {
    return {
      x: a[0] * x + a[4] * y + a[8] * z + a[12] * w,
      y: a[1] * x + a[5] * y + a[9] * z + a[13] * w,
      z: a[2] * x + a[6] * y + a[10] * z + a[14] * w,
      w: a[3] * x + a[7] * y + a[11] * z + a[15] * w,
    };
  },

  inverse(inMat) {
    const m = new Float64Array(16);
    for (let i = 0; i < 16; i++) m[i] = inMat[i];
    const inv = new Float64Array(16);

    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];

    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];

    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
             m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
              m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    const det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (Math.abs(det) < 1e-18) return null;
    const invDet = 1.0 / det;
    const out = new Float32Array(16);
    for (let i = 0; i < 16; i++) out[i] = inv[i] * invDet;
    return out;
  },

  project(world, vp, vpW, vpH) {
    const clip = Mat4.mulVec4(vp, world.x, world.y, world.z, 1.0);
    if (Math.abs(clip.w) < 1e-8) return null;
    const invW = 1.0 / clip.w;
    const ndcX = clip.x * invW;
    const ndcY = clip.y * invW;
    const ndcZ = clip.z * invW;
    return {
      x: (ndcX * 0.5 + 0.5) * vpW,
      y: (ndcY * 0.5 + 0.5) * vpH,
      z: ndcZ * 0.5 + 0.5,
      visible: clip.w > 0.0,
    };
  },

  unproject(win, invVp, vpW, vpH) {
    if (vpW <= 0 || vpH <= 0) return null;
    const ndcX = (win.x / vpW) * 2.0 - 1.0;
    const ndcY = (win.y / vpH) * 2.0 - 1.0;
    const ndcZ = win.z * 2.0 - 1.0;
    const w = Mat4.mulVec4(invVp, ndcX, ndcY, ndcZ, 1.0);
    if (Math.abs(w.w) < 1e-10) return null;
    const invW = 1.0 / w.w;
    return {
      x: w.x * invW,
      y: w.y * invW,
      z: w.z * invW,
    };
  },
};

export const Frustum = {
  fromViewProj(vp) {
    const row = (r) => ({ x: vp[r], y: vp[4 + r], z: vp[8 + r], w: vp[12 + r] });
    const r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    const makePlane = (v) => {
      const len = Math.hypot(v.x, v.y, v.z);
      const inv = len > 1e-12 ? 1.0 / len : 1.0;
      return { nx: v.x * inv, ny: v.y * inv, nz: v.z * inv, d: v.w * inv };
    };
    const add = (a, b) => ({ x: a.x + b.x, y: a.y + b.y, z: a.z + b.z, w: a.w + b.w });
    const sub = (a, b) => ({ x: a.x - b.x, y: a.y - b.y, z: a.z - b.z, w: a.w - b.w });
    return [
      makePlane(add(r3, r0)),
      makePlane(sub(r3, r0)),
      makePlane(add(r3, r1)),
      makePlane(sub(r3, r1)),
      makePlane(add(r3, r2)),
      makePlane(sub(r3, r2)),
    ];
  },

  intersectsSphere(planes, cx, cy, cz, radius) {
    for (let i = 0; i < 6; i++) {
      const p = planes[i];
      const dist = p.nx * cx + p.ny * cy + p.nz * cz + p.d;
      if (dist < -radius) return false;
    }
    return true;
  },
};

export function halton(index, base) {
  let f = 1.0;
  let r = 0.0;
  let i = index >>> 0;
  while (i > 0) {
    f /= base;
    r += f * (i % base);
    i = Math.floor(i / base);
  }
  return r;
}

export function subpixelJitterNdc(sampleIdx, vpW, vpH) {
  if (sampleIdx === 0 || vpW <= 0 || vpH <= 0) {
    return { jx: 0.0, jy: 0.0 };
  }
  const ox = halton(sampleIdx, 2) - 0.5;
  const oy = halton(sampleIdx, 3) - 0.5;
  return {
    jx: ox * (2.0 / vpW),
    jy: oy * (2.0 / vpH),
  };
}

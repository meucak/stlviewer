import { parseStlBuffer } from './stl_parser.js';

self.postMessage({ type: 'ready' });

self.onmessage = (event) => {
  const { id, buffer, name } = event.data;
  try {
    const result = parseStlBuffer(buffer, name);
    if (result.ok && result.vertices) {
      self.postMessage({ id, result }, [result.vertices.buffer]);
    } else {
      self.postMessage({ id, result });
    }
  } catch (err) {
    self.postMessage({
      id,
      result: { ok: false, error: err?.message || 'STL parse error', name },
    });
  }
};

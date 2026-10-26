/* PaperS3 EPUB image preparation. Shared by the Wi-Fi and BLE upload pages. */
(() => {
  'use strict';
  const MAX_BOOK = 64 * 1048576, MAX_IMAGE = 16 * 1048576;
  const decoder = new TextDecoder('utf-8', {fatal:true});
  const crcTable = Uint32Array.from({length:256}, (_, value) => {
    for (let n = 0; n < 8; ++n) value = (value >>> 1) ^ (0xedb88320 & -(value & 1));
    return value >>> 0;
  });
  function crc32(bytes) {
    let crc = 0xffffffff;
    for (const byte of bytes) crc = (crc >>> 8) ^ crcTable[(crc ^ byte) & 255];
    return (crc ^ 0xffffffff) >>> 0;
  }
  function check(cancel) { if (cancel?.cancelled) throw new Error('Upload preparation canceled.'); }
  const pause = () => new Promise(resolve => setTimeout(resolve, 0));
  function zip(bytes) {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const u16 = offset => view.getUint16(offset, true), u32 = offset => view.getUint32(offset, true);
    let end = bytes.length - 22;
    while (end >= Math.max(0, bytes.length - 65557) &&
           (u32(end) !== 0x06054b50 || end + 22 + u16(end + 20) !== bytes.length)) --end;
    if (end < Math.max(0, bytes.length - 65557)) throw new Error('Choose a valid EPUB ZIP archive.');
    const count = u16(end + 10), start = u32(end + 16), length = u32(end + 12);
    if (!count || count > 8192 || u16(end + 4) || u16(end + 6) || u16(end + 8) !== count || start + length > end)
      throw new Error('ZIP64, multi-volume or oversized EPUB directories are not supported.');
    const entries = [], names = new Set();
    let offset = start, nameBytes = 0;
    for (let i = 0; i < count; ++i) {
      if (offset + 46 > start + length || u32(offset) !== 0x02014b50) throw new Error('Invalid EPUB directory.');
      const nameSize = u16(offset + 28), extra = u16(offset + 30), comment = u16(offset + 32);
      const next = offset + 46 + nameSize + extra + comment;
      if (!nameSize || nameSize > 1024 || next > start + length || (nameBytes += nameSize) > 512 * 1024)
        throw new Error('Invalid EPUB filenames.');
      const rawName = bytes.subarray(offset + 46, offset + 46 + nameSize);
      let name;
      try { name = decoder.decode(rawName); }
      catch { throw new Error('Image preparation requires UTF-8 EPUB filenames. Turn it off to upload the original.'); }
      if (names.has(name)) throw new Error('EPUB contains duplicate resource names.');
      names.add(name);
      const flags = u16(offset + 8), method = u16(offset + 10), crc = u32(offset + 16);
      const compressed = u32(offset + 20), expanded = u32(offset + 24), localOffset = u32(offset + 42);
      if (flags & 1 || compressed === 0xffffffff || expanded === 0xffffffff || localOffset === 0xffffffff ||
          localOffset + 30 > start || u32(localOffset) !== 0x04034b50)
        throw new Error('Encrypted or invalid EPUB resources cannot be prepared.');
      const localNameSize = u16(localOffset + 26), localExtraSize = u16(localOffset + 28);
      const dataOffset = localOffset + 30 + localNameSize + localExtraSize;
      if (dataOffset + compressed > start || localNameSize !== nameSize || u16(localOffset + 8) !== method ||
          u16(localOffset + 6) !== flags || !rawName.every((byte, n) => byte === bytes[localOffset + 30 + n]))
        throw new Error('EPUB local resource header does not match its directory.');
      entries.push({name, flags, method, crc, compressed, expanded,
        local:bytes.subarray(localOffset, dataOffset), central:bytes.subarray(offset, next),
        data:bytes.subarray(dataOffset, dataOffset + compressed)});
      offset = next;
    }
    if (offset !== start + length) throw new Error('Invalid EPUB directory size.');
    return {entries, comment:bytes.subarray(end + 22)};
  }
  function extract(entry, limit) {
    if (entry.expanded > limit || entry.compressed > limit) throw new Error('EPUB image or metadata exceeds browser preparation limits.');
    let data;
    if (entry.method === 0) data = entry.data;
    else if (entry.method === 8) data = fflate.inflateSync(entry.data, {out:new Uint8Array(entry.expanded)});
    else throw new Error('Unsupported EPUB resource compression.');
    if (data.length !== entry.expanded || crc32(data) !== entry.crc) throw new Error('EPUB resource checksum failed: ' + entry.name);
    return data;
  }
  function jpegInfo(data) {
    if (data.length < 4 || data[0] !== 255 || data[1] !== 216) return null;
    let offset = 2;
    while (offset < data.length) {
      if (data[offset++] !== 255) return null;
      while (offset < data.length && data[offset] === 255) ++offset;
      if (offset >= data.length) return null;
      const marker = data[offset++];
      if (marker === 0xda || marker === 0xd9) return null;
      if (marker === 1 || marker >= 0xd0 && marker <= 0xd7) continue;
      if (offset + 2 > data.length) return null;
      const length = data[offset] * 256 + data[offset + 1];
      if (length < 2 || offset + length > data.length) return null;
      if (marker >= 0xc0 && marker <= 0xcf && ![0xc4,0xc8,0xcc].includes(marker)) {
        if (length < 8) return null;
        return {marker, progressive:marker === 0xc2, precision:data[offset + 2],
          width:data[offset + 5] * 256 + data[offset + 6], height:data[offset + 3] * 256 + data[offset + 4]};
      }
      offset += length;
    }
    return null;
  }
  function imagePath(base, href) {
    if (!href || href.includes(':') || href.startsWith('//')) return '';
    try {
      const relative = decodeURIComponent(href.split(/[?#]/, 1)[0]);
      const parts = (relative.startsWith('/') ? relative.slice(1) : base + relative).split('/'), result = [];
      for (const part of parts) {
        if (part === '..') { if (!result.length) return ''; result.pop(); }
        else if (part && part !== '.') result.push(part);
      }
      return result.join('/');
    } catch { return ''; }
  }
  async function convert(data, cancel) {
    const url = URL.createObjectURL(new Blob([data], {type:'image/jpeg'}));
    const image = new Image();
    try {
      await new Promise((resolve, reject) => { image.onload = resolve; image.onerror = () => reject(new Error('The browser could not decode an EPUB JPEG image.')); image.src = url; });
      check(cancel);
      const scale = Math.min(1, 540 / image.naturalWidth, 960 / image.naturalHeight);
      const canvas = document.createElement('canvas');
      canvas.width = Math.max(1, Math.floor(image.naturalWidth * scale)); canvas.height = Math.max(1, Math.floor(image.naturalHeight * scale));
      try {
        const context = canvas.getContext('2d', {alpha:false});
        if (!context) throw new Error('Browser image conversion is unavailable.');
        context.fillStyle = '#fff'; context.fillRect(0, 0, canvas.width, canvas.height);
        context.drawImage(image, 0, 0, canvas.width, canvas.height);
        const blob = await new Promise(resolve => canvas.toBlob(resolve, 'image/jpeg', 0.9));
        if (!blob) throw new Error('Browser JPEG encoding failed.');
        const output = new Uint8Array(await blob.arrayBuffer()), info = jpegInfo(output);
        if (!info || info.marker !== 0xc0 || info.precision !== 8 || info.width > 540 || info.height > 960 || output.length > 2 * 1048576)
          throw new Error('The browser did not produce a supported baseline JPEG.');
        check(cancel); return output;
      } finally { canvas.width = canvas.height = 1; }
    } finally { image.src = ''; URL.revokeObjectURL(url); }
  }
  function rebuild(archive, replacements) {
    const locals = [], central = [];
    let cursor = 0, centralSize = 0;
    const entries = [...archive.entries].sort((a,b) => (a.name === 'mimetype' ? -1 : b.name === 'mimetype' ? 1 : 0));
    for (const entry of entries) {
      const replacement = replacements.get(entry.name), payload = replacement || entry.data;
      const method = replacement ? 0 : entry.method, crc = replacement ? crc32(payload) : entry.crc;
      const expanded = replacement ? payload.length : entry.expanded, flags = entry.flags & ~8;
      const local = entry.name === 'mimetype' ? entry.local.slice(0, 30 + new DataView(entry.local.buffer, entry.local.byteOffset).getUint16(26, true)) : entry.local.slice();
      const directory = entry.central.slice();
      const lv = new DataView(local.buffer), cv = new DataView(directory.buffer);
      if (entry.name === 'mimetype') lv.setUint16(28, 0, true);
      lv.setUint16(6, flags, true); lv.setUint16(8, method, true);
      lv.setUint32(14, crc, true); lv.setUint32(18, payload.length, true); lv.setUint32(22, expanded, true);
      cv.setUint16(8, flags, true); cv.setUint16(10, method, true);
      cv.setUint32(16, crc, true); cv.setUint32(20, payload.length, true); cv.setUint32(24, expanded, true); cv.setUint32(42, cursor, true);
      locals.push(local, payload); central.push(directory); cursor += local.length + payload.length; centralSize += directory.length;
    }
    const end = new Uint8Array(22 + archive.comment.length), view = new DataView(end.buffer);
    view.setUint32(0, 0x06054b50, true); view.setUint16(8, entries.length, true); view.setUint16(10, entries.length, true);
    view.setUint32(12, centralSize, true); view.setUint32(16, cursor, true); view.setUint16(20, archive.comment.length, true); end.set(archive.comment, 22);
    return new Blob([...locals, ...central, end], {type:'application/epub+zip'});
  }
  async function prepare(file, {cancel, onProgress = () => {}} = {}) {
    check(cancel);
    if (!/\.epub$/i.test(file.name) || new TextEncoder().encode(file.name).length > 96) throw new Error('Choose an EPUB with a filename of at most 96 bytes.');
    if (!globalThis.fflate) throw new Error('Image preparation is unavailable. Reload the page or turn off image optimization.');
    if (file.size > MAX_BOOK || file.size < 22) throw new Error('Image preparation supports EPUBs up to 64 MiB.');
    const bytes = new Uint8Array(await file.arrayBuffer()); check(cancel);
    const archive = zip(bytes), replacements = new Map(), candidates = new Set();
    const mimetype = archive.entries.find(entry => entry.name === 'mimetype');
    if (!mimetype || decoder.decode(extract(mimetype, 64)) !== 'application/epub+zip') throw new Error('The selected file is not an EPUB.');
    for (const entry of archive.entries) {
      if (/\.jpe?g$/i.test(entry.name) || /\.jpe$/i.test(entry.name)) candidates.add(entry.name);
      if (/\.opf$/i.test(entry.name)) {
        const xml = new DOMParser().parseFromString(decoder.decode(extract(entry, 512 * 1024)), 'application/xml');
        if (xml.querySelector('parsererror')) throw new Error('Invalid EPUB package metadata.');
        const base = entry.name.slice(0, entry.name.lastIndexOf('/') + 1);
        for (const item of xml.getElementsByTagNameNS('*','item'))
          if (/^image\/(jpeg|jpg)$/i.test(item.getAttribute('media-type') || '')) candidates.add(imagePath(base, item.getAttribute('href')));
      }
    }
    const images = archive.entries.filter(entry => candidates.has(entry.name));
    let converted = 0;
    for (let index = 0; index < images.length; ++index) {
      check(cancel); await onProgress({current:index + 1, total:images.length, converted}); await pause(); check(cancel);
      const entry = images[index], data = extract(entry, MAX_IMAGE), info = jpegInfo(data);
      if (!info) throw new Error('Invalid JPEG image: ' + entry.name);
      if (!info.width || !info.height || info.width > 16384 || info.height > 16384 || info.width * info.height > 32 * 1048576)
        throw new Error('Image exceeds browser preparation limits: ' + entry.name);
      if (info.progressive || info.width > 540 || info.height > 960) {
        if (archive.entries.some(entry => entry.name === 'META-INF/signatures.xml')) throw new Error('Signed EPUBs cannot be modified. Turn off image optimization to upload the original.');
        replacements.set(entry.name, await convert(data, cancel)); ++converted;
      }
    }
    check(cancel);
    if (!converted) return {file, converted:0, originalSize:file.size};
    // EPUB requires an uncompressed mimetype as the first ZIP entry.
    replacements.set('mimetype', extract(mimetype, 64));
    const output = rebuild(archive, replacements);
    return {file:new File([output], file.name, {type:'application/epub+zip', lastModified:file.lastModified}), converted, originalSize:file.size};
  }
  globalThis.PaperEpub = {prepare, jpegInfo};
})();

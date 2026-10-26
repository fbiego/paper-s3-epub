export const UUID = {
  service: 'fb1e5000-54ae-4a28-9f74-dfccb248601d',
  command: 'fb1e5001-54ae-4a28-9f74-dfccb248601d',
  response: 'fb1e5002-54ae-4a28-9f74-dfccb248601d',
  data: 'fb1e5003-54ae-4a28-9f74-dfccb248601d'
};
export function updateCrc(crc, data) {
  for (const byte of data) {
    crc ^= byte;
    for (let bit = 0; bit < 8; ++bit) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return crc >>> 0;
}
export function packet(offset, bytes) {
  const data = new Uint8Array(bytes.length + 4);
  new DataView(data.buffer).setUint32(0, offset, true); data.set(bytes, 4); return data;
}
export class ReaderBle {
  constructor({timeoutMs = 60000, disconnected = () => {}} = {}) {
    this.timeoutMs = timeoutMs; this.disconnected = disconnected;
    this.pending = null; this.chunk = 16; this.device = null;
  }
  get connected() { return !!this.device?.gatt.connected; }
  async connect(bluetooth = globalThis.navigator?.bluetooth) {
    if (!bluetooth) throw new Error('Use Chrome or Edge with Web Bluetooth on localhost or HTTPS.');
    this.device = await bluetooth.requestDevice({filters:[{services:[UUID.service]}]});
    this.device.addEventListener('gattserverdisconnected', () => {
      this.reject(new Error('Bluetooth disconnected. Reconnect and sign in again.'));
      this.disconnected();
    });
    try {
      const server = await this.device.gatt.connect();
      const service = await server.getPrimaryService(UUID.service);
      this.command = await service.getCharacteristic(UUID.command);
      this.response = await service.getCharacteristic(UUID.response);
      this.data = await service.getCharacteristic(UUID.data);
      this.text = ''; this.decoder = new TextDecoder(); this.binary = new Map();
      this.response.addEventListener('characteristicvaluechanged', event => {
        this.text += this.decoder.decode(event.target.value, {stream:true});
        if (this.text.length > 131072) { this.text = ''; this.reject(new Error('Reader response is too large.')); return; }
        let newline;
        while ((newline = this.text.indexOf('\n')) >= 0) {
          const line = this.text.slice(0, newline); this.text = this.text.slice(newline + 1);
          try {
            const result = JSON.parse(line);
            if (!result.ok) this.reject(new Error(result.message || 'Reader rejected the request.'));
            else if (this.pending) { const pending = this.pending; this.pending = null; clearTimeout(pending.timer); pending.resolve(result); }
          } catch (error) { this.reject(error); }
        }
      });
      this.data.addEventListener('characteristicvaluechanged', event => {
        const view = event.target.value;
        if (view.byteLength < 4) { this.reject(new Error('Invalid download packet.')); return; }
        this.binary.set(view.getUint32(0, true), new Uint8Array(view.buffer, view.byteOffset + 4, view.byteLength - 4).slice());
      });
      await this.response.startNotifications(); await this.data.startNotifications();
      const hello = await this.request({op:'hello'});
      if (!Number.isInteger(hello.chunk) || hello.chunk < 16 || hello.chunk > 176) throw new Error('Unsupported BLE packet size.');
      this.chunk = hello.chunk; return hello;
    } catch (error) { this.disconnect(); throw error; }
  }
  disconnect() { if (this.device?.gatt.connected) this.device.gatt.disconnect(); }
  reject(error) { if (this.pending) { const pending = this.pending; this.pending = null; clearTimeout(pending.timer); pending.reject(error); } }
  async exchange(write) {
    if (!this.connected) throw new Error('Connect the reader first.');
    if (this.pending) throw new Error('Wait for the current reader operation.');
    let waiting;
    const result = new Promise((resolve, reject) => {
      waiting = {resolve,reject}; waiting.timer = setTimeout(() => this.reject(new Error('Reader response timed out.')), this.timeoutMs);
      this.pending = waiting;
    });
    // Attach rejection handling immediately, including failures during GATT writes.
    result.catch(() => {});
    try { await write(); return await result; }
    catch (error) { if (this.pending === waiting) this.reject(error); throw error; }
  }
  async request(value) {
    const bytes = new TextEncoder().encode(JSON.stringify(value) + '\n');
    return this.exchange(async () => {
      for (let offset = 0; offset < bytes.length; offset += this.chunk + 4)
        await this.command.writeValueWithResponse(bytes.slice(offset, offset + this.chunk + 4));
    });
  }
  async auth(password) { return this.request({op:'auth',password}); }
  async list(dir = '/books', offset = 0) { return (await this.request({op:'list',dir,offset})).data; }
  async upload(file, progress = () => {}, cancel = {cancelled:false}) {
    if (!/\.epub$/i.test(file.name) || new TextEncoder().encode(file.name).length > 96 || file.size < 22)
      throw new Error('Choose an EPUB with a filename of at most 96 bytes.');
    const bytes = new Uint8Array(await file.arrayBuffer());
    const start = await this.request({op:'upload',name:file.name,size:file.size});
    const chunk = Math.min(this.chunk, start.chunk);
    if (!Number.isInteger(chunk) || chunk < 1) throw new Error('Invalid transfer packet size.');
    let offset = 0, crc = 0xffffffff;
    try {
      while (offset < bytes.length) {
        if (cancel.cancelled) throw new Error('Transfer canceled.');
        const part = bytes.slice(offset, offset + chunk), frame = packet(offset, part);
        let reply;
        for (let attempt = 0; ; ++attempt) {
          try { reply = await this.exchange(() => this.data.writeValueWithResponse(frame)); break; }
          catch (error) { if (attempt >= 1 || !this.connected || cancel.cancelled) throw error; }
        }
        if (reply.op !== 'chunk' || reply.offset !== offset + part.length) throw new Error('Unexpected upload acknowledgement.');
        crc = updateCrc(crc, part); offset += part.length; progress(offset, bytes.length);
      }
      if (cancel.cancelled) throw new Error('Transfer canceled.');
      return await this.request({op:'finish',crc:(crc ^ 0xffffffff) >>> 0});
    } catch (error) { if (this.connected) { try { await this.request({op:'cancel'}); } catch {} } throw error; }
  }
  async download(path, progress = () => {}, cancel = {cancelled:false}) {
    this.binary.clear();
    const metadata = await this.request({op:'download',path});
    if (!Number.isInteger(metadata.size) || metadata.size < 0 || metadata.size > 16 * 1048576) throw new Error('Unsupported file size.');
    const bytes = new Uint8Array(metadata.size); let offset = 0;
    try {
      while (offset < bytes.length) {
        if (cancel.cancelled) throw new Error('Transfer canceled.');
        const result = await this.request({op:'read',offset});
        const until = Date.now() + this.timeoutMs;
        while (!this.binary.has(offset) && Date.now() < until && this.connected) await new Promise(resolve => setTimeout(resolve, 10));
        const part = this.binary.get(offset); this.binary.delete(offset);
        if (!part || !part.length || part.length !== result.bytes || result.offset !== offset || part.length > bytes.length - offset)
          throw new Error('Missing or out-of-order download packet.');
        bytes.set(part, offset); offset += part.length; progress(offset, bytes.length);
        if (result.eof && offset !== bytes.length) throw new Error('Download ended early.');
      }
      await this.request({op:'cancel'}); return bytes;
    } catch (error) { if (this.connected) { try { await this.request({op:'cancel'}); } catch {} } throw error; }
  }
}

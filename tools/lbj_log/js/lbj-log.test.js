'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const L = require('../js/lbj-log.js');

// ---------------------------------------------------------------------------
// Builders — encode a record exactly the way App/app/lbj_log.c does, so the
// tests lock the wire format (and not just the reader).
// ---------------------------------------------------------------------------

function putAscii(rec, off, count, text) {
  for (let i = 0; i < count; i++) {
    rec[off + i] = i < text.length ? (text.charCodeAt(i) & 0xFF) : 0;
  }
}

function putBcd(rec, off, count, digits) {
  for (let i = 0; i < count; i++) {
    const nibble = i < digits.length ? Number(digits[i]) : 0x0F;
    const index = off + (i >> 1);
    if (i % 2 === 0) rec[index] = (rec[index] & 0x0F) | (nibble << 4);
    else rec[index] = (rec[index] & 0xF0) | nibble;
  }
}

function buildRecord(o = {}) {
  const rec = new Uint8Array(L.REC_SIZE).fill(0xFF);
  const view = new DataView(rec.buffer);

  const prefix = o.prefix ?? '';
  const train = o.train ?? '';
  const route = o.route ?? '';
  const loco = o.loco ?? '';
  const speed = o.speed ?? null;
  const km = o.km ?? null;
  const model = o.model ?? null;
  const distance = o.distance ?? null;
  const direction = o.direction ?? L.DIR_NONE;

  let flags = 0;
  if (prefix || train) flags |= L.F_TRN;
  if (speed !== null) flags |= L.F_SPD;
  if (km !== null) flags |= L.F_KM;
  if (model !== null) flags |= L.F_MDL;
  if (loco) flags |= L.F_LOC;
  if (route) flags |= L.F_RT;
  if (distance !== null && distance >= 0) flags |= L.F_DST;

  rec[0] = flags;
  putAscii(rec, 1, 2, prefix);
  putBcd(rec, 3, 6, train);
  view.setUint16(6, speed === null ? L.SPD_NONE : speed, true);
  view.setUint32(8, km === null ? L.KM_NONE : km, true);   // writes 4 bytes; [11] is overwritten below
  view.setUint16(11, model === null ? L.MDL_NONE : model, true);
  putBcd(rec, 13, 8, loco);
  putAscii(rec, 17, 4, route);
  view.setInt16(21, distance === null ? -1 : distance, true);
  rec[23] = direction;
  view.setUint16(24, o.seq ?? 0, true);
  view.setUint16(26, o.uptime ?? 0, true);
  rec[28] = 0xFF;
  rec[29] = 0xFF;
  rec[30] = L.crc8(rec.subarray(0, 30));
  rec[31] = L.COMMIT;
  return rec;
}

// ---------------------------------------------------------------------------

test('CRC8 matches the firmware vector (init 0x5A, poly 0x31)', () => {
  const data = new Uint8Array(30);
  for (let i = 0; i < 30; i++) data[i] = i;
  assert.equal(L.crc8(data), 0xa8);
  assert.equal(L.crc8(new Uint8Array(0)), 0x5a);
});

test('geometry matches the firmware zone and the Python exporter', () => {
  assert.equal(L.LOG_BASE, 0x018000);
  assert.equal(L.LOG_SIZE, 0x8000);
  assert.equal(L.REC_SIZE, 32);
  assert.equal(L.REC_COUNT, 1024);
  assert.deepEqual(L.COLUMNS, [
    'seq', 'uptime_s', 'train', 'direction', 'speed_kmh', 'km_post',
    'model', 'loco_number', 'route', 'distance_km'
  ]);
});

test('decodes a complete train record', () => {
  const row = L.parseRecord(buildRecord({
    prefix: 'K', train: '123456', speed: 120, km: 34567, model: 104,
    loco: '00123456', route: 'JGX', distance: 12, direction: L.DIR_DN,
    seq: 42, uptime: 777
  }));

  assert.deepEqual(row, {
    seq: 42,
    uptime_s: 777,
    train: 'K123456',
    direction: 'DN',
    speed_kmh: 120,
    km_post: '3456.7',
    model: 'DF4',
    loco_number: '00123456',
    route: 'JGX',
    distance_km: '1.2'
  });
});

test('direction maps to blank / DN / UP and keeps an unknown code visible', () => {
  assert.equal(L.parseRecord(buildRecord({ train: '1' })).direction, '');
  assert.equal(L.parseRecord(buildRecord({ train: '1', direction: L.DIR_DN })).direction, 'DN');
  assert.equal(L.parseRecord(buildRecord({ train: '1', direction: L.DIR_UP })).direction, 'UP');
  assert.equal(L.parseRecord(buildRecord({ train: '1', direction: 7 })).direction, '?7');
});

test('a missing flag hides the field even when the payload holds real data', () => {
  const rec = buildRecord({ train: '55', speed: 80, km: 12345, model: 104, loco: '99', distance: 5 });
  rec[0] &= ~(L.F_SPD | L.F_KM | L.F_MDL | L.F_LOC | L.F_DST);  // keep only F_TRN
  rec[30] = L.crc8(rec.subarray(0, 30));

  const row = L.parseRecord(rec);
  assert.equal(row.train, '55');
  assert.equal(row.speed_kmh, '');
  assert.equal(row.km_post, '');
  assert.equal(row.model, '');
  assert.equal(row.loco_number, '');
  assert.equal(row.distance_km, '');
});

test('unknown model code falls back to the decimal code, unknown distance to blank', () => {
  const row = L.parseRecord(buildRecord({ train: '9', model: 9999, distance: -1 }));
  assert.equal(row.model, '9999');
  assert.equal(row.distance_km, '');
});

test('a train number without a prefix is kept; a prefix without digits is kept', () => {
  assert.equal(L.parseRecord(buildRecord({ train: '123' })).train, '123');
  assert.equal(L.parseRecord(buildRecord({ prefix: 'T' })).train, 'T');
  assert.equal(L.parseRecord(buildRecord({})).train, '');
});

test('rejects a record whose CRC does not match', () => {
  const rec = buildRecord({ train: '123', speed: 50, km: 1000 });
  rec[6] ^= 0x01;                       // corrupt the payload
  assert.equal(L.parseRecord(rec), null);
  assert.deepEqual(L.parseRecord(rec, { checked: true }), { error: 'crc' });
});

test('rejects a record whose commit byte was never programmed', () => {
  const rec = buildRecord({ train: '123' });
  rec[31] = 0xFF;
  assert.equal(L.parseRecord(rec), null);
  assert.deepEqual(L.parseRecord(rec, { checked: true }), { error: 'uncommitted' });
});

test('rejects a record of the wrong length', () => {
  const rec = buildRecord({ train: '123' });
  assert.equal(L.parseRecord(rec.subarray(0, 31)), null);
});

test('scans the whole zone, skips blank and corrupt slots and sorts by sequence', () => {
  const image = new Uint8Array(L.LOG_SIZE).fill(0xFF);           // every slot blank
  image.set(buildRecord({ train: '300', seq: 30 }), 5 * L.REC_SIZE);

  const corrupt = buildRecord({ train: '200', seq: 20 });
  corrupt[6] ^= 0x02;
  image.set(corrupt, 2 * L.REC_SIZE);

  image.set(buildRecord({ train: '100', seq: 10 }), 900 * L.REC_SIZE);

  const log = L.parseLog(image);
  assert.equal(log.slots, 1024);
  assert.equal(log.valid, 2);
  assert.equal(log.badCrc, 1);
  assert.equal(log.unwritten, 1021);
  assert.deepEqual(log.rows.map(row => row.seq), [10, 30]);
  assert.deepEqual(log.rows.map(row => row.train), ['100', '300']);
});

test('rejects an image that is not the exact log zone size', () => {
  assert.throws(() => L.parseLog(new Uint8Array(L.LOG_SIZE - 1)), /Unexpected LBJ log size/);
  assert.throws(() => L.parseLog(null), /empty/);
});

test('writes a UTF-8 BOM, the documented header and CRLF rows, sorted by sequence', () => {
  const csv = L.rowsToCsv([
    L.parseRecord(buildRecord({ prefix: 'K', train: '123456', speed: 120, km: 34567, model: 104, loco: '00123456', route: 'JGX', distance: 12, direction: L.DIR_DN, seq: 2, uptime: 60 })),
    L.parseRecord(buildRecord({ train: '55', seq: 1, uptime: 5 }))
  ]);

  assert.equal(csv.charCodeAt(0), 0xfeff);
  const lines = csv.slice(1).split('\r\n');
  assert.equal(lines[0], 'seq,uptime_s,train,direction,speed_kmh,km_post,model,loco_number,route,distance_km');
  assert.equal(lines[1], '1,5,55,,,,,,,');
  assert.equal(lines[2], '2,60,K123456,DN,120,3456.7,DF4,00123456,JGX,1.2');
  assert.equal(csv.endsWith('\r\n'), true);
});

test('supports the Chinese header used by tools/lbj_log.py --chinese', () => {
  const csv = L.rowsToCsv([], { chinese: true });
  assert.equal(csv.slice(1).split('\r\n')[0],
    '序号,开机秒,车次,上行下行,速度,公里标,车型,车号,线路,距离');
});

test('quotes cells that would otherwise break the CSV', () => {
  assert.equal(L.rowsToCsv([{ seq: 1, train: 'K1,2' }]).includes('"K1,2"'), true);
  assert.equal(L.rowsToCsv([{ seq: 1, route: '"X"' }]).includes('"""X"""'), true);
});

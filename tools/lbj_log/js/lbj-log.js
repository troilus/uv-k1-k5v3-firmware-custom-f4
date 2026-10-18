// LBJ train-log protocol and CSV helpers shared by the Web Serial UI and tests.
// Pure module (no DOM): mirrors tools/lbj_log.py byte for byte so both exporters
// produce the same CSV for the same 32 KiB flash image.
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.UVTOOLS_LBJ_LOG = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  // ---------------------------------------------------------------------------
  // Zone and record geometry — keep in sync with tools/lbj_log.py and
  // App/app/lbj_log.c (ENABLE_FEAT_F4HWN_LBJ).
  // ---------------------------------------------------------------------------

  const LOG_BASE = 0x018000;        // first sector of the log zone
  const LOG_SECTORS = 8;            // 8 × 4 KiB
  const SECTOR_SIZE = 0x1000;
  const LOG_SIZE = LOG_SECTORS * SECTOR_SIZE;   // 32 KiB
  const REC_SIZE = 32;
  const REC_COUNT = LOG_SIZE / REC_SIZE;        // 1024

  const COMMIT = 0xA5;              // programmed last; only then is a record valid

  const F_TRN = 0x01;
  const F_SPD = 0x02;
  const F_KM = 0x04;
  const F_MDL = 0x08;
  const F_LOC = 0x10;
  const F_RT = 0x20;
  const F_DST = 0x40;

  const DIR_NONE = 0;
  const DIR_DN = 1;
  const DIR_UP = 2;

  const SPD_NONE = 0xFFFF;
  const KM_NONE = 0xFFFFFF;
  const MDL_NONE = 0xFFFF;

  const EMPTY_BYTE = 0xFF;

  // App/app/lbj_dual.c type_name(), copied verbatim from tools/lbj_log.py.
  const MODEL = {
    1: 'JF', 3: 'QJ', 5: 'JS', 6: 'KD7', 55: 'LJK', 81: 'DF21',
    101: 'DF', 102: 'DF2', 103: 'DF3', 104: 'DF4', 105: 'DF4K', 106: 'DF4C',
    107: 'DF5', 108: 'DF5W', 109: 'DF6', 110: 'DF7', 111: 'DF8', 112: 'DF9',
    113: 'DF10', 114: 'DFH1', 115: 'DFH2', 116: 'DFH3', 117: 'DFH5', 118: 'BJ',
    119: 'BJW', 120: 'ND2', 121: 'ND3', 122: 'ND4', 123: 'ND5', 124: 'NY5',
    125: 'NY6', 126: 'NY7', 127: 'QY', 128: 'DFH21', 129: 'DF7B', 130: 'DF5S',
    131: 'DF7C', 132: 'DF7S', 133: 'GK1', 134: 'GK1F', 135: 'DF4E', 136: 'DF7D',
    137: 'GK1A', 138: 'DF11', 139: 'TA', 140: 'DF10F', 141: 'DF4D', 142: 'DF8B',
    143: 'DF12', 144: 'DF7E', 145: 'NYJ1', 146: 'NZJ1', 147: 'NZJ2', 148: 'DF4DJ',
    149: 'XSG', 150: 'SZ', 151: 'NJ2', 152: 'DF7G', 153: 'NDJ3', 156: 'DF11Z',
    157: 'FXN3D', 158: 'DF11G', 160: 'HXN3', 161: 'HXN5', 162: 'HXN3B',
    163: 'HXN5B', 167: 'FXN3B', 169: 'FXN3C', 170: 'FXN5C', 171: 'FXN3-J',
    201: '8G', 202: '8K', 203: '6G', 204: '6K', 205: 'SS1', 206: 'SS3',
    207: 'SS4', 208: 'SS5', 209: 'SS6', 210: 'SS3B', 211: 'SS7', 212: 'SS8',
    213: 'SS7B', 214: 'SS7C', 215: 'SS6B', 216: 'SS9', 217: 'SS7D', 218: 'DJXM',
    219: 'DJ1', 220: 'DJ2', 221: 'DJF', 222: 'LJD', 223: 'XFH', 224: 'SS7E',
    225: 'SS4G', 226: 'SS3C', 228: 'TS', 229: 'DJ4HX', 230: 'KTT', 231: 'HXD1',
    232: 'HXD2', 233: 'HXD3', 234: 'HXD1B', 235: 'HXD2B', 236: 'HXD3B',
    237: 'HXD1C', 238: 'HXD2C', 239: 'HXD3C', 240: 'HXD1D', 241: 'HXD2D',
    242: 'HXD3D', 243: 'FXD1B', 244: 'FXD2B', 245: 'FXD1', 246: 'FXD3',
    247: 'FXD1-J', 248: 'FXD3-J', 249: 'KZ25TA', 251: 'KZ25TB', 252: 'HXD1D-J',
    254: 'FXD1H', 300: 'XYSZ', 301: 'CRH1', 302: 'CRH2', 303: 'CRH3',
    305: 'CRH5', 306: 'CRH380A', 307: 'CRH380B', 308: 'CRH380C', 309: 'CRH380D',
    310: 'CRH6A', 311: 'CR400AF', 312: 'CR400BF', 313: 'CR300AF', 314: 'CR300BF',
    315: 'CRH2E', 316: 'CRH6F', 330: 'CJ1', 331: 'CJ2', 332: 'CJ3', 333: 'CJ4',
    334: 'CJ5', 335: 'CJ6', 400: 'GCD-1000J', 403: 'GX-160',
    3073: 'CRH380BL', 3075: 'CRH380BG', 3111: 'CR400AF-Z', 3112: 'CR400AF-S',
    3125: 'CR400BF-G'
  };

  // CSV layout, identical to tools/lbj_log.py (COLUMNS / HEAD_CN).
  const COLUMNS = [
    'seq', 'uptime_s', 'train', 'direction', 'speed_kmh', 'km_post',
    'model', 'loco_number', 'route', 'distance_km'
  ];

  const HEAD_EN = {
    seq: 'seq', uptime_s: 'uptime_s', train: 'train', direction: 'direction',
    speed_kmh: 'speed_kmh', km_post: 'km_post', model: 'model',
    loco_number: 'loco_number', route: 'route', distance_km: 'distance_km'
  };

  const HEAD_CN = {
    seq: '序号', uptime_s: '开机秒', train: '车次', direction: '上行下行',
    speed_kmh: '速度', km_post: '公里标', model: '车型',
    loco_number: '车号', route: '线路', distance_km: '距离'
  };

  // ---------------------------------------------------------------------------
  // Decoding
  // ---------------------------------------------------------------------------

  // Initial value 0x5A, polynomial 0x31 (MSB first) — App/app/lbj_log.c.
  function crc8(bytes) {
    let crc = 0x5A;
    for (let i = 0; i < bytes.length; i++) {
      crc ^= bytes[i] & 0xFF;
      for (let bit = 0; bit < 8; bit++) {
        crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) & 0xFF : (crc << 1) & 0xFF;
      }
    }
    return crc;
  }

  // Packed BCD nibbles; a nibble of 0xF means "missing". Returns null entries.
  function bcdDigits(bytes, count) {
    const out = [];
    for (let i = 0; i < count; i++) {
      const nibble = (i % 2 === 0) ? (bytes[i >> 1] >> 4) : (bytes[i >> 1] & 0x0F);
      out.push(nibble === 0x0F ? null : nibble);
    }
    return out;
  }

  // bytes -> ASCII, dropping NUL/0xFF padding (Python: decode('ascii','replace')).
  function asciiField(bytes) {
    let text = '';
    for (let i = 0; i < bytes.length; i++) {
      const c = bytes[i];
      if (c === 0 || c === EMPTY_BYTE) continue;
      text += c < 0x80 ? String.fromCharCode(c) : '\uFFFD';
    }
    return text;
  }

  function isBlankSlot(rec) {
    for (let i = 0; i < rec.length; i++) {
      if (rec[i] !== EMPTY_BYTE) return false;
    }
    return true;
  }

  function readU16(view, rec, off) {
    return view.getUint16(off, true);
  }

  // One 32-byte record -> row object, or null when the slot is not a committed,
  // checksum-valid record. Mirrors tools/lbj_log.py parse_record().
  function parseRecord(rec, options) {
    const settings = Object.assign({ checked: false }, options);
    if (rec.length !== REC_SIZE) return null;

    if (rec[31] !== COMMIT) {
      if (settings.checked) return { error: 'uncommitted' };
      return null;
    }
    if (rec[30] !== crc8(rec.subarray(0, 30))) {
      if (settings.checked) return { error: 'crc' };
      return null;
    }

    const view = new DataView(rec.buffer, rec.byteOffset, rec.byteLength);
    const flags = rec[0];

    const pref = asciiField(rec.subarray(1, 3));
    const trn = bcdDigits(rec.subarray(3, 6), 6)
      .filter(digit => digit !== null).join('');
    const loc = bcdDigits(rec.subarray(13, 17), 8)
      .filter(digit => digit !== null).join('');
    const rt = asciiField(rec.subarray(17, 21));

    const spd = readU16(view, rec, 6);
    const km = rec[8] | (rec[9] << 8) | (rec[10] << 16);
    const mdl = readU16(view, rec, 11);
    const dst = view.getInt16(21, true);
    const dr = rec[23];
    const seq = readU16(view, rec, 24);
    const uptime = readU16(view, rec, 26);

    const train = (trn || pref) ? (pref + trn) : '';
    const direction = dr === DIR_NONE ? ''
      : dr === DIR_DN ? 'DN'
        : dr === DIR_UP ? 'UP' : `?${dr}`;

    return {
      seq,
      uptime_s: uptime,
      train,
      direction,
      speed_kmh: (!(flags & F_SPD) || spd === SPD_NONE) ? '' : spd,
      km_post: (!(flags & F_KM) || km === KM_NONE) ? '' : (km / 10).toFixed(1),
      model: (!(flags & F_MDL) || mdl === MDL_NONE) ? ''
        : (MODEL[mdl] !== undefined ? MODEL[mdl] : String(mdl)),
      loco_number: (flags & F_LOC) ? loc : '',
      route: (flags & F_RT) ? rt : '',
      distance_km: ((flags & F_DST) && dst >= 0) ? (dst / 10).toFixed(1) : ''
    };
  }

  // Whole 32 KiB zone -> { rows, slots, valid, unwritten, badCrc }.
  // rows are sorted by sequence, like tools/lbj_log.py.
  function parseLog(image) {
    if (!image) throw new Error('LBJ log image is empty');
    if (image.length !== LOG_SIZE) {
      throw new Error(`Unexpected LBJ log size ${image.length} (expected ${LOG_SIZE})`);
    }

    const rows = [];
    let unwritten = 0;
    let badCrc = 0;

    for (let off = 0; off < LOG_SIZE; off += REC_SIZE) {
      const rec = image.subarray(off, off + REC_SIZE);
      if (rec[31] !== COMMIT) {
        unwritten++;
        continue;
      }
      const parsed = parseRecord(rec);
      if (parsed === null) {
        badCrc++;
        continue;
      }
      rows.push(parsed);
    }

    rows.sort((a, b) => a.seq - b.seq);

    return {
      rows,
      slots: REC_COUNT,
      valid: rows.length,
      unwritten,
      badCrc
    };
  }

  // ---------------------------------------------------------------------------
  // CSV
  // ---------------------------------------------------------------------------

  function csvCell(value) {
    const text = String(value === null || value === undefined ? '' : value);
    // Guard against spreadsheet formula injection, like UV Studio's rf-log.js.
    const safe = /^[=+\-@\t\r]/.test(text) ? `'${text}` : text;
    return /[",\r\n]/.test(safe) ? `"${safe.replace(/"/g, '""')}"` : safe;
  }

  // Rows -> CSV text. UTF-8 BOM + CRLF, matching tools/lbj_log.py
  // (open(..., encoding='utf-8-sig')) and UV Studio's rf-log.js.
  function rowsToCsv(rows, options) {
    const settings = Object.assign({ chinese: false }, options);
    const head = settings.chinese ? HEAD_CN : HEAD_EN;
    const lines = [COLUMNS.map(column => csvCell(head[column])).join(',')];

    Array.from(rows)
      .slice()
      .sort((a, b) => a.seq - b.seq)
      .forEach(row => {
        lines.push(COLUMNS.map(column => csvCell(row[column])).join(','));
      });

    return '\uFEFF' + lines.join('\r\n') + '\r\n';
  }

  function isBlankRecord(rec) {
    return isBlankSlot(rec);
  }

  return {
    LOG_BASE,
    LOG_SECTORS,
    SECTOR_SIZE,
    LOG_SIZE,
    REC_SIZE,
    REC_COUNT,
    COMMIT,
    F_TRN, F_SPD, F_KM, F_MDL, F_LOC, F_RT, F_DST,
    DIR_NONE, DIR_DN, DIR_UP,
    SPD_NONE, KM_NONE, MDL_NONE,
    MODEL,
    COLUMNS,
    HEAD_EN,
    HEAD_CN,
    crc8,
    parseRecord,
    parseLog,
    rowsToCsv,
    isBlankRecord
  };
});

// ---------------------------------------------------------------------------
// Optional Node CLI, mirroring tools/lbj_log.py's output:
//   node js/lbj-log.js dump.bin            # print a table
//   node js/lbj-log.js dump.bin -o lbj.csv # write CSV (use --chinese for CN headers)
// Handy for diffing this module against the Python exporter on one image.
// ---------------------------------------------------------------------------
if (typeof module === 'object' && module.exports && require.main === module) {
  const fs = require('node:fs');
  const api = module.exports;
  const argv = process.argv.slice(2);

  const input = argv.find(arg => !arg.startsWith('-'));
  const outIndex = argv.findIndex(arg => arg === '-o' || arg === '--out');
  const output = outIndex >= 0 ? argv[outIndex + 1] : null;
  const chinese = argv.includes('--chinese');

  if (!input) {
    console.error('usage: node js/lbj-log.js <dump.bin> [-o out.csv] [--chinese]');
    process.exit(2);
  }

  const image = new Uint8Array(fs.readFileSync(input));
  const log = api.parseLog(image);

  const width = (rows) => rows[0].map((_, column) =>
    Math.max(...rows.map(row => String(row[column]).length)));
  const head = ['seq', 'sec', 'train', 'dir', 'km/h', 'km', 'model', 'loco', 'route', 'dist'];
  const table = [head].concat(log.rows.map(row => api.COLUMNS.map(column => row[column])));

  console.log(`zone      : 0x${api.LOG_BASE.toString(16).toUpperCase()}..` +
    `0x${(api.LOG_BASE + api.LOG_SIZE - 1).toString(16).toUpperCase()}` +
    ` (${log.slots} slots)`);
  console.log(`records   : ${log.valid} valid, ${log.unwritten} blank, ${log.badCrc} bad CRC`);

  if (output) {
    fs.writeFileSync(output, api.rowsToCsv(log.rows, { chinese }));
    console.log(`CSV       : ${output}`);
  } else {
    const widths = width(table.map(row => row.map(cell => String(cell))));
    table.forEach((row, index) => {
      console.log(row.map((cell, column) => String(cell).padEnd(widths[column])).join('  '));
      if (index === 0) console.log(widths.map(value => '-'.repeat(value)).join('  '));
    });
  }
}

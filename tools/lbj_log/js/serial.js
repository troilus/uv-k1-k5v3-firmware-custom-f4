/* LBJ Log serial layer — the UV Studio protocol stack, verbatim where possible.
 *
 * Framing, obfuscation, CRC16, the 0x0514 device-info handshake and the
 * 0x0738/0x0739 raw physical-flash read are adapted from UV Studio's
 * js/flash.js (armel/armel.github.io @ v2.0.0, flash.js?v=84) so this page talks
 * to the radio exactly the way UV Studio does. Only the LBJ zone walk is new.
 *
 * Wire format: AB CD | size(u16 LE) | payload | CRC16/XMODEM(u16 LE) | DC BA,
 * XOR-obfuscated with OBFUS_TBL from byte 4 on. Replies carry no CRC; the two
 * trailing bytes are a fixed obfuscated tail.
 */
;(function () {
    "use strict";

    const BAUDRATE = 38400;
    const BUFFER_SIZE = 65536;

    // Message types (App/app/uart.c, ENABLE_FEAT_F4HWN_EXT_FLASH_RW).
    const MSG_DEV_INFO_REQ = 0x0514;
    const MSG_DEV_INFO_RESP = 0x0515;
    const MSG_READ_FLASH = 0x0738;
    const MSG_READ_FLASH_RESP = 0x0739;

    const OBFUS_TBL = new Uint8Array([
        0x16, 0x6c, 0x14, 0xe6, 0x2e, 0x91, 0x0d, 0x40,
        0x21, 0x35, 0xd5, 0x40, 0x13, 0x03, 0xe9, 0x80
    ]);

    // LBJ log zone: 8 × 4 KiB of external flash, read by physical address.
    const LOG_BASE = 0x018000;
    const LOG_SIZE = 0x8000;
    // The firmware 0x0739 reply payload is capped at 128 bytes (PY25Q16_RAW_CHUNK_SIZE).
    const READ_CHUNK = 128;
    const COMMAND_TIMEOUT_MS = 2000;
    const COMMAND_RETRIES = 3;
    const DEVICE_INFO_TIMEOUT_MS = 5000;
    const READ_BUFFER_LIMIT = 1 << 20;

    const controller = window.UVStudioSerial;
    /* UV Studio's ownership controller hands the page a client handle; every
       acquire / release / operation call goes through it (see js/flash.js). */
    const client = controller
        ? controller.register("tools", { disconnect: disconnectPort })
        : null;

    let port = null;
    let reader = null;
    let writer = null;
    let readBuffer = [];
    let isReading = false;
    let serialReadRevision = 0;
    let session = 0;
    const serialReadWaiters = new Set();

    function emitLog(message, type) {
        window.dispatchEvent(new CustomEvent("lbj:serial-log", {
            detail: { message, type: type || "" }
        }));
    }

    function isSupported() {
        return typeof navigator !== "undefined" && Boolean(navigator.serial);
    }

    // ---- Serial read bookkeeping (so commands can await new bytes) ----------

    function notifySerialRead() {
        serialReadRevision++;
        const waiters = Array.from(serialReadWaiters);
        serialReadWaiters.clear();
        for (const wake of waiters) wake();
    }

    function waitForSerialRead(afterRevision, timeoutMs) {
        if (serialReadRevision !== afterRevision) return Promise.resolve(true);
        return new Promise(resolve => {
            let timer = null;
            const wake = () => {
                clearTimeout(timer);
                resolve(true);
            };
            serialReadWaiters.add(wake);
            timer = setTimeout(() => {
                serialReadWaiters.delete(wake);
                resolve(false);
            }, timeoutMs);
        });
    }

    // ---- Framing -----------------------------------------------------------

    function createMessage(msgType, dataLen) {
        const msg = new Uint8Array(4 + dataLen);
        const view = new DataView(msg.buffer);
        view.setUint16(0, msgType, true);
        view.setUint16(2, dataLen, true);
        return msg;
    }

    function obfuscate(buf, off, size) {
        for (let i = 0; i < size; i++) buf[off + i] ^= OBFUS_TBL[i % OBFUS_TBL.length];
    }

    function calcCRC(buf, off, size) {
        let crc = 0;
        for (let i = 0; i < size; i++) {
            crc ^= (buf[off + i] & 0xff) << 8;
            for (let bit = 0; bit < 8; bit++) {
                crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
            }
        }
        return crc;
    }

    function makePacket(msg) {
        let msgLen = msg.length;
        if (msgLen % 2 !== 0) msgLen++;
        const buf = new Uint8Array(8 + msgLen);
        const view = new DataView(buf.buffer);

        view.setUint16(0, 0xCDAB, true);            // wire: AB CD
        view.setUint16(2, msgLen, true);
        view.setUint16(6 + msgLen, 0xBADC, true);   // wire: DC BA

        for (let i = 0; i < msg.length; i++) buf[4 + i] = msg[i];
        view.setUint16(4 + msgLen, calcCRC(buf, 4, msgLen), true);

        obfuscate(buf, 4, 2 + msgLen);
        return buf;
    }

    function fetchMessage(buf) {
        if (buf.length < 8) return null;

        let packBegin = -1;
        for (let i = 0; i < buf.length - 1; i++) {
            if (buf[i] === 0xab && buf[i + 1] === 0xcd) {
                packBegin = i;
                break;
            }
        }
        if (packBegin === -1) {
            if (buf.length > 0 && buf[buf.length - 1] === 0xab) buf.splice(0, buf.length - 1);
            else buf.length = 0;
            return null;
        }
        if (buf.length - packBegin < 8) return null;

        const msgLen = (buf[packBegin + 3] << 8) | buf[packBegin + 2];
        const packEnd = packBegin + 6 + msgLen;
        if (buf.length < packEnd + 2) return null;

        if (buf[packEnd] !== 0xdc || buf[packEnd + 1] !== 0xba) {
            buf.splice(0, packBegin + 2);
            return null;
        }

        const msgBuf = new Uint8Array(msgLen + 2);
        for (let i = 0; i < msgLen + 2; i++) msgBuf[i] = buf[packBegin + 4 + i];
        obfuscate(msgBuf, 0, msgLen + 2);

        const view = new DataView(msgBuf.buffer);
        const msgType = view.getUint16(0, true);
        const data = msgBuf.slice(4);

        buf.splice(0, packEnd + 2);
        return { msgType, data, rawData: msgBuf };
    }

    async function sendMessage(msg) {
        const packet = makePacket(msg);
        await writer.write(packet);
    }

    // ---- Port lifecycle ----------------------------------------------------

    function startReading() {
        if (!reader || isReading) return;
        isReading = true;
        readLoop().catch(error => {
            if (isReading) emitLog(error?.message ?? String(error), "error");
        });
    }

    async function readLoop() {
        let unexpectedlyClosed = false;
        try {
            while (isReading && reader) {
                const { value, done } = await reader.read();
                if (done) {
                    unexpectedlyClosed = isReading;
                    break;
                }
                if (value?.length) {
                    readBuffer.push(...value);
                    if (readBuffer.length > READ_BUFFER_LIMIT) {
                        readBuffer.splice(0, readBuffer.length - READ_BUFFER_LIMIT);
                    }
                    notifySerialRead();
                }
            }
        } catch (error) {
            if (isReading) unexpectedlyClosed = true;
            emitLog(error?.message ?? String(error), "error");
        }
        if (unexpectedlyClosed) void disconnectPort({ reason: "hardware-disconnect" });
    }

    async function connect() {
        if (port) return;
        if (!client) {
            throw new Error("The UV Studio serial controller is not loaded.");
        }
        if (!isSupported()) {
            throw new Error("This browser has no Web Serial API — use Chrome or Edge.");
        }

        await client.acquire({ source: "tools" });
        if (!client.isOwner()) {
            throw new Error("The serial port is busy.");
        }

        emitLog("Requesting serial port…", "info");
        port = await navigator.serial.requestPort();
        emitLog("Opening port…", "info");
        await port.open({ baudRate: BAUDRATE, bufferSize: BUFFER_SIZE });

        reader = port.readable.getReader();
        writer = port.writable.getWriter();
        session++;
        readBuffer = [];

        startReading();
        await new Promise(resolve => setTimeout(resolve, 500));
        client.setState("connected", { reason: "connection-established" });
        emitLog("Connected.", "success");
    }

    async function disconnectPort(context) {
        const hardwareDisconnect = context?.reason === "hardware-disconnect";
        isReading = false;
        const activeReader = reader;
        const activeWriter = writer;
        const activePort = port;
        reader = null;
        writer = null;
        port = null;
        if (activeReader || activeWriter || activePort) session++;
        notifySerialRead();

        await controller.closeResources({
            reader: activeReader,
            writer: activeWriter,
            port: activePort
        });
        if (!hardwareDisconnect) emitLog("Disconnected.", "info");
    }

    function disconnect() {
        return client.release("operation-complete");
    }

    function assertSession(expected) {
        if (expected !== session) {
            throw Object.assign(new Error("The serial connection changed."), {
                code: "UVSTUDIO_SERIAL_SESSION_CHANGED"
            });
        }
    }

    // ---- Commands ----------------------------------------------------------

    // 0x0514: hand the firmware a session timestamp; it is echoed by every
    // external-flash command. Also yields the firmware name for the UI.
    async function requestDeviceInfo() {
        if (!port) throw new Error("Not connected.");
        const timestamp = Date.now() & 0xffffffff;

        for (let attempt = 0; attempt <= COMMAND_RETRIES; attempt++) {
            assertSession(session);
            readBuffer = [];
            const msg = createMessage(MSG_DEV_INFO_REQ, 4);
            new DataView(msg.buffer).setUint32(4, timestamp, true);
            emitLog(`TX 0x0514 timestamp=${timestamp}`, "info");
            await sendMessage(msg);

            const deadline = performance.now() + DEVICE_INFO_TIMEOUT_MS;
            let revision = serialReadRevision;
            for (;;) {
                const response = fetchMessage(readBuffer);
                if (response && response.msgType === MSG_DEV_INFO_RESP) {
                    let name = "";
                    for (let i = 0; i < response.data.length; i++) {
                        const c = response.data[i];
                        if (c === 0x00 || c === 0xFF) break;
                        if (c >= 32 && c < 127) name += String.fromCharCode(c);
                    }
                    emitLog(`Connected to ${name || "radio"}`, "success");
                    return { timestamp, name };
                }
                const remaining = deadline - performance.now();
                if (remaining <= 0) break;
                const received = await waitForSerialRead(revision, remaining);
                revision = serialReadRevision;
                if (!received) break;
            }
        }
        throw new Error("The radio did not answer 0x0514 (external flash access unavailable?).");
    }

    async function waitForExternalFlashResponse(responseType, address, timeoutMs, activeSession) {
        let revision = serialReadRevision;
        const deadline = performance.now() + timeoutMs;

        for (;;) {
            assertSession(activeSession);

            for (;;) {
                const buffered = readBuffer.length;
                const response = fetchMessage(readBuffer);
                if (response && response.msgType === responseType && response.data.length >= 4) {
                    const view = new DataView(
                        response.data.buffer, response.data.byteOffset, response.data.byteLength);
                    if (view.getUint32(0, true) === address) return response;
                }
                if (response === null && readBuffer.length === buffered) break;
            }

            const remaining = deadline - performance.now();
            if (remaining <= 0) return null;
            const received = await waitForSerialRead(revision, remaining);
            revision = serialReadRevision;
            if (!received) return null;
        }
    }

    async function exchangeExternalFlashMessage(
        msg, responseType, address, timeoutMs, retries = COMMAND_RETRIES
    ) {
        const activeSession = session;
        for (let attempt = 0; attempt <= retries; attempt++) {
            assertSession(activeSession);
            readBuffer = [];
            await sendMessage(msg);
            const response = await waitForExternalFlashResponse(
                responseType, address, timeoutMs, activeSession);
            if (response) return response;
            if (attempt < retries) {
                emitLog(`Retry ${attempt + 1} @ 0x${address.toString(16).toUpperCase()}`, "info");
            }
        }
        return null;
    }

    // 0x0738: read `length` bytes of external flash at a true physical address.
    async function readExternalFlashChunk(address, length, timestamp) {
        const msg = createMessage(MSG_READ_FLASH, 10);
        const view = new DataView(msg.buffer);
        view.setUint32(4, address, true);
        view.setUint16(8, length, true);
        view.setUint32(10, timestamp, true);

        const response = await exchangeExternalFlashMessage(
            msg, MSG_READ_FLASH_RESP, address, COMMAND_TIMEOUT_MS
        );
        if (!response || response.data.length < 7) {
            throw new Error(`Read failed at 0x${address.toString(16).toUpperCase()}`);
        }
        const dataView = new DataView(
            response.data.buffer, response.data.byteOffset, response.data.byteLength);
        const responseSize = dataView.getUint16(4, true);
        if (response.data[6] !== 0 || responseSize !== length ||
            response.data.length < 7 + length) {
            throw new Error(`Read failed at 0x${address.toString(16).toUpperCase()}`);
        }
        return response.data.slice(7, 7 + length);
    }

    // A firmware without external-flash access never answers 0x0738.
    async function hasExternalFlashSupport(timestamp) {
        try {
            await readExternalFlashChunk(LOG_BASE, 1, timestamp);
            return true;
        } catch (error) {
            if (error?.code === "UVSTUDIO_SERIAL_SESSION_CHANGED" || !port) throw error;
            return false;
        }
    }

    // ---- The LBJ zone walk -------------------------------------------------

    async function readLog(timestamp, onProgress) {
        const image = new Uint8Array(LOG_SIZE);
        for (let offset = 0; offset < LOG_SIZE; offset += READ_CHUNK) {
            const length = Math.min(READ_CHUNK, LOG_SIZE - offset);
            image.set(await readExternalFlashChunk(LOG_BASE + offset, length, timestamp), offset);
            if (onProgress) onProgress((offset + length) / LOG_SIZE);
        }
        return image;
    }

    window.LBJSerial = {
        BAUDRATE,
        LOG_BASE,
        LOG_SIZE,
        READ_CHUNK,
        MSG_DEV_INFO_REQ,
        MSG_DEV_INFO_RESP,
        MSG_READ_FLASH,
        MSG_READ_FLASH_RESP,
        isSupported,
        connect,
        disconnect,
        isConnected: () => Boolean(port),
        requestDeviceInfo,
        hasExternalFlashSupport,
        readExternalFlashChunk,
        readLog,
        // Operation guard from the UV Studio serial controller, so the status
        // pill can show "Reading LBJ Log…" and block re-entry.
        beginOperation: (name, options) => client.beginOperation(name, options),
        endOperation: token => client.endOperation(token),
        getSnapshot: () => controller.getSnapshot(),
        // Exposed for tests/debugging.
        createMessage,
        makePacket,
        fetchMessage,
        calcCRC,
        obfuscate
    };
})();

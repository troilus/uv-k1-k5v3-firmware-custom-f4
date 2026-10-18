/* LBJ Log page wiring.
 *
 * The shell behaviour (serial status pill, global disconnect, console,
 * progress) follows UV Studio's js/studio.js (armel/armel.github.io @ v2.0.0,
 * studio.js?v=19) and the read flow follows js/flash.js (flash.js?v=84);
 * the table and export are new.
 */
;(function () {
    "use strict";

    const L = window.UVTOOLS_LBJ_LOG;
    const serial = window.LBJSerial;
    const controller = window.UVStudioSerial;
    const preferences = window.UVStudioPreferences;
    const VERSION = "0.15";

    const statusEl = document.getElementById("studioSerialStatus");
    const statusText = document.getElementById("studioSerialText");
    const statusDisconnect = document.getElementById("studioSerialDisconnect");
    const versionLabel = document.getElementById("studioVersion");
    const readBtn = document.getElementById("lbjReadBtn");
    const resultsEl = document.getElementById("lbjResults");
    const metaEl = document.getElementById("lbjMeta");
    const rowsEl = document.getElementById("lbjRows");
    const emptyEl = document.getElementById("lbjEmpty");
    const filterEl = document.getElementById("lbjFilter");
    const csvDownload = document.getElementById("lbjCsvDownload");
    const csvLink = document.getElementById("lbjCsvLink");
    const binDownload = document.getElementById("lbjBinDownload");
    const binLink = document.getElementById("lbjBinLink");
    const progressContainer = document.getElementById("progressContainer");
    const progressFill = document.getElementById("progressFill");
    const progressLabel = document.getElementById("progressLabel");
    const logDiv = document.getElementById("log");
    const logToggle = document.getElementById("logToggle");
    const hamburgerBtn = document.getElementById("hamburgerMenu");
    const themeToggle = document.getElementById("themeToggle");

    const operationTranslationKeys = {
        "read-lbj-log": "studio_operation_read_lbj_log"
    };

    let currentOperation = null;
    let parsedRows = [];
    let csvUrl = null;
    let binUrl = null;

    function t(key, ...values) {
        return window.uvStudioI18n ? window.uvStudioI18n.t(key, ...values) : key;
    }

    function escapeHtml(text) {
        return String(text ?? "").replace(/[&<>"']/g, character => ({
            "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
        })[character]);
    }

    // ---- Console / progress (UV Studio UI helpers) --------------------------

    function log(message, type = "") {
        const entry = document.createElement("div");
        entry.className = `log-entry ${type}`;
        entry.textContent = `[${new Date().toLocaleTimeString()}] ${message}`;
        if (logDiv) {
            logDiv.appendChild(entry);
            logDiv.scrollTop = logDiv.scrollHeight;
        }
    }

    function updateProgress(percent) {
        const rounded = Math.round(percent);
        const label = `${rounded}%`;
        if (progressFill) progressFill.style.width = label;
        if (progressLabel) progressLabel.textContent = label;
        const bar = document.querySelector(".progress-bar");
        if (bar) bar.setAttribute("aria-valuenow", String(rounded));
    }

    function paint() {
        if (document.visibilityState !== "visible") return Promise.resolve();
        return new Promise(resolve => requestAnimationFrame(() => resolve()));
    }

    function setProgressVisible(visible) {
        if (progressContainer) progressContainer.style.display = visible ? "block" : "none";
        if (!visible) updateProgress(0);
    }

    function refreshLogToggle() {
        if (logToggle && logDiv) {
            logToggle.textContent = logDiv.classList.contains("visible") ? t("logHide") : t("logShow");
        }
    }

    if (logToggle && logDiv) {
        logToggle.addEventListener("click", () => {
            const visible = logDiv.classList.toggle("visible");
            logToggle.setAttribute("aria-expanded", visible ? "true" : "false");
            refreshLogToggle();
        });
    }

    window.addEventListener("lbj:serial-log", event => {
        log(event.detail?.message ?? "", event.detail?.type ?? "");
    });

    // ---- Serial status pill (UV Studio studio.js) ---------------------------

    let lastSerialSnapshot = controller
        ? controller.getSnapshot()
        : { owner: null, state: "disconnected", operation: null };

    function renderSerialStatus(snapshot) {
        if (!statusEl || !statusText) return;
        lastSerialSnapshot = snapshot || lastSerialSnapshot;
        const operation = lastSerialSnapshot.operation;
        let key;

        if (operation) {
            key = operationTranslationKeys[operation.name] || "studio_operation_active";
        } else if (lastSerialSnapshot.state === "connected") {
            key = "studio_serial_connected";
        } else {
            key = `studio_serial_${lastSerialSnapshot.state || "disconnected"}`;
        }

        const label = t(key);
        statusEl.dataset.state = lastSerialSnapshot.state || "disconnected";
        statusEl.dataset.operation = String(Boolean(operation));
        statusEl.dataset.critical = String(Boolean(operation?.critical));
        statusText.textContent = label;
        statusEl.setAttribute("aria-label", label);
        statusEl.title = label;

        if (statusDisconnect) {
            const held = lastSerialSnapshot.state === "connected" ||
                lastSerialSnapshot.state === "reconnecting";
            statusDisconnect.hidden = !held || Boolean(operation);
        }
    }

    window.addEventListener("uvstudio:serialstatechange", event => {
        renderSerialStatus(event.detail);
        updateReadButton();
    });
    window.addEventListener("uvstudio:languagechange", () => {
        renderSerialStatus(lastSerialSnapshot);
        refreshLogToggle();
    });

    function beginOperation(name) {
        if (currentOperation) return null;
        const token = serial.beginOperation(name, { critical: false });
        if (!token) return null;
        currentOperation = token;
        updateReadButton();
        return token;
    }

    function endOperation(token) {
        if (!token || token !== currentOperation) return;
        serial.endOperation(token);
        currentOperation = null;
        updateReadButton();
    }

    window.addEventListener("beforeunload", event => {
        if (!currentOperation) return;
        event.preventDefault();
        event.returnValue = "";
    });

    if (statusDisconnect && controller) {
        statusDisconnect.addEventListener("click", async () => {
            statusDisconnect.disabled = true;
            try {
                await controller.releaseCurrent("user");
            } catch (error) {
                console.warn("Serial disconnect failed:", error);
            } finally {
                statusDisconnect.disabled = false;
                updateReadButton();
            }
        });
    }

    // ---- Theme / sidebar (UV Studio shell) ----------------------------------

    function applyTheme() {
        const dark = preferences ? preferences.get("isDarkTheme", "false") === "true" : false;
        document.body.setAttribute("data-theme", dark ? "dark" : "light");
    }

    if (themeToggle) {
        themeToggle.addEventListener("click", () => {
            const dark = document.body.getAttribute("data-theme") !== "dark";
            document.body.setAttribute("data-theme", dark ? "dark" : "light");
            if (preferences) preferences.set("isDarkTheme", String(dark));
        });
    }

    if (hamburgerBtn) {
        hamburgerBtn.addEventListener("click", () => {
            const collapsed = document.body.classList.toggle("sidebar-collapsed");
            hamburgerBtn.setAttribute("aria-expanded", String(!collapsed));
        });
    }

    // ---- Table --------------------------------------------------------------

    function renderRows() {
        if (!rowsEl) return;
        const needle = (filterEl?.value ?? "").trim().toLowerCase();
        const rows = needle
            ? parsedRows.filter(row =>
                String(row.train).toLowerCase().includes(needle) ||
                String(row.loco_number).toLowerCase().includes(needle))
            : parsedRows;

        rowsEl.innerHTML = rows.map(row => "<tr>" + L.COLUMNS.map(column =>
            `<td>${escapeHtml(row[column])}</td>`).join("") + "</tr>").join("");

        if (emptyEl) emptyEl.hidden = rows.length > 0;
    }

    if (filterEl) filterEl.addEventListener("input", renderRows);

    // ---- Read flow (UV Studio flash.js dump flow) ---------------------------

    function updateReadButton() {
        if (!readBtn) return;
        readBtn.disabled = Boolean(currentOperation);
    }

    async function readLbjLog() {
        const operation = beginOperation("read-lbj-log");
        if (!operation) return;

        setProgressVisible(true);
        updateProgress(0);
        if (resultsEl) resultsEl.hidden = true;
        if (csvDownload) csvDownload.style.display = "none";
        if (binDownload) binDownload.style.display = "none";

        try {
            if (!serial.isSupported()) {
                throw new Error(t("lbjBrowserUnsupported"));
            }

            if (!serial.isConnected()) await serial.connect();
            log(t("lbjReading"), "info");

            const device = await serial.requestDeviceInfo();
            if (!(await serial.hasExternalFlashSupport(device.timestamp))) {
                throw new Error(t("lbjUnsupported"));
            }

            const image = await serial.readLog(device.timestamp, fraction => {
                updateProgress(fraction * 100);
                void paint();
            });

            const parsed = L.parseLog(image);
            parsedRows = parsed.rows;

            if (csvUrl) { URL.revokeObjectURL(csvUrl); csvUrl = null; }
            if (binUrl) { URL.revokeObjectURL(binUrl); binUrl = null; }

            /* Reveal the download chips the way flash.js does (css/tools.css
               keeps .download-link at display:none until we show it). */
            if (csvLink && csvDownload) {
                csvUrl = URL.createObjectURL(new Blob(
                    [L.rowsToCsv(parsed.rows, { chinese: window.uvStudioI18n?.lang === "zh" })],
                    { type: "text/csv;charset=utf-8" }));
                csvLink.href = csvUrl;
                csvDownload.style.display = "flex";
            }
            if (binLink && binDownload) {
                binUrl = URL.createObjectURL(
                    new Blob([image], { type: "application/octet-stream" }));
                binLink.href = binUrl;
                binDownload.style.display = "flex";
            }

            if (metaEl) {
                const zone = `0x${L.LOG_BASE.toString(16).toUpperCase()}..0x${(L.LOG_BASE + L.LOG_SIZE - 1).toString(16).toUpperCase()}`;
                metaEl.textContent = t("lbjMeta", parsed.valid, parsed.unwritten, parsed.badCrc, zone);
            }
            if (resultsEl) resultsEl.hidden = false;
            if (filterEl) filterEl.value = "";
            renderRows();

            updateProgress(100);
            log(t("lbjReadDone", parsed.valid, device.name || "radio"), "success");
            setTimeout(() => setProgressVisible(false), 800);
        } catch (error) {
            setProgressVisible(false);
            log(t("error", error?.message ?? String(error)), "error");
        } finally {
            if (serial.isConnected()) await serial.disconnect().catch(() => undefined);
            endOperation(operation);
        }
    }

    if (readBtn) readBtn.addEventListener("click", () => { void readLbjLog(); });

    // ---- Boot ---------------------------------------------------------------

    // studio-i18n.js initialises itself when it loads (before this file), so the
    // dictionaries are already applied here; only refresh the pieces we own.
    applyTheme();
    if (versionLabel) versionLabel.textContent = `v${VERSION}`;
    document.title = `LBJ Log v${VERSION} — UV Studio`;
    renderSerialStatus(lastSerialSnapshot);
    refreshLogToggle();
    updateReadButton();

    if (!serial.isSupported()) {
        log(t("lbjBrowserUnsupported"), "error");
    }
})();

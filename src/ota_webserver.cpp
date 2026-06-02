#include "ota_webserver.h"

#if defined(ENABLE_OTA_WEBSERVER)

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ESPmDNS.h>
#include "ForwarderCAN.h"
#include "ForwarderConfig.h"
#include "web_state.h"

static WebServer server(80);
static bool otaActive = false;

// Module tracking from heartbeats
struct ModuleInfo {
    uint32_t lastSeen = 0;
    uint8_t addr = 0;
    uint8_t type = 0; // 0=unknown, 1=motor, 2=joystick
    uint16_t uptime = 0;
    uint8_t data5 = 0;
};
static ModuleInfo g_modules[256];
static uint32_t lastModuleScan = 0;

static int parseJsonInt(const String& json, const char* key, int searchPos);

// ---------------------------------------------------------------------------
// HTML Page
// ---------------------------------------------------------------------------
static const char* MAIN_HTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Forwarder CAN Controller</title>
<style>
* { box-sizing: border-box; }
body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    background: #0f172a;
    color: #e2e8f0;
    margin: 0;
    padding: 0;
    font-size: 14px;
}
header {
    background: #1e293b;
    padding: 16px 24px;
    border-bottom: 1px solid #334155;
    display: flex;
    align-items: center;
    justify-content: space-between;
}
h1 { margin: 0; font-size: 1.2rem; color: #38bdf8; }
.tabs {
    display: flex;
    gap: 4px;
    padding: 12px 24px 0;
    background: #1e293b;
    border-bottom: 1px solid #334155;
}
.tab {
    padding: 10px 18px;
    background: transparent;
    border: none;
    color: #94a3b8;
    cursor: pointer;
    border-bottom: 2px solid transparent;
    font-weight: 500;
}
.tab.active { color: #38bdf8; border-bottom-color: #38bdf8; }
.tab:hover { color: #e2e8f0; }
.panel { display: none; padding: 20px 24px; max-width: 1200px; }
.panel.active { display: block; }
.card {
    background: #1e293b;
    border-radius: 10px;
    padding: 16px;
    margin-bottom: 16px;
    border: 1px solid #334155;
}
.card h3 { margin: 0 0 12px; font-size: 1rem; color: #94a3b8; }
.grid2 { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }
@media (max-width: 800px) { .grid2 { grid-template-columns: 1fr; } }
.bar-track {
    background: #334155;
    border-radius: 6px;
    height: 24px;
    position: relative;
    overflow: hidden;
    margin: 6px 0;
}
.bar-fill {
    background: linear-gradient(90deg, #0ea5e9, #22d3ee);
    height: 100%;
    border-radius: 6px;
    transition: width 0.15s;
    position: relative;
}
.bar-label {
    position: absolute;
    right: 8px;
    top: 50%;
    transform: translateY(-50%);
    font-size: 11px;
    color: #fff;
    font-weight: 600;
}
.info-row { display: flex; justify-content: space-between; font-size: 0.85rem; color: #94a3b8; margin: 4px 0; }
table { width: 100%; border-collapse: collapse; font-size: 0.85rem; }
th, td { padding: 8px; text-align: left; border-bottom: 1px solid #334155; }
th { color: #94a3b8; font-weight: 500; }
button {
    background: #0ea5e9;
    color: #fff;
    border: none;
    border-radius: 6px;
    padding: 6px 12px;
    cursor: pointer;
    font-size: 0.8rem;
}
button:hover { background: #0284c7; }
button.secondary { background: #475569; }
button.secondary:hover { background: #64748b; }
button.danger { background: #ef4444; }
button.danger:hover { background: #dc2626; }
input[type="number"], select {
    background: #0f172a;
    border: 1px solid #475569;
    color: #e2e8f0;
    border-radius: 6px;
    padding: 4px 8px;
    font-size: 0.8rem;
    width: 80px;
}
input[type="range"] {
    width: 100%;
    margin: 4px 0;
}
.axis-row {
    display: grid;
    grid-template-columns: 40px 60px 70px 70px 50px 1fr 1fr 80px 80px 60px;
    gap: 8px;
    align-items: center;
    padding: 6px 0;
    border-bottom: 1px solid #334155;
    font-size: 0.8rem;
}
.axis-row.header { color: #94a3b8; font-weight: 500; border-bottom: 2px solid #475569; }
.canout-row {
    display: grid;
    grid-template-columns: 40px 50px 80px 80px 70px 90px 90px;
    gap: 8px;
    align-items: center;
    padding: 6px 0;
    border-bottom: 1px solid #334155;
    font-size: 0.8rem;
}
.canout-row.header { color: #94a3b8; font-weight: 500; border-bottom: 2px solid #475569; }
@media (max-width: 1000px) {
    .axis-row { grid-template-columns: 30px 50px 60px 60px 40px 1fr 1fr 60px 60px 50px; }
}
.slider-group { display: flex; align-items: center; gap: 8px; }
.slider-group input[type="range"] { flex: 1; }
.slider-group span { min-width: 36px; text-align: right; font-size: 0.75rem; color: #94a3b8; }
#status { position: fixed; bottom: 16px; right: 16px; padding: 10px 16px; border-radius: 8px; font-size: 0.85rem; display: none; z-index: 100; }
#status.info { background: #0ea5e9; display: block; }
#status.success { background: #22c55e; display: block; }
#status.error { background: #ef4444; display: block; }
</style>
</head>
<body>
<header>
    <h1>Forwarder CAN Controller</h1>
    <div class="info-row" style="margin:0">
        <span id="localAddr">Addr: --</span>
        <span id="busStatus" style="margin-left:16px">Bus: --</span>
    </div>
</header>
<div class="tabs">
    <button class="tab active" onclick="switchTab('dash')">Dashboard</button>
    <button class="tab" onclick="switchTab('modules')">Modules</button>
    <button class="tab" onclick="switchTab('mapping')">Motor Mapping</button>
    <button class="tab" onclick="switchTab('canout')">CAN Output</button>
    <button class="tab" onclick="switchTab('ota')">OTA Update</button>
</div>

<div id="dash" class="panel active">
    <div class="grid2">
        <div class="card">
            <h3>Joystick 1 (0x21)</h3>
            <div id="joy1_pots"></div>
            <div class="info-row"><span>Buttons:</span><span id="joy1_btns">--</span></div>
        </div>
        <div class="card">
            <h3>Joystick 2 (0x22)</h3>
            <div id="joy2_pots"></div>
            <div class="info-row"><span>Buttons:</span><span id="joy2_btns">--</span></div>
        </div>
    </div>
    <div class="card">
        <h3>Solenoid Outputs</h3>
        <div id="sol_bars"></div>
    </div>
    <div class="card">
        <h3>CAN Bus Stats</h3>
        <div class="info-row"><span>TX Count:</span><span id="txCount">0</span></div>
        <div class="info-row"><span>RX Count:</span><span id="rxCount">0</span></div>
        <div class="info-row"><span>Errors:</span><span id="errCount">0</span></div>
        <div class="info-row"><span>Uptime:</span><span id="uptime">0s</span></div>
    </div>
</div>

<div id="modules" class="panel">
    <div class="card">
        <h3>Detected Modules</h3>
        <table>
            <thead><tr><th>Address</th><th>Type</th><th>Uptime</th><th>Last Seen</th><th>Actions</th></tr></thead>
            <tbody id="moduleTable"></tbody>
        </table>
    </div>
</div>

<div id="mapping" class="panel">
    <div class="card">
        <h3>Axis Configuration</h3>
        <div id="axisList"></div>
        <div style="margin-top:12px;display:flex;gap:8px;">
            <button onclick="saveMapping()">Save to Motor Driver</button>
            <button class="secondary" onclick="loadMapping()">Refresh</button>
        </div>
    </div>
</div>

<div id="canout" class="panel">
    <div class="card">
        <h3>CAN-Triggered GPIO Outputs</h3>
        <p style="color:#94a3b8;font-size:0.85rem;margin:0 0 12px">React to incoming CAN messages by toggling or pulsing a GPIO pin (e.g. drive a relay).</p>
        <div id="canOutList"></div>
        <div style="margin-top:12px;display:flex;gap:8px;">
            <button onclick="saveCanOut()">Save</button>
            <button class="secondary" onclick="fetchCanOut()">Refresh</button>
        </div>
    </div>
</div>

<div id="ota" class="panel">
    <div class="card">
        <h3>Firmware Update</h3>
        <label style="display:block;margin:8px 0 4px;color:#94a3b8">Select firmware (.bin)</label>
        <input type="file" id="firmware" accept=".bin" style="width:100%;padding:8px;background:#0f172a;border:1px solid #475569;border-radius:6px;color:#e2e8f0">
        <button onclick="startUpload()" style="margin-top:12px;width:auto">Update Firmware</button>
        <div class="bar-track" style="margin-top:12px;height:8px"><div class="bar-fill" id="prog" style="width:0%"></div></div>
    </div>
</div>

<div id="status"></div>

<script>
let gState = {};
let gConfig = { axes: [] };

function setStatus(msg, type) {
    const s = document.getElementById('status');
    s.textContent = msg;
    s.className = type || 'info';
    setTimeout(() => s.className = '', 3000);
}

function switchTab(name) {
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.panel').forEach(p => p.classList.remove('active'));
    event.target.classList.add('active');
    document.getElementById(name).classList.add('active');
}

function barHtml(id, label, value, max, color) {
    const pct = Math.min(100, Math.max(0, (value / max) * 100)).toFixed(1);
    return `<div class="info-row"><span>${label}</span><span>${value.toFixed(0)}</span></div>
            <div class="bar-track"><div class="bar-fill" id="${id}" style="width:${pct}%;background:${color||''}"></div></div>`;
}

function renderJoysticks() {
    const joy1 = gState.joy && gState.joy[0x21] ? gState.joy[0x21] : { pots: [512,512,512], btns: 0, age: 9999 };
    const joy2 = gState.joy && gState.joy[0x22] ? gState.joy[0x22] : { pots: [512,512,512], btns: 0, age: 9999 };
    let h1 = ''; for (let i = 0; i < 3; i++) h1 += barHtml(`j1p${i}`, `Pot ${i+1}`, joy1.pots[i], 1023, 'linear-gradient(90deg,#f59e0b,#fbbf24)');
    document.getElementById('joy1_pots').innerHTML = h1;
    document.getElementById('joy1_btns').textContent = (joy1.btns & 1 ? 'Btn1 ' : '') + (joy1.btns & 2 ? 'Btn2' : '') + (joy1.btns === 0 ? 'None' : '');
    let h2 = ''; for (let i = 0; i < 3; i++) h2 += barHtml(`j2p${i}`, `Pot ${i+1}`, joy2.pots[i], 1023, 'linear-gradient(90deg,#f59e0b,#fbbf24)');
    document.getElementById('joy2_pots').innerHTML = h2;
    document.getElementById('joy2_btns').textContent = (joy2.btns & 1 ? 'Btn1 ' : '') + (joy2.btns & 2 ? 'Btn2' : '') + (joy2.btns === 0 ? 'None' : '');
}

function renderSol() {
    const sol = gState.sol || [];
    let h = '<div style="display:grid;grid-template-columns:repeat(8,1fr);gap:8px">';
    for (let i = 0; i < 16; i++) {
        const v = sol[i] || 0;
        const pct = (v / 4095 * 100).toFixed(1);
        h += `<div style="text-align:center;font-size:0.75rem">
            <div style="margin-bottom:4px">CH${i}</div>
            <div class="bar-track" style="height:60px;display:flex;align-items:flex-end">
                <div class="bar-fill" style="width:100%;height:${pct}%;border-radius:4px;background:linear-gradient(0deg,#0ea5e9,#22d3ee)"></div>
            </div>
            <div style="margin-top:4px;color:#94a3b8">${v}</div>
        </div>`;
    }
    h += '</div>';
    document.getElementById('sol_bars').innerHTML = h;
}

function renderModules() {
    const mods = gState.modules || {};
    let rows = '';
    for (const addr in mods) {
        const m = mods[addr];
        const type = m.type === 1 ? 'Motor' : (m.type === 2 ? 'Joystick' : 'Unknown');
        rows += `<tr>
            <td>0x${Number(addr).toString(16).toUpperCase().padStart(2,'0')}</td>
            <td>${type}</td>
            <td>${m.uptime}s</td>
            <td>${m.age < 2 ? 'Just now' : m.age + 's ago'}</td>
            <td>
                <button onclick="identify(${addr})">Identify</button>
                <input type="number" min="32" max="239" value="${addr}" id="addr_${addr}" style="width:60px;margin-left:4px">
                <button class="secondary" onclick="setAddr(${addr})">Set</button>
            </td>
        </tr>`;
    }
    if (!rows) rows = '<tr><td colspan="5" style="text-align:center;color:#64748b">No modules detected yet</td></tr>';
    document.getElementById('moduleTable').innerHTML = rows;
}

function renderMapping() {
    const axes = gConfig.axes || [];
    let h = '<div class="axis-row header"><div>#</div><div>En</div><div>Src</div><div>Pot</div><div>Ch</div><div>Deadband Min-Max</div><div>PWM Min-Max</div><div>DB Min</div><div>DB Max</div><div>Bidir</div></div>';
    for (let i = 0; i < 16; i++) {
        const a = axes[i] || { sourceAddress: 0, potIndex: 0, outputChannel: i, deadbandMin: 492, deadbandMax: 532, pwmMin: 64, pwmMax: 128, flags: 0 };
        const en = (a.flags & 1) ? 'checked' : '';
        const bidir = (a.flags & 2) ? 'checked' : '';
        h += `<div class="axis-row">
            <div>${i}</div>
            <div><input type="checkbox" id="a${i}_en" ${en}></div>
            <div><select id="a${i}_src"><option value="0">Off</option><option value="33" ${a.sourceAddress==33?'selected':''}>0x21</option><option value="34" ${a.sourceAddress==34?'selected':''}>0x22</option></select></div>
            <div><select id="a${i}_pot"><option value="0" ${a.potIndex==0?'selected':''}>Pot1</option><option value="1" ${a.potIndex==1?'selected':''}>Pot2</option><option value="2" ${a.potIndex==2?'selected':''}>Pot3</option></select></div>
            <div><input type="number" id="a${i}_ch" value="${a.outputChannel}" min="0" max="15" style="width:50px"></div>
            <div class="slider-group"><input type="range" id="a${i}_dbmin" min="0" max="1023" value="${a.deadbandMin}" oninput="document.getElementById('a${i}_dbmin_v').textContent=this.value"><span id="a${i}_dbmin_v">${a.deadbandMin}</span></div>
            <div class="slider-group"><input type="range" id="a${i}_dbmax" min="0" max="1023" value="${a.deadbandMax}" oninput="document.getElementById('a${i}_dbmax_v').textContent=this.value"><span id="a${i}_dbmax_v">${a.deadbandMax}</span></div>
            <div><input type="number" id="a${i}_pwmin" value="${a.pwmMin}" min="0" max="255" style="width:50px"></div>
            <div><input type="number" id="a${i}_pwmax" value="${a.pwmMax}" min="0" max="255" style="width:50px"></div>
            <div><input type="checkbox" id="a${i}_bidir" ${bidir}></div>
        </div>`;
    }
    document.getElementById('axisList').innerHTML = h;
}

async function fetchState() {
    try {
        const r = await fetch('/api/state');
        gState = await r.json();
        document.getElementById('localAddr').textContent = 'Addr: 0x' + gState.localAddr.toString(16).toUpperCase().padStart(2,'0');
        document.getElementById('busStatus').textContent = 'Bus: ' + (gState.online ? 'Online' : 'Offline');
        document.getElementById('txCount').textContent = gState.txCount;
        document.getElementById('rxCount').textContent = gState.rxCount;
        document.getElementById('errCount').textContent = gState.errCount;
        document.getElementById('uptime').textContent = gState.uptime + 's';
        renderJoysticks();
        renderSol();
        renderModules();
    } catch(e) {}
}

async function fetchConfig() {
    try {
        const r = await fetch('/api/config');
        gConfig = await r.json();
        renderMapping();
    } catch(e) {}
}

async function identify(addr) {
    try {
        await fetch('/api/identify', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({target: addr}) });
        setStatus('Identify sent to 0x' + addr.toString(16).toUpperCase(), 'success');
    } catch(e) { setStatus('Failed', 'error'); }
}

async function setAddr(current) {
    const newAddr = parseInt(document.getElementById('addr_' + current).value);
    try {
        await fetch('/api/address', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({target: current, address: newAddr}) });
        setStatus('Address change sent. Module will reboot.', 'success');
    } catch(e) { setStatus('Failed', 'error'); }
}

async function saveMapping() {
    const axes = [];
    for (let i = 0; i < 16; i++) {
        const flags = (document.getElementById('a' + i + '_en').checked ? 1 : 0) | (document.getElementById('a' + i + '_bidir').checked ? 2 : 0);
        axes.push({
            axisIdx: i,
            sourceAddress: parseInt(document.getElementById('a' + i + '_src').value),
            potIndex: parseInt(document.getElementById('a' + i + '_pot').value),
            outputChannel: parseInt(document.getElementById('a' + i + '_ch').value),
            deadbandMin: parseInt(document.getElementById('a' + i + '_dbmin').value),
            deadbandMax: parseInt(document.getElementById('a' + i + '_dbmax').value),
            pwmMin: parseInt(document.getElementById('a' + i + '_pwmin').value),
            pwmMax: parseInt(document.getElementById('a' + i + '_pwmax').value),
            flags: flags
        });
    }
    try {
        await fetch('/api/config', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({axes}) });
        setStatus('Mapping saved', 'success');
    } catch(e) { setStatus('Failed to save', 'error'); }
}

function loadMapping() { fetchConfig(); }

let gCanOut = [];

function renderCanOut() {
    const rules = gCanOut || [];
    const pins = [2,4,12,13,14,15,16,17,18,19,21,22,23,25,26,27,32,33];
    let h = '<div class="canout-row header"><div>#</div><div>En</div><div>PF (hex)</div><div>SA (hex)</div><div>GPIO</div><div>Mode</div><div>Pulse ms</div></div>';
    for (let i = 0; i < 4; i++) {
        const r = rules[i] || {enabled:false,matchPF:0,matchSA:0,gpioPin:0,mode:0,momentaryMs:500};
        let pinOpts = '';
        for (const p of pins) pinOpts += `<option value="${p}" ${r.gpioPin==p?'selected':''}>${p}</option>`;
        h += `<div class="canout-row">
            <div>${i}</div>
            <div><input type="checkbox" id="co${i}_en" ${r.enabled?'checked':''}></div>
            <div><input type="number" id="co${i}_pf" value="${r.matchPF}" min="0" max="255" style="width:60px" placeholder="0x00"></div>
            <div><input type="number" id="co${i}_sa" value="${r.matchSA}" min="0" max="255" style="width:60px" placeholder="0=any"></div>
            <div><select id="co${i}_pin" style="width:60px"><option value="0">Off</option>${pinOpts}</select></div>
            <div><select id="co${i}_mode" style="width:80px"><option value="0" ${r.mode==0?'selected':''}>Toggle</option><option value="1" ${r.mode==1?'selected':''}>Momentary</option></select></div>
            <div><input type="number" id="co${i}_ms" value="${r.momentaryMs}" min="50" max="10000" style="width:70px"></div>
        </div>`;
    }
    document.getElementById('canOutList').innerHTML = h;
}

async function fetchCanOut() {
    try {
        const r = await fetch('/api/canoutput');
        gCanOut = (await r.json()).rules || [];
        renderCanOut();
    } catch(e) {}
}

async function saveCanOut() {
    const rules = [];
    for (let i = 0; i < 4; i++) {
        rules.push({
            ruleIdx: i,
            enabled: document.getElementById('co'+i+'_en').checked,
            matchPF: parseInt(document.getElementById('co'+i+'_pf').value) || 0,
            matchSA: parseInt(document.getElementById('co'+i+'_sa').value) || 0,
            gpioPin: parseInt(document.getElementById('co'+i+'_pin').value) || 0,
            mode: parseInt(document.getElementById('co'+i+'_mode').value) || 0,
            momentaryMs: parseInt(document.getElementById('co'+i+'_ms').value) || 500
        });
    }
    try {
        await fetch('/api/canoutput', { method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({rules}) });
        setStatus('CAN output rules saved', 'success');
    } catch(e) { setStatus('Failed to save', 'error'); }
}

function startUpload() {
    const file = document.getElementById('firmware').files[0];
    if (!file) { setStatus('Please select a file', 'error'); return; }
    const prog = document.getElementById('prog');
    const xhr = new XMLHttpRequest();
    xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) prog.style.width = (e.loaded / e.total * 100).toFixed(1) + '%';
    };
    xhr.onload = () => {
        if (xhr.status === 200) {
            setStatus('Update successful! Rebooting...', 'success');
            setTimeout(() => location.reload(), 8000);
        } else {
            setStatus('Update failed: ' + xhr.responseText, 'error');
        }
    };
    xhr.onerror = () => setStatus('Network error', 'error');
    xhr.open('POST', '/update');
    xhr.send(file);
}

setInterval(fetchState, 200);
fetchState();
fetchConfig();
fetchCanOut();
</script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------------------
// HTTP Handlers
// ---------------------------------------------------------------------------
static void handleRoot() {
    server.send(200, "text/html", MAIN_HTML);
}

static void handleState() {
    String json = "{";
    json += "\"localAddr\":" + String(g_can ? g_can->getAddress() : 0) + ",";
    json += "\"online\":" + String(g_can && g_can->isOnline() ? "true" : "false") + ",";
    json += "\"uptime\":" + String(millis() / 1000) + ",";
    json += "\"txCount\":" + String(g_can ? g_can->getTxCount() : 0) + ",";
    json += "\"rxCount\":" + String(g_can ? g_can->getRxCount() : 0) + ",";
    json += "\"errCount\":" + String(g_can ? g_can->getErrorCount() : 0) + ",";

    // Joystick data
    json += "\"joy\":{";
    for (int sa = 0; sa < 256; sa++) {
        if (g_joyUpdateTime[sa] > 0 && millis() - g_joyUpdateTime[sa] < 2000) {
            json += "\"" + String(sa) + "\":{";
            json += "\"pots\":[" + String(g_joyPots[sa][0]) + "," + String(g_joyPots[sa][1]) + "," + String(g_joyPots[sa][2]) + "],";
            json += "\"age\":" + String((millis() - g_joyUpdateTime[sa]) / 1000) + "},";
        }
    }
    // Local joystick data (if running on joystick ECU)
#if defined(ECU_TYPE_JOYSTICK)
    if (g_ecuJoystickId > 0 && g_can) {
        json += "\"" + String(g_can->getAddress()) + "\":{";
        json += "\"pots\":[" + String(g_localPot1) + "," + String(g_localPot2) + "," + String(g_localPot3) + "],";
        json += "\"btns\":" + String((g_localBtn1 ? 1 : 0) | (g_localBtn2 ? 2 : 0)) + ",";
        json += "\"age\":0},";
    }
#endif
    if (json.endsWith(",")) json.remove(json.length() - 1);
    json += "},";

    // Solenoid outputs
    json += "\"sol\":[";
    for (int i = 0; i < MAX_AXIS_COUNT; i++) {
        json += String(g_solenoidValues[i]) + ",";
    }
    if (json.endsWith(",")) json.remove(json.length() - 1);
    json += "],";

    // Modules
    json += "\"modules\":{";
    for (int i = 0; i < 256; i++) {
        if (g_modules[i].lastSeen > 0 && millis() - g_modules[i].lastSeen < 5000) {
            json += "\"" + String(i) + "\":{";
            json += "\"addr\":" + String(g_modules[i].addr) + ",";
            json += "\"type\":" + String(g_modules[i].type) + ",";
            json += "\"uptime\":" + String(g_modules[i].uptime) + ",";
            json += "\"age\":" + String((millis() - g_modules[i].lastSeen) / 1000) + "},";
        }
    }
    if (json.endsWith(",")) json.remove(json.length() - 1);
    json += "}";
    json += "}";
    server.send(200, "application/json", json);
}

static void handleConfigGet() {
    String json = "{";
    json += "\"pcaCount\":" + String(g_motorCfg.pcaCount) + ",";
    json += "\"axes\":[";
    for (int i = 0; i < MAX_AXIS_COUNT; i++) {
        const AxisConfig& a = g_motorCfg.axes[i];
        json += "{";
        json += "\"sourceAddress\":" + String(a.sourceAddress) + ",";
        json += "\"potIndex\":" + String(a.potIndex) + ",";
        json += "\"outputChannel\":" + String(a.outputChannel) + ",";
        json += "\"deadbandMin\":" + String(a.deadbandMin) + ",";
        json += "\"deadbandMax\":" + String(a.deadbandMax) + ",";
        json += "\"pwmMin\":" + String(a.pwmMin) + ",";
        json += "\"pwmMax\":" + String(a.pwmMax) + ",";
        json += "\"flags\":" + String(a.flags);
        json += "},";
    }
    if (json.endsWith(",")) json.remove(json.length() - 1);
    json += "]}";
    server.send(200, "application/json", json);
}

static void handleConfigPost() {
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        // Simple JSON parsing for axis config
        // This is a minimal parser - in production you'd use ArduinoJson
        // For now, parse manually since we control the format
        for (int i = 0; i < MAX_AXIS_COUNT; i++) {
            String key = "\"axisIdx\":" + String(i);
            int idx = body.indexOf(key);
            if (idx >= 0) {
                AxisConfig a;
                a.sourceAddress = parseJsonInt(body, "sourceAddress", idx);
                a.potIndex = parseJsonInt(body, "potIndex", idx);
                a.outputChannel = parseJsonInt(body, "outputChannel", idx);
                a.deadbandMin = parseJsonInt(body, "deadbandMin", idx);
                a.deadbandMax = parseJsonInt(body, "deadbandMax", idx);
                a.pwmMin = parseJsonInt(body, "pwmMin", idx);
                a.pwmMax = parseJsonInt(body, "pwmMax", idx);
                a.flags = parseJsonInt(body, "flags", idx);
                g_motorCfg.axes[i] = a;

                // Save locally if motor driver
#if defined(ECU_TYPE_MOTOR_DRIVER)
                ForwarderConfig cfg("motorcfg");
                cfg.begin();
                cfg.saveAxisConfig(i, a);
#endif
                // Also broadcast to motor driver if this is a joystick
#if defined(ECU_TYPE_JOYSTICK)
                if (g_can) {
                    uint8_t buf[8];
                    a.pack(buf, i);
                    g_can->send(PF_CONFIG_AXIS, 0x20, buf, 8, 6);
                }
#endif
            }
        }
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static int parseJsonInt(const String& json, const char* key, int searchPos) {
    String search = String("\"") + key + "\":";
    int pos = json.indexOf(search, searchPos);
    if (pos < 0) return 0;
    pos += search.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    int end = pos;
    while (end < json.length() && (json[end] == '-' || isdigit(json[end]))) end++;
    return json.substring(pos, end).toInt();
}

static void handleIdentify() {
    if (server.hasArg("plain") && g_can) {
        String body = server.arg("plain");
        int target = parseJsonInt(body, "target", 0);
        g_can->send(PF_IDENTIFY, target, nullptr, 0, 6);
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleAddress() {
    if (server.hasArg("plain") && g_can) {
        String body = server.arg("plain");
        int target = parseJsonInt(body, "target", 0);
        int addr = parseJsonInt(body, "address", 0);
        uint8_t data[1] = { (uint8_t)addr };
        g_can->send(PF_SET_ADDRESS, target, data, 1, 6);
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleCanOutputGet() {
    String json = "{\"rules\":[";
    for (int i = 0; i < MAX_CAN_OUTPUT_RULES; i++) {
        const CanOutputRule& r = g_canOutputRules[i];
        json += "{";
        json += "\"enabled\":" + String(r.enabled ? "true" : "false") + ",";
        json += "\"matchPF\":" + String(r.matchPF) + ",";
        json += "\"matchSA\":" + String(r.matchSA) + ",";
        json += "\"gpioPin\":" + String(r.gpioPin) + ",";
        json += "\"mode\":" + String(r.mode) + ",";
        json += "\"momentaryMs\":" + String(r.momentaryMs);
        json += "},";
    }
    if (json.endsWith(",")) json.remove(json.length() - 1);
    json += "]}";
    server.send(200, "application/json", json);
}

static void handleCanOutputPost() {
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        for (int i = 0; i < MAX_CAN_OUTPUT_RULES; i++) {
            String key = "\"ruleIdx\":" + String(i);
            int idx = body.indexOf(key);
            if (idx >= 0) {
                CanOutputRule r;
                r.enabled = body.indexOf("\"enabled\":true", idx) > 0 && body.indexOf("\"enabled\":true", idx) < idx + 200;
                r.matchPF = parseJsonInt(body, "matchPF", idx);
                r.matchSA = parseJsonInt(body, "matchSA", idx);
                r.gpioPin = parseJsonInt(body, "gpioPin", idx);
                r.mode = parseJsonInt(body, "mode", idx);
                r.momentaryMs = parseJsonInt(body, "momentaryMs", idx);
                g_canOutputRules[i] = r;
#if defined(ECU_TYPE_MOTOR_DRIVER)
                ForwarderConfig cfg("motorcfg");
                cfg.begin();
                cfg.saveCanOutputRule(i, r);
#endif
            }
        }
        // Re-init outputs with new config
        can_output_setup(g_canOutputRules);
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleUpdate() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        otaActive = true;
        Serial.printf("[OTA] Start: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.printf("[OTA] Success\n");
            server.send(200, "text/plain", "OK");
            delay(500);
            ESP.restart();
        } else {
            Update.printError(Serial);
            server.send(500, "text/plain", Update.errorString());
        }
        otaActive = false;
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.end();
        otaActive = false;
        Serial.println("[OTA] Aborted");
    }
}

static void handleUpdatePost() {
    server.send(200, "text/plain", "OK");
}

// ---------------------------------------------------------------------------
// Heartbeat scanner
// ---------------------------------------------------------------------------
static void scanHeartbeats() {
    if (!g_can) return;
    CANMessage msg;
    while (g_can->receive(msg, 0)) {
        uint8_t pf = J1939_GET_PF(msg.id);
        uint8_t sa = J1939_GET_SA(msg.id);
        if (pf == PF_HEARTBEAT && sa < 256) {
            g_modules[sa].lastSeen = millis();
            g_modules[sa].addr = sa;
            g_modules[sa].uptime = msg.data[0] | ((uint16_t)msg.data[1] << 8);
            g_modules[sa].data5 = msg.data[5];
            // Heuristic type detection
            if (msg.data[5] == 16 || msg.data[5] == 8) {
                g_modules[sa].type = 1; // Motor driver
            } else if (msg.data[3] == 1 || msg.data[3] == 2) {
                g_modules[sa].type = 2; // Joystick
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void ota_setup(const char* hostname) {
    WiFi.mode(WIFI_AP);
    String ssid = String(hostname);
    WiFi.softAP(ssid.c_str(), "12345678");

    if (!MDNS.begin(hostname)) {
        Serial.println("[OTA] mDNS failed");
    } else {
        MDNS.addService("http", "tcp", 80);
    }

    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[OTA] AP '%s' started, IP: %s\n", ssid.c_str(), ip.toString().c_str());

    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/state", HTTP_GET, handleState);
    server.on("/api/config", HTTP_GET, handleConfigGet);
    server.on("/api/config", HTTP_POST, handleConfigPost);
    server.on("/api/identify", HTTP_POST, handleIdentify);
    server.on("/api/address", HTTP_POST, handleAddress);
    server.on("/api/canoutput", HTTP_GET, handleCanOutputGet);
    server.on("/api/canoutput", HTTP_POST, handleCanOutputPost);
    server.on("/update", HTTP_POST, handleUpdatePost, handleUpdate);
    server.begin();
    Serial.println("[OTA] Web server started on port 80");
}

void ota_loop() {
    server.handleClient();
    scanHeartbeats();
}

bool ota_is_active() {
    return otaActive;
}

#else // not ENABLE_OTA_WEBSERVER

void ota_setup(const char* hostname) { (void)hostname; }
void ota_loop() {}
bool ota_is_active() { return false; }

#endif

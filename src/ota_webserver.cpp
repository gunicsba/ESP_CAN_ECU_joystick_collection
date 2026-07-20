#include "ota_webserver.h"

#if defined(ENABLE_OTA_WEBSERVER)

#include "ForwarderCAN.h"
#include "ForwarderConfig.h"
#include "web_state.h"
#include <ESP2SOTA.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#if defined(ECU_TYPE_MOTOR_DRIVER)
#include "ecu_motor_driver.h"
#endif

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

static int parseJsonInt(const String &json, const char *key, int searchStart,
                        int searchEnd = -1);

// ---------------------------------------------------------------------------
// HTML Page
// ---------------------------------------------------------------------------
static const char *MAIN_HTML = R"rawliteral(
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
    overflow-x: auto;
    -webkit-overflow-scrolling: touch;
    scrollbar-width: none;
}
.tabs::-webkit-scrollbar { display: none; }
.tab {
    padding: 10px 18px;
    background: transparent;
    border: none;
    color: #94a3b8;
    cursor: pointer;
    border-bottom: 2px solid transparent;
    font-weight: 500;
    white-space: nowrap;
    flex-shrink: 0;
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
@media (max-width: 800px) {
    .grid2 { grid-template-columns: 1fr; }
    .tabs { padding: 8px 12px 0; gap: 2px; }
    .tab { padding: 8px 12px; font-size: 0.8rem; }
    .panel { padding: 12px; }
    .card { padding: 12px; }
    header { padding: 12px 16px; }
    h1 { font-size: 1rem; }
    .axis-row, .canout-row { overflow-x: auto; }
}
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
input[type="number"], select, input[type="text"] {
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
    grid-template-columns: 30px 40px 130px 80px 60px 60px 45px 40px 100px 70px;
    gap: 4px;
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
    .axis-row { grid-template-columns: 25px 35px 110px 70px 55px 55px 40px 35px 90px 60px; }
}
.slider-group { display: flex; align-items: center; gap: 8px; }
.slider-group input[type="range"] { flex: 1; }
.slider-group span { min-width: 36px; text-align: right; font-size: 0.75rem; color: #94a3b8; }
#status { position: fixed; bottom: 16px; right: 16px; padding: 10px 16px; border-radius: 8px; font-size: 0.85rem; display: none; z-index: 100; }
#status.info { background: #0ea5e9; display: block; }
#status.success { background: #22c55e; display: block; }
#status.error { background: #ef4444; display: block; }
.db-bar { background:#334155; border-radius:6px; height:32px; position:relative; margin:8px 0; overflow:hidden; }
.db-zone { position:absolute; height:100%; background:rgba(245,158,11,0.25); border-left:2px dashed #f59e0b; border-right:2px dashed #f59e0b; }
.db-ptr { position:absolute; top:0; height:100%; width:3px; background:#22c55e; border-radius:2px; transform:translateX(-1px); z-index:2; transition:left 0.15s; }
.db-ptr.stale { background:#ef4444; }
.db-step-row { display:flex; align-items:center; gap:4px; }
.db-step-row .db-lbl { font-size:0.7rem; color:#94a3b8; min-width:26px; }
.db-step-row .db-val { font-size:0.8rem; color:#e2e8f0; font-weight:600; min-width:36px; text-align:center; }
.db-btn { width:34px; height:30px; font-size:0.78rem; font-weight:700; padding:0; display:inline-flex; align-items:center; justify-content:center; border-radius:5px; }
.db-btn.m { background:#ef4444; }
.db-btn.m:hover { background:#dc2626; }
.db-btn.p { background:#22c55e; }
.db-btn.p:hover { background:#16a34a; }
.db-stale { opacity:0.5; border-color:#ef4444 !important; }
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
    <button class="tab active" onclick="switchTab('joystick')">Joystick</button>
    <button class="tab" onclick="switchTab('dash')">Dashboard</button>
    <button class="tab" onclick="switchTab('modules')">Modules</button>
    <button class="tab" onclick="switchTab('mapping')">Motor Mapping</button>
    <button class="tab" onclick="switchTab('labels')">Labels</button>
    <button class="tab" onclick="switchTab('btnout')">Button Outputs</button>
    <button class="tab" onclick="switchTab('canbtns')">CAN Buttons</button>
    <button class="tab" onclick="switchTab('dbtune')">Deadband</button>
    <button class="tab" onclick="switchTab('canout')">CAN Output</button>
    <button class="tab" onclick="switchTab('mottest')">Motor Test</button>
    <button class="tab" onclick="switchTab('led')">LED Test</button>
    <button class="tab" onclick="switchTab('ota')">OTA Update</button>
</div>

<div id="joystick" class="panel active">
    <div class="card" style="padding:10px">
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;padding:6px;border-radius:14px;background:#22272b;margin-bottom:10px">
            <button class="joy-mode-btn active" onclick="joySwitchScreen('mainScreen',this)" style="min-height:48px;border-radius:12px;background:linear-gradient(180deg,#69d2c5,#369b90);font-weight:900;font-size:14px;color:#fff;border:0;cursor:pointer">F&#x151; vez&#xe9;rl&#xe9;s</button>
            <button class="joy-mode-btn" onclick="joySwitchScreen('foldScreen',this)" style="min-height:48px;border-radius:12px;background:transparent;font-weight:900;font-size:14px;color:#e2e8f0;border:0;cursor:pointer">Keret nyit&#xe1;s / csuk&#xe1;s</button>
        </div>

        <div id="mainScreen" class="joy-screen">
            <div style="display:grid;grid-template-columns:repeat(3,1fr);grid-template-rows:repeat(3,minmax(80px,1fr));gap:8px">
                <button class="joy-btn momentary" data-cmd="bal-szarny-fel" style="min-height:80px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:clamp(13px,3.5vw,20px);font-weight:900;color:#e2e8f0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;padding:6px;cursor:pointer;touch-action:manipulation"><span style="font-size:clamp(24px,6vw,42px)">&#x2196;</span><span>Bal sz&#xe1;rny fel</span></button>
                <button class="joy-btn momentary" data-cmd="keretmagassag-fel" style="min-height:80px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:clamp(13px,3.5vw,20px);font-weight:900;color:#e2e8f0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;padding:6px;cursor:pointer;touch-action:manipulation"><span style="font-size:clamp(24px,6vw,42px)">&#x25B2;</span><span>Keretmagass&#xe1;g fel</span></button>
                <button class="joy-btn momentary" data-cmd="jobb-szarny-fel" style="min-height:80px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:clamp(13px,3.5vw,20px);font-weight:900;color:#e2e8f0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;padding:6px;cursor:pointer;touch-action:manipulation"><span style="font-size:clamp(24px,6vw,42px)">&#x2197;</span><span>Jobb sz&#xe1;rny fel</span></button>

                <button class="joy-btn momentary" data-cmd="billentes-bal" style="min-height:80px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:clamp(13px,3.5vw,20px);font-weight:900;color:#e2e8f0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;padding:6px;cursor:pointer;touch-action:manipulation"><span style="font-size:clamp(24px,6vw,42px)">&#x21B6;</span><span>Billent&#xe9;s bal</span></button>
                <button class="joy-btn" disabled style="min-height:80px;border-radius:50%;aspect-ratio:1/1;width:min(80%,140px);justify-self:center;align-self:center;background:radial-gradient(circle at 35% 30%,#626d73,#2d3337 60%,#22272a);border:6px solid #1d2124;font-weight:900;font-size:clamp(12px,3.5vw,20px);color:#666;opacity:0.5;display:flex;align-items:center;justify-content:center;cursor:not-allowed">MASTER</button>
                <button class="joy-btn momentary" data-cmd="billentes-jobb" style="min-height:80px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:clamp(13px,3.5vw,20px);font-weight:900;color:#e2e8f0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;padding:6px;cursor:pointer;touch-action:manipulation"><span style="font-size:clamp(24px,6vw,42px)">&#x21B7;</span><span>Billent&#xe9;s jobb</span></button>

                <button class="joy-btn momentary" data-cmd="bal-szarny-le" style="min-height:80px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:clamp(13px,3.5vw,20px);font-weight:900;color:#e2e8f0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;padding:6px;cursor:pointer;touch-action:manipulation"><span style="font-size:clamp(24px,6vw,42px)">&#x2199;</span><span>Bal sz&#xe1;rny le</span></button>
                <button class="joy-btn momentary" data-cmd="keretmagassag-le" style="min-height:80px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:clamp(13px,3.5vw,20px);font-weight:900;color:#e2e8f0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;padding:6px;cursor:pointer;touch-action:manipulation"><span style="font-size:clamp(24px,6vw,42px)">&#x25BC;</span><span>Keretmagass&#xe1;g le</span></button>
                <button class="joy-btn momentary" data-cmd="jobb-szarny-le" style="min-height:80px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:clamp(13px,3.5vw,20px);font-weight:900;color:#e2e8f0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;padding:6px;cursor:pointer;touch-action:manipulation"><span style="font-size:clamp(24px,6vw,42px)">&#x2198;</span><span>Jobb sz&#xe1;rny le</span></button>
            </div>

            <div style="display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin-top:8px">
                <button class="joy-btn" disabled style="min-height:68px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-weight:900;font-size:14px;color:#666;opacity:0.5;cursor:not-allowed;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px"><span style="font-size:24px">&#x25A6;</span><span>Szakaszok</span></button>
                <button class="joy-btn" disabled style="min-height:68px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-weight:900;font-size:14px;color:#666;opacity:0.5;cursor:not-allowed;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px"><span style="font-size:24px">A</span><span>Automata</span></button>
                <button class="joy-btn" disabled style="min-height:68px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-weight:900;font-size:14px;color:#666;opacity:0.5;cursor:not-allowed;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px"><span style="font-size:24px">&#x25A0;</span><span>STOP</span></button>
            </div>
        </div>

        <div id="foldScreen" class="joy-screen" style="display:none">
            <div style="padding:10px;border-radius:16px;background:rgba(0,0,0,.14);border:1px solid rgba(255,255,255,.05)">
                <h3 style="margin:0 0 8px;font-size:clamp(15px,4vw,20px)">Keret nyit&#xe1;s / csuk&#xe1;s</h3>
                <div style="display:grid;grid-template-columns:1fr 50px 1fr;gap:7px;margin-bottom:6px;color:#94a3b8;font-size:12px;font-weight:800;text-align:center">
                    <span>F&#x151;keret</span><span>Egy&#xfc;tt</span><span>Seg&#xe9;dkeret</span>
                </div>
                <div style="display:grid;grid-template-columns:1fr 50px 1fr;gap:7px;margin-bottom:9px">
                    <button class="joy-btn momentary" data-cmd="fokeret-nyit" style="min-height:96px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:16px;font-weight:900;color:#e2e8f0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;padding:6px;cursor:pointer;touch-action:manipulation"><span style="font-size:28px">&#x219E;</span><span>Nyit</span></button>
                    <button class="joy-btn momentary" data-cmd="mindketto-nyit" style="min-height:96px;border-radius:12px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:10px;font-weight:900;color:#e2e8f0;padding:4px;cursor:pointer;touch-action:manipulation;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px"><span style="font-size:20px">&#x2194;</span><span>NYIT</span></button>
                    <button class="joy-btn momentary" data-cmd="segedkeret-nyit" style="min-height:96px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:16px;font-weight:900;color:#e2e8f0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;padding:6px;cursor:pointer;touch-action:manipulation"><span style="font-size:28px">&#x21A0;</span><span>Nyit</span></button>
                </div>
                <div style="display:grid;grid-template-columns:1fr 50px 1fr;gap:7px">
                    <button class="joy-btn momentary" data-cmd="fokeret-csuk" style="min-height:96px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:16px;font-weight:900;color:#e2e8f0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;padding:6px;cursor:pointer;touch-action:manipulation"><span style="font-size:28px">&#x2192;</span><span>Csuk</span></button>
                    <button class="joy-btn momentary" data-cmd="mindketto-csuk" style="min-height:96px;border-radius:12px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:10px;font-weight:900;color:#e2e8f0;padding:4px;cursor:pointer;touch-action:manipulation;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px"><span style="font-size:20px">&#x21E5;</span><span>CSUK</span></button>
                    <button class="joy-btn momentary" data-cmd="segedkeret-csuk" style="min-height:96px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:16px;font-weight:900;color:#e2e8f0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;padding:6px;cursor:pointer;touch-action:manipulation"><span style="font-size:28px">&#x2190;</span><span>Csuk</span></button>
                </div>
            </div>
            <div style="margin-top:10px;padding:12px;border-radius:16px;background:rgba(0,0,0,.14);border:1px solid rgba(255,255,255,.05)">
                <h3 style="margin:0 0 8px;font-size:clamp(15px,4vw,20px)">Keretmagass&#xe1;g</h3>
                <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px">
                    <button class="joy-btn momentary" data-cmd="keretmagassag-le" style="min-height:88px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:16px;font-weight:900;color:#e2e8f0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;padding:6px;cursor:pointer;touch-action:manipulation"><span style="font-size:28px">&#x25BC;</span><span>Le</span></button>
                    <button class="joy-btn momentary" data-cmd="keretmagassag-fel" style="min-height:88px;border-radius:14px;background:linear-gradient(180deg,#3d454a,#30363a);border:1px solid rgba(255,255,255,.08);font-size:16px;font-weight:900;color:#e2e8f0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;padding:6px;cursor:pointer;touch-action:manipulation"><span style="font-size:28px">&#x25B2;</span><span>Fel</span></button>
                </div>
            </div>
        </div>
    </div>
</div>

<div id="dash" class="panel">
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
    <div class="grid2">
        <div class="card">
            <h3>Joystick 3 (0x23)</h3>
            <div id="joy3_pots"></div>
            <div class="info-row"><span>Buttons:</span><span id="joy3_btns">--</span></div>
        </div>
        <div class="card">
            <h3>Joystick 4 (0x24)</h3>
            <div id="joy4_pots"></div>
            <div class="info-row"><span>Buttons:</span><span id="joy4_btns">--</span></div>
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
    <div class="card" style="margin-top:16px">
        <h3>Virtual Joystick Assignment</h3>
        <p style="color:#94a3b8;font-size:0.85rem;margin:0 0 12px">Assign each virtual joystick function to an output pair. These buttons on the Joystick tab will directly drive the assigned outputs.</p>
        <div id="joyAssignList"></div>
        <div style="margin-top:12px;display:flex;gap:8px;">
            <button onclick="saveJoyAssign()">Save</button>
            <button class="secondary" onclick="fetchJoyAssign()">Refresh</button>
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

<div id="mottest" class="panel">
    <div class="card">
        <h3>Motor Output Testing</h3>
        <p style="color:#94a3b8;font-size:0.85rem;margin:0 0 12px">Manually control PWM outputs to test solenoids. Test mode auto-disables after 10 seconds of inactivity.</p>
        <div style="display:flex;align-items:center;gap:12px;margin-bottom:16px">
            <label style="display:flex;align-items:center;gap:6px;cursor:pointer">
                <input type="checkbox" id="testModeEn" onchange="toggleTestMode()" style="width:18px;height:18px">
                <span>Enable Test Mode</span>
            </label>
            <span id="testTimeout" style="color:#f59e0b;font-size:0.85rem"></span>
        </div>
        <div id="testOutList"></div>
    </div>
</div>

<div id="led" class="panel">
    <div class="card">
        <h3>WS2812 LED Color Tester</h3>
        <p style="color:#94a3b8;font-size:0.85rem;margin:0 0 12px">Send a color command to the LED on this ECU via CAN.</p>
        <div style="display:flex;align-items:center;gap:20px;margin-bottom:16px">
            <div>
                <label style="display:block;margin-bottom:4px;color:#94a3b8">Pick Color:</label>
                <input type="color" id="ledColor" value="#ff0000" style="width:80px;height:40px;border:none;cursor:pointer">
            </div>
            <div id="ledPreview" style="width:80px;height:80px;border-radius:12px;background:#ff0000;border:2px solid #475569"></div>
            <div>
                <div class="info-row"><span>R:</span><span id="ledR">255</span></div>
                <div class="info-row"><span>G:</span><span id="ledG">0</span></div>
                <div class="info-row"><span>B:</span><span id="ledB">0</span></div>
            </div>
        </div>
        <div style="display:flex;gap:8px;flex-wrap:wrap">
            <button onclick="setLed()">Set LED Color</button>
            <button class="secondary" onclick="setLedColor(255,0,0)">Red</button>
            <button class="secondary" onclick="setLedColor(0,255,0)">Green</button>
            <button class="secondary" onclick="setLedColor(0,0,255)">Blue</button>
            <button class="secondary" onclick="setLedColor(255,255,255)">White</button>
            <button class="secondary" onclick="setLedColor(0,0,0)">Off</button>
        </div>
        <div style="margin-top:16px">
            <label style="display:block;margin-bottom:4px;color:#94a3b8">Target:</label>
            <select id="ledTarget">
                <option value="255">Broadcast (all ECUs)</option>
                <option value="33">Joystick 1 (0x21)</option>
                <option value="34">Joystick 2 (0x22)</option>
            </select>
        </div>
        <div style="margin-top:16px;padding:12px;background:#0f172a;border-radius:8px;font-family:monospace;font-size:0.8rem">
            <div>CAN ID: <span id="ledCanId">0x1820FF00</span></div>
            <div>Data: <span id="ledCanData">FF 00 00 00 00 00 00 00</span></div>
        </div>
    </div>
</div>

<div id="ota" class="panel">
    <div class="card">
        <h3>Firmware Update</h3>
        <p style="color:#94a3b8;font-size:0.85rem;margin:0 0 12px">For best results, close other tabs before uploading.</p>
        <label style="display:block;margin:8px 0 4px;color:#94a3b8">Select firmware (.bin)</label>
        <input type="file" id="firmware" accept=".bin" style="width:100%;padding:8px;background:#0f172a;border:1px solid #475569;border-radius:6px;color:#e2e8f0">
        <button onclick="startUpload()" style="margin-top:12px;width:auto">Update Firmware</button>
        <div class="bar-track" style="margin-top:12px;height:8px"><div class="bar-fill" id="prog" style="width:0%"></div></div>
        <div id="otaStatus" style="margin-top:8px;color:#94a3b8"></div>
    </div>
</div>

<div id="labels" class="panel">
    <div class="card">
        <h3>Joystick Labels</h3>
        <p style="color:#94a3b8;font-size:0.85rem;margin:0 0 12px">Assign position labels and axis names to each joystick for readable dropdowns.</p>
        <div id="joyLabelsList"></div>
    </div>
    <div class="card">
        <h3>Output Labels</h3>
        <p style="color:#94a3b8;font-size:0.85rem;margin:0 0 12px">Assign names to each PWM output channel.</p>
        <div id="outLabelsList"></div>
    </div>
    <div style="margin-top:12px;display:flex;gap:8px;">
        <button onclick="saveLabels()">Save Labels</button>
        <button class="secondary" onclick="fetchLabels()">Refresh</button>
    </div>
</div>

<div id="btnout" class="panel">
    <div class="card">
        <h3>Button-to-Output Rules</h3>
        <p style="color:#94a3b8;font-size:0.85rem;margin:0 0 12px">Map buttons to PWM outputs. Each output has a "+" (maxPWM) and "-" (minPWM) direction for bidirectional control.</p>
        <div id="btnOutList"></div>
    </div>
</div>

<div id="canbtns" class="panel">
    <div class="card">
        <h3>Custom CAN Buttons</h3>
        <p style="color:#94a3b8;font-size:0.85rem;margin:0 0 12px">Define virtual buttons triggered by specific CAN messages. Each button matches a CAN ID + byte + bit. These can be assigned to outputs in Button Outputs.</p>
        <div id="customBtnList"></div>
        <div style="margin-top:12px;display:flex;gap:8px;">
            <button onclick="saveCustomBtns()">Save</button>
            <button class="secondary" onclick="fetchCustomBtns()">Refresh</button>
        </div>
    </div>
</div>

<div id="dbtune" class="panel">
    <div class="card">
        <h3>Deadband Tuning</h3>
        <p style="color:#94a3b8;font-size:0.75rem;margin:0 0 12px">Adjust deadband per joystick potentiometer. The yellow zone shows the deadband range. Move the joystick past the deadband edges to activate output. Red = data missing.</p>
        <div id="dbTuneList"></div>
        <div style="margin-top:14px;display:flex;gap:8px;align-items:center">
            <button onclick="saveDeadband()">Save All</button>
            <span id="dbSaveStatus" style="color:#94a3b8;font-size:0.8rem"></span>
        </div>
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

// ---- Joystick UI ----
function joySwitchScreen(screenId, btn) {
    document.querySelectorAll('.joy-screen').forEach(s => s.style.display = 'none');
    document.getElementById(screenId).style.display = 'block';
    document.querySelectorAll('.joy-mode-btn').forEach(b => {
        b.style.background = 'transparent';
        b.classList.remove('active');
    });
    btn.style.background = 'linear-gradient(180deg,#69d2c5,#369b90)';
    btn.classList.add('active');
}

// Combined commands that trigger two functions
const joyCombined = {
    'mindketto-nyit': ['fokeret-nyit', 'segedkeret-nyit'],
    'mindketto-csuk': ['fokeret-csuk', 'segedkeret-csuk']
};

// Global abort controller for joy commands to prevent queuing
let joyAbortController = null;
let joyHeartbeatTimer = null;

function joySendCmd(cmd, active) {
    // Abort any pending request to prevent queuing
    if (joyAbortController) {
        joyAbortController.abort();
    }
    joyAbortController = new AbortController();
    
    // Handle combined commands
    if (joyCombined[cmd]) {
        joyCombined[cmd].forEach(c => joySendSingle(c, active));
    } else {
        joySendSingle(cmd, active);
    }
}

function joySendSingle(cmd, active) {
    fetch('/api/joycmd', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({cmd: cmd, active: active}),
        signal: joyAbortController.signal
    }).catch(e => {
        if (e.name !== 'AbortError') {
            console.error('joycmd err', e);
        }
    });
}

function startJoyHeartbeat(cmd) {
    stopJoyHeartbeat();
    joyHeartbeatTimer = setInterval(() => {
        joySendCmd(cmd, true);
    }, 1000); // Send heartbeat every 1 second
}

function stopJoyHeartbeat() {
    if (joyHeartbeatTimer) {
        clearInterval(joyHeartbeatTimer);
        joyHeartbeatTimer = null;
    }
}

// Attach momentary button handlers - simple and responsive
 document.querySelectorAll('.joy-btn.momentary').forEach(btn => {
    let pressed = false;

    const press = e => {
        e.preventDefault();
        if (pressed) return;
        pressed = true;
        btn.style.background = 'linear-gradient(180deg,#5bc7bb,#36978d)';
        try { btn.setPointerCapture(e.pointerId); } catch(ex) {}
        joySendCmd(btn.dataset.cmd, true);
        startJoyHeartbeat(btn.dataset.cmd); // Keep alive while holding
    };
    
    const release = e => {
        e.preventDefault();
        if (!pressed) return;
        pressed = false;
        btn.style.background = 'linear-gradient(180deg,#3d454a,#30363a)';
        stopJoyHeartbeat();
        joySendCmd(btn.dataset.cmd, false);
    };
    
    btn.addEventListener('pointerdown', press);
    btn.addEventListener('pointerup', release);
    btn.addEventListener('pointercancel', release);
    btn.addEventListener('pointerleave', release);
    btn.addEventListener('lostpointercapture', release);
});

function barHtml(id, label, value, max, color) {
    const pct = Math.min(100, Math.max(0, (value / max) * 100)).toFixed(1);
    return `<div class="info-row"><span>${label}</span><span>${value.toFixed(0)}</span></div>
            <div class="bar-track"><div class="bar-fill" id="${id}" style="width:${pct}%;background:${color||''}"></div></div>`;
}

function renderJoysticks() {
    const joyAddrs = [0x21, 0x22, 0x23, 0x24];
    const joyNames = ['1', '2', '3', '4'];
    for (let ji = 0; ji < 4; ji++) {
        const addr = joyAddrs[ji];
        const joy = gState.joy && gState.joy[addr] ? gState.joy[addr] : { pots: [512,512,512,512], btns: 0, age: 9999 };
        let h = ''; for (let i = 0; i < 4; i++) h += barHtml(`j${ji}p${i}`, `Pot ${i+1}`, joy.pots[i] || 0, 1023, 'linear-gradient(90deg,#f59e0b,#fbbf24)');
        document.getElementById('joy' + (ji+1) + '_pots').innerHTML = h;
        const btnNames = ['Btn1', 'Btn2', 'Btn3', 'Btn4'];
        let btnStr = '';
        for (let b = 0; b < 4; b++) { if (joy.btns & (1 << b)) btnStr += (btnStr ? ' ' : '') + btnNames[b]; }
        document.getElementById('joy' + (ji+1) + '_btns').textContent = btnStr || 'None';
    }
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

let gDB = {};

function renderDeadbandTuning() {
    const axes = gConfig.axes || [];
    const pots = {};
    for (let i = 0; i < 16; i++) {
        const a = axes[i];
        if (!a || !a.sourceAddress) continue;
        const key = a.sourceAddress + '_' + a.potIndex;
        if (!pots[key]) pots[key] = { src: a.sourceAddress, pot: a.potIndex, dbMin: a.deadbandMin || 307, dbMax: a.deadbandMax || 717, axes: [] };
        pots[key].axes.push(i);
        if (gDB[key]) {
            pots[key].dbMin = gDB[key].min;
            pots[key].dbMax = gDB[key].max;
        }
    }
    // Also add pots from live joystick state not already shown
    if (gState.joy) {
        for (const srcStr in gState.joy) {
            const src = parseInt(srcStr);
            const joy = gState.joy[srcStr];
            if (joy && joy.pots) {
                for (let pi = 0; pi < 4; pi++) {
                    const key = src + '_' + pi;
                    if (!pots[key]) pots[key] = { src: src, pot: pi, dbMin: 307, dbMax: 717, axes: [] };
                }
            }
        }
    }
    const keys = Object.keys(pots).sort();
    let h = '';
    for (const key of keys) {
        const p = pots[key];
        const srcLabel = (() => {
            for (const jl of gLabels.joysticks) {
                if (jl.sourceAddress === p.src) {
                    const pos = jl.position || ('Joy' + (gLabels.joysticks.indexOf(jl)+1));
                    const ax = jl.axes[p.pot] || ('Axis' + (p.pot+1));
                    return pos + ' ' + ax;
                }
            }
            return '0x' + p.src.toString(16).toUpperCase() + ' Pot' + (p.pot+1);
        })();
        const joy = gState.joy && gState.joy[p.src];
        const online = joy && (joy.age < 3);
        const pv = joy ? (joy.pots[p.pot] || 0) : 0;
        const pct = (pv / 1023 * 100).toFixed(1);
        const dbMinP = (p.dbMin / 1023 * 100).toFixed(1);
        const dbMaxP = (p.dbMax / 1023 * 100).toFixed(1);
        const ctr = Math.round((p.dbMin + p.dbMax) / 2);
        const off = ctr - 512;
        const offPct = ((off / 512) * 100).toFixed(1);
        const offClr = Math.abs(off) < 10 ? '#22c55e' : (Math.abs(off) < 30 ? '#f59e0b' : '#ef4444');
        const staleCls = online ? '' : ' db-stale';
        const dir = !online ? 'N/A' : pv < p.dbMin ? 'REV' : pv > p.dbMax ? 'FWD' : 'DEAD';
        const dirClr = !online ? '#ef4444' : pv < p.dbMin ? '#3b82f6' : pv > p.dbMax ? '#22c55e' : '#f59e0b';
        const sa = p.src, pi = p.pot;
        h += '<div class="card' + staleCls + '" style="margin-bottom:10px">';
        h += '<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:6px">';
        h += '<span style="font-weight:600;color:#38bdf8">' + srcLabel + '</span>';
        h += '<span style="color:' + (online ? '#e2e8f0' : '#ef4444') + ';font-weight:600">' + (online ? pv : 'NO DATA') + '</span>';
        h += '<span style="color:' + dirClr + ';font-weight:600;font-size:0.8rem">' + dir + '</span>';
        h += '<span style="color:' + offClr + ';font-size:0.75rem">Off: ' + offPct + '%</span>';
        h += '</div>';
        h += '<div class="db-bar"><div class="db-zone" style="left:' + dbMinP + '%;width:' + Math.max(0.5, (p.dbMax - p.dbMin) / 1023 * 100) + '%"></div>';
        h += '<div class="db-ptr' + (online ? '' : ' stale') + '" style="left:' + pct + '%"></div></div>';
        h += '<div style="display:flex;gap:16px;flex-wrap:wrap">';
        h += '<div class="db-step-row"><span class="db-lbl">Min</span>';
        h += '<button class="db-btn m" onclick="adjDB(' + sa + ',' + pi + ',\'min\',-10)">-10</button>';
        h += '<button class="db-btn m" onclick="adjDB(' + sa + ',' + pi + ',\'min\',-1)">-1</button>';
        h += '<span class="db-val">' + p.dbMin + '</span>';
        h += '<button class="db-btn p" onclick="adjDB(' + sa + ',' + pi + ',\'min\',1)">+1</button>';
        h += '<button class="db-btn p" onclick="adjDB(' + sa + ',' + pi + ',\'min\',10)">+10</button></div>';
        h += '<div class="db-step-row"><span class="db-lbl">Max</span>';
        h += '<button class="db-btn m" onclick="adjDB(' + sa + ',' + pi + ',\'max\',-10)">-10</button>';
        h += '<button class="db-btn m" onclick="adjDB(' + sa + ',' + pi + ',\'max\',-1)">-1</button>';
        h += '<span class="db-val">' + p.dbMax + '</span>';
        h += '<button class="db-btn p" onclick="adjDB(' + sa + ',' + pi + ',\'max\',1)">+1</button>';
        h += '<button class="db-btn p" onclick="adjDB(' + sa + ',' + pi + ',\'max\',10)">+10</button></div>';
        h += '</div></div>';
    }
    if (!keys.length) h = '<div style="color:#64748b;text-align:center;padding:16px">No pots detected. Connect a joystick or configure axes in Motor Mapping.</div>';
    document.getElementById('dbTuneList').innerHTML = h;
}

function adjDB(src, pot, which, delta) {
    const key = src + '_' + pot;
    const axes = gConfig.axes || [];
    if (!gDB[key]) {
        // Find first axis with this src+pot to get current deadband
        for (let i = 0; i < 16; i++) {
            const a = axes[i];
            if (a && a.sourceAddress === src && a.potIndex === pot) {
                gDB[key] = { min: a.deadbandMin || 307, max: a.deadbandMax || 717 };
                break;
            }
        }
    }
    // If still no entry (unmapped pot from live state), use defaults
    if (!gDB[key]) gDB[key] = { min: 307, max: 717 };
    if (which === 'min') gDB[key].min = Math.max(0, Math.min(1023, gDB[key].min + delta));
    else gDB[key].max = Math.max(0, Math.min(1023, gDB[key].max + delta));
    renderDeadbandTuning();
    document.getElementById('dbSaveStatus').textContent = 'Unsaved changes';
    document.getElementById('dbSaveStatus').style.color = '#f59e0b';
}

async function saveDeadband() {
    // Re-fetch current config to avoid stale data
    try {
        const cr = await fetch('/api/config');
        gConfig = await cr.json();
    } catch(e) {}
    const axes = gConfig.axes || [];
    const updated = [];
    let changed = 0;
    // Copy axes from config
    for (let i = 0; i < 16; i++) {
        const a = axes[i] || { sourceAddress:0, potIndex:0, outputChannel:i, deadbandMin:307, deadbandMax:717, pwmMin:20, pwmMax:100, flags:0, buttonGate:0 };
        a.axisIdx = i;
        const key = a.sourceAddress + '_' + a.potIndex;
        if (gDB[key]) {
            a.deadbandMin = gDB[key].min;
            a.deadbandMax = gDB[key].max;
            changed++;
        }
        updated.push(a);
    }
    // Save unmapped pots (from live joystick) to unused axis slots
    for (const key in gDB) {
        const alreadyInAxes = updated.some(a => (a.sourceAddress + '_' + a.potIndex) === key);
        if (!alreadyInAxes) {
            const parts = key.split('_');
            const src = parseInt(parts[0]), pot = parseInt(parts[1]);
            // Find first unused axis slot (sourceAddress=0 or disabled with no real assignment)
            const slotIdx = updated.findIndex(a => !a.sourceAddress && !(a.flags & 1));
            if (slotIdx >= 0) {
                updated[slotIdx].sourceAddress = src;
                updated[slotIdx].potIndex = pot;
                updated[slotIdx].deadbandMin = gDB[key].min;
                updated[slotIdx].deadbandMax = gDB[key].max;
                updated[slotIdx].flags = 1;  // enabled for tracking, no output channel change
                changed++;
            }
        }
    }
    console.log('Saving deadband:', changed, 'overrides', JSON.stringify(gDB));
    try {
        const r = await fetch('/api/config', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({axes:updated}) });
        if (r.ok) {
            gDB = {};
            document.getElementById('dbSaveStatus').textContent = 'Saved! (' + changed + ' pots)';
            document.getElementById('dbSaveStatus').style.color = '#22c55e';
            await fetchConfig();
        } else {
            document.getElementById('dbSaveStatus').textContent = 'Failed (HTTP ' + r.status + ')';
            document.getElementById('dbSaveStatus').style.color = '#ef4444';
        }
    } catch(e) {
        document.getElementById('dbSaveStatus').textContent = 'Network error';
        document.getElementById('dbSaveStatus').style.color = '#ef4444';
    }
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
    let h = '<div class="axis-row header">';
    h += '<div title="Axis index (0-15)">#</div>';
    h += '<div title="Enable this axis">En</div>';
    h += '<div title="Joystick axis source (label-based)">Source</div>';
    h += '<div title="Output channel">Output</div>';
    h += '<div title="Minimum PWM output duty (0-255)">PWM Min</div>';
    h += '<div title="Maximum PWM output duty (0-255)">PWM Max</div>';
    h += '<div title="Bidirectional: uses paired channels for fwd/rev">Bidir</div>';
    h += '<div title="Invert: swaps forward/reverse channels">Inv</div>';
    h += '<div title="Button gate: axis only active when button is pressed">Gate</div>';
    h += '<div title="Exponential curvature">Curve</div>';
    h += '</div>';
    h += '<div style="padding:6px 8px;color:#94a3b8;font-size:0.72rem;grid-column:1/-1;line-height:1.5">';
    h += '<b>Source:</b> Select joystick axis using labels (set in Labels tab). ';
    h += '<b>Output:</b> PWM output channel (set labels in Labels tab). ';
    h += '<b>Gate:</b> Activate axis only when button is pressed.';
    h += '</div>';
    for (let i = 0; i < 16; i++) {
        const a = axes[i] || { sourceAddress: 0, potIndex: 0, outputChannel: 0, deadbandMin: 307, deadbandMax: 717, pwmMin: 20, pwmMax: 100, flags: 0, buttonGate: 0, curveExp: 2 };
        const en = (a.flags & 1) ? 'checked' : '';
        const bidir = (a.flags & 2) ? 'checked' : '';
        const invert = (a.flags & 4) ? 'checked' : '';
        const curve = a.curveExp || 2;
        const outNum = Math.floor(a.outputChannel / 2) + 1; // channel 0,1 = Out 1; channel 2,3 = Out 2; etc.
        // Output dropdown with labels (8 pair-based outputs)
        let outOpts = '';
        for (let p = 0; p < 8; p++) {
            const baseCh = p * 2; // pair base channel (even: 0,2,4,6,8,10,12,14)
            const outNumOpt = p + 1; // Out 1 through Out 8
            const ol = gLabels.outputs[baseCh];
            const lbl = (ol && ol.label) ? ol.label : ('Out ' + outNumOpt);
            outOpts += '<option value="' + (baseCh + 1) + '" ' + (outNum==outNumOpt?'selected':'') + '>' + lbl + '</option>';
        }
        const axisDropdown = buildAxisDropdown(a.sourceAddress, a.potIndex);
        const gateDropdown = buildGateDropdown(a.buttonGate);
        h += '<div class="axis-row">';
        h += '<div>' + i + '</div>';
        h += '<div><input type="checkbox" id="a' + i + '_en" ' + en + ' onchange="checkChannelConflicts()"></div>';
        h += '<div><select id="a' + i + '_src" style="min-width:120px">' + axisDropdown + '</select></div>';
        h += '<div><select id="a' + i + '_ch" onchange="checkChannelConflicts()" style="width:80px">' + outOpts + '</select></div>';
        h += '<div><input type="number" id="a' + i + '_pwmin" value="' + a.pwmMin + '" min="0" max="255" style="width:60px"></div>';
        h += '<div><input type="number" id="a' + i + '_pwmax" value="' + a.pwmMax + '" min="0" max="255" style="width:60px"></div>';
        h += '<div><input type="checkbox" id="a' + i + '_bidir" ' + bidir + ' onchange="checkChannelConflicts()"></div>';
        h += '<div><input type="checkbox" id="a' + i + '_inv" ' + invert + '></div>';
        h += '<div><select id="a' + i + '_bgate" style="min-width:100px">' + gateDropdown + '</select></div>';
        h += '<div><select id="a' + i + '_curve" style="min-width:60px"><option value="2" ' + (curve==2?'selected':'') + '>1.0x</option><option value="3" ' + (curve==3?'selected':'') + '>1.5x</option><option value="4" ' + (curve==4?'selected':'') + '>2.0x</option><option value="5" ' + (curve==5?'selected':'') + '>2.5x</option><option value="6" ' + (curve==6?'selected':'') + '>3.0x</option></select></div>';
        h += '</div>';
    }
    h += '<div id="channelWarnings" style="grid-column:1/-1;padding:8px;margin-top:8px;"></div>';
    document.getElementById('axisList').innerHTML = h;
    checkChannelConflicts();
}

function checkChannelConflicts() {
    const warnings = [];
    const outputMap = {};  // output number -> [axis indices using it]
    for (let i = 0; i < 16; i++) {
        const enEl = document.getElementById('a' + i + '_en');
        const outEl = document.getElementById('a' + i + '_ch');
        if (!enEl || !outEl) continue;
        if (!enEl.checked) continue;  // Skip disabled axes
        const outNum = parseInt(outEl.value);  // 1-16
        // Track output usage (each output uses 2 channels: fwd+rev)
        if (!outputMap[outNum]) outputMap[outNum] = [];
        outputMap[outNum].push(i);
    }
    // Check for duplicate output usage
    for (const outNum in outputMap) {
        if (outputMap[outNum].length > 1) {
            const axes = outputMap[outNum].join(', ');
            warnings.push({ type: 'error', msg: `Output ${outNum} used by multiple active axes: ${axes}` });
        }
    }
    // Render warnings
    const warnDiv = document.getElementById('channelWarnings');
    if (warnDiv) {
        if (warnings.length === 0) {
            warnDiv.innerHTML = '';
        } else {
            let h = '';
            for (const w of warnings) {
                const color = w.type === 'error' ? '#ef4444' : '#f59e0b';
                const icon = w.type === 'error' ? '✖' : '⚠';
                h += `<div style="color:${color};font-size:0.85rem;margin:4px 0;"><b>${icon}</b> ${w.msg}</div>`;
            }
            warnDiv.innerHTML = h;
        }
    }
    return warnings.length === 0;
}

// ---- Virtual Joystick Assignment ----
const joyFuncNames = [
    'Billent\xe9s (bal / jobb)',
    'Bal sz\xe1;rny (fel / le)',
    'Jobb sz\xe1;rny (fel / le)',
    'Keretmagass\xe1;g (fel / le)',
    'F\u0151keret (nyit / csuk)',
    'Seg\xe9;dkeret (nyit / csuk)'
];

function renderJoyAssign() {
    // Fetch current mappings from backend
    fetch('/api/joymapping').then(r => r.json()).then(data => {
        const m = data.mappings || [];
        const labels = data.labels || [];
        let h = '';
        for (let pi = 0; pi < joyFuncNames.length; pi++) {
            const posIdx = pi * 2; // even index = pos direction
            const curCh = m[posIdx] ? m[posIdx].ch : 0;
            // Build output dropdown (8 pair-based options with labels)
            // "Out N" means pair starting at channel (N-1)*2
            let opts = '<option value="255">Off</option>';
            for (let p = 0; p < 8; p++) {
                const baseCh = p * 2; // pair base channel (even: 0,2,4,6,8,10,12,14)
                const outNum = p + 1; // Out 1 through Out 8
                const ol = gLabels.outputs[baseCh];
                const lbl = (ol && ol.label) ? ol.label : ('Out ' + outNum);
                const sel = (curCh === baseCh) ? ' selected' : '';
                opts += '<option value="' + baseCh + '"' + sel + '>' + lbl + '</option>';
            }
            h += '<div style="display:flex;align-items:center;gap:10px;padding:8px 0;border-bottom:1px solid #1e293b">';
            h += '<span style="font-weight:600;font-size:0.85rem;min-width:180px">' + joyFuncNames[pi] + '</span>';
            h += '<span style="color:#94a3b8;font-size:0.8rem">\u2192</span>';
            h += '<select id="ja_' + pi + '" style="padding:4px 8px;background:#0f172a;border:1px solid #475569;border-radius:4px;color:#e2e8f0;font-size:0.8rem">' + opts + '</select>';
            h += '</div>';
        }
        document.getElementById('joyAssignList').innerHTML = h;
    }).catch(e => console.error('joyAssign load err', e));
}

async function saveJoyAssign() {
    // Build 20-entry mapping array
    const mappings = [];
    for (let i = 0; i < 20; i++) mappings.push({ch: 255, inv: false});
    for (let pi = 0; pi < joyFuncNames.length; pi++) {
        const ch = parseInt(document.getElementById('ja_' + pi).value);
        if (isNaN(ch) || ch > 255) continue;
        const posIdx = pi * 2;
        const negIdx = pi * 2 + 1;
        mappings[posIdx] = {ch: ch, inv: false};
        mappings[negIdx] = {ch: ch, inv: false};
    }
    try {
        await fetch('/api/joymapping', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({mappings: mappings})
        });
        setStatus('Virtual joystick assignment saved!', 'success');
    } catch(e) {
        setStatus('Save failed', 'error');
    }
}

function fetchJoyAssign() { renderJoyAssign(); }

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
        renderDeadbandTuning();
        renderSol();
        renderModules();
    } catch(e) {}
}

async function fetchConfig() {
    try {
        const r = await fetch('/api/config');
        gConfig = await r.json();
        renderMapping();
        renderJoyAssign();
        renderDeadbandTuning();
    } catch(e) {}
}

async function identify(addr) {
    try {
        await fetch('/api/identify', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({target: addr}) });
        setStatus('Identify sent to 0x' + addr.toString(16).toUpperCase(), 'success');
    } catch(e) { setStatus('Failed', 'error'); }
}

// LED Tester functions
function updateLedPreview() {
    const color = document.getElementById('ledColor').value;
    const r = parseInt(color.substr(1,2), 16);
    const g = parseInt(color.substr(3,2), 16);
    const b = parseInt(color.substr(5,2), 16);
    document.getElementById('ledPreview').style.background = color;
    document.getElementById('ledR').textContent = r;
    document.getElementById('ledG').textContent = g;
    document.getElementById('ledB').textContent = b;
    const target = parseInt(document.getElementById('ledTarget').value);
    const canId = (0x18200000 | (target << 8)).toString(16).toUpperCase().padStart(8, '0');
    document.getElementById('ledCanId').textContent = '0x' + canId;
    document.getElementById('ledCanData').textContent = 
        r.toString(16).toUpperCase().padStart(2,'0') + ' ' +
        g.toString(16).toUpperCase().padStart(2,'0') + ' ' +
        b.toString(16).toUpperCase().padStart(2,'0') + ' 00 00 00 00 00';
}

async function setLed() {
    const color = document.getElementById('ledColor').value;
    const r = parseInt(color.substr(1,2), 16);
    const g = parseInt(color.substr(3,2), 16);
    const b = parseInt(color.substr(5,2), 16);
    const target = parseInt(document.getElementById('ledTarget').value);
    await setLedTarget(r, g, b, target);
}

async function setLedColor(r, g, b) {
    const target = parseInt(document.getElementById('ledTarget').value);
    document.getElementById('ledColor').value = '#' + 
        r.toString(16).padStart(2,'0') + 
        g.toString(16).padStart(2,'0') + 
        b.toString(16).padStart(2,'0');
    updateLedPreview();
    await setLedTarget(r, g, b, target);
}

async function setLedTarget(r, g, b, target) {
    try {
        await fetch('/api/led', { method: 'POST', headers: {'Content-Type':'application/json'}, 
            body: JSON.stringify({r: r, g: g, b: b, target: target}) });
        setStatus('LED color sent (R=' + r + ' G=' + g + ' B=' + b + ')', 'success');
    } catch(e) { setStatus('Failed', 'error'); }
}

document.addEventListener('DOMContentLoaded', () => {
    const colorPicker = document.getElementById('ledColor');
    const targetSelect = document.getElementById('ledTarget');
    if (colorPicker) colorPicker.addEventListener('input', updateLedPreview);
    if (targetSelect) targetSelect.addEventListener('change', updateLedPreview);
});

async function setAddr(current) {
    const newAddr = parseInt(document.getElementById('addr_' + current).value);
    try {
        await fetch('/api/address', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({target: current, address: newAddr}) });
        setStatus('Address change sent. Module will reboot.', 'success');
    } catch(e) { setStatus('Failed', 'error'); }
}

async function saveMapping() {
    // Check for channel conflicts before saving
    if (!checkChannelConflicts()) {
        setStatus('Fix channel conflicts before saving', 'error');
        return;
    }
    const axes = [];
    const cfgAxes = gConfig.axes || [];
    // Build deadband map by (src, pot) so remapping inherits the pot's deadband
    const dbMap = {};
    for (let i = 0; i < 16; i++) {
        const a = cfgAxes[i];
        if (a && a.sourceAddress) {
            const key = a.sourceAddress + '_' + a.potIndex;
            if (!dbMap[key]) dbMap[key] = { min: a.deadbandMin || 307, max: a.deadbandMax || 717 };
        }
    }
    for (let i = 0; i < 16; i++) {
        const flags = (document.getElementById('a' + i + '_en').checked ? 1 : 0) | (document.getElementById('a' + i + '_bidir').checked ? 2 : 0) | (document.getElementById('a' + i + '_inv').checked ? 4 : 0);
        const srcVal = document.getElementById('a' + i + '_src').value.split('_');
        const src = parseInt(srcVal[0]) || 0;
        const pot = parseInt(srcVal[1]) || 0;
        const dbKey = src + '_' + pot;
        const db = dbMap[dbKey] || { min: 307, max: 717 };
        // Convert output pair number (1-8) to pair base channel (0,2,4,6,8,10,12,14)
        const outVal = parseInt(document.getElementById('a' + i + '_ch').value);
        const channel = Math.floor((outVal - 1) / 2) * 2; // value 1->ch0, 3->ch2, 5->ch4, etc.
        const gateVal = parseInt(document.getElementById('a' + i + '_bgate').value) || 0;
        axes.push({
            axisIdx: i,
            sourceAddress: src,
            potIndex: pot,
            outputChannel: channel,
            deadbandMin: db.min,
            deadbandMax: db.max,
            pwmMin: parseInt(document.getElementById('a' + i + '_pwmin').value),
            pwmMax: parseInt(document.getElementById('a' + i + '_pwmax').value),
            flags: flags,
            buttonGate: gateVal,
            curveExp: parseInt(document.getElementById('a' + i + '_curve').value) || 2
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
    if (!file) { document.getElementById('otaStatus').textContent = 'Please select a file'; return; }
    document.getElementById('otaStatus').textContent = 'Uploading...';
    const prog = document.getElementById('prog');
    const xhr = new XMLHttpRequest();
    xhr.timeout = 180000; // 3 minute timeout
    xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) prog.style.width = (e.loaded / e.total * 100).toFixed(1) + '%';
    };
    xhr.onload = () => {
        if (xhr.status === 200) {
            document.getElementById('otaStatus').textContent = 'Update successful! Rebooting...';
            setTimeout(() => location.reload(), 8000);
        } else {
            document.getElementById('otaStatus').textContent = 'Update failed: ' + xhr.responseText;
        }
    };
    xhr.onerror = () => document.getElementById('otaStatus').textContent = 'Network error. Try closing other tabs.';
    xhr.ontimeout = () => document.getElementById('otaStatus').textContent = 'Upload timeout. Try closing other tabs.';
    xhr.open('POST', '/update');
    xhr.send(file);
}

// ---------------------------------------------------------------------------
// Motor Test functions
// ---------------------------------------------------------------------------
let gTestMode = false;
let gTestValues = [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0];

function renderTestOutputs() {
    let h = '<div style="display:grid;grid-template-columns:60px 80px repeat(5,60px);gap:8px;align-items:center;font-size:0.85rem">';
    h += '<div style="color:#94a3b8;font-weight:500">Output</div>';
    h += '<div style="color:#94a3b8;font-weight:500">Value</div>';
    h += '<div></div><div></div><div></div><div></div><div></div>';
    for (let i = 0; i < 16; i++) {
        const val = gTestValues[i];
        const pct = (val / 4095 * 100).toFixed(1);
        h += `<div style="font-weight:600">Out ${i+1}</div>`;
        h += `<div><span id="tv${i}">${val}</span> <span style="color:#64748b;font-size:0.75rem">(${pct}%)</span></div>`;
        h += `<button class="secondary" onclick="adjTest(${i},-100)" style="padding:4px 8px">-100</button>`;
        h += `<button class="secondary" onclick="adjTest(${i},-10)" style="padding:4px 8px">-10</button>`;
        h += `<button class="danger" onclick="zeroTest(${i})" style="padding:4px 8px">Zero</button>`;
        h += `<button class="secondary" onclick="adjTest(${i},10)" style="padding:4px 8px">+10</button>`;
        h += `<button class="secondary" onclick="adjTest(${i},100)" style="padding:4px 8px">+100</button>`;
    }
    h += '</div>';
    document.getElementById('testOutList').innerHTML = h;
}

async function toggleTestMode() {
    gTestMode = document.getElementById('testModeEn').checked;
    if (gTestMode) {
        // Zero all values when enabling
        gTestValues = [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0];
        renderTestOutputs();
    }
    await sendTestCmd();
}

async function adjTest(idx, delta) {
    if (!gTestMode) {
        document.getElementById('testModeEn').checked = true;
        gTestMode = true;
    }
    let newVal = gTestValues[idx] + delta;
    if (newVal < 0) newVal = 0;
    if (newVal > 4095) newVal = 4095;
    gTestValues[idx] = newVal;
    document.getElementById('tv' + idx).parentElement.innerHTML = 
        `<span id="tv${idx}">${newVal}</span> <span style="color:#64748b;font-size:0.75rem">(${(newVal/4095*100).toFixed(1)}%)</span>`;
    await sendTestCmd();
}

async function zeroTest(idx) {
    gTestValues[idx] = 0;
    document.getElementById('tv' + idx).parentElement.innerHTML = 
        `<span id="tv${idx}">0</span> <span style="color:#64748b;font-size:0.75rem">(0.0%)</span>`;
    await sendTestCmd();
}

async function sendTestCmd() {
    try {
        await fetch('/api/motortest', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({enabled: gTestMode, values: gTestValues})
        });
    } catch(e) { setStatus('Test command failed', 'error'); }
}

async function fetchTestState() {
    try {
        const r = await fetch('/api/motortest');
        const d = await r.json();
        if (gTestMode !== d.enabled) {
            gTestMode = d.enabled;
            document.getElementById('testModeEn').checked = gTestMode;
        }
        gTestValues = d.values;
        if (d.timeout > 0) {
            document.getElementById('testTimeout').textContent = `Auto-disable in ${(d.timeout/1000).toFixed(1)}s`;
        } else {
            document.getElementById('testTimeout').textContent = gTestMode ? 'Waiting for command...' : '';
        }
        renderTestOutputs();
    } catch(e) {}
}

setInterval(fetchState, 1000);
fetchConfig().then(() => { fetchState(); fetchLabels(); });
fetchCanOut();
fetchTestState();
fetchCustomBtns().then(() => fetchBtnRules());
setInterval(fetchTestState, 2000);

// ---------------------------------------------------------------------------
// Labels management
// ---------------------------------------------------------------------------
let gLabels = { joysticks: [], outputs: [] };

function buildAxisDropdown(selectedSA, selectedPot) {
    // Build dropdown options from labels: "position axisLabel" -> value "SA_POT"
    let opts = '<option value="0_0">-- Off --</option>';
    const joyAddrs = [0x21, 0x22, 0x23, 0x24];
    for (let ji = 0; ji < gLabels.joysticks.length; ji++) {
        const jl = gLabels.joysticks[ji];
        if (!jl.sourceAddress) continue;
        const pos = jl.position || ('Joy' + (ji+1));
        for (let a = 0; a < 4; a++) {
            const axLbl = jl.axes[a] || ('Axis' + (a+1));
            const val = jl.sourceAddress + '_' + a;
            const sel = (jl.sourceAddress == selectedSA && a == selectedPot) ? 'selected' : '';
            opts += '<option value="' + val + '" ' + sel + '>' + pos + ' ' + axLbl + '</option>';
        }
    }
    // Fallback: if no labels configured, show raw addresses
    if (gLabels.joysticks.every(j => !j.sourceAddress)) {
        opts = '<option value="0_0">-- Off --</option>';
        for (const sa of [0x21, 0x22, 0x23, 0x24]) {
            for (let p = 0; p < 4; p++) {
                const val = sa + '_' + p;
                const sel = (sa == selectedSA && p == selectedPot) ? 'selected' : '';
                opts += '<option value="' + val + '" ' + sel + '>0x' + sa.toString(16).toUpperCase() + ' Pot' + (p+1) + '</option>';
            }
        }
    }
    return opts;
}

function buildGateDropdown(currentGate) {
    // Gate encoding: 0=none, 1=Btn1 pressed, 2=Btn1 released, 3=Btn2 pressed, 4=Btn2 released, etc.
    let opts = '<option value="0" ' + (currentGate==0?'selected':'') + '>None</option>';
    const btnNames = ['Btn1', 'Btn2', 'Btn3', 'Btn4'];
    for (let ji = 0; ji < gLabels.joysticks.length; ji++) {
        const jl = gLabels.joysticks[ji];
        if (!jl.sourceAddress) continue;
        const pos = jl.position || ('Joy' + (ji+1));
        for (let b = 0; b < 4; b++) {
            const pressedGate = b * 2 + 1;
            const releasedGate = b * 2 + 2;
            const selP = (pressedGate == currentGate) ? 'selected' : '';
            const selR = (releasedGate == currentGate) ? 'selected' : '';
            opts += '<option value="' + pressedGate + '" ' + selP + '>' + pos + ' ' + btnNames[b] + '</option>';
            opts += '<option value="' + releasedGate + '" ' + selR + '>' + pos + ' !' + btnNames[b] + '</option>';
        }
    }
    // Fallback if no labels
    if (gLabels.joysticks.every(j => !j.sourceAddress)) {
        for (let b = 0; b < 4; b++) {
            const pressedGate = b * 2 + 1;
            const releasedGate = b * 2 + 2;
            const selP = (pressedGate == currentGate) ? 'selected' : '';
            const selR = (releasedGate == currentGate) ? 'selected' : '';
            opts += '<option value="' + pressedGate + '" ' + selP + '>Btn' + (b+1) + '</option>';
            opts += '<option value="' + releasedGate + '" ' + selR + '>!Btn' + (b+1) + '</option>';
        }
    }
    return opts;
}

function renderLabels() {
    const positions = ['left', 'right', 'center', 'rear'];
    let h = '';
    const joyAddrs = [0x21, 0x22, 0x23, 0x24];
    for (let i = 0; i < 4; i++) {
        const jl = gLabels.joysticks[i] || { sourceAddress: joyAddrs[i], position: '', axes: ['','','',''] };
        let posOpts = '';
        for (const p of positions) posOpts += '<option value="' + p + '" ' + (jl.position==p?'selected':'') + '>' + p + '</option>';
        h += '<div style="display:flex;gap:8px;align-items:center;margin-bottom:8px;padding:8px;background:#0f172a;border-radius:6px">';
        h += '<span style="min-width:60px;font-weight:600;color:#38bdf8">Joy ' + (i+1) + '</span>';
        h += '<input type="text" id="jlSA' + i + '" value="0x' + (jl.sourceAddress || joyAddrs[i]).toString(16).toUpperCase() + '" style="width:70px" placeholder="0x21">';
        h += '<select id="jlPos' + i + '" style="width:80px">' + posOpts + '</select>';
        for (let a = 0; a < 4; a++) {
            h += '<input type="text" id="jlAx' + i + '_' + a + '" value="' + (jl.axes[a] || '') + '" placeholder="Axis ' + (a+1) + '" style="width:70px">';
        }
        h += '</div>';
    }
    document.getElementById('joyLabelsList').innerHTML = h;

    // Output labels (8 pair-based outputs)
    let oh = '';
    for (let p = 0; p < 8; p++) {
        const idx = p * 2; // even indices: 0, 2, 4, 6, 8, 10, 12, 14
        const ol = gLabels.outputs[idx] || { channel: idx, label: '' };
        oh += '<div style="display:inline-flex;gap:4px;align-items:center;margin:4px">';
        oh += '<span style="min-width:40px;font-size:0.8rem;color:#94a3b8">Out ' + (p+1) + '</span>';
        oh += '<input type="text" id="olLbl' + idx + '" value="' + (ol.label || '') + '" placeholder="Name" style="width:80px">';
        oh += '</div>';
    }
    document.getElementById('outLabelsList').innerHTML = oh;
}

async function fetchLabels() {
    try {
        const r = await fetch('/api/labels');
        gLabels = await r.json();
        renderLabels();
        renderMapping();
        renderJoyAssign();
    } catch(e) {}
}

async function saveLabels() {
    const joyLabels = [];
    for (let i = 0; i < 4; i++) {
        const axes = [];
        for (let a = 0; a < 4; a++) {
            axes.push(document.getElementById('jlAx' + i + '_' + a).value);
        }
        const saVal = document.getElementById('jlSA' + i).value.trim();
        const sa = parseInt(saVal, 16) || 0; // always hex
        joyLabels.push({
            idx: i,
            sourceAddress: sa,
            position: document.getElementById('jlPos' + i).value,
            axes: axes
        });
    }
    const outLabels = [];
    for (let p = 0; p < 8; p++) {
        const idx = p * 2; // even indices: 0, 2, 4, 6, 8, 10, 12, 14
        outLabels.push({
            outIdx: idx,
            channel: idx,
            label: document.getElementById('olLbl' + idx).value
        });
    }
    try {
        await fetch('/api/labels', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({ joysticks: joyLabels, outputs: outLabels }) });
        setStatus('Labels saved', 'success');
        await fetchLabels();
    } catch(e) { setStatus('Failed to save labels', 'error'); }
}

// ---------------------------------------------------------------------------
// Button Output Rules
// ---------------------------------------------------------------------------
let gBtnRules = [];


function renderBtnOutRules() {
    // Convert flat rules into paired structure (one pair per output)
    const pairs = [];
    for (let o = 0; o < 16; o++) {
        pairs.push({ out: o, posBtn: '0_0', negBtn: '0_0', posPwm: 255, negPwm: 255 });
    }
    for (let i = 0; i < gBtnRules.length; i++) {
        const r = gBtnRules[i];
        if (!r || !r.enabled) continue;
        const ch = r.outputChannel;
        if (ch < 0 || ch >= 16) continue;
        const srcVal = r.btnSourceSA + '_' + r.btnIndex;
        if (r.btnMode === 0) {
            pairs[ch].posBtn = srcVal;
            pairs[ch].posPwm = r.pwmTarget;
        } else {
            pairs[ch].negBtn = srcVal;
            pairs[ch].negPwm = r.pwmTarget;
        }
    }
    let h = '';
    for (let o = 0; o < 16; o++) {
        const p = pairs[o];
        const ol = gLabels.outputs[o];
        const lbl = (ol && ol.label) ? ol.label : ('Out ' + (o+1));
        const hasMapping = p.posBtn !== '0_0' || p.negBtn !== '0_0';
        const border = hasMapping ? '1px solid #38bdf8' : '1px solid #334155';
        h += '<div style="padding:10px;background:#0f172a;border-radius:8px;margin-bottom:8px;border:' + border + '">';
        h += '<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">';
        h += '<span style="color:#e2e8f0;font-weight:700;font-size:0.9rem">' + lbl + '</span>';
        h += '<span style="color:#64748b;font-size:0.75rem">#' + (o+1) + '</span></div>';
        h += '<div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;align-items:end">';
        // Positive direction
        h += '<div style="background:#1a2332;border-radius:6px;padding:8px">';
        h += '<label style="display:block;color:#22c55e;font-size:0.75rem;font-weight:700;margin-bottom:4px">+ Max PWM</label>';
        h += '<select id="br_pos_src_' + o + '" style="width:100%;margin-bottom:4px">' + buildBtnSourceDropdownStr(p.posBtn) + '</select>';
        h += '<input type="number" id="br_pos_pwm_' + o + '" value="' + p.posPwm + '" min="0" max="255" placeholder="PWM" style="width:100%"></div>';
        // Negative direction
        h += '<div style="background:#1a2332;border-radius:6px;padding:8px">';
        h += '<label style="display:block;color:#ef4444;font-size:0.75rem;font-weight:700;margin-bottom:4px">- Min PWM</label>';
        h += '<select id="br_neg_src_' + o + '" style="width:100%;margin-bottom:4px">' + buildBtnSourceDropdownStr(p.negBtn) + '</select>';
        h += '<input type="number" id="br_neg_pwm_' + o + '" value="' + p.negPwm + '" min="0" max="255" placeholder="PWM" style="width:100%"></div>';
        h += '</div></div>';
    }
    h += '<div style="margin-top:8px;display:flex;gap:8px">';
    h += '<button onclick="saveBtnRules()">Save</button>';
    h += '<button class="secondary" onclick="fetchBtnRules()">Refresh</button>';
    h += '</div>';
    document.getElementById('btnOutList').innerHTML = h;
}

function buildBtnSourceDropdownStr(selectedVal) {
    const parts = selectedVal.split('_');
    const selSA = parseInt(parts[0]) || 0;
    const selBtn = parseInt(parts[1]) || 0;
    let opts = '<option value="0_0">-- Off --</option>';
    const joyAddrs = [0x21, 0x22, 0x23, 0x24];
    for (let ji = 0; ji < gLabels.joysticks.length; ji++) {
        const jl = gLabels.joysticks[ji];
        const sa = jl.sourceAddress || joyAddrs[ji];
        const pos = jl.position || ('Joy' + (ji+1));
        for (let b = 0; b < 4; b++) {
            const val = sa + '_' + b;
            const sel = (sa == selSA && b == selBtn) ? 'selected' : '';
            opts += '<option value="' + val + '" ' + sel + '>' + pos + ' Btn' + (b+1) + '</option>';
        }
    }
    for (let ci = 0; ci < gCustomBtns.length; ci++) {
        const cb = gCustomBtns[ci];
        if (!cb.enabled) continue;
        const val = (0xF0 + ci) + '_0';
        const sel = ((0xF0 + ci) == selSA && 0 == selBtn) ? 'selected' : '';
        opts += '<option value="' + val + '" ' + sel + '>Virtual: ' + (cb.name || 'Btn' + (ci+1)) + '</option>';
    }
    return opts;
}

async function fetchBtnRules() {
    try {
        const r = await fetch('/api/btnrules');
        const d = await r.json();
        gBtnRules = d.rules || [];
        renderBtnOutRules();
    } catch(e) {}
}

async function saveBtnRules() {
    const rules = [];
    let ruleIdx = 0;
    for (let o = 0; o < 16; o++) {
        // Positive direction (maxPWM)
        const posSrcEl = document.getElementById('br_pos_src_' + o);
        if (!posSrcEl) continue;
        const posVal = posSrcEl.value.split('_');
        const posSA = parseInt(posVal[0]) || 0;
        const posBtn = parseInt(posVal[1]) || 0;
        if (posSA !== 0) {
            rules.push({
                ruleIdx: ruleIdx++,
                enabled: true,
                outputChannel: o,
                btnSourceSA: posSA,
                btnIndex: posBtn,
                btnMode: 0,
                pwmTarget: parseInt(document.getElementById('br_pos_pwm_' + o).value) || 255
            });
        }
        // Negative direction (minPWM)
        const negSrcEl = document.getElementById('br_neg_src_' + o);
        if (!negSrcEl) continue;
        const negVal = negSrcEl.value.split('_');
        const negSA = parseInt(negVal[0]) || 0;
        const negBtn = parseInt(negVal[1]) || 0;
        if (negSA !== 0) {
            rules.push({
                ruleIdx: ruleIdx++,
                enabled: true,
                outputChannel: o,
                btnSourceSA: negSA,
                btnIndex: negBtn,
                btnMode: 1,
                pwmTarget: parseInt(document.getElementById('br_neg_pwm_' + o).value) || 255
            });
        }
    }
    try {
        await fetch('/api/btnrules', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({ rules: rules }) });
        setStatus('Button rules saved', 'success');
    } catch(e) { setStatus('Failed to save', 'error'); }
}

// ---------------------------------------------------------------------------
// Custom CAN Buttons
// ---------------------------------------------------------------------------
let gCustomBtns = [];

function renderCustomBtns() {
    let h = '<div style="display:grid;grid-template-columns:40px 50px 100px 70px 70px 120px;gap:8px;align-items:center;font-size:0.8rem">';
    h += '<div style="color:#94a3b8;font-weight:500">#</div>';
    h += '<div style="color:#94a3b8;font-weight:500">En</div>';
    h += '<div style="color:#94a3b8;font-weight:500">CAN ID (hex)</div>';
    h += '<div style="color:#94a3b8;font-weight:500">Byte</div>';
    h += '<div style="color:#94a3b8;font-weight:500">Bit</div>';
    h += '<div style="color:#94a3b8;font-weight:500">Name</div>';
    for (let i = 0; i < 8; i++) {
        const b = gCustomBtns[i] || { enabled: false, canId: '0', byteIndex: 0, bitIndex: 0, name: '' };
        const canIdHex = typeof b.canId === 'string' ? b.canId : Number(b.canId).toString(16).toUpperCase();
        h += '<div>' + i + '</div>';
        h += '<div><input type="checkbox" id="cb' + i + '_en" ' + (b.enabled?'checked':'') + '></div>';
        h += '<div><input type="text" id="cb' + i + '_id" value="' + canIdHex + '" style="width:80px" placeholder="0x18FF0021"></div>';
        h += '<div><input type="number" id="cb' + i + '_byte" value="' + b.byteIndex + '" min="0" max="7" style="width:50px"></div>';
        h += '<div><input type="number" id="cb' + i + '_bit" value="' + b.bitIndex + '" min="0" max="7" style="width:50px"></div>';
        h += '<div><input type="text" id="cb' + i + '_name" value="' + (b.name || '') + '" style="width:110px" placeholder="Button name"></div>';
    }
    h += '</div>';
    document.getElementById('customBtnList').innerHTML = h;
}

async function fetchCustomBtns() {
    try {
        const r = await fetch('/api/custombtns');
        const d = await r.json();
        gCustomBtns = d.buttons || [];
        renderCustomBtns();
    } catch(e) {}
}

async function saveCustomBtns() {
    const buttons = [];
    for (let i = 0; i < 8; i++) {
        buttons.push({
            btnIdx: i,
            enabled: document.getElementById('cb' + i + '_en').checked,
            canId: document.getElementById('cb' + i + '_id').value,
            byteIndex: parseInt(document.getElementById('cb' + i + '_byte').value) || 0,
            bitIndex: parseInt(document.getElementById('cb' + i + '_bit').value) || 0,
            name: document.getElementById('cb' + i + '_name').value
        });
    }
    try {
        await fetch('/api/custombtns', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({ buttons: buttons }) });
        setStatus('Custom buttons saved', 'success');
        await fetchCustomBtns();
        // Re-render button outputs to pick up new virtual buttons
        renderBtnOutRules();
    } catch(e) { setStatus('Failed to save', 'error'); }
}
</script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------------------
// HTTP Handlers
// ---------------------------------------------------------------------------
static void handleRoot() {
  // Use chunked transfer to avoid heap allocation issues with large HTML
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  // Send HTML in chunks
  const char *ptr = MAIN_HTML;
  while (*ptr) {
    size_t len = strlen(ptr);
    if (len > 2048)
      len = 2048;
    server.sendContent(ptr, len);
    ptr += len;
  }
}

static void handleState() {
  // Use chunked transfer to avoid massive heap allocation
  // Build JSON in a fixed buffer
  static char buf[1536];
  int pos = 0;
  pos +=
      snprintf(buf + pos, sizeof(buf) - pos,
               "{\"localAddr\":%d,\"online\":%s,\"uptime\":%lu,\"txCount\":%lu,"
               "\"rxCount\":%lu,\"errCount\":%lu,",
               g_can ? g_can->getAddress() : 0,
               g_can && g_can->isOnline() ? "true" : "false", millis() / 1000,
               g_can ? g_can->getTxCount() : 0, g_can ? g_can->getRxCount() : 0,
               g_can ? g_can->getErrorCount() : 0);

  // Joystick data (compact)
  pos += snprintf(buf + pos, sizeof(buf) - pos, "\"joy\":{");
  bool firstJoy = true;
  for (int sa = 0; sa < 256; sa++) {
    if (g_joyUpdateTime[sa] > 0 && millis() - g_joyUpdateTime[sa] < 2000) {
      if (!firstJoy)
        pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
      pos += snprintf(buf + pos, sizeof(buf) - pos,
                      "\"%d\":{\"pots\":[%d,%d,%d,%d],\"btns\":%d,\"age\":%lu}",
                      sa, g_joyPots[sa][0], g_joyPots[sa][1], g_joyPots[sa][2],
                      g_joyPots[sa][3], g_joyButtons[sa],
                      (millis() - g_joyUpdateTime[sa]) / 1000);
      firstJoy = false;
    }
  }
#if defined(ECU_TYPE_JOYSTICK)
  if (g_ecuJoystickId > 0 && g_can) {
    if (!firstJoy)
      pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
    pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "\"%d\":{\"pots\":[%d,%d,%d,%d],\"btns\":%d,\"age\":0}",
                    g_can->getAddress(), g_localPot1, g_localPot2, g_localPot3,
                    g_localPot4,
                    (g_localBtn1 ? 1 : 0) | (g_localBtn2 ? 2 : 0) |
                        (g_localBtn3 ? 4 : 0) | (g_localBtn4 ? 8 : 0));
  }
#endif
  pos += snprintf(buf + pos, sizeof(buf) - pos, "},\"sol\":[");
  for (int i = 0; i < MAX_AXIS_COUNT; i++) {
    if (i > 0)
      pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%d", g_solenoidValues[i]);
  }
  pos += snprintf(buf + pos, sizeof(buf) - pos, "],\"modules\":{");
  bool firstMod = true;
  for (int i = 0; i < 256; i++) {
    if (g_modules[i].lastSeen > 0 && millis() - g_modules[i].lastSeen < 5000) {
      if (!firstMod)
        pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
      pos +=
          snprintf(buf + pos, sizeof(buf) - pos,
                   "\"%d\":{\"addr\":%d,\"type\":%d,\"uptime\":%d,\"age\":%lu}",
                   i, g_modules[i].addr, g_modules[i].type, g_modules[i].uptime,
                   (millis() - g_modules[i].lastSeen) / 1000);
      firstMod = false;
    }
  }
  pos += snprintf(buf + pos, sizeof(buf) - pos, "}}");
  server.send(200, "application/json", buf);
}

static void handleConfigGet() {
  String json = "{";
  json += "\"pcaCount\":" + String(g_motorCfg.pcaCount) + ",";
  json += "\"axes\":[";
  for (int i = 0; i < MAX_AXIS_COUNT; i++) {
    const AxisConfig &a = g_motorCfg.axes[i];
    if (i < 4 || a.flags) {
      Serial.printf("[Config GET] axis%d src=0x%02X pot=%d ch=%d db=%d-%d "
                    "pwm=%d-%d flags=%d\n",
                    i, a.sourceAddress, a.potIndex, a.outputChannel,
                    a.deadbandMin, a.deadbandMax, a.pwmMin, a.pwmMax, a.flags);
    }
    json += "{";
    json += "\"sourceAddress\":" + String(a.sourceAddress) + ",";
    json += "\"potIndex\":" + String(a.potIndex) + ",";
    json += "\"outputChannel\":" + String(a.outputChannel) + ",";
    json += "\"deadbandMin\":" + String(a.deadbandMin) + ",";
    json += "\"deadbandMax\":" + String(a.deadbandMax) + ",";
    json += "\"pwmMin\":" + String(a.pwmMin) + ",";
    json += "\"pwmMax\":" + String(a.pwmMax) + ",";
    json += "\"flags\":" + String(a.flags) + ",";
    json += "\"buttonGate\":" + String(a.buttonGate) + ",";
    json += "\"curveExp\":" + String(a.curveExp);
    json += "},";
  }
  if (json.endsWith(","))
    json.remove(json.length() - 1);
  json += "]}";
  server.send(200, "application/json", json);
}

static void handleConfigPost() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    Serial.printf("[Config] POST body length=%d\n", body.length());
    for (int i = 0; i < MAX_AXIS_COUNT; i++) {
      String key = "\"axisIdx\":" + String(i);
      int idx = body.indexOf(key);
      if (idx >= 0) {
        // Find the enclosing { } for this axis object
        int objStart = body.lastIndexOf('{', idx);
        int objEnd = body.indexOf('}', idx);
        if (objStart < 0)
          objStart = 0;
        if (objEnd < 0)
          objEnd = body.length();

        AxisConfig a;
        a.sourceAddress = parseJsonInt(body, "sourceAddress", objStart, objEnd);
        a.potIndex = parseJsonInt(body, "potIndex", objStart, objEnd);
        a.outputChannel = parseJsonInt(body, "outputChannel", objStart, objEnd);
        a.deadbandMin = parseJsonInt(body, "deadbandMin", objStart, objEnd);
        a.deadbandMax = parseJsonInt(body, "deadbandMax", objStart, objEnd);
        a.pwmMin = parseJsonInt(body, "pwmMin", objStart, objEnd);
        a.pwmMax = parseJsonInt(body, "pwmMax", objStart, objEnd);
        a.flags = parseJsonInt(body, "flags", objStart, objEnd);
        a.buttonGate = parseJsonInt(body, "buttonGate", objStart, objEnd);
        a.curveExp = parseJsonInt(body, "curveExp", objStart, objEnd);
        if (a.curveExp == 0)
          a.curveExp = 2; // Default to linear
        g_motorCfg.axes[i] = a;
        Serial.printf("[Config] axis%d src=0x%02X pot=%d ch=%d db=%d-%d "
                      "pwm=%d-%d flags=%d gate=%d curve=%d\n",
                      i, a.sourceAddress, a.potIndex, a.outputChannel,
                      a.deadbandMin, a.deadbandMax, a.pwmMin, a.pwmMax, a.flags,
                      a.buttonGate, a.curveExp);

        // Save locally if motor driver
#if defined(ECU_TYPE_MOTOR_DRIVER)
        cfgMgr.saveAxisConfig(i, a);
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

static int parseJsonInt(const String &json, const char *key, int searchStart,
                        int searchEnd) {
  if (searchEnd < 0)
    searchEnd = json.length();
  String search = String("\"") + key + "\":";
  int pos = json.indexOf(search, searchStart);
  if (pos < 0 || pos > searchEnd)
    return 0;
  pos += search.length();
  while (pos < searchEnd && (json[pos] == ' ' || json[pos] == '\t'))
    pos++;
  int end = pos;
  while (end < searchEnd && (json[end] == '-' || isdigit(json[end])))
    end++;
  if (end == pos)
    return 0;
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

static void handleLed() {
  if (server.hasArg("plain") && g_can) {
    String body = server.arg("plain");
    int r = parseJsonInt(body, "r", 0);
    int g = parseJsonInt(body, "g", 0);
    int b = parseJsonInt(body, "b", 0);
    int target = parseJsonInt(body, "target", 0);
    uint8_t data[8] = {(uint8_t)r, (uint8_t)g, (uint8_t)b, 0, 0, 0, 0, 0};
    g_can->send(PF_LED_COLOR, target, data, 3, 6);
    Serial.printf("[LED] Web UI set color R=%d G=%d B=%d target=0x%02X\n", r, g,
                  b, target);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleAddress() {
  if (server.hasArg("plain") && g_can) {
    String body = server.arg("plain");
    int target = parseJsonInt(body, "target", 0);
    int addr = parseJsonInt(body, "address", 0);
    uint8_t data[1] = {(uint8_t)addr};
    g_can->send(PF_SET_ADDRESS, target, data, 1, 6);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleCanOutputGet() {
  String json = "{\"rules\":[";
  for (int i = 0; i < MAX_CAN_OUTPUT_RULES; i++) {
    const CanOutputRule &r = g_canOutputRules[i];
    json += "{";
    json += "\"enabled\":" + String(r.enabled ? "true" : "false") + ",";
    json += "\"matchPF\":" + String(r.matchPF) + ",";
    json += "\"matchSA\":" + String(r.matchSA) + ",";
    json += "\"gpioPin\":" + String(r.gpioPin) + ",";
    json += "\"mode\":" + String(r.mode) + ",";
    json += "\"momentaryMs\":" + String(r.momentaryMs);
    json += "},";
  }
  if (json.endsWith(","))
    json.remove(json.length() - 1);
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
        r.enabled = body.indexOf("\"enabled\":true", idx) > 0 &&
                    body.indexOf("\"enabled\":true", idx) < idx + 200;
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

// ---------------------------------------------------------------------------
// Motor Test API - allows manual control of PWM outputs for testing
// ---------------------------------------------------------------------------
static void handleMotorTestGet() {
#if defined(ECU_TYPE_MOTOR_DRIVER)
  String json = "{";
  json += "\"enabled\":" + String(g_testMode ? "true" : "false") + ",";
  json += "\"values\":[";
  for (int i = 0; i < 16; i++) {
    json += String(g_testValues[i]);
    if (i < 15)
      json += ",";
  }
  json += "],";
  json += "\"timeout\":" +
          String((g_lastTestCmd > 0 && millis() - g_lastTestCmd < 10000)
                     ? (10000 - (millis() - g_lastTestCmd))
                     : 0);
  json += "}";
  server.send(200, "application/json", json);
#else
  server.send(
      200, "application/json",
      "{\"enabled\":false,\"values\":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]}");
#endif
}

static void handleMotorTestPost() {
#if defined(ECU_TYPE_MOTOR_DRIVER)
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    // Parse enabled flag
    if (body.indexOf("\"enabled\":true") >= 0) {
      g_testMode = true;
      g_lastTestCmd = millis();
    } else if (body.indexOf("\"enabled\":false") >= 0) {
      g_testMode = false;
      // Zero all test values when disabling
      for (int i = 0; i < 16; i++)
        g_testValues[i] = 0;
    }
    // Parse values array
    int idx = body.indexOf("\"values\":");
    if (idx >= 0) {
      int arrStart = body.indexOf('[', idx);
      int arrEnd = body.indexOf(']', arrStart);
      if (arrStart >= 0 && arrEnd > arrStart) {
        String arr = body.substring(arrStart + 1, arrEnd);
        int pos = 0;
        for (int i = 0; i < 16 && pos < arr.length(); i++) {
          int comma = arr.indexOf(',', pos);
          if (comma < 0)
            comma = arr.length();
          String val = arr.substring(pos, comma);
          val.trim();
          int v = val.toInt();
          if (v < 0)
            v = 0;
          if (v > 4095)
            v = 4095;
          g_testValues[i] = v;
          pos = comma + 1;
        }
      }
    }
    // Parse single output update (for +/- buttons)
    if (body.indexOf("\"output\":") >= 0) {
      int outIdx = parseJsonInt(body, "output", 0, body.length());
      if (outIdx >= 0 && outIdx < 16) {
        int newVal = parseJsonInt(body, "value", 0, body.length());
        if (newVal < 0)
          newVal = 0;
        if (newVal > 4095)
          newVal = 4095;
        g_testValues[outIdx] = newVal;
        g_lastTestCmd = millis();
      }
    }
  }
  server.send(200, "application/json", "{\"ok\":true}");
#else
  server.send(200, "application/json",
              "{\"ok\":false,\"error\":\"not motor driver\"}");
#endif
}

// ---------------------------------------------------------------------------
// Joystick command handler: POST /api/joycmd
// Body: {"cmd":"billentes-bal","active":true}
// ---------------------------------------------------------------------------
static void handleJoyCmdPost() {
#if defined(ECU_TYPE_MOTOR_DRIVER)
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    // Extract cmd string
    int cmdIdx = body.indexOf("\"cmd\":\"");
    if (cmdIdx >= 0) {
      int start = cmdIdx + 7;
      int end = body.indexOf("\"", start);
      if (end > start) {
        String cmd = body.substring(start, end);
        bool active = body.indexOf("\"active\":true") >= 0;

        // Map command name to function index
        // Indices match joyPairs: posIdx=even, negIdx=odd
        static const char *cmdNames[] = {"billentes-bal",     // 0 pair0 pos
                                         "billentes-jobb",    // 1 pair0 neg
                                         "bal-szarny-fel",    // 2 pair1 pos
                                         "bal-szarny-le",     // 3 pair1 neg
                                         "jobb-szarny-fel",   // 4 pair2 pos
                                         "jobb-szarny-le",    // 5 pair2 neg
                                         "keretmagassag-fel", // 6 pair3 pos
                                         "keretmagassag-le",  // 7 pair3 neg
                                         "fokeret-nyit",      // 8 pair4 pos
                                         "fokeret-csuk",      // 9 pair4 neg
                                         "segedkeret-nyit",   // 10 pair5 pos
                                         "segedkeret-csuk",   // 11 pair5 neg
                                         "mindketto-nyit",    // 12
                                         "mindketto-csuk",    // 13
                                         "master",            // 14
                                         "szakaszok",         // 15
                                         "auto",              // 16
                                         "stop"};             // 17
        const int numCmds = sizeof(cmdNames) / sizeof(cmdNames[0]);
        for (int i = 0; i < numCmds; i++) {
          if (cmd == cmdNames[i]) {
            g_joyCmdActive[i] = active;
            if (active)
              g_lastJoyCmd = millis();
            Serial.printf("[JoyCmd] %s = %s\n", cmd.c_str(),
                          active ? "ON" : "OFF");
            break;
          }
        }
      }
    }
  }
  server.send(200, "application/json", "{\"ok\":true}");
#else
  server.send(200, "application/json",
              "{\"ok\":false,\"error\":\"not motor driver\"}");
#endif
}

// ---------------------------------------------------------------------------
// Joystick mapping: GET /api/joymapping
// ---------------------------------------------------------------------------
static void handleJoyMapGet() {
#if defined(ECU_TYPE_MOTOR_DRIVER)
  String json = "{\"mappings\":[";
  for (int i = 0; i < MAX_JOY_FUNCTIONS; i++) {
    if (i > 0)
      json += ",";
    json += "{\"ch\":" + String(g_joyMappings[i].outputChannel) +
            ",\"inv\":" + String(g_joyMappings[i].invert ? "true" : "false") +
            "}";
  }
  json += "],\"labels\":[";
  for (int i = 0; i < MAX_OUTPUT_LABELS; i++) {
    if (i > 0)
      json += ",";
    if (g_outLabels[i].label[0])
      json += "\"" + String(g_outLabels[i].label) + "\"";
    else
      json += "\"Output " + String(i + 1) + "\"";
  }
  json += "]}";
  server.send(200, "application/json", json);
#else
  server.send(200, "application/json", "{\"mappings\":[],\"labels\":[]}");
#endif
}

// ---------------------------------------------------------------------------
// Joystick mapping: POST /api/joymapping
// Body: {"mappings":[{"ch":0,"inv":false},...]}
// ---------------------------------------------------------------------------
static void handleJoyMapPost() {
#if defined(ECU_TYPE_MOTOR_DRIVER)
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    int arrStart = body.indexOf("\"mappings\":[");
    if (arrStart >= 0) {
      arrStart = body.indexOf('[', arrStart);
      int arrEnd = body.indexOf(']', arrStart);
      if (arrStart >= 0 && arrEnd > arrStart) {
        String arr = body.substring(arrStart + 1, arrEnd);
        int pos = 0;
        for (int i = 0; i < MAX_JOY_FUNCTIONS && pos < arr.length(); i++) {
          int objStart = arr.indexOf('{', pos);
          int objEnd = arr.indexOf('}', objStart);
          if (objStart < 0 || objEnd < 0)
            break;
          String obj = arr.substring(objStart, objEnd + 1);
          g_joyMappings[i].outputChannel =
              parseJsonInt(obj, "ch", 0, obj.length());
          g_joyMappings[i].invert = obj.indexOf("\"inv\":true") >= 0;
          pos = objEnd + 1;
        }
        cfgMgr.saveJoystickMappings(g_joyMappings);
        Serial.println("[JoyMap] Saved joystick mappings");
      }
    }
  }
  server.send(200, "application/json", "{\"ok\":true}");
#else
  server.send(200, "application/json",
              "{\"ok\":false,\"error\":\"not motor driver\"}");
#endif
}

static void handleUpdate() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaActive = true;
    Serial.printf("[OTA] Start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
      server.send(500, "text/plain", "OTA begin failed");
      otaActive = false;
      return;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
      server.send(500, "text/plain", "OTA write failed");
      otaActive = false;
      return;
    }
    // Yield and delay to keep Ethernet stack happy and prevent resets
    yield();
    delay(10);             // Increased delay for Ethernet stack
    server.handleClient(); // Process any pending requests
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("[OTA] Success: %u bytes\n", upload.totalSize);
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

static void handleUpdatePost() { server.send(200, "text/plain", "OK"); }

// ---------------------------------------------------------------------------
// Labels API
// ---------------------------------------------------------------------------
static void handleLabelsGet() {
  String json = "{\"joysticks\":[";
  for (int i = 0; i < MAX_JOYSTICK_LABELS; i++) {
    if (i > 0)
      json += ",";
    json += "{\"sourceAddress\":" + String(g_joyLabels[i].sourceAddress);
    json += ",\"position\":\"" + String(g_joyLabels[i].position) + "\"";
    json += ",\"axes\":[";
    for (int a = 0; a < 4; a++) {
      if (a > 0)
        json += ",";
      json += "\"" + String(g_joyLabels[i].axisLabels[a]) + "\"";
    }
    json += "]}";
  }
  json += "],\"outputs\":[";
  for (int i = 0; i < MAX_OUTPUT_LABELS; i++) {
    if (i > 0)
      json += ",";
    json += "{\"channel\":" + String(g_outLabels[i].channel);
    json += ",\"label\":\"" + String(g_outLabels[i].label) + "\"}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

static void handleLabelsPost() {
  if (!server.hasArg("plain")) {
    server.send(400);
    return;
  }
  String body = server.arg("plain");
  // Parse joystick labels
  for (int i = 0; i < MAX_JOYSTICK_LABELS; i++) {
    String searchKey = "\"idx\":" + String(i);
    int idx = body.indexOf(searchKey);
    if (idx < 0)
      continue;
    int objStart = body.lastIndexOf('{', idx);
    int objEnd = body.indexOf('}', idx);
    if (objStart < 0)
      objStart = 0;
    if (objEnd < 0)
      objEnd = body.length();
    // Find nested axis object end
    int braceCount = 0;
    for (int c = objStart; c < body.length(); c++) {
      if (body[c] == '{')
        braceCount++;
      if (body[c] == '}') {
        braceCount--;
        if (braceCount == 0) {
          objEnd = c;
          break;
        }
      }
    }
    String sub = body.substring(objStart, objEnd + 1);
    g_joyLabels[i].sourceAddress = parseJsonInt(sub, "sourceAddress", 0);
    // Parse position string
    int posStart = sub.indexOf("\"position\":\"");
    if (posStart >= 0) {
      posStart += 12;
      int posEnd = sub.indexOf("\"", posStart);
      String pos = sub.substring(posStart, posEnd);
      strncpy(g_joyLabels[i].position, pos.c_str(),
              sizeof(g_joyLabels[i].position) - 1);
      g_joyLabels[i].position[sizeof(g_joyLabels[i].position) - 1] = '\0';
    }
    // Parse axis labels array
    int axesStart = sub.indexOf("\"axes\":[");
    if (axesStart >= 0) {
      int arrStart = sub.indexOf('[', axesStart);
      int arrEnd = sub.indexOf(']', arrStart);
      String arr = sub.substring(arrStart + 1, arrEnd);
      for (int a = 0; a < 4; a++) {
        int qs = arr.indexOf("\"");
        if (qs < 0)
          break;
        int qe = arr.indexOf("\"", qs + 1);
        if (qe < 0)
          break;
        String lbl = arr.substring(qs + 1, qe);
        strncpy(g_joyLabels[i].axisLabels[a], lbl.c_str(),
                sizeof(g_joyLabels[i].axisLabels[a]) - 1);
        g_joyLabels[i].axisLabels[a][sizeof(g_joyLabels[i].axisLabels[a]) - 1] =
            '\0';
        arr = arr.substring(qe + 1);
      }
    }
    cfgMgr.saveJoystickLabel(i, g_joyLabels[i]);
  }
  // Parse output labels
  for (int i = 0; i < MAX_OUTPUT_LABELS; i++) {
    String searchKey = "\"outIdx\":" + String(i);
    int idx = body.indexOf(searchKey);
    if (idx < 0)
      continue;
    int objStart = body.lastIndexOf('{', idx);
    int objEnd = body.indexOf('}', idx);
    if (objStart < 0)
      objStart = 0;
    String sub = body.substring(objStart, objEnd + 1);
    g_outLabels[i].channel = parseJsonInt(sub, "channel", 0);
    int lblStart = sub.indexOf("\"label\":\"");
    if (lblStart >= 0) {
      lblStart += 9;
      int lblEnd = sub.indexOf("\"", lblStart);
      String lbl = sub.substring(lblStart, lblEnd);
      strncpy(g_outLabels[i].label, lbl.c_str(),
              sizeof(g_outLabels[i].label) - 1);
      g_outLabels[i].label[sizeof(g_outLabels[i].label) - 1] = '\0';
    }
    cfgMgr.saveOutputLabel(i, g_outLabels[i]);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// Button Output Rules API
// ---------------------------------------------------------------------------
static void handleBtnRulesGet() {
  String json = "{\"rules\":[";
  for (int i = 0; i < MAX_BUTTON_OUTPUT_RULES; i++) {
    if (i > 0)
      json += ",";
    const ButtonOutputRule &r = g_btnOutputRules[i];
    json += "{\"enabled\":" + String(r.enabled ? "true" : "false");
    json += ",\"outputChannel\":" + String(r.outputChannel);
    json += ",\"btnSourceSA\":" + String(r.btnSourceSA);
    json += ",\"btnIndex\":" + String(r.btnIndex);
    json += ",\"btnMode\":" + String(r.btnMode);
    json += ",\"pwmTarget\":" + String(r.pwmTarget);
    json += "}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

static void handleBtnRulesPost() {
  if (!server.hasArg("plain")) {
    server.send(400);
    return;
  }
  String body = server.arg("plain");
  // Clear all rules first
  for (int i = 0; i < MAX_BUTTON_OUTPUT_RULES; i++) {
    ButtonOutputRule empty;
    empty.enabled = false;
    empty.outputChannel = 0;
    empty.btnSourceSA = 0;
    empty.btnIndex = 0;
    empty.btnMode = 0;
    empty.pwmTarget = 255;
    g_btnOutputRules[i] = empty;
    cfgMgr.saveButtonOutputRule(i, empty);
  }
  // Parse rules from JSON array
  for (int i = 0; i < MAX_BUTTON_OUTPUT_RULES; i++) {
    String key = "\"ruleIdx\":" + String(i);
    int idx = body.indexOf(key);
    if (idx < 0)
      continue;
    int objStart = body.lastIndexOf('{', idx);
    int objEnd = body.indexOf('}', idx);
    if (objStart < 0)
      objStart = 0;
    String sub = body.substring(objStart, objEnd + 1);
    ButtonOutputRule r;
    r.enabled = sub.indexOf("\"enabled\":true") > 0;
    r.outputChannel = parseJsonInt(sub, "outputChannel", 0);
    r.btnSourceSA = parseJsonInt(sub, "btnSourceSA", 0);
    r.btnIndex = parseJsonInt(sub, "btnIndex", 0);
    r.btnMode = parseJsonInt(sub, "btnMode", 0);
    r.pwmTarget = parseJsonInt(sub, "pwmTarget", 255);
    g_btnOutputRules[i] = r;
    cfgMgr.saveButtonOutputRule(i, r);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// Custom CAN Buttons API
// ---------------------------------------------------------------------------
static void handleCustomBtnsGet() {
  String json = "{\"buttons\":[";
  for (int i = 0; i < MAX_CUSTOM_CAN_BUTTONS; i++) {
    if (i > 0)
      json += ",";
    const CustomCanButton &b = g_customCanButtons[i];
    json += "{\"enabled\":" + String(b.enabled ? "true" : "false");
    json += ",\"canId\":\"" + String(b.canId, HEX) + "\"";
    json += ",\"byteIndex\":" + String(b.byteIndex);
    json += ",\"bitIndex\":" + String(b.bitIndex);
    json += ",\"name\":\"" + String(b.name) + "\"";
    json += "}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

static void handleCustomBtnsPost() {
  if (!server.hasArg("plain")) {
    server.send(400);
    return;
  }
  String body = server.arg("plain");
  for (int i = 0; i < MAX_CUSTOM_CAN_BUTTONS; i++) {
    String key = "\"btnIdx\":" + String(i);
    int idx = body.indexOf(key);
    if (idx < 0)
      continue;
    int objStart = body.lastIndexOf('{', idx);
    int objEnd = body.indexOf('}', idx);
    if (objStart < 0)
      objStart = 0;
    String sub = body.substring(objStart, objEnd + 1);
    CustomCanButton b;
    b.enabled = sub.indexOf("\"enabled\":true") > 0;
    b.byteIndex = parseJsonInt(sub, "byteIndex", 0);
    b.bitIndex = parseJsonInt(sub, "bitIndex", 0);
    // Parse canId as hex string
    int cidStart = sub.indexOf("\"canId\":\"");
    if (cidStart >= 0) {
      cidStart += 9;
      int cidEnd = sub.indexOf("\"", cidStart);
      String cidStr = sub.substring(cidStart, cidEnd);
      b.canId = (uint32_t)strtol(cidStr.c_str(), NULL, 16);
    }
    // Parse name
    int nameStart = sub.indexOf("\"name\":\"");
    if (nameStart >= 0) {
      nameStart += 8;
      int nameEnd = sub.indexOf("\"", nameStart);
      String name = sub.substring(nameStart, nameEnd);
      strncpy(b.name, name.c_str(), sizeof(b.name) - 1);
      b.name[sizeof(b.name) - 1] = '\0';
    }
    g_customCanButtons[i] = b;
    cfgMgr.saveCustomCanButton(i, b);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// Heartbeat scanner
// ---------------------------------------------------------------------------
static void scanHeartbeats() {
  if (!g_can)
    return;
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
// Dedicated OTA page - minimal page with no background polling
// ---------------------------------------------------------------------------
static void handleOtaPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Firmware Update</title>
  <style>
    body { font-family: sans-serif; background: #0f172a; color: #e2e8f0; padding: 20px; }
    .card { background: #1e293b; border-radius: 12px; padding: 20px; max-width: 400px; margin: 0 auto; }
    h3 { margin: 0 0 16px; }
    input[type=file] { width: 100%; padding: 8px; background: #0f172a; border: 1px solid #475569; border-radius: 6px; color: #e2e8f0; }
    button { margin-top: 12px; width: 100%; padding: 10px; background: #3b82f6; color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: bold; }
    button:hover { background: #2563eb; }
    .bar-track { margin-top: 12px; height: 8px; background: #334155; border-radius: 4px; overflow: hidden; }
    .bar-fill { height: 100%; background: #22c55e; width: 0%; transition: width 0.3s; }
    .status { margin-top: 12px; padding: 8px; border-radius: 6px; display: none; }
    .success { background: #166534; display: block; }
    .error { background: #991b1b; display: block; }
  </style>
</head>
<body>
  <div class="card">
    <h3>Firmware Update</h3>
    <label style="display:block;margin:8px 0 4px;color:#94a3b8">Select firmware (.bin)</label>
    <input type="file" id="firmware" accept=".bin">
    <button onclick="startUpload()">Update Firmware</button>
    <div class="bar-track"><div class="bar-fill" id="prog"></div></div>
    <div id="status" class="status"></div>
  </div>
  <script>
    function setStatus(msg, type) {
      const el = document.getElementById('status');
      el.textContent = msg;
      el.className = 'status ' + type;
    }
    function startUpload() {
      const file = document.getElementById('firmware').files[0];
      if (!file) { setStatus('Please select a file', 'error'); return; }
      const prog = document.getElementById('prog');
      const xhr = new XMLHttpRequest();
      xhr.timeout = 120000;
      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) prog.style.width = (e.loaded / e.total * 100).toFixed(1) + '%';
      };
      xhr.onload = () => {
        if (xhr.status === 200) {
          setStatus('Update successful! Rebooting...', 'success');
          setTimeout(() => window.close(), 5000);
        } else {
          setStatus('Update failed: ' + xhr.responseText, 'error');
        }
      };
      xhr.onerror = () => setStatus('Network error', 'error');
      xhr.ontimeout = () => setStatus('Upload timeout', 'error');
      xhr.open('POST', '/update');
      xhr.send(file);
    }
  </script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ota_trackModule(uint8_t sa, const CANMessage &msg) {
  g_modules[sa].lastSeen = millis();
  g_modules[sa].addr = sa;
  g_modules[sa].uptime = msg.data[0] | ((uint16_t)msg.data[1] << 8);
  g_modules[sa].data5 = msg.data[5];
  if (msg.data[5] == 16 || msg.data[5] == 8) {
    g_modules[sa].type = 1; // Motor driver
  } else if (msg.data[3] == 1 || msg.data[3] == 2) {
    g_modules[sa].type = 2; // Joystick
  }
}

void ota_setup(const char *hostname) {
  WiFi.mode(WIFI_AP);
  String ssid = String(hostname);
  WiFi.softAP(ssid.c_str(), "12345678");

  if (!MDNS.begin(hostname)) {
    Serial.println("[OTA] mDNS failed");
  } else {
    MDNS.addService("http", "tcp", 80);
  }

  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[OTA] AP '%s' started, IP: %s\n", ssid.c_str(),
                ip.toString().c_str());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/ota", HTTP_GET, handleOtaPage);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/identify", HTTP_POST, handleIdentify);
  server.on("/api/led", HTTP_POST, handleLed);
  server.on("/api/address", HTTP_POST, handleAddress);
  server.on("/api/canoutput", HTTP_GET, handleCanOutputGet);
  server.on("/api/canoutput", HTTP_POST, handleCanOutputPost);
  server.on("/api/motortest", HTTP_GET, handleMotorTestGet);
  server.on("/api/motortest", HTTP_POST, handleMotorTestPost);
  server.on("/api/labels", HTTP_GET, handleLabelsGet);
  server.on("/api/labels", HTTP_POST, handleLabelsPost);
  server.on("/api/btnrules", HTTP_GET, handleBtnRulesGet);
  server.on("/api/btnrules", HTTP_POST, handleBtnRulesPost);
  server.on("/api/custombtns", HTTP_GET, handleCustomBtnsGet);
  server.on("/api/custombtns", HTTP_POST, handleCustomBtnsPost);
  server.on("/api/joycmd", HTTP_POST, handleJoyCmdPost);
  server.on("/api/joymapping", HTTP_GET, handleJoyMapGet);
  server.on("/api/joymapping", HTTP_POST, handleJoyMapPost);
  server.begin();
  ESP2SOTA.begin(&server);
  Serial.println("[OTA] Web server started on port 80");
}

void ota_loop() {
  server.handleClient();
  // scanHeartbeats removed - processCAN() handles all incoming messages
}

bool ota_is_active() { return otaActive; }

#else // not ENABLE_OTA_WEBSERVER

void ota_setup(const char *hostname) { (void)hostname; }
void ota_loop() {}
bool ota_is_active() { return false; }

#endif

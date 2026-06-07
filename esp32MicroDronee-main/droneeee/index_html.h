#ifndef INDEX_HTML_H
#define INDEX_HTML_H

#include <pgmspace.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="tr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="screen-orientation" content="landscape">
<title>Drone Kumanda</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }

  @media (orientation: portrait) {
    body::before {
      content: 'Telefonu yana cevirin';
      position: fixed; top: 0; left: 0; right: 0; bottom: 0;
      background: #000; color: #fff;
      display: flex; align-items: center; justify-content: center;
      font-size: 22px; font-family: sans-serif;
      z-index: 9999; text-align: center; padding: 20px;
    }
    body > * { display: none !important; }
  }

  body {
    background: #0a0a0a;
    color: #e0e0e0;
    font-family: -apple-system, 'Segoe UI', Roboto, Arial, sans-serif;
    touch-action: none; user-select: none;
    -webkit-user-select: none; -webkit-touch-callout: none;
    height: 100dvh; width: 100dvw;
    display: flex; flex-direction: column;
    overflow: hidden;
  }

  /* === UST BAR === */
  .topbar {
    display: flex; justify-content: space-between; align-items: center;
    padding: 4px 14px;
    background: #111;
    border-bottom: 1px solid #333;
    height: 32px; min-height: 32px;
  }
  .topbar .title {
    font-size: 13px; font-weight: 700;
    color: #fff; letter-spacing: 1px;
  }
  #status {
    font-size: 12px; font-weight: 600;
    padding: 2px 10px; border-radius: 10px;
  }
  .st-ok { background: #1a3a1a; color: #4ade80; }
  .st-err { background: #3a1a1a; color: #f87171; }
  .st-wait { background: #2a2a1a; color: #d4d4d4; }

  /* === ANA KONTROL ALANI === */
  .main {
    flex: 1; display: flex;
    padding: 4px 8px;
    gap: 6px;
    min-height: 0;
  }

  /* Sol panel: joystick */
  .panel-joy {
    flex: 1; display: flex;
    flex-direction: column; align-items: center;
    justify-content: center;
  }
  .joy-label {
    font-size: 11px; color: #888;
    margin-bottom: 4px; font-weight: 600;
    letter-spacing: 0.5px;
  }
  .joy-ring {
    width: 150px; height: 150px;
    border: 2px solid #444; border-radius: 50%;
    position: relative;
    background: #141414;
  }
  .joy-ring::before {
    content: ''; position: absolute;
    top: 50%; left: 8%; width: 84%; height: 1px;
    background: #2a2a2a;
  }
  .joy-ring::after {
    content: ''; position: absolute;
    left: 50%; top: 8%; width: 1px; height: 84%;
    background: #2a2a2a;
  }
  .knob {
    width: 48px; height: 48px;
    background: #333; border: 2px solid #555;
    border-radius: 50%;
    position: absolute; top: 50%; left: 50%;
    transform: translate(-50%, -50%);
    pointer-events: none; z-index: 5;
    transition: background 0.1s;
  }
  .knob.active { background: #555; border-color: #888; }

  /* Orta panel: telemetri + butonlar */
  .panel-center {
    width: 200px; min-width: 180px;
    display: flex; flex-direction: column;
    gap: 4px; justify-content: center;
  }

  .telem-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 3px;
  }
  .telem-card {
    background: #141414;
    border: 1px solid #2a2a2a;
    border-radius: 4px;
    padding: 4px 6px;
    text-align: center;
  }
  .telem-lbl { font-size: 9px; color: #666; display: block; }
  .telem-val { font-size: 15px; color: #fff; font-weight: 700; }

  .motor-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 3px;
  }
  .motor-card {
    background: #141414;
    border: 1px solid #222;
    border-radius: 4px;
    padding: 3px 4px;
    text-align: center;
  }
  .motor-lbl { font-size: 8px; color: #555; display: block; }
  .motor-val { font-size: 13px; color: #ccc; font-weight: 700; }

  .btn-row {
    display: flex; gap: 4px;
    margin-top: 2px;
  }
  .btn {
    flex: 1; padding: 8px 4px;
    font-size: 11px; font-weight: 700;
    border-radius: 4px; border: 1px solid #333;
    cursor: pointer; font-family: inherit;
    letter-spacing: 0.5px;
    transition: background 0.15s;
  }
  .btn-cal {
    background: #1a1a1a; color: #e0e0e0;
    border-color: #444;
  }
  .btn-cal:active { background: #333; }
  .btn-stop {
    background: #2a0a0a; color: #f87171;
    border-color: #5a2020;
  }
  .btn-stop:active { background: #4a1a1a; }
  .btn-trim {
    padding: 6px 2px; font-size: 9px;
    background: #1a1a1a; color: #bbb;
    border-color: #333;
  }
  .btn-trim:active { background: #444; }

  /* Gaz slider (sol taraf) */
  .throttle-area {
    display: flex; flex-direction: column;
    align-items: center; justify-content: center;
    width: 50px;
  }
  .throttle-lbl {
    font-size: 9px; color: #666; margin-bottom: 4px;
    font-weight: 600;
  }
  .throttle-val {
    font-size: 13px; color: #fff; font-weight: 700;
    margin-top: 4px;
  }
  .throttle-track {
    width: 28px; height: 130px;
    background: #141414; border: 1px solid #333;
    border-radius: 14px; position: relative;
    cursor: pointer;
  }
  .throttle-fill {
    position: absolute; bottom: 2px; left: 2px; right: 2px;
    background: #444; border-radius: 12px;
    transition: height 0.05s;
  }
  .throttle-thumb {
    position: absolute; left: 50%;
    width: 32px; height: 16px;
    background: #555; border: 1px solid #777;
    border-radius: 8px;
    transform: translate(-50%, 50%);
    pointer-events: none;
  }

  /* Responsive */
  @media (max-height: 300px) {
    .joy-ring { width: 120px; height: 120px; }
    .knob { width: 40px; height: 40px; }
    .panel-center { width: 160px; min-width: 140px; }
    .throttle-track { height: 100px; }
  }
</style>
</head>
<body>

<div class="topbar">
  <span class="title">DRONE KUMANDA</span>
  <span id="status" class="st-wait">Baglaniyor...</span>
</div>

<div class="main">

  <!-- Sol: Gaz Slider -->
  <div class="throttle-area">
    <span class="throttle-lbl">GAZ</span>
    <div class="throttle-track" id="throttleTrack">
      <div class="throttle-fill" id="throttleFill"></div>
      <div class="throttle-thumb" id="throttleThumb"></div>
    </div>
    <span class="throttle-val" id="throttleVal">0</span>
  </div>

  <!-- Sol Joystick: Gaz + Yaw -->
  <div class="panel-joy">
    <span class="joy-label">GAZ / YAW</span>
    <div class="joy-ring" id="joyL"><div class="knob" id="knobL"></div></div>
  </div>

  <!-- Orta: Telemetri + Motor + Butonlar -->
  <div class="panel-center">
    <div class="telem-grid">
      <div class="telem-card"><span class="telem-lbl">PITCH</span><span class="telem-val" id="pV">0.0</span></div>
      <div class="telem-card"><span class="telem-lbl">ROLL</span><span class="telem-val" id="rV">0.0</span></div>
      <div class="telem-card"><span class="telem-lbl">YAW</span><span class="telem-val" id="yV">0.0</span></div>
      <div class="telem-card"><span class="telem-lbl">GAZ</span><span class="telem-val" id="tV">0%</span></div>
    </div>
    <div class="motor-grid">
      <div class="motor-card"><span class="motor-lbl">M1 Sol On</span><span class="motor-val" id="m1V">0</span></div>
      <div class="motor-card"><span class="motor-lbl">M4 Sag On</span><span class="motor-val" id="m4V">0</span></div>
      <div class="motor-card"><span class="motor-lbl">M2 Sol Arka</span><span class="motor-val" id="m2V">0</span></div>
      <div class="motor-card"><span class="motor-lbl">M3 Sag Arka</span><span class="motor-val" id="m3V">0</span></div>
    </div>
    <div class="btn-row">
      <button class="btn btn-cal" onclick="doCal()">KALIBRE</button>
      <button class="btn btn-stop" onclick="doStop()">DURDUR</button>
    </div>
    <div class="btn-row">
      <button class="btn btn-trim" onclick="doTrim('L')" title="Sola Trim">TRM SOL</button>
      <button class="btn btn-trim" onclick="doTrim('R')" title="Saga Trim">TRM SAG</button>
      <button class="btn btn-trim" onclick="doTrim('F')" title="Ileri Trim">TRM ILR</button>
      <button class="btn btn-trim" onclick="doTrim('B')" title="Geri Trim">TRM GER</button>
    </div>
    <div style="text-align:center; font-size:9px; color:#aaa; margin-top:3px;">
      Trim: Roll <span id="trV">0.0</span> / Pitch <span id="tpV">0.0</span>
    </div>
  </div>

  <!-- Sag Joystick: Pitch + Roll -->
  <div class="panel-joy">
    <span class="joy-label">PITCH / ROLL</span>
    <div class="joy-ring" id="joyR"><div class="knob" id="knobR"></div></div>
  </div>

</div>

<script>
let ws, t=0, p=0, r=0, y=0;

// Throttle slider elements
const thrTrack = document.getElementById('throttleTrack');
const thrFill = document.getElementById('throttleFill');
const thrThumb = document.getElementById('throttleThumb');
const thrVal = document.getElementById('throttleVal');

function updateThrottleUI() {
  let pct = (t / 255) * 100;
  let h = thrTrack.clientHeight;
  let fillH = (t / 255) * (h - 4);
  thrFill.style.height = fillH + 'px';
  thrThumb.style.bottom = fillH + 'px';
  thrVal.innerText = Math.round(pct) + '%';
}

// Throttle slider touch
let thrActive = false;
function thrMove(clientY) {
  let rect = thrTrack.getBoundingClientRect();
  let ratio = 1 - ((clientY - rect.top) / rect.height);
  ratio = Math.max(0, Math.min(1, ratio));
  t = Math.round(ratio * 255);
  updateThrottleUI();
}

thrTrack.addEventListener('touchstart', e => { thrActive = true; thrMove(e.touches[0].clientY); }, {passive: true});
thrTrack.addEventListener('touchmove', e => { if(thrActive) { e.preventDefault(); thrMove(e.touches[0].clientY); } }, {passive: false});
thrTrack.addEventListener('touchend', () => { thrActive = false; });
thrTrack.addEventListener('mousedown', e => { thrActive = true; thrMove(e.clientY); });
window.addEventListener('mousemove', e => { if(thrActive) thrMove(e.clientY); });
window.addEventListener('mouseup', () => { thrActive = false; });

// WebSocket - guclu yeniden baglanti sistemi
let wsConnected = false;
let reconnectDelay = 500;
let reconnectTimer = null;
let lastMsgTime = 0;

function connect() {
  // Eski soketi temizle
  if (ws) {
    try { ws.onclose = null; ws.onerror = null; ws.close(); } catch(e) {}
    ws = null;
  }

  let st = document.getElementById('status');
  st.innerText = 'Baglaniyor...';
  st.className = 'st-wait';
  wsConnected = false;

  try {
    ws = new WebSocket('ws://' + location.hostname + ':81/');
  } catch(e) {
    scheduleReconnect();
    return;
  }

  ws.onopen = () => {
    st.innerText = 'Bagli';
    st.className = 'st-ok';
    wsConnected = true;
    reconnectDelay = 500;
    lastMsgTime = Date.now();
  };

  ws.onclose = (e) => {
    wsConnected = false;
    st.innerText = 'Koptu!';
    st.className = 'st-err';
    scheduleReconnect();
  };

  ws.onerror = (e) => {
    wsConnected = false;
  };

  ws.onmessage = (e) => {
    lastMsgTime = Date.now();
    let d = e.data;
    if (d.startsWith('TEL:')) {
      let o = {};
      d.substring(4).split(',').forEach(x => {
        let kv = x.split(':');
        o[kv[0]] = parseFloat(kv[1]);
      });
      if (o['P'] !== undefined) document.getElementById('pV').innerText = o['P'].toFixed(1);
      if (o['R'] !== undefined) document.getElementById('rV').innerText = o['R'].toFixed(1);
      if (o['Y'] !== undefined) document.getElementById('yV').innerText = o['Y'].toFixed(1);
      if (o['T'] !== undefined) document.getElementById('tV').innerText = Math.round(o['T']/255*100) + '%';
      if (o['M1'] !== undefined) document.getElementById('m1V').innerText = Math.round(o['M1']);
      if (o['M2'] !== undefined) document.getElementById('m2V').innerText = Math.round(o['M2']);
      if (o['M3'] !== undefined) document.getElementById('m3V').innerText = Math.round(o['M3']);
      if (o['M4'] !== undefined) document.getElementById('m4V').innerText = Math.round(o['M4']);
      if (o['TR'] !== undefined) document.getElementById('trV').innerText = o['TR'].toFixed(1);
      if (o['TP'] !== undefined) document.getElementById('tpV').innerText = o['TP'].toFixed(1);
    }
  };
}

function scheduleReconnect() {
  if (reconnectTimer) clearTimeout(reconnectTimer);
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connect();
  }, reconnectDelay);
  reconnectDelay = Math.min(reconnectDelay * 2, 3000);
}

function send() {
  if (!ws || ws.readyState !== 1) return;
  try {
    ws.send('CMD:T:' + t + ',P:' + p + ',R:' + r + ',Y:' + y);
  } catch(e) {}
}
setInterval(send, 50);

// Sessiz kopma tespiti: 5sn telemetri gelmezse yeniden baglan
setInterval(() => {
  if (wsConnected && (Date.now() - lastMsgTime > 5000)) {
    wsConnected = false;
    try { ws.close(); } catch(e) {}
    connect();
  }
}, 2000);

function doStop() {
  t=0; p=0; r=0; y=0;
  updateThrottleUI();
  send();
}

function doCal() {
  if(ws && ws.readyState===1) ws.send('CMD:CALIBRATE');
}

function doTrim(dir) {
  if(ws && ws.readyState===1) ws.send('CMD:TRIM:' + dir);
}

// Joystick factory
function mkJoy(jid, kid, isLeft) {
  let j = document.getElementById(jid);
  let k = document.getElementById(kid);
  let active = false, tid = null;
  let sz, cx, cy, maxR;

  function recalc() {
    sz = j.clientWidth;
    cx = sz / 2; cy = sz / 2;
    maxR = sz * 0.36;
  }
  recalc();

  function move(px, py) {
    let rect = j.getBoundingClientRect();
    let dx = px - rect.left - cx;
    let dy = py - rect.top - cy;
    let dist = Math.min(Math.sqrt(dx*dx + dy*dy), maxR);
    let ang = Math.atan2(dy, dx);
    dx = Math.cos(ang) * dist;
    dy = Math.sin(ang) * dist;
    k.style.transform = 'translate(calc(-50% + ' + dx + 'px), calc(-50% + ' + dy + 'px))';
    k.classList.add('active');

    if (isLeft) {
      // Y: gaz (yukari=255, asagi=0)
      let calcT = Math.max(0, Math.min(255, Math.round(((maxR - dy) / (2 * maxR)) * 255)));
      t = calcT;
      updateThrottleUI();
      // X: yaw
      y = Math.round((dx / maxR) * 100);
    } else {
      // Y: pitch
      p = Math.round((-dy / maxR) * 100);
      // X: roll
      r = Math.round((dx / maxR) * 100);
    }
  }

  function end() {
    active = false; tid = null;
    k.classList.remove('active');
    if (!isLeft) {
      p = 0; r = 0;
      k.style.transform = 'translate(-50%, -50%)';
    } else {
      y = 0;
      recalc();
      let yOff = maxR - (t * 2 * maxR / 255);
      k.style.transform = 'translate(-50%, calc(-50% + ' + yOff + 'px))';
    }
  }

  j.addEventListener('touchstart', e => {
    recalc();
    for (let i = 0; i < e.touches.length; i++) {
      let rect = j.getBoundingClientRect();
      if (e.touches[i].clientX >= rect.left && e.touches[i].clientX <= rect.right &&
          e.touches[i].clientY >= rect.top && e.touches[i].clientY <= rect.bottom) {
        tid = e.touches[i].identifier;
        active = true;
        move(e.touches[i].clientX, e.touches[i].clientY);
        break;
      }
    }
  }, {passive: true});

  j.addEventListener('touchmove', e => {
    if (!active) return;
    e.preventDefault();
    for (let i = 0; i < e.touches.length; i++) {
      if (e.touches[i].identifier === tid) {
        move(e.touches[i].clientX, e.touches[i].clientY);
        break;
      }
    }
  }, {passive: false});

  j.addEventListener('touchend', e => {
    if (!active) return;
    let found = false;
    for (let i = 0; i < e.touches.length; i++) {
      if (e.touches[i].identifier === tid) { found = true; break; }
    }
    if (!found) end();
  });

  j.addEventListener('mousedown', e => { recalc(); active = true; move(e.clientX, e.clientY); });
  window.addEventListener('mousemove', e => { if (active) move(e.clientX, e.clientY); });
  window.addEventListener('mouseup', () => { if (active) end(); });
}

window.onload = () => {
  connect();
  mkJoy('joyL', 'knobL', true);
  mkJoy('joyR', 'knobR', false);
  // Sol joystick: gaz 0 = knob en altta
  let sz = document.getElementById('joyL').clientWidth;
  let maxR = sz * 0.36;
  document.getElementById('knobL').style.transform = 'translate(-50%, calc(-50% + ' + maxR + 'px))';
  updateThrottleUI();
};
</script>
</body>
</html>
)rawliteral";

#endif

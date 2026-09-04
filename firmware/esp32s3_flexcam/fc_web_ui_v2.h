// =====================================================================
//  fc_web_ui_v2.h — interfaz web (se sirve desde flash, no usa Internet).
//
//  La estabilización NO es un truco de CSS: cada fotograma se dibuja en
//  un <canvas> ya girado y recortado. Ese mismo canvas es el que se
//  exporta a foto y el que alimenta a MediaRecorder, así que lo que se
//  descarga está realmente estabilizado, no sólo "se ve recto".
//  El ESP32 sigue mandando su MJPEG sin tocar un solo píxel.
// =====================================================================
#pragma once
#include <Arduino.h>

static const char FC_INDEX_HTML[] PROGMEM = R"HTMLDOC(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover,user-scalable=no">
<meta name="theme-color" content="#05060a">
<title>FlexCam S26</title>
<style>
:root{
  --bg:#05060a; --panel:rgba(18,20,26,.72); --panel2:rgba(30,33,42,.78);
  --line:rgba(255,255,255,.10); --txt:#eef1f6; --dim:#8f97a6;
  --amber:#ffb02e; --amber2:#ff7a18; --cyan:#4dd6ff;
  --ok:#4ade80; --bad:#ff5d5d; --rec:#ff3b30;
  --r:16px;
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{height:100%;margin:0;background:var(--bg);color:var(--txt);
  font:14px/1.35 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
  overscroll-behavior:none;-webkit-user-select:none;user-select:none}
button,select{font:inherit;color:inherit}
#app{height:100dvh;display:grid;grid-template-rows:auto 1fr auto;gap:6px;padding:6px;
  padding-top:max(6px,env(safe-area-inset-top));
  padding-bottom:max(6px,env(safe-area-inset-bottom));
  max-width:100vw;overflow-x:hidden}

/* ---------- barra superior ---------- */
#topbar{display:flex;gap:5px;align-items:center;flex-wrap:wrap;min-width:0;
  background:var(--panel);border:1px solid var(--line);border-radius:var(--r);
  padding:7px 9px;backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px)}
.chip{display:inline-flex;align-items:center;gap:5px;padding:3px 9px;border-radius:999px;
  background:rgba(255,255,255,.055);border:1px solid var(--line);font-size:11.5px;white-space:nowrap}
.chip b{font-weight:600;font-variant-numeric:tabular-nums}
.dot{width:7px;height:7px;border-radius:50%;background:var(--bad);flex:none}
.dot.on{background:var(--ok);box-shadow:0 0 7px var(--ok)}
.dot.warn{background:var(--amber)}
#brand{font-weight:700;letter-spacing:.3px;margin-right:1px;font-size:13px}
#brand span{color:var(--amber)}

/* ---------- visor ---------- */
#stage{position:relative;min-height:0;border-radius:var(--r);overflow:hidden;
  background:#000;border:1px solid var(--line);display:grid;place-items:center}
#view{max-width:100%;max-height:100%;display:block;background:#000}
#hud{position:absolute;inset:0;pointer-events:none}
#lvl{position:absolute;left:50%;top:50%;width:38%;height:2px;margin-left:-19%;
  background:rgba(255,255,255,.5);border-radius:2px;transform-origin:50% 50%;display:none}
#lvl.on{display:block}
#lvl.lock{background:var(--amber);box-shadow:0 0 10px var(--amber)}
#grid{position:absolute;inset:0;display:none;
  background-image:linear-gradient(rgba(255,255,255,.14) 1px,transparent 1px),
                   linear-gradient(90deg,rgba(255,255,255,.14) 1px,transparent 1px);
  background-size:33.33% 33.33%}
#grid.on{display:block}
#notice{position:absolute;left:50%;top:10px;transform:translateX(-50%);
  background:rgba(0,0,0,.76);border:1px solid var(--line);border-radius:999px;
  padding:5px 13px;font-size:12px;opacity:0;transition:opacity .16s;white-space:nowrap;max-width:92%}
#notice.show{opacity:1}
#warn{position:absolute;left:50%;bottom:10px;transform:translateX(-50%);
  background:rgba(255,176,46,.16);border:1px solid var(--amber);color:var(--amber);
  border-radius:999px;padding:4px 11px;font-size:11.5px;display:none;white-space:nowrap}
#warn.on{display:block}
#recbadge{position:absolute;left:10px;top:10px;display:none;align-items:center;gap:6px;
  background:rgba(0,0,0,.7);border:1px solid var(--rec);border-radius:999px;padding:4px 11px;
  font-size:12px;font-variant-numeric:tabular-nums}
#recbadge.on{display:flex}
#recdot{width:9px;height:9px;border-radius:50%;background:var(--rec);animation:bl 1s infinite}
@keyframes bl{50%{opacity:.25}}
#flash{position:absolute;inset:0;background:#fff;opacity:0;pointer-events:none}
#flash.go{animation:fl .26s ease-out}
@keyframes fl{0%{opacity:.8}100%{opacity:0}}

/* ---------- dock inferior ---------- */
#dock{background:var(--panel);border:1px solid var(--line);border-radius:var(--r);
  padding:8px;backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);
  display:grid;gap:8px;min-width:0}
#tabs{display:flex;gap:6px;justify-content:center}
.tab{padding:5px 18px;border-radius:999px;background:transparent;border:1px solid transparent;
  color:var(--dim);font-size:13px;letter-spacing:.4px;cursor:pointer}
.tab[aria-selected="true"]{color:var(--amber);border-color:rgba(255,176,46,.4);
  background:rgba(255,176,46,.10);font-weight:600}
#modes{display:flex;gap:5px;overflow-x:auto;scrollbar-width:none;min-width:0;padding-bottom:2px}
#modes::-webkit-scrollbar{display:none}
.mode{flex:none;padding:6px 13px;border-radius:999px;background:rgba(255,255,255,.055);
  border:1px solid var(--line);white-space:nowrap;font-size:12.5px;cursor:pointer}
.mode[aria-pressed="true"]{background:rgba(77,214,255,.14);border-color:var(--cyan);color:#cdefff}
.mode:disabled{opacity:.4;cursor:not-allowed}
#main{display:flex;align-items:center;justify-content:space-between;gap:10px;min-width:0}
#gal{width:52px;height:52px;border-radius:12px;border:2px solid rgba(255,255,255,.28);
  background:rgba(255,255,255,.05) center/cover no-repeat;flex:none;cursor:pointer;padding:0;
  overflow:hidden;display:grid;place-items:center;font-size:10px;color:var(--dim)}
#gal img{width:100%;height:100%;object-fit:cover;display:block}
#shutter{width:70px;height:70px;border-radius:50%;border:3px solid rgba(255,255,255,.92);
  background:radial-gradient(circle at 50% 40%,#fff,#e4e7ee);cursor:pointer;flex:none;
  box-shadow:0 5px 20px rgba(0,0,0,.5);transition:transform .07s}
#shutter:active{transform:scale(.93)}
#shutter:disabled{opacity:.45}
#shutter.vid{background:radial-gradient(circle at 50% 40%,#ff6b60,var(--rec));border-color:#ffd9d6}
#shutter.recording{border-radius:18px;width:60px;height:60px;
  background:var(--rec);border-color:#fff}
#hlwrap{display:flex;align-items:center;gap:4px;flex:none}
#hl{display:flex;flex-direction:column;align-items:center;gap:2px;width:56px;padding:6px 2px;
  border-radius:14px;background:rgba(255,255,255,.055);border:1px solid var(--line);cursor:pointer}
#hl svg{width:22px;height:22px;fill:none;stroke:currentColor;stroke-width:1.9;
  stroke-linecap:round;stroke-linejoin:round}
#hl small{font-size:9px;letter-spacing:.3px;color:var(--dim)}
#hl[aria-pressed="true"]{background:linear-gradient(180deg,rgba(255,176,46,.26),rgba(255,122,24,.18));
  border-color:var(--amber);color:var(--amber);box-shadow:0 0 16px rgba(255,176,46,.32)}
#hl[aria-pressed="true"] small{color:var(--amber)}
#hl.noref{border-color:var(--amber2);color:var(--amber2)}
#caret{width:26px;height:44px;border-radius:10px;background:rgba(255,255,255,.055);
  border:1px solid var(--line);display:grid;place-items:center;cursor:pointer;padding:0}
#caret svg{width:14px;height:14px;transition:transform .2s;fill:none;stroke:currentColor;stroke-width:2.4;stroke-linecap:round}
#app.panel #caret svg{transform:rotate(180deg)}

/* ---------- panel de detalle ---------- */
#panel{display:none;border-top:1px solid var(--line);padding-top:8px;gap:8px;
  max-height:44dvh;overflow-y:auto;-webkit-overflow-scrolling:touch}
#app.panel #panel{display:grid}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(112px,1fr));gap:6px}
.kv{background:rgba(255,255,255,.045);border:1px solid var(--line);border-radius:10px;padding:6px 8px;min-width:0}
.kv .k{font-size:9.5px;color:var(--dim);text-transform:uppercase;letter-spacing:.5px;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.kv .v{font-size:15px;font-weight:600;font-variant-numeric:tabular-nums;margin-top:1px;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.kv .v.s{font-size:12px;font-weight:500;white-space:normal}
.kv.wide{grid-column:1/-1}
.kv.hi{border-color:rgba(255,176,46,.45);background:rgba(255,176,46,.08)}
.bar{height:3px;border-radius:2px;background:rgba(255,255,255,.10);margin-top:5px;overflow:hidden}
.bar i{display:block;height:100%;background:var(--cyan);width:0}
.sect{font-size:10px;color:var(--dim);text-transform:uppercase;letter-spacing:.8px;
  margin:4px 0 -2px;grid-column:1/-1}
.row{display:flex;gap:6px;flex-wrap:wrap;align-items:flex-end}
.btn{padding:7px 12px;border-radius:10px;background:rgba(255,255,255,.06);
  border:1px solid var(--line);cursor:pointer;font-size:12.5px;white-space:nowrap}
.btn:active{background:rgba(255,255,255,.12)}
.btn.on{background:rgba(255,176,46,.18);border-color:var(--amber);color:var(--amber)}
label.f{display:flex;flex-direction:column;gap:3px;font-size:9.5px;color:var(--dim);
  text-transform:uppercase;letter-spacing:.5px;min-width:0}
select{background:rgba(255,255,255,.06);border:1px solid var(--line);border-radius:9px;
  padding:7px 6px;font-size:12.5px;cursor:pointer;max-width:100%}

/* ---------- tablet / horizontal ---------- */
@media(min-width:900px) and (orientation:landscape){
  #app{grid-template-columns:1fr 340px;grid-template-rows:auto 1fr;
       grid-template-areas:"top side" "stage side"}
  #topbar{grid-area:top} #stage{grid-area:stage}
  #dock{grid-area:side;align-content:start;min-height:0;overflow-y:auto}
  #panel{max-height:none}
}
@media(orientation:landscape) and (max-height:500px){
  #shutter{width:56px;height:56px} #gal{width:42px;height:42px}
  #hl{width:48px} .mode{padding:5px 10px;font-size:11.5px}
  #panel{max-height:38dvh}
}
</style>
</head>
<body>
<div id="app">

  <header id="topbar">
    <span id="brand">Flex<span>Cam</span></span>
    <span class="chip"><i class="dot" id="dNet"></i><b id="tNet">Conectando</b></span>
    <span class="chip">Res <b id="tRes">—</b></span>
    <span class="chip">FPS <b id="tFps">0.0</b></span>
    <span class="chip"><i class="dot" id="dImu"></i>IMU <b id="tImu">—</b></span>
    <span class="chip">Temp <b id="tTemp">—</b></span>
  </header>

  <main id="stage">
    <canvas id="view" width="800" height="600"></canvas>
    <div id="hud">
      <div id="grid"></div>
      <div id="lvl"></div>
      <div id="warn"></div>
      <div id="recbadge"><i id="recdot"></i><span id="rectime">0:00</span><span id="recsize"></span></div>
    </div>
    <div id="notice"></div>
    <div id="flash"></div>
  </main>

  <section id="dock">
    <div id="tabs" role="tablist">
      <button class="tab" id="tabPhoto" role="tab" aria-selected="true">FOTO</button>
      <button class="tab" id="tabVideo" role="tab" aria-selected="false">VÍDEO</button>
    </div>
    <div id="modes"></div>
    <div id="main">
      <button id="gal" title="Última captura">vacío</button>
      <button id="shutter" title="Capturar"></button>
      <div id="hlwrap">
        <button id="hl" aria-pressed="false" title="Horizon Lock">
          <svg viewBox="0 0 24 24"><path d="M2.5 12h19"/><circle cx="12" cy="12" r="7.6"/><path d="M12 4.4v3M12 16.6v3"/></svg>
          <small>HORIZONTE</small>
        </button>
        <button id="caret" title="Detalles"><svg viewBox="0 0 24 24"><path d="M6 9l6 6 6-6"/></svg></button>
      </div>
    </div>

    <div id="panel">
      <div class="sect">Estado</div>
      <div class="grid">
        <div class="kv wide hi"><div class="k">Horizon Lock</div><div class="v s" id="vState">Apagado</div></div>
        <div class="kv"><div class="k">Horizonte bruto</div><div class="v" id="vHraw">—</div></div>
        <div class="kv"><div class="k">Horizonte continuo</div><div class="v" id="vHcont">—</div></div>
        <div class="kv"><div class="k">Referencia</div><div class="v" id="vHref">—</div></div>
        <div class="kv hi"><div class="k">Compensación</div><div class="v" id="vComp">—</div></div>
        <div class="kv"><div class="k">Dirección</div><div class="v s" id="vDir">—</div></div>
        <div class="kv"><div class="k">Rotación total</div><div class="v" id="vTotal">—</div></div>
        <div class="kv"><div class="k">Zoom / recorte</div><div class="v" id="vZoom">—</div></div>
      </div>

      <div class="sect">IMU</div>
      <div class="grid">
        <div class="kv"><div class="k">Roll</div><div class="v" id="vRoll">—</div></div>
        <div class="kv"><div class="k">Pitch</div><div class="v" id="vPitch">—</div></div>
        <div class="kv"><div class="k">Yaw</div><div class="v" id="vYaw">—</div></div>
        <div class="kv"><div class="k">Frecuencia real</div><div class="v" id="vHz">—</div></div>
        <div class="kv"><div class="k">Confianza gravedad</div><div class="v" id="vConf">—</div>
          <div class="bar"><i id="barConf"></i></div></div>
        <div class="kv"><div class="k">Calibración</div><div class="v s" id="vAcc">—</div>
          <div class="bar"><i id="barAcc"></i></div></div>
        <div class="kv wide"><div class="k">Cuaternión (i, j, k, real)</div><div class="v s" id="vQuat">—</div></div>
        <div class="kv wide"><div class="k">Vector gravedad (x, y, z)</div><div class="v s" id="vGrav">—</div></div>
      </div>

      <div class="sect">Rendimiento</div>
      <div class="grid">
        <div class="kv"><div class="k">FPS captura / objetivo</div><div class="v" id="vCapFps">—</div></div>
        <div class="kv"><div class="k">FPS enviados</div><div class="v" id="vSendFps">—</div></div>
        <div class="kv"><div class="k">FPS render · decod.</div><div class="v" id="vDrawFps">—</div></div>
        <div class="kv"><div class="k">Edad frame nav · esp</div><div class="v" id="vAge">—</div></div>
        <div class="kv"><div class="k">Latencia rel.</div><div class="v" id="vLat">—</div></div>
        <div class="kv"><div class="k">Perdidos captura / pool</div><div class="v" id="vDrop">—</div></div>
        <div class="kv"><div class="k">Temperatura</div><div class="v" id="vTemp2">—</div></div>
        <div class="kv"><div class="k">Reinicios IMU</div><div class="v" id="vRs">—</div></div>
        <div class="kv wide"><div class="k">Memoria libre</div><div class="v s" id="vMem">—</div></div>
      </div>

      <div class="sect">Controles</div>
      <div class="row">
        <button class="btn" id="bLock">Activar Lock</button>
        <button class="btn" id="bRecenter">Recentrar horizonte</button>
        <button class="btn" id="bGrid">Cuadrícula</button>
      </div>
      <div class="row">
        <label class="f">Rotar vista (manual)
          <select id="selRot">
            <option value="0">0°</option><option value="90">90°</option>
            <option value="180">180°</option><option value="270">270°</option>
            <option value="360">360°</option>
          </select>
        </label>
        <label class="f">Perfil de encuadre
          <select id="selCrop">
            <option value="dyn">Dinámico · sin bordes</option>
            <option value="fix">Estable 360° · zoom fijo</option>
            <option value="wide">Amplio · puede mostrar bordes</option>
          </select>
        </label>
      </div>
      <div class="row">
        <label class="f">Montaje IMU
          <select id="selMount">
            <option value="0">0°</option><option value="90">90°</option>
            <option value="180">180°</option><option value="270">270°</option>
          </select>
        </label>
        <label class="f">Eje óptico del IMU
          <select id="selPlane">
            <option value="0">Z (módulo paralelo)</option>
            <option value="1">Y</option>
            <option value="2">X</option>
          </select>
        </label>
        <label class="f">Sentido
          <select id="selInv"><option value="0">Normal</option><option value="1">Invertido</option></select>
        </label>
      </div>
      <div class="row">
        <label class="f">Calidad de vídeo
          <select id="selVq">
            <option value="24">24 fps · normal</option>
            <option value="30">30 fps · fluido</option>
            <option value="15">15 fps · ligero</option>
          </select>
        </label>
        <button class="btn" id="bFlip">Voltear sensor</button>
      </div>
      <div class="kv wide"><div class="k">Última captura</div><div class="v s" id="vShot">—</div></div>
    </div>
  </section>
</div>
<script>
"use strict";
// ===================================================================
//  Ajustes del encuadre. Todo el giro y el recorte ocurren aquí, sobre
//  un canvas real: lo que se ve es exactamente lo que se exporta.
// ===================================================================
const WIDE_ZOOM_MAX = 1.25;   // tope del perfil "Amplio"
const CANVAS_MAX_PX = 1280;   // techo del lado mayor del canvas, por el móvil
const REC_BITRATE_PER_PX = 5; // bits/s por píxel y fps -> bitrate del vídeo

const $ = id => document.getElementById(id);
const view = $("view"), ctx = view.getContext("2d", { alpha:false, desynchronized:true });
const app  = $("app");

// --------------------------- estado --------------------------------
const S = {
  modes:[], mode:null, srcW:800, srcH:600, camReady:false,
  capFpsEsp:0, sendFps:0, targetFps:0, drop:0, poolDrop:0,
  capMs:0, sendMs:0, ageEsp:0, thermal:0,
  heap:0, largest:0, psram:0, temp:null,
  // IMU (última muestra, nunca una cola)
  imu:{ ok:false, state:"—", hz:0, acc:0, roll:0, pitch:0, yaw:0,
        hraw:0, hcont:0, hfilt:0, conf:0, valid:false, seq:0, ts:0,
        q:[0,0,0,1], g:[0,0,-1], epoch:0, mount:0, invert:false, plane:0, resets:0 },
  // frames
  frame:{ bmp:null, w:0, h:0, tArrive:0, tsEsp:0, seq:0 },
  capFps:0, drawFps:0, latency:null
};
let lockOn=false, refAngle=0, haveRef=false, manualRot=0, cropMode="dyn", gridOn=false;
let tab="photo", busy=false, recording=false;
let curComp=0, curZoom=1, curTotal=0, lastEpochSeen=-1;

const norm180 = a => { a=(a+180)%360; if(a<0)a+=360; return a-180; };
const fmt = (v,d=2) => (v>=0?"+":"−")+Math.abs(v).toFixed(d)+"°";
const clamp = (v,a,b) => v<a?a:(v>b?b:v);

// ---------- geometría: escala mínima para cubrir sin bordes ----------
function coverScale(W,H,w,h,deg){
  const t=deg*Math.PI/180, c=Math.abs(Math.cos(t)), s=Math.abs(Math.sin(t));
  return Math.max((W*c+H*s)/w,(W*s+H*c)/h);
}
// Peor caso sobre TODOS los ángulos: sqrt(W²+H²)/min(w,h).
// Para 4:3 dentro de 4:3 son 5/3 = 1.667. No es un número inventado.
function cover360(W,H,w,h){ return Math.hypot(W,H)/Math.min(w,h); }

// ---------- aviso flotante ----------
let noticeT=null;
function notice(txt,ms){
  const n=$("notice"); n.textContent=txt; n.classList.add("show");
  clearTimeout(noticeT); noticeT=setTimeout(()=>n.classList.remove("show"),ms||1600);
}
function warn(txt){
  const w=$("warn");
  if(txt){ w.textContent=txt; w.classList.add("on"); } else w.classList.remove("on");
}

// ===================================================================
//  Fuente de vídeo.
//  Preferente: fetch + troceado del multipart + createImageBitmap. Da la
//  marca de tiempo real de cada frame (cabeceras X-Ts / X-Seq), que es lo
//  que permite mostrar edad y latencia de verdad en vez de estimarlas.
//  Respaldo: <img> con MJPEG, si el navegador no trae streams o bitmaps.
// ===================================================================
const Stream = {
  ctrl:null, imgEl:null, mode:null, running:false, decoding:false,
  capCount:0, capWindow:0, gen:0,

  url(){ return "http://"+location.hostname+":81/stream?g="+Date.now(); },

  start(){
    this.stop();
    this.running=true;
    const gen=++this.gen;
    const canFetch = typeof ReadableStream!=="undefined" &&
                     typeof createImageBitmap!=="undefined" && !!window.AbortController;
    if(canFetch){ this.mode="fetch"; this._fetchLoop(gen); }
    else { this.mode="img"; this._imgFallback(); }
  },

  stop(){
    this.running=false; this.gen++;
    if(this.ctrl){ try{ this.ctrl.abort(); }catch(e){} this.ctrl=null; }
    if(this.imgEl){ this.imgEl.onerror=null; this.imgEl.removeAttribute("src"); this.imgEl=null; }
    if(S.frame.bmp && S.frame.bmp.close){ try{ S.frame.bmp.close(); }catch(e){} }
    S.frame.bmp=null;
  },

  async _fetchLoop(gen){
    while(this.running && gen===this.gen){
      try{
        this.ctrl=new AbortController();
        const res=await fetch(this.url(),{signal:this.ctrl.signal,cache:"no-store"});
        if(!res.ok||!res.body) throw new Error("HTTP "+res.status);
        const ct=res.headers.get("Content-Type")||"";
        const bm=/boundary=([^;\s]+)/i.exec(ct);
        const boundary=new TextEncoder().encode("--"+(bm?bm[1]:"fcframe"));
        await this._parse(res.body.getReader(), boundary, gen);
      }catch(e){
        if(!this.running||gen!==this.gen) return;
        // Si el troceado no es viable en este navegador, pasar al <img>.
        if(this.mode==="fetch" && e && e.name!=="AbortError" && this.capCount===0){
          this.mode="img"; this._imgFallback(); return;
        }
      }
      if(!this.running||gen!==this.gen) return;
      await new Promise(r=>setTimeout(r,600));
    }
  },

  async _parse(reader, boundary, gen){
    let buf=new Uint8Array(0);
    const push=chunk=>{ const n=new Uint8Array(buf.length+chunk.length);
                        n.set(buf); n.set(chunk,buf.length); buf=n; };
    const find=(hay,needle,from)=>{
      outer: for(let i=from;i<=hay.length-needle.length;i++){
        for(let j=0;j<needle.length;j++) if(hay[i+j]!==needle[j]) continue outer;
        return i;
      } return -1;
    };
    const CRLF2=new Uint8Array([13,10,13,10]);
    while(this.running && gen===this.gen){
      const {done,value}=await reader.read();
      if(done) break;
      push(value);
      // Puede haber varias partes completas en un mismo trozo.
      for(;;){
        const b=find(buf,boundary,0);
        if(b<0){ if(buf.length>1<<21) buf=new Uint8Array(0); break; }
        const hEnd=find(buf,CRLF2,b);
        if(hEnd<0) break;
        const head=new TextDecoder().decode(buf.subarray(b,hEnd));
        const lenM=/Content-Length:\s*(\d+)/i.exec(head);
        if(!lenM){ buf=buf.subarray(hEnd+4); continue; }
        const len=+lenM[1];
        const start=hEnd+4;
        if(buf.length<start+len) break;             // aún falta cuerpo
        const jpeg=buf.slice(start,start+len);
        buf=buf.subarray(start+len);
        const tsM=/X-Ts:\s*(\d+)/i.exec(head), sqM=/X-Seq:\s*(\d+)/i.exec(head);
        this._decode(jpeg, tsM?+tsM[1]:0, sqM?+sqM[1]:0, gen);
      }
    }
  },

  // "El último gana": si ya hay una decodificación en curso se descarta el
  // frame anterior pendiente. Así nunca se forma una cola que añada retraso.
  _pending:null,
  _decode(bytes, tsEsp, seq, gen){
    this._pending={bytes,tsEsp,seq};
    if(this.decoding) return;
    this.decoding=true;
    const run=async()=>{
      while(this._pending && this.running && gen===this.gen){
        const job=this._pending; this._pending=null;
        try{
          const bmp=await createImageBitmap(new Blob([job.bytes],{type:"image/jpeg"}));
          if(!this.running||gen!==this.gen){ bmp.close&&bmp.close(); break; }
          if(S.frame.bmp && S.frame.bmp.close) { try{S.frame.bmp.close();}catch(e){} }
          S.frame={ bmp, w:bmp.width, h:bmp.height, tArrive:performance.now(),
                    tsEsp:job.tsEsp, seq:job.seq };
          this._noteCapture(job.tsEsp);
        }catch(e){ /* frame corrupto: se ignora, el siguiente llega solo */ }
      }
      this.decoding=false;
    };
    run();
  },

  _imgFallback(){
    const img=new Image();
    img.crossOrigin="anonymous";
    this.imgEl=img;
    img.onerror=()=>{ if(this.running) setTimeout(()=>{ if(this.running) img.src=this.url(); },800); };
    img.onload =()=>{ S.frame={ bmp:img, w:img.naturalWidth, h:img.naturalHeight,
                                tArrive:performance.now(), tsEsp:0, seq:0 };
                      this._noteCapture(0); };
    img.src=this.url();
    // Un <img> MJPEG no avisa por frame: se cuenta el ritmo de pintado.
    const tick=()=>{ if(!this.running||this.imgEl!==img) return;
      if(img.naturalWidth){ S.frame.bmp=img; S.frame.w=img.naturalWidth; S.frame.h=img.naturalHeight;
        S.frame.tArrive=performance.now(); this._noteCapture(0); }
      setTimeout(tick,40); };
    setTimeout(tick,300);
  },

  _minOffset:null,
  _noteCapture(tsEsp){
    this.capCount++;
    const now=performance.now();
    if(!this.capWindow) this.capWindow=now;
    if(now-this.capWindow>=1000){
      S.capFps=this.capCount*1000/(now-this.capWindow);
      this.capCount=0; this.capWindow=now;
    }
    if(tsEsp>0){
      // Latencia relativa: se descuenta el mejor trayecto observado, que
      // absorbe el desfase de relojes. Es un valor relativo, no absoluto.
      const d=now-tsEsp;
      if(this._minOffset===null||d<this._minOffset) this._minOffset=d;
      S.latency=Math.max(0,d-this._minOffset);
    }
  }
};

// ===================================================================
//  Render: un canvas, un dibujo por frame de pantalla.
// ===================================================================
let drawCount=0, drawWindow=0, canvasLocked=false;

function targetCanvasSize(){
  let w=S.frame.w||S.srcW, h=S.frame.h||S.srcH;
  const m=Math.max(w,h);
  if(m>CANVAS_MAX_PX){ const k=CANVAS_MAX_PX/m; w=Math.round(w*k); h=Math.round(h*k); }
  // Con 90/270 el lienzo se pone de canto: así la imagen encaja sin recortar
  // un tercio sólo por estar de lado.
  const q=((manualRot%360)+360)%360;
  return (q===90||q===270) ? {w:h,h:w} : {w,h};
}

function computeTransform(){
  const base=((manualRot%360)+360)%360;
  let comp=0, noRef=false;
  if(lockOn && haveRef){
    if(S.imu.ok && S.imu.valid){
      comp = -(S.imu.hfilt - refAngle);     // continuo: pasa 180/360/720 sin salto
      curComp=comp;
    }else{
      comp = curComp;                       // congelado, el último válido
      noRef = true;
    }
  }else{ curComp=0; }
  return { base, comp, total: base+comp, noRef };
}

function draw(){
  requestAnimationFrame(draw);
  const f=S.frame;
  const {base, comp, total, noRef} = computeTransform();
  curTotal=total;

  const t=targetCanvasSize();
  if(!canvasLocked && (view.width!==t.w||view.height!==t.h)){ view.width=t.w; view.height=t.h; }
  const W=view.width, H=view.height;

  if(!f.bmp||!f.w||!f.h){
    ctx.fillStyle="#000"; ctx.fillRect(0,0,W,H);
  }else{
    const dyn=coverScale(W,H,f.w,f.h,total);
    let z;
    if(cropMode==="fix")      z=Math.max(cover360(W,H,f.w,f.h), dyn);
    else if(cropMode==="wide"){ z=Math.min(dyn, Math.max(coverScale(W,H,f.w,f.h,base),WIDE_ZOOM_MAX)); }
    else                      z=dyn;
    curZoom=z;

    ctx.save();
    ctx.fillStyle="#000"; ctx.fillRect(0,0,W,H);
    ctx.translate(W/2,H/2);
    ctx.rotate(total*Math.PI/180);
    ctx.scale(z,z);
    ctx.imageSmoothingQuality="high";
    try{ ctx.drawImage(f.bmp,-f.w/2,-f.h/2,f.w,f.h); }catch(e){}
    ctx.restore();

    if(cropMode==="wide" && dyn>z+0.001) warn("Encuadre amplio: pueden verse bordes");
    else if(noRef) warn("Horizonte sin referencia");
    else warn(null);
  }

  drawCount++;
  const now=performance.now();
  if(!drawWindow) drawWindow=now;
  if(now-drawWindow>=1000){ S.drawFps=drawCount*1000/(now-drawWindow); drawCount=0; drawWindow=now; }

  const lv=$("lvl");
  lv.classList.toggle("on", S.imu.ok);
  lv.classList.toggle("lock", lockOn);
  lv.style.transform="rotate("+(lockOn? total-base : (S.imu.valid? S.imu.hfilt-(haveRef?refAngle:0) : 0)).toFixed(2)+"deg)";

  if(now-lastPanel>100){ lastPanel=now; paintPanel(); }
}
let lastPanel=0;

// ===================================================================
//  Panel
// ===================================================================
function paintPanel(){
  if(!app.classList.contains("panel")) return;   // plegado: no gastar CPU
  const i=S.imu;
  let est="Apagado";
  if(!i.ok) est="IMU no disponible";
  else if(lockOn && !i.valid) est="Sin referencia · compensación congelada";
  else if(lockOn) est="Activo";
  $("vState").textContent=est;
  $("hl").classList.toggle("noref", lockOn && i.ok && !i.valid);

  $("vHraw").textContent  = i.ok? fmt(i.hraw) : "—";
  $("vHcont").textContent = i.ok? fmt(i.hfilt,1) : "—";
  $("vHref").textContent  = haveRef? fmt(refAngle,1) : "sin tomar";
  $("vComp").textContent  = lockOn? fmt(curComp) : "—";
  $("vDir").textContent   = !lockOn? "—" : (Math.abs(curComp)<0.05? "centrado"
                            : (curComp>0? "horaria ↻" : "antihoraria ↺"));
  $("vTotal").textContent = fmt(curTotal,1);
  $("vZoom").textContent  = curZoom.toFixed(3)+"×";

  $("vRoll").textContent  = i.ok? fmt(i.roll) : "—";
  $("vPitch").textContent = i.ok? fmt(i.pitch): "—";
  $("vYaw").textContent   = i.ok? fmt(i.yaw)  : "—";
  $("vHz").textContent    = i.ok? i.hz.toFixed(0)+" Hz" : "—";
  $("vConf").textContent  = i.ok? i.conf.toFixed(3) : "—";
  $("barConf").style.width=(clamp(i.conf,0,1)*100)+"%";
  $("barConf").style.background = i.valid? "var(--cyan)" : "var(--amber)";
  $("vAcc").textContent   = i.ok? (["Sin calibrar","Baja","Media","Alta"][i.acc]||"—") : "—";
  $("barAcc").style.width =(i.acc/3*100)+"%";
  $("vQuat").textContent  = i.ok? i.q.map(v=>v.toFixed(4)).join("  ") : "—";
  $("vGrav").textContent  = i.ok? i.g.map(v=>v.toFixed(3)).join("  ") : "—";

  $("vCapFps").textContent = S.capFpsEsp.toFixed(1)+
                             (S.targetFps? " / "+S.targetFps : "");
  $("vSendFps").textContent= S.sendFps.toFixed(1);
  $("vDrawFps").textContent= S.drawFps.toFixed(0)+" · dec "+S.capFps.toFixed(0);
  const age = S.frame.tArrive? (performance.now()-S.frame.tArrive) : null;
  $("vAge").textContent    = (age===null? "—" : age.toFixed(0)+" ms")+
                             (S.ageEsp? " · esp "+S.ageEsp+" ms" : "");
  $("vLat").textContent    = S.latency===null? "—" : S.latency.toFixed(0)+" ms";
  $("vDrop").textContent   = S.drop+" / "+S.poolDrop;
  $("vTemp2").textContent  = (S.temp===null? "—" : S.temp.toFixed(1)+" °C")+
                             (S.thermal? (S.thermal>1? " · crítico":" · limitado") : "");
  $("vRs").textContent     = i.resets;
  $("vMem").textContent    = Math.round(S.heap/1024)+" KB internos (mayor "+
                             Math.round(S.largest/1024)+" KB) · "+
                             Math.round(S.psram/1024)+" KB PSRAM · captura "+
                             S.capMs.toFixed(1)+" ms · envío "+S.sendMs.toFixed(1)+" ms";
}
function paintTop(){
  $("tRes").textContent = (S.frame.w||S.srcW)+"×"+(S.frame.h||S.srcH);
  $("tFps").textContent = S.sendFps.toFixed(1);
  $("tTemp").textContent= S.temp===null? "—" : S.temp.toFixed(0)+" °C";
  $("tTemp").parentElement.style.color = S.thermal? "var(--amber)" : "";
  $("tImu").textContent = S.imu.ok? S.imu.hz.toFixed(0)+" Hz" : "no disp.";
  $("dImu").className   = "dot"+(S.imu.ok? (S.imu.valid?" on":" warn") : "");
}

// ===================================================================
//  Horizon Lock
// ===================================================================
function setLock(on,quiet){
  lockOn=on;
  $("hl").setAttribute("aria-pressed", on?"true":"false");
  $("bLock").textContent = on? "Desactivar Lock" : "Activar Lock";
  $("bLock").classList.toggle("on", on);
  if(on){ takeRef(); if(!quiet) notice("Horizon Lock activado"); }
  else { haveRef=false; curComp=0; if(!quiet) notice("Horizon Lock desactivado"); }
}
function takeRef(){
  if(S.imu.ok && S.imu.valid){ refAngle=S.imu.hfilt; haveRef=true; curComp=0; }
  else if(!haveRef){ refAngle=S.imu.hfilt||0; haveRef=true; curComp=0; }
}
$("hl").onclick=()=>{
  if(!S.imu.ok && !lockOn){ notice("IMU no disponible",1800); return; }
  setLock(!lockOn);
};
$("bLock").onclick=()=>$("hl").click();
$("caret").onclick=()=>{
  app.classList.toggle("panel");
  try{ localStorage.setItem("fc_panel", app.classList.contains("panel")?"1":"0"); }catch(e){}
};
$("bRecenter").onclick=()=>{
  if(!S.imu.ok){ notice("IMU no disponible",1600); return; }
  if(!S.imu.valid){ notice("Sin referencia: apunta menos vertical",2000); return; }
  takeRef(); notice("Horizonte recentrado");
};
$("bGrid").onclick=()=>{ gridOn=!gridOn; $("grid").classList.toggle("on",gridOn);
  $("bGrid").classList.toggle("on",gridOn); };

$("selRot").onchange=e=>{
  if(recording){ e.target.value=String(manualRot); notice("No se puede rotar durante la grabación",2200); return; }
  manualRot=parseInt(e.target.value,10)||0;
  try{ localStorage.setItem("fc_rot",String(manualRot)); }catch(e){}
  notice(manualRot===360? "Rotación 360° (igual que 0°)" : "Rotación "+manualRot+"°");
};
$("selCrop").onchange=e=>{
  cropMode=e.target.value;
  try{ localStorage.setItem("fc_crop",cropMode); }catch(e){}
  notice({dyn:"Encuadre dinámico",fix:"Encuadre estable 360°",wide:"Encuadre amplio"}[cropMode]);
};
async function pushImuCfg(){
  const q="mount="+$("selMount").value+"&invert="+$("selInv").value+"&plane="+$("selPlane").value;
  try{ applyState(await api("/api/imucfg?"+q,{method:"POST"})); notice("Montaje del IMU actualizado"); }
  catch(e){ notice("No se pudo cambiar el montaje",2000); }
}
$("selMount").onchange=pushImuCfg;
$("selInv").onchange=pushImuCfg;
$("selPlane").onchange=pushImuCfg;
$("bFlip").onclick=async()=>{
  const on=!$("bFlip").classList.contains("on");
  $("bFlip").classList.toggle("on",on);
  try{ await api("/api/flip?v="+(on?1:0),{method:"POST"}); }catch(e){}
};

// ===================================================================
//  API y modos
// ===================================================================
async function api(path,opts){
  const r=await fetch(path,Object.assign({cache:"no-store"},opts||{}));
  if(!r.ok) throw new Error("HTTP "+r.status);
  return r.json();
}
function applyState(s){
  if(s.modes) S.modes=s.modes;
  if(s.mode!=null) S.mode=s.mode;
  if(s.w) S.srcW=s.w; if(s.h) S.srcH=s.h;
  if(s.camReady!=null) S.camReady=s.camReady;
  if(s.flip!=null) $("bFlip").classList.toggle("on", !!s.flip);
  if(s.imu){
    $("selMount").value=String(s.imu.mount);
    $("selInv").value  = s.imu.invert?"1":"0";
    $("selPlane").value=String(s.imu.plane);
  }
  buildModes(); paintTop();
}
function buildModes(){
  const box=$("modes");
  if(box.childElementCount!==S.modes.length){
    box.innerHTML="";
    S.modes.forEach(m=>{
      const b=document.createElement("button");
      b.className="mode"; b.textContent=m.label; b.dataset.id=m.id;
      b.setAttribute("aria-pressed","false");
      b.onclick=()=>setMode(m.id);
      box.appendChild(b);
    });
  }
  [...box.children].forEach(b=>{
    const on=S.modes[S.mode] && b.dataset.id===S.modes[S.mode].id;
    if(on && b.getAttribute("aria-pressed")!=="true")
      try{ b.scrollIntoView({block:"nearest",inline:"center",behavior:"smooth"}); }catch(e){}
    b.setAttribute("aria-pressed",on?"true":"false");
    b.disabled=busy||recording;
  });
}
async function setMode(id){
  if(busy||recording) return;
  busy=true; buildModes(); $("shutter").disabled=true;
  Stream.stop(); notice("Cambiando de modo…",4000);
  try{
    const s=await api("/api/mode?m="+encodeURIComponent(id),{method:"POST"});
    notice(s.error? s.error : "Modo: "+s.label, s.error?2600:1200);
    applyState(s);
  }catch(e){ notice("No se pudo cambiar de modo",2200); }
  busy=false; $("shutter").disabled=false; buildModes(); Stream.start();
}

// ===================================================================
//  Foto: la "estabilizada" se re-renderiza de verdad en un canvas
//  aparte, con la misma rotación, zoom y recorte que el visor.
// ===================================================================
let lastShot=null;   // {origUrl, stabUrl, videoUrl, ...}
// Libera un lote de blobs. Se llama con el lote ANTERIOR una vez el nuevo ya
// está puesto, para no revocar una URL que todavía se está mostrando.
function revokeShot(sh){
  const t = (sh===undefined)? lastShot : sh;
  if(!t) return;
  if(t.origUrl)  URL.revokeObjectURL(t.origUrl);
  if(t.stabUrl)  URL.revokeObjectURL(t.stabUrl);
  if(t.videoUrl) URL.revokeObjectURL(t.videoUrl);
  if(sh===undefined) lastShot=null;
}
async function capturePhoto(){
  if(busy||recording) return;
  busy=true; $("shutter").disabled=true; buildModes();
  const m=S.modes[S.mode];
  Stream.stop();
  notice("Capturando"+(m?" a "+m.captureLabel:"")+"…",8000);
  try{
    const r=await fetch("/api/photo",{cache:"no-store"});
    if(!r.ok) throw new Error("HTTP "+r.status);
    const blob=await r.blob();
    if(blob.size<512) throw new Error("imagen vacía");

    // Ángulo del instante EXACTO del disparo, medido en el ESP32.
    const hAtShot=parseFloat(r.headers.get("X-Horizon")||"NaN");
    const hValid =(r.headers.get("X-Hvalid")||"0")==="1";

    const bmp=await createImageBitmap(blob);
    const ow=bmp.width, oh=bmp.height;
    const origUrl=URL.createObjectURL(blob);

    let stabUrl=null, sw=0, sh=0, zoom=1, stabSize=0, ang=0;
    if(lockOn && haveRef){
      const compAtShot = (hValid && !isNaN(hAtShot)) ? -(hAtShot-refAngle) : curComp;
      const base=((manualRot%360)+360)%360;
      ang=base+compAtShot;
      const q=((manualRot%360)+360)%360;
      sw=(q===90||q===270)?oh:ow; sh=(q===90||q===270)?ow:oh;
      const dyn=coverScale(sw,sh,ow,oh,ang);
      zoom = cropMode==="fix" ? Math.max(cover360(sw,sh,ow,oh),dyn)
           : cropMode==="wide"? Math.min(dyn,Math.max(coverScale(sw,sh,ow,oh,base),WIDE_ZOOM_MAX))
           : dyn;
      const c=document.createElement("canvas"); c.width=sw; c.height=sh;
      const cx=c.getContext("2d");
      cx.fillStyle="#000"; cx.fillRect(0,0,sw,sh);
      cx.translate(sw/2,sh/2); cx.rotate(ang*Math.PI/180); cx.scale(zoom,zoom);
      cx.imageSmoothingQuality="high";
      cx.drawImage(bmp,-ow/2,-oh/2,ow,oh);
      const sblob=await new Promise(res=>c.toBlob(res,"image/jpeg",0.92));
      if(sblob){ stabUrl=URL.createObjectURL(sblob); stabSize=sblob.size; }
    }
    bmp.close&&bmp.close();

    const prev=lastShot;
    lastShot={ origUrl, stabUrl, ow, oh, sw, sh, zoom, ang,
               origSize:blob.size, stabSize };
    const g=$("gal"); g.innerHTML="";
    const th=new Image(); th.src=stabUrl||origUrl; g.appendChild(th);
    revokeShot(prev);
    $("flash").classList.remove("go"); void $("flash").offsetWidth; $("flash").classList.add("go");

    $("vShot").textContent =
      "Original "+ow+"×"+oh+" ("+Math.round(blob.size/1024)+" KB)" +
      (stabUrl? " · Estabilizada "+sw+"×"+sh+" ("+Math.round(stabSize/1024)+" KB), "+
                "recorte "+zoom.toFixed(3)+"×, giro "+ang.toFixed(1)+"°"
              : " · sin estabilizar (Horizon Lock apagado)");
    notice(stabUrl? "Foto lista · toca la miniatura para descargar"
                  : "Foto original lista · toca la miniatura",2600);
  }catch(e){ notice("Fallo al capturar: "+e.message,2800); }
  busy=false; $("shutter").disabled=false; buildModes(); Stream.start();
}
function download(url,name){
  const a=document.createElement("a");
  a.href=url; a.download=name; document.body.appendChild(a); a.click(); a.remove();
}
$("gal").onclick=()=>{
  if(!lastShot){ notice("Todavía no hay captura",1400); return; }
  if(lastShot.videoUrl){ download(lastShot.videoUrl,lastShot.videoName); notice("Descargando vídeo"); return; }
  const stamp=Date.now();
  if(lastShot.stabUrl){
    download(lastShot.stabUrl,"flexcam_"+stamp+"_estabilizada.jpg");
    setTimeout(()=>download(lastShot.origUrl,"flexcam_"+stamp+"_original.jpg"),700);
    notice("Descargando estabilizada + original",2400);
  }else{
    download(lastShot.origUrl,"flexcam_"+stamp+".jpg");
    notice("Descargando foto",1600);
  }
};

// ===================================================================
//  Vídeo: se graba el CANVAS ya corregido, nunca el MJPEG bruto.
// ===================================================================
let rec=null, recStream=null, recChunks=[], recT0=0, recTimer=null, recBytes=0, recMime="";
function pickMime(){
  const cand=["video/mp4;codecs=avc1.42E01E","video/mp4",
              "video/webm;codecs=vp9","video/webm;codecs=vp8","video/webm"];
  if(typeof MediaRecorder==="undefined") return null;
  for(const c of cand) if(MediaRecorder.isTypeSupported(c)) return c;
  return "";
}
function startRec(){
  if(recording) return;
  const mime=pickMime();
  if(mime===null){ notice("Este navegador no puede grabar vídeo",2600); return; }
  const fps=parseInt($("selVq").value,10)||24;
  try{
    canvasLocked=true;                       // el lienzo no puede cambiar de tamaño
    recStream=view.captureStream(fps);
    const px=view.width*view.height;
    const bps=Math.min(8_000_000, Math.max(800_000, Math.round(px*fps*REC_BITRATE_PER_PX/10)));
    rec=new MediaRecorder(recStream, mime? {mimeType:mime, videoBitsPerSecond:bps}
                                         : {videoBitsPerSecond:bps});
  }catch(e){ canvasLocked=false; notice("No se pudo iniciar la grabación: "+e.message,2800); return; }
  recMime=rec.mimeType||mime||"video/webm";
  recChunks=[]; recBytes=0; recT0=Date.now();
  rec.ondataavailable=e=>{ if(e.data&&e.data.size){ recChunks.push(e.data); recBytes+=e.data.size; } };
  rec.onstop=finishRec;
  rec.onerror=()=>{ notice("Error de grabación",2400); stopRec(); };
  rec.start(1000);
  recording=true;
  $("shutter").classList.add("recording");
  $("recbadge").classList.add("on");
  $("selRot").disabled=true; buildModes();
  recTimer=setInterval(()=>{
    const s=Math.floor((Date.now()-recT0)/1000);
    $("rectime").textContent=Math.floor(s/60)+":"+String(s%60).padStart(2,"0");
    $("recsize").textContent=" · "+(recBytes/1048576).toFixed(1)+" MB";
  },250);
  const fmtTxt = recMime.includes("mp4")? "MP4" : "WebM";
  notice("Grabando "+view.width+"×"+view.height+" a "+fps+" fps · "+fmtTxt+
         (fmtTxt==="WebM"? " (este navegador no ofrece MP4)":""), 3200);
}
function stopRec(){
  if(!recording) return;
  try{ rec && rec.state!=="inactive" && rec.stop(); }catch(e){ finishRec(); }
}
function finishRec(){
  clearInterval(recTimer); recTimer=null;
  recording=false;
  $("shutter").classList.remove("recording");
  $("recbadge").classList.remove("on");
  $("selRot").disabled=false; canvasLocked=false; buildModes();
  if(recStream){ recStream.getTracks().forEach(t=>{ try{t.stop();}catch(e){} }); recStream=null; }
  rec=null;
  if(!recChunks.length){ notice("La grabación quedó vacía",2200); return; }
  const ext=recMime.includes("mp4")?"mp4":"webm";
  const blob=new Blob(recChunks,{type:recMime.split(";")[0]||("video/"+ext)});
  recChunks=[];
  const prev=lastShot;
  const url=URL.createObjectURL(blob);
  const name="flexcam_"+Date.now()+(lockOn?"_estabilizado":"")+"."+ext;
  lastShot={ videoUrl:url, videoName:name };
  const g=$("gal"); g.innerHTML=""; g.textContent=ext.toUpperCase();
  revokeShot(prev);
  download(url,name);
  $("vShot").textContent="Vídeo "+view.width+"×"+view.height+" · "+
    (blob.size/1048576).toFixed(2)+" MB · "+ext.toUpperCase()+
    (lockOn?" · estabilizado":" · sin estabilizar");
  notice("Vídeo guardado en Descargas ("+(blob.size/1048576).toFixed(1)+" MB)",3000);
}

// ---------- pestañas y obturador ----------
function setTab(t){
  tab=t;
  $("tabPhoto").setAttribute("aria-selected", t==="photo"?"true":"false");
  $("tabVideo").setAttribute("aria-selected", t==="video"?"true":"false");
  $("shutter").classList.toggle("vid", t==="video");
  $("shutter").title = t==="video"? "Grabar" : "Capturar";
}
$("tabPhoto").onclick=()=>{ if(recording){ notice("Detén la grabación primero",1800); return; } setTab("photo"); };
$("tabVideo").onclick=()=>{ setTab("video"); };
$("shutter").onclick=()=>{
  if(tab==="video"){ recording? stopRec() : startRec(); }
  else capturePhoto();
};

// ===================================================================
//  WebSocket de telemetría: una sola conexión, siempre la última muestra.
// ===================================================================
let ws=null, wsTimer=null, wsFail=0;
function wsConnect(){
  if(ws && (ws.readyState===0||ws.readyState===1)) return;   // nunca duplicar
  try{ ws=new WebSocket("ws://"+location.host+"/ws"); }
  catch(e){ scheduleWs(); return; }
  ws.onopen=()=>{ wsFail=0; $("dNet").className="dot on"; $("tNet").textContent="Conectado"; };
  ws.onclose=()=>{ $("dNet").className="dot"; $("tNet").textContent="Reconectando"; ws=null; scheduleWs(); };
  ws.onerror=()=>{ try{ws.close();}catch(e){} };
  ws.onmessage=ev=>{
    let d; try{ d=JSON.parse(ev.data); }catch(e){ return; }
    if(d.t==="i"){
      const i=S.imu;
      i.seq=d.sq; i.ts=d.ts; i.roll=d.r; i.pitch=d.p; i.yaw=d.y;
      i.hraw=d.hr; i.hcont=d.hc; i.hfilt=d.hf; i.conf=d.cf; i.valid=d.hv===1;
      i.q=[d.qi,d.qj,d.qk,d.qr]; i.g=[d.gx,d.gy,d.gz];
      i.hz=d.hz; i.acc=d.a; i.ok=d.ok===1; i.state=d.s; i.resets=d.rs;
      i.mount=d.mo; i.invert=d.iv===1; i.plane=d.pl;
      if(d.ep!==i.epoch){
        i.epoch=d.ep;
        if(lastEpochSeen>=0 && lockOn){ takeRef(); notice("Montaje del IMU cambiado: referencia retomada",2200); }
        lastEpochSeen=d.ep;
        $("selMount").value=String(d.mo);
        $("selInv").value = d.iv? "1":"0";
        $("selPlane").value=String(d.pl);
      }
      paintTop();
    }else if(d.t==="s"){
      // Nombres de la capa de camara con pool latest-frame:
      //   fps   = fotogramas capturados y publicados por el productor
      //   sfps  = fotogramas realmente entregados por red
      //   pdrop = descartados porque el cliente iba atrasado
      S.capFpsEsp=d.fps; S.sendFps=d.sfps; S.targetFps=d.tfps;
      S.srcW=d.w; S.srcH=d.h; S.heap=d.heap; S.largest=d.largest; S.psram=d.ps;
      S.drop=d.drop; S.poolDrop=d.pdrop; S.capMs=d.capms; S.sendMs=d.sendms;
      S.ageEsp=d.age; S.thermal=d.thermal;
      S.temp=(d.temp>-100)? d.temp : null;
      S.camReady=d.cam===1;
      if(d.mode!=null && d.mode!==S.mode && !busy){ S.mode=d.mode; buildModes(); }
      paintTop();
    }
  };
}
function scheduleWs(){
  clearTimeout(wsTimer);
  wsFail=Math.min(wsFail+1,6);
  wsTimer=setTimeout(wsConnect,300*wsFail);
}

// ===================================================================
//  Arranque y limpieza
// ===================================================================
(function init(){
  try{
    const r=localStorage.getItem("fc_rot");  if(r!==null) manualRot=parseInt(r,10)||0;
    const c=localStorage.getItem("fc_crop"); if(c) cropMode=c;
    if(localStorage.getItem("fc_panel")==="1") app.classList.add("panel");
  }catch(e){}
  $("selRot").value=String(manualRot);
  $("selCrop").value=cropMode;
  setTab("photo");
  setLock(false,true);              // nunca se activa solo
  requestAnimationFrame(draw);
  api("/api/state").then(applyState).catch(()=>notice("Sin respuesta del ESP32",3000));
  wsConnect();
  Stream.start();
})();

// Ventana de diagnóstico: abre la consola del navegador y escribe FC para
// ver el estado real (útil para depurar en el móvil sin cables).
window.FC = {
  get state(){ return S; },
  get stream(){ return Stream; },
  get lock(){ return lockOn; },
  get ref(){ return haveRef? refAngle : null; },
  get comp(){ return curComp; },
  get zoom(){ return curZoom; },
  get total(){ return curTotal; },
  get manualRot(){ return manualRot; },
  get crop(){ return cropMode; },
  get shot(){ return lastShot; },
  get recording(){ return recording; },
  get recorder(){ return { rec, stream:recStream, timer:recTimer }; },
  restartStream(){ Stream.stop(); Stream.start(); }
};

addEventListener("pagehide",()=>{
  try{ stopRec(); }catch(e){}
  Stream.stop();
  if(ws){ try{ws.close();}catch(e){} ws=null; }
  clearTimeout(wsTimer); clearInterval(recTimer);
  revokeShot();
});
document.addEventListener("visibilitychange",()=>{
  // Al volver a la pestaña, si el stream murió se levanta solo. Nunca se
  // abre un segundo stream: Stream.start() cierra el anterior primero.
  if(!document.hidden && !Stream.running && !busy) Stream.start();
});
</script>
</body>
</html>
)HTMLDOC";

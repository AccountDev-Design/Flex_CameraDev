// =====================================================================
//  fc_web_ui.h — interfaz web (se sirve desde flash, no usa Internet).
//  Toda la rotación e inclinación de la imagen ocurre AQUÍ, en el
//  navegador. El ESP32 nunca toca los píxeles.
// =====================================================================
#pragma once
#include <Arduino.h>

static const char FC_INDEX_HTML[] PROGMEM = R"HTMLDOC(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover,user-scalable=no">
<meta name="theme-color" content="#07080b">
<title>FlexCam S26</title>
<style>
:root{
  --bg:#07080b; --glass:rgba(20,22,28,.62); --glass2:rgba(32,35,44,.72);
  --line:rgba(255,255,255,.10); --txt:#eef1f6; --dim:#98a0ae;
  --accent:#ff8a3d; --accent2:#4dd6ff; --ok:#4ade80; --bad:#ff5d5d;
  --r:16px; --pad:12px;
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{height:100%;margin:0;background:var(--bg);color:var(--txt);
  font:14px/1.35 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
  overscroll-behavior:none}
button,select{font:inherit;color:inherit}

#app{height:100dvh;display:grid;grid-template-rows:auto 1fr auto;gap:8px;padding:8px;
  max-width:100vw;overflow-x:hidden;
  padding-top:max(8px,env(safe-area-inset-top));
  padding-bottom:max(8px,env(safe-area-inset-bottom))}

/* ---------- barra superior ---------- */
#topbar{display:flex;gap:6px;align-items:center;flex-wrap:wrap;min-width:0;
  background:var(--glass);border:1px solid var(--line);border-radius:var(--r);
  padding:8px 10px;backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px)}
.chip{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;border-radius:999px;
  background:rgba(255,255,255,.06);border:1px solid var(--line);font-size:12px;white-space:nowrap}
.chip b{font-weight:600;font-variant-numeric:tabular-nums}
.dot{width:8px;height:8px;border-radius:50%;background:var(--bad);flex:none}
.dot.on{background:var(--ok);box-shadow:0 0 8px var(--ok)}
.dot.warn{background:var(--accent)}
#brand{font-weight:700;letter-spacing:.4px;margin-right:2px}
#brand span{color:var(--accent)}

/* ---------- visor ---------- */
#stage{position:relative;min-height:0;border-radius:var(--r);overflow:hidden;
  background:#000;border:1px solid var(--line)}
#viewport{position:absolute;inset:0;overflow:hidden}
#stream{position:absolute;left:50%;top:50%;transform-origin:50% 50%;
  will-change:transform;transition:none;display:block;image-rendering:auto}
#hud{position:absolute;inset:0;pointer-events:none}
#hlevel{position:absolute;left:50%;top:50%;width:44%;height:2px;margin-left:-22%;
  background:rgba(255,255,255,.55);border-radius:2px;transform-origin:50% 50%;
  display:none;box-shadow:0 0 6px rgba(0,0,0,.6)}
#hlevel.on{display:block}
#hlevel.locked{background:var(--accent);box-shadow:0 0 10px var(--accent)}
#grid{position:absolute;inset:0;display:none;
  background-image:linear-gradient(rgba(255,255,255,.16) 1px,transparent 1px),
                   linear-gradient(90deg,rgba(255,255,255,.16) 1px,transparent 1px);
  background-size:33.33% 33.33%}
#grid.on{display:block}
#notice{position:absolute;left:50%;top:14px;transform:translateX(-50%);
  background:rgba(0,0,0,.72);border:1px solid var(--line);border-radius:999px;
  padding:6px 14px;font-size:12.5px;opacity:0;transition:opacity .18s;pointer-events:none}
#notice.show{opacity:1}
#limitbadge{position:absolute;left:50%;bottom:12px;transform:translateX(-50%);
  background:rgba(255,138,61,.16);border:1px solid var(--accent);color:var(--accent);
  border-radius:999px;padding:5px 12px;font-size:12px;display:none}
#limitbadge.on{display:block}
#flash{position:absolute;inset:0;background:#fff;opacity:0;pointer-events:none}
#flash.go{animation:fl .28s ease-out}
@keyframes fl{0%{opacity:.85}100%{opacity:0}}
#shot{position:absolute;right:10px;bottom:10px;width:74px;border-radius:10px;
  border:2px solid rgba(255,255,255,.75);display:none;box-shadow:0 4px 18px rgba(0,0,0,.6)}
#shot.on{display:block}

/* ---------- panel IMU ---------- */
#imupanel{min-width:0;background:var(--glass);border:1px solid var(--line);border-radius:var(--r);
  backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);overflow:hidden}
#imuhead{display:flex;align-items:center;gap:8px;padding:10px 12px;cursor:pointer;
  user-select:none;background:none;border:0;width:100%;text-align:left}
#imuhead .caret{margin-left:auto;transition:transform .2s}
#imupanel.open #imuhead .caret{transform:rotate(180deg)}
#imubody{display:none;padding:0 12px 12px;
  grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px}
#imupanel.open #imubody{display:grid}
.kv{background:rgba(255,255,255,.05);border:1px solid var(--line);border-radius:10px;padding:8px 10px}
.kv .k{font-size:11px;color:var(--dim);text-transform:uppercase;letter-spacing:.5px}
.kv .v{font-size:17px;font-weight:600;font-variant-numeric:tabular-nums;margin-top:2px}
.kv .v.small{font-size:14px;font-weight:500}
.bar{height:4px;border-radius:2px;background:rgba(255,255,255,.10);margin-top:6px;overflow:hidden}
.bar i{display:block;height:100%;background:var(--accent2);width:0;transition:width .12s linear}

/* ---------- controles ---------- */
#controls{min-width:0;background:var(--glass);border:1px solid var(--line);border-radius:var(--r);
  padding:10px;backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);
  display:grid;gap:10px}
#modes{display:flex;gap:6px;overflow-x:auto;scrollbar-width:none;padding-bottom:2px;
  min-width:0;-webkit-overflow-scrolling:touch}
#modes::-webkit-scrollbar{display:none}
.mode{flex:none;padding:8px 14px;border-radius:999px;background:rgba(255,255,255,.06);
  border:1px solid var(--line);white-space:nowrap;font-size:13px;cursor:pointer}
.mode[aria-pressed="true"]{background:rgba(255,138,61,.18);border-color:var(--accent);color:#ffd9bd}
.mode:disabled{opacity:.45;cursor:not-allowed}
#row{display:flex;align-items:center;gap:10px;justify-content:space-between;
  min-width:0;flex-wrap:wrap}
#leftctl,#rightctl{display:flex;align-items:center;gap:8px;min-width:0}
.icon{width:44px;height:44px;border-radius:50%;background:rgba(255,255,255,.07);
  border:1px solid var(--line);display:grid;place-items:center;cursor:pointer;flex:none}
.icon svg{width:21px;height:21px;fill:none;stroke:currentColor;stroke-width:1.9;
  stroke-linecap:round;stroke-linejoin:round}
.icon[aria-pressed="true"]{background:rgba(255,138,61,.20);border-color:var(--accent);
  color:var(--accent);box-shadow:0 0 14px rgba(255,138,61,.35)}
.icon:disabled{opacity:.4;cursor:not-allowed}
#shutter{width:70px;height:70px;border-radius:50%;border:3px solid rgba(255,255,255,.92);
  background:radial-gradient(circle at 50% 40%,#fff,#e6e8ee);cursor:pointer;flex:none;
  box-shadow:0 6px 22px rgba(0,0,0,.5);transition:transform .08s}
#shutter:active{transform:scale(.93)}
#shutter:disabled{opacity:.5}
#shutter.busy{background:radial-gradient(circle at 50% 40%,#ffd9bd,var(--accent))}
select{background:rgba(255,255,255,.07);border:1px solid var(--line);border-radius:10px;
  padding:9px 8px;font-size:13px;cursor:pointer}
label.f{display:flex;flex-direction:column;gap:3px;font-size:11px;color:var(--dim);
  text-transform:uppercase;letter-spacing:.5px}

/* ---------- tablet y horizontal ---------- */
@media(min-width:820px) and (orientation:landscape){
  #app{grid-template-columns:1fr 320px;grid-template-rows:auto 1fr;
       grid-template-areas:"top side" "stage side"}
  #topbar{grid-area:top} #stage{grid-area:stage}
  #side{grid-area:side;display:grid;grid-template-rows:auto 1fr;gap:8px;min-height:0;min-width:0}
  #imupanel{order:2} #controls{order:1}
  #imupanel.open #imubody{grid-template-columns:1fr 1fr}
}
@media(max-width:819px),(orientation:portrait){
  #side{display:contents}
}
@media(orientation:landscape) and (max-height:520px){
  #shutter{width:56px;height:56px} .icon{width:38px;height:38px}
  .mode{padding:6px 11px;font-size:12px}
}
</style>
</head>
<body>
<div id="app">

  <header id="topbar">
    <span id="brand">Flex<span>Cam</span></span>
    <span class="chip"><i class="dot" id="dNet"></i><b id="tNet">Conectando</b></span>
    <span class="chip">Modo <b id="tMode">—</b></span>
    <span class="chip">Res <b id="tRes">—</b></span>
    <span class="chip">FPS <b id="tFps">0.0</b></span>
    <span class="chip"><i class="dot" id="dImu"></i>IMU <b id="tImu">—</b></span>
  </header>

  <main id="stage">
    <div id="viewport"><img id="stream" alt="Visor"></div>
    <div id="hud">
      <div id="grid"></div>
      <div id="hlevel"></div>
      <div id="limitbadge">Límite de Horizon Lock</div>
    </div>
    <div id="notice"></div>
    <div id="flash"></div>
    <img id="shot" alt="Última foto">
  </main>

  <div id="side">
    <section id="controls">
      <div id="modes"></div>
      <div id="row">
        <div id="leftctl">
          <button class="icon" id="bHorizon" aria-pressed="false" title="Horizon Lock">
            <svg viewBox="0 0 24 24"><path d="M2 12h20"/><path d="M12 4a8 8 0 100 16 8 8 0 000-16z"/></svg>
          </button>
          <button class="icon" id="bRecenter" title="Recentrar horizonte">
            <svg viewBox="0 0 24 24"><path d="M12 3v4M12 17v4M3 12h4M17 12h4"/><circle cx="12" cy="12" r="3.2"/></svg>
          </button>
          <button class="icon" id="bGrid" aria-pressed="false" title="Cuadrícula">
            <svg viewBox="0 0 24 24"><path d="M3 9h18M3 15h18M9 3v18M15 3v18"/></svg>
          </button>
        </div>
        <button id="shutter" title="Capturar"></button>
        <div id="rightctl">
          <label class="f">Rotar vista
            <select id="selRot">
              <option value="0">0°</option><option value="90">90°</option>
              <option value="180">180°</option><option value="270">270°</option>
              <option value="360">360°</option>
            </select>
          </label>
          <button class="icon" id="bFlip" aria-pressed="false" title="Voltear sensor">
            <svg viewBox="0 0 24 24"><path d="M12 3v18"/><path d="M7 8L4 12l3 4"/><path d="M17 8l3 4-3 4"/></svg>
          </button>
        </div>
      </div>
    </section>

    <section id="imupanel">
      <button id="imuhead">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor"
             stroke-width="1.9" stroke-linecap="round"><path d="M3 12h4l2-6 4 12 2-6h6"/></svg>
        <span>IMU en tiempo real</span>
        <svg class="caret" width="16" height="16" viewBox="0 0 24 24" fill="none"
             stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M6 9l6 6 6-6"/></svg>
      </button>
      <div id="imubody">
        <div class="kv"><div class="k">Roll</div><div class="v" id="vRoll">+0.00°</div></div>
        <div class="kv"><div class="k">Pitch</div><div class="v" id="vPitch">+0.00°</div></div>
        <div class="kv"><div class="k">Yaw</div><div class="v" id="vYaw">+0.00°</div></div>
        <div class="kv"><div class="k">Compensación</div><div class="v" id="vComp">+0.00°</div></div>
        <div class="kv"><div class="k">Zoom horizonte</div><div class="v" id="vZoom">1.00×</div></div>
        <div class="kv"><div class="k">Rotación total</div><div class="v" id="vTotal">+0.00°</div></div>
        <div class="kv"><div class="k">IMU real</div><div class="v" id="vHz">0 Hz</div></div>
        <div class="kv"><div class="k">FPS cámara</div><div class="v" id="vFps">0.0</div></div>
        <div class="kv"><div class="k">Estado IMU</div><div class="v small" id="vState">—</div></div>
        <div class="kv"><div class="k">Calibración</div><div class="v small" id="vAcc">—</div>
          <div class="bar"><i id="barAcc"></i></div></div>
        <div class="kv"><div class="k">Límite útil</div><div class="v small" id="vLimit">±0.0°</div></div>
        <div class="kv"><div class="k">Memoria libre</div><div class="v small" id="vMem">—</div></div>
      </div>
    </section>
  </div>
</div>

<script>
"use strict";
// ===================================================================
//  Ajustes de la compensación visual. Todo esto ocurre en el navegador.
// ===================================================================
const HORIZON_LIMIT_DEG = 15.0;  // tope duro pedido para la compensación
const ZOOM_MIN          = 1.08;  // recorte mínimo con Horizon Lock activo
const ZOOM_MAX          = 1.20;  // recorte máximo permitido
// El ángulo realmente compensable sale de ZOOM_MAX y de la geometría del
// visor: con 4:3 dentro de un visor 4:3, un zoom de 1.20x sólo tapa las
// esquinas hasta unos 9°. Sube ZOOM_MAX (p.ej. 1.35) si quieres los 15°
// completos, a cambio de recortar más encuadre.

const $ = id => document.getElementById(id);
const stream=$("stream"), stage=$("stage"), viewport=$("viewport");

let state = {
  modes:[], mode:null, w:800, h:600, fps:0, camReady:false,
  roll:0, pitch:0, yaw:0, hz:0, acc:0, imuState:"", imuOk:false,
  heap:0, psram:0, clients:0
};
let horizonOn=false, refRoll=0, manualRot=0, gridOn=false, flipOn=false;
let busy=false, limitHit=false, curZoom=1, curComp=0, effLimit=HORIZON_LIMIT_DEG;
let lastPanel=0;

const norm180 = a => { a=(a+180)%360; if(a<0)a+=360; return a-180; };
const fmt = v => (v>=0?"+":"−")+Math.abs(v).toFixed(2)+"°";

// ---- geometría: escala mínima para que la imagen girada cubra el visor ----
function coverScale(W,H,w,h,deg){
  const t=deg*Math.PI/180, c=Math.abs(Math.cos(t)), s=Math.abs(Math.sin(t));
  return Math.max((W*c+H*s)/w,(W*s+H*c)/h);
}
// Mayor ángulo de compensación cuyo zoom no pasa de ZOOM_MAX.
function maxCompensable(W,H,w,h,base){
  const k0=coverScale(W,H,w,h,base);
  if(coverScale(W,H,w,h,base+HORIZON_LIMIT_DEG)/k0<=ZOOM_MAX &&
     coverScale(W,H,w,h,base-HORIZON_LIMIT_DEG)/k0<=ZOOM_MAX) return HORIZON_LIMIT_DEG;
  let lo=0, hi=HORIZON_LIMIT_DEG;
  for(let i=0;i<24;i++){
    const m=(lo+hi)/2;
    const r=Math.max(coverScale(W,H,w,h,base+m),coverScale(W,H,w,h,base-m))/k0;
    if(r<=ZOOM_MAX) lo=m; else hi=m;
  }
  return lo;
}

// Tamaño de maquetación del <img>: ajuste "contain" para no deformar nunca.
let layoutW=0, layoutH=0, baseK=1;
function relayout(){
  const W=viewport.clientWidth, H=viewport.clientHeight;
  if(!W||!H) return;
  const ar=(state.w||4)/(state.h||3);
  let w=W, h=W/ar;
  if(h>H){ h=H; w=H*ar; }
  layoutW=w; layoutH=h;
  stream.style.width=w+"px"; stream.style.height=h+"px";
  baseK=coverScale(W,H,w,h,manualRot%360);
  effLimit=maxCompensable(W,H,w,h,manualRot%360);
  $("vLimit").textContent="±"+effLimit.toFixed(1)+"°";
  render();
}

// ---- bucle de pintado: desacoplado del WebSocket, siempre al ritmo de la
//      pantalla y sin transiciones CSS, para que no se note retraso. ----
function render(){
  const base=manualRot%360;                 // 360° se comporta igual que 0°
  let comp=0, zoomRatio=1;
  if(horizonOn && state.imuOk){
    const rel=norm180(state.roll-refRoll);
    const want=-rel;                        // giro contrario al de la cámara
    comp=Math.max(-effLimit,Math.min(effLimit,want));
    limitHit=Math.abs(want)>effLimit+0.01;
    const W=viewport.clientWidth,H=viewport.clientHeight;
    const kFull=coverScale(W,H,layoutW,layoutH,base+comp);
    zoomRatio=Math.min(ZOOM_MAX,Math.max(ZOOM_MIN,kFull/baseK));
  }else{
    limitHit=false;
  }
  curComp=comp; curZoom=zoomRatio;
  const total=base+comp;
  stream.style.transform=
    "translate(-50%,-50%) rotate("+total.toFixed(3)+"deg) scale("+(baseK*zoomRatio).toFixed(4)+")";
  $("limitbadge").classList.toggle("on",limitHit);
  const now=performance.now();
  if(now-lastPanel>80){ lastPanel=now; paintDerived(); }
  const lv=$("hlevel");
  lv.classList.toggle("on",horizonOn||state.imuOk);
  lv.classList.toggle("locked",horizonOn);
  lv.style.transform="rotate("+(horizonOn?0:norm180(state.roll-refRoll)).toFixed(2)+"deg)";
  requestAnimationFrame(render);
}

function paintDerived(){
  $("vComp").textContent=fmt(curComp);
  $("vZoom").textContent=curZoom.toFixed(3)+"×";
  $("vTotal").textContent=fmt(norm180(manualRot%360+curComp));
}
function paintImu(){
  $("vRoll").textContent=fmt(state.roll);
  $("vPitch").textContent=fmt(state.pitch);
  $("vYaw").textContent=fmt(state.yaw);
  $("vHz").textContent=state.hz.toFixed(0)+" Hz";
  $("vState").textContent=state.imuState||"—";
  $("vAcc").textContent=["Sin calibrar","Baja","Media","Alta"][state.acc]||"—";
  $("barAcc").style.width=(state.acc/3*100)+"%";
  $("tImu").textContent=state.imuOk?state.hz.toFixed(0)+" Hz":"no disp.";
  const d=$("dImu"); d.className="dot"+(state.imuOk?" on":(state.imuState==="Conectando"?" warn":""));
}
function paintCam(){
  $("tFps").textContent=state.fps.toFixed(1);
  $("vFps").textContent=state.fps.toFixed(1);
  $("tRes").textContent=state.w+"×"+state.h;
  $("vMem").textContent=Math.round(state.heap/1024)+" KB / "+Math.round(state.psram/1024)+" KB PSRAM";
}

function notice(txt,ms){
  const n=$("notice"); n.textContent=txt; n.classList.add("show");
  clearTimeout(notice._t); notice._t=setTimeout(()=>n.classList.remove("show"),ms||1600);
}

// ---- stream MJPEG ----
function streamUrl(){
  return "http://"+location.hostname+":81/stream?g="+Date.now();
}
function startStream(){
  stream.onload=()=>{};
  stream.onerror=()=>{ setTimeout(()=>{ if(!busy) stream.src=streamUrl(); },900); };
  stream.src=streamUrl();
}
function stopStream(){ stream.removeAttribute("src"); }

// ---- API ----
async function api(path,opts){
  const r=await fetch(path,Object.assign({cache:"no-store"},opts||{}));
  if(!r.ok) throw new Error("HTTP "+r.status);
  return r.json();
}
function applyState(s){
  const prev=state.mode;
  if(s.modes) state.modes=s.modes;
  if(s.mode!=null) state.mode=s.mode;
  if(s.w) state.w=s.w; if(s.h) state.h=s.h;
  if(s.camReady!=null) state.camReady=s.camReady;
  if(s.flip!=null){ flipOn=!!s.flip; $("bFlip").setAttribute("aria-pressed",flipOn); }
  buildModes(); autoHorizon(prev); paintCam(); relayout();
}
function buildModes(){
  const box=$("modes");
  if(box.childElementCount!==state.modes.length){
    box.innerHTML="";
    state.modes.forEach(m=>{
      const b=document.createElement("button");
      b.className="mode"; b.textContent=m.label; b.dataset.id=m.id;
      b.setAttribute("aria-pressed","false");
      b.onclick=()=>setMode(m.id);
      box.appendChild(b);
    });
  }
  [...box.children].forEach(b=>{
    const on=state.modes[state.mode] && b.dataset.id===state.modes[state.mode].id;
    if(on && b.getAttribute("aria-pressed")!=="true"){
      try{ b.scrollIntoView({block:"nearest",inline:"center",behavior:"smooth"}); }catch(e){}
    }
    b.setAttribute("aria-pressed",on?"true":"false");
    b.disabled=busy;
  });
  const m=state.modes[state.mode];
  $("tMode").textContent=m?m.label:"—";
}
// Los modos de horizonte encienden la compensación al ENTRAR en ellos. No se
// vuelve a forzar después, para que se pueda apagar a mano sin que reviva.
function autoHorizon(prev){
  if(prev===state.mode) return;
  const m=state.modes[state.mode];
  if(m&&m.horizon){ setHorizon(true,true); refRoll=state.roll; }
  else if(!m||!m.horizon){ setHorizon(false,true); }
}

async function setMode(id){
  if(busy) return;
  busy=true; buildModes(); $("shutter").disabled=true;
  stopStream(); notice("Cambiando de modo…",4000);
  try{
    const s=await api("/api/mode?m="+encodeURIComponent(id),{method:"POST"});
    if(s.error){ notice(s.error,2600); } else { notice("Modo: "+s.label,1200); }
    applyState(s);
  }catch(e){ notice("No se pudo cambiar de modo",2200); }
  busy=false; $("shutter").disabled=false; buildModes(); startStream();
}

async function capture(){
  if(busy) return;
  busy=true; $("shutter").disabled=true; $("shutter").classList.add("busy");
  const m=state.modes[state.mode];
  const big=m&&m.captureLabel&&m.captureLabel!==(state.w+"×"+state.h);
  stopStream();
  notice(big?"Capturando a "+m.captureLabel+"…":"Capturando…",6000);
  try{
    const r=await fetch("/api/photo",{cache:"no-store"});
    if(!r.ok) throw new Error("HTTP "+r.status);
    const blob=await r.blob();
    if(blob.size<512) throw new Error("imagen vacía");
    const url=URL.createObjectURL(blob);
    const img=$("shot");
    if(img.dataset.url) URL.revokeObjectURL(img.dataset.url);
    img.dataset.url=url; img.src=url; img.classList.add("on");
    $("flash").classList.remove("go"); void $("flash").offsetWidth; $("flash").classList.add("go");
    notice("Foto lista · "+Math.round(blob.size/1024)+" KB · toca la miniatura para guardar",2600);
  }catch(e){ notice("Fallo al capturar: "+e.message,2600); }
  busy=false; $("shutter").disabled=false; $("shutter").classList.remove("busy");
  startStream();
}
$("shot").onclick=()=>{
  const u=$("shot").dataset.url; if(!u) return;
  const a=document.createElement("a");
  a.href=u; a.download="flexcam_"+Date.now()+".jpg"; a.click();
};

function setHorizon(on,quiet){
  horizonOn=on;
  $("bHorizon").setAttribute("aria-pressed",on?"true":"false");
  if(on){ refRoll=state.roll; if(!quiet) notice("Horizon Lock activado"); }
  else if(!quiet) notice("Horizon Lock desactivado");
}
$("bHorizon").onclick=()=>{
  if(!state.imuOk&&!horizonOn){ notice("IMU no disponible",1800); return; }
  setHorizon(!horizonOn);
};
$("bRecenter").onclick=()=>{ refRoll=state.roll; notice("Horizonte recentrado"); };
$("bGrid").onclick=()=>{ gridOn=!gridOn; $("grid").classList.toggle("on",gridOn);
  $("bGrid").setAttribute("aria-pressed",gridOn?"true":"false"); };
$("bFlip").onclick=async()=>{
  flipOn=!flipOn;
  $("bFlip").setAttribute("aria-pressed",flipOn?"true":"false");
  try{ applyState(await api("/api/flip?v="+(flipOn?1:0),{method:"POST"})); }catch(e){}
};
$("imuhead").onclick=()=>{
  const p=$("imupanel"); p.classList.toggle("open");
  try{ localStorage.setItem("fc_imupanel",p.classList.contains("open")?"1":"0"); }catch(e){}
};

// ---- rotación manual: sólo visual, no toca el IMU ni el ESP32 ----
$("selRot").onchange=e=>{
  manualRot=parseInt(e.target.value,10)||0;
  try{ localStorage.setItem("fc_rot",String(manualRot)); }catch(e){}
  relayout();
  notice(manualRot===360?"Rotación 360° (igual que 0°)":"Rotación "+manualRot+"°");
};

// ---- WebSocket de telemetría ----
let ws=null, wsTimer=null, wsFail=0;
function wsConnect(){
  try{ ws=new WebSocket("ws://"+location.host+"/ws"); }
  catch(e){ scheduleWs(); return; }
  ws.onopen=()=>{ wsFail=0; $("dNet").className="dot on"; $("tNet").textContent="Conectado"; };
  ws.onclose=()=>{ $("dNet").className="dot"; $("tNet").textContent="Reconectando"; scheduleWs(); };
  ws.onerror=()=>{ try{ws.close();}catch(e){} };
  ws.onmessage=ev=>{
    let d; try{ d=JSON.parse(ev.data); }catch(e){ return; }
    if(d.t==="i"){
      state.roll=d.r; state.pitch=d.p; state.yaw=d.y;
      state.hz=d.hz; state.acc=d.a; state.imuState=d.s;
      state.imuOk=(d.ok===1);
      paintImu();
    }else if(d.t==="s"){
      state.fps=d.fps; state.w=d.w; state.h=d.h; state.heap=d.heap;
      state.psram=d.ps; state.clients=d.cli; state.camReady=(d.cam===1);
      if(d.mode!=null&&d.mode!==state.mode&&!busy){
        const prev=state.mode; state.mode=d.mode; buildModes(); autoHorizon(prev);
      }
      paintCam();
      if(stream.style.width!==""&&Math.abs(layoutW/layoutH-state.w/state.h)>0.01) relayout();
    }
  };
}
function scheduleWs(){
  clearTimeout(wsTimer);
  wsFail=Math.min(wsFail+1,6);
  wsTimer=setTimeout(wsConnect,300*wsFail);
}

// ---- arranque ----
(function init(){
  try{
    const r=localStorage.getItem("fc_rot"); if(r!==null) manualRot=parseInt(r,10)||0;
    if(localStorage.getItem("fc_imupanel")==="1") $("imupanel").classList.add("open");
  }catch(e){}
  $("selRot").value=String(manualRot);
  addEventListener("resize",relayout);
  addEventListener("orientationchange",()=>setTimeout(relayout,250));
  new ResizeObserver(relayout).observe(viewport);
  requestAnimationFrame(render);
  api("/api/state").then(applyState).catch(()=>notice("Sin respuesta del ESP32",3000));
  wsConnect();
  startStream();
})();
</script>
</body>
</html>
)HTMLDOC";

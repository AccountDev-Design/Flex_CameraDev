// =====================================================================
//  fc_web_ui_v2.h — UI local, Horizon Lock 360° y grabación del visor.
//  El ESP32 entrega JPEG + cuaternión; el navegador compone los píxeles.
// =====================================================================
#pragma once
#include <Arduino.h>

static const char FC_INDEX_HTML[] PROGMEM = R"HTMLDOC(<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover,user-scalable=no">
<meta name="theme-color" content="#07080b">
<title>FlexCam S26</title>
<style>
:root{--bg:#07080b;--glass:rgba(20,22,28,.68);--glass2:rgba(35,38,48,.76);
--line:rgba(255,255,255,.11);--txt:#f2f4f8;--dim:#9ca4b2;--orange:#ff8a3d;
--cyan:#4dd6ff;--green:#4ade80;--red:#ff5d68;--r:17px}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;min-height:100%;background:var(--bg);color:var(--txt);
font:14px/1.35 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;overscroll-behavior:none}
button,select{font:inherit;color:inherit}
#app{min-height:100dvh;display:grid;grid-template-rows:auto minmax(260px,1fr) auto;
gap:8px;padding:8px;padding-top:max(8px,env(safe-area-inset-top));
padding-bottom:max(8px,env(safe-area-inset-bottom));overflow-x:hidden}
.glass{background:var(--glass);border:1px solid var(--line);border-radius:var(--r);
backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px)}
#topbar{display:flex;align-items:center;gap:6px;flex-wrap:wrap;padding:8px 10px}
#brand{font-weight:750;letter-spacing:.4px;margin-right:2px}#brand span{color:var(--orange)}
.chip{display:inline-flex;align-items:center;gap:6px;padding:4px 9px;border-radius:999px;
background:rgba(255,255,255,.06);border:1px solid var(--line);font-size:12px;white-space:nowrap}
.chip b{font-weight:650;font-variant-numeric:tabular-nums}.dot{width:8px;height:8px;
border-radius:50%;background:var(--red)}.dot.on{background:var(--green);box-shadow:0 0 8px var(--green)}
.dot.warn{background:var(--orange)}#recChip{display:none;color:var(--red)}#recChip.on{display:inline-flex}
#stage{position:relative;min-height:0;overflow:hidden;border-radius:var(--r);background:#000;
border:1px solid var(--line)}#viewport{position:absolute;inset:0;overflow:hidden}
#stream{position:absolute;left:50%;top:50%;display:block;transform-origin:50% 50%;
will-change:transform;image-rendering:auto}#hud{position:absolute;inset:0;pointer-events:none}
#grid{position:absolute;inset:0;display:none;background-image:
linear-gradient(rgba(255,255,255,.16) 1px,transparent 1px),
linear-gradient(90deg,rgba(255,255,255,.16) 1px,transparent 1px);background-size:33.333% 33.333%}
#grid.on{display:block}#hlevel{position:absolute;left:28%;right:28%;top:50%;height:2px;
background:rgba(255,255,255,.58);border-radius:2px;transform-origin:center;display:none;
box-shadow:0 0 7px #000}#hlevel.on{display:block}#hlevel.locked{background:var(--orange);
box-shadow:0 0 10px var(--orange)}#notice{position:absolute;z-index:5;left:50%;top:14px;
transform:translateX(-50%);max-width:92%;background:rgba(0,0,0,.78);border:1px solid var(--line);
border-radius:999px;padding:7px 14px;font-size:12.5px;text-align:center;opacity:0;
transition:opacity .18s;pointer-events:none}#notice.show{opacity:1}
#statusBadge{position:absolute;z-index:4;left:50%;bottom:12px;transform:translateX(-50%);
max-width:92%;background:rgba(255,138,61,.18);border:1px solid var(--orange);color:#ffc69e;
border-radius:999px;padding:5px 12px;font-size:12px;text-align:center;display:none}
#statusBadge.on{display:block}#statusBadge.bad{border-color:var(--red);color:#ffb4ba;background:rgba(255,93,104,.18)}
#flash{position:absolute;inset:0;background:#fff;opacity:0;pointer-events:none}#flash.go{animation:fl .28s ease-out}
@keyframes fl{0%{opacity:.85}100%{opacity:0}}#shot{position:absolute;right:10px;bottom:10px;
width:74px;border-radius:10px;border:2px solid rgba(255,255,255,.8);display:none;
box-shadow:0 4px 18px #000}#shot.on{display:block}#recordCanvas{display:none}
#side{display:grid;gap:8px}.panel{min-width:0;overflow:hidden}
#controls{padding:10px;display:grid;gap:10px}#modes{display:flex;gap:6px;overflow-x:auto;
scrollbar-width:none;padding-bottom:2px}#modes::-webkit-scrollbar{display:none}.mode{flex:none;
padding:8px 14px;border-radius:999px;background:rgba(255,255,255,.06);border:1px solid var(--line);
white-space:nowrap;font-size:13px;cursor:pointer}.mode[aria-pressed=true]{background:rgba(255,138,61,.19);
border-color:var(--orange);color:#ffd5b8}.mode:disabled{opacity:.45}
#controlRow{display:flex;align-items:center;gap:10px;justify-content:space-between;flex-wrap:wrap}
#leftCtl,#rightCtl{display:flex;align-items:center;gap:8px;flex-wrap:wrap;min-width:0}
.icon{width:43px;height:43px;border-radius:50%;background:rgba(255,255,255,.07);border:1px solid var(--line);
display:grid;place-items:center;cursor:pointer;flex:none}.icon svg{width:21px;height:21px;fill:none;
stroke:currentColor;stroke-width:1.9;stroke-linecap:round;stroke-linejoin:round}
.icon[aria-pressed=true]{background:rgba(255,138,61,.2);border-color:var(--orange);color:var(--orange);
box-shadow:0 0 14px rgba(255,138,61,.32)}.icon.recording{background:rgba(255,93,104,.22);
border-color:var(--red);color:var(--red);animation:pulse 1.1s ease-in-out infinite}
@keyframes pulse{50%{box-shadow:0 0 18px rgba(255,93,104,.55)}}.icon:disabled{opacity:.4}
#shutter{width:68px;height:68px;border-radius:50%;border:3px solid rgba(255,255,255,.94);
background:radial-gradient(circle at 50% 38%,#fff,#e3e6ed);box-shadow:0 6px 22px #000;cursor:pointer}
#shutter:active{transform:scale(.93)}#shutter.busy{background:radial-gradient(circle,#ffd7bc,var(--orange))}
label.field{display:flex;flex-direction:column;gap:3px;color:var(--dim);font-size:10px;
text-transform:uppercase;letter-spacing:.45px}select{background:rgba(255,255,255,.07);border:1px solid var(--line);
border-radius:10px;padding:8px 7px;font-size:12px;cursor:pointer}
#imuHead{display:flex;align-items:center;gap:8px;width:100%;padding:10px 12px;background:none;
border:0;text-align:left;cursor:pointer}#imuHead .caret{margin-left:auto;transition:transform .2s}
#imuPanel.open #imuHead .caret{transform:rotate(180deg)}#imuBody{display:none;padding:0 12px 12px;
grid-template-columns:repeat(auto-fit,minmax(125px,1fr));gap:8px}#imuPanel.open #imuBody{display:grid}
.kv{background:rgba(255,255,255,.05);border:1px solid var(--line);border-radius:10px;padding:8px 10px;
min-width:0}.kv .k{font-size:10px;color:var(--dim);text-transform:uppercase;letter-spacing:.45px}
.kv .v{font-size:16px;font-weight:650;font-variant-numeric:tabular-nums;margin-top:2px;overflow-wrap:anywhere}
.kv .v.small{font-size:12px;font-weight:500}.bar{height:4px;border-radius:2px;background:rgba(255,255,255,.1);
margin-top:6px;overflow:hidden}.bar i{display:block;height:100%;width:0;background:var(--cyan)}
@media(min-width:820px) and (orientation:landscape){#app{height:100dvh;min-height:0;overflow:hidden;
grid-template-columns:minmax(0,1fr) 340px;grid-template-rows:auto minmax(0,1fr);
grid-template-areas:"top side" "stage side"}#topbar{grid-area:top}#stage{grid-area:stage}#side{grid-area:side;
min-height:0;overflow-y:auto;grid-template-rows:auto auto}#controls{order:1}#imuPanel{order:2}#imuBody{grid-template-columns:1fr 1fr}}
@media(max-width:520px){#app{grid-template-rows:auto minmax(250px,48vh) auto}.icon{width:40px;height:40px}
#shutter{width:62px;height:62px}#rightCtl{width:100%;justify-content:center}.mode{padding:7px 12px}}
</style>
</head>
<body>
<div id="app">
  <header id="topbar" class="glass">
    <span id="brand">Flex<span>Cam</span></span>
    <span class="chip"><i class="dot" id="dNet"></i><b id="tNet">Conectando</b></span>
    <span class="chip">Modo <b id="tMode">—</b></span>
    <span class="chip">Res <b id="tRes">—</b></span>
    <span class="chip">FPS <b id="tFps">0.0</b></span>
    <span class="chip"><i class="dot" id="dImu"></i>IMU <b id="tImu">—</b></span>
    <span class="chip">Temp <b id="tTemp">—</b></span>
    <span class="chip" id="recChip">● REC <b id="tRec">00:00</b></span>
  </header>

  <main id="stage">
    <div id="viewport"><img id="stream" alt="Transmisión en vivo"></div>
    <div id="hud"><div id="grid"></div><div id="hlevel"></div></div>
    <div id="notice"></div><div id="statusBadge"></div><div id="flash"></div>
    <img id="shot" alt="Última foto"><canvas id="recordCanvas"></canvas>
  </main>

  <div id="side">
    <section id="controls" class="glass panel">
      <div id="modes"></div>
      <div id="controlRow">
        <div id="leftCtl">
          <button class="icon" id="bHorizon" aria-pressed="false" title="Horizon Lock 360°">
            <svg viewBox="0 0 24 24"><path d="M2 12h20"/><circle cx="12" cy="12" r="8"/></svg></button>
          <button class="icon" id="bRecenter" title="Definir posición inicial">
            <svg viewBox="0 0 24 24"><path d="M12 3v4M12 17v4M3 12h4M17 12h4"/><circle cx="12" cy="12" r="3"/></svg></button>
          <button class="icon" id="bGrid" aria-pressed="false" title="Cuadrícula">
            <svg viewBox="0 0 24 24"><path d="M3 9h18M3 15h18M9 3v18M15 3v18"/></svg></button>
          <button class="icon" id="bRecord" aria-pressed="false" title="Grabar vista estabilizada">
            <svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="8"/><circle cx="12" cy="12" r="3" fill="currentColor" stroke="none"/></svg></button>
        </div>
        <button id="shutter" title="Capturar fotografía"></button>
        <div id="rightCtl">
          <label class="field">Rotar cámara<select id="selRot"><option value="0">0°</option>
            <option value="90">90°</option><option value="180">180°</option>
            <option value="270">270°</option><option value="360">360°</option></select></label>
          <label class="field">Encuadre<select id="selCrop"><option value="dynamic">Dinámico</option>
            <option value="stable">Fijo 360°</option><option value="wide">Amplio</option></select></label>
          <label class="field">Montaje IMU<select id="selMount"><option value="0">0°</option>
            <option value="90">90°</option><option value="180">180°</option><option value="270">270°</option></select></label>
          <label class="field">Sentido IMU<select id="selDir"><option value="1">Normal</option>
            <option value="-1">Invertido</option></select></label>
          <button class="icon" id="bFlip" aria-pressed="false" title="Voltear sensor">
            <svg viewBox="0 0 24 24"><path d="M12 3v18M7 8l-3 4 3 4M17 8l3 4-3 4"/></svg></button>
        </div>
      </div>
    </section>

    <section id="imuPanel" class="glass panel">
      <button id="imuHead"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9"><path d="M3 12h4l2-6 4 12 2-6h6"/></svg>
        <span>IMU y rendimiento en tiempo real</span><svg class="caret" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M6 9l6 6 6-6"/></svg></button>
      <div id="imuBody">
        <div class="kv"><div class="k">Roll</div><div class="v" id="vRoll">+0.00°</div></div>
        <div class="kv"><div class="k">Pitch</div><div class="v" id="vPitch">+0.00°</div></div>
        <div class="kv"><div class="k">Yaw</div><div class="v" id="vYaw">+0.00°</div></div>
        <div class="kv"><div class="k">Horizonte cámara</div><div class="v" id="vHorizon">+0.00°</div></div>
        <div class="kv"><div class="k">Compensación</div><div class="v" id="vComp">+0.00°</div></div>
        <div class="kv"><div class="k">Rotación total</div><div class="v" id="vTotal">+0.00°</div></div>
        <div class="kv"><div class="k">Zoom horizonte</div><div class="v" id="vZoom">1.000×</div></div>
        <div class="kv"><div class="k">Rango Horizon Lock</div><div class="v small">360° continuo</div></div>
        <div class="kv"><div class="k">IMU real</div><div class="v" id="vHz">0 Hz</div></div>
        <div class="kv"><div class="k">FPS captura / red</div><div class="v" id="vFps">0.0 / 0.0</div></div>
        <div class="kv"><div class="k">Estado IMU</div><div class="v small" id="vState">—</div></div>
        <div class="kv"><div class="k">Calibración</div><div class="v small" id="vAcc">—</div><div class="bar"><i id="barAcc"></i></div></div>
        <div class="kv"><div class="k">Confianza horizonte</div><div class="v small" id="vConfidence">—</div></div>
        <div class="kv"><div class="k">Cuaternión W · X · Y · Z</div><div class="v small" id="vQuat">—</div></div>
        <div class="kv"><div class="k">Gravedad X · Y · Z</div><div class="v small" id="vGravity">—</div></div>
        <div class="kv"><div class="k">Captura / envío</div><div class="v small" id="vLatency">—</div></div>
        <div class="kv"><div class="k">Descartes sensor / atraso</div><div class="v small" id="vDrops">0 / 0</div></div>
        <div class="kv"><div class="k">Frame / antigüedad</div><div class="v small" id="vFrame">—</div></div>
        <div class="kv"><div class="k">Memoria libre</div><div class="v small" id="vMem">—</div></div>
        <div class="kv"><div class="k">Temperatura SoC</div><div class="v small" id="vTemp">—</div></div>
      </div>
    </section>
  </div>
</div>

<script>
"use strict";
const $=id=>document.getElementById(id);
const stream=$("stream"),viewport=$("viewport"),canvas=$("recordCanvas"),ctx=canvas.getContext("2d");
const state={modes:[],mode:null,w:800,h:600,fps:0,sfps:0,targetFps:30,camReady:false,
roll:0,pitch:0,yaw:0,horizon:0,horizonWrapped:0,horizonConfidence:0,horizonValid:false,
qi:0,qj:0,qk:0,qr:1,gx:0,gy:0,gz:1,hz:0,acc:0,imuState:"",imuOk:false,
heap:0,largest:0,psram:0,clients:0,drop:0,poolDrop:0,captureMs:0,sendMs:0,
frameBytes:0,frameAge:0,temp:-127,thermal:0,captured:0,sent:0};

let horizonOn=false,refHorizon=0,manualRot=0,imuMount=0,horizonDirection=1;
let cropProfile="dynamic",gridOn=false,flipOn=false,busy=false;
let layoutW=0,layoutH=0,baseScale=1,safeScale=1,curZoom=1,curComp=0,totalRotation=0;
let lastTransform="",lastPanelPaint=0,wideEdges=false,lastImuArrival=0;

const norm180=a=>{a=(a+180)%360;if(a<0)a+=360;return a-180};
const fmt=a=>(a>=0?"+":"−")+Math.abs(a).toFixed(2)+"°";
const clamp=(v,lo,hi)=>Math.max(lo,Math.min(hi,v));
function coverScale(W,H,w,h,deg){const t=deg*Math.PI/180,c=Math.abs(Math.cos(t)),s=Math.abs(Math.sin(t));
return Math.max((W*c+H*s)/w,(W*s+H*c)/h)}
function worstScale(W,H,w,h){let max=0;for(let d=0;d<=90;d+=.5)max=Math.max(max,coverScale(W,H,w,h,d));return max}
function measuredHorizon(){return state.horizon+imuMount}

function relayout(){
  const W=viewport.clientWidth,H=viewport.clientHeight;if(!W||!H)return;
  const ar=(state.w||4)/(state.h||3);let w=W,h=W/ar;if(h>H){h=H;w=H*ar}
  layoutW=w;layoutH=h;stream.style.width=w+"px";stream.style.height=h+"px";
  baseScale=coverScale(W,H,w,h,manualRot%360);safeScale=worstScale(W,H,w,h);
  lastTransform="";
}

function desiredZoom(angle){
  const W=viewport.clientWidth,H=viewport.clientHeight;
  const needed=coverScale(W,H,layoutW,layoutH,angle)/baseScale;
  wideEdges=false;
  if(cropProfile==="stable")return Math.max(1,safeScale/baseScale);
  if(cropProfile==="wide"){wideEdges=needed>1.2001;return clamp(needed,1,1.2)}
  return Math.max(1.015,needed);
}

function updateStatusBadge(){
  const badge=$("statusBadge");let text="",bad=false;
  if(state.thermal===2){text="Temperatura crítica: FPS reducido";bad=true}
  else if(state.thermal===1){text="Protección térmica activa"}
  else if(horizonOn&&(!state.imuOk||!state.horizonValid)){text="Horizonte sin referencia: se mantiene el último ángulo"}
  else if(wideEdges){text="Encuadre amplio: pueden aparecer bordes"}
  badge.textContent=text;badge.classList.toggle("on",!!text);badge.classList.toggle("bad",bad);
}

function render(now){
  const base=manualRot%360;
  if(horizonOn&&state.imuOk&&state.horizonValid){curComp=-(measuredHorizon()-refHorizon)*horizonDirection}
  else if(!horizonOn){curComp=0}
  totalRotation=base+curComp;
  const targetZoom=horizonOn?desiredZoom(totalRotation):1;
  curZoom=targetZoom>curZoom?targetZoom:curZoom+(targetZoom-curZoom)*.18;
  if(Math.abs(curZoom-targetZoom)<.0005)curZoom=targetZoom;
  const transform="translate(-50%,-50%) rotate("+totalRotation.toFixed(3)+"deg) scale("+
    (baseScale*curZoom).toFixed(4)+")";
  if(transform!==lastTransform){stream.style.transform=transform;lastTransform=transform}

  const level=$("hlevel");level.classList.toggle("on",horizonOn||state.imuOk);
  level.classList.toggle("locked",horizonOn);
  level.style.transform="rotate("+(horizonOn?0:norm180(measuredHorizon())).toFixed(2)+"deg)";
  if(recording)drawRecordFrame(totalRotation);
  if(now-lastPanelPaint>=50){lastPanelPaint=now;paintAll();updateStatusBadge()}
  requestAnimationFrame(render);
}

function paintAll(){
  $("vRoll").textContent=fmt(state.roll);$("vPitch").textContent=fmt(state.pitch);
  $("vYaw").textContent=fmt(state.yaw);$("vHorizon").textContent=fmt(measuredHorizon());
  $("vComp").textContent=fmt(curComp);$("vTotal").textContent=fmt(totalRotation);
  $("vZoom").textContent=curZoom.toFixed(3)+"×";$("vHz").textContent=state.hz.toFixed(0)+" Hz";
  $("vFps").textContent=state.fps.toFixed(1)+" / "+state.sfps.toFixed(1);
  $("vState").textContent=state.imuState||"—";
  $("vAcc").textContent=["Sin calibrar","Baja","Media","Alta"][state.acc]||"—";
  $("barAcc").style.width=clamp(state.acc/3*100,0,100)+"%";
  $("vConfidence").textContent=(state.horizonConfidence*100).toFixed(0)+"%"+
    (state.horizonValid?"":" · indeterminado");
  $("vQuat").textContent=[state.qr,state.qi,state.qj,state.qk].map(v=>v.toFixed(4)).join(" · ");
  $("vGravity").textContent=[state.gx,state.gy,state.gz].map(v=>v.toFixed(3)).join(" · ");
  $("vLatency").textContent=state.captureMs.toFixed(1)+" ms / "+state.sendMs.toFixed(1)+" ms";
  $("vDrops").textContent=state.drop+" / "+state.poolDrop;
  $("vFrame").textContent=Math.round(state.frameBytes/1024)+" KB / "+state.frameAge+" ms";
  $("vMem").textContent=Math.round(state.heap/1024)+" KB DRAM · "+Math.round(state.psram/1024)+" KB PSRAM";
  const tempOk=state.temp>-100;$("vTemp").textContent=tempOk?state.temp.toFixed(1)+" °C · "+
    (["Normal","Limitada","Crítica"][state.thermal]||"—"):"—";
  $("tTemp").textContent=tempOk?state.temp.toFixed(0)+"°C":"—";
  $("tFps").textContent=state.fps.toFixed(1);$("tRes").textContent=state.w+"×"+state.h;
  $("tImu").textContent=state.imuOk?state.hz.toFixed(0)+" Hz":"no disp.";
  $("dImu").className="dot"+(state.imuOk?" on":state.imuState==="Conectando"?" warn":"");
}

function notice(text,ms=1800){const n=$("notice");n.textContent=text;n.classList.add("show");
clearTimeout(notice.timer);notice.timer=setTimeout(()=>n.classList.remove("show"),ms)}

let streamEpoch=0,streamRetry=null;
function streamUrl(){return "http://"+location.hostname+":81/stream?v="+Date.now()}
function startStream(){
  const epoch=++streamEpoch;clearTimeout(streamRetry);stream.crossOrigin="anonymous";
  stream.onload=()=>{};stream.onerror=()=>{if(epoch!==streamEpoch||busy)return;
    clearTimeout(streamRetry);streamRetry=setTimeout(()=>{if(epoch===streamEpoch&&!busy){stream.src=streamUrl()}},650)};
  stream.src=streamUrl();
}
function stopStream(){++streamEpoch;clearTimeout(streamRetry);stream.onerror=null;stream.removeAttribute("src")}

async function api(path,options){const response=await fetch(path,Object.assign({cache:"no-store"},options||{}));
if(!response.ok)throw new Error("HTTP "+response.status);return response.json()}
function buildModes(){
  const box=$("modes");if(box.childElementCount!==state.modes.length){box.innerHTML="";
    state.modes.forEach(mode=>{const b=document.createElement("button");b.className="mode";b.textContent=mode.label;
      b.dataset.id=mode.id;b.title=mode.prev+" · objetivo "+mode.targetFps+" FPS";b.onclick=()=>setMode(mode.id);box.appendChild(b)})}
  [...box.children].forEach(b=>{const current=state.modes[state.mode];const on=current&&b.dataset.id===current.id;
    b.setAttribute("aria-pressed",on?"true":"false");b.disabled=busy});
  const current=state.modes[state.mode];$("tMode").textContent=current?current.label:"—";
}
function applyState(data){
  const previous=state.mode;if(data.modes)state.modes=data.modes;if(data.mode!=null)state.mode=data.mode;
  if(data.w)state.w=data.w;if(data.h)state.h=data.h;if(data.fps!=null)state.fps=data.fps;
  if(data.sendFps!=null)state.sfps=data.sendFps;if(data.targetFps!=null)state.targetFps=data.targetFps;
  if(data.camReady!=null)state.camReady=data.camReady;if(data.flip!=null){flipOn=!!data.flip;
    $("bFlip").setAttribute("aria-pressed",flipOn?"true":"false")}
  buildModes();autoHorizon(previous);relayout();
}
function autoHorizon(previous){if(previous===state.mode)return;const mode=state.modes[state.mode];
  if(mode&&mode.horizon)setHorizon(true,true);else setHorizon(false,true)}

async function setMode(id){
  if(busy)return;if(recording)stopRecording();busy=true;buildModes();$("shutter").disabled=true;
  stopStream();notice("Cambiando de modo…",5000);
  try{const data=await api("/api/mode?m="+encodeURIComponent(id),{method:"POST"});
    if(data.error)notice(data.error,3000);else notice("Modo: "+data.label+(data.targetFps?" · "+data.targetFps+" FPS objetivo":""),1800);
    applyState(data)}catch(error){notice("No se pudo cambiar de modo",2600)}
  finally{busy=false;$("shutter").disabled=false;buildModes();startStream()}
}

async function capturePhoto(){
  if(busy)return;if(recording)stopRecording();busy=true;$("shutter").disabled=true;$("shutter").classList.add("busy");
  const mode=state.modes[state.mode],label=mode&&mode.captureLabel?mode.captureLabel:"máxima resolución";
  stopStream();notice("Capturando a "+label+"…",8000);
  try{const response=await fetch("/api/photo",{cache:"no-store"});if(!response.ok)throw new Error(await response.text());
    const blob=await response.blob();if(blob.size<512)throw new Error("imagen vacía");
    const width=Number(response.headers.get("X-FlexCam-Width"))||0,height=Number(response.headers.get("X-FlexCam-Height"))||0;
    const url=URL.createObjectURL(blob),shot=$("shot");if(shot.dataset.url)URL.revokeObjectURL(shot.dataset.url);
    shot.dataset.url=url;shot.dataset.width=width;shot.dataset.height=height;shot.src=url;shot.classList.add("on");
    $("flash").classList.remove("go");void $("flash").offsetWidth;$("flash").classList.add("go");
    const mp=width&&height?(width*height/1e6).toFixed(2)+" MP · ":"";
    notice("Foto real "+(width?width+"×"+height+" · ":"")+mp+Math.round(blob.size/1024)+" KB",3500)
  }catch(error){notice("Fallo al capturar: "+error.message,3500)}
  finally{busy=false;$("shutter").disabled=false;$("shutter").classList.remove("busy");startStream()}
}
$("shot").onclick=()=>{const shot=$("shot"),url=shot.dataset.url;if(!url)return;const a=document.createElement("a");
a.href=url;a.download="flexcam_"+(shot.dataset.width||"")+"x"+(shot.dataset.height||"")+"_"+Date.now()+".jpg";a.click()};

function setHorizon(on,quiet=false){horizonOn=on;$("bHorizon").setAttribute("aria-pressed",on?"true":"false");
if(on){refHorizon=measuredHorizon();if(!quiet)notice("Horizon Lock 360° activado")}
else if(!quiet)notice("Horizon Lock desactivado")}
$("bHorizon").onclick=()=>{if(!state.imuOk&&!horizonOn){notice("IMU no disponible");return}setHorizon(!horizonOn)};
$("bRecenter").onclick=()=>{refHorizon=measuredHorizon();curComp=0;notice("Posición inicial definida")};
$("bGrid").onclick=()=>{gridOn=!gridOn;$("grid").classList.toggle("on",gridOn);$("bGrid").setAttribute("aria-pressed",gridOn?"true":"false")};
$("shutter").onclick=capturePhoto;
$("bFlip").onclick=async()=>{flipOn=!flipOn;$("bFlip").setAttribute("aria-pressed",flipOn?"true":"false");
try{applyState(await api("/api/flip?v="+(flipOn?1:0),{method:"POST"}))}catch(error){notice("No se pudo voltear el sensor")}};

$("selRot").onchange=event=>{manualRot=Number(event.target.value)||0;localSet("fc_rot",manualRot);relayout();
notice(manualRot===360?"Cámara 360° · equivalente a 0°":"Cámara rotada "+manualRot+"°")};
$("selCrop").onchange=event=>{cropProfile=event.target.value;localSet("fc_crop",cropProfile);curZoom=1;relayout();notice("Encuadre: "+event.target.options[event.target.selectedIndex].text)};
$("selMount").onchange=event=>{imuMount=Number(event.target.value)||0;localSet("fc_mount",imuMount);refHorizon=measuredHorizon();curComp=0;notice("Montaje IMU "+imuMount+"° · recalibrado")};
$("selDir").onchange=event=>{horizonDirection=Number(event.target.value)===-1?-1:1;localSet("fc_dir",horizonDirection);refHorizon=measuredHorizon();curComp=0;notice("Sentido IMU: "+(horizonDirection===1?"normal":"invertido"))};
$("imuHead").onclick=()=>{const panel=$("imuPanel");panel.classList.toggle("open");localSet("fc_imu_panel",panel.classList.contains("open")?1:0)};
function localSet(key,value){try{localStorage.setItem(key,String(value))}catch(error){}}

let recorder=null,recording=false,recordChunks=[],recordStarted=0,recordTimer=null;
function recordingMime(){if(!window.MediaRecorder)return"";const types=["video/mp4;codecs=avc1.42E01E","video/webm;codecs=vp9","video/webm;codecs=vp8","video/webm"];
return types.find(type=>MediaRecorder.isTypeSupported(type))||""}
function setupRecordCanvas(){const sourceW=state.w||800,sourceH=state.h||600;
const scale=Math.min(1,1280/sourceW,960/sourceH);canvas.width=Math.max(2,Math.round(sourceW*scale/2)*2);
canvas.height=Math.max(2,Math.round(sourceH*scale/2)*2)}
function recordScaleFor(angle,sw,sh){const W=canvas.width,H=canvas.height,base=coverScale(W,H,sw,sh,manualRot%360);
if(cropProfile==="stable")return worstScale(W,H,sw,sh);const needed=coverScale(W,H,sw,sh,angle);
return cropProfile==="wide"?Math.min(needed,base*1.2):needed}
function drawRecordFrame(angle){if(!ctx||!stream.naturalWidth||!stream.naturalHeight)return;
const W=canvas.width,H=canvas.height,sw=stream.naturalWidth,sh=stream.naturalHeight;
ctx.setTransform(1,0,0,1,0,0);ctx.fillStyle="#000";ctx.fillRect(0,0,W,H);ctx.translate(W/2,H/2);
ctx.rotate(angle*Math.PI/180);const scale=recordScaleFor(angle,sw,sh);ctx.scale(scale,scale);
try{ctx.drawImage(stream,-sw/2,-sh/2,sw,sh)}catch(error){}}
function updateRecClock(){if(!recording)return;const seconds=Math.floor((Date.now()-recordStarted)/1000);
$("tRec").textContent=String(Math.floor(seconds/60)).padStart(2,"0")+":"+String(seconds%60).padStart(2,"0");
if(seconds>=300){stopRecording();notice("Grabación detenida a los 5 minutos",3000)}}
function startRecording(){
  if(recording)return;if(!canvas.captureStream||!window.MediaRecorder){notice("Este navegador no permite grabar el visor",3000);return}
  if(!stream.naturalWidth){notice("Espera a que aparezca la transmisión",2200);return}
  const mime=recordingMime();if(!mime){notice("Formato de grabación no compatible",2600);return}
  try{setupRecordCanvas();drawRecordFrame(totalRotation);const media=canvas.captureStream(Math.min(30,Math.max(4,state.targetFps||30)));
    recorder=new MediaRecorder(media,{mimeType:mime,videoBitsPerSecond:3000000});recordChunks=[];
    recorder.ondataavailable=event=>{if(event.data&&event.data.size)recordChunks.push(event.data)};
    recorder.onerror=()=>{notice("La grabación falló",2600);if(recording)stopRecording()};
    recorder.onstop=()=>{media.getTracks().forEach(track=>track.stop());const type=recorder.mimeType||mime;
      const blob=new Blob(recordChunks,{type}),url=URL.createObjectURL(blob),ext=type.includes("mp4")?"mp4":"webm";
      const a=document.createElement("a");a.href=url;a.download="flexcam_estabilizado_"+Date.now()+"."+ext;a.click();
      setTimeout(()=>URL.revokeObjectURL(url),15000);recordChunks=[];notice("Vídeo estabilizado guardado · "+Math.round(blob.size/1024)+" KB",3200)};
    recorder.start(1000);recording=true;recordStarted=Date.now();$("bRecord").classList.add("recording");
    $("bRecord").setAttribute("aria-pressed","true");$("recChip").classList.add("on");updateRecClock();
    recordTimer=setInterval(updateRecClock,500);notice("Grabando la vista estabilizada")
  }catch(error){recording=false;notice("No se pudo iniciar: "+error.message,3000)}
}
function stopRecording(){if(!recording)return;recording=false;clearInterval(recordTimer);$("bRecord").classList.remove("recording");
$("bRecord").setAttribute("aria-pressed","false");$("recChip").classList.remove("on");
if(recorder&&recorder.state!=="inactive")recorder.stop()}
$("bRecord").onclick=()=>recording?stopRecording():startRecording();

let ws=null,wsTimer=null,wsFailures=0;
function connectWs(){
  try{ws=new WebSocket("ws://"+location.host+"/ws")}catch(error){scheduleWs();return}
  ws.onopen=()=>{wsFailures=0;$("dNet").className="dot on";$("tNet").textContent="Conectado"};
  ws.onclose=()=>{$("dNet").className="dot";$("tNet").textContent="Reconectando";scheduleWs()};
  ws.onerror=()=>{try{ws.close()}catch(error){}};
  ws.onmessage=event=>{let data;try{data=JSON.parse(event.data)}catch(error){return}
    if(data.t==="i"){
      const wasUsable=state.imuOk&&state.horizonValid;
      state.roll=data.r;state.pitch=data.p;state.yaw=data.y;state.horizon=data.h;state.horizonWrapped=data.hw;
      state.horizonConfidence=data.hc;state.horizonValid=data.hv===1;state.qi=data.qi;state.qj=data.qj;
      state.qk=data.qk;state.qr=data.qr;state.gx=data.gx;state.gy=data.gy;state.gz=data.gz;
      state.hz=data.hz;state.acc=data.a;state.imuState=data.s;state.imuOk=data.ok===1;lastImuArrival=performance.now();
      if(horizonOn&&!wasUsable&&state.imuOk&&state.horizonValid){refHorizon=measuredHorizon()+curComp/horizonDirection}
    }else if(data.t==="s"){
      state.fps=data.fps;state.sfps=data.sfps;state.targetFps=data.tfps;state.w=data.w;state.h=data.h;
      state.heap=data.heap;state.largest=data.largest;state.psram=data.ps;state.clients=data.cli;
      state.camReady=data.cam===1;state.drop=data.drop;state.poolDrop=data.pdrop;state.captureMs=data.capms;
      state.sendMs=data.sendms;state.frameBytes=data.bytes;state.frameAge=data.age;state.temp=data.temp;
      state.thermal=data.thermal;state.captured=data.captured;state.sent=data.sent;
      if(data.mode!=null&&data.mode!==state.mode&&!busy){const previous=state.mode;state.mode=data.mode;buildModes();autoHorizon(previous)}
    }
  }
}
function scheduleWs(){clearTimeout(wsTimer);wsFailures=Math.min(wsFailures+1,7);wsTimer=setTimeout(connectWs,300*Math.pow(1.45,wsFailures))}

(function init(){
  try{manualRot=Number(localStorage.getItem("fc_rot"))||0;imuMount=Number(localStorage.getItem("fc_mount"))||0;
    horizonDirection=Number(localStorage.getItem("fc_dir"))===-1?-1:1;
    const savedCrop=localStorage.getItem("fc_crop");if(["dynamic","stable","wide"].includes(savedCrop))cropProfile=savedCrop;
    if(localStorage.getItem("fc_imu_panel")==="1")$("imuPanel").classList.add("open")
  }catch(error){}
  $("selRot").value=String(manualRot);$("selMount").value=String(imuMount);
  $("selDir").value=String(horizonDirection);$("selCrop").value=cropProfile;
  addEventListener("resize",relayout);addEventListener("orientationchange",()=>setTimeout(relayout,220));
  if(window.ResizeObserver)new ResizeObserver(relayout).observe(viewport);
  document.addEventListener("visibilitychange",()=>{if(document.hidden&&recording)stopRecording()});
  requestAnimationFrame(render);api("/api/state").then(applyState).catch(()=>notice("Sin respuesta del ESP32",3000));
  connectWs();startStream();
})();
</script>
</body>
</html>)HTMLDOC";

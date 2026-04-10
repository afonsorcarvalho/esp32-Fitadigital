/**
 * @file web_remote_html.h
 * @brief Página HTML/JS/CSS embutida para o Web Remote Display (JPEG stream).
 *
 * Recebe frames JPEG completos via WebSocket binary.
 * Exibe via <img> nativo (decodificação JPEG pelo browser).
 * Suporta touch/mouse remoto, auto-reconnect com backoff.
 */
#pragma once

static const char WEB_REMOTE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0,user-scalable=no">
<title>FitaDigital Remote</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{
  background:#1a1a2e;
  display:flex;flex-direction:column;
  align-items:center;justify-content:center;
  min-height:100vh;
  font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;
  color:#e0e0e0;
  overflow:hidden;
}
#bar{
  display:flex;align-items:center;gap:10px;
  margin-bottom:10px;font-size:14px;
}
#dot{
  width:10px;height:10px;border-radius:50%;
  background:#f44;transition:background .3s;
}
#dot.on{background:#4f4}
#fps{color:#888;font-size:12px;min-width:80px}
#sz{color:#666;font-size:12px;min-width:60px}
#wrap{
  border:2px solid #333;border-radius:4px;
  overflow:hidden;position:relative;
  box-shadow:0 4px 20px rgba(0,0,0,.5);
}
#display{
  display:block;cursor:pointer;
  image-rendering:auto;
  width:800px;height:480px;
  background:#000;
}
canvas{display:none;position:absolute;top:0;left:0;pointer-events:none}
#foot{margin-top:10px;font-size:12px;color:#666}
</style>
</head>
<body>
<div id="bar">
  <span id="dot"></span>
  <span id="stxt">Desconectado</span>
  <span id="fps"></span>
  <span id="sz"></span>
</div>
<div id="wrap">
  <img id="display" alt="Remote Display">
</div>
<div id="foot">FitaDigital &mdash; Remote Display (JPEG stream, <span id="vres">800x480</span>)</div>
<script>
(function(){
"use strict";
var img=document.getElementById("display"),
    dot=document.getElementById("dot"),
    stxt=document.getElementById("stxt"),
    fpsEl=document.getElementById("fps"),
    szEl=document.getElementById("sz"),
    vresEl=document.getElementById("vres"),
    ws=null,rtimer=null,
    RDLY_MIN=1000,RDLY_MAX=8000,rdly=RDLY_MIN,
    frameCount=0,lastFpsTime=0,
    CW=800,CH=480,
    prevUrl=null;

function connect(){
  if(ws&&ws.readyState<=1)return;
  stxt.textContent="Conectando\u2026";
  try{ws=new WebSocket("ws://"+location.host+"/ws");}catch(e){sched();return;}
  ws.binaryType="arraybuffer";
  ws.onopen=function(){
    dot.classList.add("on");
    stxt.textContent="Conectado";
    rdly=RDLY_MIN;
    frameCount=0;lastFpsTime=performance.now();
  };
  ws.onclose=function(){
    dot.classList.remove("on");
    stxt.textContent="Reconectando ("+Math.round(rdly/1000)+"s)\u2026";
    sched();
  };
  ws.onerror=function(e){
    try{ws.close();}catch(e2){}
  };
  ws.onmessage=onMsg;
}
function sched(){
  if(rtimer)return;
  rtimer=setTimeout(function(){rtimer=null;connect();},rdly);
  rdly=Math.min(rdly*1.5,RDLY_MAX);
}

function onMsg(evt){
  if(typeof evt.data==="string"){
    /* Mensagem de controlo: CFG,<cw>,<ch>,<q>,<ms> */
    if(evt.data.indexOf("CFG,")===0){
      var p=evt.data.split(",");
      if(p.length>=3){
        var nw=parseInt(p[1],10), nh=parseInt(p[2],10);
        if(nw>0&&nh>0){
          CW=nw;CH=nh;
          if(vresEl)vresEl.textContent=CW+"x"+CH;
          fit();
        }
      }
    }
    return;
  }
  if(!(evt.data instanceof ArrayBuffer)||evt.data.byteLength<2){
    return;
  }

  /* Dados recebidos = frame JPEG completo */
  var blob=new Blob([evt.data],{type:"image/jpeg"});
  var url=URL.createObjectURL(blob);

  /* Libera URL anterior para evitar leak de memória */
  if(prevUrl)URL.revokeObjectURL(prevUrl);
  prevUrl=url;
  img.src=url;

  /* Métricas */
  frameCount++;
  szEl.textContent=Math.round(evt.data.byteLength/1024)+" KB";
  var now=performance.now();
  if(now-lastFpsTime>=2000){
    fpsEl.textContent=(frameCount/((now-lastFpsTime)/1000)).toFixed(1)+" fps";
    frameCount=0;lastFpsTime=now;
  }
}

img.onload=function(){};
img.onerror=function(){};

/* Ajusta tamanho do display ao viewport */
function fit(){
  var mw=window.innerWidth-32, mh=window.innerHeight-90,
      sc=Math.min(mw/CW,mh/CH,4);
  img.style.width=Math.floor(CW*sc)+"px";
  img.style.height=Math.floor(CH*sc)+"px";
}
window.addEventListener("resize",fit);
fit();

/* Ponteiro remoto (touch/mouse) */
function sendPtr(flag,x,y){
  if(!ws||ws.readyState!==1)return;
  var b=new ArrayBuffer(5),dv=new DataView(b);
  dv.setUint8(0,flag);dv.setUint16(1,x);dv.setUint16(3,y);
  ws.send(b);
}
function pos(e){
  var r=img.getBoundingClientRect(),
      sx=CW/r.width, sy=CH/r.height,
      cx,cy;
  if(e.touches&&e.touches.length){cx=e.touches[0].clientX;cy=e.touches[0].clientY;}
  else{cx=e.clientX;cy=e.clientY;}
  return{x:Math.max(0,Math.min(CW-1,Math.round((cx-r.left)*sx))),
         y:Math.max(0,Math.min(CH-1,Math.round((cy-r.top)*sy)))};
}
img.addEventListener("mousedown",function(e){var p=pos(e);sendPtr(1,p.x,p.y);});
img.addEventListener("mousemove",function(e){if(e.buttons&1){var p=pos(e);sendPtr(1,p.x,p.y);}});
img.addEventListener("mouseup",function(e){var p=pos(e);sendPtr(0,p.x,p.y);});
img.addEventListener("mouseleave",function(e){var p=pos(e);sendPtr(0,p.x,p.y);});
img.addEventListener("touchstart",function(e){e.preventDefault();var p=pos(e);sendPtr(1,p.x,p.y);},{passive:false});
img.addEventListener("touchmove",function(e){e.preventDefault();var p=pos(e);sendPtr(1,p.x,p.y);},{passive:false});
img.addEventListener("touchend",function(e){e.preventDefault();sendPtr(0,0,0);},{passive:false});
img.addEventListener("touchcancel",function(e){e.preventDefault();sendPtr(0,0,0);},{passive:false});

connect();
})();
</script>
</body>
</html>
)rawliteral";

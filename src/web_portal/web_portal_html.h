/**
 * @file web_portal_html.h
 * @brief Interface única do portal (configurações, logs, ficheiros).
 */
#pragma once

#include <Arduino.h>

static const char WEB_PORTAL_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pt-BR"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FitaDigital</title>
<style>
:root{--bg:#12121a;--card:#1e1e2a;--txt:#e8e8f0;--acc:#5b8cff;--bd:#333}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);color:var(--txt);min-height:100vh}
header{padding:12px 16px;background:var(--card);border-bottom:1px solid var(--bd);display:flex;flex-wrap:wrap;align-items:center;gap:12px}
h1{font-size:1.1rem;margin:0}
nav{display:flex;gap:8px;flex-wrap:wrap}
nav button{background:#2a2a3a;border:1px solid var(--bd);color:var(--txt);padding:8px 14px;border-radius:6px;cursor:pointer}
nav button.on{background:var(--acc);border-color:var(--acc);color:#fff}
#ip{font-size:12px;opacity:.7}
main{padding:16px;max-width:900px;margin:0 auto}
.panel{display:none}.panel.on{display:block}
.card{background:var(--card);border:1px solid var(--bd);border-radius:8px;padding:16px;margin-bottom:16px}
label{display:block;margin:10px 0 4px;font-size:13px;opacity:.9}
input,select{width:100%;max-width:420px;padding:8px;border-radius:6px;border:1px solid var(--bd);background:#0d0d12;color:var(--txt)}
button.btn{background:var(--acc);color:#fff;border:none;padding:10px 18px;border-radius:6px;cursor:pointer;margin-top:8px}
button.btn2{background:#3a3a4a;color:var(--txt)}
#logBox{width:100%;height:280px;font-family:ui-monospace,monospace;font-size:12px;background:#0a0a0f;color:#b4e0b4;padding:10px;border:1px solid var(--bd);border-radius:6px;overflow:auto;white-space:pre-wrap}
#fsPath{max-width:100%}
table{width:100%;border-collapse:collapse;font-size:14px}
th,td{padding:8px;border-bottom:1px solid var(--bd);text-align:left}
th{opacity:.8}a{color:var(--acc)}
.msg{padding:10px;border-radius:6px;margin:8px 0;font-size:14px}
.msg.ok{background:#1a3d2a}.msg.err{background:#3d1a1a}
h2{font-size:1rem;margin:0 0 12px}
</style></head>
<body>
<header><h1>FitaDigital</h1><nav>
<button type="button" class="on" data-p="cfg">Config</button>
<button type="button" data-p="log">Logs</button>
<button type="button" data-p="fs">Ficheiros</button>
</nav><span id="ip"></span></header>
<main>
<section id="p-cfg" class="panel on"><h2>Configurações</h2>
<div id="cfgMsg"></div>
<div class="card"><h2>Wi‑Fi</h2>
<label>SSID</label><input id="wSsid" autocomplete="off">
<label>Palavra-passe (deixe em branco para não alterar)</label><input id="wPass" type="password" autocomplete="new-password">
</div>
<div class="card"><h2>Interface</h2>
<label>Tamanho da fonte (0–3)</label><input id="uFont" type="number" min="0" max="3">
</div>
<div class="card"><h2>FTP</h2>
<label>Utilizador</label><input id="fUser">
<label>Palavra-passe</label><input id="fPass" type="password">
</div>
<div class="card"><h2>Data / hora</h2>
<label><input type="checkbox" id="nEn"> NTP activo</label>
<label>Servidor NTP</label><input id="nSrv">
<label>Fuso (UTC+horas)</label><select id="nTz"></select>
</div>
<div class="card"><h2>WireGuard</h2>
<label><input type="checkbox" id="gEn"> Activo</label>
<label>IP local</label><input id="gIp">
<label>Chave privada</label><input id="gPk">
<label>Chave pública do peer</label><input id="gPub">
<label>Endpoint</label><input id="gEp">
<label>Porta</label><input id="gPt" type="number">
</div>
<button class="btn" type="button" id="btnSave">Guardar</button>
</section>
<section id="p-log" class="panel"><h2>Consola de logs</h2>
<p style="font-size:13px;opacity:.8">Tempo real via WebSocket. O ficheiro no SD é <code>/fdigi.log</code>.</p>
<button class="btn btn2" type="button" id="btnRefLog">Recarregar</button>
<button class="btn btn2" type="button" id="btnClrLog">Limpar log</button>
<div id="logBox"></div>
</section>
<section id="p-fs" class="panel"><h2>Explorador (cartão SD)</h2>
<div class="card">
<label>Caminho</label><input id="fsPath" value="/" placeholder="/">
<button class="btn btn2" type="button" id="btnFsGo">Abrir</button>
<table><thead><tr><th>Nome</th><th>Tam.</th><th></th></tr></thead><tbody id="fsBody"></tbody></table>
</div>
</section>
</main>
<script>
(function(){
var ws=null,logBox=document.getElementById("logBox");
function $(id){return document.getElementById(id);}
function showPanel(id){
document.querySelectorAll(".panel").forEach(function(p){p.classList.remove("on");});
document.querySelectorAll("nav button").forEach(function(b){b.classList.remove("on");});
$("p-"+id).classList.add("on");
document.querySelector('nav button[data-p="'+id+'"]').classList.add("on");
if(id==="log") connectLog();
else disconnectLog();
if(id==="fs") loadFs();
}
document.querySelectorAll("nav button").forEach(function(b){
b.onclick=function(){showPanel(b.getAttribute("data-p"));};
});
/* So' enviar Content-Type em POST: GET com esse header dispara preflight CORS (OPTIONS) e o AsyncWebServer nao responde -> "Failed to fetch". */
function api(m,u,b){
var opt={method:m};
if(b!==undefined){opt.headers={"Content-Type":"application/json"};opt.body=JSON.stringify(b);}
return fetch(u,opt).then(function(r){if(!r.ok)throw new Error(r.status+" "+r.statusText);return r.json();});
}
function loadCfg(){
api("GET","/api/settings").then(function(d){
$("wSsid").value=d.wifi.ssid||"";
$("uFont").value=d.fontIndex;
$("fUser").value=d.ftp.user||"";
$("fPass").value="";
$("nEn").checked=!!d.ntp.enabled;
$("nSrv").value=d.ntp.server||"";
$("nTz").value=String(d.tzOffsetSec/3600);
$("gEn").checked=!!d.wireguard.enabled;
$("gIp").value=d.wireguard.localIp||"";
$("gPk").value=d.wireguard.privateKey||"";
$("gPub").value=d.wireguard.peerPublicKey||"";
$("gEp").value=d.wireguard.endpoint||"";
$("gPt").value=d.wireguard.port||51820;
$("ip").textContent=d.status.ip?("IP: "+d.status.ip):"";
}).catch(function(e){$("cfgMsg").innerHTML='<div class="msg err">'+e+'</div>';});
}
var tz=$("nTz");
for(var h=-12;h<=14;h++){
var o=document.createElement("option");
o.value=String(h);o.textContent="UTC"+(h>=0?"+":"")+h;
if(h===0)o.selected=true;tz.appendChild(o);
}
$("btnSave").onclick=function(){
var body={
wifi:{ssid:$("wSsid").value.trim(),password:$("wPass").value},
fontIndex:parseInt($("uFont").value,10)||0,
ftp:{user:$("fUser").value.trim(),password:$("fPass").value},
ntp:{enabled:$("nEn").checked,server:$("nSrv").value.trim()},
tzOffsetSec:parseInt($("nTz").value,10)*3600,
wireguard:{
enabled:$("gEn").checked,
localIp:$("gIp").value.trim(),
privateKey:$("gPk").value,
peerPublicKey:$("gPub").value,
endpoint:$("gEp").value.trim(),
port:parseInt($("gPt").value,10)||51820
}
};
$("cfgMsg").innerHTML="";
api("POST","/api/settings",body).then(function(){
$("cfgMsg").innerHTML='<div class="msg ok">Guardado.</div>';
$("wPass").value="";
loadCfg();
}).catch(function(e){$("cfgMsg").innerHTML='<div class="msg err">'+e+'</div>';});
};
function connectLog(){
disconnectLog();
var wsProto=(location.protocol==="https:")?"wss://":"ws://";
try{ws=new WebSocket(wsProto+location.host+"/ws/logs");}catch(e){logBox.textContent="WebSocket: "+e;return;}
ws.onmessage=function(ev){logBox.textContent+=ev.data+"\n";logBox.scrollTop=logBox.scrollHeight;};
ws.onerror=function(){logBox.textContent+="[WS erro de ligacao — veja se abriu http://"+location.hostname+"]\n";};
ws.onclose=function(ev){if(ev.code!==1000)logBox.textContent+="[WS fechado code="+ev.code+"]\n";};
ws.onopen=function(){
fetch("/api/logs/tail").then(function(r){if(!r.ok)throw new Error(r.status);return r.json();}).then(function(d){
logBox.textContent=(d.text||"")+(d.note?("\n-- "+d.note):"");logBox.scrollTop=logBox.scrollHeight;
}).catch(function(e){logBox.textContent="GET /api/logs/tail: "+e+"\n";});
};
}
function disconnectLog(){if(ws){try{ws.close();}catch(e){}ws=null;}}
$("btnRefLog").onclick=function(){fetch("/api/logs/tail").then(function(r){if(!r.ok)throw new Error(r.status);return r.json();}).then(function(d){logBox.textContent=(d.text||"")+(d.note?("\n-- "+d.note):"");}).catch(function(e){logBox.textContent="Erro: "+e;});};
$("btnClrLog").onclick=function(){if(!confirm("Limpar fdigi.log?"))return;
fetch("/api/logs",{method:"DELETE"}).then(function(){logBox.textContent="(limpo)\n";});};
function loadFs(){
var p=$("fsPath").value||"/";
fetch("/api/fs/list?path="+encodeURIComponent(p)).then(function(r){return r.json();}).then(function(d){
var tb=$("fsBody");tb.innerHTML="";
(d.entries||[]).forEach(function(e){
var tr=document.createElement("tr");
var td1=document.createElement("td");
if(e.dir){var a=document.createElement("a");a.href="#";a.textContent=e.name+"/";
a.onclick=function(ev){ev.preventDefault();$("fsPath").value=p.replace(/\/$/,"")+"/"+e.name;loadFs();};
td1.appendChild(a);}else{td1.textContent=e.name;}
var td2=document.createElement("td");td2.textContent=e.dir?"-":String(e.size);
var td3=document.createElement("td");
if(!e.dir){var dl=document.createElement("a");dl.textContent="Descarregar";
dl.href="/api/fs/file?path="+encodeURIComponent(p.replace(/\/$/,"")+"/"+e.name);dl.target="_blank";td3.appendChild(dl);}
tr.appendChild(td1);tr.appendChild(td2);tr.appendChild(td3);tb.appendChild(tr);
});
}).catch(function(e){$("fsBody").innerHTML="<tr><td colspan='3'>Erro: "+(e&&e.message?e.message:String(e))+"</td></tr>";});
}
$("btnFsGo").onclick=loadFs;
loadCfg();
})();
</script>
</body></html>)rawliteral";

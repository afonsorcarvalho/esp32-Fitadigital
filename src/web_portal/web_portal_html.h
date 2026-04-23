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
#logo{height:32px;width:auto}
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
<header><img id="logo" src="data:image/png;base64,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" alt="AFR Logo"><h1>FitaDigital</h1><nav>
<button type="button" class="on" data-p="cfg">Config</button>
<button type="button" data-p="log">Logs</button>
<button type="button" data-p="fs">Ficheiros</button>
<button type="button" data-p="ota">OTA</button>
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
<section id="p-ota" class="panel"><h2>Atualizar Firmware</h2>
<div id="otaMsg"></div>
<div class="card">
<label>Ficheiro .bin</label><input type="file" id="fw-file" accept=".bin">
<button class="btn" type="button" id="fw-btn" onclick="doUpload()">Enviar e Flashar</button>
<div id="fw-prog" style="display:none;margin-top:12px">
<progress id="fw-bar" value="0" max="100" style="width:100%;height:20px"></progress>
<span id="fw-pct" style="display:inline-block;margin-top:8px">0%</span>
</div>
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
function doUpload(){
var f=$("fw-file").files[0];
if(!f){$("otaMsg").innerHTML='<div class="msg err">Selecione um ficheiro .bin</div>';return;}
var form=new FormData();form.append("firmware",f);
var xhr=new XMLHttpRequest();
xhr.open("POST","/api/ota/upload");
xhr.upload.onprogress=function(e){
if(e.lengthComputable){
var pct=Math.round(e.loaded*100/e.total);
$("fw-bar").value=pct;
$("fw-pct").textContent=pct+"%";
}
};
xhr.onload=function(){
try{var d=JSON.parse(xhr.responseText);
if(d.ok){$("otaMsg").innerHTML='<div class="msg ok">Flash OK! A reiniciar...</div>';}
else{$("otaMsg").innerHTML='<div class="msg err">Erro: '+(d.error||"desconhecido")+'</div>';}
}catch(e){$("otaMsg").innerHTML='<div class="msg ok">Flash OK! (device reiniciou)</div>';}
};
xhr.onerror=function(){$("otaMsg").innerHTML='<div class="msg ok">Flash OK! (device reiniciou)</div>';};
$("fw-prog").style.display="";
$("fw-btn").disabled=true;
xhr.send(form);
}
loadCfg();
})();
</script>
</body></html>)rawliteral";

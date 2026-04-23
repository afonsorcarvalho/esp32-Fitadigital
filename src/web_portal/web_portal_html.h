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
<header><img id="logo" src="data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiIHN0YW5kYWxvbmU9Im5vIj8+CjxzdmcKICAgd2lkdGg9IjEwNy4zMTkxNW1tIgogICBoZWlnaHQ9IjMzLjEyNjc1OW1tIgogICB2aWV3Qm94PSIwIDAgMTA3LjMxOTE1IDMzLjEyNjc1OSIKICAgdmVyc2lvbj0iMS4xIgogICBpZD0ic3ZnODQyIgogICBpbmtzY2FwZTp2ZXJzaW9uPSIxLjEtZGV2ICgyNTI1YTRkLCAyMDIwLTA5LTE1KSIKICAgc29kaXBvZGk6ZG9jbmFtZT0iQUZSIExPR08uc3ZnIgogICB4bWxuczppbmtzY2FwZT0iaHR0cDovL3d3dy5pbmtzY2FwZS5vcmcvbmFtZXNwYWNlcy9pbmtzY2FwZSIKICAgeG1sbnM6c29kaXBvZGk9Imh0dHA6Ly9zb2RpcG9kaS5zb3VyY2Vmb3JnZS5uZXQvRFREL3NvZGlwb2RpLTAuZHRkIgogICB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciCiAgIHhtbG5zOnN2Zz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciPgogIDxkZWZzCiAgICAgaWQ9ImRlZnM4MzgiIC8+CiAgPHNvZGlwb2RpOm5hbWVkdmlldwogICAgIGlkPSJiYXNlIgogICAgIHBhZ2Vjb2xvcj0iI2ZmZmZmZiIKICAgICBib3JkZXJjb2xvcj0iIzY2NjY2NiIKICAgICBib3JkZXJvcGFjaXR5PSIxLjAiCiAgICAgaW5rc2NhcGU6cGFnZW9wYWNpdHk9IjAuMCIKICAgICBpbmtzY2FwZTpwYWdlc2hhZG93PSIyIgogICAgIGlua3NjYXBlOnpvb209IjEuMjgiCiAgICAgaW5rc2NhcGU6Y3g9IjE1OS43NjU2MyIKICAgICBpbmtzY2FwZTpjeT0iNDUuNzAzMTI1IgogICAgIGlua3NjYXBlOmRvY3VtZW50LXVuaXRzPSJtbSIKICAgICBpbmtzY2FwZTpjdXJyZW50LWxheWVyPSJsYXllcjEiCiAgICAgc2hvd2dyaWQ9ImZhbHNlIgogICAgIGlua3NjYXBlOndpbmRvdy13aWR0aD0iMjU2MCIKICAgICBpbmtzY2FwZTp3aW5kb3ctaGVpZ2h0PSIxMDE3IgogICAgIGlua3NjYXBlOndpbmRvdy14PSItOCIKICAgICBpbmtzY2FwZTp3aW5kb3cteT0iLTgiCiAgICAgaW5rc2NhcGU6d2luZG93LW1heGltaXplZD0iMSIKICAgICBsb2NrLW1hcmdpbnM9InRydWUiCiAgICAgZml0LW1hcmdpbi10b3A9IjAiCiAgICAgZml0LW1hcmdpbi1sZWZ0PSIwIgogICAgIGZpdC1tYXJnaW4tcmlnaHQ9IjAiCiAgICAgZml0LW1hcmdpbi1ib3R0b209IjAiIC8+CiAgPGcKICAgICBpbmtzY2FwZTpsYWJlbD0iQ2FtYWRhIDEiCiAgICAgaW5rc2NhcGU6Z3JvdXBtb2RlPSJsYXllciIKICAgICBpZD0ibGF5ZXIxIgogICAgIHRyYW5zZm9ybT0idHJhbnNsYXRlKC00NS4xNTg1MzUsLTEzLjI5NjYyMykiPgogICAgPHBhdGgKICAgICAgIHN0eWxlPSJmaWxsOiMwMDgwMDA7c3Ryb2tlLXdpZHRoOjAuMjQ4ODc2IgogICAgICAgZD0ibSA5Ny43MjQ4OTgsNDUuOTk3MDAzIHYgLTAuNDAwMjkgbCAwLjYyMjE4NywwLjE1NjE2IGMgMS4wNDAwMjQsMC4yNjEwMyAwLjUxMDEyLC0wLjYxMzg5IC0wLjU4MTY4NSwtMC45NjA0MSBsIC0wLjU1ODQ3OCwtMC4xNzcyNSAwLjA3MjMzLC0xLjkwODgyIDAuMDcyMzMsLTEuOTA4ODEgaCAxLjM2ODgyMyAxLjM2ODgyNSBsIDAuMDc5NSwwLjU1OTk3IDAuMDc5NSwwLjU1OTk3IGggLTAuMzkwNjI2IGMgLTAuMjE0ODM5LDAgLTAuMzkwNjIyLC0wLjE2Nzk5IC0wLjM5MDYyMiwtMC4zNzMzMSB2IC0wLjM3MzMyIGggLTAuNzQ2NjI3IC0wLjc0NjYzNiB2IDEuNDkzMjYgMS40OTMyNiBoIDAuNzQ2NjM2IDAuNzQ2NjI3IHYgLTAuNTE4NDkgYyAwLC0wLjY2NTY2IDAuNTk1Njk4LC0wLjQ5MjY3IDAuNjk4NTY4LDAuMjAyODYgMC4wNDU2LDAuMzA4MTYgLTAuMTM3NDUsMC42NjYyMiAtMC40NTM4MTIsMC44ODc4MSBsIC0wLjUzMDE5MSwwLjM3MTM2IDAuMjY3MTU2LDAuMTY1MTEgYyAwLjE0NjkzLDAuMDkwOCAwLjI2NzE1MiwwLjM4MjQ5IDAuMjY3MTUyLDAuNjQ4MTcgdiAwLjQ4MzA3IGggLTAuOTk1NSAtMC45OTU1MDkgeiBtIDMzLjY4NjkzMiwwLjAyMzMgYyAtMC4wODUxLC0wLjIyMTc0IC0wLjAyODMsLTAuNTI5NiAwLjEyNjI4LC0wLjY4NDE0IGwgMC4yODEsLTAuMjgwOTkgMC4zMjg0NywwLjQ0OTIyIGMgMC4zODAxLDAuNTE5ODEgMS4yNTA4NywwLjQ0NDEzIDEuMzU5MTQsLTAuMTE4MTIgbCAwLjA3MzgsLTAuMzgzMDYgLTEuMDY2NDEsLTAuMjM2MjggLTEuMDY2NDIsLTAuMjM2MjYgdiAtMS44NjY1NyAtMS44NjY1NyBsIDEuNDM5MzEsLTAuMDc0OSAxLjQzOTMsLTAuMDc0OSAtMC4wNzA1LDIuODEyNTIgLTAuMDcwNSwyLjgxMjUzIC0xLjMwOTM4LDAuMDc1MyAtMS4zMDkzOCwwLjA3NTMgLTAuMTU0NzEsLTAuNDAzMTQgeiBtIDIuMTUxMjcsLTMuMzU2MiB2IC0xLjQ5MzI2IGggLTAuNzQ2NjIgLTAuNzQ2NjMgdiAxLjQ5MzI2IDEuNDkzMjYgaCAwLjc0NjYzIDAuNzQ2NjIgeiBtIC01MC4yNTk1NDEsMS44ODI4MyBjIC0wLjM3MDEzOCwtMC40NDU5OSAtMC4yNDU3NjQsLTEuOTMzNTQgMC4xNzMxNjEsLTIuMDcxMDYgbCAwLjMxMTEwMSwtMC4xMDIxMyB2IDAuODkxODEgMC44OTE4MSBoIDAuODcxMDY4IDAuODcxMDY3IHYgLTAuODE4MjEgLTAuODE4MiBsIC0xLjI0NDM4OSwtMS4yMjU2NyAtMS4yNDQzODIsLTEuMjI1NjcgdiAtMC43Njg0MyBjIDAsLTEuMjIyNDUgMS4zMzQ3MTYsLTEuNzkyOTYgMi44NDE3MjIsLTEuMjE0NjcgbCAwLjM5MzY3NywwLjE1MTA3IHYgMC45Njg4OCAwLjk2ODg4IEggODUuOTAzMjcgODUuNTI5OTU2IHYgLTAuNzQ2NjMgLTAuNzQ2NjMgaCAtMC44NzEwNjcgLTAuODcxMDY4IHYgMC41NjkzMyAwLjU2OTMyIGwgMS4yNDQzODEsMS4yMjU2NyAxLjI0NDM4MiwxLjIyNTY4IHYgMS4xNjQ4OSAxLjE2NDg5IGwgLTAuMzkzNjc3LDAuMTUxMDYgYyAtMC43MjYyODYsMC4yNzg3MSAtMi4yODAwNzcsMC4xNTQ2MiAtMi41NzkzNDgsLTAuMjA1OTkgeiBtIDQuNTI4NDk4LDAuMTkyMjMgLTAuMzExMDk1LC0wLjEyNTUzIHYgLTEuOTcwMjcgLTEuOTcwMjcgaCAxLjM0MjE5MSAxLjM0MjE5NyBsIDAuMTUxMDY1LDAuMzkzNjcgYyAwLjA4MzA4LDAuMjE2NTIgMC4xNTEwNjQsMS4wNTI1NCAwLjE1MTA2NCwxLjg1NzgxIHYgMS40NjQxNSBsIC0wLjQ4MTM4NiwwLjI1NzY0IGMgLTAuNDk2MTM1LDAuMjY1NTIgLTEuNjQ1NTA4LDAuMzE0MTQgLTIuMTk0MDM2LDAuMDkyOCB6IG0gMS42Nzk5MTQsLTIuMDc1MDYgdiAtMS40OTMyNiBIIDg4Ljg4OTc4NSA4OC4yNjc1OSB2IDEuNDkzMjYgMS40OTMyNiBoIDAuNjIyMTk1IDAuNjIyMTg2IHogbSAxLjk5MTAxNywtMS4yNDQzOCB2IC0zLjUyNTc1IGwgMC4zMTEwOTIsMC4xMDM3IDAuMzExMSwwLjEwMzcgdiAzLjMxODM1IDMuMzE4MzUgbCAtMC4zMTExLDAuMTAzNyAtMC4zMTEwOTIsMC4xMDM3IHogbSAyLjA1MzIyNywzLjMwMjcyIC0wLjMxMTA5NCwtMC4xMDg4MSB2IC0xLjk3MDI3IC0xLjk3MDI3IGggMC4zNzMzMTMgMC4zNzMzMTQgdiAxLjc0MjEzIDEuNzQyMTQgaCAwLjc0NjYzNSAwLjc0NjYyOCB2IC0xLjc0MjE0IC0xLjc0MjEzIGggMC4zODMzNzkgMC4zODMzODIgbCAtMC4wNzIyOSwyLjAxMzg0IC0wLjA3MjI4LDIuMDEzODQgLTEuMTE5OTQ3LDAuMDY1MiBjIC0wLjYxNTk2NywwLjAzNTkgLTEuMjU5OTM3LDAuMDE2MyAtMS40MzEwNDEsLTAuMDQzNiB6IG0gNy45NjQwNDUsMC4wMTY3IC0wLjMxMTA5LC0wLjEyNTUzIHYgLTEuOTgyNjkgLTEuOTgyNyBsIDEuNDMxMDQsMC4wNzQ2IDEuNDMxMDMsMC4wNzQ2IDAuMDc0MSwxLjcwMjMyIDAuMDc0MSwxLjcwMjMyIC0wLjI5MTIzLDAuMzUwOTEgYyAtMC4yNzg2NywwLjMzNTc3IC0xLjc1NDM4LDAuNDQ5ODEgLTIuNDA3OTYsMC4xODYwOSB6IG0gMS45Mjg4LC0yLjA3NTA2IHYgLTEuNDkzMjYgaCAtMC43NDY2MyAtMC43NDY2MyB2IDEuNDkzMjYgMS40OTMyNiBoIDAuNzQ2NjMgMC43NDY2MyB6IG0gMi4wNTMyMiwyLjA3NTA2IC0wLjMxMTA5LC0wLjEyNTUzIHYgLTEuOTcwMjcgLTEuOTcwMjggaCAxLjM4MzEzIDEuMzgzMTMgbCAtMC4wNzY1LDEuMDU3NzMgLTAuMDc2NSwxLjA1NzczIC0wLjkzMzI4LDAuMDc3MiAtMC45MzMyOSwwLjA3NzIgdiAwLjYwNzE3IDAuNjA3MTcgaCAwLjU5NTU2IGMgMC4zMjc1NiwwIDAuNjYwMDMsLTAuMTY3OTkgMC43Mzg4MiwtMC4zNzMzMiAwLjIyNDExLC0wLjU4NDAyIDAuNjU2NjMsLTAuNDM3NjIgMC42NTY2MywwLjIyMjI1IDAsMC43OTM5NyAtMS4zMDExOSwxLjE4Njk2IC0yLjQyNjU1LDAuNzMyODcgeiBtIDEuNjc5OTIsLTMuMDcwNTcgdiAtMC40OTc3NSBoIC0wLjYyMjE5IC0wLjYyMjE5IHYgMC40OTc3NSAwLjQ5Nzc1IGggMC42MjIxOSAwLjYyMjE5IHogbSAyLjAwMDk0LDIuODc0MDQgYyAtMC4zNTgxLC0wLjQzMTQ5IC0wLjI1ODYzLC0xLjE3MzcyIDAuMTc2NzMsLTEuMzE4NjggMC4xODI0OCwtMC4wNjA4IDAuMzExMSwwLjExMDc5IDAuMzExMSwwLjQxNDkxIHYgMC41MTg1IGggMC43NDY2MyAwLjc0NjYyIHYgLTAuMzI1NDEgYyAwLC0wLjE3ODk3IC0wLjUxMTM3LC0wLjY4NTg2IC0xLjEzNjQsLTEuMTI2NDIgbCAtMS4xMzY0LC0wLjgwMTAxIDAuMDc4NywtMC41NTM1IDAuMDc4NywtMC41NTM1IGggMS4zNjg4MyAxLjM2ODgyIGwgMC4wNzk1LDAuNTU5OTggMC4wNzk1LDAuNTU5OTcgaCAtMC4zOTA2MyBjIC0wLjIxNDg0LDAgLTAuMzkwNjMsLTAuMTY3OTkgLTAuMzkwNjMsLTAuMzczMzIgdiAtMC4zNzMzMSBoIC0wLjc0NjYyIGMgLTEuMDQxMTMsMCAtMC45MzI2OSwwLjM3Njg2IDAuMzczMzEsMS4yOTc0MiBsIDEuMTE5OTUsMC43ODk0MSB2IDAuNTI0NSBjIDAsMS4wNjM2NSAtMi4wMTA4NCwxLjYyNDI1IC0yLjcyNzcyLDAuNzYwNDYgeiBtIDUuODMwNzUsMC4wNTcyIGMgLTAuMDcyOSwtMC4xOTAwOCAtMC4xMDEsLTEuNzM5MzEgLTAuMDYyNCwtMy40NDI3NSBsIDAuMDcwMywtMy4wOTcxNSBoIDAuMzczMzEgMC4zNzMzMSB2IDMuMzM5MSAzLjMzOTA5IGwgLTAuMzEwOTYsMC4xMDM2NSBjIC0wLjE3MTAzLDAuMDU3IC0wLjM3MDY1LC0wLjA1MTkgLTAuNDQzNTksLTAuMjQxOTQgeiBtIDIuMDQ1NiwwLjE0MzQ3IGMgLTAuMDk0MSwtMC4wOTQxIC0wLjE3MTExLC0xLjAzNzU4IC0wLjE3MTExLC0yLjA5NjYxIHYgLTEuOTI1NTEgbCAxLjI0NTg3LC0wLjA4NjIgYyAxLjc2MTI4LC0wLjEyMTkxIDEuNzQwNjYsLTAuMTQ3OTIgMS43NDA2NiwyLjE5NTE0IHYgMi4xMTU0NCBsIC0wLjMxMTEsLTAuMTAyODYgLTAuMzExMSwtMC4xMDI4NyAtMC4wNzMxLC0xLjc4NDQ0IC0wLjA3MzEsLTEuNzg0NDUgaCAtMC42MDAzOCAtMC42MDAzOSBsIC0wLjA3MzEsMS43ODQ0NSBjIC0wLjA2NjgsMS42MzEwOCAtMC4zMjczOSwyLjIzMzY3IC0wLjc3MzA5LDEuNzg3OTYgeiBtIDQuMzY3OTgsLTAuMTI4MzggYyAtMC4xNzg5LC0wLjE3ODkgLTAuMzA4MTgsLTAuOTU3MjQgLTAuMzA4MTgsLTEuODU1MzcgdiAtMS41NDcxOCBsIC0wLjMxMTExLC0wLjEyNjIzIC0wLjMxMTA5LC0wLjEyNjIzIDAuMjg2MDEsLTAuMjA1NjEgYyAwLjE1NzMsLTAuMTEzMDggMC4zMjUyOSwtMC40NzY4MSAwLjM3MzMxLC0wLjgwODMgMC4wNDgsLTAuMzMxNDggMC4yMjczMSwtMC42NDg4IDAuMzk4NDEsLTAuNzA1MTUgbCAwLjMxMTA5LC0wLjEwMjQ1IHYgMC42NjYyMyAwLjY2NjIzIGwgMC40OTc3NiwwLjEzMDE2IGMgMC4yNzM3NiwwLjA3MTYgMC40OTc3NSwwLjIyOTEgMC40OTc3NSwwLjM1MDAyIDAsMC4xMjA5MSAtMC4yMjM5OSwwLjIxOTg1IC0wLjQ5Nzc1LDAuMjE5ODUgaCAtMC40OTc3NiB2IDEuNDkzMjYgMS40OTMyNiBoIDAuNTE4NSBjIDAuNjQ2MDEsMCAwLjUwMjU3LDAuNTk0IC0wLjE2NzU4LDAuNjkzOTQgLTAuMjY0NjQsMC4wMzk1IC0wLjYxOTg1LC0wLjA2NjkgLTAuNzg5MzYsLTAuMjM2NDMgeiBtIDIuNDkxNjcsMC4xMjQyOSAtMC4zMTEwOSwtMC4xMjU1MyB2IC0xLjk4MjY5IC0xLjk4MjcgbCAxLjQzMTA0LDAuMDc0NiAxLjQzMTA0LDAuMDc0NiB2IDAuOTk1NTEgMC45OTU1MSBsIC0xLjA1NzczLDAuMDc2NSAtMS4wNTc3MywwLjA3NjUgdiAwLjYwNzg4IDAuNjA3ODggaCAwLjc0NjY0IDAuNzQ2NjMgdiAtMC4zNzMzMiBjIDAsLTAuMjA1MzIgMC4xNjc5OSwtMC4zNzMzMSAwLjM3MzMxLC0wLjM3MzMxIDAuNDQ2NTgsMCAwLjQ5NDk4LDAuNzc0MjkgMC4wNzQ3LDEuMTk0NjEgLTAuMjk0NjksMC4yOTQ2OCAtMS43NzE5NywwLjM3Nzg2IC0yLjM3Njc3LDAuMTMzODIgeiBtIDEuOTI4OCwtMy4wNzA1NyB2IC0wLjQ5Nzc1IGggLTAuNzQ2NjMgLTAuNzQ2NjQgdiAwLjQ5Nzc1IDAuNDk3NzUgaCAwLjc0NjY0IDAuNzQ2NjMgeiBtIDEuOTA4MDUsMy4wNjk0OCBjIC0wLjA5MTMsLTAuMDkxMyAtMC4xNjU5MiwtMS42NTkxOCAtMC4xNjU5MiwtMy40ODQyNyB2IC0zLjMxODM1IGggMC4zNzMzMSAwLjM3MzMyIHYgMy40ODQyNyBjIDAsMy4xODQ2NSAtMC4xMDY0LDMuNzkyNjYgLTAuNTgwNzEsMy4zMTgzNSB6IG0gMS44MjUxLC0xLjkyODggdiAtMi4xMzYxOSBoIDAuMzgzMzcgMC4zODMzOCBsIC0wLjA3MjMsMi4wMzMyIC0wLjA3MjMsMi4wMzMyIC0wLjMxMTA5LDAuMTAyOTkgLTAuMzExMSwwLjEwMjk5IHogbSA2LjAzNTI1LDEuOTI5ODkgLTAuMzExMSwtMC4xMjU1MyB2IC0xLjk3MDI3IC0xLjk3MDI4IGggMS4zODMxMyAxLjM4MzE0IGwgLTAuMDc2NSwxLjA1NzczIC0wLjA3NjUsMS4wNTc3MyAtMC45MzMyOSwwLjA3NzIgLTAuOTMzMjksMC4wNzcyIHYgMC42MDcxNyAwLjYwNzE3IGggMC41OTU1NiBjIDAuMzI3NTcsMCAwLjY2MDAzLC0wLjE2Nzk5IDAuNzM4ODIsLTAuMzczMzIgMC4yMjQxMSwtMC41ODQwMiAwLjY1NjYzLC0wLjQzNzYyIDAuNjU2NjMsMC4yMjIyNSAwLDAuNzkzOTcgLTEuMzAxMTksMS4xODY5NiAtMi40MjY1NCwwLjczMjg3IHogbSAxLjY3OTkxLC0zLjA3MDU3IHYgLTAuNDk3NzUgaCAtMC42MjIxOSAtMC42MjIxOSB2IDAuNDk3NzUgMC40OTc3NSBoIDAuNjIyMTkgMC42MjIxOSB6IG0gMS45OTEwMSwxLjE2NTc1IHYgLTIuMTExMTMgbCAxLjE4MjE3LC0wLjEwNjg0IGMgMC42NTAxOSwtMC4wNTg4IDEuMzI2NTUsMC4wMDMgMS41MDMwMywwLjEzNzk3IGwgMC4zMjA4OCwwLjI0NDc5IC0wLjA3MiwxLjg5MDIxIC0wLjA3MiwxLjg5MDIxIGggLTAuMzczMzIgLTAuMzczMzEgbCAtMC4wNzMxLC0xLjgwNDM2IC0wLjA3MzEsLTEuODA0MzUgaCAtMC42MDAzOCAtMC42MDAzOCBsIC0wLjA3MzEsMS43ODQ0NSAtMC4wNzMxLDEuNzg0NDQgLTAuMzExMDksMC4xMDI4NyAtMC4zMTExLDAuMTAyODYgeiBtIDQuNTQyLDEuOTA0ODIgLTAuMzExMDksLTAuMTI1NTMgdiAtMS43MjE0IC0xLjcyMTM5IGggLTAuMzczMzIgYyAtMC4yMDUzMywwIC0wLjM3MzMxLC0wLjEwMDAxIC0wLjM3MzMxLC0wLjIyMjI1IDAsLTAuMTIyMjQgMC4xNjc5OCwtMC4yODY3MSAwLjM3MzMxLC0wLjM2NTUxIDAuMjA1MzIsLTAuMDc4OCAwLjM3MzMyLC0wLjQzNjA4IDAuMzczMzIsLTAuNzkzOTkgdiAtMC42NTA3NCBsIDAuMzExMDksMC4xMDIxNSBjIDAuMTcxMSwwLjA1NjIgMC4zNDY1NCwwLjQwMTUgMC4zODk4NSwwLjc2NzM3IDAuMDQ0NCwwLjM3NTA2IDAuMjM0MzYsMC42NjUyMSAwLjQzNTUzLDAuNjY1MjEgMC4xOTYyMywwIDAuMzU2NzgsMC4xMTIgMC4zNTY3OCwwLjI0ODg4IDAsMC4xMzY4OCAtMC4xNjc5OSwwLjI0ODg4IC0wLjM3MzMxLDAuMjQ4ODggaCAtMC4zNzMzMiB2IDEuNDkzMjYgMS40OTMyNiBoIDAuMzczMzIgYyAwLjIwNTMyLDAgMC4zNzMzMSwwLjE2Nzk5IDAuMzczMzEsMC4zNzMzMSAwLDAuMzYzNjggLTAuNTU0OTksMC40NjE1NiAtMS4xODIxNiwwLjIwODQ5IHogbSAyLjE5NjI2LC0wLjE4NjA5IC0wLjI5MTIzLC0wLjM1MDkxIDAuMDc0MSwtMS43MDIzMiAwLjA3NDEsLTEuNzAyMzIgMS40NDU5OCwtMC4wNzUxIDEuNDQ1OTgsLTAuMDc1MSAtMC4wNzcyLDEuMDcwNiAtMC4wNzcyLDEuMDcwNTkgLTEuMDU3NzIsMC4wNzY1IC0xLjA1NzczLDAuMDc2NSB2IDAuNjA3ODggMC42MDc4OCBoIDAuNzQ2NjMgMC43NDY2MyB2IC0wLjM3MzMyIGMgMCwtMC4yMDUzMiAwLjE2Nzk5LC0wLjM3MzMxIDAuMzczMzIsLTAuMzczMzEgaCAwLjM3MzMxIHYgMC41OTU1NiBjIDAsMC45MDQ0NyAtMi4wNzU3NiwxLjMyMTg5IC0yLjcxOTA1LDAuNTQ2NzggeiBtIDEuOTcyNDIsLTIuODg0NDggdiAtMC40OTc3NSBoIC0wLjc0NjYzIC0wLjc0NjYzIHYgMC40OTc3NSAwLjQ5Nzc1IGggMC43NDY2MyAwLjc0NjYzIHogbSAyLjA1MzIzLDMuMDcwNTcgYyAtMC4zNzk2NCwtMC4xNTMxOSAtMC40MTMyOSwtMS4yMzU5IC0wLjA0NTUsLTEuNDYzMjIgMC4xNDYyNiwtMC4wOTA0IDAuMzI3MDYsMC4wNzA4IDAuNDAyMzQsMC4zNTg2MiBsIDAuMTM2NzEsMC41MjI4IGggMC41OTMxNyAwLjU5MzE2IHYgLTAuMzY5NzkgYyAwLC0wLjIwMzM4IC0wLjQ0Nzk3LC0wLjY2NjI0IC0wLjk5NTUxLC0xLjAyODU4IGwgLTAuOTk1NSwtMC42NTg3OSB2IC0wLjcxMzU2IC0wLjcxMzU2IGggMS4zNjg4MiAxLjM2ODgyIHYgMC42MjIyIDAuNjIyMTkgaCAtMC4zNzMzMSBjIC0wLjIwNTMzLDAgLTAuMzczMzIsLTAuMTY3OTkgLTAuMzczMzIsLTAuMzczMzIgdiAtMC4zNzMzMSBoIC0wLjYyMjE5IGMgLTAuOTI1NTIsMCAtMC43NjE1NSwwLjU4ODI2IDAuMzczMzIsMS4zMzkyOCBsIDAuOTk1NSwwLjY1ODc5IHYgMC43MTY0NyBjIDAsMC45MTI2OSAtMS4yMTgyNiwxLjM0MTMzIC0yLjQyNjU0LDAuODUzNzggeiBNIDQ2LjgwOTc0NSw0NC4xODU4NDMgYyAtMi4xMDQwMDksLTEuMjgzMDIgLTEuOTk4Mzc0LC00LjEzNzA5IDAuMjA0Mjk4LC01LjUxOTY1IGwgMC44MTExMjUsLTAuNTA5MTEgMS4wODA4MTEsMC4xMTAxIDEuMDgwODA0LDAuMTEwMTEgMC45NTUyOTgsMC45MzI4MyAwLjk1NTI5OCwwLjkzMjgyIDAuNTc3MSwtMC42NDk4IGMgMC4zMTc0MDEsLTAuMzU3NCAxLjc1MzAzNSwtMS45OTIxMSAzLjE5MDI5NiwtMy42MzI2OSBsIDIuNjEzMjAyLC0yLjk4Mjg5IDQuMTc0ODQ1LC0wLjAxIDQuMTc0ODM5LC0wLjAxIDAuMjE3NjcyLC0wLjU1OTk3IGMgMC4xMTk3MTcsLTAuMzA3OTkgMC41NTI0MjUsLTAuODc2OTQgMC45NjE1NzYsLTEuMjY0MzQgbCAwLjc0MzkwNCwtMC43MDQzNyAwLjk4Mjc2OSwtMC4xMzE4MiAwLjk4Mjc3NiwtMC4xMzE4MiAwLjc5ODEzNywwLjQxMjc0IGMgMS4wNTkxMTUsMC41NDc2OSAxLjc2OTU1NSwxLjc2MzU1IDEuNzcwNywzLjAzMDQzIGwgMC4wMDExLDEuMDI0MTYgLTAuNTE5NzI4LDAuNjYwNzIgYyAtMS42MjU0NjMsMi4wNjY0NSAtNC41NjY5OTQsMS43NjUwNyAtNS41OTg4MjgsLTAuNTczNjMgbCAtMC40NDgxNywtMS4wMTU0NyBIIDYyLjc0OTk5IDU4Ljk4MDQxMiBsIC0yLjY3ODg2OCwzLjA0ODczIGMgLTEuNDczMzc2LDEuNjc2ODEgLTMuMTQzMzQxLDMuNzA2NjUgLTMuNzExMDM4LDQuNTEwNzYgLTAuNTY3NjkxLDAuODA0MTEgLTEuNDM4OTcsMS44OTYwNiAtMS45MzYxNzYsMi40MjY1NSBsIC0wLjkwNDAwNiwwLjk2NDUyIC0xLjA4NzAwMiwtMTBlLTQgYyAtMC42MjQwMTksLTUuNmUtNCAtMS40MTM1MDcsLTAuMjAwMDMgLTEuODUzNTUyLC0wLjQ2ODM3IHogbSAzLjA5MTIwOSwtMC45MjQ2MyBjIDAuMzEyODYsLTAuMjE5MTQgMC42NzcxMjEsLTAuNjgzMjUgMC44MDk0NzUsLTEuMDMxMzYgbCAwLjI0MDYzOSwtMC42MzI5MiAtMC4yNzcwODIsLTAuODM5NTUgYyAtMC41MTkwMjcsLTEuNTcyNjcgLTIuMjM1MzExLC0yLjAwNzQ4IC0zLjQ5MTk3MSwtMC44ODQ2NiBsIC0wLjc3NTQ0MiwwLjY5Mjg2IHYgMC45Nzg1IDAuOTc4NSBsIDAuNjYwOTU5LDAuNTY4NTMgYyAwLjc3OTEyOCwwLjY3MDE3IDIuMDEzNjUzLDAuNzQ0MjkgMi44MzM0MjIsMC4xNzAxIHogbSAyMS41Nzg3OSwtOC4xNzY1IDAuNjEwODc5LC0wLjYxMDg4IHYgLTAuOTAxNTIgLTAuOTAxNTIgbCAtMC43Mjc0ODYsLTAuNzI3NDkgYyAtMC44MTg5OTQsLTAuODE4OTkgLTEuNDQ1MjcsLTAuOTAzNjggLTIuNTMwOTU4LC0wLjM0MjI1IC0xLjM2NDQxOSwwLjcwNTU3IC0xLjU5MzQ5NCwyLjI2NzQxIC0wLjQ5Mzg0OCwzLjM2NzA2IGwgMC43Mjc0ODYsMC43Mjc0OCBoIDAuOTAxNTI0IDAuOTAxNTI1IHogbSAyOS45NzgyOTYsNC42OTExNiB2IC0wLjMzNjM2IGwgMS4xMTk5NSwwLjA0ODcgMS4xMTk5NCwwLjA0ODcgdiAwLjMxMDExIDAuMzEwMSBsIC0xLjExOTk0LC0wLjAyMjUgLTEuMTE5OTUsLTAuMDIyNSB2IC0wLjMzNjM2IHogbSAyOC4xMjMwNSwtMC40NzE1NyBjIDAsLTAuMjA1MzIgMC4xNjc5OCwtMC4zNzMzMSAwLjM3MzMxLC0wLjM3MzMxIDAuMjA1MzIsMCAwLjM3MzMxLDAuMTY3OTkgMC4zNzMzMSwwLjM3MzMxIDAsMC4yMDUzMiAtMC4xNjc5OSwwLjM3MzMxIC0wLjM3MzMxLDAuMzczMzEgLTAuMjA1MzMsMCAtMC4zNzMzMSwtMC4xNjc5OSAtMC4zNzMzMSwtMC4zNzMzMSB6IG0gLTgyLjYzMDc1NiwtMS45Mjg3OSBjIC0wLjQ1MzIwMSwtMC4zMDc5OSAtMS4wMzYzMjQsLTAuOTcxNSAtMS4yOTU4MzQsLTEuNDc0NDcgbCAtMC40NzE4MzgsLTAuOTE0NTEgMC4yNjcsLTAuOTkxNTUgMC4yNjcsLTAuOTkxNTYgMC44NzYyNTgsLTAuNzY5MzYgMC44NzYyNTgsLTAuNzY5MzcgaCAwLjk5OTIwMSBjIDEuMTk1NTA2LDAgMi4yNDY3NzMsMC41MDAyNyAyLjg4MTA3MSwxLjM3MTAzIGwgMC40NjQ5OTgsMC42MzgzMyAzLjMwMzM3NiwtMy43NDIzMiAzLjMwMzM4NCwtMy43NDIzMiBoIDQuMDkwNjc0IDQuMDkwNjggbCAwLjM5MTc4MywtMC43NTc2MyBjIDEuNTA4Nzc1LC0yLjkxNzY1IDYuMDg0NTE1LC0xLjkxODQgNi4wOTAxNDMsMS4zMjk5NiAwLjAwNjEsMy41MjUwNiAtNC4zNzc3MDUsNC40ODY1NCAtNi4wOTU4MjksMS4zMzY5NyBsIC0wLjQ5Nzc1NCwtMC45MTI0NSAtMy44NTc1ODMsMC4wMDcgLTMuODU3NTkxLDAuMDA3IC0yLjUzMjYyMiwyLjkxNyBjIC0yLjkxNTYyMywzLjM1ODE0IC00LjI3NzI4Myw1LjA5NjIgLTQuODE1NjMzLDYuMTQ2OCAtMC44ODQyNjcsMS43MjU2OSAtMi45Njk0MDUsMi4zMzY4MiAtNC40NzcxNDIsMS4zMTIyMSB6IG0gMi43MDUzMTcsLTAuNzkwMTkgYyAxLjM4NDE0NCwtMC43MTU3NiAxLjY0MzE5NCwtMi4zMDc0MyAwLjU1Mjk4NywtMy4zOTc2NCBsIC0wLjcyNzQ4NiwtMC43Mjc0OCBoIC0wLjkwMTUyNCAtMC45MDE1MjQgbCAtMC42MTA4NzksMC42MTA4OCBjIC0xLjc5MTkxNSwxLjc5MTkxIDAuMzM0OTczLDQuNjc5NTUgMi41ODg0MjYsMy41MTQyNCB6IG0gMjEuODQ4NDY5LC04LjM4OTU1IDAuNTg2NTAzLC0wLjU1MDk5IHYgLTAuOTYzNDQgLTAuOTYzNDMgbCAtMC42MTA4NzksLTAuNjEwODggLTAuNjEwODc4LC0wLjYxMDg4IEggNjkuODYyMDUgNjguODU1MjI2IGwgLTAuNDkxNDE3LDAuNDkxNDIgYyAtMS4wMTAyMzIsMS4wMTAyNCAtMC44NDQ2OTEsMi43ODc1MSAwLjMzOTA2MSwzLjY0MDIxIGwgMC41MjU2NzcsMC4zNzg2NyAwLjg0NDUzMSwtMC4xMjk4NCBjIDAuNDY0NDk0LC0wLjA3MTQgMS4xMDg0NjUsLTAuMzc3NzkgMS40MzEwNDIsLTAuNjgwODQgeiBtIDExLjAzOTMxMSwtMi41NTQwNCB2IC05LjMwNzI0IGwgMS43MTMyMjgsLTEuNTE4ODkgMS43MTMyMjUsLTEuNTE4ODkgaCAxLjE0ODg1NyAxLjE0ODg0OSB2IDEwLjgyNjEzIDEwLjgyNjEyIGggLTIuODYyMDc0IC0yLjg2MjA4NSB6IG0gMTMuNDM5MzM0LDYuMzI3NTUgdiAtMi45Nzk2OCBsIC0zLjU0NjQ5MSwtMC4wNjkxIC0zLjU0NjQ4OSwtMC4wNjkxIHYgLTIuMzY0MzMgLTIuMzY0MzIgbCAzLjU1NDMyOCwtMC4wNjkxIDMuNTU0MzI0LC0wLjA2OTEgLTAuMDcwMDUsLTIuOTE3NDIgLTAuMDcwMDYsLTIuOTE3NDIgLTMuNTQ2NDksLTAuMDY5IC0zLjU0NjQ5LC0wLjA2OTEgdiAtMi4zNTc0OSAtMi4zNTc1IGggNC43OTgwNjIgNC43OTgwNjIgbCAxLjU0ODI4OSwxLjU2MjE4IDEuNTQ4MjksMS41NjIxNyB2IDkuMjYzOTUgOS4yNjM5NSBoIC0yLjczNzY0IC0yLjczNzY0MiB6IG0gMTIuNjkyNjk1LC02LjQ1Nzc0IHYgLTkuNDM3NDMgbCAxLjU1NTQ3LC0xLjM4NzU4IDEuNTU1NDksLTEuMzg3NTggMS4xODIxNiwtMTBlLTQgMS4xODIxNywtMTBlLTQgdiAxMC44MjYxMyAxMC44MjYxMiBoIC0yLjczNzY1IC0yLjczNzY0IHogbSAyNC4xNDEwMiwtMS4zODg2OCB2IC0xMC44MjYxMyBoIDIuODYyMDggMi44NjIwNyB2IDEwLjgyNjEzIDEwLjgyNjEyIGggLTIuODYyMDcgLTIuODYyMDggeiBtIDEyLjg3NzQ4LDguNjQ4NDUgYyAtMC43MzYyNSwtMS4xOTc3MSAtMS43MjI5OCwtMi44MTUwNSAtMi4xOTI3NSwtMy41OTQwOCBsIC0wLjg1NDExLC0xLjQxNjQyIC0xLjc0MjE0LC0wLjA3NjggLTEuNzQyMTMsLTAuMDc2OCB2IC0yLjM2NDMzIC0yLjM2NDMzIGwgMy42NzA5MiwtMC4wNjg5IDMuNjcwOTMsLTAuMDY4OSB2IC0yLjM1NzY3IC0yLjM1NzY3IGggLTMuNzMzMTQgLTMuNzMzMTUgdiAtMi4zNjQzMyAtMi4zNjQzMyBoIDQuOTIxOTggNC45MjE5OSBsIDEuNjczMjQsMS42ODYxNiAxLjY3MzI0LDEuNjg2MTcgdiAzLjg1OTEzIDMuODU5MTMgbCAtMS40OTMyNiwxLjQ3NzY3IGMgLTEuODAwMzcsMS43ODE1OSAtMS43OTMxNCwxLjM4OTM5IC0wLjA3NDMsNC4wMjYzNiBsIDEuNDE4OTgsMi4xNzY5MiAwLjEwNDE5LDEuNDQwMzYgMC4xMDQxOCwxLjQ0MDM1IGggLTIuNjI4MDQgLTIuNjI4MDUgeiBNIDQ3LjUzMjgsMzAuODA4MTgzIGMgLTAuNTIzNjA0LC0wLjE4MjMxIC0wLjk3NTI5NCwtMC4zMzMxNyAtMS4wMDM3NDMsLTAuMzM1MjMgLTAuMDI4NDUsLTAuMDAyIC0wLjM0ODQ1NiwtMC41MTAwNyAtMC43MTExMjYsLTEuMTI4OTIgbCAtMC42NTkzOTYsLTEuMTI1MTggMC4xNjEwOCwtMC44NTg2NCBjIDAuMDg4NiwtMC40NzIyNSAwLjUzMzgyNSwtMS4yNTM4NiAwLjk4OTM5LC0xLjczNjkgbCAwLjgyODMxLC0wLjg3ODI2IGggMS4yODk1NyBjIDEuNDY5MTQ5LDAgMi42NDg5NDQsMC41MDgzNSAyLjg4MTY5MSwxLjI0MTY1IDAuMjU4MTE2LDAuODEzMjcgMC40MzkzNzQsMC42NjQxNyAzLjc3NjgyMSwtMy4xMDY4IGwgMy4xOTI1OCwtMy42MDcyOSA0LjExNTAyNywtMC4wMDEgNC4xMTUwMiwtMTBlLTQgMC40MDUzMSwtMC42ODQ0MSBjIDAuNjUxNDE4LC0xLjA5OTk5IDEuNTU3MzU4LC0xLjgxMzYzIDIuNDk1NTk2LC0xLjk2NTg5IGwgMC44NzM5NTgsLTAuMTQxODIgMC44OTk5NDcsMC40MjcwNSBjIDMuMjIwMDM2LDEuNTI4MDMgMi4xNTIwMyw2LjA5ODI0IC0xLjQyNTA4NCw2LjA5ODI0IGggLTEuMTEzMjEgbCAtMC43MzM0NDMsLTAuNjE3MTUgYyAtMC40MDMzOTEsLTAuMzM5NDQgLTAuODczNTQ5LC0wLjk1NTQxIC0xLjA0NDc5MiwtMS4zNjg4MiBsIC0wLjMxMTM0OSwtMC43NTE2NyBIIDYyLjYxNjk0IDU4LjY3ODkyMiBsIC0zLjE1NjIxOCwzLjY3MDkzIGMgLTEuNzM1OTE1LDIuMDE5IC0zLjQ4MzQ3NCw0LjI0OTU3IC0zLjg4MzQ2Miw0Ljk1NjgxIC0xLjEwOTc4NiwxLjk2MjI3IC0yLjMzMjg3MywyLjUzMjY5IC00LjEwNjQ0MiwxLjkxNTE2IHogbSAyLjUzMzQ0NiwtMS40Mzc0OCBjIDAuODE5ODY0LC0wLjgxOTg2IDAuOTkyMzE4LC0xLjY3NTk2IDAuNTIxMTQ0LC0yLjU4NzExIGwgLTAuMzg5MjM1LC0wLjc1MjcgLTAuODcxNDkxLC0wLjI4NzYyIGMgLTIuMjcxNzc2LC0wLjc0OTc1IC0zLjk0NjA0MSwxLjkzMDcgLTIuMjU5NDM5LDMuNjE3MzEgbCAwLjYxMDg3OSwwLjYxMDg4IGggMC44OTM2OTggMC44OTM2OTEgeiBtIDIxLjAxMTA3MSwtNy43Mjc0IGMgMS42NzU0NTUsLTAuODY2NDEgMS4xODQxNjksLTMuNTcxMDcgLTAuNzI0ODIxLC0zLjk5MDM1IGwgLTAuNzM2NTYsLTAuMTYxNzcgLTAuNzMyNDU3LDAuNDMyNjcgYyAtMS4zNzEzNCwwLjgxMDA3IC0xLjYyMjQ5OCwyLjEzNjMyIC0wLjYyODMyLDMuMzE3ODMgbCAwLjY0NDMyMiwwLjc2NTczIGggMC43MzY4NTkgYyAwLjQwNTI4MiwwIDEuMDUzNzIsLTAuMTYzODUgMS40NDA5NzcsLTAuMzY0MTEgeiBtIDQzLjkyNjM3Myw0Ljc2MTk3IGMgLTAuMDY5OCwtMC4xODE5NCAtMC4wOTQzLC0xLjMyOTg4IC0wLjA1NDQsLTIuNTUwOTggbCAwLjA3MjUsLTIuMjIwMiA1LjAzOTc0LC0wLjA2NzQgNS4wMzk3NSwtMC4wNjc0IHYgMi42MTgzNSAyLjYxODM2IGggLTQuOTg1MzQgLTQuOTg1MzQgeiBtIC0wLjEwNjMyLC0xMC43NDQyMSB2IC0yLjM2NDMzIGggNS45NzMwNCA1Ljk3MzAzIHYgMi4zNjQzMyAyLjM2NDMzIGggLTUuOTczMDMgLTUuOTczMDQgeiIKICAgICAgIGlkPSJwYXRoODQyIiAvPgogIDwvZz4KPC9zdmc+Cg==" style="height:32px;width:auto"/><h1>FitaDigital</h1><nav>
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
<button class="btn" type="button" id="fw-btn">Enviar e Flashar</button>
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
$("otaMsg").innerHTML='<div class="msg ok">Enviando '+f.name+' ('+Math.round(f.size/1024)+'KB)...</div>';
$("fw-prog").style.display="";
$("fw-btn").disabled=true;
var form=new FormData();form.append("firmware",f);
var xhr=new XMLHttpRequest();
xhr.open("POST","/api/ota/upload");
xhr.upload.onprogress=function(e){
if(e.lengthComputable){
var pct=Math.round(e.loaded*100/e.total);
$("fw-bar").value=pct;
$("fw-pct").textContent=pct+"%";
if(pct===100)$("otaMsg").innerHTML='<div class="msg ok">Upload concluido, a processar...</div>';
}
};
xhr.onload=function(){
try{var d=JSON.parse(xhr.responseText);
if(d.ok){$("otaMsg").innerHTML='<div class="msg ok">Flash OK! A reiniciar em 2s...</div>';}
else{$("otaMsg").innerHTML='<div class="msg err">Erro servidor: '+(d.error||"desconhecido")+'</div>';$("fw-btn").disabled=false;}
}catch(e){$("otaMsg").innerHTML='<div class="msg ok">Flash OK! Device a reiniciar...</div>';}
};
xhr.onerror=function(){$("otaMsg").innerHTML='<div class="msg ok">Conexao perdida (normal apos reboot). Flash OK!</div>';};
xhr.ontimeout=function(){$("otaMsg").innerHTML='<div class="msg err">Timeout — tente novamente</div>';$("fw-btn").disabled=false;};
xhr.send(form);
}
$("fw-btn").onclick=doUpload;
loadCfg();
})();
</script>
</body></html>)rawliteral";

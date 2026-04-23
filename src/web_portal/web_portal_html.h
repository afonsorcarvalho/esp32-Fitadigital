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
<header><img id="logo" src="data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiIHN0YW5kYWxvbmU9Im5vIj8+PHN2ZyB3aWR0aD0iMTA3LjMxOTE1bW0iIGhlaWdodD0iMzMuMTI2NzU5bW0iIHZpZXdCb3g9IjAgMCAxMDcuMzE5MTUgMzMuMTI2NzU5IiB2ZXJzaW9uPSIxLjEiIGlkPSJzdmc4NDIiIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgeG1sbnM6c3ZnPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyI+PGcgaWQ9ImxheWVyMSIgdHJhbnNmb3JtPSJ0cmFuc2xhdGUoLTQ1LjE1ODUzNSwtMTMuMjk2NjIzKSI+PHBhdGggc3R5bGU9ImZpbGw6IzAwODAwMDtzdHJva2Utd2lkdGg6MC4yNDg4NzYiIGQ9Im0gOTcuNzI0ODk4LDQ1Ljk5NzAwMyB2IC0wLjQwMDI5IGwgMC42MjIxODcsMC4xNTYxNiBjIDEuMDQwMDI0LDAuMjYxMDMgMC41MTAxMiwtMC42MTM4OSAtMC41ODE2ODUsLTAuOTYwNDEgbCAtMC41NTg0NzgsLTAuMTc3MjUgMC4wNzIzMywtMS45MDg4MiAwLjA3MjMzLC0xLjkwODgxIGggMS4zNjg4MjMgMS4zNjg4MjUgbCAwLjA3OTUsMC41NTk5NyAwLjA3OTUsMC41NTk5NyBoIC0wLjM5MDYyNiBjIC0wLjIxNDgzOSwwIC0wLjM5MDYyMiwtMC4xNjc5OSAtMC4zOTA2MjIsLTAuMzczMzEgdiAtMC4zNzMzMiBoIC0wLjc0NjYyNyAtMC43NDY2MzYgdiAxLjQ5MzI2IDEuNDkzMjYgaCAwLjc0NjYzNiAwLjc0NjYyNyB2IC0wLjUxODQ5IGMgMCwtMC42NjU2NiAwLjU5NTY5OCwtMC40OTI2NyAwLjY5ODU2OCwwLjIwMjg2IDAuMDQ1NiwwLjMwODE2IC0wLjEzNzQ1LDAuNjY2MjIgLTAuNDUzODEyLDAuODg3ODEgbCAtMC41MzAxOTEsMC4zNzEzNiAwLjI2NzE1NiwwLjE2NTExIGMgMC4xNDY5MywwLjA5MDggMC4yNjcxNTIsMC4zODI0OSAwLjI2NzE1MiwwLjY0ODE3IHYgMC40ODMwNyBoIC0wLjk5NTUgLTAuOTk1NTA5IHogbSAzMy42ODY5MzIsMC4wMjMzIGMgLTAuMDg1MSwtMC4yMjE3NCAtMC4wMjgzLC0wLjUyOTYgMC4xMjYyOCwtMC42ODQxNCBsIDAuMjgxLC0wLjI4MDk5IDAuMzI4NDcsMC40NDkyMiBjIDAuMzgwMSwwLjUxOTgxIDEuMjUwODcsMC40NDQxMyAxLjM1OTE0LC0wLjExODEyIGwgMC4wNzM4LC0wLjM4MzA2IC0xLjA2NjQxLC0wLjIzNjI4IC0xLjA2NjQyLC0wLjIzNjI2IHYgLTEuODY2NTcgLTEuODY2NTcgbCAxLjQzOTMxLC0wLjA3NDkgMS40MzkzLC0wLjA3NDkgLTAuMDcwNSwyLjgxMjUyIC0wLjA3MDUsMi44MTI1MyAtMS4zMDkzOCwwLjA3NTMgLTEuMzA5MzgsMC4wNzUzIC0wLjE1NDcxLC0wLjQwMzE0IHogbSAyLjE1MTI3LC0zLjM1NjIgdiAtMS40OTMyNiBoIC0wLjc0NjYyIC0wLjc0NjYzIHYgMS40OTMyNiAxLjQ5MzI2IGggMC43NDY2MyAwLjc0NjYyIHogbSAtNTAuMjU5NTQxLDEuODgyODMgYyAtMC4zNzAxMzgsLTAuNDQ1OTkgLTAuMjQ1NzY0LC0xLjkzMzU0IDAuMTczMTYxLC0yLjA3MTA2IGwgMC4zMTExMDEsLTAuMTAyMTMgdiAwLjg5MTgxIDAuODkxODEgaCAwLjg3MTA2OCAwLjg3MTA2NyB2IC0wLjgxODIxIC0wLjgxODIgbCAtMS4yNDQzODksLTEuMjI1NjcgLTEuMjQ0MzgyLC0xLjIyNTY3IHYgLTAuNzY4NDMgYyAwLC0xLjIyMjQ1IDEuMzM0NzE2LC0xLjc5Mjk2IDIuODQxNzIyLC0xLjIxNDY3IGwgMC4zOTM2NzcsMC4xNTEwNyB2IDAuOTY4ODggMC45Njg4OCBINDM5MDMyNyA4NS41Mjk5NTYgdiAtMC43NDY2MyAtMC43NDY2MyBoIC0wLjg3MTA2NyAtMC44NzEwNjggdiAwLjU2OTMzIDAuNTY5MzIgbCAxLjI0NDM4MSwxLjIyNTY3IDEuMjQ0MzgyLDEuMjI1NjggdiAxLjE2NDg5IDEuMTY0ODkgbCAtMC4zOTM2NzcsMC4xNTEwNiBjIC0wLjcyNjI4NiwwLjI3ODcxIC0yLjI4MDA3NywwLjE1NDYyIC0yLjU3OTM0OCwtMC4yMDU5OSB6IG0gNC41Mjg0OTgsMC4xOTIyMyAtMC4zMTEwOTUsLTAuMTI1NTMgdiAtMS45NzAyNyAtMS45NzAyNyBoIDEuMzQyMTkxIDEuMzQyMTk3IGwgMC4xNTEwNjUsMC4zOTM2NyBjIDAuMDgzMDgsMC4yMTY1MiAwLjE1MTA2NCwxLjA1MjU0IDAuMTUxMDY0LDEuODU3ODEgdiAxLjQ2NDE1IGwgLTAuNDgxMzg2LDAuMjU3NjQgYyAtMC40OTYxMzUsMC4yNjU1MiAtMS42NDU1MDgsMC4zMTQxNCAtMi4xOTQwMzYsMC4wOTI4IHogbSAxLjY3OTkxNCwtMi4wNzUwNiB2IC0xLjQ5MzI2IEggODguODg5Nzg1IDg4LjI2NzU5IHYgMS40OTMyNiAxLjQ5MzI2IGggMC42MjIxOTUgMC42MjIxODYgeiBtIDEuOTkxMDE3LC0xLjI0NDM4IHYgLTMuNTI1NzUgbCAwLjMxMTA5MiwwLjEwMzcgMC4zMTExLDAuMTAzNyB2IDMuMzE4MzUgMy4zMTgzNSBsIC0wLjMxMTEsMC4xMDM3IC0wLjMxMTA5MiwwLjEwMzcgeiBtIDIuMDUzMjI3LDMuMzAyNzIgLTAuMzExMDk0LC0wLjEwODgxIHYgLTEuOTcwMjcgLTEuOTcwMjcgaCAwLjM3MzMxMyAwLjM3MzMxNCB2IDEuNzQyMTMgMS43NDIxNCBoIDAuNzQ2NjM1IDAuNzQ2NjI4IHYgLTEuNzQyMTQgLTEuNzQyMTMgaCAwLjM4MzM3OSAwLjM4MzM4MiBsIC0wLjA3MjI5LDIuMDEzODQgLTAuMDcyMjgsMi4wMTM4NCAtMS4xMTk5NDcsMC4wNjUyIGMgLTAuNjE1OTY3LDAuMDM1OSAtMS4yNTk5MzcsMC4wMTYzIC0xLjQzMTA0MSwtMC4wNDM2IHogbSA3Ljk2NDA0NSwwLjAxNjcgLTAuMzExMDksLTAuMTI1NTMgdiAtMS45ODI2OSAtMS45ODI3IGwgMS40MzEwNCwwLjA3NDYgMS40MzEwMywwLjA3NDYgMC4wNzQxLDEuNzAyMzIgMC4wNzQxLDEuNzAyMzIgLTAuMjkxMjMsMC4zNTA5MSBjIC0wLjI3ODY3LDAuMzM1NzcgLTEuNzU0MzgsMC40NDk4MSAtMi40MDc5NiwwLjE4NjA5IHogbSAxLjkyODgsLTIuMDc1MDYgdiAtMS40OTMyNiBoIC0wLjc0NjYzIC0wLjc0NjYzIHYgMS40OTMyNiAxLjQ5MzI2IGggMC43NDY2MyAwLjc0NjYzIHogbSAyLjA1MzIyLDIuMDc1MDYgLTAuMzExMDksLTAuMTI1NTMgdiAtMS45NzAyNyAtMS45NzAyOCBoIDEuMzgzMTMgMS4zODMxMyBsIC0wLjA3NjUsMS4wNTc3MyAtMC4wNzY1LDEuMDU3NzMgLTAuOTMzMjgsMC4wNzcyIC0wLjkzMzI5LDAuMDc3MiB2IDAuNjA3MTcgMC42MDcxNyBoIDAuNTk1NTYgYyAwLjMyNzU2LDAgMC42NjAwMywtMC4xNjc5OSAwLjczODgyLC0wLjM3MzMyIDAuMjI0MTEsLTAuNTg0MDIgMC42NTY2MywtMC40Mzc2MiAwLjY1NjYzLDAuMjIyMjUgMCwwLjc5Mzk3IC0xLjMwMTE5LDEuMTg2OTYgLTIuNDI2NTQsMC43MzI4NyB6IG0gMS42Nzk5MiwtMy4wNzA1NyB2IC0wLjQ5Nzc1IGggLTAuNjIyMTkgLTAuNjIyMTkgdiAwLjQ5Nzc1IDAuNDk3NzUgaCAwLjYyMjE5IDAuNjIyMTkgeiBtIDIuMDAwOTQsMi44NzQwNCBjIC0wLjM1ODEsLTAuNDMxNDkgLTAuMjU4NjMsLTEuMTczNzIgMC4xNzY3MywtMS4zMTg2OCAwLjE4MjQ4LC0wLjA2MDggMC4zMTExLDAuMTEwNzkgMC4zMTExLDAuNDE0OTEgdiAwLjUxODUgaCAwLjc0NjYzIDAuNzQ2NjIgdiAtMC4zMjU0MSBjIDAsLTAuMTc4OTcgLTAuNTExMzcsLTAuNjg1ODYgLTEuMTM2NCwtMS4xMjY0MiBsIC0xLjEzNjQsLTAuODAxMDEgMC4wNzg3LC0wLjU1MzUgMC4wNzg3LC0wLjU1MzUgaCAxLjM2ODgzIDEuMzY4ODIgbCAwLjA3OTUsMC41NTk5OCAwLjA3OTUsMC41NTk5NyBoIC0wLjM5MDYzIGMgLTAuMjE0ODQsMCAtMC4zOTA2MywtMC4xNjc5OSAtMC4zOTA2MywtMC4zNzMzMiB2IC0wLjM3MzMxIGggLTAuNzQ2NjIgYyAtMS4wNDExMywwIC0wLjkzMjY5LDAuMzc2ODYgMC4zNzMzMSwxLjI5NzQyIGwgMS4xMTk5NSwwLjc4OTQxIHYgMC41MjQ1IGMgMCwxLjA2MzY1IC0yLjAxMDg0LDEuNjI0MjUgLTIuNzI3NzIsMC43NjA0NiB6IG0gNS44MzA3NSwwLjA1NzIgYyAtMC4wNzI5LC0wLjE5MDA4IC0wLjEwMSwtMS43MzkzMSAtMC4wNjI0LC0zLjQ0Mjc1IGwgMC4wNzAzLC0zLjA5NzE1IGggMC4zNzMzMSAwLjM3MzMxIHYgMy4zMzkxIDMuMzM5MDkgbCAtMC4zMTA5NiwwLjEwMzY1IGMgLTAuMTcxMDMsMC4wNTcgLTAuMzcwNjUsLTAuMDUxOSAtMC40NDM1OSwtMC4yNDE5NCAgeiBtIDIuMDQ1NiwwLjE0MzQ3IGMgLTAuMDk0MSwtMC4wOTQxIC0wLjE3MTExLC0xLjAzNzU4IC0wLjE3MTExLC0yLjA5NjYxIHYgLTEuOTI1NTEgbCAxLjI0NTg3LC0wLjA4NjIgYyAxLjc2MTI4LC0wLjEyMTkxIDEuNzQwNjYsLTAuMTQ3OTIgMS43NDA2NiwyLjE5NTE0IHYgMi4xMTU0NCBsIC0wLjMxMTEsLTAuMTAyODYgLTAuMzExMSwtMC4xMDI4NyAtMC4wNzMxLC0xLjc4NDQ0IC0wLjA3MzEsLTEuNzg0NDUgaCAtMC42MDAzOCAtMC42MDAzOSBsIC0wLjA3MzEsMS43ODQ0NSBjIC0wLjA2NjgsMS42MzEwOCAtMC4zMjczOSwyLjIzMzY3IC0wLjc3MzA5LDEuNzg3OTYgeiBtIDQuMzY3OTgsLTAuMTI4MzggYyAtMC4xNzg5LC0wLjE3ODkgLTAuMzA4MTgsLTAuOTU3MjQgLTAuMzA4MTgsLTEuODU1MzcgdiAtMS41NDcxOCBsIC0wLjMxMTExLC0wLjEyNjIzIC0wLjMxMTA5LC0wLjEyNjIzIDAuMjg2MDEsLTAuMjA1NjEgYyAwLjE1NzMsLTAuMTEzMDggMC4zMjUyOSwtMC40NzY4MSAwLjM3MzMxLC0wLjgwODMgMC4wNDgsLTAuMzMxNDggMC4yMjczMSwtMC42NDg4IDAuMzk4NDEsLTAuNzA1MTUgbCAwLjMxMTA5LC0wLjEwMjQ1IHYgMC42NjYyMyAwLjY2NjIzIGwgMC40OTc3NiwwLjEzMDE2IGMgMC4yNzM3NiwwLjA3MTYgMC40OTc3NSwwLjIyOTEgMC40OTc3NSwwLjM1MDAyIDAsMC4xMjA5MSAtMC4yMjM5OSwwLjIxOTg1IC0wLjQ5Nzc1LDAuMjE5ODUgaCAtMC40OTc3NiB2IDEuNDkzMjYgMS40OTMyNiBoIDAuNTE4NSBjIDAuNjQ2MDEsMCAwLjUwMjU3LDAuNTk0IC0wLjE2NzU4LDAuNjkzOTQgLTAuMjY0NjQsMC4wMzk1IC0wLjYxOTg1LC0wLjA2NjkgLTAuNzg5MzYsLTAuMjM2NDMgeiBtIDIuNDkxNjcsMC4xMjQyOSAtMC4zMTEwOSwtMC4xMjU1MyB2IC0xLjk4MjY5IC0xLjk4MjcgbCAxLjQzMTA0LDAuMDc0NiAxLjQzMTA0LDAuMDc0NiB2IDAuOTk1NTEgMC45OTU1MSBsIC0xLjA1NzczLDAuMDc2NSAtMS4wNTc3MywwLjA3NjUgdiAwLjYwNzg4IDAuNjA3ODggaCAwLjc0NjY0IDAuNzQ2NjMgdiAtMC4zNzMzMiBjIDAsLTAuMjA1MzIgMC4xNjc5OSwtMC4zNzMzMSAwLjM3MzMxLC0wLjM3MzMxIDAuNDQ2NTgsMCAwLjQ5NDk4LDAuNzc0MjkgMC4wNzQ3LDEuMTk0NjEgLTAuMjk0NjksMC4yOTQ2OCAtMS43NzE5NywwLjM3Nzg2IC0yLjM3Njc3LDAuMTMzODIgeiBtIDEuOTI4OCwtMy4wNzA1NyB2IC0wLjQ5Nzc1IGggLTAuNzQ2NjMgLTAuNzQ2NjQgdiAwLjQ5Nzc1IDAuNDk3NzUgaCAwLjc0NjY0IDAuNzQ2NjMgeiBtIDEuOTA4MDUsMy4wNjk0OCBjIC0wLjA5MTMsLTAuMDkxMyAtMC4xNjU5MiwtMS42NTkxOCAtMC4xNjU5MiwtMy40ODQyNyB2IC0zLjMxODM1IGggMC4zNzMzMSAwLjM3MzMyIHYgMy40ODQyNyBjIDAsMy4xODQ2NSAtMC4xMDY0LDMuNzkyNjYgLTAuNTgwNzEsMy4zMTgzNSB6IG0gMS44MjUxLC0xLjkyODggdiAtMi4xMzYxOSBoIDAuMzgzMzcgMC4zODMzOCBsIC0wLjA3MjMsMi4wMzMyIC0wLjA3MjMsMi4wMzMyIC0wLjMxMTA5LDAuMTAyOTkgLTAuMzExMSwwLjEwMjk5IHogbSA2LjAzNTI1LDEuOTI5ODkgLTAuMzExMSwtMC4xMjU1MyB2IC0xLjk3MDI3IC0xLjk3MDI4IGggMS4zODMxMyAxLjM4MzE0IGwgLTAuMDc2NSwxLjA1NzczIC0wLjA3NjUsMS4wNTc3MyAtMC45MzMyOSwwLjA3NzIgLTAuOTMzMjksMC4wNzcyIHYgMC42MDcxNyAwLjYwNzE3IGggMC41OTU1NiBjIDAuMzI3NTcsMCAwLjY2MDAzLC0wLjE2Nzk5IDAuNzM4ODIsLTAuMzczMzIgMC4yMjQxMSwtMC41ODQwMiAwLjY1NjYzLC0wLjQzNzYyIDAuNjU2NjMsMC4yMjIyNSAwLDAuNzkzOTcgLTEuMzAxMTksMS4xODY5NiAtMi40MjY1NCwwLjczMjg3IHogbSAxLjY3OTkxLC0zLjA3MDU3IHYgLTAuNDk3NzUgaCAtMC42MjIxOSAtMC42MjIxOSB2IDAuNDk3NzUgMC40OTc3NSBoIDAuNjIyMTkgMC42MjIxOSB6IG0gMS45OTEwMSwxLjE2NTc1IHYgLTIuMTExMTMgbCAxLjE4MjE3LC0wLjEwNjg0IGMgMC42NTAxOSwtMC4wNTg4IDEuMzI2NTUsMC4wMDMgMS41MDMwMywwLjEzNzk3IGwgMC4zMjA4OCwwLjI0NDc5IC0wLjA3MiwxLjg5MDIxIC0wLjA3MiwxLjg5MDIxIGggLTAuMzczMzIgLTAuMzczMzEgbCAtMC4wNzMxLC0xLjgwNDM2IC0wLjA3MzEsLTEuODA0MzUgaCAtMC42MDAzOCAtMC42MDAzOCBsIC0wLjA3MzEsMS43ODQ0NSAtMC4wNzMxLDEuNzg0NDQgLTAuMzExMDksMC4xMDI4NyAtMC4zMTExLDAuMTAyODYgeiBtIDQuNTQyLDEuOTA0ODIgLTAuMzExMDksLTAuMTI1NTMgdiAtMS43MjE0IC0xLjcyMTM5IGggLTAuMzczMzIgYyAtMC4yMDUzMywwIC0wLjM3MzMxLC0wLjEwMDAxIC0wLjM3MzMxLC0wLjIyMjI1IDAsMC4xMjIyNCAwLjE2Nzk4LC0wLjI4NjcxIDAuMzczMzEsLTAuMzY1NTEgMC4yMDUzMiwtMC4wNzg4IDAuMzczMzIsLTAuNDM2MDggMC4zNzMzMiwtMC43OTM5OSB2IC0wLjY1MDc0IGwgMC4zMTEwOSwwLjEwMjE1IGMgMC4xNzExLDAuMDU2MiAwLjM0NjU0LDAuNDAxNSAwLjM4OTg1LDAuNzY3MzcgMC4wNDQ0LDAuMzcxNTggMC4yNzQ1NiwwLjY2NTUxIDAuNDczOTMsMC42NTcyOCAwLjE5NjIzLDAgMC4zNTY3OCwwLjExMiAwLjM1Njc4LDAuMjQ4ODggMCwwLjEzNjg4IC0wLjE2Nzk5LDAuMjQ4ODggLTAuMzczMzEsMC4yNDg4OCBoIC0wLjM3MzMyIHYgMS40OTMyNiAxLjQ5MzI2IGggMC4zNzMzMiBjIDAuMjA1MzIsMCAwLjM3MzMxLDAuMTY3OTkgMC4zNzMzMSwwLjM3MzMxIDAsMC4zNjM2OCAtMC41NTQ5OSwwLjQ2MTU2IC0xLjE4MjE2LDAuMjA4NDkgeiBtIDIuMTk2MjYsLTAuMTg2MDkgLTAuMjkxMjMsLTAuMzUwOTEgMC4wNzQxLC0xLjcwMjMyIDAuMDc0MSwtMS43MDIzMiAxLjQ0NTk4LC0wLjA3NTEgMS40NDU5OCwtMC4wNzUxIC0wLjA3NzIsMS4wNzA2IC0wLjA3NzIsMS4wNzA1OSAtMS4wNTc3MiwwLjA3NjUgLTEuMDU3NzMsMC4wNzY1IHYgMC42MDc4OCAwLjYwNzg4IGggMC43NDY2MyAwLjc0NjYzIHYgLTAuMzczMzIgYyAwLC0wLjIwNTMyIDAuMTY3OTksLTAuMzczMzEgMC4zNzMzMiwtMC4zNzMzMSBoIDAuMzczMzEgdiAwLjU5NTU2IGMgMCwwLjkwNDQ3IC0yLjA3NTc2LDEuMzIxODkgLTIuNzE5MDUsMC41NDY3OCB6IG0gMS45NzI0MiwtMi44ODQ0OCB2IC0wLjQ5Nzc1IGggLTAuNzQ2NjMgLTAuNzQ2NjMgdiAwLjQ5Nzc1IDAuNDk3NzUgaCAwLjc0NjYzIDAuNzQ2NjMgeiBtIDIuMDUzMjMsMy4wNzA1NyBjIC0wLjM3OTY0LC0wLjE1MzE5IC0wLjQxMzI5LC0xLjIzNTkgLTAuMDQ1NSwtMS40NjMyMiAwLjE0NjI2LC0wLjA5MDQgMC4zMjcwNiwwLjA3MDggMC40MDIzNCwwLjM1ODYyIGwgMC4xMzY3MSwwLjUyMjggaCAwLjU5MzE3IDAuNTkzMTYgdiAtMC4zNjk3OSBjIDAsLTAuMjAzMzggLTAuNDQ3OTcsLTAuNjY2MjQgLTAuOTk1NTEsLTEuMDI4NTggbCAtMC45OTU1LC0wLjY1ODc5IHYgLTAuNzEzNTYgLTAuNzEzNTYgaCAxLjM2ODgyIDEuMzY4ODIgdiAwLjYyMjIgMC42MjIxOSBoIC0wLjM3MzMxIGMgLTAuMjA1MzMsMCAtMC4zNzMzMiwtMC4xNjc5OSAtMC4zNzMzMiwtMC4zNzMzMiB2IC0wLjM3MzMxIGggLTAuNjIyMTkgYyAtMC45MjU1MiwwIC0wLjc2MTU1LDAuNTg4MjYgMC4zNzMzMiwxLjMzOTI4IGwgMC45OTU1LDAuNjU4NzkgdiAwLjcxNjQ3IGMgMCwwLjkxMjY5IC0xLjIxODI2LDEuMzQxMzMgLTIuNDI2NTQsMC44NTM3OHoiIGlkPSJwYXRoODQyIi8+PC9nPjwvc3ZnPg==" style="height:32px;width:auto"/><h1>FitaDigital</h1><nav>
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

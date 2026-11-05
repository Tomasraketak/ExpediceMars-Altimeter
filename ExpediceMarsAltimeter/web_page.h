#pragma once
#include <Arduino.h>

// Cela webova aplikace v jednom souboru - AP nema pripojeni k internetu,
// takze se nic nesmi tahat z CDN.
const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html lang="cs"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Výškoměr ExpediceMars</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{--bg:#0e1116;--card:#171c24;--line:#242c38;--fg:#e8ecf1;--mut:#8b95a5;
--acc:#ff6b35;--ok:#3ddc84;--warn:#ffc857;--err:#ff5d5d}
body{background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
padding:12px 12px 40px;max-width:760px;margin:0 auto;-webkit-tap-highlight-color:transparent}
h1{font-size:19px;letter-spacing:.2px}h2{font-size:15px;color:var(--mut);font-weight:600;
text-transform:uppercase;letter-spacing:.7px;margin:18px 0 8px}
header{display:flex;align-items:center;gap:10px;padding:6px 2px 10px}
header .dot{width:10px;height:10px;border-radius:50%;background:var(--mut);flex:none}
header .dot.on{background:var(--ok);box-shadow:0 0 8px var(--ok)}
header .dot.fly{background:var(--acc);box-shadow:0 0 8px var(--acc)}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-bottom:10px}
.big{font-size:44px;font-weight:700;line-height:1.05;letter-spacing:-1px}
.big small{font-size:17px;color:var(--mut);font-weight:500;margin-left:4px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(88px,1fr));gap:10px;margin-top:12px}
.grid div span{display:block;font-size:11px;color:var(--mut);text-transform:uppercase;letter-spacing:.5px}
.grid div b{font-size:16px;font-weight:600}
.pill{display:inline-block;padding:3px 10px;border-radius:999px;font-size:12px;font-weight:600;
background:#222a36;color:var(--mut)}
.pill.arm{background:#2a2216;color:var(--warn)}.pill.fly{background:#331a10;color:var(--acc)}
.pill.ok{background:#12301f;color:var(--ok)}
button{font:inherit;font-weight:600;color:var(--fg);background:#232b38;border:1px solid var(--line);
border-radius:10px;padding:11px 14px;cursor:pointer}
button:active{transform:translateY(1px)}
button.pri{background:var(--acc);border-color:var(--acc);color:#1a0d06}
button.dan{background:#3a1b1b;border-color:#5a2626;color:#ffb3b3}
button.sm{padding:7px 10px;font-size:13px;border-radius:8px}
.row{display:flex;gap:8px;flex-wrap:wrap}.row>button{flex:1 1 auto}
input,select{font:inherit;color:var(--fg);background:#0f141b;border:1px solid var(--line);
border-radius:9px;padding:9px 10px;width:100%}
label{display:block;font-size:12px;color:var(--mut);margin:10px 0 4px}
.fl{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px;margin-bottom:8px;
display:flex;align-items:center;gap:12px;cursor:pointer}
.fl:active{border-color:var(--acc)}
.fl .apo{font-size:26px;font-weight:700;min-width:96px;text-align:right;color:var(--acc)}
.fl .apo small{font-size:13px;color:var(--mut);font-weight:500}
.fl .meta{flex:1;min-width:0}
.fl .nm{font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.fl .sub{font-size:12px;color:var(--mut);margin-top:2px}
.tag{font-size:10px;padding:1px 6px;border-radius:5px;background:#3a2a12;color:var(--warn);margin-left:6px}
.overlay{position:fixed;inset:0;background:#0b0e13;overflow-y:auto;padding:12px 12px 40px;z-index:9}
.hidden{display:none}
canvas{width:100%;height:230px;display:block;background:#0f141b;border:1px solid var(--line);border-radius:10px}
.muted{color:var(--mut);font-size:13px}
.err{color:var(--err)}
.top{display:flex;align-items:center;gap:10px;margin-bottom:12px}
.top h1{flex:1;font-size:17px}
footer{text-align:center;color:var(--mut);font-size:11px;margin-top:20px}
</style></head><body>

<header><span class="dot" id="dot"></span><h1>Výškoměr ExpediceMars</h1>
<span class="pill" id="modep">…</span></header>

<div class="card">
  <div class="big"><span id="alt">—</span><small>m nad startem</small></div>
  <div class="muted" id="altmsl">&nbsp;</div>
  <div class="grid">
    <div><span>Rychlost</span><b id="vsp">—</b></div>
    <div><span>Teplota</span><b id="tmp">—</b></div>
    <div><span>Tlak</span><b id="prs">—</b></div>
    <div><span>Reference</span><b id="pad">—</b></div>
    <div><span>Baterie</span><b id="bat">—</b></div>
    <div><span>Paměť</span><b id="mem">—</b></div>
  </div>
</div>

<div class="card">
  <div class="row">
    <button class="pri" id="bzero">Nulovat&nbsp;+&nbsp;ARM</button>
    <button id="barm">Odzbrojit</button>
  </div>
  <div class="row" style="margin-top:8px">
    <button class="sm" id="btest">Testovací let</button>
    <button class="sm" id="bsleep">Uspat výškoměr</button>
    <button class="sm" id="brel">Obnovit</button>
  </div>
  <div class="muted" id="hint" style="margin-top:10px"></div>
</div>

<h2>Zaznamenané lety</h2>
<div id="list"><div class="muted">Načítám…</div></div>

<details class="card"><summary style="cursor:pointer;font-weight:600">Nastavení</summary>
  <label>Nadmořská výška startoviště (m n. m.) – jen pro přepočet zobrazení</label>
  <input id="cMsl" type="number" step="1">
  <label>Práh startu nad startovní plochou (m)</label>
  <input id="cLaunch" type="number" step="1">
  <label>Pokles pod maximum pro detekci apogea (m)</label>
  <input id="cApo" type="number" step="1">
  <label>Název WiFi sítě</label><input id="cSsid" type="text" maxlength="24">
  <label>Heslo (min. 8 znaků)</label><input id="cPass" type="text" maxlength="32" placeholder="beze změny">
  <div class="row" style="margin-top:12px">
    <button class="pri" id="bsave">Uložit</button>
    <button class="dan sm" id="bwipe">Smazat všechny lety</button>
  </div>
</details>

<footer>ExpediceMars · fw <span id="fw">?</span> · <span id="ups">?</span></footer>

<div class="overlay hidden" id="detail">
  <div class="top"><button class="sm" id="dback">‹ Zpět</button><h1 id="dtitle">Let</h1></div>
  <div class="card">
    <div class="big"><span id="dapo">—</span><small>m apogeum</small></div>
    <div class="muted" id="dwhen"></div>
    <div class="grid">
      <div><span>Max. rychlost</span><b id="dspd">—</b></div>
      <div><span>Čas apogea</span><b id="dtap">—</b></div>
      <div><span>Délka záznamu</span><b id="ddur">—</b></div>
      <div><span>Vzorků</span><b id="dcnt">—</b></div>
    </div>
  </div>
  <canvas id="chart"></canvas>
  <div class="muted" id="dread" style="margin:6px 2px 0">Táhni prstem po grafu pro odečet.</div>
  <div class="card" style="margin-top:10px">
    <label>Název letu</label>
    <div class="row"><input id="dname" maxlength="40" placeholder="např. Alfa 3 – C6-5">
    <button class="pri sm" id="dsave">Uložit</button></div>
    <div class="row" style="margin-top:10px">
      <button class="sm" id="dcsv">Export CSV</button>
      <button class="sm" id="dcsvx">CSV pro Excel</button>
      <button class="dan sm" id="ddel">Smazat let</button>
    </div>
  </div>
</div>

<script>
const $=s=>document.querySelector(s);
const MODES=['Připraven','Ozbrojeno','Stoupání','Klesání','Přistáno'];
let S={},cur=null,pts=[],hoverI=-1;

async function api(p,o){try{const r=await fetch(p,o);return await r.json();}catch(e){return null;}}
const f1=(v,d)=>v==null||isNaN(v)?'—':v.toFixed(d===undefined?1:d);
function dur(ms){const s=ms/1000;return s<60?s.toFixed(1)+' s':Math.floor(s/60)+':'+String(Math.round(s%60)).padStart(2,'0')+' min';}
function when(e){if(!e)return'čas neznámý';const d=new Date(e*1000);
return d.toLocaleDateString('cs-CZ')+' '+d.toLocaleTimeString('cs-CZ',{hour:'2-digit',minute:'2-digit'});}

async function status(){
  const s=await api('/api/status');if(!s){$('#modep').textContent='bez spojení';return;}
  S=s;
  $('#alt').textContent=f1(s.alt);
  $('#altmsl').textContent=s.padMsl?('= '+f1(s.alt+s.padMsl)+' m n. m. (startoviště '+f1(s.padMsl,0)+' m)'):'';
  $('#vsp').textContent=f1(s.vspeed)+' m/s';
  $('#tmp').textContent=f1(s.temp)+' °C';
  $('#prs').textContent=f1(s.press/100)+' hPa';
  $('#pad').textContent=s.padSet?f1(s.padPa/100)+' hPa':'není';
  $('#bat').textContent=s.vbat>0?f1(s.vbat,2)+' V':'—';
  $('#mem').textContent=Math.round((s.fsTotal-s.fsUsed)/1024)+' kB volno';
  $('#fw').textContent=s.fw;
  $('#ups').textContent='běží '+dur(s.uptime*1000)+' · '+s.boots+'. zapnutí';
  const p=$('#modep');p.textContent=MODES[s.mode]||'?';
  p.className='pill'+(s.mode==1?' arm':s.mode==2||s.mode==3?' fly':s.mode==4?' ok':'');
  const d=$('#dot');d.className='dot'+(s.mode==2||s.mode==3?' fly':s.armed?' on':'');
  let h='';
  if(!s.sensorOk)h='<span class="err">Senzor BMP580 neodpovídá – zkontroluj I2C.</span>';
  else if(!s.padSet)h='Nejdřív nastav startovní výšku (tlačítko „Nulovat + ARM“ nebo držení tlačítka 3 s).';
  else if(s.mode==1)h='Ozbrojeno. Start se pozná při překročení '+f1(s.launchM,0)+' m nad startovní plochou.';
  else if(s.mode==2||s.mode==3)h='Probíhá záznam letu č. '+s.flightId+', maximum '+f1(s.maxAlt)+' m.';
  else if(s.mode==4)h='Let č. '+s.lastId+' uložen, apogeum '+f1(s.lastApogee)+' m.';
  else h='Odzbrojeno. Po 3 minutách nečinnosti se výškoměr sám uspí.';
  $('#hint').innerHTML=h;
  if(document.activeElement!==$('#cMsl')){$('#cMsl').value=s.padMsl;$('#cLaunch').value=s.launchM;
    $('#cApo').value=s.apoDrop;if(!$('#cSsid').value)$('#cSsid').value=s.ssid;}
}

async function flights(){
  const l=await api('/api/flights');const box=$('#list');
  if(!l){box.innerHTML='<div class="muted">Nelze načíst.</div>';return;}
  if(!l.length){box.innerHTML='<div class="muted">Zatím žádný let. Ozbroj výškoměr a leť!</div>';return;}
  box.innerHTML=l.map(f=>`<div class="fl" data-id="${f.id}">
    <div class="meta"><div class="nm">${f.name||('Let č. '+f.id)}
      ${f.test?'<span class="tag">test</span>':''}
      ${f.recovered?'<span class="tag">obnoveno po resetu</span>':''}
      ${f.done?'':'<span class="tag">nedokončeno</span>'}</div>
      <div class="sub">${when(f.epoch)} · ${dur(f.dur)} · ${f.samples} vzorků</div></div>
    <div class="apo">${f1(f.apogee)}<small> m</small></div></div>`).join('');
  box.querySelectorAll('.fl').forEach(e=>e.onclick=()=>open_(+e.dataset.id));
}

async function open_(id){
  const d=await api('/api/flight?id='+id+'&max=900');if(!d)return;
  cur=d;pts=[];
  for(let i=0;i<d.p.length;i+=2)pts.push([d.p[i],d.p[i+1]]);
  $('#dtitle').textContent=d.name||('Let č. '+d.id);
  $('#dapo').textContent=f1(d.apogee);
  $('#dwhen').textContent=when(d.epoch)+(d.padMsl?(' · vrchol '+f1(d.apogee+d.padMsl)+' m n. m.'):'');
  $('#dspd').textContent=f1(d.maxSpeed)+' m/s';
  $('#dtap').textContent=f1(d.apogeeT/1000)+' s';
  $('#ddur').textContent=dur(d.dur);
  $('#dcnt').textContent=d.samples+(d.step>1?(' (zobrazen každý '+d.step+'.)'):'');
  $('#dname').value=d.name||'';
  $('#detail').classList.remove('hidden');window.scrollTo(0,0);
  hoverI=-1;chart();
}

function nice(r,n){const raw=r/n,m=Math.pow(10,Math.floor(Math.log10(raw))),x=raw/m;
  return (x>5?10:x>2?5:x>1?2:1)*m;}

function chart(){
  const cv=$('#chart'),dpr=devicePixelRatio||1,w=cv.clientWidth,h=cv.clientHeight;
  cv.width=w*dpr;cv.height=h*dpr;const g=cv.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);
  g.clearRect(0,0,w,h);
  const ok=pts.filter(p=>p[1]!=null);if(!ok.length)return;
  const L=44,R=10,T=12,B=26;
  let t1=pts[pts.length-1][0]||1,a0=0,a1=Math.max(...ok.map(p=>p[1]));
  a0=Math.min(0,...ok.map(p=>p[1]));
  const pad=(a1-a0)*0.08||1;a1+=pad;a0-=pad;
  const X=t=>L+(w-L-R)*(t/t1),Y=a=>h-B-(h-T-B)*((a-a0)/(a1-a0));
  g.font='10px system-ui';g.lineWidth=1;
  const sy=nice(a1-a0,4);
  for(let v=Math.ceil(a0/sy)*sy;v<a1;v+=sy){const y=Y(v);
    g.strokeStyle=Math.abs(v)<1e-6?'#3a4554':'#1e2530';g.beginPath();g.moveTo(L,y);g.lineTo(w-R,y);g.stroke();
    g.fillStyle='#8b95a5';g.textAlign='right';g.fillText(Math.round(v),L-6,y+3);}
  const sx=nice(t1/1000,5);
  for(let v=0;v<=t1/1000+1e-6;v+=sx){const x=X(v*1000);if(x>w-R+1)break;
    g.strokeStyle='#1e2530';g.beginPath();g.moveTo(x,T);g.lineTo(x,h-B);g.stroke();
    g.fillStyle='#8b95a5';g.textAlign='center';g.fillText(v+'s',x,h-B+14);}
  g.strokeStyle='#ff6b35';g.lineWidth=2;g.beginPath();let down=false;
  for(const p of pts){if(p[1]==null){down=false;continue;}
    const x=X(p[0]),y=Y(p[1]);if(!down){g.moveTo(x,y);down=true;}else g.lineTo(x,y);}
  g.stroke();
  if(cur&&cur.apogee!=null){const x=X(cur.apogeeT),y=Y(cur.apogee);
    g.fillStyle='#3ddc84';g.beginPath();g.arc(x,y,4,0,7);g.fill();
    g.fillStyle='#3ddc84';g.textAlign=x>w/2?'right':'left';
    g.fillText(f1(cur.apogee)+' m',x+(x>w/2?-8:8),y-6);}
  if(hoverI>=0&&hoverI<pts.length&&pts[hoverI][1]!=null){
    const x=X(pts[hoverI][0]),y=Y(pts[hoverI][1]);
    g.strokeStyle='#8b95a5';g.lineWidth=1;g.beginPath();g.moveTo(x,T);g.lineTo(x,h-B);g.stroke();
    g.fillStyle='#e8ecf1';g.beginPath();g.arc(x,y,3.5,0,7);g.fill();
    $('#dread').textContent=(pts[hoverI][0]/1000).toFixed(1)+' s  →  '+f1(pts[hoverI][1])+' m';}
}

function hover(ev){
  if(!pts.length)return;const cv=$('#chart'),r=cv.getBoundingClientRect();
  const cx=(ev.touches?ev.touches[0].clientX:ev.clientX)-r.left;
  const L=44,R=10,t1=pts[pts.length-1][0]||1;
  const t=Math.max(0,Math.min(t1,(cx-L)/(r.width-L-R)*t1));
  let bi=0,bd=1e12;pts.forEach((p,i)=>{const d=Math.abs(p[0]-t);if(d<bd){bd=d;bi=i;}});
  hoverI=bi;chart();if(ev.cancelable)ev.preventDefault();
}

$('#chart').addEventListener('mousemove',hover);
$('#chart').addEventListener('touchmove',hover,{passive:false});
$('#chart').addEventListener('touchstart',hover,{passive:false});
addEventListener('resize',()=>{if(!$('#detail').classList.contains('hidden'))chart();});

$('#dback').onclick=()=>{$('#detail').classList.add('hidden');flights();};
$('#dsave').onclick=async()=>{await api('/api/name?id='+cur.id,{method:'POST',body:$('#dname').value});
  cur.name=$('#dname').value;$('#dtitle').textContent=cur.name||('Let č. '+cur.id);};
$('#dcsv').onclick=()=>location.href='/api/csv?id='+cur.id;
$('#dcsvx').onclick=()=>location.href='/api/csv?id='+cur.id+'&dec=,';
$('#ddel').onclick=async()=>{if(!confirm('Opravdu smazat let č. '+cur.id+'?'))return;
  await api('/api/delete?id='+cur.id,{method:'POST'});$('#detail').classList.add('hidden');flights();};

$('#bzero').onclick=async()=>{if(!confirm('Nastavit současnou výšku jako startovní a ozbrojit?'))return;
  await api('/api/zero',{method:'POST'});$('#hint').textContent='Kalibruji referenci…';setTimeout(status,3000);};
$('#barm').onclick=async()=>{await api('/api/arm?on=0',{method:'POST'});setTimeout(status,300);};
$('#btest').onclick=async()=>{if(!confirm('Nastavit současnou výšku jako startovní a hned spustit testovací záznam?\nKonec nejdřív po 10 s, až bude výška < 25 m a změna < 1 m/s po 1 s. Web během záznamu neodpovídá.'))return;
  await api('/api/test',{method:'POST'});$('#hint').textContent='Kalibruji a spouštím testovací let…';setTimeout(()=>{status();flights();},15000);};
$('#bsleep').onclick=async()=>{if(!confirm('Uspat výškoměr? Probudíš ho stiskem tlačítka.'))return;
  await api('/api/sleep',{method:'POST'});};
$('#brel').onclick=()=>{status();flights();};
$('#bsave').onclick=async()=>{
  const q='padMsl='+(+$('#cMsl').value||0)+'&launchM='+(+$('#cLaunch').value||10)
    +'&apoDrop='+(+$('#cApo').value||10)+'&ssid='+encodeURIComponent($('#cSsid').value)
    +'&pass='+encodeURIComponent($('#cPass').value);
  await api('/api/settings?'+q,{method:'POST'});$('#cPass').value='';alert('Uloženo. Změna WiFi se projeví po restartu AP.');status();};
$('#bwipe').onclick=async()=>{if(!confirm('Smazat VŠECHNY uložené lety? Nelze vrátit.'))return;
  await api('/api/deleteall?confirm=1',{method:'POST'});flights();};

api('/api/time?epoch='+Math.floor(Date.now()/1000),{method:'POST'}).then(()=>{status();flights();});
setInterval(status,2000);
setInterval(()=>{if($('#detail').classList.contains('hidden')&&S.mode!=2&&S.mode!=3)flights();},15000);
</script></body></html>
)HTMLPAGE";

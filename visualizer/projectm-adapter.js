import createProjectM from './projectm/projectm.js';
const engineSelect=document.getElementById('engine'),presetSelect=document.getElementById('preset');
const engineStatus=document.getElementById('engineStatus'),scope=document.getElementById('scope');
const glCanvas=document.getElementById('canvas'),modeLabel=document.getElementById('modeLabel');
const presetLabel=document.getElementById('presetLabel');
const toneButton=document.getElementById('tone'),paletteControl=document.getElementById('palette');
const library = new Map(Object.entries(MILK_PRESETS).map(([id,text])=>[id,{name:'WaveScope '+id[0].toUpperCase()+id.slice(1),text}]));
function rebuildPresets(){
 const selected=presetSelect.value;
 presetSelect.replaceChildren();
 for(const [id,preset] of library)presetSelect.add(new Option(preset.name,id));
 if(library.has(selected))presetSelect.value=selected;
}
rebuildPresets();
let module=null,api=null,pcmPointer=0,limit=0,active=false,initializing=false,failed=false,lastPcm=0;
let generation=0,refreshGeneration=0,loadedPreset=presetSelect.value;
function status(text){engineStatus.textContent=text;report('projectM: '+text);}
async function loadPreset(blend=0){
 if(!api)return;
 const ticket=++generation,id=presetSelect.value,preset=library.get(id);
 if(!preset)throw Error('Preset is no longer available');
 let text=preset.text;
 if(text===undefined){
  try{
   const response=await fetch('./user-presets/'+id.slice(5)+'.milk',{cache:'no-store'});
   if(!response.ok)throw Error('Could not read saved preset');
   text=await response.text();
  }catch(error){if(ticket!==generation)return;throw error;}
 }
 if(ticket!==generation)return;
 const result=api.load(text,blend);
 if(result!==0){presetSelect.value=loadedPreset;throw Error(api.error()||'Preset rejected; previous visual retained');}
 loadedPreset=id;
}
async function refreshImports(){
 const ticket=++refreshGeneration;
 try{
  const response=await fetch('./user-presets.json',{cache:'no-store'});
  if(!response.ok)throw Error('Could not list saved presets');
  const presets=await response.json();
  if(ticket!==refreshGeneration)return;
  for(const [id] of library)if(id.startsWith('user:'))library.delete(id);
  presets.sort((a,b)=>String(a.name).localeCompare(String(b.name)));
  for(const preset of presets)if(/^[a-f0-9]{64}$/i.test(preset.id))library.set('user:'+preset.id,{name:preset.name});
  rebuildPresets();
 }catch(error){if(ticket===refreshGeneration)status('Saved presets: '+error.message);}
}
window.modulezPresetLibrary={refresh:refreshImports};
document.getElementById('importPresets').onclick=()=>report('import-milk-presets');
void refreshImports();
async function initialize(){
 const probe=document.createElement('canvas');const gl=probe.getContext('webgl2');
 if(!gl)throw Error('WebGL2 unavailable in this WebKit/GPU configuration');
 gl.getExtension('WEBGL_lose_context')?.loseContext();
 // Only one GL context on the real projectM canvas, as required by its C wrapper.
 resizeCanvas();
 module=await createProjectM({canvas:glCanvas,noInitialRun:true,
   locateFile:name=>new URL('./projectm/'+name,location.href).href,
   printErr:text=>report('projectM engine: '+text),onAbort:text=>status('Engine aborted: '+text)});
 const wrap=(name,ret,args)=>module.cwrap(name,ret,args);
 api={init:wrap('sv_init','number',['number','number','number','number']),
  resize:wrap('sv_resize',null,['number','number']),load:wrap('sv_load_preset_data','number',['string','number']),
  error:wrap('sv_last_error','string',[]),add:wrap('sv_add_pcm_float',null,['number','number','number']),
  maximum:wrap('sv_max_samples','number',[]),render:wrap('sv_render',null,[]),destroy:wrap('sv_destroy',null,[])};
 const rc=api.init(glCanvas.width,glCanvas.height,32,24);if(rc!==0)throw Error('WebGL/projectM initialization returned '+rc);
 limit=Math.max(1,Math.min(8192,api.maximum()||512));
 pcmPointer=module._malloc(limit*2*4);if(!pcmPointer)throw Error('Could not allocate PCM buffer');
 await loadPreset();status('Ready • 30 fps target • bounded resolution');
}
function resizeCanvas(){
 const rect=scope.parentElement.getBoundingClientRect(),scale=Math.min(1,960/Math.max(1,rect.width));
 const w=Math.max(1,Math.round(rect.width*scale)),h=Math.max(1,Math.min(720,Math.round(rect.height*scale)));
 if(glCanvas.width!==w||glCanvas.height!==h){glCanvas.width=w;glCanvas.height=h;if(api)api.resize(w,h);}
}
function showStandard(){active=false;glCanvas.hidden=true;scope.hidden=false;modeLabel.hidden=false;presetLabel.hidden=true;toneButton.disabled=false;paletteControl.disabled=false;}
engineSelect.onchange=async()=>{
 if(engineSelect.value==='standard'){showStandard();return;}
 if(initializing)return;
 initializing=true;engineSelect.disabled=true;
 try{
  if(failed)throw Error('Engine previously failed; restart this test before retrying');
  // Make GL surface visible so the initial layout is meaningful.
  glCanvas.hidden=false;scope.hidden=true;modeLabel.hidden=true;presetLabel.hidden=false;
  if(toneButton.textContent.endsWith('on'))toneButton.click();
  toneButton.disabled=true;paletteControl.disabled=true;
  if(!api){status('Loading projectM…');await initialize();}
  active=true;lastPcm=performance.now();
 }catch(error){failed=true;status(error.message);report('projectM failure: '+error.stack);engineSelect.value='standard';showStandard();}
 finally{initializing=false;engineSelect.disabled=false;}
};
presetSelect.onchange=async()=>{try{await loadPreset(1.5);}catch(error){presetSelect.value=loadedPreset;status(error.message);}};
async function changePreset(direction){
 if(!active||!library.size)return;
 const ids=[...library.keys()];let index=Math.max(0,ids.indexOf(presetSelect.value));
 if(direction==='random'&&ids.length>1)index=(index+1+Math.floor(Math.random()*(ids.length-1)))%ids.length;
 else if(direction==='next')index=(index+1)%ids.length;
 else if(direction==='previous')index=(index+ids.length-1)%ids.length;
 presetSelect.value=ids[index];await presetSelect.onchange();
}
function changeWavescopeMode(direction){
 const select=document.getElementById('mode');
 const count=select.options.length;
 if(!count)return;
 let index=select.selectedIndex;
 if(direction==='random'&&count>1)
  index=(index+1+Math.floor(Math.random()*(count-1)))%count;
 else if(direction==='next')index=(index+1)%count;
 else if(direction==='previous')index=(index+count-1)%count;
 select.selectedIndex=index;
 select.dispatchEvent(new Event('change',{bubbles:true}));
}
const menu=document.getElementById('presetMenu');
function menuCommand(label,command){
 const button=document.createElement('button');
 button.type='button';button.setAttribute('role','menuitem');button.textContent=label;
 button.onclick=()=>{closeMenu();report(command);};
 menu.appendChild(button);
 return button;
}
const previousSongItem=menuCommand('Previous Song    Ctrl+←','previous-song');
previousSongItem.style.borderTop='1px solid #34616a';
const nextSongItem=menuCommand('Next Song    Ctrl+→','next-song');
const fullscreenItem=menuCommand('Enter Fullscreen    F11','toggle-fullscreen');
fullscreenItem.style.borderTop='1px solid #34616a';
function closeMenu(){menu.hidden=true;}
for(const button of menu.querySelectorAll('[data-preset-action]'))button.onclick=()=>{
 closeMenu();
 if(active)void changePreset(button.dataset.presetAction);
 else changeWavescopeMode(button.dataset.presetAction);
};
document.getElementById('menuImport').onclick=()=>{closeMenu();report('import-milk-presets');};
for(const canvas of [scope,glCanvas])canvas.addEventListener('contextmenu',event=>{
 event.preventDefault();
 for(const button of menu.querySelectorAll('[data-preset-action]')){
  const action=button.dataset.presetAction;
  button.disabled=active?!library.size:!document.getElementById('mode').options.length;
  button.textContent=(action==='previous'?'Previous ':action==='next'?'Next ':'Random ')+(active?'Preset':'Mode');
 }
 document.getElementById('menuImport').hidden=!active;
 fullscreenItem.textContent=(document.body.dataset.fullscreen==='true'?'Exit Fullscreen':'Enter Fullscreen')+'    F11';
 menu.hidden=false;
 menu.style.left=Math.max(0,Math.min(event.clientX,innerWidth-menu.offsetWidth-8))+'px';
 menu.style.top=Math.max(0,Math.min(event.clientY,innerHeight-menu.offsetHeight-8))+'px';
 menu.querySelector('button:not(:disabled)').focus();
});
document.addEventListener('pointerdown',event=>{if(!menu.contains(event.target))closeMenu();});
document.addEventListener('keydown',event=>{
 if(menu.hidden)return;
 if(event.key==='Escape'){closeMenu();event.preventDefault();}
 if(event.key==='ArrowDown'||event.key==='ArrowUp'){
  const buttons=[...menu.querySelectorAll('button:not(:disabled)')],index=buttons.indexOf(document.activeElement);
  buttons[(index+(event.key==='ArrowDown'?1:buttons.length-1))%buttons.length].focus();event.preventDefault();
 }
});
window.addEventListener('blur',closeMenu);
window.modulezProjectM={
 get active(){return active;},
 clearAudio(){if(api&&module&&pcmPointer){module.HEAPF32.fill(0,pcmPointer>>2,(pcmPointer>>2)+limit*2);api.add(pcmPointer,limit,2);}lastPcm=0;},
 feed(raw){
  if(!active||!api)return;
  try{
   const total=Math.floor(raw.length/4);
   for(let offset=0;offset<total;offset+=limit){const n=Math.min(limit,total-offset),values=new Float32Array(n*2);
    for(let i=0;i<n*2;i++){const at=offset*4+i*2;let x=raw.charCodeAt(at)|(raw.charCodeAt(at+1)<<8);values[i]=(x>=32768?x-65536:x)/32768;}
    module.HEAPF32.set(values,pcmPointer>>2);api.add(pcmPointer,n,2);
   }
   lastPcm=performance.now();
  }catch(error){active=false;status('PCM feed failed: '+error.message);engineSelect.value='standard';showStandard();}
 },
 render(now){
  if(!active||!api)return;
  try{resizeCanvas();if(now-lastPcm>180){module.HEAPF32.fill(0,pcmPointer>>2,(pcmPointer>>2)+limit*2);api.add(pcmPointer,limit,2);}api.render();}
  catch(error){status('Render failed: '+error.message);failed=true;engineSelect.value='standard';showStandard();}
 }
};
window.addEventListener('pagehide',()=>{active=false;if(module&&pcmPointer)module._free(pcmPointer);if(api)api.destroy();pcmPointer=0;api=null;module=null;});
report('projectM controls ready • select projectM / MilkDrop to test');

const root=document.documentElement;
root.classList.add('enhanced');
const reduced=matchMedia('(prefers-reduced-motion: reduce)');
let paused=reduced.matches,scene=null,animationFrame=0,telemetryTime=0,disposed=false,gsapContext=null;
const motion=document.querySelector('.motion-toggle');
const story=document.querySelector('.hardware-story');
const stage=document.querySelector('#hardware-stage');
const explode=document.querySelector('#explode-button');
let assembled=false,lastScroll=-1;
function progress(){return Math.max(0,Math.min(1,(scrollY-story.offsetTop)/(story.offsetHeight-innerHeight)));}
function motionUI(){root.classList.toggle('motion-paused',paused);motion.setAttribute('aria-pressed',String(paused));motion.setAttribute('aria-label',paused?'Play animations':'Pause animations');motion.querySelector('img').src=`./assets/icons/${paused?'play':'pause'}.svg`;scene?.setPaused(paused);}
motionUI();
function syncStory(){if(scrollY===lastScroll)return;lastScroll=scrollY;scene?.setProgress(progress());assembled=false;explode.querySelector('span').textContent='Assemble console';}
window.addEventListener('scroll',syncStory,{passive:true});
window.addEventListener('resize',()=>{lastScroll=-1;syncStory();},{passive:true});
try{
  const {createConsoleScene}=await import('./scene.js');
  scene=createConsoleScene(document.querySelector('#console-canvas'),stage,{paused});
  scene.setProgress(progress());
}catch(error){stage.classList.add('scene-unavailable');document.querySelector('.scene-controls').hidden=true;explode.hidden=true;console.warn('The interactive illustration is unavailable; showing the static artwork.',error);}
for(const button of document.querySelectorAll('[data-rotate]'))button.addEventListener('click',()=>scene?.rotate(Number(button.dataset.rotate)));
explode.addEventListener('click',()=>{assembled=!assembled;scene?.setExploded(!assembled);explode.querySelector('span').textContent=assembled?'Explore the layers':'Assemble console';});
for(const button of document.querySelectorAll('[data-view]'))button.addEventListener('click',()=>{
  const library=button.dataset.view==='library';
  document.querySelector('#library-image').hidden=!library;
  document.querySelector('#inspector-image').hidden=library;
  document.querySelector('#capture-view-label').textContent=library?'GAME LIBRARY':'HARDWARE INSPECTOR';
  for(const other of document.querySelectorAll('[data-view]')){other.classList.toggle('active',other===button);other.setAttribute('aria-pressed',String(other===button));}
});

// Scroll supplies progress; there is no scroll hijacking or artificial page loading.
const gsap=window.gsap;
function configureScrollEffects(){
  gsapContext?.revert();gsapContext=null;
  if(paused||!gsap||!window.ScrollTrigger)return;
  gsap.registerPlugin(window.ScrollTrigger);
  gsapContext=gsap.matchMedia();
  gsapContext.add('(prefers-reduced-motion: no-preference)',()=>{
    gsap.from('.site-header',{opacity:0,y:-12,duration:.85,ease:'power2.out'});
    gsap.from('.hero h1,.hero-description,.hero-cta',{opacity:0,y:24,duration:1.1,delay:.15,ease:'power2.out'});
    gsap.fromTo('.hero-title-wrap',{opacity:1,y:0},{opacity:0,y:-75,ease:'none',immediateRender:false,scrollTrigger:{trigger:'.hero',start:'35% top',end:'85% top',scrub:.5}});
    gsap.to('.floating-spec',{opacity:0,y:-40,ease:'none',scrollTrigger:{trigger:'.hero',start:'20% top',end:'65% top',scrub:.5}});
    gsap.from('.autopsy-panel',{opacity:0,y:45,duration:1,scrollTrigger:{trigger:'.anatomy',start:'top 55%',end:'top 5%',scrub:.6}});
    for(const section of document.querySelectorAll('.section-wrap'))gsap.from(section.querySelector('.section-heading')||section.querySelector('.eyebrow'),{opacity:0,y:22,duration:.8,scrollTrigger:{trigger:section,start:'top 84%',once:true}});
  });
}

configureScrollEffects();

// Lightweight architecture illustrations. These are explicitly labelled demo data in the UI.
const heat=document.querySelector('#heatmap'),wave=document.querySelector('#waveform'),net=document.querySelector('#network-canvas');
const hc=heat.getContext('2d'),wc=wave.getContext('2d'),nc=net.getContext('2d');
const active=new Set();
const visibility=new IntersectionObserver(entries=>{for(const e of entries){if(e.isIntersecting)active.add(e.target);else active.delete(e.target);}drawTelemetry();},{rootMargin:'80px'});
for(const canvas of[heat,wave,net])visibility.observe(canvas);
function renderHeat(t){
  hc.fillStyle='#07160e';hc.fillRect(0,0,320,256);
  for(let y=0;y<40;y++)for(let x=0;x<50;x++){
    const hash=((x*37+y*13+x*y*7)%101)/100;
    const region=(x<29&&y<25)||(y>31&&x>12&&x<44);
    const pulse=.5+.5*Math.sin(t*.4+x*.15+y*.31);
    hc.fillStyle=region?`rgba(${hash>.81?'64,174,208':hash>.64?'109,226,144':'31,135,84'},${.12+hash*pulse*.65})`:'#102b1d';
    hc.fillRect(x*6.4,y*6.4,4.8,4.8);
  }
}
function renderWave(t){
  wc.clearRect(0,0,640,114);
  const colors=['#36e994','#76c68c','#b4dd98','#518472'];
  for(let ch=0;ch<4;ch++){
    const baseline=15+ch*28;wc.strokeStyle='#294935';wc.lineWidth=.6;wc.beginPath();wc.moveTo(0,baseline);wc.lineTo(640,baseline);wc.stroke();
    wc.strokeStyle=colors[ch];wc.lineWidth=1.3;wc.beginPath();
    for(let x=0;x<=640;x++){const phase=x*.055+t*.7;const value=ch<2?(Math.sin(phase*(ch+1))>0?7:-7):ch===2?Math.sin(phase)*7:Math.sin(phase*13)*Math.cos(phase*9)*7;if(x===0)wc.moveTo(x,baseline+value);else wc.lineTo(x,baseline+value);}wc.stroke();
  }
}
function renderNetwork(t){
  nc.clearRect(0,0,800,200);
  for(let path=0;path<3;path++){
    const offset=(path-1)*32;nc.strokeStyle=path===1?'#286a49':'#193f2c';nc.lineWidth=1;nc.beginPath();nc.moveTo(0,100+offset);nc.bezierCurveTo(250,100+offset*2,550,100-offset*2,800,100-offset);nc.stroke();
    for(let i=0;i<5;i++){
      let p=(t*.075+i*.2+path*.13)%1;if(path===2)p=1-p;
      const x=800*p,y=100+offset*(1-6*p*p+4*p*p*p);
      nc.shadowColor='#26f395';nc.shadowBlur=13;nc.fillStyle=path===1?'#79ffb3':'#298657';nc.beginPath();nc.arc(x,y,path===1?3:2,0,Math.PI*2);nc.fill();nc.shadowBlur=0;
    }
  }
}
let lastDraw=0;
function drawTelemetry(timestamp=0){
  if(disposed)return;if(animationFrame){cancelAnimationFrame(animationFrame);animationFrame=0;}
  if(timestamp-lastDraw>40||timestamp===0){
    if(!paused)telemetryTime=timestamp/1000;lastDraw=timestamp;
    if(active.has(heat)||timestamp===0)renderHeat(telemetryTime);
    if(active.has(wave)||timestamp===0)renderWave(telemetryTime);
    if(active.has(net)||timestamp===0)renderNetwork(telemetryTime);
  }
  if(!paused&&active.size&&!document.hidden)animationFrame=requestAnimationFrame(drawTelemetry);
}
drawTelemetry();
motion.addEventListener('click',()=>{paused=!paused;motionUI();configureScrollEffects();drawTelemetry();});
reduced.addEventListener('change',event=>{paused=event.matches;motionUI();configureScrollEffects();drawTelemetry();});
document.addEventListener('visibilitychange',()=>{if(document.hidden){cancelAnimationFrame(animationFrame);animationFrame=0;}else drawTelemetry();});
window.addEventListener('pagehide',event=>{if(event.persisted)return;disposed=true;cancelAnimationFrame(animationFrame);visibility.disconnect();scene?.dispose();gsapContext?.revert();});

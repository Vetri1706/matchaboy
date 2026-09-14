import gsap from 'gsap';
import {ScrollTrigger} from 'gsap/ScrollTrigger';
import Lenis from 'lenis';
import {Component,lazy,Suspense,useCallback,useEffect,useLayoutEffect,useRef,useState,type ReactNode} from 'react';
import {Arcade} from './Arcade';
import {Inspector} from './Inspector';
import {ScenePoster} from './ScenePoster';
import {createPresentation} from './presentation';
const ReviewTools=lazy(()=>import('./ReviewTools'));
const reviewing=import.meta.env.DEV&&new URLSearchParams(location.search).has('review');
const Scene=lazy(()=>import('./HandheldScene'));
gsap.registerPlugin(ScrollTrigger);
export const repo='https://github.com/Vetri1706/matchaboy';
export function Mark({light=false}:{light?:boolean}){return <a className={'brand '+(light?'brand-light':'')} href="#top" aria-label="Matchaboy home"><img src="/brand/mark.svg" width="38" height="41" alt=""/><span>matcha<b>boy</b></span></a>}
class SceneBoundary extends Component<{children:ReactNode;onFail:()=>void},{failed:boolean}> {
  state={failed:false};static getDerivedStateFromError(){return {failed:true};}componentDidCatch(){this.props.onFail();}
  render(){return this.state.failed?null:this.props.children;}
}
function useReducedMotion(){const [reduced,setReduced]=useState(()=>window.matchMedia('(prefers-reduced-motion: reduce)').matches);useEffect(()=>{const q=window.matchMedia('(prefers-reduced-motion: reduce)'),update=()=>setReduced(q.matches);q.addEventListener('change',update);return()=>q.removeEventListener('change',update);},[]);return reduced;}
function App(){
  const preference=useReducedMotion(),[reviewReduced,setReviewReduced]=useState(false),reduced=preference||reviewReduced,story=useRef<HTMLElement>(null),progress=useRef(0),cursorVelocity=useRef(0),cursor=useRef<HTMLDivElement>(null),lenisRef=useRef<Lenis|null>(null);
  const [dark,setDark]=useState(false),[chapter,setChapter]=useState(0),[sceneReady,setSceneReady]=useState(false),[playing,setPlaying]=useState(false),[still,setStill]=useState(false),[failed,setFailed]=useState(false),[pausedMoment,setPausedMoment]=useState(false),[inspectionRequest,setInspectionRequest]=useState(0);
  const replayRequest=useRef(0),staticView=reduced||still||failed;
  const sceneStarted=useCallback(()=>setSceneReady(true),[]),sceneFailed=useCallback(()=>setFailed(true),[]);
  useEffect(()=>{if(sceneReady||staticView)return;const t=setTimeout(sceneFailed,12000);return()=>clearTimeout(t);},[sceneReady,staticView,sceneFailed]);
  useEffect(()=>{if(reduced)return;const l=new Lenis({duration:.9,smoothWheel:true,wheelMultiplier:.9,anchors:true});lenisRef.current=l;const tick=(time:number)=>l.raf(time*1000);l.on('scroll',ScrollTrigger.update);gsap.ticker.add(tick);return()=>{gsap.ticker.remove(tick);l.destroy();lenisRef.current=null;};},[reduced]);
  useLayoutEffect(()=>{
    if(!story.current)return;const el=story.current;
    let target=0,lastChapter=-1,lastDark=false;
    const presentation=createPresentation();
    const update=(p:number)=>{
      progress.current=p;
      const next=p<.22?0:p<.45?1:p<.77?2:3;
      if(next!==lastChapter){lastChapter=next;setChapter(next);}
      const night=Math.min(1,Math.max(0,(p-.31)/.18)),nextDark=night>.57;
      if(nextDark!==lastDark){lastDark=nextDark;setDark(nextDark);}
      el.style.setProperty('--night',String(night));
      el.style.setProperty('--expand',String(Math.min(1,Math.max(0,(p-.32)/.26))));
      el.style.setProperty('--adaptive-body',night<.2?'#3d4c40':night<.57?'#061109':night<.97?'#ffffff':'#d1dbcf');
      el.style.setProperty('--adaptive-title',night<.2?'#17261f':night<.57?'#061109':night<.97?'#ffffff':'#eef1e6');
      el.style.setProperty('--journey',String(p));
      el.dataset.progress=p.toFixed(4);
      window.dispatchEvent(new Event('presentation-change'));
    };
    const trigger=ScrollTrigger.create({trigger:el,start:'top top',end:'bottom bottom',onUpdate:self=>{target=self.progress;},onRefresh:self=>{target=self.progress;}});
    const tick=(_time:number,delta:number)=>{const next=presentation.advance(target,delta/1000,reduced,replayRequest.current);if(Math.abs(next-progress.current)>.00001||lastChapter===-1)update(next);};
    gsap.ticker.add(tick);
    const t=setTimeout(()=>ScrollTrigger.refresh(),150);return()=>{gsap.ticker.remove(tick);trigger.kill();clearTimeout(t);};
  },[reduced]);
  useEffect(()=>{
    if(reduced||!window.matchMedia('(pointer:fine)').matches)return;let px=0,py=0;const node=cursor.current;if(!node)return;
    const x=gsap.quickTo(node,'x',{duration:.16,ease:'power3'}),y=gsap.quickTo(node,'y',{duration:.16,ease:'power3'});
    const move=(e:PointerEvent)=>{const target=(e.target as HTMLElement).closest<HTMLElement>('a,button,[data-cursor]'),r=target?.getBoundingClientRect(),snap=r&&r.width<240;
      x(snap?e.clientX+(r.left+r.width/2-e.clientX)*.25:e.clientX);y(snap?e.clientY+(r.top+r.height/2-e.clientY)*.25:e.clientY);
      node.dataset.active=target?'true':'false';node.dataset.visible='true';node.textContent=target?.dataset.cursor??'';cursorVelocity.current=Math.min(1,Math.hypot(e.clientX-px,e.clientY-py)/80);px=e.clientX;py=e.clientY;};
    const decay=()=>{cursorVelocity.current*=.9;},hide=()=>{node.dataset.visible='false';};
    window.addEventListener('pointermove',move,{passive:true});document.addEventListener('pointerleave',hide);gsap.ticker.add(decay);
    return()=>{window.removeEventListener('pointermove',move);document.removeEventListener('pointerleave',hide);gsap.ticker.remove(decay);};
  },[reduced]);
  useEffect(()=>{if(chapter!==1)setPlaying(false);},[chapter]);
  useEffect(()=>{if(staticView)setPlaying(false);},[staticView]);
  const goChapter=(i:number)=>{if(!story.current)return;const y=story.current.offsetTop+(story.current.offsetHeight-innerHeight)*[0,.28,.74,.89][i];if(lenisRef.current)lenisRef.current.scrollTo(y);else window.scrollTo({top:y,behavior:reduced?'instant':'smooth'});};
  const replayOpening=()=>{if(story.current){const min=story.current.offsetTop,max=min+story.current.offsetHeight-innerHeight-2;const y=Math.max(min,Math.min(max,window.scrollY));if(Math.abs(y-window.scrollY)>1){if(lenisRef.current)lenisRef.current.scrollTo(y,{immediate:true});else window.scrollTo({top:y,behavior:"instant"});}}requestAnimationFrame(()=>{replayRequest.current+=1;});};
  const magnet=(e:React.PointerEvent<HTMLAnchorElement>)=>{if(reduced||e.pointerType!=='mouse')return;const el=e.currentTarget,r=el.getBoundingClientRect();gsap.to(el,{x:(e.clientX-r.left-r.width/2)*.12,y:(e.clientY-r.top-r.height/2)*.12,duration:.3});};
  const resetMagnet=(e:React.PointerEvent<HTMLAnchorElement>)=>{gsap.to(e.currentTarget,{x:0,y:0,duration:.5,ease:'power3.out'});};
  const gameInput=(key:string,active:boolean)=>window.dispatchEvent(new CustomEvent('game-input',{detail:{key,active}}));
  const inspectRecorded=()=>{setInspectionRequest(n=>n+1);if(lenisRef.current)lenisRef.current.scrollTo('#inspector',{offset:0});else document.getElementById('inspector')?.scrollIntoView({behavior:'instant'});};
  const seekReview=(value:number)=>{if(!story.current)return;const y=story.current.offsetTop+(story.current.offsetHeight-innerHeight)*value;if(lenisRef.current)lenisRef.current.scrollTo(y,{immediate:true});else window.scrollTo({top:y,behavior:'instant'});};
  return <main id="top" data-reduced={reduced}>
    <div ref={cursor} className="cursor" aria-hidden="true"/>
    <header><Mark/><nav aria-label="Main navigation"><a href="#inside">Inside Matchaboy</a><a href="#arcade">The games</a><a href="#netplay">Play together</a></nav><a href="#download" className="nav-download tactile">Download <span>↗</span></a></header>
    <section className="webgl-story" ref={story} id="inside" aria-label="Explore Matchaboy">
      <div className={'stage '+(dark?'stage-dark':'')} data-chapter={chapter}>
        <div className="light-field"/><div className="dark-field"/><div className="stage-grain"/>
        <div className="scene-area" id="console-preview" tabIndex={playing?0:-1} aria-label="Playable console. Arrow keys to move, Z to jump, Escape to stop." onKeyDown={e=>{if(e.key==="Escape"){setPlaying(false);document.getElementById("play-toggle")?.focus({preventScroll:true});}}} data-cursor="Drag">{(staticView||!sceneReady)&&<ScenePoster opened={chapter>=2} failed={failed} loading={!staticView&&!sceneReady}/>}{!staticView&&<SceneBoundary onFail={sceneFailed}><Suspense fallback={null}><Scene progress={progress} cursorVelocity={cursorVelocity} onReady={sceneStarted} reduced={reduced} playing={playing} frozen={pausedMoment||chapter>=2||reviewing}/></Suspense></SceneBoundary>}</div>
        <div className="story-copy-area">
          <div className={'story-copy hero-copy '+(chapter===0?'is-active':'')} aria-hidden={chapter!==0} inert={chapter!==0}><p className="eyebrow"><i/> SMALL MACHINE. ENDLESS CURIOSITY.</p><h1>Play it.<br/><em>Open it up.</em></h1><p><span className="desktop-story-text">Your favourite handheld worlds, right at home on your desktop. Play Game Boy and Game Boy Advance. Then discover what makes them tick.</span><span className="phone-story-text">A desktop emulator for Game Boy and Game Boy Advance. Play, pause and look inside.</span></p><div className="hero-actions"><a className="primary tactile" href="#download" onPointerMove={magnet} onPointerLeave={resetMagnet}>Download Matchaboy <span>↗</span></a><button className="text-button" onClick={()=>goChapter(1)} aria-label="Explore the console">Explore ↓</button></div><small className="compatibility">Windows · macOS · Linux / Free & open source</small></div>
          <div className={'story-copy '+(chapter===1?'is-active':'')} aria-hidden={chapter!==1} inert={chapter!==1}><p className="eyebrow">01 / PICK IT UP</p><h2>A little screen.<br/>A whole world.</h2><p>{staticView?"Explore the console illustration, then see a recorded moment from the desktop Inspector.":<><span className="desktop-story-text">A familiar set of buttons. A world waiting on the other side. Drag the console to look around, or try the little jumping game on its screen.</span><span className="phone-story-text">Drag to look around. Try the screen, then pause and open the console.</span></>}</p><button className={'pill tactile '+(playing?'selected':'')} id="play-toggle" onClick={()=>{if(staticView){goChapter(2);return;}setPausedMoment(false);setPlaying(!playing);if(!playing)requestAnimationFrame(()=>document.getElementById("console-preview")?.focus({preventScroll:true}));}}>{staticView?'Open the shell':playing?'Stop playing':'Try the screen'} <span>{playing?'×':'↗'}</span></button>{playing&&<button className="pill tactile pause-open" onClick={()=>{setPlaying(false);setPausedMoment(true);goChapter(2);}}>Pause &amp; open →</button>}<small className="caption">Browser mini-game · The desktop app runs your ROMs.</small></div>
          <div className={'story-copy '+(chapter===2?'is-active':'')} aria-hidden={chapter!==2} inert={chapter!==2}><p className="eyebrow">02 / UNDER THE SHELL</p><h2>There’s more<br/>to the magic.</h2><p>Every jump begins with an instruction. Memory holds the world. The picture and sound bring it to life.</p><div className="component-list"><span><i>01</i> The shell <b>Feel</b></span><span><i>02</i> The board <b>Think</b></span><span><i>03</i> The screen <b>Play</b></span></div><small className="caption">An illustrated view of handheld hardware.</small></div>
          <div className={'story-copy '+(chapter===3?'is-active':'')} aria-hidden={chapter!==3} inert={chapter!==3}><p className="eyebrow">03 / FOLLOW THE FRAME</p><h2>See what<br/>happens next.</h2><p><span className="desktop-story-text">Pause the desktop game. Step through an instruction. Watch the registers, tiles and sound channels reveal how a moment is made.</span><span className="phone-story-text">Pause a game. Follow its instructions, picture and sound in the desktop Inspector.</span></p><button className="pill tactile" onClick={inspectRecorded}>Inspect a recorded moment →</button><div className="mini-spec"><span>CPU</span><span>MEMORY</span><span>PICTURE</span><span>SOUND</span></div></div>
        </div>
        <div className="scene-caption"><span>{chapter<2?'MATCHABOY / POCKET STUDY':'MATCHABOY / INSIDE THE MACHINE'}</span>{playing?<div className="touch-controls">{[['left','←'],['right','→'],['action','Z / JUMP']].map(([key,label])=><button key={key} aria-label={key==='action'?'Jump':'Move '+key} onPointerDown={()=>gameInput(key,true)} onPointerUp={()=>gameInput(key,false)} onPointerLeave={()=>gameInput(key,false)}>{label}</button>)}</div>:<span>{chapter<2?'Drag to rotate ↔':<>{!staticView&&<button className="replay-opening" onClick={replayOpening}>Replay opening <span aria-hidden="true">↻</span></button>}</>}</span>}<button className="scene-mode" onClick={()=>{setPlaying(false);setFailed(false);setStill(!staticView);if(staticView)setSceneReady(false);}} disabled={reduced}>{reduced?'Still view':staticView?'Enable 3D':'Use still view'}</button></div>
        <nav className="journey-nav" aria-label="Console story">{['Play','Pick it up','Open the shell','Follow the frame'].map((label,i)=><button key={label} aria-current={chapter===i?'step':undefined} onClick={()=>goChapter(i)}><span>0{i+1}</span>{label}<i/></button>)}</nav>
      </div>
    </section>
    {reviewing&&<Suspense fallback={null}><ReviewTools seek={seekReview} setReduced={setReviewReduced} fail={()=>setFailed(true)}/></Suspense>}
    <Inspector request={inspectionRequest}/>
    <div className="lower-journey">
      <Arcade/>
      <section className="netplay-section" id="netplay"><div><p className="eyebrow">A LITTLE LESS DISTANCE</p><h2>Good games.<br/>Better together.</h2><p>Two consoles. One shared adventure. Open the same link-enabled game, host a session, and invite a friend.</p><a className="text-button" href={repo+'/blob/feature/original-arcade/NETPLAY.md'} target="_blank" rel="noreferrer">Read the connection guide ↗</a></div><div className="connection-guide"><div className="connection-line"><img src="/brand/mark.svg" alt="Your console"/><svg viewBox="0 0 300 70" aria-hidden="true"><path d="M0 35 C75 35 70 7 150 35 S225 35 300 35"/></svg><img src="/brand/mark.svg" alt="Your friend's console"/></div><div className="connection-columns"><div><b>01 / YOU HOST</b><p>Open your game.<br/>Choose Host Game.<br/>Share your room code.</p></div><div><b>02 / THEY JOIN</b><p>Open the same game.<br/>Enter your address and code.<br/>Use the game’s link mode.</p></div></div><small>Internet play needs a reachable address, a private VPN or UDP port forwarding. No matchmaking service. The included Tobu games are single-player.</small></div></section>
      <section className="making-of"><img src="/brand/mark.svg" alt=""/><div><p className="eyebrow">MADE WITH CURIOSITY</p><h3>The work is part of the story.</h3><p>Built with Astra in Codex, directed by the Matchaboy maintainer. Explore the original engine, Windows audio work and game experiments. GBA emulation is powered by mGBA.</p></div><a className="text-button" href={repo+'/blob/feature/original-arcade/games/PROVENANCE.md'} target="_blank" rel="noreferrer">Read the making-of ↗</a></section>
      <footer id="download"><div className="footer-top"><Mark light/><p>GOOD GAMES. GREENER DAYS.</p></div><h2>Play it.<br/><em>Understand it.</em></h2><div className="download-heading"><span>GET MATCHABOY</span><a href={repo+'/releases/tag/v0.1.0'}>v0.1.0 · Release notes ↗</a></div><div className="download-options">{[{id:'windows-x64',name:'Windows',arch:'10 / 11 · x64',note:'Unzip. Open Matchaboy.exe. No installer.'},{id:'macos-arm64',name:'macOS',arch:'13+ · Apple Silicon',note:'Unzip. Open Matchaboy.app. See Start Here for first-open guidance.'},{id:'linux-x64',name:'Linux',arch:'x64 · X11 desktop',note:'Unzip. Keep the assets folder alongside the player. Requires X11/Xft.'}].map(p=><a key={p.id} className="download-option tactile" href={repo+'/releases/download/v0.1.0/matchaboy-'+p.id+'.zip'}><span>{p.arch}<i>↗</i></span><b>{p.name}</b><p>{p.note}</p></a>)}</div><p className="release-note">This release includes Tobu Tobu Girl and Tobu Tobu Girl Deluxe by Tangram Games. Deluxe runs in Game Boy compatibility mode; full Game Boy Color emulation is not supported.</p><div className="legal"><span>© Matchaboy contributors · <a href={repo+'/blob/feature/original-arcade/LICENSE'}>GNU GPL v3</a></span><div><a href={repo}>Source</a><a href={repo+'/blob/feature/original-arcade/THIRD_PARTY.md'}>Credits & licenses</a><a href={repo+'/issues'}>Report a bug ↗</a></div><span>Bring your own legally obtained games. Not affiliated with Nintendo.</span></div></footer>
    </div>
  </main>;
}
export default App;

import {RoundedBox} from '@react-three/drei';
import {useEffect, useMemo} from 'react';
import * as THREE from 'three';
import {Batch, Lettering, type Detail} from './hardware-primitives';
import {plate, roundRect} from './model-geometry';

export const MOUNTS: [number,number][] = [[-1.38,2.30],[1.38,2.30],[-1.38,-2.30],[1.30,-2.30]];
const CHIPS = [
  {x:.10,y:.24,w:1.12,h:1.13,pins:16,quad:true},
  {x:-.32,y:-1.19,w:.80,h:.47,pins:10},
  {x:-1.09,y:.02,w:.36,h:.68,pins:7},
  {x:1.03,y:.92,w:.38,h:.43,pins:5},
  {x:.58,y:1.69,w:.40,h:.31,pins:5},
  {x:-.52,y:1.38,w:.35,h:.37,pins:4},
  {x:1.13,y:-.93,w:.31,h:.37,pins:4},
  {x:-1.07,y:-1.89,w:.37,h:.35,pins:4},
];

function IntegratedCircuit({x,y,w,h,pins,quad=false}: typeof CHIPS[number]) {
  const legs=useMemo(() => {
    const top:Detail[]=[], side:Detail[]=[];
    for(let i=0;i<pins;i++) for(const sign of [-1,1]) {
      top.push({position:[(i-(pins-1)/2)*(w-.09)/pins,sign*(h/2+.05),.075]});
      if(quad) side.push({position:[sign*(w/2+.05),(i-(pins-1)/2)*(h-.09)/pins,.075]});
    }
    return {top,side};
  },[w,h,pins,quad]);
  return <group position={[x,y,.047]}>
    <Batch items={legs.top} size={[.023,.125,.025]} color="#b4bbb0" metalness={.82} roughness={.24}/>
    {quad&&<Batch items={legs.side} size={[.125,.023,.025]} color="#b4bbb0" metalness={.82} roughness={.24}/>}
    <RoundedBox args={[w,h,.135]} radius={.025} smoothness={3} position={[0,0,.09]}><meshPhysicalMaterial color="#161e1c" roughness={.46} clearcoat={.12}/></RoundedBox>
    <mesh position={[-w*.34,h*.33,.159]}><circleGeometry args={[.024,16]}/><meshStandardMaterial color="#080f0d" roughness={.8}/></mesh>
    {quad?<><Lettering text="MATCHABOY" width={.68} position={[0,.04,.161]} color="#b6bdb0"/><Lettering text="MB-01" width={.35} position={[0,-.12,.161]} color="#929e90"/></>:w>.6?<><Lettering text="MATCHA" width={.34} position={[0,.055,.16]} color="#7c897b"/><Lettering text="SRAM" width={.39} position={[0,-.075,.16]} color="#758071"/></>:null}
  </group>;
}

// Original illustrative routing. Components and traces are authored together;
// the artwork is not represented as an electrically verified circuit diagram.
function boardArtwork() {
  const canvas=document.createElement('canvas'); canvas.width=1024; canvas.height=1600;
  const c=canvas.getContext('2d')!;
  const px=(x:number)=>(x/3.25+.5)*canvas.width, py=(y:number)=>(.5-y/5.1)*canvas.height;
  c.fillStyle='#104a35'; c.fillRect(0,0,1024,1600);
  const wash=c.createLinearGradient(0,0,1024,1600);wash.addColorStop(0,'#1c6144');wash.addColorStop(.5,'#103e2e');wash.addColorStop(1,'#1a5039');c.fillStyle=wash;c.fillRect(0,0,1024,1600);
  let seed=37;const random=()=>{seed=(seed*1664525+1013904223)>>>0;return seed/4294967296;};
  for(let i=0;i<14000;i++){c.fillStyle=`rgba(187,210,161,${random()*.07})`;c.fillRect(random()*1024,random()*1600,1,1);}
  const route=(points:number[][], bright=false)=>{
    c.beginPath();points.forEach(([x,y],i)=>i?c.lineTo(px(x),py(y)):c.moveTo(px(x),py(y)));
    c.lineWidth=bright?2.5:1.5;c.lineJoin='round';c.lineCap='round';c.strokeStyle=bright?'#81915a':'#4c7950';c.stroke();
    const end=points[points.length-1];c.beginPath();c.arc(px(end[0]),py(end[1]),3.5,0,Math.PI*2);c.strokeStyle='#b7a968';c.lineWidth=2;c.stroke();
  };
  // Dense fan-outs from the four sides of the processor, with staggered vias.
  for(let side of [-1,1]) for(let i=0;i<16;i++){
    const y=.24+(i-7.5)*.064,x=.10+side*.64;
    const lane=side*(.87+(i%4)*.12),outY=side===1?1.30+(i%6)*.14:-.94-(i%6)*.12;
    route([[x,y],[lane,y],[lane+side*.075,y+(outY>y?.075:-.075)],[lane+side*.075,outY],[lane-side*.11,outY+.10]],i%3===0);
    const pinX=.1+(i-7.5)*.064,sy=.24+side*.65,laneY=sy+side*(.17+(i%4)*.065),endY=side>0?1.70+(i%3)*.14:-1.68-(i%3)*.13;
    route([[pinX,sy],[pinX,laneY],[pinX+side*.13,laneY+side*.13],[pinX+side*.13,endY]],i%4===0);
  }
  for(let i=0;i<32;i++){
    const x=-1.32+(i%8)*.365,y=-2.15+Math.floor(i/8)*1.3;
    route([[x,y],[x+.13,y],[x+.20,y+.09],[x+.20,y+.28]],i%4===0);
  }
  c.strokeStyle='#adbca2';c.fillStyle='#b5c1a8';c.lineWidth=1.6;c.font='13px monospace';
  for(let i=0;i<CHIPS.length;i++) {const {x,y,w,h}=CHIPS[i];c.strokeRect(px(x-w/2-.10),py(y+h/2+.10),(w+.2)/3.25*1024,(h+.2)/5.1*1600);c.fillText('U'+(i+1),px(x-w/2),py(y+h/2+.17));}
  for(let i=0;i<38;i++)c.fillText((i%2?'R':'C')+(i+1),px(-1.4+(i%7)*.41),py(1.98-Math.floor(i/7)*.77));
  c.font='18px monospace';c.fillText('MATCHABOY  /  MB-01',px(-.98),py(-2.35));
  const texture=new THREE.CanvasTexture(canvas);texture.colorSpace=THREE.SRGBColorSpace;texture.anisotropy=8;
  return texture;
}

export function PrintedCircuitBoard(){
  const artwork=useMemo(boardArtwork,[]);
  const boardGeo=useMemo(()=>{const shape=roundRect(3.25,5.1,.09);for(const [x,y] of MOUNTS){const hole=new THREE.Path();hole.absarc(x,y,.095,0,Math.PI*2,false);shape.holes.push(hole);}return plate(shape,.075,.008);},[]);
  const faceGeo=useMemo(()=>{const shape=roundRect(3.25,5.1,.09);for(const [x,y] of MOUNTS){const hole=new THREE.Path();hole.absarc(x,y,.095,0,Math.PI*2,false);shape.holes.push(hole);}const geo=new THREE.ShapeGeometry(shape);const p=geo.attributes.position,uv=geo.attributes.uv;for(let i=0;i<p.count;i++)uv.setXY(i,p.getX(i)/3.25+.5,p.getY(i)/5.1+.5);return geo;},[]);
  const passives=useMemo(()=>{
    const items:Detail[]=[],ends:Detail[]=[];
    const clusters=[[-1.13,1.83,5],[.08,1.92,5],[1.20,1.69,4],[-1.26,-.76,4],[.80,-.40,4],[.10,-2.10,5],[-.15,.99,4],[1.22,-1.47,4]];
    clusters.forEach(([x,y,n],cluster)=>{for(let i=0;i<n;i++){
      const vertical=cluster%2===0,px=x+(i%2)*.20,py=y-Math.floor(i/2)*.22;
      items.push({position:[px,py,.087],rotation:[0,0,vertical?Math.PI/2:0]});
      for(const s of [-1,1])ends.push({position:[px+(vertical?0:s*.058),py+(vertical?s*.058:0),.089],rotation:[0,0,vertical?Math.PI/2:0]});
    }});return {items,ends};
  },[]);
  useEffect(()=>()=>artwork.dispose(),[artwork]);
  return <group>
    <mesh geometry={boardGeo}><meshStandardMaterial color="#65713c" roughness={.61}/></mesh>
    <mesh geometry={faceGeo} position={[0,0,.046]}><meshPhysicalMaterial map={artwork} roughness={.37} metalness={.12} clearcoat={.3} clearcoatRoughness={.44}/></mesh>
    <Batch items={MOUNTS.map(([x,y])=>({position:[x,y,.050]}))} size={[.096,.16,40]} color="#c7b777" roughness={.28} metalness={.78} ring/>
    {CHIPS.map((chip,i)=><IntegratedCircuit key={i} {...chip}/>)}
    <Batch items={passives.items} size={[.13,.068,.06]} color="#b5ad87" roughness={.64} metalness={.06}/>
    <Batch items={passives.ends} size={[.026,.073,.064]} color="#babfb3" roughness={.27} metalness={.84}/>
    {[[-.88,1.78],[-.74,1.06],[.71,1.15]].map(([x,y],i)=><group key={i} position={[x,y,.12]}><mesh rotation={[Math.PI/2,0,0]}><cylinderGeometry args={[.091,.096,.20,24]}/><meshStandardMaterial color="#2e6151" roughness={.34}/></mesh><mesh position={[0,0,.102]}><circleGeometry args={[.081,24]}/><meshStandardMaterial color="#c4c6b8" roughness={.24} metalness={.85}/></mesh><mesh position={[0,0,.106]}><boxGeometry args={[.13,.009,.006]}/><meshStandardMaterial color="#657063"/></mesh></group>)}
    <group position={[.73,-1.74,.15]}>
      <mesh rotation={[Math.PI/2,0,0]}><cylinderGeometry args={[.43,.45,.20,64]}/><meshPhysicalMaterial color="#1d2521" roughness={.35} metalness={.38}/></mesh>
      <mesh position={[0,0,.112]}><torusGeometry args={[.375,.038,12,64]}/><meshStandardMaterial color="#5b655b" roughness={.32} metalness={.72}/></mesh>
      <mesh position={[0,0,.115]}><circleGeometry args={[.334,64]}/><meshStandardMaterial color="#303932" roughness={.49}/></mesh>
      <mesh position={[0,0,.117]}><torusGeometry args={[.12,.012,8,32]}/><meshStandardMaterial color="#1f2822" roughness={.5}/></mesh>
    </group>
    {/* Metal port cages project beyond the edge of the board. */}
    {[1.35,-.40,-1.77].map((y,i)=><group key={y} position={[1.64,y,-.035]}><RoundedBox args={[.25,.40,.26]} radius={.03} smoothness={3}><meshStandardMaterial color="#a1aba1" metalness={.82} roughness={.26}/></RoundedBox><mesh position={[.015,0,.137]}><planeGeometry args={[.16,.27]}/><meshStandardMaterial color="#17221c" roughness={.7}/></mesh></group>)}
    <RoundedBox args={[2.36,.24,.20]} radius={.02} position={[0,2.26,-.15]}><meshStandardMaterial color="#1e2722" roughness={.5}/></RoundedBox>
  </group>;
}

export function RearHousing(){
  const geometries=useMemo(()=>{
    const outer=roundRect(3.68,5.7,.25,0,0,.72);
    outer.holes.push(roundRect(3.35,5.37,.16,0,0,.57));
    const inner=roundRect(3.31,5.32,.16,0,0,.55);
    const battery=roundRect(2.69,1.69,.08,0,-1.21);
    battery.holes.push(roundRect(2.47,1.47,.04,0,-1.21));
    return {rim:plate(outer,.53,.025),floor:plate(inner,.065,.014),battery:plate(battery,.19,.016)};
  },[]);
  const plastic='#c5c6b8',inner='#a4aa9a';
  return <group>
    <mesh geometry={geometries.rim}><meshPhysicalMaterial color={plastic} roughness={.36} clearcoat={.17}/></mesh>
    <mesh geometry={geometries.floor} position={[0,0,-.24]}><meshStandardMaterial color={inner} roughness={.55}/></mesh>
    <mesh geometry={geometries.battery} position={[0,0,-.05]}><meshStandardMaterial color="#c0c2b3" roughness={.43}/></mesh>
    <RoundedBox args={[2.41,1.43,.052]} radius={.025} position={[0,-1.21,-.16]}><meshStandardMaterial color="#8d9786" roughness={.6}/></RoundedBox>
    <mesh position={[0,-1.21,-.055]}><boxGeometry args={[.055,1.39,.17]}/><meshStandardMaterial color="#bfc2b2" roughness={.48}/></mesh>
    {[-1,1].map(side=><group key={side}>
      <RoundedBox args={[.06,4.85,.19]} radius={.012} position={[side*1.45,.03,-.13]}><meshStandardMaterial color="#b8bcab" roughness={.49}/></RoundedBox>
      {[-1.80,-.35,.74,1.72].map((y,i)=><group key={y} position={[side*1.36,y,-.115]}>
        <RoundedBox args={[.25,.075,.22]} radius={.015}><meshStandardMaterial color={plastic} roughness={.48}/></RoundedBox>
        {i%2===0&&<mesh position={[-side*.07,.08,.065]}><boxGeometry args={[.055,.19,.10]}/><meshStandardMaterial color={plastic} roughness={.47}/></mesh>}
      </group>)}
    </group>)}
    {[-2.15,-.24,1.73].map(y=><RoundedBox key={y} args={[2.85,.055,.12]} radius={.012} position={[0,y,-.17]}><meshStandardMaterial color="#b9bcad" roughness={.49}/></RoundedBox>)}
    {MOUNTS.map(([x,y],i)=><group key={i} position={[x,y,-.055]}>
      <mesh rotation={[Math.PI/2,0,0]}><cylinderGeometry args={[.15,.19,.46,32]}/><meshStandardMaterial color={plastic} roughness={.43}/></mesh>
      <mesh position={[0,0,.236]}><circleGeometry args={[.073,24]}/><meshStandardMaterial color="#333d32" roughness={.8}/></mesh>
      <mesh position={[0,0,.24]}><ringGeometry args={[.071,.139,32]}/><meshStandardMaterial color="#d8dacc" roughness={.36}/></mesh>
      <mesh position={[0,0,.241]}><ringGeometry args={[.062,.08,24]}/><meshStandardMaterial color="#a4a795" metalness={.45} roughness={.5}/></mesh>
    </group>)}
    {[[-1.29,.91],[1.28,.02]].map(([x,y],i)=><group key={i} position={[x,y,-.13]}><mesh rotation={[Math.PI/2,0,0]}><cylinderGeometry args={[.09,.13,.25,24]}/><meshStandardMaterial color={plastic} roughness={.5}/></mesh><mesh position={[0,0,.13]}><circleGeometry args={[.035,16]}/><meshStandardMaterial color="#4b5546"/></mesh></group>)}
    {[-.64,.64].map((x,i)=><group key={x} position={[x,-1.21,-.066]}>
      <Lettering text={i?'-':'+'} width={.16} position={[0,0,.016]} color="#465340"/>
      {[-1,1].map(sign=><group key={sign} position={[0,sign*.55,.04]}>
        <RoundedBox args={[.23,.17,.037]} radius={.015}><meshStandardMaterial color="#c1c6b9" metalness={.88} roughness={.24}/></RoundedBox>
        <mesh position={[0,0,.024]}><boxGeometry args={[.18,.017,.02]}/><meshStandardMaterial color="#5c695a" metalness={.7}/></mesh>
        <mesh position={[0,-sign*.05,.055]}><boxGeometry args={[.13,.036,.085]}/><meshStandardMaterial color="#c6ccbe" metalness={.85} roughness={.24}/></mesh>
      </group>)}
    </group>)}
    <Lettering text="MATCHABOY" position={[-.20,.99,-.178]} width={1.25} color="#56604e"/>
    <Lettering text="PLAY" position={[-.65,.52,-.177]} width={.33} color="#637059"/>
    <Lettering text="LEARN" position={[-.57,.30,-.177]} width={.47} color="#637059"/>
    <Lettering text="UNDERSTAND" position={[-.33,.08,-.177]} width={.94} color="#637059"/>
    {[-.93,.1,1.04].map(x=><RoundedBox key={x} args={[.16,.16,.15]} radius={.018} position={[x,2.55,.11]}><meshStandardMaterial color={plastic} roughness={.48}/></RoundedBox>)}
  </group>;
}

export function Fastener(){
  const thread=useMemo(()=>{
    const pts=Array.from({length:100},(_,i)=>{const a=i/99*Math.PI*12;return new THREE.Vector3(.043*Math.cos(a),.043*Math.sin(a),-.29+i/99*.25);});
    return new THREE.TubeGeometry(new THREE.CatmullRomCurve3(pts),100,.009,5,false);
  },[]);
  return <group>
    <mesh rotation={[Math.PI/2,0,0]} position={[0,0,-.14]}><cylinderGeometry args={[.035,.032,.30,20]}/><meshStandardMaterial color="#aeb6af" metalness={.92} roughness={.25}/></mesh>
    <mesh geometry={thread}><meshStandardMaterial color="#c7cec5" metalness={.9} roughness={.23}/></mesh>
    <mesh rotation={[Math.PI/2,0,0]} position={[0,0,.025]}><cylinderGeometry args={[.085,.073,.060,32]}/><meshStandardMaterial color="#cbd0c7" metalness={.92} roughness={.24}/></mesh>
    <mesh position={[0,0,.057]}><boxGeometry args={[.094,.019,.005]}/><meshStandardMaterial color="#37443b" roughness={.7}/></mesh>
    <mesh position={[0,0,.058]}><boxGeometry args={[.019,.094,.005]}/><meshStandardMaterial color="#37443b" roughness={.7}/></mesh>
  </group>;
}

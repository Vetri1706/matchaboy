import {Environment,Lightformer,RoundedBox} from '@react-three/drei';
import {Canvas,useFrame,useThree,invalidate} from '@react-three/fiber';
import {useEffect,useMemo,useRef,useState,type MutableRefObject} from 'react';
import * as THREE from 'three';
import {createGameTexture} from './game-texture';
import {roundRect,plate,shellGeometry,bezelGeometry,dpadGeometry} from './model-geometry';
import {assemblyPose,createAssemblyFramer,type AssemblyPose} from './assembly-motion';
import {SceneRenderer,reviewMode} from './SceneRenderer';
import {Lettering} from './hardware-primitives';
import {PrintedCircuitBoard,RearHousing,Fastener,MOUNTS} from './HardwareDetails';

type Props={progress:MutableRefObject<number>;cursorVelocity:MutableRefObject<number>;onReady:()=>void;reduced:boolean;playing:boolean;frozen:boolean};
const vertex=`
varying vec2 vUv;
void main(){vUv=uv;vec3 p=position;vec2 c=uv-.5;p.z-=dot(c,c)*.025;gl_Position=projectionMatrix*modelViewMatrix*vec4(p,1.);}
`;
const fragment=`
uniform sampler2D uTexture;uniform float uVelocity;varying vec2 vUv;
void main(){
  vec2 c=vUv-.5;vec2 uv=vUv+c*dot(c,c)*.025;
  if(uv.x<0.||uv.x>1.||uv.y<0.||uv.y>1.)discard;
  float shift=.0002+min(uVelocity,1.)*.0006;
  vec3 col=vec3(texture2D(uTexture,uv+vec2(shift,0.)).r,texture2D(uTexture,uv).g,texture2D(uTexture,uv-vec2(shift,0.)).b);
  col*=.985+.015*sin(uv.y*144.*3.14159);
  col*=1.-dot(c,c)*.16;
  gl_FragColor=vec4(col,1.);
  #include <colorspace_fragment>
}`;

function LiveScreen({playing,reduced,frozen}:{playing:boolean;reduced:boolean;frozen:boolean}){
 const game=useMemo(createGameTexture,[]),ref=useRef<THREE.ShaderMaterial>(null),idle=useRef(0),drawn=useRef(false);
 const uniforms=useMemo(()=>({uTexture:{value:game.texture},uVelocity:{value:0}}),[game]);
 useEffect(()=>{
   const map=(key:string)=>['ArrowLeft','a','A'].includes(key)?'left':['ArrowRight','d','D'].includes(key)?'right':[' ','z','Z','ArrowUp'].includes(key)?'action':null;
   const update=(e:KeyboardEvent,active:boolean)=>{const key=map(e.key);if(!active&&key){game.setKey(key,false);return;}if(!playing||(e.target instanceof HTMLElement&&e.target.closest('button,a,input,textarea')))return;if(key){game.setKey(key,true);e.preventDefault();}};
   const down=(e:KeyboardEvent)=>update(e,true),up=(e:KeyboardEvent)=>update(e,false),reset=()=>{game.setKey('left',false);game.setKey('right',false);game.setKey('action',false);};
   const input=(e:Event)=>{const {key,active}=(e as CustomEvent).detail;if(playing&&['left','right','action'].includes(key))game.setKey(key,active);};
   window.addEventListener('keydown',down);window.addEventListener('keyup',up);window.addEventListener('blur',reset);window.addEventListener('game-input',input);
   reset();return()=>{window.removeEventListener('keydown',down);window.removeEventListener('keyup',up);window.removeEventListener('blur',reset);window.removeEventListener('game-input',input);reset();};
 },[game,playing]);
 useFrame((_,delta)=>{idle.current+=delta;if(!drawn.current||(!frozen&&(!reduced||playing||idle.current>1))){game.draw(frozen||reduced&&!playing?0:Math.min(delta,.04));drawn.current=true;idle.current=0;}});
 return <mesh position={[0,.78,.225]}><planeGeometry args={[2.1,1.89,12,12]}/><shaderMaterial ref={ref} vertexShader={vertex} fragmentShader={fragment} uniforms={uniforms}/></mesh>;
}
function AssemblyAccents({pose}:{pose:MutableRefObject<AssemblyPose>}){
 const screws=useRef<THREE.Group>(null),lines=useRef<THREE.LineSegments>(null),shadows=useRef<THREE.Group>(null);
 const mounts=useMemo(()=>[...MOUNTS,[1.38,0]] as [number,number][],[]);
 const geometry=useMemo(()=>{const g=new THREE.BufferGeometry();g.setAttribute('position',new THREE.BufferAttribute(new Float32Array(5*3*2*3),3));return g;},[]);
 const scratch=useMemo(()=>({point:new THREE.Vector3(),quaternion:new THREE.Quaternion(),euler:new THREE.Euler()}),[]);
 useFrame(()=>{
  const p=pose.current,opacity=THREE.MathUtils.smoothstep(p.reveal,.02,.50);
  if(screws.current){screws.current.visible=opacity>.001;screws.current.position.set(...p.rear.position);screws.current.rotation.set(...p.rear.rotation);screws.current.children.forEach((s,i)=>{s.position.set(mounts[i][0]+opacity*1.10,mounts[i][1],-.35-opacity*.12);s.rotation.set(0,Math.PI/2,0);s.children[0].rotation.z=opacity*Math.PI*2;});}
  if(lines.current){
    lines.current.visible=opacity>.001;(lines.current.material as THREE.LineDashedMaterial).opacity=opacity*.32;
    const positions=geometry.attributes.position;let n=0;
    for(let i=0;i<5;i++){
      const [x,y]=mounts[i];
      const points=[{part:p.front,point:[x,y,-.3]},{part:p.board,point:[x,y,.04]},{part:p.rear,point:[x,y,.16]},{part:p.rear,point:[x+opacity*1.10,y,-.35-opacity*.12]}];
      for(let j=0;j<3;j++) for(const entry of [points[j],points[j+1]]){
        scratch.quaternion.setFromEuler(scratch.euler.set(...entry.part.rotation));
        scratch.point.set(entry.point[0],entry.point[1],entry.point[2]).applyQuaternion(scratch.quaternion);
        positions.setXYZ(n++,scratch.point.x+entry.part.position[0],scratch.point.y+entry.part.position[1],scratch.point.z+entry.part.position[2]);
      }
    }
    positions.needsUpdate=true;lines.current.computeLineDistances();
  }
  if(shadows.current)shadows.current.children.forEach((node,i)=>{const part=[p.front,p.board,p.rear][i];node.position.set(part.position[0],-3.25,part.position[2]-.10);});
 });
 const shadowMaterial=useMemo(()=>new THREE.ShaderMaterial({transparent:true,depthWrite:false,side:THREE.DoubleSide,uniforms:{},vertexShader:'varying vec2 vUv;void main(){vUv=uv;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.);}',fragmentShader:'varying vec2 vUv;void main(){vec2 p=(vUv-.5)*2.;float r=dot(p,p);float a=exp(-r*4.5)*smoothstep(1.,.3,r)*.34;gl_FragColor=vec4(.015,.035,.025,a);}'}),[]);
 useEffect(()=>()=>{geometry.dispose();shadowMaterial.dispose();},[geometry,shadowMaterial]);
 return <>
  <lineSegments ref={lines} geometry={geometry} frustumCulled={false}><lineDashedMaterial color="#cbd4c6" dashSize={.065} gapSize={.055} transparent opacity={0} depthWrite={false}/></lineSegments>
  <group ref={screws}>{mounts.map((_,i)=><group key={i}><group><Fastener/></group></group>)}</group>
  <group ref={shadows}>{[0,1,2].map(i=><mesh key={i} rotation={[-Math.PI/2,0,0]} material={shadowMaterial} receiveShadow={false} castShadow={false}><planeGeometry args={[5.1,3.4]}/></mesh>)}</group>
 </>;
}

function Device({progress,onReady,reduced,playing,frozen,rotation}:{rotation:MutableRefObject<{x:number;y:number}>}&Props){
 const root=useRef<THREE.Group>(null),interaction=useRef<THREE.Group>(null),front=useRef<THREE.Group>(null),rear=useRef<THREE.Group>(null),board=useRef<THREE.Group>(null),{camera,size,gl}=useThree();
 const keyLight=useRef<THREE.DirectionalLight>(null),rimLight=useRef<THREE.DirectionalLight>(null),fillLight=useRef<THREE.HemisphereLight>(null);
 const framer=useMemo(createAssemblyFramer,[]),smoothRotation=useRef({x:0,y:0});
 const currentPose=useRef(assemblyPose(0));
 const frontGeo=useMemo(shellGeometry,[]),bezelGeo=useMemo(bezelGeometry,[]),padGeo=useMemo(dpadGeometry,[]);
 const frontLip=useMemo(()=>{const s=roundRect(3.62,5.64,.23,0,0,.69);s.holes.push(roundRect(3.40,5.42,.16,0,0,.58));return plate(s,.23,.017);},[]);
 useEffect(()=>{root.current?.traverse(o=>{if(o instanceof THREE.Mesh){const m=o.material as THREE.Material;o.castShadow=!m.transparent&&m.type!=='ShaderMaterial';o.receiveShadow=true;}});},[]);
 useFrame((_,delta)=>{
   const dt=Math.min(delta,.05),time=progress.current;
   smoothRotation.current.x=THREE.MathUtils.damp(smoothRotation.current.x,rotation.current.x,10,dt);
   smoothRotation.current.y=THREE.MathUtils.damp(smoothRotation.current.y,rotation.current.y,10,dt);
   if(Math.abs(smoothRotation.current.x-rotation.current.x)+Math.abs(smoothRotation.current.y-rotation.current.y)>.00001)invalidate();
   const phase=time<.38?"assembled":time<.47?"rear-release":time<.60?"front-release":time<.74?"board-reveal":"settled"; if(gl.domElement.dataset.assemblyPhase!==phase)gl.domElement.dataset.assemblyPhase=phase; const pose=assemblyPose(time,smoothRotation.current,reduced);
   currentPose.current=pose;
   root.current?.rotation.set(...pose.rotation);interaction.current?.rotation.set(...pose.interaction);
   if(reviewMode){gl.domElement.dataset.progress=time.toFixed(4);gl.domElement.dataset.pose=JSON.stringify(pose);}
   for(const [node,part] of [[front.current,pose.front],[board.current,pose.board],[rear.current,pose.rear]] as const){
    node?.position.set(...part.position);node?.rotation.set(...part.rotation);
   }
   const framing=framer(pose,size.width/size.height);
   camera.position.copy(framing.position);camera.lookAt(framing.target);

 },-1);
 return <>
  <ambientLight intensity={.15}/><hemisphereLight ref={fillLight} args={['#f6f5ef','#30392f',.48]}/>
  <directionalLight ref={keyLight} castShadow position={[-3,6,8]} intensity={2.8} color="#fff5e7" shadow-mapSize={[2048,2048]} shadow-camera-left={-10} shadow-camera-right={10} shadow-camera-top={10} shadow-camera-bottom={-10} shadow-normalBias={.018} shadow-bias={-.0001} shadow-radius={3}/>
  <directionalLight ref={rimLight} position={[4,2,-5]} intensity={2.1} color="#d5e7ed"/>
  <Environment frames={1} resolution={128} environmentIntensity={.55}><Lightformer form="rect" intensity={4} position={[-3,4,6]} scale={[4,7,1]} target={[0,0,0]}/><Lightformer form="rect" intensity={2} position={[4,1,5]} scale={[1.2,6,1]} target={[0,0,0]}/><Lightformer form="rect" color="#d4e2ee" intensity={3} position={[1,-3,-5]} scale={[5,2,1]} target={[0,0,0]}/></Environment>
  <group ref={root} name="Presentation"><group ref={interaction} name="Interaction">
   <AssemblyAccents pose={currentPose}/>
   <group ref={rear} name="BackShell"><RearHousing/></group>
   <group ref={board} name="MainBoard"><PrintedCircuitBoard/></group>
   <group ref={front} name="FrontAssembly"><group name="FrontShell">
    <mesh geometry={frontGeo}><meshPhysicalMaterial color="#d4d2c3" roughness={.48} clearcoat={.12} clearcoatRoughness={.48}/></mesh>
    <mesh geometry={frontLip} position={[0,0,-.26]}><meshPhysicalMaterial color="#c3c4b8" roughness={.38} clearcoat={.15}/></mesh>
    </group><group name="ScreenAssembly"><mesh geometry={bezelGeo} position={[0,0,.19]}><meshPhysicalMaterial color="#323f3b" roughness={.32} clearcoat={.36} clearcoatRoughness={.28}/></mesh>
    <RoundedBox args={[2.25,2.04,.023]} radius={.035} smoothness={3} position={[0,.78,.192]}><meshStandardMaterial color="#131e18" roughness={.45}/></RoundedBox>
    <LiveScreen playing={playing} reduced={reduced} frozen={frozen}/>
    <RoundedBox args={[2.17,1.96,.018]} radius={.033} smoothness={3} position={[0,.78,.248]}><meshPhysicalMaterial color="#d8e4df" roughness={.075} metalness={.15} clearcoat={1} clearcoatRoughness={.08} transparent opacity={.085} depthWrite={false}/></RoundedBox>
    <Lettering text="DOT MATRIX / STEREO" position={[0,2.04,.284]} width={2.1} color="#c9cfbd"/>
    <mesh position={[-1.35,1.12,.274]}><circleGeometry args={[.038,24]}/><meshStandardMaterial color="#e96939" emissive="#c84123" emissiveIntensity={.65}/></mesh>
    <Lettering text="POWER" position={[-1.32,.96,.285]} width={.27} color="#c3cbb9"/>
    </group><Lettering text="matchaboy" position={[-.61,-.80,.195]} width={1.68} color="#354c37"/>
    <group name="Controls"><group position={[-.95,-1.48,.20]}>
     <mesh geometry={padGeo} position={[0,0,.068]}><meshPhysicalMaterial color="#202b25" roughness={.65} clearcoat={.06}/></mesh>
     <mesh position={[0,0,.165]}><circleGeometry args={[.1,32]}/><meshStandardMaterial color="#17221b" roughness={.7}/></mesh>
     {[0,1,2,3].map(i=><group key={i} rotation={[0,0,i*Math.PI/2]}><mesh position={[0,.4,.16]}><boxGeometry args={[.15,.014,.01]}/><meshStandardMaterial color="#4e5c4b" roughness={.7}/></mesh></group>)}
    </group>
    {[[.63,-1.53,'B'],[1.21,-1.18,'A']].map(([x,y,label])=><group key={label} position={[x as number,y as number,.198]}>
     <mesh position={[0,0,-.075]} rotation={[Math.PI/2,0,0]}><cylinderGeometry args={[.319,.319,.15,48,1,true]}/><meshStandardMaterial color="#7a8375" roughness={.6} side={THREE.DoubleSide}/></mesh>
     <mesh><ringGeometry args={[.303,.332,48]}/><meshStandardMaterial color="#b4b8a9" roughness={.48}/></mesh>
     <mesh position={[0,0,.071]} rotation={[Math.PI/2,0,0]}><cylinderGeometry args={[.277,.286,.126,64]}/><meshPhysicalMaterial color="#72344b" roughness={.3} clearcoat={.42} clearcoatRoughness={.25}/></mesh>
     <mesh position={[0,0,.137]}><circleGeometry args={[.269,64]}/><meshStandardMaterial color="#79354d" roughness={.3}/></mesh>
     <Lettering text={String(label)} position={[.08,-.46,.003]} width={.14} color="#283e2a"/>
    </group>)}
    {[-.47,.20].map((x,i)=><group key={i} position={[x,-2.31,.186]} rotation={[0,0,.14]}>
     <RoundedBox args={[.52,.19,.017]} radius={.08} smoothness={4}><meshStandardMaterial color="#8b9680" roughness={.8}/></RoundedBox>
     <RoundedBox args={[.44,.123,.09]} radius={.055} smoothness={4} position={[0,0,.04]}><meshStandardMaterial color="#525c49" roughness={.62}/></RoundedBox>
     <Lettering text={i?'START':'SELECT'} position={[0,-.22,.015]} width={.49} color="#344833"/>
    </group>)}
    </group><mesh position={[1.1,-2.22,-.143]}><planeGeometry args={[1.35,.97]}/><meshBasicMaterial color="#1b2920"/></mesh>
    <mesh position={[0,2.84,-.02]}><boxGeometry args={[1.1,.08,.20]}/><meshStandardMaterial color="#333e30" roughness={.55}/></mesh>

   </group>
  </group></group>
  <SceneRenderer onReady={onReady}/>
 </>;
}
export default function HandheldScene(props:Props){
 const rotation=useRef({x:0,y:0}),drag=useRef<{x:number;y:number;rx:number;ry:number}|null>(null),surface=useRef<HTMLDivElement>(null),[visible,setVisible]=useState(true);
 useEffect(()=>{const observer=new IntersectionObserver(([entry])=>setVisible(entry.isIntersecting));if(surface.current)observer.observe(surface.current);return()=>observer.disconnect();},[]);
 return <div ref={surface} className="scene-interaction" onPointerDown={e=>{if(e.pointerType!=='mouse')return;drag.current={x:e.clientX,y:e.clientY,rx:rotation.current.x,ry:rotation.current.y};e.currentTarget.setPointerCapture(e.pointerId);}} onPointerMove={e=>{if(!drag.current)return;rotation.current={y:THREE.MathUtils.clamp(drag.current.ry+(e.clientX-drag.current.x)*.006,-.65,.65),x:THREE.MathUtils.clamp(drag.current.rx+(e.clientY-drag.current.y)*.005,-.4,.4)};invalidate();}} onPointerUp={()=>{drag.current=null;}} onPointerCancel={()=>{drag.current=null;}}>
  <Canvas frameloop={visible?(props.frozen?'demand':'always'):'never'} shadows="percentage" camera={{position:[0,0,18],fov:32,near:.1,far:120}} dpr={[1,2]} gl={{alpha:true,antialias:true,preserveDrawingBuffer:reviewMode,powerPreference:'high-performance',toneMapping:THREE.ACESFilmicToneMapping,toneMappingExposure:1.1}}><Device {...props} rotation={rotation}/></Canvas>
 </div>;
}

import * as THREE from 'three';
import {RoundedBoxGeometry} from './vendor/RoundedBoxGeometry.js';

/** An explorable product illustration, not a hardware schematic or browser emulator. */
export function createConsoleScene(canvas, stage, {paused = false} = {}) {
  const renderer = new THREE.WebGLRenderer({canvas, antialias: true, alpha: false, powerPreference: 'low-power'});
  renderer.setPixelRatio(Math.min(devicePixelRatio, 1.6));
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.15;
  renderer.setClearColor('#060c09');
  renderer.shadowMap.enabled=true;renderer.shadowMap.type=THREE.PCFShadowMap;
  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(33, 1, .1, 60);
  camera.position.set(0, 0, 12.7);
  const machine = new THREE.Group(); scene.add(machine);
  const front = new THREE.Group(), lcd = new THREE.Group(), pcb = new THREE.Group(), rear = new THREE.Group();
  machine.add(front, lcd, pcb, rear);
  const materials = [], textures = [];
  const mat = (color, roughness = .4, metalness = .15, extra = {}) => {
    const m = new THREE.MeshStandardMaterial({color, roughness, metalness, ...extra}); materials.push(m); return m;
  };
  const shell = mat('#153629', .29, .32);
  const shellDark=new THREE.Color('#153629'),shellOpen=new THREE.Color('#527a63');
  const edge = mat('#357151', .25, .45);
  const bezel = mat('#06140e', .22, .38);
  const black = mat('#07140f', .38, .2);
  const green = mat('#17452f', .23, .38);
  const led = mat('#32ef87', .2, .1, {emissive:'#15c768',emissiveIntensity:1});
  const gold = mat('#a69551', .24, .8);
  const screw = mat('#7caa91', .26, .85);
  const silicon = mat('#061d13', .46, .25);
  function mesh(geometry, material, group, x=0, y=0, z=0) {
    const m = new THREE.Mesh(geometry, material);m.castShadow=true;m.receiveShadow=true; m.position.set(x,y,z); group.add(m); return m;
  }
  function box(w,h,d,r,material,group,x=0,y=0,z=0) {
    return mesh(new RoundedBoxGeometry(w,h,d,3,r),material,group,x,y,z);
  }
  function disk(r,d,material,group,x,y,z) {
    const m = mesh(new THREE.CylinderGeometry(r,r,d,40),material,group,x,y,z); m.rotation.x=Math.PI/2;return m;
  }
  function contour(w,h,r,cx=0,cy=0,path=new THREE.Shape()) {
    const x=cx-w/2,y=cy-h/2;
    path.moveTo(x+r,y);path.lineTo(x+w-r,y);path.quadraticCurveTo(x+w,y,x+w,y+r);
    path.lineTo(x+w,y+h-r);path.quadraticCurveTo(x+w,y+h,x+w-r,y+h);
    path.lineTo(x+r,y+h);path.quadraticCurveTo(x,y+h,x,y+h-r);
    path.lineTo(x,y+r);path.quadraticCurveTo(x,y,x+r,y);return path;
  }
  function frame(w,h,r,holeW,holeH,holeY,depth,material,group,z) {
    const shape=contour(w,h,r);
    shape.holes.push(contour(holeW,holeH,.055,0,holeY,new THREE.Path()));
    return mesh(new THREE.ExtrudeGeometry(shape,{depth,bevelEnabled:true,bevelSegments:3,steps:1,bevelSize:.025,bevelThickness:.025,curveSegments:12}),material,group,0,0,z);
  }
  function label(text,w,h,color,group,x,y,z,size=60) {
    const c=document.createElement('canvas');c.width=1024;c.height=160;const ctx=c.getContext('2d');
    ctx.fillStyle=color;ctx.font=`600 ${size}px Arial`;ctx.textAlign='center';ctx.textBaseline='middle';ctx.fillText(text,512,80);
    const t=new THREE.CanvasTexture(c);t.colorSpace=THREE.SRGBColorSpace;textures.push(t);
    const material=new THREE.MeshBasicMaterial({map:t,transparent:true,depthWrite:false});materials.push(material);
    const textMesh=mesh(new THREE.PlaneGeometry(w,h),material,group,x,y,z);textMesh.castShadow=false;return textMesh;
  }

  // A genuinely open chassis: the display travels on its own layer during the autopsy.
  frame(3.2,4.96,.22,2.3,2.09,.83,.17,shell,front,.02);
  const displayFrame=frame(2.79,2.63,.15,2.27,2.045,.0,.07,bezel,front,.225);
  displayFrame.position.y=.83;
  label('MATCHABOY',2.35,.27,'#91c9a8',front,0,2.04,.32,49);
  label('DOT MATRIX WITH STEREO SOUND',2.3,.15,'#658471',front,.03,-.375,.32,32);
  label('matchaboy',1.7,.28,'#9acdae',front,-.51,-.71,.22,73);
  disk(.038,.035,led,front,-1.26,1.49,.31);
  label('ON',.25,.11,'#99b8a7',front,-1.255,1.32,.322,55);
  disk(.53,.025,bezel,front,-.88,-1.32,.23);
  box(.9,.28,.19,.04,black,front,-.88,-1.32,.3);
  box(.28,.9,.19,.04,black,front,-.88,-1.32,.305);
  disk(.105,.025,bezel,front,-.88,-1.32,.414);
  disk(.275,.04,bezel,front,.91,-1.12,.23);disk(.275,.04,bezel,front,.33,-1.43,.23);
  disk(.218,.17,green,front,.91,-1.12,.31);disk(.218,.17,green,front,.33,-1.43,.31);
  label('A',.22,.14,'#83b296',front,1.04,-1.49,.23);label('B',.22,.14,'#83b296',front,.46,-1.8,.23);
  box(.43,.13,.08,.06,black,front,-.39,-2.04,.24).rotation.z=.12;
  box(.43,.13,.08,.06,black,front,.22,-2.04,.24).rotation.z=.12;
  label('SELECT',.53,.13,'#759882',front,-.39,-2.23,.22,42);
  label('START',.5,.13,'#759882',front,.22,-2.23,.22,42);
  for(let i=0;i<5;i++)box(.07,.62,.027,.026,black,front,.73+i*.13,-2.03,.224).rotation.z=-.24;
  for(const [x,y] of [[-1.36,2.25],[1.36,2.25],[-1.34,-2.25]]){
    disk(.06,.018,screw,front,x,y,.218);box(.072,.012,.009,.002,black,front,x,y,.23);
  }

  box(2.5,2.28,.095,.06,black,lcd,0,.83,0);
  box(2.38,2.17,.016,.04,gold,lcd,0,.83,.048);
  let ready=false,raf=0,disposed=false,visible=true,progress=0,manual=null,yaw=0;
  const pointer={x:0,y:0};
  const screenTexture=new THREE.TextureLoader().load('./assets/tobu-gameplay.png',()=>requestFrame());
  screenTexture.colorSpace=THREE.SRGBColorSpace;screenTexture.magFilter=THREE.NearestFilter;screenTexture.minFilter=THREE.NearestFilter;textures.push(screenTexture);
  const screenMaterial=new THREE.ShaderMaterial({uniforms:{picture:{value:screenTexture}},vertexShader:'varying vec2 vUv; void main(){vUv=uv;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}',fragmentShader:'uniform sampler2D picture; varying vec2 vUv; void main(){vec3 c=texture2D(picture,vUv).rgb;vec2 p=fract(vUv*vec2(160.,144.));float grid=smoothstep(.02,.17,p.x)*smoothstep(.02,.17,p.y);float l=dot(c,vec3(.299,.587,.114));c=mix(vec3(.018,.09,.035),vec3(.46,.84,.30),l);c=mix(c*.8,c,grid);gl_FragColor=vec4(c,1.); #include <tonemapping_fragment>\n #include <colorspace_fragment>\n}'});
  // Preprocessor directives must begin on a separate line.
  screenMaterial.fragmentShader=screenMaterial.fragmentShader.replace('; #include',';\n #include');
  materials.push(screenMaterial);mesh(new THREE.PlaneGeometry(2.24,2.016),screenMaterial,lcd,0,.83,.066);
  box(.46,.73,.03,.02,gold,lcd,.43,-.61,-.015);
  for(let i=0;i<8;i++)box(.015,.65,.005,.002,black,lcd,.25+i*.05,-.61,.003);

  box(3.0,4.66,.07,.13,silicon,pcb);
  const boardTexture=new THREE.TextureLoader().load('./assets/pcb-texture.png',()=>requestFrame());
  boardTexture.colorSpace=THREE.SRGBColorSpace;boardTexture.offset.set(.085,.047);boardTexture.repeat.set(.83,.918);textures.push(boardTexture);
  const boardMaterial=mat('#c7e7cf',.68,.15,{map:boardTexture});mesh(new THREE.PlaneGeometry(2.96,4.59),boardMaterial,pcb,0,0,.042);
  box(.74,.72,.09,.035,black,pcb,.04,.35,.097);label('MATCHA',.66,.12,'#9bafa4',pcb,.04,.38,.15,55);
  for(let i=0;i<10;i++)for(const side of [-1,1])box(.1,.023,.017,.003,screw,pcb,.45*side,-.005+i*.073,.098);
  box(.34,.59,.09,.02,black,pcb,-.85,.1,.095);
  for(let i=0;i<8;i++)box(.06,.026,.02,.002,gold,pcb,-1.06,-.14+i*.067,.095);
  for(let i=0;i<14;i++)box(.12,.32,.015,.008,gold,pcb,-.96+i*.147,2.09,.062);
  for(const [x,y]of[[-1.32,2.15],[1.32,2.15],[-1.32,-2.13],[1.32,-2.13]])disk(.072,.018,gold,pcb,x,y,.058);
  box(3.16,4.9,.25,.22,shell,rear,0,0,-.06);
  box(2.7,2.08,.04,.1,bezel,rear,0,-.91,.089);
  box(2.58,1.96,.035,.08,shell,rear,0,-.91,.114);
  box(.39,.13,.025,.04,black,rear,0,.015,.14);
  for(let i=0;i<9;i++)box(1.51,.025,.018,.007,black,rear,0,1+i*.09,.082);
  for(const[x,y]of[[-1.32,2.14],[1.32,2.14],[-1.31,-2.17],[1.31,-2.17]])disk(.07,.025,screw,rear,x,y,.082);
  // Crisp seam catches the rim light around the assembled shell.
  const seam=frame(3.17,4.91,.21,3.07,4.81,0,.035,edge,rear,.08);
  seam.material=mat('#204e36',.28,.48);

  const chips=new THREE.Group();scene.add(chips);
  const floating=[];
  for(let i=0;i<4;i++){
    const g=new THREE.Group();chips.add(g);const s=i%2===0?.46:.32;
    box(s,s,.11,.025,black,g);label(i%2===0?'DMG':'M',s*.88,s*.25,'#84a99a',g,0,0,.064,64);
    for(let j=0;j<5;j++)for(const side of[-1,1])box(.1,.028,.025,.003,gold,g,side*(s/2+.035),-s*.36+j*s*.18,0);
    floating.push(g);
  }
  const envScene=new THREE.Scene();envScene.background=new THREE.Color('#11251c');
  const lightPanel=(color,intensity,x,y,z,w,h)=>{const m=new THREE.Mesh(new THREE.PlaneGeometry(w,h),new THREE.MeshBasicMaterial({color}));m.position.set(x,y,z);m.lookAt(0,0,0);envScene.add(m);};
  lightPanel('#c9f9df',1,-4,4,3,3,7);lightPanel('#36ef97',1,4,1,-2,2,7);lightPanel('#465a51',1,0,5,-2,6,2);
  const pmrem=new THREE.PMREMGenerator(renderer);const environment=pmrem.fromScene(envScene,.08);scene.environment=environment.texture;
  pmrem.dispose();envScene.traverse(o=>{if(o.isMesh){o.geometry.dispose();o.material.dispose();}});
  scene.add(new THREE.HemisphereLight('#83be9c','#020704',1.1));
  const key=new THREE.DirectionalLight('#e4fff0',3.7);key.position.set(-3,5,6);key.castShadow=true;key.shadow.mapSize.set(1024,1024);key.shadow.camera.left=-7;key.shadow.camera.right=7;key.shadow.camera.top=7;key.shadow.camera.bottom=-7;key.shadow.normalBias=.025;key.shadow.bias=-.0001;scene.add(key);
  const rim=new THREE.DirectionalLight('#20ff88',6);rim.position.set(3,2,-3);scene.add(rim);
  const fill=new THREE.DirectionalLight('#427d69',1.1);fill.position.set(-4,-1,2);scene.add(fill);

  // A subtle world-space technical grid recedes behind the exploded assembly.
  const grid=new THREE.GridHelper(32,16,'#1e4c36','#122b20');grid.rotation.x=Math.PI/2;grid.position.z=-3.9;grid.material.transparent=true;grid.material.opacity=0;scene.add(grid);
  const glowMaterial=new THREE.ShaderMaterial({transparent:true,depthWrite:false,uniforms:{strength:{value:.36}},vertexShader:'varying vec2 vUv;void main(){vUv=uv;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.);}',fragmentShader:'varying vec2 vUv;uniform float strength;void main(){float d=length((vUv-.5)*vec2(1.,.75));float a=exp(-d*d*17.)*strength;gl_FragColor=vec4(.025,.24,.11,a);}',blending:THREE.AdditiveBlending});materials.push(glowMaterial);
  const atmosphere=mesh(new THREE.PlaneGeometry(17,13),glowMaterial,scene,0,.1,-4.2);atmosphere.castShadow=false;atmosphere.receiveShadow=false;
  const startTime=performance.now();let lastTime=0,width=1,height=1;
  function render(){
    raf=0;if(disposed||!ready)return;
    const t=paused?lastTime:(performance.now()-startTime)/1000;lastTime=t;
    const chapterProgress=paused?(progress<.5?0:1):THREE.MathUtils.smoothstep(progress,.12,.91);
    const p=manual===null?chapterProgress:manual;
    const mobile=width<760;
    const bob=paused?0:Math.sin(t*.65)*.04;
    machine.position.set(mobile?-.1:-2.65*p,-.18+(mobile?.5*p:-.15*p)+bob,0);
    machine.rotation.set(-.075+.15*p+pointer.y*.035,-.32-.38*p+yaw+pointer.x*.045,-.07+.08*p);
    machine.scale.setScalar((mobile?.82:.91)*(1-(mobile?.3:.12)*p));
    shell.color.copy(shellDark).lerp(shellOpen,p*.78);
    front.position.z=.13+2.15*p;front.position.x=-.15*p;lcd.position.z=.31+.53*p;pcb.position.z=-.13-.76*p;rear.position.z=-.34-1.77*p;
    lcd.position.y=1.18*p;lcd.rotation.x=-.38*p;lcd.rotation.y=.13*p;pcb.rotation.x=-.18*p;pcb.rotation.y=.22*p;
    grid.material.opacity=p*.28;
    floating.forEach((g,i)=>{const points=[[-2.66,1.05,.2],[2.69,1.64,-.6],[-2.95,-1.2,-.4],[2.92,-1.48,0]];g.position.set(points[i][0],points[i][1]+Math.sin(t*.55+i)*.08,points[i][2]);g.rotation.set(.22+i*.16,.2+Math.sin(t*.2+i)*.17,.3+i*.4);g.scale.setScalar(1-p);});
    renderer.render(scene,camera);
    if(!paused&&visible&&!document.hidden)raf=requestAnimationFrame(render);
  }
  function requestFrame(){if(ready&&visible&&!document.hidden&&!raf&&!disposed)raf=requestAnimationFrame(render);}
  function resize(){width=stage.clientWidth;height=stage.clientHeight;camera.aspect=width/height;camera.position.z=width<760?15:12.7;camera.updateProjectionMatrix();renderer.setSize(width,height,false);requestFrame();}
  function move(event){if(paused||event.pointerType==='touch')return;pointer.x=(event.clientX/width-.5)*2;pointer.y=(event.clientY/height-.5)*2;requestFrame();}
  function visibility(){if(document.hidden){cancelAnimationFrame(raf);raf=0;}else requestFrame();}
  const observer=new ResizeObserver(resize);observer.observe(stage);
  const intersection=new IntersectionObserver(([entry])=>{visible=entry.isIntersecting;if(visible)requestFrame();else{cancelAnimationFrame(raf);raf=0;}},{threshold:0});intersection.observe(stage);
  document.addEventListener('pointermove',move,{passive:true});document.addEventListener('visibilitychange',visibility);
  ready=true;stage.classList.add('scene-ready');resize();
  return {
    setProgress(value){progress=Math.max(0,Math.min(1,value));manual=null;requestFrame();},
    setExploded(value){manual=value?1:0;requestFrame();},
    rotate(direction){yaw+=direction*.22;requestFrame();},
    setPaused(value){paused=value;requestFrame();},
    dispose(){disposed=true;cancelAnimationFrame(raf);observer.disconnect();intersection.disconnect();document.removeEventListener('pointermove',move);document.removeEventListener('visibilitychange',visibility);scene.traverse(o=>{if(o.geometry)o.geometry.dispose();});materials.forEach(m=>m.dispose());textures.forEach(t=>t.dispose());grid.material.dispose();environment.dispose();key.shadow.dispose();renderer.dispose();}
  };
}

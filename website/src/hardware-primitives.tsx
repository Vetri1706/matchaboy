import {useEffect, useLayoutEffect, useMemo, useRef} from 'react';
import * as THREE from 'three';

export function Lettering({text, position, width=.9, color='#263729'}: {
  text: string; position: [number, number, number]; width?: number; color?: string;
}) {
  const {texture, aspect} = useMemo(() => {
    const canvas = document.createElement('canvas');
    let ctx = canvas.getContext('2d')!;
    ctx.font = 'bold 64px Arial';
    canvas.width = Math.ceil(ctx.measureText(text).width + 22); canvas.height = 96;
    ctx = canvas.getContext('2d')!; ctx.font = 'bold 64px Arial';
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillStyle = color;
    ctx.fillText(text, canvas.width/2, 48);
    const texture = new THREE.CanvasTexture(canvas);
    texture.colorSpace = THREE.SRGBColorSpace; texture.anisotropy = 8;
    return {texture, aspect: canvas.width/96};
  }, [text, color]);
  useEffect(() => () => texture.dispose(), [texture]);
  return <mesh position={position}><planeGeometry args={[width,width/aspect]}/><meshBasicMaterial map={texture} transparent alphaTest={.015} depthWrite={false} toneMapped={false}/></mesh>;
}

export type Detail = {position: [number,number,number]; rotation?: [number,number,number]; scale?: [number,number,number]};
export function Batch({items, size, color, metalness=.6, roughness=.4, ring=false}: {
  items: Detail[]; size: [number,number,number]; color: string; metalness?: number; roughness?: number; ring?: boolean;
}) {
  const mesh=useRef<THREE.InstancedMesh>(null);
  useLayoutEffect(() => {
    if(!mesh.current) return;
    const dummy=new THREE.Object3D();
    items.forEach((item,i) => {
      dummy.position.set(...item.position); dummy.rotation.set(...(item.rotation??[0,0,0]));
      dummy.scale.set(...(item.scale??[1,1,1])); dummy.updateMatrix(); mesh.current!.setMatrixAt(i,dummy.matrix);
    });
    mesh.current.instanceMatrix.needsUpdate=true; mesh.current.computeBoundingSphere();
  },[items]);
  return <instancedMesh ref={mesh} args={[undefined,undefined,items.length]} castShadow receiveShadow>
    {ring?<ringGeometry args={size}/>:<boxGeometry args={size}/>}
    <meshStandardMaterial color={color} metalness={metalness} roughness={roughness}/>
  </instancedMesh>;
}

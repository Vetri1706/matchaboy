import assert from 'node:assert/strict';
import * as THREE from 'three';
import {assemblyPose, createAssemblyFramer, PART_BOUNDS, ASSEMBLED, ASSEMBLY_AXIS} from '../src/assembly-motion.ts';
import {createPresentation} from '../src/presentation.ts';

// Regression for the clipped exploded assembly: independently project all
// transformed part bounds through the real perspective camera at each pose.
const frame = createAssemblyFramer();
const aspects = [.6, 1, 1.5, 2.5];
const camera = new THREE.PerspectiveCamera(32, 1, .1, 120);
let cases = 0, maximumEdge = 0;
for (const aspect of aspects) for (const x of [-.4, 0, .4]) for (const y of [-.65, 0, .65]) {
  for (let step = 0; step <= 200; step++) {
    const pose = assemblyPose(step / 200, {x, y}), framing = frame(pose, aspect);
    camera.aspect = aspect;
    camera.position.copy(framing.position); camera.lookAt(framing.target);
    camera.updateProjectionMatrix(); camera.updateMatrixWorld();
    const root = new THREE.Group(); root.rotation.set(...pose.rotation);
    const interaction = new THREE.Group(); interaction.rotation.set(...pose.interaction); root.add(interaction);
    for (const name of ['front', 'board', 'rear']) {
      const part = new THREE.Group(); part.position.set(...pose[name].position); part.rotation.set(...pose[name].rotation); interaction.add(part);
      root.updateMatrixWorld(true);
      const box = PART_BOUNDS[name];
      for (const px of [box.min.x, box.max.x]) for (const py of [box.min.y, box.max.y]) for (const pz of [box.min.z, box.max.z]) {
        const projected = new THREE.Vector3(px, py, pz).applyMatrix4(part.matrixWorld).project(camera);
        const edge = Math.max(Math.abs(projected.x), Math.abs(projected.y));
        maximumEdge = Math.max(maximumEdge, edge);
        assert(edge < .92, `Clipped ${name}, progress ${step / 200}, aspect ${aspect}, drag ${x}/${y}: ${edge}`);
        assert(projected.z > -1 && projected.z < 1, 'Part crossed the camera near/far plane');
      }
    }
    cases++;
  }
}
// Rear releases before the front, rather than three simultaneous translations.
assert(assemblyPose(.44).rear.position[0] > .1);
assert(Math.abs(assemblyPose(.44).front.position[0]) < 1e-8);
assert(assemblyPose(.58).front.position[2] > 1);
assert.deepEqual(assemblyPose(.50, undefined, true), assemblyPose(.95, undefined, true));
assert.deepEqual(assemblyPose(.10, undefined, true), assemblyPose(.40, undefined, true));
// No jumps in part velocity at the phase boundaries.
for (const boundary of [.13, .32, .38, .43, .45, .51, .61, .72, .75, .77, .96]) {
  const before=assemblyPose(boundary-.0001), after=assemblyPose(boundary+.0001);
  for (const name of ['front','board','rear']) assert(new THREE.Vector3(...before[name].position).distanceTo(new THREE.Vector3(...after[name].position)) < .008);
}
console.log(`${cases} framing poses passed; maximum viewport edge ${maximumEdge.toFixed(3)}. Staggered release, phase continuity and reduced-motion stills passed.`);

// Release remains on one axis at every intermediate frame; pointer interaction
// cannot mutate the presentation pose or the positions of individual parts.
for(let i=0;i<=200;i++){
  const p=assemblyPose(i/200), dragged=assemblyPose(i/200,{x:.3,y:.5});
  for(const name of ['front','rear']){
    const displacement=new THREE.Vector3(...p[name].position).sub(new THREE.Vector3(...ASSEMBLED[name]));
    assert(displacement.cross(new THREE.Vector3(...ASSEMBLY_AXIS)).length()<1e-10);
    assert.deepEqual(p[name],dragged[name]);
  }
  assert.deepEqual(p.rotation,dragged.rotation);
}
const owner=createPresentation();
const settle=(target,request=0)=>{let value;for(let i=0;i<180;i++)value=owner.advance(target,1/60,false,request);return value;};
const forward=Array.from({length:9},(_,i)=>settle(i/8));
for(let i=8;i>=0;i--)assert(Math.abs(settle(i/8)-forward[i])<1e-7,'Scroll reversal changed the settled pose');
settle(.9);owner.advance(.9,1/60,false,1);
assert(Math.abs(settle(.4,1)-.4)<1e-7,'Scroll input must interrupt replay');
assert.equal(owner.advance(.8,1/60,true,2),.8,'Reduced motion must settle immediately');
console.log('Shared-axis release, isolated pointer rotation, forward/reverse convergence and replay interruption passed.');

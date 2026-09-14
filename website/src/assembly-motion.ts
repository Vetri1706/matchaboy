import * as THREE from 'three';

export type PartPose = {position: [number, number, number]; rotation: [number, number, number]};
export type AssemblyPose = {
  front: PartPose; board: PartPose; rear: PartPose;
  rotation: [number, number, number]; interaction: [number, number, number]; camera: [number, number, number];
  reveal: number; settle: number;
};

// Shared assembly coordinates. The oblique presentation axis exposes each
// layer without fanning or rotating individual parts away from one another.
export const ASSEMBLED = {front: [0, 0, .44], board: [0, 0, 0], rear: [0, 0, -.38]} as const;
export const ASSEMBLY_AXIS = [-1.6, 0, 3.215] as const;
export const EXPLODED = {front: [-1.6, 0, 3.655], board: [0, 0, 0], rear: [1.6, 0, -3.595]} as const;
const position = (name: keyof typeof ASSEMBLED, amount: number): [number, number, number] =>
  ASSEMBLED[name].map((v, i) => THREE.MathUtils.lerp(v, EXPLODED[name][i], amount)) as [number, number, number];

// Quintic ramps arrive and leave with zero acceleration. All movement belongs
// to scroll position, so reversing the scroll also reverses the choreography.
const ramp = (t: number, start: number, end: number) => {
  const x = THREE.MathUtils.clamp((t - start) / (end - start), 0, 1);
  return x * x * x * (x * (x * 6 - 15) + 10);
};

export function assemblyPose(t: number, drag = {x: 0, y: 0}, reduced = false): AssemblyPose {
  // Reduced motion uses the assembled or fully opened still, without an orbit.
  if (reduced) t = t < .45 ? .28 : .75;
  const lift = ramp(t, .13, .32);
  const anticipation = ramp(t, .32, .43);
  const backRelease = ramp(t, .38, .61);
  const frontRelease = ramp(t, .45, .72);
  const boardRelease = ramp(t, .51, .75);
  const settle = ramp(t, .77, .96);
  const sensitivity = reduced ? 0 : 1 - boardRelease * .45;
  return {
    front: {
      position: position('front', frontRelease),
      rotation: [0, 0, 0],
    },
    board: {
      position: [0, 0, 0],
      rotation: [0, 0, 0],
    },
    rear: {
      position: position('rear', backRelease),
      rotation: [0, 0, 0],
    },
    rotation: [
      .025,
      -.30 - .25 * boardRelease,
      .012 + .055 * boardRelease,
    ],
    interaction: [drag.x * .25 * sensitivity, drag.y * sensitivity, 0],
    camera: reduced ? [3.0, 3.1, 18] : [
      1.2 + .6 * lift + 2.4 * anticipation - .9 * boardRelease - .3 * settle,
      .7 + .6 * anticipation + 1.8 * boardRelease,
      18,
    ],
    reveal: boardRelease,
    settle,
  };
}

// Conservative local bounds include the labels, button caps and cartridge lip.
// Fit actual transformed corners, not an assumed width: perspective, dragging,
// intermediate separation and narrow windows all share the same safe framing.
export const PART_BOUNDS = {
  front: new THREE.Box3(new THREE.Vector3(-1.94, -2.99, -.41), new THREE.Vector3(1.94, 2.99, .52)),
  board: new THREE.Box3(new THREE.Vector3(-1.72, -2.59, -.27), new THREE.Vector3(1.85, 2.59, .40)),
  rear: new THREE.Box3(new THREE.Vector3(-1.94, -2.99, -.60), new THREE.Vector3(2.66, 2.99, .48)),
};

export function createAssemblyFramer() {
  const matrix = new THREE.Matrix4(), rotationMatrix = new THREE.Matrix4();
  const rootQ = new THREE.Quaternion(), dragQ = new THREE.Quaternion(), partQ = new THREE.Quaternion(), viewQ = new THREE.Quaternion();
  const orientation = new THREE.Quaternion(), euler = new THREE.Euler();
  const direction = new THREE.Vector3(), center = new THREE.Vector3(), localPosition = new THREE.Vector3();
  const unit = new THREE.Vector3(1, 1, 1), origin = new THREE.Vector3(), up = new THREE.Vector3(0, 1, 0);
  const points = Array.from({length: 24}, () => new THREE.Vector3());
  const position = new THREE.Vector3(), target = new THREE.Vector3();
  return (pose: AssemblyPose, aspect: number, fov = 32) => {
    direction.fromArray(pose.camera).normalize();
    rotationMatrix.lookAt(direction, origin, up);
    orientation.setFromRotationMatrix(rotationMatrix);
    viewQ.copy(orientation).invert();
    rootQ.setFromEuler(euler.set(...pose.rotation));
    dragQ.setFromEuler(euler.set(...pose.interaction));rootQ.multiply(dragQ);
    let n = 0, minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
    for (const name of ['front', 'board', 'rear'] as const) {
      const part = pose[name], box = PART_BOUNDS[name];
      partQ.setFromEuler(euler.set(...part.rotation));
      matrix.compose(localPosition.fromArray(part.position), partQ, unit);
      for (const x of [box.min.x, box.max.x]) for (const y of [box.min.y, box.max.y]) for (const z of [box.min.z, box.max.z]) {
        const v = points[n++].set(x, y, z).applyMatrix4(matrix).applyQuaternion(rootQ).applyQuaternion(viewQ);
        minX = Math.min(minX, v.x); maxX = Math.max(maxX, v.x);
        minY = Math.min(minY, v.y); maxY = Math.max(maxY, v.y);
      }
    }
    center.set((minX + maxX) / 2, (minY + maxY) / 2, 0);
    const tanY = Math.tan(THREE.MathUtils.degToRad(fov / 2)), tanX = tanY * aspect;
    // Ease a small dolly back into the release; every part has room to travel.
    const occupancy = .91 - .025 * Math.sin(pose.reveal * Math.PI);
    let distance = 1;
    for (const v of points) distance = Math.max(distance, v.z + Math.max(Math.abs(v.x - center.x) / tanX, Math.abs(v.y - center.y) / tanY) / occupancy);
    target.copy(center).applyQuaternion(orientation);
    position.copy(direction).multiplyScalar(distance).add(target);
    return {position, target, distance};
  };
}

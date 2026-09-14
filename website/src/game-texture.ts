import {CanvasTexture, NearestFilter, SRGBColorSpace} from "three";

type Keys = {left: boolean; right: boolean; action: boolean};

export function createGameTexture() {
  const canvas = document.createElement("canvas");
  canvas.width = 160;
  canvas.height = 144;
  const ctx = canvas.getContext("2d")!;
  const texture = new CanvasTexture(canvas);
  texture.minFilter = NearestFilter;
  texture.magFilter = NearestFilter;
  texture.colorSpace = SRGBColorSpace;

  const keys: Keys = {left: false, right: false, action: false};
  let playerX = 76;
  let playerY = 116;
  let velocityY = 0;
  let tick = 0;
  let score = 0;

  const setKey = (key: keyof Keys, active: boolean) => { keys[key] = active; };

  const draw = (delta: number) => {
    tick += delta * 60;
    if (keys.left) playerX -= delta * 68;
    if (keys.right) playerX += delta * 68;
    if (keys.action && playerY >= 116) velocityY = -92;
    velocityY += delta * 205;
    playerY = Math.min(116, playerY + velocityY * delta);
    if (playerY >= 116) velocityY = 0;
    playerX = Math.max(12, Math.min(142, playerX));
    score = Math.floor(tick / 12);

    ctx.fillStyle = "#d2d8bd";
    ctx.fillRect(0, 0, 160, 144);
    ctx.fillStyle = "#2b392b";
    ctx.fillRect(0, 0, 160, 12);
    ctx.font = "8px monospace";
    ctx.fillStyle = "#d2d8bd";
    ctx.fillText("MATCHA RUN", 6, 9);
    ctx.fillText(String(score).padStart(4, "0"), 130, 9);

    ctx.fillStyle = "#b0bd97";
    for(let i=0;i<5;i++){
      const mx=i*48-((tick*.06)%48)-20;
      ctx.beginPath();ctx.moveTo(mx,113);ctx.lineTo(mx+29,59+(i%2)*15);ctx.lineTo(mx+59,113);ctx.fill();
    }
    ctx.fillStyle = "#e0e3cd";
    for(let i=0;i<3;i++){
      const cx=(i*63-tick*.025+200)%200-30,cy=29+i%2*13;
      ctx.fillRect(cx,cy,24,5);ctx.fillRect(cx+6,cy-4,12,5);
    }
    ctx.fillStyle = "#879873";
    for (let x = -24; x < 190; x += 24) {
      const px = x - (tick % 24);
      ctx.fillRect(px, 121, 15, 3);
      ctx.fillRect(px + 4, 128, 12, 2);
    }
    ctx.fillStyle = "#52674c";
    ctx.fillRect(0, 132, 160, 12);
    for (let x = -20; x < 190; x += 30) {
      const px = x - ((tick * .7) % 30);
      ctx.fillRect(px, 136, 18, 2);
    }

    const obstacleX = 165 - ((tick * .8) % 190);
    ctx.fillStyle = "#5c7253";
    ctx.fillRect(obstacleX, 105, 9, 16);
    ctx.fillRect(obstacleX - 3, 111, 15, 10);

    ctx.fillStyle = "#26372b";
    ctx.fillRect(Math.round(playerX), Math.round(playerY), 10, 12);
    ctx.fillStyle = "#d2d8bd";
    ctx.fillRect(Math.round(playerX) + 3, Math.round(playerY) + 2, 4, 4);
    ctx.fillRect(Math.round(playerX) - 3, Math.round(playerY) + 10, 6, 3);
    ctx.fillRect(Math.round(playerX) + 7, Math.round(playerY) + 10, 6, 3);

    texture.needsUpdate = true;
  };

  return {texture, draw, setKey};
}

// Автотест моста: два bridge-процесса в одной комнате LiveKit,
// звук идёт по UDP: tone -> bridge A -> LiveKit -> bridge B -> receiver.
import crypto from 'node:crypto';
import dgram from 'node:dgram';
import { spawn } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';

const url = process.env.LIVEKIT_URL || 'ws://127.0.0.1:7880';
const key = process.env.LIVEKIT_KEY || 'devkey';
const secret = process.env.LIVEKIT_SECRET || 'secret';

const b64u = (value) => Buffer.from(value).toString('base64url');

function token(room, identity, name) {
  const header = b64u(JSON.stringify({ alg: 'HS256', typ: 'JWT' }));
  const now = Math.floor(Date.now() / 1000);
  const payload = b64u(JSON.stringify({
    iss: key, sub: identity, name,
    nbf: now - 10, exp: now + 600,
    video: { roomJoin: true, room, canPublish: true, canSubscribe: true },
  }));
  const signature = crypto.createHmac('sha256', secret)
    .update(`${header}.${payload}`).digest('base64url');
  return `${header}.${payload}.${signature}`;
}

const portA = 46011;
const portB = 46012;
const receiverPort = 46013;

const bridgeA = spawn(process.execPath, ['index.js', '--url', url,
  '--token', token('selftest', 'bridge-a', 'A'), '--port', String(portA)],
  { stdio: ['ignore', 'pipe', 'pipe'] });
const bridgeB = spawn(process.execPath, ['index.js', '--url', url,
  '--token', token('selftest', 'bridge-b', 'B'), '--port', String(portB)],
  { stdio: ['ignore', 'pipe', 'pipe'] });

for (const [name, child] of [['A', bridgeA], ['B', bridgeB]]) {
  child.stdout.on('data', (data) => process.stdout.write(`[${name}] ${data}`));
  child.stderr.on('data', (data) => process.stderr.write(`[${name}] ${data}`));
}

const receiver = dgram.createSocket('udp4');
let receivedBytes = 0;
receiver.on('message', (message) => { receivedBytes += message.length; });
await new Promise((resolve) => receiver.bind(receiverPort, '127.0.0.1', resolve));

const sender = dgram.createSocket('udp4');
const frame = Buffer.alloc(1920);
// «Привет» мосту B, чтобы он запомнил наш адрес.
const hello = setInterval(() => receiver.send(Buffer.alloc(2), portB, '127.0.0.1'), 500);
receiver.send(Buffer.alloc(2), portB, '127.0.0.1');

await sleep(4000);
if (bridgeA.exitCode !== null || bridgeB.exitCode !== null) {
  console.error(`SELFTEST: мосты не поднялись (A=${bridgeA.exitCode}, B=${bridgeB.exitCode})`);
  process.exit(2);
}

for (let i = 0; i < 150; ++i) {
  for (let s = 0; s < 960; ++s) {
    const value = Math.sin(2 * Math.PI * 440 * (i * 960 + s) / 48000) * 0.3;
    frame.writeInt16LE(Math.round(value * 32767), s * 2);
  }
  sender.send(frame, portA, '127.0.0.1');
  await sleep(20);
}

await sleep(1500);
for (const child of [bridgeA, bridgeB]) child.kill();
receiver.close();
sender.close();

if (receivedBytes > 10000) {
  console.log(`SELFTEST OK: получено ${receivedBytes} байт звука`);
  process.exit(0);
}
console.error(`SELFTEST FAIL: получено ${receivedBytes} байт`);
process.exit(1);

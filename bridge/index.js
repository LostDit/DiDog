// DiDog LiveKit bridge: соединяет локальный UDP-поток PCM с комнатой LiveKit.
// Запуск: node index.js --url wss://... --token <jwt> --port 46123
// Протокол: клиент шлёт 20-мс кадры PCM s16le 48 кГц моно (1920 байт),
// мост публикует их в LiveKit и пересылает входящий звук теми же кадрами.

import { AudioFrame, AudioSource, AudioStream, LocalAudioTrack, Room, RoomEvent,
         TrackSource, TrackPublishOptions, RemoteAudioTrack } from '@livekit/rtc-node';
import dgram from 'node:dgram';

const args = process.argv.slice(2);
const option = (name, fallback) => {
  const index = args.indexOf(`--${name}`);
  return index >= 0 && args[index + 1] ? args[index + 1] : fallback;
};

const url = option('url', '');
const token = option('token', '');
const port = Number(option('port', '46123'));
const sampleRate = Number(option('sample-rate', '48000'));
const channels = Number(option('channels', '1'));
const samplesPerFrame = Number(option('frame-samples', '960'));

if (!url || !token) {
  console.error('BRIDGE: нужны --url и --token');
  process.exit(2);
}

const socket = dgram.createSocket('udp4');
let clientAddress = null;
let clientPort = 0;
let source = null;

const sendToClient = (buffer) => {
  if (clientAddress && clientPort)
    socket.send(buffer, clientPort, clientAddress);
};

socket.on('message', (message, rinfo) => {
  clientAddress = rinfo.address;
  clientPort = rinfo.port;
  if (!source || message.length < 2)
    return;

  const samples = new Int16Array(message.buffer, message.byteOffset,
                                 Math.floor(message.length / 2));
  const frame = new AudioFrame(samples, sampleRate, channels, samplesPerFrame);
  source.captureFrame(frame);
});

socket.on('listening', () => {
  console.log(`BRIDGE: UDP ${socket.address().port}`);
});
socket.on('error', (error) => {
  console.error(`BRIDGE: UDP ошибка: ${error.message}`);
  process.exit(3);
});
socket.bind(port, '127.0.0.1');

const room = new Room();

const attachRemote = async (track) => {
  if (!(track instanceof RemoteAudioTrack))
    return;
  const stream = new AudioStream(track);
  for await (const frame of stream) {
    const data = Buffer.from(frame.data.buffer, frame.data.byteOffset,
                             frame.data.byteLength);
    sendToClient(data);
  }
};

room.on(RoomEvent.TrackSubscribed, (track) => {
  console.log('BRIDGE: подписались на аудиодорожку');
  attachRemote(track).catch((error) => {
    console.error(`BRIDGE: аудиопоток упал: ${error.message}`);
  });
});

room.on(RoomEvent.Disconnected, () => {
  console.log('BRIDGE: отключились от LiveKit');
  process.exit(0);
});

try {
  await room.connect(url, token);
  console.log(`BRIDGE: подключены к ${url}`);

  source = new AudioSource(sampleRate, channels, 1000);
  const track = LocalAudioTrack.createAudioTrack('microphone', source);
  const options = new TrackPublishOptions();
  options.source = TrackSource.SOURCE_MICROPHONE;
  await room.localParticipant.publishTrack(track, options);
  console.log('BRIDGE: микрофон опубликован');
} catch (error) {
  console.error(`BRIDGE: не удалось подключиться: ${error.message}`);
  process.exit(4);
}

process.on('SIGINT', () => process.exit(0));
process.on('SIGTERM', () => process.exit(0));

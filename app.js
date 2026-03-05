const SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const TX_UUID      = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
const RX_UUID      = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";

let device, server, txChar, rxChar;
let isRecording = false;
let audioChunks = [];
let voiceMessages = [];
let sosCountdownTimer = null;
let audioCtx = null;

function getAudioContext() {
  if (!audioCtx) audioCtx = new AudioContext({ sampleRate: 11025 });
  return audioCtx;
}

// Load saved phone number
const savedPhone = localStorage.getItem('sos_phone') || '';
if (savedPhone) document.getElementById('emergency-number').value = savedPhone;

function savePhone() {
  const num = document.getElementById('emergency-number').value.trim();
  if (!num) return;
  localStorage.setItem('sos_phone', num);
  const saved = document.getElementById('phone-saved');
  saved.style.display = 'block';
  setTimeout(() => saved.style.display = 'none', 2000);
}

function getPhone() {
  return document.getElementById('emergency-number').value.trim() || localStorage.getItem('sos_phone') || '';
}

async function connect() {
  try {
    setStatus('connecting');
    if (!device) {
      const scanAll = document.getElementById('scanAllToggle').checked;
      const requestOptions = scanAll
        ? { acceptAllDevices: true, optionalServices: [SERVICE_UUID] }
        : { filters: [{ name: 'C3_Supermini_BLE' }], optionalServices: [SERVICE_UUID] };
      log(scanAll ? "Scanning all nearby BLE devices..." : "Scanning for C3_Supermini_BLE...", 'info');
      device = await navigator.bluetooth.requestDevice(requestOptions);
      device.addEventListener('gattserverdisconnected', onDisconnected);
    }
    log("Connecting to GATT server...", 'info');
    server = await device.gatt.connect();
    await setupCharacteristics();
  } catch (e) {
    log("Error: " + e, 'alert');
    setStatus('disconnected');
  }
}

async function setupCharacteristics() {
  await new Promise(r => setTimeout(r, 500));
  const service = await server.getPrimaryService(SERVICE_UUID);
  txChar = await service.getCharacteristic(TX_UUID);
  rxChar  = await service.getCharacteristic(RX_UUID);
  await txChar.startNotifications();
  txChar.addEventListener('characteristicvaluechanged', onData);
  setStatus('connected');
  log("Connected to C3_Supermini_BLE", 'ok');
  document.getElementById('disconnectBtn').disabled = false;
  document.getElementById('sendBtn').disabled = false;
  document.getElementById('pingBtn').disabled = false;
}

let manualDisconnect = false;

async function disconnectDevice() {
  manualDisconnect = true;
  if (device && device.gatt.connected) device.gatt.disconnect();
}

function onDisconnected() {
  setStatus('disconnected');
  log("Device disconnected", 'alert');
  ['disconnectBtn','sendBtn','pingBtn'].forEach(id => document.getElementById(id).disabled = true);
  document.getElementById('rec-indicator').classList.remove('active');
  if (!manualDisconnect) {
    setTimeout(() => {
      if (device) {
        log("Attempting reconnect...", 'info');
        device.gatt.connect().then(s => { server = s; return setupCharacteristics(); }).catch(() => {});
      }
    }, 3000);
  }
  manualDisconnect = false;
}

function onData(event) {
  const raw = event.target.value;

  // Handle binary audio frames first — skip UTF-8 decode for audio packets
  if (isRecording) {
    audioChunks.push(new Uint8Array(raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.byteLength)));
    return;
  }

  const text = new TextDecoder().decode(raw);

  if (text === "REC_START") {
    isRecording = true;
    audioChunks = [];
    document.getElementById('rec-indicator').classList.add('active');
    log("🎙 Recording started", 'event');

  } else if (text === "REC_END") {
    isRecording = false;
    document.getElementById('rec-indicator').classList.remove('active');
    log("✅ Voice message received — press Play to listen", 'ok');
    storeVoiceMessage([...audioChunks]);
    audioChunks = [];

  } else if (text === "Request Sent") {
    log("⚠ Help request received!", 'alert');
    showRequestAlert(false);

  } else if (text === "SOS_ALERT") {
    log("🚨 EMERGENCY — Long press triggered!", 'alert');
    showRequestAlert(true);
    triggerSOSCall();
    showCallModal();

  } else {
    log("ESP32: " + text, 'info');
  }
}

function triggerSOSCall() {
  const phone = getPhone();
  const modal = document.getElementById('sos-modal');
  document.getElementById('sos-time').textContent = new Date().toLocaleTimeString();
  document.getElementById('sos-phone-num').textContent = phone || 'No number set';
  document.getElementById('countdown-num').textContent = '5';

  const callBtn = document.getElementById('call-now-btn');
  if (phone) {
    callBtn.href = 'tel:' + phone;
    callBtn.style.pointerEvents = 'auto';
    callBtn.style.opacity = '1';
  } else {
    callBtn.href = '#';
    callBtn.style.opacity = '0.4';
    callBtn.style.pointerEvents = 'none';
    log("⚠ No emergency number set — add one in the phone config above", 'alert');
  }

  modal.classList.add('visible');

  // Auto-call countdown
  if (sosCountdownTimer) clearInterval(sosCountdownTimer);
  let count = 5;
  sosCountdownTimer = setInterval(() => {
    count--;
    document.getElementById('countdown-num').textContent = count;
    if (count <= 0) {
      clearInterval(sosCountdownTimer);
      sosCountdownTimer = null;
      document.getElementById('sos-countdown').textContent = 'Opening dialer...';
      if (phone) {
        log("📞 Auto-dialing " + phone, 'alert');
        window.location.href = 'tel:' + phone;
      } else {
        document.getElementById('sos-countdown').textContent = 'No number configured.';
      }
    }
  }, 1000);
}

function dismissSOS() {
  if (sosCountdownTimer) { clearInterval(sosCountdownTimer); sosCountdownTimer = null; }
  document.getElementById('sos-modal').classList.remove('visible');
  log("SOS modal dismissed", 'info');
}

function showRequestAlert(emergency) {
  document.getElementById('request-msg').textContent = emergency
    ? '🚨 EMERGENCY — User triggered emergency mode!'
    : 'The user is requesting assistance.';
  document.getElementById('request-time').textContent = new Date().toLocaleTimeString();
  document.getElementById('request-alert').classList.add('visible');
}

async function acceptRequest() {
  if (!rxChar) return;
  await rxChar.writeValue(new TextEncoder().encode("Accepted"));
  log("Accepted — notification sent to device", 'ok');
  dismissRequest();
}

function dismissRequest() {
  document.getElementById('request-alert').classList.remove('visible');
}

function storeVoiceMessage(chunks) {
  const id = Date.now();
  const time = new Date().toLocaleTimeString();
  voiceMessages.push({ id, chunks, time });
  document.getElementById('voice-empty')?.remove();
  const li = document.createElement('li');
  li.id = 'vm-' + id;
  li.innerHTML = `<span>🎤 Message <span class="meta">${time}</span></span><button class="btn" onclick="playVoiceMessage(${id})">▶ Play</button>`;
  document.getElementById('voice-list').appendChild(li);
}

function playVoiceMessage(id) {
  const msg = voiceMessages.find(m => m.id === id);
  if (!msg) return;
  playAudio(msg.chunks);
  log(`Playing voice message from ${msg.time}`, 'event');
}

async function playAudio(chunks) {
  const ctx = getAudioContext();
  await ctx.resume(); // Unblock autoplay suspension
  const totalLength = chunks.reduce((s, c) => s + c.length, 0);
  const combined = new Uint8Array(totalLength);
  let offset = 0;
  for (const chunk of chunks) { combined.set(chunk, offset); offset += chunk.length; }
  const numSamples = combined.length / 2;
  const buf = ctx.createBuffer(1, numSamples, ctx.sampleRate);
  const ch = buf.getChannelData(0);
  for (let i = 0; i < numSamples; i++) {
    const lo = combined[i*2], hi = combined[i*2+1];
    let s = (hi << 8) | lo;
    if (s > 32767) s -= 65536;
    ch[i] = s / 32768.0;
  }
  const src = ctx.createBufferSource();
  src.buffer = buf;
  src.connect(ctx.destination);
  src.start();
}

async function sendPing() {
  if (!rxChar) return;
  await rxChar.writeValue(new TextEncoder().encode("Nearby"));
  log("Proximity ping sent to device", 'ok');
}

async function sendData() {
  const val = document.getElementById("sendText").value.trim();
  if (!rxChar || !val) return;
  await rxChar.writeValue(new TextEncoder().encode(val));
  log("Sent: " + val, 'info');
  document.getElementById("sendText").value = "";
}

document.getElementById("sendText").addEventListener("keydown", e => { if (e.key === "Enter") sendData(); });

function setStatus(state) {
  document.getElementById('status-dot').className = state;
  document.getElementById('status-text').textContent = { connected:'Connected', connecting:'Connecting...', disconnected:'Disconnected' }[state];
}

function showCallModal() {
  document.getElementById('call-time').textContent = new Date().toLocaleTimeString();
  document.getElementById('call-modal').classList.add('visible');
  log("📞 Incoming emergency call", 'alert');
}

function acceptCall() {
  document.getElementById('call-modal').classList.remove('visible');
  log("✓ Call accepted", 'ok');
}

function endCall() {
  document.getElementById('call-modal').classList.remove('visible');
  log("✕ Call ended", 'info');
}

function log(msg, type='info') {
  const term = document.getElementById('terminal');
  const ts = new Date().toLocaleTimeString('en-GB', { hour12: false });
  const div = document.createElement('div');
  div.className = 'log-entry ' + type;
  div.textContent = `[${ts}] ${msg}`;
  term.appendChild(div);
  term.scrollTop = term.scrollHeight;
}

# Product Requirement Documentation

Request is a Hardware-Software product that aims to help blind people seek help remotely from sighted people. The product consists of a IoT device attached to their walking cane and a mobile application. 


## Features
- The primary interaction interface is a single button with different press, hold, and double-press actions. The device will also have a small speaker for audio feedback and a microphone for voice commands. 
- The mobile application will allow sighted volunteers to receive help requests and provide assistance through voice communication.
- The Device communicates with the mobile application via Bluetooth Low Energy (BLE).

## User Stories
1. As a blind user, I want to be able to easily request help by pressing a button on my walking cane, so that I can get assistance when needed.
2. As a sighted volunteer, I want to receive notifications on my mobile app when a blind user requests help, so that I can provide assistance promptly.
3. As a blind user, I want to be able to communicate with the sighted volunteer through voice commands, so that I can describe my situation and receive guidance effectively.
4. As a sighted volunteer, I want to be able to respond to help requests and communicate with the blind user through the mobile app, so that I can provide the necessary assistance.
5. As a blind user, I want the device to provide audio feedback when I press the button, so that I can confirm that my help request has been sent successfully.

---

## Technical Implementation

### BLE Configuration

| Item | Value |
|---|---|
| Service UUID | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` (Nordic UART Service) |
| TX Characteristic (ESP32 → Browser) | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| RX Characteristic (Browser → ESP32) | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| Target Device Name | `C3_Supermini_BLE` |
| Audio Sample Rate | `11025 Hz` |
| Audio Bit Depth | `16-bit PCM, little-endian signed` |
| Audio Channels | `1 (mono)` |

---

### BLE Connection Functions

**`connect()`**
- Calls `navigator.bluetooth.requestDevice()` filtered by device name + optional service UUID
- Connects to GATT server
- Registers `gattserverdisconnected` event listener
- Calls `setupCharacteristics()` on success

**`setupCharacteristics()`**
- 500ms delay after GATT connect (hardware stabilisation)
- Resolves primary service by `SERVICE_UUID`
- Gets TX and RX characteristics
- Starts notifications on TX characteristic
- Binds `onData` handler to `characteristicvaluechanged`

**`disconnectDevice()`**
- Sets `manualDisconnect = true` guard to suppress auto-reconnect
- Calls `device.gatt.disconnect()`

**`onDisconnected()`**
- Updates UI status
- Disables controls
- Auto-reconnects after **3 seconds** if disconnect was not manual

---

### Incoming Data Handler — `onData(event)`

Parses raw bytes as UTF-8 text and branches on protocol messages:

| Message | Action |
|---|---|
| `"REC_START"` | Sets `isRecording = true`, clears `audioChunks[]`, activates rec indicator |
| `"REC_END"` | Sets `isRecording = false`, calls `storeVoiceMessage()`, clears chunks |
| `"Request Sent"` | Triggers help-request alert (non-emergency) |
| `"SOS_ALERT"` | Triggers emergency alert + `triggerSOSCall()` |
| *(any other data while `isRecording`)* | Appends raw `Uint8Array` bytes to `audioChunks[]` |
| *(any other data while not recording)* | Logs as generic ESP32 message |

---

### Outgoing Write Functions (Browser → ESP32)

**`sendData()`**
- Writes UTF-8 encoded text input to RX characteristic via `rxChar.writeValue()`

**`sendPing()`**
- Writes fixed string `"Nearby"` to RX — proximity acknowledgment signal

**`acceptRequest()`**
- Writes fixed string `"Accepted"` to RX — confirms help request received

---

### Audio Pipeline

**`storeVoiceMessage(chunks)`**
- Stores raw chunk arrays in `voiceMessages[]` array with timestamp and ID
- Dynamically appends a playback entry to the voice list UI

**`playVoiceMessage(id)`**
- Looks up stored message by ID and calls `playAudio()`

**`playAudio(chunks)`**
- Flattens all `Uint8Array` chunks into one contiguous buffer
- Creates a Web Audio API `AudioContext` at 11025 Hz
- Decodes interleaved 16-bit little-endian PCM samples into float32 range `[-1.0, 1.0]`
- Creates and starts an `AudioBufferSourceNode` on the default output

---

### State Variables

| Variable | Purpose |
|---|---|
| `device` | BLE device reference |
| `server` | GATT server reference |
| `txChar` | TX characteristic (notifications in) |
| `rxChar` | RX characteristic (writes out) |
| `isRecording` | Flag — streaming audio chunks currently |
| `audioChunks[]` | Buffer for in-progress audio stream |
| `voiceMessages[]` | Persistent store of received voice messages |
| `sosCountdownTimer` | Interval handle for 5-second SOS auto-dial countdown |
| `manualDisconnect` | Guard flag to prevent auto-reconnect on intentional disconnect |

---

### SOS / Alert Logic

**`triggerSOSCall()`**
- Reads emergency phone number from DOM / `localStorage`
- Populates and shows `#sos-modal`
- Starts a 5-second countdown interval; on expiry navigates to `tel:<number>` (opens device dialer)

**`dismissSOS()`**
- Clears countdown interval, hides modal

**`showRequestAlert(emergency)`**
- Shows `#request-alert` with context-appropriate message (emergency vs. standard)

**`dismissRequest()`**
- Hides `#request-alert`

---

### Persistence

- Emergency phone number saved to / loaded from `localStorage` under key `sos_phone`
- `savePhone()` writes on button press; `getPhone()` reads from DOM first, falls back to `localStorage`


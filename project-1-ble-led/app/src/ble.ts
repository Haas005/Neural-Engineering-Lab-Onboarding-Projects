import { BleClient } from '@capacitor-community/bluetooth-le';

// These UUIDs must match firmware/led_ble/led_ble.ino.
// Keep them lowercase: the Android plugin is case sensitive.
export const SERVICE_UUID = '4fafc201-1fb5-459e-8fcc-c5c9c331914b';
export const LED_CHAR_UUID = 'beb5483e-36e1-4688-b7f5-ea07361b26a8';
export const PATTERN_CHAR_UUID = '1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e';

export const PATTERNS = [
  { id: 0, label: 'Manual' },
  { id: 1, label: 'Blink all' },
  { id: 2, label: 'Chase' },
  { id: 3, label: 'Alternate' },
] as const;

/** Firmware pattern id that plays the song. */
export const MUSIC_PATTERN = 4;

export const APP_TITLE = 'ESP32 Bluetooth LED & Music Demo';

export interface DeviceState {
  leds: [boolean, boolean, boolean];
  pattern: number;
  speed: number; // step time in units of 10 ms
}

let initialized = false;

// BLE allows one GATT operation at a time, so chain writes in a queue.
let queue: Promise<unknown> = Promise.resolve();
function enqueue<T>(task: () => Promise<T>): Promise<T> {
  const run = queue.then(task, task);
  queue = run.catch(() => undefined);
  return run;
}

function bytes(values: number[]): DataView {
  return new DataView(new Uint8Array(values).buffer);
}

/** Opens the device picker (Chrome) or system scan (Android), then connects. */
export async function connectToDevice(onDisconnect: () => void): Promise<string> {
  if (!initialized) {
    // androidNeverForLocation: we never derive location from BLE scans, so
    // Android 12+ does not need to ask for location permission.
    await BleClient.initialize({ androidNeverForLocation: true });
    initialized = true;
  }
  const device = await BleClient.requestDevice({ services: [SERVICE_UUID] });
  await BleClient.connect(device.deviceId, () => onDisconnect());
  return device.deviceId;
}

export async function disconnectDevice(deviceId: string): Promise<void> {
  await BleClient.disconnect(deviceId);
}

export function readState(deviceId: string): Promise<DeviceState> {
  return enqueue(async () => {
    const led = await BleClient.read(deviceId, SERVICE_UUID, LED_CHAR_UUID);
    const pat = await BleClient.read(deviceId, SERVICE_UUID, PATTERN_CHAR_UUID);
    return {
      leds: [
        led.getUint8(0) === 1,
        led.getUint8(1) === 1,
        led.getUint8(2) === 1,
      ],
      pattern: pat.getUint8(0),
      speed: pat.byteLength > 1 ? pat.getUint8(1) : 50,
    };
  });
}

export function writeLeds(deviceId: string, leds: boolean[]): Promise<void> {
  return enqueue(() =>
    BleClient.write(
      deviceId,
      SERVICE_UUID,
      LED_CHAR_UUID,
      bytes(leds.map((on) => (on ? 1 : 0))),
    ),
  );
}

export function writePattern(
  deviceId: string,
  pattern: number,
  speed: number,
): Promise<void> {
  return enqueue(() =>
    BleClient.write(
      deviceId,
      SERVICE_UUID,
      PATTERN_CHAR_UUID,
      bytes([pattern, speed]),
    ),
  );
}

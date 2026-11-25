import { useCallback, useEffect, useRef, useState } from 'react';
import {
  IonApp,
  IonButton,
  IonCard,
  IonCardContent,
  IonCardHeader,
  IonCardTitle,
  IonChip,
  IonContent,
  IonHeader,
  IonItem,
  IonLabel,
  IonPage,
  IonRange,
  IonSegment,
  IonSegmentButton,
  IonTitle,
  IonToast,
  IonToggle,
  IonToolbar,
} from '@ionic/react';
import {
  APP_TITLE,
  MUSIC_PATTERN,
  PATTERNS,
  connectToDevice,
  disconnectDevice,
  readState,
  writeLeds,
  writePattern,
} from './ble';

type Leds = [boolean, boolean, boolean];

const LED_INFO = [
  { name: 'Red', color: '#e53935' },
  { name: 'White', color: '#ffffff' },
  { name: 'Green', color: '#2e9d4a' },
];

export default function App() {
  const [deviceId, setDeviceId] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [leds, setLeds] = useState<Leds>([false, false, false]);
  const [pattern, setPattern] = useState(0);
  const [speed, setSpeed] = useState(50); // x10 ms
  const [toast, setToast] = useState<string | null>(null);

  // Lets the disconnect callback ignore stale connections.
  const activeId = useRef<string | null>(null);

  const fail = useCallback((err: unknown) => {
    const msg = err instanceof Error ? err.message : String(err);
    setToast(msg);
  }, []);

  const handleConnect = async () => {
    setBusy(true);
    try {
      const id = await connectToDevice(() => {
        activeId.current = null;
        setDeviceId(null);
        setToast('Disconnected from the ESP32');
      });
      activeId.current = id;
      const state = await readState(id);
      setLeds(state.leds);
      setPattern(state.pattern);
      setSpeed(state.speed);
      setDeviceId(id);
    } catch (err) {
      fail(err);
    } finally {
      setBusy(false);
    }
  };

  const handleDisconnect = async () => {
    if (!deviceId) return;
    try {
      await disconnectDevice(deviceId);
    } catch (err) {
      fail(err);
    }
    activeId.current = null;
    setDeviceId(null);
  };

  const toggleLed = (index: number, on: boolean) => {
    if (!deviceId) return;
    const next = [...leds] as Leds;
    next[index] = on;
    setLeds(next);
    setPattern(0); // writing LED state puts the firmware in manual mode
    writeLeds(deviceId, next).catch(fail);
  };

  const choosePattern = (id: number) => {
    if (!deviceId) return;
    setPattern(id);
    writePattern(deviceId, id, speed).catch(fail);
  };

  const playing = pattern === MUSIC_PATTERN;

  const toggleMusic = () => {
    if (!deviceId) return;
    if (playing) {
      // Writing the manual pattern stops the song and silences the speaker.
      setPattern(0);
      setLeds([false, false, false]);
      writeLeds(deviceId, [false, false, false]).catch(fail);
    } else {
      setPattern(MUSIC_PATTERN);
      writePattern(deviceId, MUSIC_PATTERN, speed).catch(fail);
    }
  };

  // While the song plays, poll so the button resets when the ESP32 finishes.
  useEffect(() => {
    if (!deviceId || pattern !== MUSIC_PATTERN) return;
    const timer = setInterval(() => {
      readState(deviceId)
        .then((state) => {
          if (activeId.current !== deviceId) return;
          if (state.pattern !== MUSIC_PATTERN) {
            setPattern(state.pattern);
            setLeds(state.leds);
          }
        })
        .catch(() => undefined);
    }, 1000);
    return () => clearInterval(timer);
  }, [deviceId, pattern]);

  const connected = deviceId !== null;

  return (
    <IonApp>
      <IonPage>
        <IonHeader>
          <IonToolbar>
            <IonTitle>{APP_TITLE}</IonTitle>
          </IonToolbar>
        </IonHeader>

        <IonContent className="ion-padding">
          <IonCard>
            <IonCardHeader>
              <IonCardTitle>Connection</IonCardTitle>
            </IonCardHeader>
            <IonCardContent>
              <IonChip color={connected ? 'success' : 'medium'}>
                <IonLabel>{connected ? 'Connected' : 'Not connected'}</IonLabel>
              </IonChip>
              <div style={{ marginTop: 12 }}>
                {connected ? (
                  <IonButton color="medium" onClick={handleDisconnect}>
                    Disconnect
                  </IonButton>
                ) : (
                  <IonButton disabled={busy} onClick={handleConnect}>
                    {busy ? 'Connecting...' : 'Connect to XIAO-LED'}
                  </IonButton>
                )}
              </div>
            </IonCardContent>
          </IonCard>

          <IonCard>
            <IonCardHeader>
              <IonCardTitle>LEDs</IonCardTitle>
            </IonCardHeader>
            <IonCardContent>
              {leds.map((on, i) => (
                <IonItem key={i}>
                  <IonToggle
                    checked={on}
                    disabled={!connected}
                    onIonChange={(e) => toggleLed(i, e.detail.checked)}
                  >
                    <span
                      style={{
                        display: 'inline-block',
                        width: 14,
                        height: 14,
                        borderRadius: '50%',
                        marginRight: 10,
                        verticalAlign: 'middle',
                        background: LED_INFO[i].color,
                        border: '1px solid #888',
                      }}
                    />
                    {LED_INFO[i].name}
                  </IonToggle>
                </IonItem>
              ))}
            </IonCardContent>
          </IonCard>

          <IonCard>
            <IonCardHeader>
              <IonCardTitle>Music</IonCardTitle>
            </IonCardHeader>
            <IonCardContent>
              <IonButton
                expand="block"
                color={playing ? 'danger' : 'primary'}
                disabled={!connected}
                onClick={toggleMusic}
              >
                {playing ? 'Stop' : 'Play music'}
              </IonButton>
              <p style={{ marginTop: 8, fontSize: 13, opacity: 0.7 }}>
                The speaker plays the tune while the red, white and green LEDs flash with the notes.
              </p>
            </IonCardContent>
          </IonCard>

          <IonCard>
            <IonCardHeader>
              <IonCardTitle>Patterns</IonCardTitle>
            </IonCardHeader>
            <IonCardContent>
              <IonSegment
                value={String(pattern)}
                disabled={!connected}
                scrollable
                onIonChange={(e) => {
                  const id = Number(e.detail.value);
                  if (!Number.isNaN(id) && id !== pattern) choosePattern(id);
                }}
              >
                {PATTERNS.map((p) => (
                  <IonSegmentButton key={p.id} value={String(p.id)}>
                    <IonLabel>{p.label}</IonLabel>
                  </IonSegmentButton>
                ))}
              </IonSegment>

              <IonItem lines="none">
                <IonRange
                  label={`Step time: ${speed * 10} ms`}
                  labelPlacement="stacked"
                  min={10}
                  max={100}
                  step={5}
                  value={speed}
                  disabled={!connected || playing}
                  onIonChange={(e) => setSpeed(e.detail.value as number)}
                  onIonKnobMoveEnd={(e) => {
                    if (deviceId) {
                      writePattern(deviceId, pattern, e.detail.value as number).catch(fail);
                    }
                  }}
                />
              </IonItem>
            </IonCardContent>
          </IonCard>
        </IonContent>

        <IonToast
          isOpen={toast !== null}
          message={toast ?? ''}
          duration={3500}
          onDidDismiss={() => setToast(null)}
        />
      </IonPage>
    </IonApp>
  );
}

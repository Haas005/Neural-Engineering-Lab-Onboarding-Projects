# Neural Engineering Lab Onboarding Projects

These are starter projects for new students in the Neural Engineering Lab. Each one is a small build that teaches something the main research needs, like programming a microcontroller, wiring a circuit or sending data wirelessly.

The first project is a warm-up for the control side of the open-hand prosthesis. In that system, an ESP32 will read muscle (EMG) signals and drive a robotic hand. You don't need any embedded systems experience to start.

## Contents

1. [Development environment setup](#development-environment-setup)
2. [Repository layout](#repository-layout)
3. [Working in the repo](#working-in-the-repo)
4. [Adding a project](#adding-a-project)
5. [Project 1: ESP32 Bluetooth LED & Music Demo](#project-1-esp32-bluetooth-led--music-demo)
6. [Contact](#contact)

## Development environment setup

You only need to do this once. The steps are written for Windows. The tools are the same on macOS and Linux, but only Windows has been tested.

### 1. Install the tools

| Tool | What it's for | Link |
|------|---------------|------|
| Git | Cloning the repo and tracking your changes | [git-scm.com](https://git-scm.com/downloads) |
| Arduino IDE 2.x | Writing and uploading the ESP32 firmware | [arduino.cc](https://www.arduino.cc/en/software) |
| Node.js 22 or newer | Running the web app (npm comes with it) | [nodejs.org](https://nodejs.org) |
| Chrome or Edge | Web Bluetooth. Safari, Firefox and iPhones don't support it. | |
| VS Code (optional) | Editing the app code | [code.visualstudio.com](https://code.visualstudio.com) |
| Android Studio (optional) | Running the app on an Android phone | [developer.android.com/studio](https://developer.android.com/studio) |
| KiCad 9 or newer (optional) | Opening the schematic in `docs/kicad/` | [kicad.org](https://www.kicad.org/download/) |

### 2. Check that Git and Node work

Open PowerShell (or Terminal on macOS) and run these three commands:

```
git --version
node -v
npm -v
```

Each one should print a version number, and `node -v` needs to show 22 or higher. If a command isn't found, close the terminal and open a new one. If it still fails, reinstall that tool.

### 3. Add the ESP32 board package to the Arduino IDE

1. Open the Arduino IDE.
2. Click the Boards Manager icon on the left, or go to Tools > Board > Boards Manager.
3. Search for `esp32` and install **esp32 by Espressif Systems**. This project was tested with version 3.1.3.
4. Plug in the XIAO with a USB-C data cable.
5. Go to Tools > Board > esp32 and pick your XIAO, for example `XIAO_ESP32S3`.
6. Go to Tools > Port and pick the COM port that shows up when the board is plugged in.

You don't need to install any extra libraries. The Bluetooth library comes with the board package.

### 4. Get the code

Clone the repo into a plain folder like `C:\dev`, not into OneDrive. OneDrive tries to sync the thousands of small files that npm creates, and that causes strange errors.

```
mkdir C:\dev
cd C:\dev
git clone https://github.com/Haas005/Neural-Engineering-Lab-Onboarding-Projects.git
cd Neural-Engineering-Lab-Onboarding-Projects
```

### 5. Check that it all works

Before you start a project, do a quick test:

1. In the Arduino IDE, open File > Examples > 01.Basics > Blink and click **Upload**. The board's built-in LED should blink.
2. In a terminal, go into the app folder (for Project 1 that's `project-1-ble-led/app`) and run `npm install`. It should finish without errors.

If either one fails, check the Troubleshooting section under Project 1.

## Repository layout

```
.
├── README.md
└── project-1-ble-led/
    ├── firmware/led_ble/led_ble.ino    ESP32 sketch
    ├── app/                            Ionic + React + Capacitor app
    │   └── src/
    │       ├── App.tsx                 The screen
    │       ├── ble.ts                  Bluetooth functions and UUIDs
    │       └── main.tsx                App entry point
    └── docs/
        ├── bill_of_materials.csv
        └── kicad/                      KiCad schematic and library files
```

## Working in the repo

- Work on your own branch or a fork, and open a pull request when you want feedback.
- Keep your changes inside the folder of the project you're working on.
- Write commit messages that say what changed and why.
- Don't commit `node_modules`, build output, passwords or keys. The `.gitignore` covers the usual ones.

## Adding a project

1. Make a folder called `project-N-short-name/` with the next number.
2. Add a section to this README after the last project. Cover the purpose, parts, wiring, how to run it, how it works, extra challenges and troubleshooting.
3. Add the project to the Contents list at the top.

## Project 1: ESP32 Bluetooth LED & Music Demo

This is the classic "blink an LED" project with one change: you control the LEDs from a computer or phone over Bluetooth. A web app talks to a Seeed Studio XIAO ESP32 using Bluetooth Low Energy (BLE). From the app you can switch three LEDs on and off, run light patterns, and play a song on a buzzer while the LEDs flash with the notes.

The app runs in Chrome on a computer, and you can also push it to an Android phone.

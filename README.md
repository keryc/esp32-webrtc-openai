# ESP32-S3 WebRTC OpenAI Real-Time Assistant

An ESP32-S3/P4 based AI assistant that uses WebRTC for real-time audio/video communication with OpenAI's GPT models. This project transforms your ESP32-S3 camera board into an intelligent assistant with voice interaction and visual analysis capabilities.

## Features

- **Real-Time Voice Interaction**: Seamless bidirectional audio communication with OpenAI's real-time API
- **Vision Capabilities**: Camera integration for visual scene analysis and object recognition
- **WebRTC Protocol**: Low-latency, high-quality audio/video streaming using industry-standard WebRTC
- **Multi-Board Support**: Compatible with multiple ESP32-S3/P4 development boards
- **Audio Feedback**: Custom sound effects for system events (mic and player)
- **Console Interface**: Interactive command-line interface for configuration and control
- **Memory Management**: Advanced memory monitoring and optimization for stable operation
- **Modular Architecture**: Clean component-based design for easy customization

## Supported Hardware

### Development Boards

| Board | Flash | PSRAM | Description |
|-------|-------|-------|-------------|
| **FREENOVE ESP32-S3 WROOM** | 8MB | Octal SPI | High-performance board with fast PSRAM |
| **ESP32-S3-N16R8-CAM** | 16MB | Quad SPI | Large flash storage for extended functionality |

### Required Peripherals

- OV2640 or OV5640 camera module
- I2S audio codec (MAX98357 or similar)
- Microphone and speaker (INMP441 or similar)
- USB cable for programming and debugging

## Quick Start

### Prerequisites

1. **ESP-IDF v5.4+**: Install the Espressif IoT Development Framework
   ```bash
   # Follow instructions at https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/
   ```

2. **OpenAI API Key**: Obtain from [OpenAI Platform](https://platform.openai.com)

3. **Git with Submodules Support**: Required for dependencies

### Installation

1. **Clone the repository with submodules**:
   ```bash
   git clone --recursive https://github.com/keryc/esp32-webrtc-openai.git
   cd esp32-webrtc-openai
   ```

2. **Set up environment variables**:
   ```bash
   export OPENAI_API_KEY="your-api-key-here"
   ```

3. **Configure WiFi credentials**:
   ```bash
   # Edit the default configuration
   # Update CONFIG_AG_WIFI_SSID and CONFIG_AG_WIFI_PASSWORD
   nano sdkconfig.defaults
   ```

### Building and Flashing

This project uses an advanced Makefile for simplified build management:

#### Basic Commands

```bash
# Build for FREENOVE board (default)
make build

# Build for N16R8-CAM board
make build BOARD=n16r8cam

# Flash to device (auto-detects port)
make flash

# Monitor serial output
make monitor

# Complete cycle: Build + Flash + Monitor
make all BOARD=freenove
```

#### Production Build

```bash
# Build with production optimizations
make prod BOARD=n16r8cam

# Flash production build
make flash
```

#### Advanced Usage

```bash
# Specify custom port
make flash PORT=/dev/ttyUSB0

# Open configuration menu
make menuconfig

# Clean build files
make clean

# Erase entire flash
make erase

# Show size analysis
make size

# List available ports
make ports
```

## Configuration

### Default Settings

The project includes sensible defaults in `sdkconfig.defaults`:

- **WiFi**: Configure SSID and password
- **Audio**: Volume levels and microphone gain
- **Vision**: Frame rate, quality, and buffer settings
- **WebRTC**: Opus codec support and data channels
- **OpenAI**: Model selection and voice settings

### Board-Specific Configuration

Each board has its own configuration file:
- `sdkconfig.defaults.esp32s3.freenove` - FREENOVE board settings
- `sdkconfig.defaults.esp32s3.n16r8cam` - N16R8-CAM board settings

### Audio Volume Configuration

Configure audio volume levels in `sdkconfig.defaults`:

```bash
# Speaker/Playback Volume (0-100)
CONFIG_AG_AUDIO_DEFAULT_PLAYBACK_VOL=95

# Microphone Gain (0-100)
CONFIG_AG_AUDIO_DEFAULT_MIC_GAIN=95
```

You can also adjust these at runtime using console commands:
- `audio volume <0-100>` - Set speaker volume
- `audio gain <0-100>` - Set microphone gain

### OpenAI Settings

Configure the AI assistant behavior:
- **Model**: `gpt-realtime` (default)
- **Voice**: `ash` (customizable)
- **Tool Choice**: `auto` (function calling)
- **Vision**: Automatic scene analysis with `look_around` function

### Customizing AI Prompts

You can customize the AI assistant's personality and behavior by editing `components/webrtc/prompts.h`:

```c
// Audio-only mode prompt
#define INSTRUCTIONS_AUDIO_ONLY "You are Jarvis, a AI resident in your wearer's glasses (mode audio)."

// Audio + Vision mode prompt  
#define INSTRUCTIONS_AUDIO_VISION "You are Jarvis, a AI resident in your wearer's glasses (mode audio+vision). You can see using the function look_around."

// Vision function configuration
#define VISION_FUNCTION_NAME "look_around"
#define VISION_FUNCTION_DESCRIPTION "Gets a detailed description of the user's current field of view."
```

Modify these prompts to:
- Change the AI assistant's personality (e.g., from "Jarvis" to your custom assistant)
- Adjust the interaction style and tone
- Add specific instructions or constraints
- Customize the vision analysis behavior

## Project Structure

```
esp32-webrtc-openai/
├── main/                      # Main application
│   └── main.c                 # Entry point and initialization
├── components/                # Modular components
│   ├── audio/                 # Audio processing and feedback
│   ├── system/                # System utilities and console
│   ├── vision/                # Camera and image processing
│   ├── webrtc/                # WebRTC and OpenAI integration
│   └── wifi/                  # WiFi management
├── third_party/               # External dependencies
│   └── esp-webrtc-solution/  # WebRTC implementation (submodule)
├── spiffs/                    # SPIFFS filesystem
│   └── sounds/                # Audio feedback files
├── Makefile                   # Advanced build system
├── CMakeLists.txt            # CMake configuration
├── partitions.csv            # Flash partition table
└── sdkconfig.defaults*       # Configuration files
```

## Console Commands

The interactive console provides various commands for runtime control:

### WiFi Commands
- `wifi connect <ssid> <password>` - Connect to WiFi network
- `wifi disconnect` - Disconnect from WiFi
- `wifi status` - Show connection status
- `wifi scan` - Scan for available networks

### WebRTC Commands
- `webrtc start` - Start WebRTC session
- `webrtc stop` - Stop WebRTC session
- `webrtc status` - Show WebRTC status
- `webrtc send <message>` - Send text message

### Camera Commands
- `cam start` - Start camera stream
- `cam stop` - Stop camera stream
- `cam capture` - Capture single frame
- `cam preview` - Start HTTP preview server

### Audio Commands
- `audio volume <0-100>` - Set speaker volume
- `audio gain <0-100>` - Set microphone gain
- `audio mute` - Mute microphone
- `audio unmute` - Unmute microphone

### System Commands
- `sys info` - Show system information
- `sys mem` - Display memory usage
- `sys tasks` - List running tasks
- `sys restart` - Restart the device

## Dependencies

This project relies on:

- **[esp-webrtc-solution](https://github.com/keryc/esp-webrtc-solution)**: Fork of Espressif's WebRTC implementation (included as submodule)
- **ESP-IDF v5.4+**: Espressif IoT Development Framework
- **esp32-camera**: Camera driver component
- **OpenAI Real-Time API**: For AI processing

## Adding Support for New Boards

To add support for a new ESP32-S3/P4 board, follow these steps:

### 1. Create Board Configuration File

Create a new sdkconfig file for your board:
```bash
# Create board-specific configuration
nano sdkconfig.defaults.[IDF_TARGET].[YOUR-BOARD]
```

Add board-specific settings:
```bash
# Board Identification
CONFIG_AG_SYSTEM_BOARD_NAME="YOUR_BOARD_NAME"

# Flash Configuration
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y

# SPIRAM Configuration (Quad or Octal)
CONFIG_SPIRAM_MODE_QUAD=y  # or CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
```

### 2. Update Codec Board Configuration

Edit `third_party/esp-webrtc-solution/components/codec_board/board_cfg.txt`:
```
Board: YOUR_BOARD_NAME
i2c: {sda: 17, scl: 18}
i2s: {mclk: 16, bclk: 9, ws: 45, din: 10, dout: 8}
out: {codec: MAX98357, pa: 48, pa_gain: 6, use_mclk: 0}
in: {codec: INMP441}
camera: {type: dvp, xclk: 40, pclk: 11, vsync: 21, href: 38, 
         d0: 13, d1: 47, d2: 14, d3: 3, d4: 12, d5: 42, d6: 41, d7: 39}
```

Adjust pin assignments according to your board's schematic.

### 3. Update Makefile

Add your board to the Makefile:
```makefile
# Add to VALID_BOARDS list (line 30)
VALID_BOARDS := freenove n16r8cam yourboard

# Add board configuration mapping (after line 42)
else ifeq ($(BOARD),yourboard)
    BOARD_CONFIG := yourboard
    BOARD_FULL_NAME := YOUR_BOARD_FULL_NAME
```

### 5. Test Your Configuration

```bash
# Build for your new board
make build BOARD=yourboard

# Flash and monitor
make all BOARD=yourboard
```

## Memory Optimization

The project includes aggressive memory optimization for stable operation:

- Dynamic buffer allocation
- Reduced WiFi and LWIP buffers
- Optimized task stack sizes
- Runtime memory monitoring
- Automatic garbage collection

## Audio Feedback

Custom audio feedback is provided through SPIFFS:
- `starting.wav` - Played when system initializes
- Additional sounds can be added to `/spiffs/sounds/`

## Security Considerations

- **API Key**: Store OpenAI API key as environment variable only
- **WiFi Credentials**: Use NVS encryption for production
- **HTTPS**: All OpenAI communication uses TLS
- **WebRTC**: DTLS-SRTP encryption for media streams

## Troubleshooting

### Common Issues

1. **Build Fails**: Ensure ESP-IDF v5.4+ is installed and activated
2. **Submodule Missing**: Run `git submodule update --init --recursive`
3. **Port Not Found**: Use `make ports` to list available serial ports
4. **Memory Issues**: Reduce video quality or frame rate in configuration
5. **WiFi Connection**: Check credentials and signal strength

### Debug Mode

Enable debug logging:
```bash
make menuconfig
# Navigate to Component config → Log output → Default log verbosity → Debug
```

## Contributing

Contributions are welcome! Please follow these guidelines:

1. Fork the repository
2. Create a feature branch
3. Commit your changes
4. Push to the branch
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- **Espressif Systems** for ESP-IDF and WebRTC implementation
- **OpenAI** for the real-time API
- **Community Contributors** for testing and feedback

## Contact

For questions and support, please open an issue on GitHub.

---

**Note**: This project depends on the forked [esp-webrtc-solution](https://github.com/keryc/esp-webrtc-solution) repository as a submodule, which contains necessary WebRTC components not yet available in the ESP-IDF Component Registry.
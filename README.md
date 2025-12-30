# M5PaperS3 + FastEPD Image Display

An automated image display system for the M5Stack PaperS3 e-paper display that fetches images from GitHub and shows them on the device.

## Overview

This project allows you to:
1. Upload vertical images (e.g., from iPhone) to the `input/` folder
2. A Python worker automatically converts them to 540x960 grayscale images
3. The M5PaperS3 periodically checks for new images and displays them
4. Images are archived in the `output/` folder

## Hardware Requirements

- [M5Stack PaperS3](https://docs.m5stack.com/en/core/PaperS3)
- USB-C cable for programming
- WiFi network

## Software Requirements

### For Image Processing (Python Worker)
- Python 3.8+
- Pillow library

### For M5PaperS3 Firmware
- [PlatformIO](https://platformio.org/)
- Arduino framework
- Libraries (auto-installed via platformio.ini):
  - M5Unified
  - FastEPD (optional, currently using M5Unified for JPEG display)
  - ArduinoJson

## Project Structure

```
M5PS3_FEPD/
├── input/           # Drop your images here
├── image/           # Current image (displayed on M5PaperS3)
├── output/          # Archive of all processed images
├── src/
│   ├── main.cpp     # M5PaperS3 firmware
│   ├── config.h     # WiFi credentials (not in git)
│   └── config.h.template  # Template for config
├── worker.py        # Image processing script
├── requirements.txt # Python dependencies
└── platformio.ini   # PlatformIO configuration
```

## Setup Instructions

### 1. Clone and Setup Repository

```bash
git clone https://github.com/marcelloemme/M5PS3_FEPD.git
cd M5PS3_FEPD
```

### 2. Setup Python Worker

```bash
# Create virtual environment (optional but recommended)
python3 -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate

# Install dependencies
pip install -r requirements.txt
```

### 3. Configure M5PaperS3 Firmware

```bash
# Copy config template
cp src/config.h.template src/config.h

# Edit src/config.h with your WiFi credentials
# Change WIFI_SSID and WIFI_PASSWORD
```

### 4. Build and Upload Firmware

```bash
# Using PlatformIO CLI
pio run -t upload

# Or use PlatformIO IDE in VS Code
```

## Usage

### Adding Images

1. **From iPhone**: Save vertical photos to your computer or directly upload to the GitHub repo's `input/` folder

2. **Process images** using the Python worker:
   ```bash
   # Single run (processes all images in input/)
   python worker.py

   # Watch mode (continuously monitors input/)
   python worker.py watch
   ```

3. **Commit and push** the processed image to GitHub:
   ```bash
   git add image/current.jpg
   git commit -m "Update display image"
   git push
   ```

### How It Works

1. **Image Upload**: Place vertical images (JPG/PNG) in `input/` folder
2. **Processing**: Worker script:
   - Crops image to 9:16 aspect ratio (center crop)
   - Resizes to 540x960 pixels
   - Converts to 4-bit grayscale (16 levels)
   - Saves as JPG to `image/current.jpg`
   - Archives previous image to `output/` with timestamp
3. **Display**: M5PaperS3:
   - Wakes from deep sleep every 30 minutes
   - Connects to WiFi
   - Downloads `image/current.jpg` from GitHub
   - Compares SHA256 hash with previous image
   - If different, displays new image
   - Returns to deep sleep

### Automation with GitHub Actions

You can automate the image processing using GitHub Actions. Create `.github/workflows/process-images.yml`:

```yaml
name: Process Images

on:
  push:
    paths:
      - 'input/**'

jobs:
  process:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - uses: actions/setup-python@v4
        with:
          python-version: '3.10'
      - run: pip install -r requirements.txt
      - run: python worker.py
      - uses: stefanzweifel/git-auto-commit-action@v4
        with:
          commit_message: "Auto-process images"
```

## Configuration

### M5PaperS3 Settings (src/config.h)

- `WIFI_SSID`: Your WiFi network name
- `WIFI_PASSWORD`: Your WiFi password
- `IMAGE_URL`: URL to the raw image on GitHub
- `SLEEP_DURATION_US`: Time between updates (default: 30 minutes)
- `DISPLAY_ROTATION`: Display orientation (0-3)

### Worker Settings (worker.py)

- `TARGET_WIDTH`: 540 (M5PaperS3 width)
- `TARGET_HEIGHT`: 960 (M5PaperS3 height)
- `CURRENT_IMAGE_NAME`: "current.jpg"

## Troubleshooting

### M5PaperS3 Not Updating

1. Check serial monitor for error messages
2. Verify WiFi credentials in `src/config.h`
3. Ensure `IMAGE_URL` is accessible (try opening in browser)
4. Check that image is committed and pushed to GitHub

### Image Quality Issues

- Use high-resolution source images (at least 540x960)
- Vertical images work best
- The worker converts to 4-bit grayscale (16 shades)

### Worker Not Processing

- Ensure Pillow is installed: `pip install Pillow`
- Check that input files are valid images
- Look for error messages in console

## Power Consumption

The M5PaperS3 uses deep sleep between updates:
- Active time: ~10-15 seconds (WiFi + download + display)
- Sleep time: 30 minutes (configurable)
- Estimated battery life: Several weeks on battery

## License

MIT License

## Credits

- Inspired by [WS_images](https://github.com/marcelloemme/WS_images)
- Uses [FastEPD](https://github.com/bitbank2/FastEPD) library by Larry Bank
- Built for [M5Stack PaperS3](https://docs.m5stack.com/en/core/PaperS3)

## Contributing

Feel free to open issues or submit pull requests!

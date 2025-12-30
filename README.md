# M5PaperS3 + FastEPD Image Display

An automated image display system for the M5Stack PaperS3 e-paper display that fetches images from GitHub and shows them on the device.

## Overview

This project allows you to:
1. Upload vertical images from iPhone (timestamped: YYYYMMDD_HHMMSS.jpg) to the `input/` folder
2. A Python worker automatically converts them to 540x960 4-bit grayscale images
3. Processed images keep their original timestamp filename
4. The M5PaperS3 periodically checks GitHub for the latest image and displays it
5. Previous images are archived in the `output/` folder

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
├── input/           # Drop timestamped images here (YYYYMMDD_HHMMSS.jpg)
├── image/           # Latest processed image (M5PaperS3 downloads from here)
├── output/          # Archive of all previous processed images
├── src/
│   ├── main.cpp     # M5PaperS3 firmware
│   ├── config.h     # WiFi and GitHub config (not in git)
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

1. **From iPhone**:
   - Use Shortcuts to rename and upload photos with timestamp format: `YYYYMMDD_HHMMSS.jpg`
   - Upload to the GitHub repo's `input/` folder via git or directly on GitHub web

2. **Process images** using the Python worker:
   ```bash
   # Single run (processes only new images in input/)
   python worker.py

   # Watch mode (continuously monitors input/ for new images)
   python worker.py watch
   ```
   The worker only processes new files, not existing ones - it's optimized!

3. **Commit and push** the processed image to GitHub:
   ```bash
   git add image/ output/
   git commit -m "Update display image"
   git push
   ```

### How It Works

1. **Image Upload**: Upload vertical images (JPG/PNG) to `input/` with timestamp filename
2. **Processing**: Worker script:
   - Monitors `input/` folder for new images
   - Crops image to 9:16 aspect ratio (center crop)
   - Resizes to 540x960 pixels
   - Converts to 4-bit grayscale (16 levels)
   - Saves as JPG to `image/` **keeping the original timestamp filename**
   - Moves previous image from `image/` to `output/` archive
   - Deletes processed file from `input/`
3. **Display**: M5PaperS3:
   - Wakes from deep sleep every 30 minutes (configurable)
   - Connects to WiFi
   - Queries GitHub API to find the latest image in `image/` folder
   - Compares filename with last displayed image
   - If different, downloads and displays new image
   - Stores filename in RTC memory
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
- `GITHUB_USER`: Your GitHub username (default: "marcelloemme")
- `GITHUB_REPO`: Repository name (default: "M5PS3_FEPD")
- `GITHUB_BRANCH`: Branch name (default: "main")
- `SLEEP_DURATION_US`: Time between updates (default: 30 minutes)
- `DISPLAY_ROTATION`: Display orientation (0-3)

### Worker Settings (worker.py)

- `TARGET_WIDTH`: 540 (M5PaperS3 width)
- `TARGET_HEIGHT`: 960 (M5PaperS3 height)
- Images preserve original timestamp filenames

## Troubleshooting

### M5PaperS3 Not Updating

1. Check serial monitor for error messages
2. Verify WiFi credentials in `src/config.h`
3. Ensure GitHub repository is public or accessible
4. Check that processed image is committed and pushed to GitHub `image/` folder
5. Verify GitHub API is accessible (check URL in serial monitor)

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

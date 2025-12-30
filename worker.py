#!/usr/bin/env python3
"""
Image processing worker for M5PaperS3
Monitors input/ folder, converts images to 540x960 4-bit grayscale JPG,
manages image/ folder (single current image) and output/ archive
"""

import os
import sys
import time
import shutil
from pathlib import Path
from PIL import Image, ImageEnhance
import hashlib

# Configuration
INPUT_DIR = Path("input")
IMAGE_DIR = Path("image")
OUTPUT_DIR = Path("output")
TARGET_WIDTH = 540
TARGET_HEIGHT = 960
CURRENT_IMAGE_NAME = "current.jpg"

def calculate_hash(file_path):
    """Calculate SHA256 hash of a file"""
    sha256_hash = hashlib.sha256()
    with open(file_path, "rb") as f:
        for byte_block in iter(lambda: f.read(4096), b""):
            sha256_hash.update(byte_block)
    return sha256_hash.hexdigest()

def center_crop(img, target_width, target_height):
    """
    Center crop image to target dimensions maintaining aspect ratio
    """
    img_width, img_height = img.size
    target_ratio = target_width / target_height
    img_ratio = img_width / img_height

    if img_ratio > target_ratio:
        # Image is wider than target, crop width
        new_width = int(img_height * target_ratio)
        left = (img_width - new_width) // 2
        img = img.crop((left, 0, left + new_width, img_height))
    else:
        # Image is taller than target, crop height
        new_height = int(img_width / target_ratio)
        top = (img_height - new_height) // 2
        img = img.crop((0, top, img_width, top + new_height))

    return img

def convert_to_4bit_grayscale(img):
    """
    Convert image to 4-bit grayscale (16 levels)
    """
    # Convert to grayscale
    img = img.convert('L')

    # Quantize to 16 levels (4-bit)
    # Map 256 levels to 16 levels
    img = img.point(lambda x: (x // 16) * 17)

    return img

def process_image(input_path, output_path):
    """
    Process a single image: crop, resize, convert to 4-bit grayscale
    """
    print(f"Processing {input_path.name}...")

    # Open and auto-rotate based on EXIF
    img = Image.open(input_path)
    img = img.convert('RGB')  # Ensure RGB mode

    # Auto-rotate based on EXIF orientation
    try:
        img = img._getexif()
        if img is not None:
            for orientation in [274]:  # EXIF orientation tag
                exif = dict(img._getexif().items())
                if orientation in exif:
                    if exif[orientation] == 3:
                        img = img.rotate(180, expand=True)
                    elif exif[orientation] == 6:
                        img = img.rotate(270, expand=True)
                    elif exif[orientation] == 8:
                        img = img.rotate(90, expand=True)
    except (AttributeError, KeyError, TypeError):
        pass

    # Reload image (EXIF handling workaround)
    img = Image.open(input_path)

    # Center crop to target aspect ratio
    img = center_crop(img, TARGET_WIDTH, TARGET_HEIGHT)

    # Resize to exact target dimensions
    img = img.resize((TARGET_WIDTH, TARGET_HEIGHT), Image.Resampling.LANCZOS)

    # Convert to 4-bit grayscale
    img = convert_to_4bit_grayscale(img)

    # Save as JPG
    img.save(output_path, 'JPEG', quality=85, optimize=True)
    print(f"Saved to {output_path}")

def archive_current_image():
    """
    Move current image from image/ to output/ with timestamp
    """
    current_image_path = IMAGE_DIR / CURRENT_IMAGE_NAME

    if current_image_path.exists():
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        archive_name = f"image_{timestamp}.jpg"
        archive_path = OUTPUT_DIR / archive_name

        shutil.move(str(current_image_path), str(archive_path))
        print(f"Archived current image to {archive_path}")

def process_new_images():
    """
    Process all new images in input/ folder
    """
    input_files = sorted(INPUT_DIR.glob("*.jpg")) + sorted(INPUT_DIR.glob("*.jpeg")) + \
                  sorted(INPUT_DIR.glob("*.JPG")) + sorted(INPUT_DIR.glob("*.JPEG")) + \
                  sorted(INPUT_DIR.glob("*.png")) + sorted(INPUT_DIR.glob("*.PNG"))

    if not input_files:
        print("No new images found in input/")
        return

    for input_file in input_files:
        try:
            # Archive current image if exists
            archive_current_image()

            # Process new image to image/current.jpg
            output_path = IMAGE_DIR / CURRENT_IMAGE_NAME
            process_image(input_file, output_path)

            # Remove processed file from input
            input_file.unlink()
            print(f"Removed {input_file.name} from input/")

        except Exception as e:
            print(f"Error processing {input_file.name}: {e}")
            continue

def watch_mode():
    """
    Continuously watch input/ folder for new images
    """
    print("Starting worker in watch mode...")
    print(f"Monitoring {INPUT_DIR.absolute()}")
    print("Press Ctrl+C to stop")

    try:
        while True:
            process_new_images()
            time.sleep(5)  # Check every 5 seconds
    except KeyboardInterrupt:
        print("\nWorker stopped")

def main():
    """
    Main entry point
    """
    # Ensure directories exist
    INPUT_DIR.mkdir(exist_ok=True)
    IMAGE_DIR.mkdir(exist_ok=True)
    OUTPUT_DIR.mkdir(exist_ok=True)

    if len(sys.argv) > 1 and sys.argv[1] == "watch":
        watch_mode()
    else:
        # Single run mode
        process_new_images()

if __name__ == "__main__":
    main()

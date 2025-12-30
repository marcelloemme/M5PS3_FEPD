#!/usr/bin/env python3
"""
Image processing worker for M5PaperS3
Monitors input/ folder, converts images to 540x960 4-bit grayscale JPG,
preserves original filenames with timestamp format (YYYYMMDD_HHMMSS.jpg)
Archives previous images to output/ folder
"""

import os
import sys
import time
import shutil
from pathlib import Path
from PIL import Image, ImageOps, ImageEnhance
import numpy as np

# Configuration
INPUT_DIR = Path("input")
IMAGE_DIR = Path("image")
OUTPUT_DIR = Path("output")
TARGET_WIDTH = 540
TARGET_HEIGHT = 960

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

def auto_levels(img):
    """
    Auto-levels adjustment like Photoshop: stretch histogram to use full 0-255 range
    Maps darkest pixel to 0 and brightest pixel to 255
    """
    pixels = np.array(img, dtype=np.float32)

    # Find current min and max values
    min_val = pixels.min()
    max_val = pixels.max()

    # If already using full range, skip
    if min_val == 0 and max_val == 255:
        return img

    # Stretch histogram to 0-255
    # Formula: (pixel - min) * 255 / (max - min)
    if max_val > min_val:  # Avoid division by zero
        pixels = (pixels - min_val) * 255.0 / (max_val - min_val)

    pixels = np.clip(pixels, 0, 255).astype(np.uint8)
    return Image.fromarray(pixels, mode='L')

def convert_to_4bit_grayscale(img):
    """
    Convert image to 4-bit grayscale (16 levels) with Floyd-Steinberg dithering
    Uses manual dithering implementation for better control
    """
    # Convert to grayscale
    img = img.convert('L')

    # Apply auto-levels to maximize contrast
    img = auto_levels(img)

    # Convert to numpy array for manipulation
    pixels = np.array(img, dtype=np.float32)
    height, width = pixels.shape

    # Floyd-Steinberg dithering
    for y in range(height):
        for x in range(width):
            old_pixel = pixels[y, x]

            # Quantize to 16 levels (0, 17, 34, ..., 255)
            new_pixel = np.round(old_pixel / 17) * 17
            pixels[y, x] = new_pixel

            # Calculate quantization error
            error = old_pixel - new_pixel

            # Distribute error to neighboring pixels
            if x + 1 < width:
                pixels[y, x + 1] += error * 7 / 16
            if y + 1 < height:
                if x > 0:
                    pixels[y + 1, x - 1] += error * 3 / 16
                pixels[y + 1, x] += error * 5 / 16
                if x + 1 < width:
                    pixels[y + 1, x + 1] += error * 1 / 16

    # Clip values to valid range and convert back
    pixels = np.clip(pixels, 0, 255).astype(np.uint8)
    img = Image.fromarray(pixels, mode='L')

    return img

def process_image(input_path, output_path):
    """
    Process a single image: crop, resize, convert to 4-bit grayscale
    Automatically handles EXIF orientation from iPhone
    """
    print(f"Processing {input_path.name}...")

    # Open image
    img = Image.open(input_path)

    # Auto-rotate based on EXIF orientation tag
    # This handles images rotated by iPhone/camera
    img = ImageOps.exif_transpose(img)

    # Ensure RGB mode
    if img.mode != 'RGB':
        img = img.convert('RGB')

    # Center crop to target aspect ratio
    img = center_crop(img, TARGET_WIDTH, TARGET_HEIGHT)

    # Resize to exact target dimensions
    img = img.resize((TARGET_WIDTH, TARGET_HEIGHT), Image.Resampling.LANCZOS)

    # Convert to 4-bit grayscale
    img = convert_to_4bit_grayscale(img)

    # Save as JPG
    img.save(output_path, 'JPEG', quality=85, optimize=True)
    print(f"Saved to {output_path}")

def get_latest_image_in_folder(folder):
    """
    Get the latest image file in a folder (by filename, assuming YYYYMMDD_HHMMSS.jpg format)
    """
    image_files = sorted(folder.glob("*.jpg")) + sorted(folder.glob("*.jpeg")) + \
                  sorted(folder.glob("*.JPG")) + sorted(folder.glob("*.JPEG"))

    # Filter out .gitkeep
    image_files = [f for f in image_files if f.name != '.gitkeep']

    if image_files:
        # Return latest (last in sorted order, which matches timestamp format)
        return sorted(image_files)[-1]
    return None

def archive_old_images():
    """
    Move ALL images from image/ to output/ before processing new one
    """
    image_files = sorted(IMAGE_DIR.glob("*.jpg")) + sorted(IMAGE_DIR.glob("*.jpeg")) + \
                  sorted(IMAGE_DIR.glob("*.JPG")) + sorted(IMAGE_DIR.glob("*.JPEG"))

    # Filter out .gitkeep
    image_files = [f for f in image_files if f.name != '.gitkeep']

    # Archive all images found in image/
    for img_file in image_files:
        archive_path = OUTPUT_DIR / img_file.name
        shutil.move(str(img_file), str(archive_path))
        print(f"Archived {img_file.name} to output/")

def process_new_images():
    """
    Process all new images in input/ folder, preserving original filenames
    Only processes images that don't already exist in image/ folder
    Keeps original images in input/ folder
    """
    input_files = sorted(INPUT_DIR.glob("*.jpg")) + sorted(INPUT_DIR.glob("*.jpeg")) + \
                  sorted(INPUT_DIR.glob("*.JPG")) + sorted(INPUT_DIR.glob("*.JPEG")) + \
                  sorted(INPUT_DIR.glob("*.png")) + sorted(INPUT_DIR.glob("*.PNG"))

    # Filter out .gitkeep
    input_files = [f for f in input_files if f.name != '.gitkeep']

    if not input_files:
        return  # Silently return, no new images

    print(f"Found {len(input_files)} new image(s) in input/")

    for input_file in input_files:
        try:
            # Keep original filename but change extension to .jpg
            original_name = input_file.stem + ".jpg"
            output_path = IMAGE_DIR / original_name

            # Check if already processed
            if output_path.exists():
                print(f"Skipping {input_file.name} (already exists in image/)")
                continue

            # Archive old images (move previous image to output/)
            archive_old_images()

            # Process new image preserving the filename
            process_image(input_file, output_path)

            print(f"Processed {input_file.name} -> image/{original_name}")

        except Exception as e:
            print(f"Error processing {input_file.name}: {e}")
            import traceback
            traceback.print_exc()
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

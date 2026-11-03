from PIL import Image, ImageDraw, ImageFont
import argparse
import sys
import os

# vis cfg
BG_COLOR = (255, 255, 255)
BORDER_COLOR = (0, 128, 0)
DOT_COLOR = (0, 0, 0)

TITLE_FONT_SIZE = 40
TITLE_HEIGHT = 80
MARGIN = 20
BORDER_THICKNESS = 5

# RGB colors for wires, I copied this from prev semester java code
WIRE_COLORS = [
    (0, 0, 255),    # Blue
    (0, 0, 0),      # Black
    (0, 128, 0),    # Green
    (255, 0, 0),    # Red
    (0, 255, 255),  # Cyan
    (255, 0, 255),  # Magenta
    (90, 200, 90),  # Light Green
    (187, 92, 80),  # Not sure
    (90, 88, 177)   # Hmmmmm
]

def parse_wire_file(filename):
    wires = []
    dims = (0, 0)
    if not os.path.exists(filename): 
        print(f"Error: {filename} not found")
        sys.exit(1)
        
    with open(filename, 'r') as f:
        lines = [l.strip() for l in f.readlines() if l.strip()]
        if len(lines) < 2: sys.exit(1)
        
        try:
            dims = (int(lines[0].split()[0]), int(lines[0].split()[1]))
        except:
            print("Error parsing dimensions")
            sys.exit(1)
            
        for line in lines[2:]:
            parts = line.split()
            if not parts: continue
            try: coords = [int(p) for p in parts]
            except: continue
            if len(coords) < 4: continue
            points = [(coords[i], coords[i+1]) for i in range(0, len(coords), 2)]
            wires.append(points)
    return dims, wires

def draw_wires(dims, wires, output_filename, scale_factor=2):
    grid_w, grid_h = dims
    area_w = grid_w * scale_factor
    area_h = grid_h * scale_factor
    
    img_w = area_w + (2 * MARGIN)
    img_h = area_h + (2 * MARGIN) + TITLE_HEIGHT
    
    print(f"Creating image {img_w}x{img_h}...")
    img = Image.new('RGB', (img_w, img_h), BG_COLOR)
    draw = ImageDraw.Draw(img)

    # title
    title_text = f"Wire Routing: {grid_w}x{grid_h} Grid, {len(wires)} Wires"
    
    try:
        # linux/mac usually have arial or dejavu
        font = ImageFont.truetype("arial.ttf", TITLE_FONT_SIZE)
    except IOError:
        try:
            font = ImageFont.truetype("DejaVuSans.ttf", TITLE_FONT_SIZE)
        except IOError:
            # fall back
            print("Warning: Custom fonts not found, using default.")
            font = ImageFont.load_default()

    draw.text((MARGIN, MARGIN), title_text, fill=(0,0,0), font=font)

    # adjust coordinate
    origin_x = MARGIN
    origin_y = TITLE_HEIGHT + MARGIN
    offset = scale_factor // 2

    # draw bbox
    draw.rectangle(
        [
            (origin_x - BORDER_THICKNESS, origin_y - BORDER_THICKNESS), 
            (origin_x + area_w + BORDER_THICKNESS, origin_y + area_h + BORDER_THICKNESS)
        ], 
        outline=BORDER_COLOR, 
        width=BORDER_THICKNESS
    )

    # draa wire
    print(f"Drawing {len(wires)} wires...")
    line_width = max(1, int(scale_factor / 2)) 

    for i, wire in enumerate(wires):
        color = WIRE_COLORS[i % len(WIRE_COLORS)]
        
        scaled_points = []
        for x, y in wire:
            px = (x * scale_factor) + offset + origin_x
            py = (y * scale_factor) + offset + origin_y
            scaled_points.append((px, py))
        
        draw.line(scaled_points, fill=color, width=line_width, joint='curve')
        
        # draw endpoints
        r = line_width + 1
        sx, sy = scaled_points[0]
        ex, ey = scaled_points[-1]
        
        draw.ellipse((sx-r, sy-r, sx+r, sy+r), fill=DOT_COLOR)
        draw.ellipse((ex-r, ey-r, ex+r, ey+r), fill=DOT_COLOR)

    if output_filename:
        folder = os.path.dirname(output_filename)
        if folder and not os.path.exists(folder): os.makedirs(folder)
        print(f"Saving to {output_filename}...")
        img.save(output_filename)
    else:
        img.show()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--wires_output_file', type=str, default='output/wire_output.txt')
    parser.add_argument('--wires_output_plot', type=str, default='output/wire_plot.png')
    parser.add_argument('--scale', type=int, default=4, help='Pixels per grid unit') 
    args = parser.parse_args()

    dims, wires = parse_wire_file(args.wires_output_file)
    draw_wires(dims, wires, args.wires_output_plot, args.scale)
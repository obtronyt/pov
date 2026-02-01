import matplotlib.pyplot as plt
import numpy as np
from PIL import Image
import math
for i in range(1,7):
    path = r"D:\\Proj\\pov\\sml"+str(i)+r".jpg"
    img = Image.open(path).convert("L")
    size = min(img.size)
    img = img.crop((0, 0, size, size))
    img_array = np.array(img)
    img_array = img_array / 255.0
    img_bw = (img_array > 0.5).astype(float)
    h,w = img_bw.shape
    cx,cy = w//2, h//2
    R_MAX = cx
    #24 mm circle has no LEDs, Last LED sits at 67mm so 24/67 = 0.35
    R_MIN = int(0.35*R_MAX)

    Y, X = np.ogrid[:h, :w]
    dist = np.sqrt((X - cx)**2 + (Y - cy)**2)

    donut_mask = (dist >= R_MIN) & (dist <= R_MAX)

    img_donut = img_bw.copy()
    img_donut[~donut_mask] = 0

    SLICE_NUM = 360
    ROWS = 12

    frame = np.zeros((SLICE_NUM, ROWS))

    for i in range(SLICE_NUM):
        ang = math.pi * 2 * i / SLICE_NUM - (math.pi / 2)
        for j in range(ROWS):
            r = R_MIN + ((j/ROWS)*(R_MAX - R_MIN))
            x = int(cx + r*math.cos(ang))
            y = int(cy + r*math.sin(ang))
            if(0 <= x < w and 0 <= y < h):
                frame[i, j] = img_donut[y,x]

    # ------------------------------------------------------------
    # PACK FRAME INTO 16-BIT WORDS
    # ------------------------------------------------------------

    c_words = []

    for i in range(SLICE_NUM):
        word = 0
        for j in range(ROWS):
            if frame[i, j] > 0.5:
                bit = ROWS - 1 -j
                word |= (1 << bit)     # bit j = LED j
        c_words.append(word)

    # ------------------------------------------------------------
    # PRINT AS VALID C ARRAY
    # ------------------------------------------------------------

    print()
    print(f"const uint16_t pov_frame[{SLICE_NUM}] = {{")

    for i, val in enumerate(c_words):
        if i % 8 == 0:
            print("    ", end="")    # indent every row

        print(f"0x{val:04X}, ", end="")

        if i % 8 == 7:
            print()

    # Handle final newline cleanly
    if SLICE_NUM % 8 != 0:
        print()

    print("};")
    print()

# plt.figure(figsize=(14, 4))

# plt.subplot(1, 3, 1)
# plt.imshow(img_bw, cmap="gray")
# plt.title("Input Image (B/W)")
# plt.axis("off")

# plt.subplot(1, 3, 2)
# plt.imshow(img_donut, cmap="gray")
# plt.title("After Donut Mask")
# plt.axis("off")

# plt.subplot(1, 3, 3)
# plt.imshow(frame.T, aspect="auto", cmap="gray")
# plt.title("POV Sampled Data\n(radius vs angle)")
# plt.xlabel("Angle → time")
# plt.ylabel("LED index → radius")

# plt.tight_layout()
# plt.show()
import numpy as np
from PIL import Image
import matplotlib.pyplot as plt

def cimg(path):
    IMAGE_PATH = path   # Image is 100px high x 60px wide
    ROWS = 12                         # Number of vertical LEDs
    COLS = 10                         # Number of horizontal slices

    img = Image.open(IMAGE_PATH).convert("L")   # grayscale
    arr = np.asarray(img)

    h, w = arr.shape
    assert h == 12 and w == 10, "Expected image of size 10*12"

    out = (arr == 0).astype(np.uint8)
    print("{")
    for i in range(COLS):
        value = 0
        for j in range(ROWS):
            value = value | out[j][i] << j
        print(f"0x{value:04X}, ", end='')

for i in range(1,6):
    cimg('k'+str(i)+'.jpg')
cimg("fall.jpg")

# # binary_10x60 = (arr > TH).astype(np.uint8) * 255

# # # -----------------------------
# # # Plot result
# # # -----------------------------
# plt.figure(figsize=(6, 4))

# plt.imshow(out, cmap="gray", aspect="auto")
# plt.title("POV Sampled Image (12*10)")
# plt.xlabel("Slices (Angle)")
# plt.ylabel("LED Rows")

# plt.tight_layout()
# plt.show()

"""Генерирует app.ico для исполняемых файлов и установщика."""
from PIL import Image, ImageDraw

SIZE = 256
image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
draw = ImageDraw.Draw(image)

# Синий скруглённый квадрат.
draw.rounded_rectangle((8, 8, SIZE - 8, SIZE - 8), radius=60, fill=(59, 125, 246, 255))

# Белый «пузырь» сообщения.
draw.rounded_rectangle((56, 64, 200, 168), radius=28, fill=(255, 255, 255, 245))
draw.polygon([(92, 160), (92, 208), (132, 168)], fill=(255, 255, 255, 245))

# Три точки.
for x in (100, 128, 156):
    draw.ellipse((x - 9, 107, x + 9, 125), fill=(59, 125, 246, 255))

image.save(r"C:\MessengerApp\resources\app.ico",
           sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("ok")

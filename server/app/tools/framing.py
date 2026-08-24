"""Encuadre compartido con el firmware.

El ESP32 enseña en su visor un **cuadrado centrado** que abarca el 75 % del
ancho del sensor y todo el alto (`framing_init()` en `firmware/main/main.c`).
La foto que sube es el frame entero de 800x600, así que aquí hay que recortar
ese mismo cuadrado antes de mandarla a la IA. Si estas dos piezas se separan,
el usuario recibe una imagen distinta de la que encuadró.

De paso, el cuadrado es lo que espera el modelo: genera 1024x1024, así que la
cadena sensor → visor → IA → pantalla no deforma ni recorta en ningún paso.
"""

from PIL import Image


def center_square(img: Image.Image) -> Image.Image:
    """Recorta el cuadrado centrado más grande que quepa en la imagen."""
    lado = min(img.width, img.height)
    izq = (img.width - lado) // 2
    arriba = (img.height - lado) // 2
    return img.crop((izq, arriba, izq + lado, arriba + lado))

from enum import Enum


class Style(str, Enum):
    """Estilos que el ESP32 puede pedir. El valor viaja como campo de texto."""

    ANIME = "anime"
    CARTOON = "cartoon"
    SKETCH = "sketch"
    WATERCOLOR = "watercolor"
    PIXEL = "pixel"


# El trabajo no es solo "aplicar un filtro". El OV5640 con la optica de la placa
# entrega fotos ruidosas, blandas y con dominante de color, asi que al modelo hay
# que pedirle dos cosas que tiran en direcciones opuestas: que recupere el
# detalle que la camara perdio, y que no toque nada de lo que identifica a la
# persona. Por eso los bloques comunes viajan con TODOS los estilos.

# Este bloque va PRIMERO y es incondicional: la camara apunta a lo que sea, no
# solo a caras. Una version anterior daba por supuesto que siempre habia una
# persona y el modelo, al no encontrarla, se inventaba una entera.
_ESCENA = (
    "Restyle the photo that is actually in front of you. Keep the same subject matter, the same "
    "framing and crop, the same composition and camera distance, and the same number of people "
    "(including none). Never add a person, face, animal, object or background element that is not "
    "in the source, and never zoom in or recompose. If the source shows a screen, a wall, an "
    "object or a landscape, restyle exactly that — do not turn it into a portrait."
)

# El bloque de identidad va condicionado ("if the photo contains people") para
# que no arrastre al modelo hacia un retrato cuando no hay nadie. Dentro, manda:
# preferimos un estilo mas flojo a una cara que no es la de quien poso.
_IDENTIDAD = (
    "If the photo contains people, their identity must survive the restyling — this matters more "
    "than the style. Treat each face and body as reference data to be matched, not as something to "
    "improve or idealize. Preserve exactly: face shape and jawline, cheekbones, nose shape and "
    "width, eye shape, spacing and colour, eyebrow shape and thickness, lip shape and fullness, "
    "hairline, hair length, texture and colour, skin tone and undertone, facial hair, freckles, "
    "moles, scars, glasses, apparent age, body build, clothing and pose. Do not slim, beautify, "
    "symmetrise, de-age, lighten, or otherwise standardise anyone, and never substitute a generic "
    "face. Someone who knows these people must recognise them instantly in the result."
)

# La restauracion se pide explicitamente acotada: "reconstruye lo que esta ahi",
# no "mejora la foto". Sin ese limite el modelo rellena huecos inventando.
_DETALLE = (
    "The source is a small, low-quality sensor capture: expect heavy noise, soft focus, blown "
    "highlights, crushed shadows and a colour cast. Restore what the camera lost — crisp edges, "
    "legible eyes and eyelashes, individual hair strands, skin and fabric texture, clean separation "
    "between tones, neutral white balance and natural contrast — so the result looks like it came "
    "from a good camera. Reconstruct only detail that the pixels genuinely imply."
)

# El pixel art no puede llevar detalle fino por definicion, asi que pedirselo
# seria contradictorio. Aqui la fidelidad se juega en la silueta y el color.
_DETALLE_PIXEL = (
    "The source is a small, low-quality sensor capture: noisy, soft and colour-cast. Clean it up "
    "before reducing it — neutralise the white balance, recover the tonal range, and separate the "
    "subject from the background clearly. Spend the limited palette and pixel budget on the "
    "features that carry the likeness (eye position, hairline, silhouette, skin tone, clothing "
    "colours) rather than on sensor noise."
)

# Las frases de estilo describen la TECNICA de dibujo, nunca el motivo: decir
# "portrait" aqui es lo que empujaba al modelo a fabricar un retrato.
_ESTILOS: dict[Style, str] = {
    Style.ANIME: (
        "Redraw this photo as a 90s anime cel: clean confident linework, flat cel shading with "
        "hard-edged shadows, restrained highlights. Keep real proportions — do not apply anime face "
        "conventions such as enlarged eyes, shrunken nose or pointed chin."
    ),
    Style.CARTOON: (
        "Redraw this photo as a bold western cartoon: thick confident outlines, bright flat colour "
        "fills, simple shading. Stylise the rendering, not the anatomy — no caricature, no "
        "exaggerated or squashed features."
    ),
    Style.SKETCH: (
        "Redraw this photo as a graphite pencil drawing on textured paper: visible pencil strokes, "
        "hatching for the shadows, paper white for the highlights, no colour. Draw it the way an "
        "artist working from life would — accurate proportions above stylistic flourish."
    ),
    Style.WATERCOLOR: (
        "Redraw this photo as a loose watercolour painting: soft bleeding edges, visible paper "
        "grain, translucent washes, a few confident darker accents. Let the background stay loose, "
        "but keep faces and hands drawn accurately underneath the paint."
    ),
    Style.PIXEL: (
        "Redraw this photo as 16-bit pixel art with a limited palette, chunky uniform pixels, "
        "deliberate dithering for gradients and a clear readable silhouette."
    ),
}


def _componer(style: Style, estilo: str) -> str:
    detalle = _DETALLE_PIXEL if style is Style.PIXEL else _DETALLE
    return f"{estilo}\n\n{_ESCENA}\n\n{_IDENTIDAD}\n\n{detalle}"


PROMPTS: dict[Style, str] = {s: _componer(s, t) for s, t in _ESTILOS.items()}

"""Poda de las carpetas de fotos.

La VM que sirve el proxy comparte disco con los otros proyectos, y cada
resultado ocupa 1,4 MB. Sin limite, unas 770 fotos llenan el disco, y lo que
cae entonces no es solo la camara: se lleva por delante los logs de nginx y el
resto de apps de la maquina.

La poda nunca debe tumbar una foto que ya salio bien: si algo falla al borrar
se anota y se sigue, porque el usuario ya tiene su imagen en la pantalla.
"""

import logging
from pathlib import Path

log = logging.getLogger(__name__)

MAXIMO = 50


def _mtime(f: Path) -> float:
    """0 si el fichero desaparecio entre el listado y el stat."""
    try:
        return f.stat().st_mtime
    except OSError:
        return 0.0


def podar(directorio: Path, maximo: int = MAXIMO) -> None:
    """Deja las `maximo` fotos mas recientes y borra las demas."""
    try:
        ficheros = [f for f in directorio.iterdir()
                    if f.is_file() and f.name != ".gitkeep"]
    except OSError as exc:
        log.warning("no se pudo listar %s: %s", directorio, exc)
        return

    if len(ficheros) <= maximo:
        return

    ficheros.sort(key=_mtime, reverse=True)
    borrados = 0
    for viejo in ficheros[maximo:]:
        try:
            viejo.unlink()
            borrados += 1
        except OSError as exc:
            log.warning("no se pudo borrar %s: %s", viejo, exc)

    if borrados:
        log.info("poda en %s: %d borradas, quedan %d",
                 directorio.name, borrados, len(ficheros) - borrados)

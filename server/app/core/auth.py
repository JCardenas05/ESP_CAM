"""Token compartido para los endpoints que cuestan dinero.

En la LAN no hacia falta: nadie mas alcanzaba el proxy. Publicado en internet
si, porque cada POST a /stylize es una llamada facturada a OpenAI y la URL
acabara apareciendo en los logs de Cloudflare, en el historial del navegador o
en cualquier escaneo de subdominios.

Si ESPCAM_TOKEN no esta definido no se exige nada, para que el despliegue local
en la LAN siga funcionando igual que siempre sin tocar su .env.
"""

import logging
import secrets

from fastapi import Header, HTTPException

from app.core.config import Config

log = logging.getLogger(__name__)


def requiere_token(authorization: str = Header(default="")) -> None:
    """Dependencia de FastAPI: exige "Authorization: Bearer <token>"."""
    esperado = Config.AUTH_TOKEN
    if not esperado:
        return

    prefijo = "Bearer "
    recibido = authorization[len(prefijo):] if authorization.startswith(prefijo) else ""

    # compare_digest y no ==, para no filtrar el token por el tiempo que tarda
    # en fallar la comparacion caracter a caracter.
    if not secrets.compare_digest(recibido, esperado):
        log.warning("peticion rechazada: token ausente o incorrecto")
        raise HTTPException(status_code=401, detail="Token invalido")

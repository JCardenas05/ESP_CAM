import os

from dotenv import load_dotenv

load_dotenv()


class Config:
    """Configuración base del proxy de estilización."""

    ENVIRONMENT: str = os.getenv("ENVIRONMENT", "development")
    DEBUG: bool = os.getenv("DEBUG", "false").lower() in ("1", "true", "yes")

    class OpenAI:
        API_KEY: str = os.getenv("OPENAI_API_KEY")
        IMAGE_MODEL: str = os.getenv("OPENAI_IMAGE_MODEL", "gpt-image-1")
        SIZE: str = os.getenv("OPENAI_IMAGE_SIZE", "1024x1024")

        # Medido sobre la misma foto y el mismo prompt: low 16.8 s con 272 tokens
        # de salida, medium 20.3 s con 1056. El salto cuesta 3,5 s pero casi 4x
        # en dinero, y compra el detalle que el sensor no da. La fidelidad de
        # entrada va alta porque es lo que mantiene reconocible a quien posa.
        QUALITY: str = os.getenv("OPENAI_IMAGE_QUALITY", "medium")
        INPUT_FIDELITY: str = os.getenv("OPENAI_INPUT_FIDELITY", "high")

        # Borradores que el modelo suelta antes de la imagen buena. Con 2, el
        # primero llega sobre los 7 s frente a los ~20 del final. Se facturan
        # como imagenes generadas, asi que subirlo cuesta dinero, no tiempo.
        PARTIAL_IMAGES: int = int(os.getenv("OPENAI_PARTIAL_IMAGES", "2"))
        TIMEOUT_S: float = float(os.getenv("OPENAI_TIMEOUT_S", "120"))

        @staticmethod
        def validate():
            if not Config.OpenAI.API_KEY:
                raise EnvironmentError("Falta OPENAI_API_KEY en el entorno.")

    class LCD:
        """Formato exacto que espera la pantalla del ESP32-S3."""

        WIDTH: int = int(os.getenv("LCD_WIDTH", "240"))
        HEIGHT: int = int(os.getenv("LCD_HEIGHT", "320"))

        # El area util es cuadrada y centrada, con bandas arriba y abajo. Los
        # modelos de imagen generan cuadrado, asi que asi no se recorta nada de
        # lo que devuelve la IA. Debe coincidir con LCD_IMG_SIZE del firmware.
        IMAGE: int = int(os.getenv("IMAGE_SIZE", "240"))

        @staticmethod
        def offset_y() -> int:
            return (Config.LCD.HEIGHT - Config.LCD.IMAGE) // 2

        @staticmethod
        def frame_bytes() -> int:
            """Bytes exactos que el ESP32 espera recibir de /stylize."""
            return Config.LCD.IMAGE * Config.LCD.IMAGE * 2


# OpenAI.validate() no se llama al importar, a diferencia de Notion en Pomodoro_ESP:
# /health tiene que responder aunque todavía no haya API key configurada. La
# validación ocurre al entrar en /stylize.

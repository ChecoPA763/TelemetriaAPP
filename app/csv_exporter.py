"""Exportacion segura de sesiones CSV.

En Android usa Storage Access Framework (ACTION_CREATE_DOCUMENT), por lo que
la aplicacion no necesita acceso amplio al almacenamiento. En Windows y otros
entornos de desarrollo crea una copia con nombre unico en Descargas.
"""

from __future__ import annotations

import os
import shutil
from collections.abc import Callable
from pathlib import Path


ExportCallback = Callable[[bool, str], None]


def suggested_filename(source_path: str) -> str:
    """Devuelve un nombre CSV seguro conservando el nombre de la sesion."""

    name = os.path.basename(source_path.strip())
    if not name:
        name = "sesion.csv"
    if not name.lower().endswith(".csv"):
        name += ".csv"
    return name


def unique_destination(directory: str | os.PathLike[str], filename: str) -> Path:
    """Calcula un destino que no sobrescribe archivos existentes."""

    folder = Path(directory)
    candidate = folder / filename
    stem = candidate.stem
    suffix = candidate.suffix or ".csv"
    number = 2
    while candidate.exists():
        candidate = folder / f"{stem}_{number}{suffix}"
        number += 1
    return candidate


def copy_csv_to_directory(source_path: str, destination_dir: str | os.PathLike[str]) -> str:
    """Copia un CSV privado a un directorio visible sin sobrescribir otro."""

    source = Path(source_path)
    if not source.is_file():
        raise FileNotFoundError(f"No se encontro la sesion: {source.name}")
    if source.suffix.lower() != ".csv":
        raise ValueError("Solo se pueden exportar archivos CSV.")

    folder = Path(destination_dir)
    folder.mkdir(parents=True, exist_ok=True)
    destination = unique_destination(folder, suggested_filename(str(source)))
    with source.open("rb") as input_file, destination.open("xb") as output_file:
        shutil.copyfileobj(input_file, output_file, length=64 * 1024)
    return str(destination)


class CsvExporter:
    """Exporta un CSV usando Android SAF o una copia en Descargas."""

    REQUEST_CODE = 4611

    def __init__(self) -> None:
        self._pending_path: str | None = None
        self._callback: ExportCallback | None = None
        self._activity_bound = False

    @property
    def is_busy(self) -> bool:
        return self._pending_path is not None

    def export(self, source_path: str, callback: ExportCallback) -> None:
        if self.is_busy:
            callback(False, "Ya hay una exportacion en curso.")
            return
        if not os.path.isfile(source_path):
            callback(False, "El archivo de la sesion ya no existe.")
            return

        try:
            from kivy.utils import platform

            if platform == "android":
                self._export_android(source_path, callback)
            else:
                self._export_desktop(source_path, callback)
        except Exception as exc:
            self._clear_pending()
            callback(False, f"No se pudo iniciar la exportacion: {exc}")

    def _export_desktop(self, source_path: str, callback: ExportCallback) -> None:
        downloads = Path.home() / "Downloads"
        destination = copy_csv_to_directory(source_path, downloads)
        callback(True, f"CSV guardado en: {destination}")

    def _export_android(self, source_path: str, callback: ExportCallback) -> None:
        # Importaciones locales: estos modulos solo existen dentro del APK.
        from android import activity
        from jnius import autoclass

        Intent = autoclass("android.content.Intent")
        PythonActivity = autoclass("org.kivy.android.PythonActivity")

        intent = Intent(Intent.ACTION_CREATE_DOCUMENT)
        intent.addCategory(Intent.CATEGORY_OPENABLE)
        intent.setType("text/csv")
        intent.putExtra(Intent.EXTRA_TITLE, suggested_filename(source_path))

        self._pending_path = source_path
        self._callback = callback
        activity.bind(on_activity_result=self._on_activity_result)
        self._activity_bound = True
        PythonActivity.mActivity.startActivityForResult(intent, self.REQUEST_CODE)

    def _on_activity_result(self, request_code, result_code, intent) -> None:
        if request_code != self.REQUEST_CODE:
            return

        callback = self._callback
        source_path = self._pending_path
        try:
            from jnius import autoclass

            Activity = autoclass("android.app.Activity")
            if result_code != Activity.RESULT_OK or intent is None:
                if callback:
                    callback(False, "Exportacion cancelada.")
                return

            uri = intent.getData()
            if uri is None:
                raise RuntimeError("Android no devolvio un destino.")

            PythonActivity = autoclass("org.kivy.android.PythonActivity")
            resolver = PythonActivity.mActivity.getContentResolver()
            output_stream = resolver.openOutputStream(uri, "w")
            if output_stream is None:
                raise OSError("No se pudo abrir el destino seleccionado.")

            try:
                with open(source_path, "rb") as input_file:
                    while True:
                        chunk = input_file.read(64 * 1024)
                        if not chunk:
                            break
                        output_stream.write(bytearray(chunk))
                output_stream.flush()
            finally:
                output_stream.close()

            if callback:
                callback(True, f"CSV exportado: {suggested_filename(source_path)}")
        except Exception as exc:
            if callback:
                callback(False, f"No se pudo exportar el CSV: {exc}")
        finally:
            self._clear_pending()

    def _clear_pending(self) -> None:
        if self._activity_bound:
            try:
                from android import activity

                activity.unbind(on_activity_result=self._on_activity_result)
            except Exception:
                pass
        self._activity_bound = False
        self._pending_path = None
        self._callback = None



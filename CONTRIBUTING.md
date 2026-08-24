# Cómo trabajar en este prototipo

## Ramas

- No modificar `main` directamente.
- Crear una rama por cambio: `feature/v1.x-descripcion`,
  `fix/v1.x-descripcion`, `codex/v1.x-descripcion` o
  `claude/v1.x-descripcion`.
- Una persona o herramienta por rama a la vez.
- Integrar mediante Pull Request.

## Commits

Usar mensajes cortos: `feat:`, `fix:`, `docs:`, `refactor:` o `chore:`. Si una
IA realiza el cambio, conservar su coautoría en el commit.

## Antes de integrar

- Compilar el componente modificado.
- Si cambia un protocolo, actualizar emisor, receptor, esquema y
  `docs/CONTRATOS.md` en el mismo cambio.
- Si cambia un pin o parámetro físico, actualizar `docs/HARDWARE.md`.
- No mezclar una reorganización de archivos con cambios funcionales grandes.

## Seguridad

Nunca subir `secrets.h`, contraseñas, SSID, tokens, llaves ni CSV privados. TLS
debe validar la CA en cualquier uso real.


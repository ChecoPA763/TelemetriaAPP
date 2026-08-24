[app]
title = Lobos Racing Telemetria
package.name = lobostelemetry
package.domain = mx.lobosracing
source.dir = .
source.include_exts = py,png,jpg,kv,atlas
source.exclude_dirs = .venv,bin,.git,__pycache__,p4a-recipes
version = 1.1.1
requirements = python3,kivy==2.3.1,paho-mqtt==2.1.0,certifi,charset_normalizer
icon.filename = %(source.dir)s/assets/icon.png
orientation = portrait
fullscreen = 0

# La app solo necesita Internet para MQTT/TLS.
android.permissions = INTERNET
android.api = 36
android.minapi = 24
android.ndk = 29
android.archs = arm64-v8a
android.accept_sdk_license = True
p4a.branch = develop
p4a.local_recipes = ./p4a-recipes

[buildozer]
log_level = 2
warn_on_root = 1


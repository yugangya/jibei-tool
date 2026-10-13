set(LIBRE_GEOLENS_PLUGIN_DIR libre_geo_lens)
set(LIBRE_GEOLENS_PY_FILES
  __init__.py
  libre_geo_lens.py
  ollama_client.py
  settings_dialog.py
  ui_theme.py
)
PLUGIN_INSTALL(${LIBRE_GEOLENS_PLUGIN_DIR} . ${LIBRE_GEOLENS_PY_FILES} metadata.txt README.md)

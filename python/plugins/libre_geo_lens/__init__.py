"""QGIS entry point for the Jianghe LibreGeoLens integration."""


def classFactory(iface):
    from .libre_geo_lens import LibreGeoLens

    return LibreGeoLens(iface)

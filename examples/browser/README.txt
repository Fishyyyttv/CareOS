Example .care package that ships its own icon.

Rebuild the bundle after changing anything in here:

    python3 tools/care-pack.py examples/browser --out examples/browser.care \
        --set name=browser --set version=1.0 --set exec=main \
        --set icon=icon.cri --set category=Internet

icon.cri is a 48x48 CareOS Raster Image, extracted from the baked theme in
assets/careos-icons.cra. care-pack.py notices it is binary and emits it as a
FILEB64 section, which is the only way a byte-oriented payload survives the
line-oriented .care format.

On install, carepkg rebases icon=icon.cri onto the install directory, so the
app database ends up holding /apps/browser/icon.cri and the launcher draws the
real artwork instead of a vector glyph.

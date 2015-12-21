

# Shadows
* Move from one shadow buffer that gets reused for each light to a shadow buffer texture array with a separate space for each light.
* Better filtering

# Performance
* Frustum culling
* Multiple command lists
	- multithreaded command list creation


# Postprocess
* better vignette
* HDR
	- bloom
	- tonemap

# Lights

* Sky Lights?
	- loading cubemaps?

Requires light volume rendering + stencil buffer stuff:
* Point Lights?
* Spot Lights?

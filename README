Biclops - A Stereoscopic Display Driver by Emerson v0.1
-------------------------------------------------------

Usage Notes:
* This is a DirectX 9 wrapper DLL. When you build it, it will create a D3D9.dll that you place in the folder alongside your target application's binaries. You also need to put the Scene.fx file there, for barrel warping.

* I used Microsoft Visual C++ 2010 Express Edition to compile this.

* Your DirectX lib/include directories might not match mine, which are in E:\Program Files (x86)\Microsoft DirectX SDK (June 2010). If you see d3d9.h or d3d9.lib errors, check this in your IDE.

* I've hardcoded a couple hotkeys to adjust the 3D settings. 
** PAGEUP/PAGEDOWN increase/decrease the inter-eye distance respectively. ("Separation")
** HOME/END increase/decrease the frustum offsets respectively. ("Convergence")
** INSERT outputs the current eye offset and frustum offset to a file named "d3dlog.txt" in the game binary folder, so you can change the hard coded constants for your particular game.
** DELETE toggles between normal and "crosseyed" viewing modes, effectively flipping which frames go to the left or right eye respectively.
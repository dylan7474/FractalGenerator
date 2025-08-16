# FractalGenerator

A high-performance, constantly zooming Mandelbrot set explorer written in C and SDL2.
The program uses multithreading and SSE2 optimizations to render fractals in real time.

## Building

### Linux
1. Run `./configure` to check for required build tools and libraries.
2. Build the project:
   ```
   make
   ```

### Windows
1. Run the configuration script to verify dependencies.
2. Cross-compile using MinGW:
   ```
   make -f Makefile.win
   ```

## Controls
- The application starts in full-screen mode and continuously zooms into the Mandelbrot set.
- **Left click**: set a new zoom target at the cursor position.
- **Close window / Alt+F4**: exit the program.

## Roadmap
- Configurable color palettes.
- Screenshot or recording support.
- Presets for interesting fractal locations.
- Toggle for full-resolution rendering.

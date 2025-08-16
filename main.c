/*
 * main.c - A constantly zooming Mandelbrot set fractal.
 *
 * Cross-compiles on Linux for Windows using the provided framework.
 * This version is extremely optimized, using:
 * 1. Multithreading to use all CPU cores.
 * 2. SIMD (SSE2) instructions to process two pixels simultaneously (on x86).
 * 3. A standard C fallback for non-x86 architectures (like ARM).
 * 4. Periodicity checking to skip calculations for large black areas.
 * 5. Interactive mouse clicks to change the zoom target.
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>     // For multithreading
#include <SDL_cpuinfo.h> // To get the number of CPU cores

// --- MODIFIED: Conditionally include SSE2 header only for x86/x64 builds ---
#if defined(__x86_64__) || defined(__i386__)
#include <emmintrin.h>   // For SSE2 intrinsics on x86/x64
#endif

// --- Constants ---
int SCREEN_WIDTH = 800;
int SCREEN_HEIGHT = 600;
const int MAX_ITERATIONS = 255; // Max iterations for Mandelbrot calculation

// --- Structs ---
// Simple struct to hold RGB color values
typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
} Color;

// Struct to pass arguments to each rendering thread
typedef struct {
    int thread_id;
    int num_threads;
    void* pixels;
    int pitch;
    double center_r;
    double center_i;
    double zoom;
} ThreadArgs;


// --- Mandelbrot Calculation ---
/**
 * @brief Maps an iteration count to a color.
 */
Color get_color(int n) {
    Color color;
    if (n >= MAX_ITERATIONS) {
        color.r = 0; color.g = 0; color.b = 0; // Black for points inside the set
    } else {
        // Psychedelic coloring
        color.r = (int)(sin(0.1 * n) * 127 + 128);
        color.g = (int)(sin(0.1 * n + 2) * 127 + 128);
        color.b = (int)(sin(0.1 * n + 4) * 127 + 128);
    }
    return color;
}

/**
 * @brief Checks if a point is within the main cardioid or period-2 bulb.
 * This is a major optimization, allowing us to skip the main loop for many pixels.
 * @return 1 if the point is in a checked region (and thus in the set), 0 otherwise.
 */
int periodicity_check(double cr, double ci) {
    // Check for period-2 bulb
    if ((cr + 1.0) * (cr + 1.0) + ci * ci < 0.0625) { // 1/16
        return 1;
    }
    // Check for main cardioid
    double q = (cr - 0.25) * (cr - 0.25) + ci * ci;
    if (q * (q + (cr - 0.25)) < 0.25 * ci * ci) {
        return 1;
    }
    return 0;
}


/**
 * @brief The function executed by each thread.
 * It renders a horizontal slice of the fractal.
 */
void* render_thread(void* args) {
    ThreadArgs* thread_args = (ThreadArgs*)args;

    // Determine the Y-range this thread is responsible for
    int start_y = (SCREEN_HEIGHT / thread_args->num_threads) * thread_args->thread_id;
    int end_y = (SCREEN_HEIGHT / thread_args->num_threads) * (thread_args->thread_id + 1);

    // Get parameters from the args struct
    void* pixels = thread_args->pixels;
    int pitch = thread_args->pitch;
    double center_r = thread_args->center_r;
    double center_i = thread_args->center_i;
    double zoom = thread_args->zoom;
    
    double aspect_ratio = (double)SCREEN_WIDTH / (double)SCREEN_HEIGHT;
    double x_scale = (4.0 * aspect_ratio * zoom) / SCREEN_WIDTH;
    double y_scale = (4.0 * zoom) / SCREEN_WIDTH;

// --- MODIFIED: Use SSE2 optimized path for x86, standard C path for ARM/others ---
#if defined(__x86_64__) || defined(__i386__)
    // --- SSE2 (x86/x64) Optimized Path ---

    // SSE2 constants
    const __m128d _fours = _mm_set1_pd(4.0);
    const __m128d _ones = _mm_set1_pd(1.0);
    const __m128d _two = _mm_set1_pd(2.0);

    // Iterate over this thread's assigned portion of the pixels
    for (int y = start_y; y < end_y; y++) {
        Uint32* row = (Uint32*)((Uint8*)pixels + y * pitch);
        double ci_base = center_i + (y - SCREEN_HEIGHT / 2.0) * y_scale;

        for (int x = 0; x < SCREEN_WIDTH; x += 2) {
            double cr_base0 = center_r + (x - SCREEN_WIDTH / 2.0) * x_scale;
            double cr_base1 = center_r + (x + 1 - SCREEN_WIDTH / 2.0) * x_scale;
            
            // --- Periodicity Check ---
            // If both pixels are in a known black area, we can skip them entirely.
            if (periodicity_check(cr_base0, ci_base) && periodicity_check(cr_base1, ci_base)) {
                row[x] = 0xFF000000;
                if(x + 1 < SCREEN_WIDTH) row[x+1] = 0xFF000000;
                continue;
            }

            // --- SIMD Calculation ---
            __m128d _cr = _mm_set_pd(cr_base1, cr_base0);
            __m128d _ci = _mm_set1_pd(ci_base);
            
            __m128d _zr = _mm_setzero_pd();
            __m128d _zi = _mm_setzero_pd();
            
            __m128d _iterations = _mm_setzero_pd();
            
            for (int i = 0; i < MAX_ITERATIONS; i++) {
                __m128d _zr2 = _mm_mul_pd(_zr, _zr);
                __m128d _zi2 = _mm_mul_pd(_zi, _zi);
                
                // Check if the points have escaped
                __m128d _mag2 = _mm_add_pd(_zr2, _zi2);
                __m128d _escape_mask = _mm_cmplt_pd(_mag2, _fours);
                
                // If all points have escaped, we can break early
                if (_mm_movemask_pd(_escape_mask) == 0) break;
                
                // Add 1 to iteration count for points that have not escaped
                _iterations = _mm_add_pd(_iterations, _mm_and_pd(_escape_mask, _ones));
                
                // Calculate next Mandelbrot iteration: z = z^2 + c
                __m128d _zri = _mm_mul_pd(_zr, _zi);
                __m128d _zr_temp = _mm_add_pd(_mm_sub_pd(_zr2, _zi2), _cr);
                _zi = _mm_add_pd(_mm_mul_pd(_zri, _two), _ci);
                _zr = _zr_temp;
            }
            
            // --- Unpack results and color pixels ---
            double n_values[2];
            _mm_storeu_pd(n_values, _iterations);
            
            Color color1 = get_color((int)n_values[0]);
            row[x] = (0xFF << 24) | (color1.r << 16) | (color1.g << 8) | color1.b;
            
            if (x + 1 < SCREEN_WIDTH) {
                Color color2 = get_color((int)n_values[1]);
                row[x+1] = (0xFF << 24) | (color2.r << 16) | (color2.g << 8) | color2.b;
            }
        }
    }

#else
    // --- Standard C (ARM / Fallback) Path ---

    // Iterate over this thread's assigned portion of the pixels
    for (int y = start_y; y < end_y; y++) {
        Uint32* row = (Uint32*)((Uint8*)pixels + y * pitch);
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            // Map pixel to complex plane
            double cr = center_r + (x - SCREEN_WIDTH / 2.0) * x_scale;
            double ci = center_i + (y - SCREEN_HEIGHT / 2.0) * y_scale;

            // Check if the point is in a known black region
            if (periodicity_check(cr, ci)) {
                row[x] = 0xFF000000;
                continue;
            }

            double zr = 0.0, zi = 0.0;
            int n = 0;
            for (; n < MAX_ITERATIONS; n++) {
                double zr2 = zr * zr;
                double zi2 = zi * zi;
                if (zr2 + zi2 >= 4.0) {
                    break;
                }
                zi = 2.0 * zr * zi + ci;
                zr = zr2 - zi2 + cr;
            }

            Color color = get_color(n);
            row[x] = (0xFF << 24) | (color.r << 16) | (color.g << 8) | color.b;
        }
    }

#endif
    return NULL;
}


// --- Main Function ---
int main(int argc, char* argv[]) {
    // --- Initialization ---
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;

    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
        SCREEN_WIDTH = dm.w;
        SCREEN_HEIGHT = dm.h;
    }

    SDL_Window* window = SDL_CreateWindow("Mandelbrot - Click to change zoom target",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window) return 1;

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) return 1;

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!texture) return 1;

    // --- Threading Setup ---
    int num_threads = SDL_GetCPUCount();
    if (num_threads < 1) num_threads = 1; // Fallback for safety
    printf("Using %d threads for rendering.\n", num_threads);
    pthread_t* threads = (pthread_t*)malloc(num_threads * sizeof(pthread_t));
    ThreadArgs* thread_args = (ThreadArgs*)malloc(num_threads * sizeof(ThreadArgs));

    // --- Fractal Parameters ---
    double zoom = 1.0;
    double zoom_speed = 0.985;
    double center_r = -0.743643887037151;
    double center_i = 0.131825904205330;

    // --- Main Loop ---
    int is_running = 1;
    while (is_running) {
        SDL_Event e;
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                is_running = 0;
            }
            // --- ADDED: Mouse click handling ---
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                if (e.button.button == SDL_BUTTON_LEFT) {
                    // Convert screen coordinates to complex plane coordinates
                    double aspect_ratio = (double)SCREEN_WIDTH / (double)SCREEN_HEIGHT;
                    double mouse_r = center_r + (e.button.x - SCREEN_WIDTH / 2.0) * (4.0 * aspect_ratio * zoom) / SCREEN_WIDTH;
                    double mouse_i = center_i + (e.button.y - SCREEN_HEIGHT / 2.0) * (4.0 * zoom) / SCREEN_WIDTH;

                    // Set the new center point
                    center_r = mouse_r;
                    center_i = mouse_i;

                    printf("New center: (%f, %f)\n", center_r, center_i);
                }
            }
        }

        zoom *= zoom_speed;

        // --- Drawing (Multi-threaded & SIMD) ---
        void* pixels;
        int pitch;
        SDL_LockTexture(texture, NULL, &pixels, &pitch);

        // Launch all threads
        for (int i = 0; i < num_threads; i++) {
            thread_args[i] = (ThreadArgs){ .thread_id = i, .num_threads = num_threads,
                .pixels = pixels, .pitch = pitch, .center_r = center_r,
                .center_i = center_i, .zoom = zoom };
            pthread_create(&threads[i], NULL, render_thread, &thread_args[i]);
        }

        // Wait for all threads to complete their work
        for (int i = 0; i < num_threads; i++) {
            pthread_join(threads[i], NULL);
        }

        SDL_UnlockTexture(texture);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    // --- Cleanup ---
    free(threads);
    free(thread_args);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

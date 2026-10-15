/**********************************************************************************************
*
*   app_dpi.h - Make the process DPI-aware so mouse coordinates match the window.
*
*   Why: On Windows with display scaling > 100% (e.g. 150% / 144 DPI), a process
*   that is NOT DPI-aware gets virtualized: the window is created at logical size
*   but the system scales its rendering to physical pixels. GLFW's cursor callback
*   then reports coordinates that don't line up with what raylib draws, so
*   GetMousePosition() returns (0,0) or values that never hit the canvas.
*
*   Marking the process DPI-aware opts out of virtualization so raylib's 1:1
*   coordinate model is restored and the mouse works.
*
*   NOTE: This header deliberately does NOT #include <windows.h>, because that
*   header conflicts with raylib.h (Rectangle / CloseWindow / ShowCursor ...).
*   We declare the one Win32 prototype we need ourselves and link user32, which
*   raylib already pulls in on Windows. SetProcessDPIAware() exists since Windows
*   XP, so it is always available.
*
*   IMPORTANT: MinGW's loader does NOT reliably run __attribute__((constructor)),
*   so you MUST call app_enable_dpi() explicitly at the very top of main(), before
*   InitWindow(). The constructor below is only a best-effort fallback.
*
*   Usage:
*       #include "app_dpi.h"
*       int main(void) { app_enable_dpi(); InitWindow(...); ... }
*
**********************************************************************************************/

#ifndef APP_DPI_H
#define APP_DPI_H

#if defined(_WIN32) || defined(__CYGWIN__)

/* Forward-declare the minimal Win32 prototype without pulling in <windows.h>,
   which would conflict with raylib.h. BOOL is int; WINAPI = __stdcall on x86,
   a no-op on x64. */
#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
  #define APP_WINAPI
#else
  #define APP_WINAPI __stdcall
#endif

#ifdef __cplusplus
extern "C"
#endif
int APP_WINAPI SetProcessDPIAware(void);

/* Query our own process DPI-awareness (in-process, authoritative). */
#ifdef __cplusplus
extern "C"
#endif
int APP_WINAPI IsProcessDPIAware(void);

/* Returns 1 if this process is DPI-aware, 0 otherwise. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static int app_is_dpi_aware(void)
{
    return IsProcessDPIAware();
}

/* Call this at the very top of main(), before InitWindow(). Returns 1 on
*  success, 0 if the call failed or we're not on Windows. Idempotent. */
static int app_enable_dpi(void)
{
    return SetProcessDPIAware();
}

/* Best-effort static-init fallback (MinGW loader may skip this). */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void app_dpi_ctor(void)
{
    (void)SetProcessDPIAware();
}

#else /* !_WIN32 */

/* On non-Windows platforms raylib/GLFW already handles DPI correctly,
   so app_enable_dpi() is a no-op. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static int app_enable_dpi(void)
{
    return 1;
}

#endif /* _WIN32 */

#endif /* APP_DPI_H */

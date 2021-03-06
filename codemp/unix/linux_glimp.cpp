/*
** GLW_IMP.C
**
** This file contains ALL Linux specific stuff having to do with the
** OpenGL refresh.  When a port is being made the following functions
** must be implemented by the port:
**
** GLimp_EndFrame
** GLimp_Init
** GLimp_Shutdown
** GLimp_SwitchFullscreen
**
*/

#include <termios.h>
#include <sys/ioctl.h>
#ifdef __linux__
#include <sys/stat.h>
#include <sys/vt.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>

// bk001204
#include <dlfcn.h>

// bk001206 - from my Heretic2 by way of Ryan's Fakk2
// Needed for the new X11_PendingInput() function.
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "../game/q_shared.h"
#include "../renderer/tr_local.h"
#include "../client/client.h"
#include "linux_local.h" // bk001130

#include "unix_glw.h"

#include <GL/glx.h>

#include <X11/keysym.h>
#include <X11/cursorfont.h>

#include <X11/extensions/Xxf86dga.h>
#include <X11/extensions/xf86vmode.h>

#define	WINDOW_CLASS_NAME	"Jedi Academy"

typedef enum {
	RSERR_OK,

	RSERR_INVALID_FULLSCREEN,
	RSERR_INVALID_MODE,

	RSERR_UNKNOWN
} rserr_t;

glwstate_t glw_state;

static Display *dpy = NULL;
static int scrnum;
static Window win = 0;
static GLXContext ctx = NULL;

// bk001206 - not needed anymore
// static qboolean autorepeaton = qtrue;

#define KEY_MASK (KeyPressMask | KeyReleaseMask)
#define MOUSE_MASK (ButtonPressMask | ButtonReleaseMask | \
		    PointerMotionMask | ButtonMotionMask )
#define X_MASK (KEY_MASK | MOUSE_MASK | VisibilityChangeMask | StructureNotifyMask )

static qboolean        mouse_avail;
static qboolean        mouse_active;
static int mwx, mwy;
static int mx = 0, my = 0;

// Time mouse was reset, we ignore the first 50ms of the mouse to allow settling of events
static int mouseResetTime = 0;
#define MOUSE_RESET_DELAY 50

static cvar_t	*in_mouse;
static cvar_t	*in_dgamouse;

// bk001130 - from cvs1.17 (mkv), but not static
cvar_t   *in_joystick      = NULL;
cvar_t   *in_joystickDebug = NULL;
cvar_t   *joy_threshold    = NULL;

cvar_t	*r_allowSoftwareGL;		// don't abort out if the pixelformat claims software
cvar_t	*r_previousglDriver;

qboolean dgamouse = qfalse;
qboolean vidmode_ext = qfalse;

static int win_x, win_y;

static XF86VidModeModeInfo **vidmodes;
//static int default_dotclock_vidmode; // bk001204 - unused
static int num_vidmodes;
static qboolean vidmode_active = qfalse;

static int mouse_accel_numerator;
static int mouse_accel_denominator;
static int mouse_threshold;    

bool g_bTextureRectangleHack = false;

/*
* Find the first occurrence of find in s.
*/
// bk001130 - from cvs1.17 (mkv), const
// bk001130 - made first argument const
static const char *Q_stristr( const char *s, const char *find)
{
register char c, sc;
register size_t len;

	if ((c = *find++) != 0) {
		if (c >= 'a' && c <= 'z') {
			c -= ('a' - 'A');
		}
		len = strlen(find);
		do {
			do {
				if ((sc = *s++) == 0)
					return NULL;
				if (sc >= 'a' && sc <= 'z') {
					sc -= ('a' - 'A');
				}
			} while (sc != c);
		} while (Q_stricmpn(s, find, len) != 0);
		s--;
	}
	return s;
}

/*****************************************************************************/
/* KEYBOARD                                                                  */
/*****************************************************************************/

// bk001204 - unused
// static unsigned int	keyshift[256];		// key to map to if shift held down in console
// static qboolean shift_down=qfalse;

static char *XLateKey(XKeyEvent *ev, int *key)
{
	static char buf[64];
	KeySym keysym;
	// static qboolean setup = qfalse; // bk001204 - unused
	// int i; // bk001204 - unused

	*key = 0;

	XLookupString(ev, buf, sizeof buf, &keysym, 0);

	switch(keysym)
	{
		case XK_KP_Page_Up:	
		case XK_KP_9:	 *key = A_KP_9; break;
		case XK_Page_Up:	 *key = A_PAGE_UP; break;

		case XK_KP_Page_Down: 
		case XK_KP_3: *key = A_KP_3; break;
		case XK_Page_Down:	 *key = A_PAGE_DOWN; break;

		case XK_KP_Home:
		case XK_KP_7: *key = A_KP_7; break;
		case XK_Home:	 *key = A_HOME; break;

		case XK_KP_End:
		case XK_KP_1:	  *key = A_KP_1; break;
		case XK_End:	 *key = A_END; break;

		case XK_KP_Left:
		case XK_KP_4: *key = A_KP_4; break;
		case XK_Left:	 *key = A_CURSOR_LEFT; break;

		case XK_KP_Right:
		case XK_KP_6: *key = A_KP_6; break;
		case XK_Right:	*key = A_CURSOR_RIGHT;		break;

		case XK_KP_Down:
		case XK_KP_2: 	 *key = A_KP_2; break;
		case XK_Down:	 *key = A_CURSOR_DOWN; break;

		case XK_KP_Up:   
		case XK_KP_8:    *key = A_KP_8; break;
		case XK_Up:		 *key = A_CURSOR_UP;	 break;

		case XK_Escape: *key = A_ESCAPE;		break;

		case XK_KP_Enter: *key = A_KP_ENTER;	break;
		case XK_Return: *key = A_ENTER;		 break;

		case XK_Tab:		*key = A_TAB;			 break;

		case XK_F1:		 *key = A_F1;				break;

		case XK_F2:		 *key = A_F2;				break;

		case XK_F3:		 *key = A_F3;				break;

		case XK_F4:		 *key = A_F4;				break;

		case XK_F5:		 *key = A_F5;				break;

		case XK_F6:		 *key = A_F6;				break;

		case XK_F7:		 *key = A_F7;				break;

		case XK_F8:		 *key = A_F8;				break;

		case XK_F9:		 *key = A_F9;				break;

		case XK_F10:		*key = A_F10;			 break;

		case XK_F11:		*key = A_F11;			 break;

		case XK_F12:		*key = A_F12;			 break;

		  // bk001206 - from Ryan's Fakk2 
		  //case XK_BackSpace: *key = 8; break; // ctrl-h
                  case XK_BackSpace: *key = A_BACKSPACE; break; // ctrl-h

		case XK_KP_Delete:
		case XK_KP_Decimal: *key = A_KP_PERIOD; break;
		case XK_Delete: *key = A_DELETE; break;

		case XK_Pause:	*key = A_PAUSE;		 break;

		case XK_Shift_L:
		case XK_Shift_R:	*key = A_SHIFT;		break;

		case XK_Execute: 
		case XK_Control_L: 
		case XK_Control_R:	*key = A_CTRL;		 break;

		case XK_Alt_L:	
		case XK_Meta_L: 
		case XK_Alt_R:	
		case XK_Meta_R: *key = A_ALT;			break;

		case XK_KP_Begin: *key = A_KP_5;	break;

		case XK_Insert:		*key = A_INSERT; break;
		case XK_KP_Insert:
		case XK_KP_0: *key = A_KP_0; break;

		case XK_KP_Multiply: *key = '*'; break;
		case XK_KP_Add:  *key = A_KP_PLUS; break;
		case XK_KP_Subtract: *key = A_KP_MINUS; break;
#if 0
		case XK_KP_Divide: *key = A_KP_SLASH; break;
#endif

		  // bk001130 - from cvs1.17 (mkv)
	case XK_exclam: *key = '1'; break;
	case XK_at: *key = '2'; break;
	case XK_numbersign: *key = '3'; break;
	case XK_dollar: *key = '4'; break;
	case XK_percent: *key = '5'; break;
	case XK_asciicircum: *key = '6'; break;
	case XK_ampersand: *key = '7'; break;
	case XK_asterisk: *key = '8'; break;
	case XK_parenleft: *key = '9'; break;
	case XK_parenright: *key = '0'; break;

		default:
			*key = *(unsigned char *)buf;
			if (*key >= 'A' && *key <= 'Z')
				*key = *key - 'A' + 'a';
			break;
	} 

	return buf;
}

// ========================================================================
// makes a null cursor
// ========================================================================

static Cursor CreateNullCursor(Display *display, Window root)
{
    Pixmap cursormask; 
    XGCValues xgc;
    GC gc;
    XColor dummycolour;
    Cursor cursor;

    cursormask = XCreatePixmap(display, root, 1, 1, 1/*depth*/);
    xgc.function = GXclear;
    gc =  XCreateGC(display, cursormask, GCFunction, &xgc);
    XFillRectangle(display, cursormask, gc, 0, 0, 1, 1);
    dummycolour.pixel = 0;
    dummycolour.red = 0;
    dummycolour.flags = 04;
    cursor = XCreatePixmapCursor(display, cursormask, cursormask,
          &dummycolour,&dummycolour, 0,0);
    XFreePixmap(display,cursormask);
    XFreeGC(display,gc);
    return cursor;
}

static void install_grabs(void)
{
	// inviso cursor
	XWarpPointer(dpy, None, win,
				 0, 0, 0, 0,
				 glConfig.vidWidth / 2, glConfig.vidHeight / 2);
	XSync(dpy, False);

	XDefineCursor(dpy, win, CreateNullCursor(dpy, win));

	XGrabPointer(dpy, win, // bk010108 - do this earlier?
				 False,
				 MOUSE_MASK,
				 GrabModeAsync, GrabModeAsync,
				 win,
				 None,
				 CurrentTime);

	XGetPointerControl(dpy, &mouse_accel_numerator, &mouse_accel_denominator,
		&mouse_threshold);

	XChangePointerControl(dpy, True, True, 1, 1, 0);

	XSync(dpy, False);

	mouseResetTime = Sys_Milliseconds ();

	if (in_dgamouse->value) {
		int MajorVersion, MinorVersion;

		if (!XF86DGAQueryVersion(dpy, &MajorVersion, &MinorVersion)) { 
			// unable to query, probalby not supported
			Com_Printf( "Failed to detect XF86DGA Mouse\n" );
			Cvar_Set( "in_dgamouse", "0" );
		} else {
			dgamouse = qtrue;
			XF86DGADirectVideo(dpy, DefaultScreen(dpy), XF86DGADirectMouse);
			XWarpPointer(dpy, None, win, 0, 0, 0, 0, 0, 0);
		}
	} else {
		mwx = glConfig.vidWidth / 2;
		mwy = glConfig.vidHeight / 2;
		mx = my = 0;
	}

	XGrabKeyboard(dpy, win,
				  False,
				  GrabModeAsync, GrabModeAsync,
				  CurrentTime);

	XSync(dpy, False);
}

static void uninstall_grabs(void)
{
	if (dgamouse) {
		dgamouse = qfalse;
		XF86DGADirectVideo(dpy, DefaultScreen(dpy), 0);
	}

	XChangePointerControl(dpy, qtrue, qtrue, mouse_accel_numerator, 
		mouse_accel_denominator, mouse_threshold);

	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);

	XWarpPointer(dpy, None, win,
				 0, 0, 0, 0,
				 glConfig.vidWidth / 2, glConfig.vidHeight / 2);

	// inviso cursor
	XUndefineCursor(dpy, win);
}



// bk001206 - from Ryan's Fakk2
/**
 * XPending() actually performs a blocking read 
 *  if no events available. From Fakk2, by way of
 *  Heretic2, by way of SDL, original idea GGI project.
 * The benefit of this approach over the quite
 *  badly behaved XAutoRepeatOn/Off is that you get
 *  focus handling for free, which is a major win
 *  with debug and windowed mode. It rests on the
 *  assumption that the X server will use the
 *  same timestamp on press/release event pairs 
 *  for key repeats. 
 */
static qboolean X11_PendingInput(void) {

  assert(dpy != NULL);

  // Flush the display connection
  //  and look to see if events are queued
  XFlush( dpy );
  if ( XEventsQueued( dpy, QueuedAlready) ) {
    return qtrue;
  }

  // More drastic measures are required -- see if X is ready to talk
  {
    static struct timeval zero_time;
    int x11_fd;
    fd_set fdset;

    x11_fd = ConnectionNumber( dpy );
    FD_ZERO(&fdset);
    FD_SET(x11_fd, &fdset);
    if ( select(x11_fd+1, &fdset, NULL, NULL, &zero_time) == 1 ) {
      if (XPending(dpy))
        return qtrue;
      else
        return qfalse;
    }
  }
  
  // Oh well, nothing is ready ..
  return qfalse;
}


// bk001206 - from Ryan's Fakk2. See above.
static qboolean repeated_press(XEvent *event)
{
    XEvent        peekevent;
    qboolean      repeated = qfalse;

    assert(dpy != NULL);
  
    if (X11_PendingInput())
    {
        XPeekEvent(dpy, &peekevent);

        if ((peekevent.type == KeyPress) &&
            (peekevent.xkey.keycode == event->xkey.keycode) &&
            (peekevent.xkey.time == event->xkey.time))
        {
            repeated = qtrue;
            XNextEvent(dpy, &peekevent);  // skip event.
        } // if
    } // if

    return(repeated);
} // repeated_press



static void HandleEvents(void)
{
	int b;
	int key;
	XEvent event;
	qboolean dowarp = qfalse;
	char *p;
	int dx, dy;
	int t;
   
	if (!dpy)
		return;

	while (XPending(dpy)) {
		XNextEvent(dpy, &event);
		switch(event.type) {
		case KeyPress:
			p = XLateKey(&event.xkey, &key);
			if (key)
				Sys_QueEvent( 0, SE_KEY, key, qtrue, 0, NULL );
			while (*p)
				Sys_QueEvent( 0, SE_CHAR, *p++, 0, 0, NULL );
			break;

		case KeyRelease:

                // bk001206 - handle key repeat w/o XAutRepatOn/Off
                //            also: not done if console/menu is active.
		// From Ryan's Fakk2.
		// see game/q_shared.h, KEYCATCH_* . 0 == in 3d game.  
		  if (cls.keyCatchers == 0) {   // FIXME: KEYCATCH_NONE
                   if (repeated_press(&event) == qtrue)
                      continue;
                } // if
			XLateKey(&event.xkey, &key);
			
			Sys_QueEvent( 0, SE_KEY, key, qfalse, 0, NULL );
			break;

		case MotionNotify:
			if (mouse_active) {
				if (dgamouse) {
					if (abs(event.xmotion.x_root) > 1)
						mx += event.xmotion.x_root * 2;
					else
						mx += event.xmotion.x_root;
					if (abs(event.xmotion.y_root) > 1)
						my += event.xmotion.y_root * 2;
					else
						my += event.xmotion.y_root;
					t = Sys_Milliseconds();
					if (t - mouseResetTime > MOUSE_RESET_DELAY ) {
						Sys_QueEvent( t, SE_MOUSE, mx, my, 0, NULL );
					}
					mx = my = 0;
				} 
				else 
				{
// Com_Printf( "MotionNotify: %d,%d:  ", event.xmotion.x, event.xmotion.y );
					// If it's a center motion, we've just returned from our warp
					if (event.xmotion.x == glConfig.vidWidth/2 &&
						event.xmotion.y == glConfig.vidHeight/2) {
						mwx = glConfig.vidWidth/2;
						mwy = glConfig.vidHeight/2;
// Com_Printf( "SE_MOUSE (%d,%d)\n", mx, my );
						t = Sys_Milliseconds();
						if (t - mouseResetTime > MOUSE_RESET_DELAY ) {
							Sys_QueEvent( t, SE_MOUSE, mx, my, 0, NULL );
						}
						mx = my = 0;
						break;
					}

					dx = ((int)event.xmotion.x - mwx);
					dy = ((int)event.xmotion.y - mwy);
					if (abs(dx) > 1)
						mx += dx * 2;
					else
						mx += dx;
					if (abs(dy) > 1)
						my += dy * 2;
					else
						my += dy;

// Com_Printf( "mx=%d,my=%d [%d - %d,%d - %d]\n", mx, my, event.xmotion.x, mwx, event.xmotion.y, mwy );
					mwx = event.xmotion.x;
					mwy = event.xmotion.y;
						dowarp = qtrue;
				}
			}
			break;

		case ButtonPress:
			if (event.xbutton.button == 4) {
				Sys_QueEvent( 0, SE_KEY, A_MWHEELUP, qtrue, 0, NULL );
			} else if (event.xbutton.button == 5) {
				Sys_QueEvent( 0, SE_KEY, A_MWHEELDOWN, qtrue, 0, NULL );
			} else {
			b=-1;
				if (event.xbutton.button == 1) {
				b = 0;
				} else if (event.xbutton.button == 2) {
				b = 2;
				} else if (event.xbutton.button == 3) {
				b = 1;
				}

			Sys_QueEvent( 0, SE_KEY, A_MOUSE1 + b, qtrue, 0, NULL );
			}
			break;

		case ButtonRelease:
			if (event.xbutton.button == 4) {
				Sys_QueEvent( 0, SE_KEY, A_MWHEELUP, qfalse, 0, NULL );
			} else if (event.xbutton.button == 5) {
				Sys_QueEvent( 0, SE_KEY, A_MWHEELDOWN, qfalse, 0, NULL );
			} else {
			b=-1;
				if (event.xbutton.button == 1) {
				b = 0;
				} else if (event.xbutton.button == 2) {
				b = 2;
				} else if (event.xbutton.button == 3) {
				b = 1;
				}
			Sys_QueEvent( 0, SE_KEY, A_MOUSE1 + b, qfalse, 0, NULL );
			}
			break;

		case CreateNotify :
			win_x = event.xcreatewindow.x;
			win_y = event.xcreatewindow.y;
			break;

		case ConfigureNotify :
			win_x = event.xconfigure.x;
			win_y = event.xconfigure.y;
			break;
		}
	}

	if (dowarp) {
		XWarpPointer(dpy,None,win,0,0,0,0, 
				(glConfig.vidWidth/2),(glConfig.vidHeight/2));
	}
}

void KBD_Init(void)
{
}

void KBD_Close(void)
{
}

void IN_ActivateMouse( void ) 
{
	if (!mouse_avail || !dpy || !win)
		return;

	if (!mouse_active) {
		install_grabs();
		mouse_active = qtrue;
	}
}

void IN_DeactivateMouse( void ) 
{
	if (!mouse_avail || !dpy || !win)
		return;

	if (mouse_active) {
		uninstall_grabs();
		mouse_active = qfalse;
	}
}
/*****************************************************************************/

static qboolean signalcaught = qfalse;;

void Sys_Exit(int); // bk010104 - abstraction

static void signal_handler(int sig) // bk010104 - replace this...
{
	if (signalcaught) {
	  printf("DOUBLE SIGNAL FAULT: Received signal %d, exiting...\n", sig);
	  Sys_Exit(1); // bk010104 - abstraction
	}

	signalcaught = qtrue;
	printf("Received signal %d, exiting...\n", sig);
	GLimp_Shutdown(); // bk010104 - shouldn't this be CL_Shutdown
	Sys_Exit(1); // bk010104 - abstraction
}

static void InitSig(void)
{
	signal(SIGHUP, signal_handler);
	signal(SIGQUIT, signal_handler);
	signal(SIGILL, signal_handler);
	signal(SIGTRAP, signal_handler);
	signal(SIGIOT, signal_handler);
	signal(SIGBUS, signal_handler);
	signal(SIGFPE, signal_handler);
	signal(SIGSEGV, signal_handler);
	signal(SIGTERM, signal_handler);
}

/*
** GLimp_SetGamma
**
** This routine should only be called if glConfig.deviceSupportsGamma is TRUE
*/
void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] )
{
}

/*
** GLimp_Shutdown
**
** This routine does all OS specific shutdown procedures for the OpenGL
** subsystem.  Under OpenGL this means NULLing out the current DC and
** HGLRC, deleting the rendering context, and releasing the DC acquired
** for the window.  The state structure is also nulled out.
**
*/
void GLimp_Shutdown( void )
{
	if (!ctx || !dpy)
		return;
	IN_DeactivateMouse();
	// bk001206 - replaced with H2/Fakk2 solution
	// XAutoRepeatOn(dpy);
	// autorepeaton = qfalse; // bk001130 - from cvs1.17 (mkv)
	if (dpy) {
		if (ctx)
			qglXDestroyContext(dpy, ctx);
		if (win)
			XDestroyWindow(dpy, win);
		if (vidmode_active)
			XF86VidModeSwitchToMode(dpy, scrnum, vidmodes[0]);
		XCloseDisplay(dpy);
	}
	vidmode_active = qfalse;
	dpy = NULL;
	win = 0;
	ctx = NULL;

	memset( &glConfig, 0, sizeof( glConfig ) );
	memset( &glState, 0, sizeof( glState ) );

	QGL_Shutdown();
}

/*
** GLimp_LogComment
*/
void GLimp_LogComment( char *comment ) 
{
	if ( glw_state.log_fp ) {
		fprintf( glw_state.log_fp, "%s", comment );
	}
}

/*
** GLW_StartDriverAndSetMode
*/
// bk001204 - prototype needed
int GLW_SetMode( const char *drivername, int mode, qboolean fullscreen );
static qboolean GLW_StartDriverAndSetMode( const char *drivername, 
										   int mode, 
										   qboolean fullscreen )
{
	rserr_t err;

	// don't ever bother going into fullscreen with a voodoo card
#if 1	// JDC: I reenabled this
	if ( Q_stristr( drivername, "Voodoo" ) ) {
		Cvar_Set( "r_fullscreen", "0" );
		r_fullscreen->modified = qfalse;
		fullscreen = qfalse;
	}
#endif

	err = (rserr_t) GLW_SetMode( drivername, mode, fullscreen );

	switch ( err )
	{
	case RSERR_INVALID_FULLSCREEN:
		Com_Printf( "...WARNING: fullscreen unavailable in this mode\n" );
		return qfalse;
	case RSERR_INVALID_MODE:
		Com_Printf( "...WARNING: could not set the given mode (%d)\n", mode );
		return qfalse;
	default:
		break;
	}
	return qtrue;
}

/*
** GLW_SetMode
*/
int GLW_SetMode( const char *drivername, int mode, qboolean fullscreen )
{
	int attrib[] = {
		GLX_RGBA,					// 0
		GLX_RED_SIZE, 4,			// 1, 2
		GLX_GREEN_SIZE, 4,			// 3, 4
		GLX_BLUE_SIZE, 4,			// 5, 6
		GLX_DOUBLEBUFFER,			// 7
		GLX_DEPTH_SIZE, 1,			// 8, 9
		GLX_STENCIL_SIZE, 1,		// 10, 11
		None
	};
	// these match in the array
#define ATTR_RED_IDX 2
#define ATTR_GREEN_IDX 4
#define ATTR_BLUE_IDX 6
#define ATTR_DEPTH_IDX 9
#define ATTR_STENCIL_IDX 11
	Window root;
	XVisualInfo *visinfo;
	XSetWindowAttributes attr;
	unsigned long mask;
	int colorbits, depthbits, stencilbits;
	int tcolorbits, tdepthbits, tstencilbits;
	int MajorVersion, MinorVersion;
	int actualWidth, actualHeight;
	int i;
	const char*   glstring; // bk001130 - from cvs1.17 (mkv)

	Com_Printf( "Initializing OpenGL display\n");

	Com_Printf ("...setting mode %d:", mode );

	if ( !R_GetModeInfo( &glConfig.vidWidth, &glConfig.vidHeight, mode ) )
	{
		Com_Printf( " invalid mode\n" );
		return RSERR_INVALID_MODE;
	}
	Com_Printf( " %d %d\n", glConfig.vidWidth, glConfig.vidHeight);

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "Error couldn't open the X display\n");
		return RSERR_INVALID_MODE;
	}

	scrnum = DefaultScreen(dpy);
	root = RootWindow(dpy, scrnum);

	actualWidth = glConfig.vidWidth;
	actualHeight = glConfig.vidHeight;

	// Get video mode list
	MajorVersion = MinorVersion = 0;
	if (!XF86VidModeQueryVersion(dpy, &MajorVersion, &MinorVersion)) { 
		vidmode_ext = qfalse;
	} else {
		Com_Printf("Using XFree86-VidModeExtension Version %d.%d\n",
			MajorVersion, MinorVersion);
		vidmode_ext = qtrue;
	}

	// Check for DGA
	if (in_dgamouse->value) {
		if (!XF86DGAQueryVersion(dpy, &MajorVersion, &MinorVersion)) { 
			// unable to query, probalby not supported
			Com_Printf( "Failed to detect XF86DGA Mouse\n" );
			Cvar_Set( "in_dgamouse", "0" );
		} else {
			Com_Printf( "XF86DGA Mouse (Version %d.%d) initialized\n",
				MajorVersion, MinorVersion);
		}
	}

	if (vidmode_ext) {
		int best_fit, best_dist, dist, x, y;
		
		XF86VidModeGetAllModeLines(dpy, scrnum, &num_vidmodes, &vidmodes);

		// Are we going fullscreen?  If so, let's change video mode
		if (fullscreen) {
			best_dist = 9999999;
			best_fit = -1;

			for (i = 0; i < num_vidmodes; i++) {
				if (glConfig.vidWidth > vidmodes[i]->hdisplay ||
					glConfig.vidHeight > vidmodes[i]->vdisplay)
					continue;

				x = glConfig.vidWidth - vidmodes[i]->hdisplay;
				y = glConfig.vidHeight - vidmodes[i]->vdisplay;
				dist = (x * x) + (y * y);
				if (dist < best_dist) {
					best_dist = dist;
					best_fit = i;
				}
			}

			if (best_fit != -1) {
				actualWidth = vidmodes[best_fit]->hdisplay;
				actualHeight = vidmodes[best_fit]->vdisplay;

				// change to the mode
				XF86VidModeSwitchToMode(dpy, scrnum, vidmodes[best_fit]);
				vidmode_active = qtrue;

				// Move the viewport to top left
				XF86VidModeSetViewPort(dpy, scrnum, 0, 0);

				Com_Printf("XFree86-VidModeExtension Activated at %dx%d\n",
					actualWidth, actualHeight);

			} else {
				fullscreen = qfalse;
				Com_Printf("XFree86-VidModeExtension: No acceptable modes found\n");
			}
		} else {
			Com_Printf("XFree86-VidModeExtension:  Ignored on non-fullscreen/Voodoo\n");
		}
	}


	if (!r_colorbits->value)
		colorbits = 24;
	else
		colorbits = r_colorbits->value;

	if (!r_depthbits->value)
		depthbits = 24;
	else
		depthbits = r_depthbits->value;
	stencilbits = r_stencilbits->value;

	for (i = 0; i < 16; i++) {
		// 0 - default
		// 1 - minus colorbits
		// 2 - minus depthbits
		// 3 - minus stencil
		if ((i % 4) == 0 && i) {
			// one pass, reduce
			switch (i / 4) {
			case 2 :
				if (colorbits == 24)
					colorbits = 16;
				break;
			case 1 :
				if (depthbits == 24)
					depthbits = 16;
				else if (depthbits == 16)
					depthbits = 8;
			case 3 :
				if (stencilbits == 24)
					stencilbits = 16;
				else if (stencilbits == 16)
					stencilbits = 8;
			}
		}

		tcolorbits = colorbits;
		tdepthbits = depthbits;
		tstencilbits = stencilbits;

		if ((i % 4) == 3) { // reduce colorbits
			if (tcolorbits == 24)
				tcolorbits = 16;
		}	

		if ((i % 4) == 2) { // reduce depthbits
			if (tdepthbits == 24)
				tdepthbits = 16;
			else if (tdepthbits == 16)
				tdepthbits = 8;
		}

		if ((i % 4) == 1) { // reduce stencilbits
			if (tstencilbits == 24)
				tstencilbits = 16;
			else if (tstencilbits == 16)
				tstencilbits = 8;
			else
				tstencilbits = 0;
		}

		if (tcolorbits == 24) {
			attrib[ATTR_RED_IDX] = 8;
			attrib[ATTR_GREEN_IDX] = 8;
			attrib[ATTR_BLUE_IDX] = 8;
		} else  {
			// must be 16 bit
			attrib[ATTR_RED_IDX] = 4;
			attrib[ATTR_GREEN_IDX] = 4;
			attrib[ATTR_BLUE_IDX] = 4;
		}

		attrib[ATTR_DEPTH_IDX] = tdepthbits; // default to 24 depth
		attrib[ATTR_STENCIL_IDX] = tstencilbits;

		visinfo = qglXChooseVisual(dpy, scrnum, attrib);
		if (!visinfo) {
			continue;
		}

		Com_Printf( "Using %d/%d/%d Color bits, %d depth, %d stencil display.\n", 
			attrib[ATTR_RED_IDX], attrib[ATTR_GREEN_IDX], attrib[ATTR_BLUE_IDX],
			attrib[ATTR_DEPTH_IDX], attrib[ATTR_STENCIL_IDX]);

		glConfig.colorBits = tcolorbits;
		glConfig.depthBits = tdepthbits;
		glConfig.stencilBits = tstencilbits;
		break;
	}

	if (!visinfo) {
		Com_Printf( "Couldn't get a visual\n" );
		return RSERR_INVALID_MODE;
	}

	/* window attributes */
	attr.background_pixel = BlackPixel(dpy, scrnum);
	attr.border_pixel = 0;
	attr.colormap = XCreateColormap(dpy, root, visinfo->visual, AllocNone);
	attr.event_mask = X_MASK;
	if (vidmode_active) {
		mask = CWBackPixel | CWColormap | CWSaveUnder | CWBackingStore | 
			CWEventMask | CWOverrideRedirect;
		attr.override_redirect = True;
		attr.backing_store = NotUseful;
		attr.save_under = False;
	} else
		mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

	win = XCreateWindow(dpy, root, 0, 0, 
			actualWidth, actualHeight, 
			0, visinfo->depth, InputOutput,
			visinfo->visual, mask, &attr);

	XStoreName( dpy, win, WINDOW_CLASS_NAME );

	XMapWindow( dpy, win );

	if (vidmode_active)
		XMoveWindow(dpy, win, 0, 0);

	XFlush(dpy);
	XSync(dpy,False); // bk001130 - from cvs1.17 (mkv)
	ctx = qglXCreateContext(dpy, visinfo, NULL, True);
	XSync(dpy,False); // bk001130 - from cvs1.17 (mkv)

	qglXMakeCurrent(dpy, win, ctx);

	// bk001130 - from cvs1.17 (mkv)
	glstring = (const char *) qglGetString (GL_RENDERER);
        Com_Printf( "GL_RENDERER: %s\n", glstring );

	// bk010122 - new software token (Indirect)
	if ( !Q_stricmp( glstring, "Mesa X11")
	     || !Q_stricmp( glstring, "Mesa GLX Indirect") ) 
	{
	        if ( !r_allowSoftwareGL->integer ) {
		  Com_Printf( "\n\n***********************************************************\n" );
		  Com_Printf( " You are using software Mesa (no hardware acceleration)!   \n" );
		  Com_Printf( " Driver DLL used: %s\n", drivername ); 
		  Com_Printf( " If this is intentional, add\n" );
		  Com_Printf( "       \"+set r_allowSoftwareGL 1\"\n" );
		  Com_Printf( " to the command line when starting the game.\n" );
		  Com_Printf( "***********************************************************\n");
		  GLimp_Shutdown( );
		  return RSERR_INVALID_MODE;
		} else {
		  Com_Printf( "...using software Mesa (r_allowSoftwareGL==1).\n" );
		}
	}

	return RSERR_OK;
}

/*
** GLW_InitExtensions
*/
static void GLW_InitExtensions( void )
{
	if ( !r_allowExtensions->integer )
	{
		Com_Printf( "*** IGNORING OPENGL EXTENSIONS ***\n" );
		return;
	}

	Com_Printf( "Initializing OpenGL extensions\n" );

	// GL_S3_s3tc
	if ( Q_stristr( glConfig.extensions_string, "GL_S3_s3tc" ) )
	{
		if ( r_ext_compressed_textures->value )
		{
			glConfig.textureCompression = TC_S3TC;
			Com_Printf( "...using GL_S3_s3tc\n" );
		}
		else
		{
			glConfig.textureCompression = TC_NONE;
			Com_Printf( "...ignoring GL_S3_s3tc\n" );
		}
	}
	else
	{
		glConfig.textureCompression = TC_NONE;
		Com_Printf( "...GL_S3_s3tc not found\n" );
	}

	// GL_EXT_texture_env_add
	glConfig.textureEnvAddAvailable = qfalse;
	if ( Q_stristr( glConfig.extensions_string, "EXT_texture_env_add" ) )
	{
		if ( r_ext_texture_env_add->integer )
		{
			glConfig.textureEnvAddAvailable = qtrue;
			Com_Printf( "...using GL_EXT_texture_env_add\n" );
		}
		else
		{
			glConfig.textureEnvAddAvailable = qfalse;
			Com_Printf( "...ignoring GL_EXT_texture_env_add\n" );
		}
	}
	else
	{
		Com_Printf( "...GL_EXT_texture_env_add not found\n" );
	}

	// GL_ARB_multitexture
	qglMultiTexCoord2fARB = NULL;
	qglActiveTextureARB = NULL;
	qglClientActiveTextureARB = NULL;
	if ( Q_stristr( glConfig.extensions_string, "GL_ARB_multitexture" ) )
	{
		if ( r_ext_multitexture->value )
		{
			qglMultiTexCoord2fARB = ( PFNGLMULTITEXCOORD2FARBPROC ) dlsym( glw_state.OpenGLLib, "glMultiTexCoord2fARB" );
			qglActiveTextureARB = ( PFNGLACTIVETEXTUREARBPROC ) dlsym( glw_state.OpenGLLib, "glActiveTextureARB" );
			qglClientActiveTextureARB = ( PFNGLCLIENTACTIVETEXTUREARBPROC ) dlsym( glw_state.OpenGLLib, "glClientActiveTextureARB" );

			if ( qglActiveTextureARB )
			{
				qglGetIntegerv( GL_MAX_ACTIVE_TEXTURES_ARB, &glConfig.maxActiveTextures );

				if ( glConfig.maxActiveTextures > 1 )
				{
					Com_Printf( "...using GL_ARB_multitexture\n" );
				}
				else
				{
					qglMultiTexCoord2fARB = NULL;
					qglActiveTextureARB = NULL;
					qglClientActiveTextureARB = NULL;
					Com_Printf( "...not using GL_ARB_multitexture, < 2 texture units\n" );
				}
			}
		}
		else
		{
			Com_Printf( "...ignoring GL_ARB_multitexture\n" );
		}
	}
	else
	{
		Com_Printf( "...GL_ARB_multitexture not found\n" );
	}

	// GL_EXT_compiled_vertex_array
	if ( Q_stristr( glConfig.extensions_string, "GL_EXT_compiled_vertex_array" ) )
	{
		if ( r_ext_compiled_vertex_array->value )
		{
			Com_Printf( "...using GL_EXT_compiled_vertex_array\n" );
			qglLockArraysEXT = ( void ( APIENTRY * )( int, int ) ) dlsym( glw_state.OpenGLLib, "glLockArraysEXT" );
			qglUnlockArraysEXT = ( void ( APIENTRY * )( void ) ) dlsym( glw_state.OpenGLLib, "glUnlockArraysEXT" );
			if (!qglLockArraysEXT || !qglUnlockArraysEXT) {
				Com_Error (ERR_FATAL, "bad getprocaddress");
			}
		}
		else
		{
			Com_Printf( "...ignoring GL_EXT_compiled_vertex_array\n" );
		}
	}
	else
	{
		Com_Printf( "...GL_EXT_compiled_vertex_array not found\n" );
	}

}

/*
** GLW_LoadOpenGL
**
** GLimp_win.c internal function that that attempts to load and use 
** a specific OpenGL DLL.
*/
static qboolean GLW_LoadOpenGL()
{
	char name[1024];
	qboolean fullscreen;

	strcpy( name, OPENGL_DRIVER_NAME );

	Com_Printf( "...loading %s: ", name );

	// disable the 3Dfx splash screen and set gamma
	// we do this all the time, but it shouldn't hurt anything
	// on non-3Dfx stuff
	putenv("FX_GLIDE_NO_SPLASH=0");

	// Mesa VooDoo hacks
	putenv("MESA_GLX_FX=fullscreen\n");

	// load the QGL layer
	if ( QGL_Init( name ) ) 
	{
		fullscreen = (r_fullscreen->integer) ? qtrue : qfalse;

		// create the window and set up the context
		if ( !GLW_StartDriverAndSetMode( name, r_mode->integer, fullscreen ) )
		{
			if (r_mode->integer != 3) {
				if ( !GLW_StartDriverAndSetMode( name, 3, fullscreen ) ) {
					goto fail;
				}
			} else
				goto fail;
		}

		return qtrue;
	}
	else
	{
		Com_Printf( "failed\n" );
	}
fail:

	QGL_Shutdown();

	return qfalse;
}

static void GLW_StartOpenGL( void )
{
	//
	// load and initialize the specific OpenGL driver
	//
	if ( !GLW_LoadOpenGL() )
	{
		Com_Error( ERR_FATAL, "GLW_StartOpenGL() - could not load OpenGL subsystem\n" );
	}
}

/*
** GLimp_Init
**
** This routine is responsible for initializing the OS specific portions
** of OpenGL.  
*/
void GLimp_Init( void )
{
	qboolean attemptedlibGL = qfalse;
	qboolean attempted3Dfx = qfalse;
	qboolean success = qfalse;
	char	buf[1024];
	cvar_t *lastValidRenderer = Cvar_Get( "r_lastValidRenderer", "(uninitialized)", CVAR_ARCHIVE );
	// cvar_t	*cv; // bk001204 - unused

	r_allowSoftwareGL = Cvar_Get( "r_allowSoftwareGL", "0", CVAR_LATCH );

	r_previousglDriver = Cvar_Get( "r_previousglDriver", "", CVAR_ROM );

	glConfig.deviceSupportsGamma = qfalse;

	InitSig();

	//
	// load and initialize the specific OpenGL driver
	//
	GLW_StartOpenGL();

	// get our config strings
	glConfig.vendor_string = (const char *) qglGetString (GL_VENDOR);
	glConfig.renderer_string = (const char *) qglGetString (GL_RENDERER);
	glConfig.version_string = (const char *) qglGetString (GL_VERSION);
	glConfig.extensions_string = (const char *) qglGetString (GL_EXTENSIONS);

	qglGetIntegerv( GL_MAX_TEXTURE_SIZE, &glConfig.maxTextureSize );

	//
	// chipset specific configuration
	//
	strcpy( buf, glConfig.renderer_string );
	Q_strlwr( buf );

	//
	// NOTE: if changing cvars, do it within this block.  This allows them
	// to be overridden when testing driver fixes, etc. but only sets
	// them to their default state when the hardware is first installed/run.
	//
	if ( Q_stricmp( lastValidRenderer->string, glConfig.renderer_string ) )
	{
		Cvar_Set( "r_textureMode", "GL_LINEAR_MIPMAP_NEAREST" );
		Cvar_Set( "r_picmip", "1" );
	}

	Cvar_Set( "r_lastValidRenderer", glConfig.renderer_string );

	// initialize extensions
	GLW_InitExtensions();

	InitSig();

	return;
}


/*
** GLimp_EndFrame
** 
** Responsible for doing a swapbuffers and possibly for other stuff
** as yet to be determined.  Probably better not to make this a GLimp
** function and instead do a call to GLimp_SwapBuffers.
*/
void GLimp_EndFrame (void)
{
	// don't flip if drawing to front buffer
	// if ( stricmp( r_drawBuffer->string, "GL_FRONT" ) != 0 )
	{
		qglXSwapBuffers(dpy, win);
	}

	// check logging
	QGL_EnableLogging( (qboolean)r_logFile->integer ); // bk001205 - was ->value
}

#ifdef SMP
/*
===========================================================

SMP acceleration

===========================================================
*/

sem_t	renderCommandsEvent;
sem_t	renderCompletedEvent;
sem_t	renderActiveEvent;

void (*glimpRenderThread)( void );

void *GLimp_RenderThreadWrapper( void *stub ) {
	glimpRenderThread();
	return NULL;
}


/*
=======================
GLimp_SpawnRenderThread
=======================
*/
pthread_t	renderThreadHandle;
qboolean GLimp_SpawnRenderThread( void (*function)( void ) ) {

	sem_init( &renderCommandsEvent, 0, 0 );
	sem_init( &renderCompletedEvent, 0, 0 );
	sem_init( &renderActiveEvent, 0, 0 );

	glimpRenderThread = function;

	if (pthread_create( &renderThreadHandle, NULL,
		GLimp_RenderThreadWrapper, NULL)) {
		return qfalse;
	}

	return qtrue;
}

static	void	*smpData;
//static	int		glXErrors; // bk001204 - unused

void *GLimp_RendererSleep( void ) {
	void	*data;

	// after this, the front end can exit GLimp_FrontEndSleep
	sem_post ( &renderCompletedEvent );

	sem_wait ( &renderCommandsEvent );

	data = smpData;

	// after this, the main thread can exit GLimp_WakeRenderer
	sem_post ( &renderActiveEvent );

	return data;
}


void GLimp_FrontEndSleep( void ) {
	sem_wait ( &renderCompletedEvent );
}


void GLimp_WakeRenderer( void *data ) {
	smpData = data;

	// after this, the renderer can continue through GLimp_RendererSleep
	sem_post( &renderCommandsEvent );

	sem_wait( &renderActiveEvent );
}

#else

void GLimp_RenderThreadWrapper( void *stub ) { }
qboolean GLimp_SpawnRenderThread( void (*function)( void ) ) {
	return qfalse;
}
void *GLimp_RendererSleep( void ) {
	return NULL;
}
void GLimp_FrontEndSleep( void ) { }
void GLimp_WakeRenderer( void *data ) { }

#endif

/*****************************************************************************/
/* MOUSE                                                                     */
/*****************************************************************************/

void IN_Init(void) {
  // mouse variables
  in_mouse = Cvar_Get ("in_mouse", "1", CVAR_ARCHIVE);
  in_dgamouse = Cvar_Get ("in_dgamouse", "1", CVAR_ARCHIVE);
  
  // bk001130 - from cvs.17 (mkv), joystick variables
  in_joystick = Cvar_Get ("in_joystick", "0", CVAR_ARCHIVE|CVAR_LATCH);
  // bk001130 - changed this to match win32
  in_joystickDebug = Cvar_Get ("in_debugjoystick", "0", CVAR_TEMP);
  joy_threshold = Cvar_Get ("joy_threshold", "0.15", CVAR_ARCHIVE); // FIXME: in_joythreshold

  if (in_mouse->value)
    mouse_avail = qtrue;
  else
    mouse_avail = qfalse;

  IN_StartupJoystick( ); // bk001130 - from cvs1.17 (mkv)
}

void IN_Shutdown(void)
{
	mouse_avail = qfalse;
}

void IN_Frame (void) {

  // bk001130 - from cvs 1.17 (mkv)
  IN_JoyMove(); // FIXME: disable if on desktop?
  
  if ( cls.keyCatchers & KEYCATCH_CONSOLE ) {
    // temporarily deactivate if not in the game and
    // running on the desktop
    // voodoo always counts as full screen
    if (Cvar_VariableValue ("r_fullscreen") == 0) {
      IN_DeactivateMouse ();
      return;
    }
    // bk001206 - not used, now done the H2/Fakk2 way
    //if (dpy && !autorepeaton) {
    //  XAutoRepeatOn(dpy);
    //  autorepeaton = qtrue;
    //}
  } 
  //else if (dpy && autorepeaton) {
  //XAutoRepeatOff(dpy);
  //autorepeaton = qfalse;
  //}
 
  IN_ActivateMouse();
}

void IN_Activate(void)
{
}

// bk001130 - cvs1.17 joystick code (mkv) was here, no linux_joystick.c

void Sys_SendKeyEvents (void) {
  // XEvent event; // bk001204 - unused

  if (!dpy)
    return;
  HandleEvents();
}


// bk010216 - added stubs for non-Linux UNIXes here
// FIXME - use NO_JOYSTICK or something else generic

#if 1
void IN_StartupJoystick( void ) {}
void IN_JoyMove( void ) {}
#endif

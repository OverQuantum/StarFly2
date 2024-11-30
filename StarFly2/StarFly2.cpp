/*==========================================================================================================================
StarFly v2 Screen Saver

Windows screensaver, simulates flying thru star field with some FTL speed.
Resembles "Starfield" screensaver from old Windows, but with more stars, colors and other small features.

Copyright (C) 2024, OverQuantum
This program is licensed under the GNU General Public License v3

Apparently big stars are drawn as circles of fixed color, apparently small - as single pixels with color fading with distance.
Stars varies in size, distribution is somewhat like in real space (configurable).
Star colors could be random RGB or more real black-body spectrum (distribution is just uniform).

Render via GDI but pretty fast. 16-bit integer z-buffer used.
No anti-aliasing of circles or several stars 'combining light' in one pixel.
Initial generation makes even 3D star distribution in viewing cone (limited by FarPlane).
If star moves out of sight, another is generated in the distance and fades-in from blackness.

Configuration: Number of stars, fly speed, render rate, center position, zoom,
star scale and scheme, colorization and scheme, color fading coefficient, fade-in time.

Settings are loaded from ini-file located and named as .scr/.exe (StarFly2.ini by default)
This ini-file should contain strings in the form "Name = value".

Supported settings (default values are in supplied StarFly2.ini):
Stars            - Number of stars seen simultaneously.
Speed            - Fly speed. 1.0 means 5s for flying distance to FarPlane; 0.005 - 1000s.
TimerRate        - Interval between frames in ms. 40 means render with ~25 fps.
CenterX, CenterY - 'Destination point' location. (0.5, 0.5) is a center of desktop.
                   If you have two similar monitors placed horizontally then (0.75, 0.5) would be center of right one.
                   Negative values and >1.0 are possible, but not fully supported.
Zoom             - 1.0 - ~90' view angle, >>1.0 - telescope, <<1.0 - fish-eye. Does not affect star sizes, only their motion and fading with distance.
StarSize         - Base for star size. Value = distance at which star have radius 1 pixel.
SizeType         - 0 - All stars have StarSize;
                   1 - StarSize * [0.0, 2.0);
                   2 - Something like Gamma distribution with max at StarSize.
DarkestRGB       - Darkest value for RGB components of random star color. 0 - black, full red, full blue are possible, 255 - all stars are white
ColorType        - Type of color generation for stars
                   0 - random in range [DarkestRGB, 255], could produce green, purple, cyan and so on - colors are not possible in real space. DarkestRGB recommended is 64;
                   1 - random black-body radiation color (value DarkestRGB allows to decrease color saturation).
FadePower        - How fast star brightness fades with distance. 1.0 - linearly, 0.0 - does not fade.
FadeInTime       - How fast newly generated star appears, ms. Does not affect program start.


Some base code got from Phosphor2 screensaver, 2010 Evan Green, GPLv3
	https://github.com/evangreen/phosphor


Change log:
2024-11-14 Template
2024-11-15 CreateDIBSection, fine-grained circle
2024-11-16 Classes, removed global variables
2024-11-17 Fading, sorting, resize protection, force exit, even distribution
2024-11-19 Linked list
2024-11-21 Settings from file, Different star sizes, randStarRadius
2024-11-22 z-buffer, sorting and list removed
2024-11-24 Giant stars generated further, black-body spectrum
2024-11-26 Slow fade-in
2024-11-27 Type of star size distribution
2024-11-30 .scr starting handling, ini from near exe

Possible future improvements:
- Support side-view / backward fly
- Rotation
- Support clouds/clusters and galaxies
==========================================================================================================================*/

#include <windows.h>
#include <math.h>
#include <stdio.h>

// Definitions ----------------------------------------------------------------

#define FP_TYPE   float     // Floating-point type

// Data Structure Definitions -------------------------------------------------

enum StarState : UINT8
{
	State_New = 0,       // Star to be created on program start
	State_Generated,     // Star generated normally
};

enum RandomColorType
{
	ColorType_RandomRGB = 0,         // Random color in range [DarkestRGB, 256)
	ColorType_RandomBlackBody = 1,   // Random black-body radiation color
};

enum RandomStarSize
{
	SizeType_AllEqual = 0,    // All stars have StarSize
	SizeType_From0to2 = 1,    // StarSize * [0.0, 2.0)
	SizeType_GammaLike = 2,   // Something like Gamma distribution with max at StarSize
};

class StarFly2;

class Star
{
public:
	static const FP_TYPE giantFactor; // Giants are generated N-times further

	// Absolute values
	UINT8 r; // Color
	UINT8 g;
	UINT8 b;
	StarState state;
	FP_TYPE x;
	FP_TYPE y;
	FP_TYPE z;
	FP_TYPE size;
	int fadeIn; // How much time left to full color, ms

	// Viewed values
	FP_TYPE xp; // Position on screen
	FP_TYPE yp;
	FP_TYPE viewSize; // Radius on screen
	FP_TYPE fade; // Fade (0.0 - black, 1.0 - r,g,b)

	void Process( StarFly2* app);
	void Render( StarFly2* app);
};

class StarFly2
{
public:
	static const char ApplicationName[];

	static const int SettlingTime = 500;  // ms, During this time after start mouse moves, clicks and keypress will not trigger exit
	static const int MouseTolerance = 5;  // pixels, Smaller moves will not trigger exit

	static const FP_TYPE  FarPlane;       // Distance, at which most new stars are generated

	// Configuration
	bool ScreenSaverWindowed;
	FP_TYPE StarSizeFactor;
	FP_TYPE CenterX;
	FP_TYPE CenterY;
	FP_TYPE FadePower;
	FP_TYPE XrandSpan;
	FP_TYPE YrandSpan;
	UINT8 DarkestRGB;
	RandomColorType ColorType;
	RandomStarSize SizeType;
	int FadeInTime;

	// State
	POINT MousePosition;
	int ScreenWidth, ScreenHeight;
	FP_TYPE ScreenScale;
	FP_TYPE FadeInK;

private:
	int StarCount;
	int FrameInterval;
	FP_TYPE FlySpeed;
	FP_TYPE Zoom;

	int TotalTimeMs;
	bool inRender; // Barrier flag
	unsigned int PrevTime;

	// GDI objects
	HWND OurWindow;
	MMRESULT OurTimer;
	HBITMAP MemBitmap;
	HBITMAP OrigBitmap1;
	HDC MemDc;
	UINT8* MemBuffer;
	UINT16* zBuffer;

	Star* allStars;  // All stars (array in fact)

	void Process( int index, int flag );

public:
	StarFly2();

	void LoadSettings ( const char * filename );
	bool Initialize ( HWND Window );
	void Destroy ();
	bool UpdateScreen ( );
	void PutPixelOnBufferZ(int x, int y, UINT8 r, UINT8 g, UINT8 b, UINT16 z);
	void PutPixelOnBufferCheckZ(int x, int y, UINT8 r, UINT8 g, UINT8 b, UINT16 z);

	static VOID CALLBACK TimerEvent ( UINT TimerId, UINT Message, DWORD_PTR User, DWORD_PTR Parameter1, DWORD_PTR Parameter2 );
	static LRESULT WINAPI ScreenSaverProc ( HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam );

#ifdef _DEBUG
	int RandCount; // Counter of star randomization
#endif

};

const char StarFly2::ApplicationName[] = "StarFly2";
const FP_TYPE StarFly2::FarPlane = (FP_TYPE)5000.0;
const FP_TYPE Star::giantFactor = (FP_TYPE)5.0;

// Functions ----------------------------------------------------------------

// Function to generate random FP number in range [0, 1)
FP_TYPE randFloat()
{
	static const FP_TYPE divisor = ((FP_TYPE)1.0)/(FP_TYPE)(RAND_MAX+1); // +1 to not generate 1.0
	return rand()*divisor;
}

// Generates distribution vaguely resembling Gamma distribution for k=3.0-5.0
// Maximum of distribution is 1.0, values are in range [0, ~27.15] ( if RAND_MAX = 0x7FFF - typical )
FP_TYPE randStarRadius()
{
	static const FP_TYPE one = (FP_TYPE)1.0;
	static const FP_TYPE pwr = (FP_TYPE)0.3;
	static const FP_TYPE coeff = (FP_TYPE)1.2;
	FP_TYPE r = randFloat();
	return coeff * pow(r / (one - r), pwr);
}

// Trim spaces fom string (modifies input string)
char* trim(char* text)
{
	char* back = text + strlen(text);
	while(isspace(*text))
		text++; // Shift pointer to non-space char
	while(isspace(*--back)); // Shift back pointer to non-space char
	*(back+1) = 0; // Put EOL
	return text;
}

// Methods ------------------------------------------------------------------

// Project star to screen and regenerate it if necessary
// app - pointer to main object
void Star::Process( StarFly2* app )
{
	static const FP_TYPE one = (FP_TYPE)1.0;

	while (true)
	{
		// Project star to screen coordinates and check if it is still visible
		do
		{
			if (0 > z) break; // Star is behind viewer - generate new one

			FP_TYPE dist2 = x*x + y*y + z*z;
			viewSize = size / sqrt(dist2);
			int size1 = (int)viewSize;

			FP_TYPE k1 = app->ScreenScale/z;

			xp = app->CenterX*app->ScreenWidth + x * k1;
			if (xp<-size1 || xp>=(app->ScreenWidth+size1))
				break; // Star circle is outside viewed cone - generate new one

			yp = app->CenterY*app->ScreenHeight + y * k1;
			if (yp<-size1 || yp>=(app->ScreenHeight+size1))
				break;

			if (one > viewSize)
				fade = pow(viewSize, app->FadePower); // Calculate fade of color with distance
			else
				fade = one;

			// Fade in of star (regardless of FadePower)
			if (0 < fadeIn)
			{
				FP_TYPE k2 = (FP_TYPE)(one - fadeIn * app->FadeInK); // FadeInK = 1.0 / FadeInTime
				if (one < viewSize)
				{	// size0 > 1  =>  1) fade 0->1, viewSize 0->1, 2) fade=1, viewSize 1->size0
					viewSize *= k2;
					fade = (one > viewSize) ? viewSize : one;
				}
				else
				{	// size0 < 1  =>  1) fade 0->fade0, viewSize 0->size0
					viewSize *= k2;
					fade *= k2;
				}
			}

			return; // Proejction OK - exit
		}
		while(false);

		// Randomize star
#ifdef _DEBUG
		app->RandCount++;
#endif
		// Position generation
		if (State_New == state) // Initial star randomization - inside rect.cuboid, which will be limited to viewing cone by projection and checks (see above)
		{
			z = randFloat()*app->FarPlane; // Current way
			fadeIn = 0; // No fade-in
		}
		else // New stars during fly - on FarPlane
		{
			z = app->FarPlane;
			fadeIn = app->FadeInTime; // Normal fade-in
		}

		// Take into account screen width, height, ScreenScale, FarPlane and CenterX/CenterY
		// xp_min = CenterX*ScreenWidth + x_min * ScreenScale/FarPlane = 0
		// xp_max = CenterX*ScreenWidth + x_max * ScreenScale/FarPlane = ScreenWidth
		// x = (rnd[0-1] - CenterX)*XrandSpan  where XrandSpan = ScreenWidth*FarPlane/ScreenScale
		x = (randFloat() - app->CenterX)*app->XrandSpan;
		y = (randFloat() - app->CenterY)*app->YrandSpan;
		// If z=FarPlane then only fp-precision and star size can trigger again regeneration of a star after this
		// On z=[0..FarPlane) - regeneration will happen with probability 2/3 (star is outsize pyramid volume of viewed space)
		// If CenterX/CenterY<0.0 or >1.0 - probability of regeneration increases

		// Size generation
		FP_TYPE sizeR;
		if (SizeType_AllEqual == app->SizeType)
			sizeR = one;
		else if (SizeType_From0to2 == app->SizeType)
			sizeR = randFloat()*(FP_TYPE)2.0; // [0.0 - 2.0) with max at 1.0
		else //if (SizeType_GammaLike == app->SizeType)
			sizeR = randStarRadius(); // (0 - ~27) with max at 1.0

		if (sizeR > giantFactor)
		{	// Giant stars should appear n-times further to not pop-up as circles
			z *= giantFactor;
			x *= giantFactor;
			y *= giantFactor;
		}
		size = app->StarSizeFactor*sizeR;

		// Color generation
		if (ColorType_RandomRGB == app->ColorType)
		{
			// Random color in range [DarkestRGB, 256)
			// Could produce green, purple, cyan and so on - colors are not possible in real space
			static const FP_TYPE colorRand = (FP_TYPE)(256 - app->DarkestRGB); // Span of color generation
			r = app->DarkestRGB + (UINT8)(randFloat()*colorRand);
			g = app->DarkestRGB + (UINT8)(randFloat()*colorRand);
			b = app->DarkestRGB + (UINT8)(randFloat()*colorRand);
		}
		else //if (ColorType_RandomBlackBody == app->ColorType)
		{
			// Random black-body radiation color
			// Based on https://stackoverflow.com/questions/21977786/star-b-v-color-index-to-apparent-rgb-color/#22630970  (optimized a bit)
			FP_TYPE t, bv = (FP_TYPE)(-0.4 + randFloat()*2.4); // BV in range [-0.4, 2.4)
			// Convert BV into RGB
			static const FP_TYPE colorRange = (FP_TYPE)(255 - app->DarkestRGB); // Colorization
			r = app->DarkestRGB, b = app->DarkestRGB, g = app->DarkestRGB;
			if (bv < 0.00) // Switch for red
			{
				t = (FP_TYPE)((bv + 0.40)/(0.00 + 0.40));
				r += (UINT8)(colorRange*(0.61+(0.11*t)+(0.1*t*t)));
			}
			else if (bv < 0.40)
			{
				t = (FP_TYPE)((bv - 0.00)/(0.40 - 0.00));
				r += (UINT8)(colorRange*(0.83+(0.17*t)));
			}
			else
				r += (UINT8)(colorRange);
			if (bv < 0.00) // Switch for green
			{
				t = (FP_TYPE)((bv + 0.40)/(0.00 + 0.40));
				g += (UINT8)(colorRange*(0.70+(0.07*t)+(0.1*t*t)));
			}
			else if (bv < 0.40)
			{
				t = (FP_TYPE)((bv - 0.00)/(0.40 - 0.00));
				g += (UINT8)(colorRange*(0.87+(0.11*t)));
			}
			else if (bv < 1.60)
			{
				t = (FP_TYPE)((bv - 0.40)/(1.60 - 0.40));
				g += (UINT8)(colorRange*(0.98-(0.16*t)));
			}
			else
			{
				t = (FP_TYPE)((bv - 1.60)/(2.00 - 1.60));
				g += (UINT8)(colorRange*(0.82-(0.5*t*t)));
			}
			if (bv < 0.40) // Switch for blue
				b += (UINT8)(colorRange);
			else if (bv < 1.50)
			{
				t = (FP_TYPE)((bv - 0.40)/(1.50 - 0.40));
				b += (UINT8)(colorRange*(1.00-(0.47*t)+(0.1*t*t)));
			}
			else if (bv < 1.94)
			{
				t = (FP_TYPE)((bv - 1.50)/(1.94 - 1.50));
				b += (UINT8)(colorRange*(0.63-(0.6*t*t)));
			}
		}
	} // Will rerun projection due to while-true loop
}

// Render star to memory buffer
// app - pointer to main object
void Star::Render( StarFly2* app )
{
	static const FP_TYPE minSize = (FP_TYPE)0.8; // If larger - draw as circle, smaller - just 1 pixel
	UINT16 zp = (UINT16)z; // z-buffer value
	UINT8 r0 = (UINT8)(r*fade), g0 = (UINT8)(g*fade), b0 = (UINT8)(b*fade);

	if (minSize < viewSize)
	{
		int size1 = ((FP_TYPE)1.0 < viewSize) ? (int)viewSize : 1;
		int xp1 = (int)xp; // Integer coordinates
		int yp1 = (int)yp;

#if 0 // Square
		for (int j = max(0,yp1-size1);j<min(yp1+size1,app->ScreenHeight);j++)
		for (int k = max(0,xp1-size1);k<min(xp1+size1,app->ScreenWidth);k++)
			app->PutPixelOnBufferZ(k,j, r,g,b ,zp);
		return;
#endif

#if 1 // Fine-grained circle with FP center
		bool drawn = false;
		FP_TYPE lim = viewSize*viewSize;
		// Loops (j,k) - on intersection of screen and bounding rectangle around star circle
		for (int j = max(0, yp1-size1);
				j < min(yp1+size1+2, app->ScreenHeight); j++)
		{
			FP_TYPE yd = j - yp;        // (xd,yd) - vector from star center to current pixel
			FP_TYPE lim2 = lim - yd*yd; // From 'xd*xd + yd*yd <= lim' we can write 'xd*xd <= lim - yd*yd'
			if (0 <= lim2)
				for (int k = max(0, xp1-size1);
						k < min(xp1+size1+2, app->ScreenWidth); k++)
				{
					FP_TYPE xd = k - xp;
					if (xd*xd > lim2) continue; // Length of vector (xd,yd) is larger than circle radius
					app->PutPixelOnBufferZ(k,j, r0,g0,b0,zp);
					drawn = true;
				}
			// Note: We could also limit loop on k based on lim2 value, but this will require sqrt calc or something apprx.
			// Also we could use memset instead of writing r,g,b per byte, but this could improve perf. only if we have many large circles
		}
#endif
		if (drawn)
			return;
		// If no pixels were drawn - fall back to single point
	}
	// Single point
	app->PutPixelOnBufferCheckZ((int)xp,(int)yp,r0,g0,b0,zp);
}

// Put single pixel into memory buffer without screen border checks
// x,y - coordinates
// r,g,b - color
// z - z-buffer value
void StarFly2::PutPixelOnBufferZ(int x, int y, UINT8 r, UINT8 g, UINT8 b, UINT16 z)
{
	if (zBuffer[x + y*ScreenWidth] < z) // Check z-buffer
		return;
	int offset = (x + y*ScreenWidth)<<2;
	// Rows are inverted in DIB Section, but our star field looks the same
	MemBuffer[offset] = b;   // Blue
	MemBuffer[offset+1] = g; // Green
	MemBuffer[offset+2] = r; // Red
	zBuffer[x + y*ScreenWidth] = z;
}

// Put single pixel into memory buffer with border checks
// x,y - coordinates
// r,g,b - color
// z - z-buffer value
void StarFly2::PutPixelOnBufferCheckZ(int x, int y, UINT8 r, UINT8 g, UINT8 b, UINT16 z)
{
	if (0>x || 0>y || x>=ScreenWidth || y>=ScreenHeight)
		return;
	PutPixelOnBufferZ(x,y, r,g,b, z);
}

// Main object constructor
StarFly2::StarFly2()
{
	// Default values
	ScreenSaverWindowed = false; // Fullscreen by default
		
	StarCount = 4000;
	StarSizeFactor = 500;

	FrameInterval = 40; // ~25 fps
	FadeInTime = 2000;

	FlySpeed = (FP_TYPE)0.005;
	Zoom = (FP_TYPE)1.0;
	CenterX = (FP_TYPE)0.5;
	CenterY = (FP_TYPE)0.5;
	FadePower = (FP_TYPE)1.0;
	DarkestRGB = 64;
	ColorType = ColorType_RandomBlackBody;
	SizeType = SizeType_GammaLike;

	XrandSpan = 0;
	YrandSpan = 0;
	ScreenWidth = 1024;
	ScreenHeight = 768;
	ScreenScale = 768;
	FadeInK = 0;
	TotalTimeMs = 0;
	inRender = false;
	PrevTime  = 0;
	MemBuffer = NULL;
	zBuffer = NULL;
	allStars = NULL;

#ifdef _DEBUG
	RandCount = 0;
#endif
}

// Load settings from ini-like file
void StarFly2::LoadSettings ( const char* filename)
{
	// Load from ini file
	FILE * f1;
	fopen_s(&f1, filename, "rt");
	if (NULL != f1)
	{
		while(!feof(f1))
		{
			char buffer[200];
			fgets(buffer,sizeof(buffer),f1);
			char* rightPart = strchr(buffer, '=');
			if (NULL == rightPart)
				continue; // Skip lines without '='
			*rightPart++ = 0; // Put EOL
			char* leftPart = trim(buffer); // Trim extra spaces

			// Integer settings
			if (0 == _stricmp(leftPart, "Stars")) // 
				StarCount = atoi(rightPart);
			else if (0 == _stricmp(leftPart, "FrameInterval"))
				FrameInterval = atoi(rightPart);
			else if (0 == _stricmp(leftPart, "SizeType"))
				SizeType = (RandomStarSize)atoi(rightPart);
			else if (0 == _stricmp(leftPart, "ColorType"))
				ColorType = (RandomColorType)atoi(rightPart);
			else if (0 == _stricmp(leftPart, "DarkestRGB"))
				DarkestRGB = (UINT8)atoi(rightPart);
			else if (0 == _stricmp(leftPart, "FadeInTime"))
				FadeInTime = atoi(rightPart);
			else if (0 == _stricmp(leftPart, "StarSize"))
				StarSizeFactor = (FP_TYPE)atof(rightPart);
			// Float settings
			else if (0 == _stricmp(leftPart, "Speed"))
				FlySpeed = (FP_TYPE)atof(rightPart);
			else if (0 == _stricmp(leftPart, "Zoom"))
				Zoom = (FP_TYPE)atof(rightPart);
			else if (0 == _stricmp(leftPart, "CenterX"))
				CenterX = (FP_TYPE)atof(rightPart);
			else if (0 == _stricmp(leftPart, "CenterY"))
				CenterY = (FP_TYPE)atof(rightPart);
			else if (0 == _stricmp(leftPart, "FadePower"))
				FadePower = (FP_TYPE)atof(rightPart);
		}
		fclose(f1);
	}
	// Only last value matters
	// Not found settings are kept default
	CenterY = (FP_TYPE)1.0 - CenterY; // Rows are reverted in DIB section, so we just invert CenterY
}

/*++
Description:
    This routine initializes the screen saver.
Arguments:
    Window - handle of the window.
Return Value:
    true on success.
    false on failure.
--*/
bool StarFly2::Initialize ( HWND Window )
{
	HDC Dc;
	bool Result = false;

	do
	{
		RECT ScreenRect;

		inRender = false;
		PrevTime = timeGetTime();
		srand(PrevTime); // Reseed random generator to see each time different star field

		Dc = NULL;

		// Save the window.
		OurWindow = Window;
		OurTimer = 0;
		TotalTimeMs = 0;

		// Get window size
		if (FALSE == GetClientRect(OurWindow, &ScreenRect))
			break;

		ScreenWidth = ScreenRect.right - ScreenRect.left;   // Only initial window sizes are used for render
		ScreenHeight = ScreenRect.bottom - ScreenRect.top;
		ScreenScale = min(ScreenWidth, ScreenHeight) * Zoom;
		FadeInK = (FP_TYPE)1.0 / FadeInTime;

		XrandSpan = ScreenWidth * FarPlane / ScreenScale;  // Spans on X and Y axis of rect.cuboid in which stars are generated
		YrandSpan = ScreenHeight * FarPlane / ScreenScale; // FarPlane is far side of this cuboid and it is completely seen on screen

		allStars = new Star[StarCount];
		zBuffer = new UINT16[ScreenWidth*ScreenHeight];

#ifdef _DEBUG
		RandCount = 0;
#endif
		for (int i = 0; i<StarCount; i++)
		{
			allStars[i].z = (FP_TYPE)-1.0; // To trigger randomize in Process
			allStars[i].state = State_New;
			allStars[i].Process(this);
			allStars[i].state = State_Generated;
		}

#if 0	// Debug - star dead ahead
		allStars[0].x = 0;
		allStars[0].y = 0;
		allStars[0].z = 200;
		allStars[0].size = StarSizeFactor*(FP_TYPE)27.15; // Biggest possible with cur random generator
#endif

		// Prepare DC and bitmap for fast drawing
		Dc = GetDC(Window);
		MemDc = CreateCompatibleDC(Dc);
		if (NULL == MemDc)
			break;

		// Based on https://stackoverflow.com/questions/10036527/render-buffer-on-screen-in-windows
		BITMAPINFO bmi;
		memset(&bmi, 0, sizeof(bmi));
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = ScreenWidth;
		bmi.bmiHeader.biHeight = ScreenHeight;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		MemBitmap = CreateDIBSection(Dc, &bmi, DIB_RGB_COLORS, (void **)&MemBuffer, 0, 0);
		if (NULL == MemBuffer || NULL == MemBitmap)
			break;

		OrigBitmap1 = (HBITMAP)SelectObject(MemDc, MemBitmap);
		if (NULL == OrigBitmap1)
			break;

		// Kick off the timer.
		OurTimer = timeSetEvent(FrameInterval,   // Interval
								FrameInterval,   // Resolution
								TimerEvent,      // Which function to call
								(DWORD_PTR)this, // User data - pointer to our main object
								TIME_PERIODIC | TIME_CALLBACK_FUNCTION);

		if (0 == OurTimer)
			break;

		PrevTime = timeGetTime();

		Result = true;
	} while (false);

	if (NULL != Dc)
		ReleaseDC(Window, Dc);
	return Result;
}

/*++
Description:
    This routine tears down screensaver.
--*/
VOID StarFly2::Destroy (  )
{
	if (0 != OurTimer)
		timeKillEvent(OurTimer);

	// If Exit event happens during rendering - we should wait till it ended before freeing arrays
	unsigned int Time1 = timeGetTime();
	while(inRender &&
		timeGetTime()<(Time1 + 1000)) // But no more than 1 second
	{
		Sleep(1);
	}
	StarCount = 0; // Should additionally trigger exit from render loop

	// Restore GDI objects
	SelectObject(MemDc, OrigBitmap1);
	DeleteObject(MemBitmap);
	DeleteDC(MemDc);

	// Free allocations
	delete[] allStars;

	return;
}

/*++
Description:
    This routine updates the screen.
Return Value:
    true on success.
    false if a serious failure occurred.
--*/
bool StarFly2::UpdateScreen ( )
{
	HDC Dc;
	bool Result = false;

	do
	{
		inRender = true; // Set barrier

		Dc = GetDC(OurWindow);
		if (NULL == Dc)
			break;

		// Update main time.
		unsigned int CurTime = timeGetTime();
		unsigned int PassedTimeMs = CurTime - PrevTime;
		PrevTime = CurTime;
		TotalTimeMs += PassedTimeMs;

		RECT ScreenRect;

		// Get window size
		if (FALSE == GetClientRect(OurWindow, &ScreenRect))
			break;

		int WindowWidth = ScreenRect.right - ScreenRect.left;
		int WindowHeight = ScreenRect.bottom - ScreenRect.top;

		// Clear - fill with black color
		memset(MemBuffer, 0, ScreenHeight * ScreenWidth * 4); // 4 bytes for 32-bit RGB
		memset(zBuffer, 0xFF, ScreenHeight * ScreenWidth * sizeof(UINT16)); // Max distance

#ifdef _DEBUG
		RandCount = 0;
#endif
		FP_TYPE movedZ = FlySpeed*PassedTimeMs;

		// Render all stars
		for (int i = 0; i < StarCount; i++)
		{
			allStars[i].z -= movedZ;                // Stars are moved towards viewer
			if (0 < allStars[i].fadeIn)
				allStars[i].fadeIn -= PassedTimeMs; // Tick fade-in
			allStars[i].Process(this);              // Update star screen position or randomize it
			allStars[i].Render(this);               // Render star (to MemBuffer)
		}

#if _DEBUG //Debug prints
		{
			char txt[300];
			int len1 = sprintf_s(txt,sizeof(txt),"ms:%u rnd:%i",PassedTimeMs,RandCount);
			TextOut(MemDc,0,0,txt,len1);
		}
#endif

		if (FALSE == BitBlt( // Copy rendered to main screen
			Dc,
			0, 0,
			min(ScreenWidth, WindowWidth),   // Just in case window size changed for some reason
			min(ScreenHeight, WindowHeight),
			MemDc,
			0, 0,
			SRCCOPY))
			break;

		Result = true;
	} while(false);

	if (NULL != Dc)
		ReleaseDC(OurWindow, Dc);
	InvalidateRect(OurWindow, NULL, FALSE);

	inRender = false; // Clear barrier

	return Result;
}

// Static Methods and Global Functions ------------------------------------------------------------------

/*++
Description:
    This routine is the callback from the timer.
Arguments:
    TimerId - Supplies the timer ID that fired. Unused.
    Message - Supplies the timer message. Unused.
    User - Supplies the user paramater. Currently it is pointer to our main object.
    Parameter1 - Supplies an unused parameter.
    Parameter2 - Supplies an unused parameter.
--*/
VOID CALLBACK StarFly2::TimerEvent ( UINT TimerId, UINT Message, DWORD_PTR User, DWORD_PTR Parameter1, DWORD_PTR Parameter2 )
{
	StarFly2* app = (StarFly2*)User;

	if (!app->UpdateScreen())
		PostQuitMessage(0);
	return;
}

/*++
Description:
    This routine is the main message pump for the screen saver window. It
    receives messages pertaining to the window and handles interesting ones.
Arguments:
    hWnd - Supplies the handle for the overall window.
    Message - Supplies the message being sent to the window.
    WParam - Supplies the "width" parameter, basically the first parameter of
        the message.
    LParam - Supplies the "length" parameter, basically the second parameter of
        the message.
Return Value:
    Returns false if the message was handled, or true if the message was not
    handled and the default handler should be invoked.
--*/
LRESULT WINAPI StarFly2::ScreenSaverProc ( HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam )
{
    BOOL Result;

    switch (Message)
	{
    case WM_CREATE:
		{
			StarFly2* const app = (StarFly2*) ((CREATESTRUCT*)lParam)->lpCreateParams; // Gets lpParam of CreateWindow/Ex
			SetWindowLongPtr(hWnd, GWL_USERDATA, (LONG)app); // Set window user data to be pointer to our main object

			Result = app->Initialize(hWnd);
			if (!Result)
				PostQuitMessage(0);

			GetCursorPos(&app->MousePosition);
		}
		break;
	case WM_ERASEBKGND:
		{
			RECT ScreenRect;
			HDC Dc = GetDC(hWnd);
			GetClientRect(hWnd, &ScreenRect);
			FillRect(Dc, &ScreenRect, (HBRUSH)GetStockObject(BLACK_BRUSH));
			ReleaseDC(hWnd, Dc);
			break;
		}
	case WM_DESTROY:
		{
			StarFly2* app = (StarFly2*) GetWindowLongPtr(hWnd, GWL_USERDATA);
			app->Destroy();
			PostQuitMessage(0);
			break;
		}
	case WM_SETCURSOR:
		{
			StarFly2 const * app = (StarFly2*) GetWindowLongPtr(hWnd, GWL_USERDATA);
			if (!app->ScreenSaverWindowed)
				SetCursor(NULL);
			break;
		}
	case WM_CLOSE:
		{
			StarFly2 const * app = (StarFly2*) GetWindowLongPtr(hWnd, GWL_USERDATA);
			if (!app->ScreenSaverWindowed)
				ShowCursor(TRUE);
			break;
		}
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_KEYDOWN:
	case WM_KEYUP:
		{
			StarFly2 const * app = (StarFly2*) GetWindowLongPtr(hWnd, GWL_USERDATA);
			if (SettlingTime < app->TotalTimeMs)
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			break;
		}
	case WM_MOUSEMOVE:
		{
			POINT Difference;
			POINT CurMousePosition;
			StarFly2* const app = (StarFly2*) GetWindowLongPtr(hWnd, GWL_USERDATA);
			// Ignore mouse movements if the screen saver is in the preview window.
			if (app->ScreenSaverWindowed)
				break;

			// Random little mouse movements or spurious messages need to be
			// tolerated. If the mouse has moved more than a few pixels in any
			// direction, the user really is controlling it, and the screensaver
			// needs to close.
			GetCursorPos(&CurMousePosition);
			Difference.x = CurMousePosition.x - app->MousePosition.x;
			Difference.y = CurMousePosition.y - app->MousePosition.y;
			if (0 > Difference.x)
				Difference.x = -Difference.x;
			if (0 > Difference.y)
				Difference.y = -Difference.y;
			if (((Difference.x > MouseTolerance) ||
				(Difference.y > MouseTolerance)) &&
				(app->TotalTimeMs > SettlingTime))
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			break;
		}
	case WM_SYSCOMMAND:
		if((SC_SCREENSAVE == wParam) || (SC_CLOSE == wParam))
			return FALSE;
		break;
	}

	return DefWindowProc(hWnd, Message, wParam, lParam);
}


/*++
Description:
    This routine is the main entry point for a Win32 application.
Arguments:
    hInstance - Supplies a handle to the current instance of the application.
    hPrevInstance - Supplies a handle to the previous instance of the
        application.
    lpCmdLine - Supplies a pointer to a null-terminated string specifying the
        command line for the application, excluding the program name.
    nCmdShow - Specifies how the window is to be shown.
Return Value:
    Returns true on success, false on failure.
--*/
INT WINAPI WinMain ( HINSTANCE hInstance, HINSTANCE hPrevInst, LPSTR lpszCmdParam, INT nCmdShow )

{
	WNDCLASS Class;
	MSG Message;
	HWND Parent = NULL;
	RECT ParentRect;
	DWORD Properties;
	INT Return = -1;
	HWND Window;
	LONG WindowWidth = 1024;
	LONG WindowHeight = 768;
	bool Configure = false;

	StarFly2 starFly;                    // Our main object. Will be hold by WinMain till it exits

	// Check filename of our executable file
	char z[MAX_PATH];
	DWORD len = GetModuleFileName(NULL, z, sizeof(z));
	if ( 3 < len ) // 4 chars or more
	{
		if (sizeof(z) == len)
			len--;
		z[len] = 0; // [GetModuleFileName] 'Windows XP: The string is truncated to nSize characters and is not null-terminated.' - So add null-terminator

		// For .scr files "Configure" in context menu will launch file without arguments
		// Simple click or "Test" will launch file with "/S"
		// On 'Screen saver settings':
		// Small preview on dialog - file launched with "/p <id>"
		// Button "Preview" - file launched with "/s"
		// Button "Settings..." - file launched with "/c:<id>"
		// For .exe file default is start without arguments
		
		Configure = (0 == _stricmp(z+len-4, ".scr")); // So if we are ".scr" file then default is Configuration, otherwise - default is Show

		// Notes on current directory
		// Preview from 'Screen saver settings' run file with cwd = c:\Windows\ImmersiveControlPanel\
		// Screen saver normal execution - with cwd = c:\Windows\System32\
		// (observed on Win10-x64)

		// Load settings from .ini located and named as .scr/.exe
		if ('.' == z[len-4]) // ~Check if extension has proper len
		{
			strcpy_s(z+len-3, 4, "ini"); // 4 with null
			starFly.LoadSettings(z);  // Load settings from ini
		}
	}

	// Parse any parameters. /C runs the 'configure' dialog.
	if ((strstr(lpszCmdParam, "/c") != NULL) ||
		(strstr(lpszCmdParam, "/C") != NULL)) {

		Configure = true;
	}

	// /S runs the 'show' - how .scr file is called by user click
	if ((strstr(lpszCmdParam, "/s") != NULL) ||
		(strstr(lpszCmdParam, "/S") != NULL)) {

		Configure = false;
	}

	// /W runs the application in a window.
	if ((strstr(lpszCmdParam, "/w") != NULL) ||
		(strstr(lpszCmdParam, "/W") != NULL)) {

		starFly.ScreenSaverWindowed = true;
		Configure = false;
	}

	// P or I also runs the application in a window with a parent.
	if ((strstr(lpszCmdParam, "/p") != NULL) ||
		(strstr(lpszCmdParam, "/P") != NULL) ||
		(strstr(lpszCmdParam, "/i") != NULL) ||
		(strstr(lpszCmdParam, "/I") != NULL)) {

		Parent = (HWND)atoi(lpszCmdParam + 3);
		GetWindowRect(Parent, &ParentRect);
		if (FALSE == IsWindow(Parent)) {
			Return = 0;
			goto WinMainEnd;
		}

		WindowWidth = ParentRect.right - ParentRect.left;
		WindowHeight = ParentRect.bottom - ParentRect.top;
		starFly.ScreenSaverWindowed = true;
		Configure = false;
	}

	// Register the window class.
	Class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	Class.lpfnWndProc = StarFly2::ScreenSaverProc;
	Class.cbClsExtra = 0;
	Class.cbWndExtra = 0;
	Class.hInstance = hInstance;
	Class.hIcon = NULL;
	Class.hCursor = NULL;
	Class.hbrBackground = NULL;
	Class.lpszMenuName = NULL;
	Class.lpszClassName = starFly.ApplicationName;
	if (0 == RegisterClass(&Class))
		return 0;

	// For configurations, show the message box and quit.
	if (Configure)
	{	// No configuration GUI, sorry
		MessageBox(NULL, "Please edit .ini file for configuration.", starFly.ApplicationName, MB_OK | MB_ICONINFORMATION);
		goto WinMainEnd;
	}

	// Create the window.
	if (starFly.ScreenSaverWindowed) {
		if (Parent != NULL)
			Properties = WS_VISIBLE | WS_CHILD;
		else
			Properties = WS_VISIBLE | WS_POPUP;

		Window = CreateWindowEx(WS_EX_TOPMOST,
								starFly.ApplicationName, starFly.ApplicationName,
								Properties,
#ifdef _DEBUG
								0, -WindowHeight, // Windowed debug on top monitor
#else
								0, 0,
#endif
								WindowWidth, WindowHeight,
								Parent,
								NULL,
								hInstance,
								&starFly);

	} else {
		Window = CreateWindowEx(WS_EX_TOPMOST,
								starFly.ApplicationName, starFly.ApplicationName,
								WS_VISIBLE | WS_POPUP,
								GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
								GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN),
								NULL,
								NULL,
								hInstance,
								&starFly);
	}

	if (NULL == Window) {
		Return = 0;
		goto WinMainEnd;
	}

	if (!starFly.ScreenSaverWindowed)
		ShowCursor(FALSE);

	SetFocus(Window);
	UpdateWindow(Window);

	// Pump messages to the window.
	while (GetMessage(&Message, NULL, 0, 0) > 0) {
		TranslateMessage(&Message);
		DispatchMessage(&Message);
	}

	WinMainEnd:
	ShowCursor(TRUE);
	UnregisterClass(starFly.ApplicationName, hInstance);
	return Return;
}

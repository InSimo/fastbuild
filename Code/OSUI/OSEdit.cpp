// OSEdit.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "OSEdit.h"

// OSUI
#include "OSUI/OSFont.h"
#include "OSUI/OSWindow.h"

// Core
#include "Core/Env/Assert.h"

// System
#if defined( __WINDOWS__ )
    #include "Core/Env/WindowsHeader.h"
#endif

// Defines
//------------------------------------------------------------------------------

// CONSTRUCTOR
//------------------------------------------------------------------------------
OSEdit::OSEdit( OSWindow * parentWindow ) :
    OSWidget( parentWindow ),
    m_Font( nullptr )
{
}

// SetFont
//------------------------------------------------------------------------------
void OSEdit::SetFont( OSFont * font )
{
    ASSERT( !m_Initialized ); // Change font after Init not currently supported
    m_Font = font;
}

// Init
//------------------------------------------------------------------------------
void OSEdit::Init( int32_t x, int32_t y, uint32_t w, uint32_t h, const char * editText )
{
    m_Text = editText;
    #if defined( __WINDOWS__ )
        // Create control
        m_Handle = CreateWindowEx( WS_EX_TRANSPARENT,
                                   "EDIT",
                                   "",
                                   WS_CHILD | WS_VISIBLE | ES_LEFT | WS_SYSMENU | WS_BORDER,
                                   x, y,
                                   (int32_t)w, (int32_t)h,
                                   (HWND)m_Parent->GetHandle(),
                                   NULL,
                                   (HINSTANCE)m_Parent->GetHInstance(),
                                   NULL );

        // Set font
        SendMessage( (HWND)m_Handle, WM_SETFONT, (WPARAM)m_Font->GetFont(), NULL );

        // Set text
        SendMessage( (HWND)m_Handle, WM_SETTEXT, NULL, (LPARAM)m_Text.Get() );
    #else
        (void)x;
        (void)y;
        (void)w;
        (void)h;
    #endif

    OSWidget::Init();
}

// GetText
//------------------------------------------------------------------------------
const AString& OSEdit::GetText()
{
    #if defined( __WINDOWS__ )
        uint32_t len = (uint32_t) SendMessage((HWND)m_Handle, (UINT)WM_GETTEXTLENGTH, (WPARAM)0, (LPARAM)0 );
        m_Text.SetLength( len );
        if (len > 0)
            m_Text.SetLength( (uint32_t) SendMessage((HWND)m_Handle, (UINT)WM_GETTEXT, (WPARAM)len + 1, (LPARAM)m_Text.Get() ) );
    #else
    #endif
    return m_Text;
}

//------------------------------------------------------------------------------

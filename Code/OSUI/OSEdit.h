// OSEdit.h
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "OSWidget.h"
// Core
#include "Core/Env/Types.h"
#include "Core/Strings/AString.h"

// Forward Declarations
//------------------------------------------------------------------------------
class OSFont;

// OSEdit
//------------------------------------------------------------------------------
class OSEdit : public OSWidget
{
public:
    explicit OSEdit( OSWindow * parentWindow );

    void SetFont( OSFont * font );

    void Init( int32_t x, int32_t y, uint32_t w, uint32_t h, const char * editText );

    const AString& GetText();

protected:
    OSFont *    m_Font;
    AString     m_Text;
};

//------------------------------------------------------------------------------


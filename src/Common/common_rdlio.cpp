// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// RDL readers and writers: the RDLX tag-markup parser (SAX2 content handler), BinaryRDLReader /
// BinaryRDLWriter and XMLReader / XMLWriter -- split out of common_text.cpp (retail
// RichDisplayList.cpp) in CMN-SPLIT phase B.

#include "common_text.h"
#include "common_text_internal.h"
#include "dacom.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <map>
#include <string>
#include <deque>
#include <list>
#include <vector>
#include <windows.h>
#include <objbase.h>   // CoCreateInstance, IUnknown -- for the XMLReader::read_buffer/read_file
#include <oleauto.h>   // BSTR/VARIANT, SysAllocStringByteLen family -- same purpose
#include <comdef.h>    // _bstr_t / _com_issue_error (XMLReader::read_buffer); links comsuppw(d).lib

// sax2.h/msxml2.h (Microsoft's real SAX2 COM headers) are NOT present anywhere under this
// toolchain's Windows SDK (checked: no sax2.h under any "Windows Kits" install on this machine),
// so XMLReader::read_buffer/read_file below hand-declare the tiny slice of MSXML2's SAX2 COM
// surface they actually need, following the same EXTERN_C const CLSID/IID pattern already
// established for the movie GUIDs in src/movie/MoviePlay.cpp.
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")   // IID_IUnknown's definition lives here

// DAT_063ed98c / PTR_u_gray_063ed9ac — named colors recognized by the RDLX text-markup
// "color=" attribute of the TRA tag (ParseRdlxTag's wcsncmp loop against these 8 names;
// values are COLORREF 0x00BBGGRR).
struct FRdlxNamedColor
{
    wchar_t const* Name;
    unsigned long Value; // COLORREF (0x00BBGGRR)
};
static const FRdlxNamedColor GRdlxNamedColors[8] = {
    {L"gray", 0x00808080},   {L"blue", 0x00E04848},  {L"green", 0x001DBF3B},
    {L"aqua", 0x00E0C387},   {L"red", 0x001D1DBF},    {L"fuchsia", 0x00C20088},
    {L"yellow", 0x0052EAF5}, {L"white", 0x00FFFFFF},
};

// ============================================================================
// RDLX tag-markup parser -- item 2416 / retail FUN_0632b020
// (E:\FL\Scratch\Source\Common\RichDisplayList.cpp, Common.dll.txt:362628-364001).
// These SAX-handler helpers are Common.dll-internal: retail exports none of them (they are
// call-table entries and plain helpers of RichDisplayList.cpp), and nothing outside this file
// calls them, so they are file-local (static) here and not declared in common_text.h.
// ============================================================================

// The MSXML SAX2 interfaces the RDLX parser talks to. sax2.h / msxml2.h are not available in
// this toolchain, so the slice that is needed is hand-declared from Microsoft's public SAX2 IDL
// (method order and signatures are the documented ones, not guessed). Retail calls them as COM
// methods: `this` pushed on the stack, callee pops (__stdcall), through the object's vtable.
namespace
{
    // {079AA557-4A18-424A-8EEE-E39F0A8D41B9} CLSID_SAXXMLReader and
    // {A4F96ED0-F829-476E-81C0-CDC7BD2A0802} IID_ISAXXMLReader, read byte for byte out of
    // Common.dll's .data (DAT_063ed96c / DAT_063ed97c).
    const CLSID CLSID_SAXXMLReader_Rdlx =
        { 0x079aa557, 0x4a18, 0x424a, { 0x8e, 0xee, 0xe3, 0x9f, 0x0a, 0x8d, 0x41, 0xb9 } };
    const IID IID_ISAXXMLReader_Rdlx =
        { 0xa4f96ed0, 0xf829, 0x476e, { 0x81, 0xc0, 0xcd, 0xc7, 0xbd, 0x2a, 0x08, 0x02 } };

    // ISAXAttributes, the attribute list startElement receives. ParseRdlxTag uses getLength
    // (slot 3, +0x0C, 0632b09f), getLocalName (slot 5, +0x14, 0632b0c2) and getValue (slot 13,
    // +0x34, 0632b0d1).
    struct ISAXAttributesRdlx : public IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE getLength(int* pnLength) = 0;
        virtual HRESULT STDMETHODCALLTYPE getURI(int nIndex, const wchar_t** ppwchUri, int* pcchUri) = 0;
        virtual HRESULT STDMETHODCALLTYPE getLocalName(int nIndex, const wchar_t** ppwchLocalName,
                                                       int* pcchLocalName) = 0;
        virtual HRESULT STDMETHODCALLTYPE getQName(int nIndex, const wchar_t** ppwchQName, int* pcchQName) = 0;
        virtual HRESULT STDMETHODCALLTYPE getName(int nIndex, const wchar_t** ppwchUri, int* pcchUri,
                                                  const wchar_t** ppwchLocalName, int* pcchLocalName,
                                                  const wchar_t** ppwchQName, int* pcchQName) = 0;
        virtual HRESULT STDMETHODCALLTYPE getIndexFromName(const wchar_t* pwchUri, int cchUri,
                                                           const wchar_t* pwchLocalName, int cchLocalName,
                                                           int* pnIndex) = 0;
        virtual HRESULT STDMETHODCALLTYPE getIndexFromQName(const wchar_t* pwchQName, int cchQName,
                                                            int* pnIndex) = 0;
        virtual HRESULT STDMETHODCALLTYPE getType(int nIndex, const wchar_t** ppwchType, int* pcchType) = 0;
        virtual HRESULT STDMETHODCALLTYPE getTypeFromName(const wchar_t* pwchUri, int cchUri,
                                                          const wchar_t* pwchLocalName, int cchLocalName,
                                                          const wchar_t** ppwchType, int* pcchType) = 0;
        virtual HRESULT STDMETHODCALLTYPE getTypeFromQName(const wchar_t* pwchQName, int cchQName,
                                                           const wchar_t** ppwchType, int* pcchType) = 0;
        virtual HRESULT STDMETHODCALLTYPE getValue(int nIndex, const wchar_t** ppwchValue, int* pcchValue) = 0;
        virtual HRESULT STDMETHODCALLTYPE getValueFromName(const wchar_t* pwchUri, int cchUri,
                                                           const wchar_t* pwchLocalName, int cchLocalName,
                                                           const wchar_t** ppwchValue, int* pcchValue) = 0;
        virtual HRESULT STDMETHODCALLTYPE getValueFromQName(const wchar_t* pwchQName, int cchQName,
                                                            const wchar_t** ppwchValue, int* pcchValue) = 0;
    };

    // ISAXContentHandler, implemented by the RDLX sink below.
    struct ISAXContentHandlerRdlx : public IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE putDocumentLocator(IUnknown* pLocator) = 0;
        virtual HRESULT STDMETHODCALLTYPE startDocument() = 0;
        virtual HRESULT STDMETHODCALLTYPE endDocument() = 0;
        virtual HRESULT STDMETHODCALLTYPE startPrefixMapping(const wchar_t* pwchPrefix, int cchPrefix,
                                                              const wchar_t* pwchUri, int cchUri) = 0;
        virtual HRESULT STDMETHODCALLTYPE endPrefixMapping(const wchar_t* pwchPrefix, int cchPrefix) = 0;
        virtual HRESULT STDMETHODCALLTYPE startElement(const wchar_t* pwchNamespaceUri, int cchNamespaceUri,
                                                       const wchar_t* pwchLocalName, int cchLocalName,
                                                       const wchar_t* pwchQName, int cchQName,
                                                       ISAXAttributesRdlx* pAttributes) = 0;
        virtual HRESULT STDMETHODCALLTYPE endElement(const wchar_t* pwchNamespaceUri, int cchNamespaceUri,
                                                     const wchar_t* pwchLocalName, int cchLocalName,
                                                     const wchar_t* pwchQName, int cchQName) = 0;
        virtual HRESULT STDMETHODCALLTYPE characters(const wchar_t* pwchChars, int cchChars) = 0;
        virtual HRESULT STDMETHODCALLTYPE ignorableWhitespace(const wchar_t* pwchChars, int cchChars) = 0;
        virtual HRESULT STDMETHODCALLTYPE processingInstruction(const wchar_t* pwchTarget, int cchTarget,
                                                                 const wchar_t* pwchData, int cchData) = 0;
        virtual HRESULT STDMETHODCALLTYPE skippedEntity(const wchar_t* pwchName, int cchName) = 0;
    };

    // ISAXXMLReader. XMLReader::read_buffer / read_file use putContentHandler (slot 10, +0x28),
    // parse (slot 19, +0x4c) and parseURL (slot 20, +0x50). The handler-type parameters of the
    // slots nobody calls are declared as IUnknown*.
    struct ISAXXMLReaderRdlx : public IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE getFeature(const wchar_t* pwchName, VARIANT_BOOL* pvfValue) = 0;
        virtual HRESULT STDMETHODCALLTYPE putFeature(const wchar_t* pwchName, VARIANT_BOOL vfValue) = 0;
        virtual HRESULT STDMETHODCALLTYPE getProperty(const wchar_t* pwchName, VARIANT* pvarValue) = 0;
        virtual HRESULT STDMETHODCALLTYPE putProperty(const wchar_t* pwchName, VARIANT varValue) = 0;
        virtual HRESULT STDMETHODCALLTYPE getEntityResolver(IUnknown** ppResolver) = 0;
        virtual HRESULT STDMETHODCALLTYPE putEntityResolver(IUnknown* pResolver) = 0;
        virtual HRESULT STDMETHODCALLTYPE getContentHandler(ISAXContentHandlerRdlx** ppHandler) = 0;
        virtual HRESULT STDMETHODCALLTYPE putContentHandler(ISAXContentHandlerRdlx* pHandler) = 0;
        virtual HRESULT STDMETHODCALLTYPE getDTDHandler(IUnknown** ppHandler) = 0;
        virtual HRESULT STDMETHODCALLTYPE putDTDHandler(IUnknown* pHandler) = 0;
        virtual HRESULT STDMETHODCALLTYPE getErrorHandler(IUnknown** ppHandler) = 0;
        virtual HRESULT STDMETHODCALLTYPE putErrorHandler(IUnknown* pHandler) = 0;
        virtual HRESULT STDMETHODCALLTYPE getBaseURL(const wchar_t** ppwchBaseUrl) = 0;
        virtual HRESULT STDMETHODCALLTYPE putBaseURL(const wchar_t* pwchBaseUrl) = 0;
        virtual HRESULT STDMETHODCALLTYPE getSecureBaseURL(const wchar_t** ppwchSecureBaseUrl) = 0;
        virtual HRESULT STDMETHODCALLTYPE putSecureBaseURL(const wchar_t* pwchSecureBaseUrl) = 0;
        virtual HRESULT STDMETHODCALLTYPE parse(VARIANT varInput) = 0;
        virtual HRESULT STDMETHODCALLTYPE parseURL(const wchar_t* pwchUrl) = 0;
    };
}

// The parser state retail's SAX sink carries behind its vtable (RDLXParser below; layout read off
// XMLReader::read_buffer's stack initialisation .text:0632ac5a-0632ac6a). The three content
// callbacks get the sink as their stack-passed `this`.
struct RdlxParseContext
{
    ISAXXMLReaderRdlx* Reader;    // +0x04 -- created straight into this slot by CoCreateInstance
    unsigned char bSkipParse;     // +0x08 -- zeroed and never written again; read_buffer / read_file
                                  //          skip the parse and fail when it is set
    RenderDisplayList* Rdl;       // +0x0C -- target list new nodes are appended to
    bool bTextNodeOpen;           // +0x10 -- true once a TEXT tag has created its single, shared
                                  //          TextNode for the current RDL (guards against a
                                  //          second one; see ParseRdlxTag's TEXT case). Cleared
                                  //          by ParseRdlxEndElement (retail LAB_0632bdd0, call
                                  //          table index 9) when the matching </TEXT> end tag is
                                  //          seen, so a later sibling TEXT tag can open a new one.
};

// The three content callbacks of the RDLX sink (vftable .rdata:063a4644, ISAXContentHandler
// order): slot 8 startElement = ParseRdlxTag (FUN_0632b020), slot 9 endElement =
// ParseRdlxEndElement (LAB_0632bdd0), slot 10 characters = ParseRdlxCharacters (LAB_0632be60).
// Retail receives the sink as its stack `this` plus the full SAX argument list; only the local
// name (and its length), the attributes, and the character run are read, so the port passes
// just those. Every other sink slot is a no-op.

// startElement: RDL returns at once; TRA, TEXT, PARA, POS, STYLE, JUST, PUSH and POP each append
// one node to Ctx.Rdl; any other tag name does nothing.
static void __cdecl ParseRdlxTag(RdlxParseContext& Ctx, wchar_t const* TagName,
                                 unsigned int TagNameLen, ISAXAttributesRdlx* Attributes);

// endElement: only a "TEXT" end tag has an effect (clears Ctx.bTextNodeOpen).
static void __cdecl ParseRdlxEndElement(RdlxParseContext& Ctx, wchar_t const* TagName,
                                        unsigned int TagNameLen);

// characters: appends the run to the last node of Ctx.Rdl when a TEXT tag is open and that node
// is a TextNode (RDLNode slot +0x20, operator TextNode*).
static void __cdecl ParseRdlxCharacters(RdlxParseContext& Ctx, wchar_t const* Text,
                                        unsigned int Len);

// ============================================================================
// ParseRdlxTag -- item 2416, retail FUN_0632b020 (Common.dll.txt:362628-364001). Caller:
// RDLXParser::startElement (sink vtable .rdata:063a4644 slot 8); RDL is a no-op; PUSH/POP tags
// just build an AttribStackNode like every other leaf tag -- the actual attribute push/pop
// happens later at AttribStackNode::Execute() time.
// ============================================================================

namespace CmnText
{
    // Every append site of the RDLX parser and of BinaryRDLReader::read_buffer (e.g. PARA
    // 0632b76a-0632b795, PUSH 0632bbf8-0632bcb8 with FUN_0632c490 inlined, read_buffer's
    // records): Type::Clone() of the stack node; FUN_0632c490 links a cell before the sentinel
    // and stores a second Clone() of that clone (FUN_0632d550, RDLNode slot +0x1c); the count
    // is bumped; then the first clone is deleted when non-null (FUN_063272c0 / 0632bcad,
    // scalar deleting dtor with 1).
    void RdlxAppendNode(RenderDisplayList& Rdl, RDLNode const& Node)
    {
        RDLNode* FirstClone = Node.Clone();
        InsertRdlCloneBefore(Rdl, Rdl.NodeList, FirstClone);
        delete FirstClone;
    }
}

namespace
{
    // FUN_0632bd10 (Common.dll.c:179087-179105): single hex digit -> value, anything else -> 0.
    unsigned int RdlxHexNibble(wchar_t Ch)
    {
        if (iswdigit(Ch)) return (unsigned int)Ch - 0x30;
        if (Ch > 0x40 && Ch < 0x47) return (unsigned int)Ch - 0x37; // 'A'-'F'
        if (Ch > 0x60 && Ch < 0x67) return (unsigned int)Ch - 0x57; // 'a'-'f'
        return 0;
    }

    // FUN_0632bd50, and the same sequence inlined at every numeric RDLX attribute
    // (mask/def/data/font/id): the value is copied into an alloca buffer (FUN_06391dc0 is the
    // _chkstk probe) of 2*Len+2 bytes -- Len+1 characters, so the character AFTER the value
    // (normally the closing quote) is copied too, and no terminator is added. Then decimal via
    // wcstol, unless Len > 2 (signed JLE) and the copy starts with "0x" (case-sensitive), in
    // which case hex via wcstoul. The port copies the same Len+1 characters and terminates
    // after them; retail's parse would run on into uninitialised stack if Value[Len] and the
    // bytes behind it were all digits (undefined behaviour, not reproduced).
    unsigned long ParseRdlxNumber(wchar_t const* Value, int Len)
    {
        std::wstring Temp(Value, (std::wstring::size_type)Len + 1u);
        if (Len > 2 && Temp[0] == L'0' && Temp[1] == L'x')
        {
            return wcstoul(Temp.c_str(), nullptr, 16);
        }
        return (unsigned long)wcstol(Temp.c_str(), nullptr, 10);
    }

    // "true"/"false" boolean attribute parser shared by TRA's bold/italic/underline and POS's
    // relV/relH (Common.dll.txt ~0x0632b1f0 area and duplicates, all citing RichDisplayList.cpp
    // line 0xd5a in the warning). Anything other than "true"/"false" logs a WARNING:General and
    // is treated as false.
    bool ParseRdlxBool(wchar_t const* Value, int Len)
    {
        if (wcsncmp(Value, L"true", Len) == 0)
        {
            return true;
        }
        if (wcsncmp(Value, L"false", Len) != 0)
        {
            FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                  "E:\\FL\\Scratch\\Source\\Common\\RichDisplayList.cpp", 0xd5a,
                  "Invalid boolean in RDLX file. Assuming false.\n");
        }
        return false;
    }
}

void __cdecl ParseRdlxTag(RdlxParseContext& Ctx, wchar_t const* TagName, unsigned int TagNameLen,
                          ISAXAttributesRdlx* Attributes)
{
    if (wcsncmp(TagName, L"RDL", TagNameLen) == 0)
    {
        // 0632b058: straight to the shared epilogue.
        return;
    }

    // Ctx+0x0C is dereferenced at each append without a test (0632b689 and the other tags).
    RenderDisplayList* Rdl = Ctx.Rdl;

    if (wcsncmp(TagName, L"TRA", TagNameLen) == 0)
    {
        // Common.dll.txt:0632b072-0632b6ad. Builds a TRANode from up to 8 recognized attributes.
        unsigned int Mask = 0;    // param_5 -- which bit-ranges this node actually applies
        unsigned int DefBits = 0; // param_4 -- which bit-ranges should be inherited from context
        unsigned int AttrsIn = 0; // local_34 -- explicit packed value (color in bits 8-31, font
                                  //             index + bold/italic/underline in bits 0-7)

        auto ApplyDefaultBit = [&](unsigned int Bit)
        {
            // Shared LAB_0632b543 tail: an attribute value of literal "default" marks its bit
            // range as both "applies" (Mask) and "inherit from context" (DefBits), leaving
            // AttrsIn's corresponding bits untouched (TRANode::Execute already implements the
            // inherit-vs-own-value selection via Field10/DefBits).
            Mask |= Bit;
            DefBits |= Bit;
        };

        int AttrCount = 0;
        Attributes->getLength(&AttrCount);
        for (int i = 0; i < AttrCount; ++i)
        {
            wchar_t const* Name = nullptr; int NameLen = 0;
            wchar_t const* Value = nullptr; int ValueLen = 0;
            Attributes->getLocalName(i, &Name, &NameLen);
            Attributes->getValue(i, &Value, &ValueLen);

            // Common.dll.txt: `wcsncmp(Value,"default",ValueLen) != 0`. True = an explicit,
            // non-"default" value was given (needs real parsing); false = value=="default".
            bool bExplicitValue = (wcsncmp(Value, L"default", ValueLen) != 0);

            if (wcsncmp(Name, L"mask", NameLen) == 0)
            {
                Mask = ParseRdlxNumber(Value, ValueLen);
            }
            else if (wcsncmp(Name, L"def", NameLen) == 0)
            {
                DefBits = ParseRdlxNumber(Value, ValueLen);
            }
            else if (wcsncmp(Name, L"data", NameLen) == 0)
            {
                AttrsIn = ParseRdlxNumber(Value, ValueLen);
            }
            else if (wcsncmp(Name, L"color", NameLen) == 0)
            {
                unsigned int ColorValue = 0xffffff;
                bool bApply = true;
                if (Value[0] == L'#')
                {
                    if (ValueLen == 7)
                    {
                        // #RRGGBB. Common.dll.c:178606-178615 -- BUG-FOR-BUG transcription:
                        // retail builds each channel byte with its two hex digits SWAPPED
                        // (second digit in the high nibble) before packing as a standard
                        // 0x00BBGGRR COLORREF. Not "corrected" to the more usual digit order --
                        // this is what the disassembly literally does.
                        unsigned int R2 = RdlxHexNibble(Value[2]);
                        unsigned int R1 = RdlxHexNibble(Value[1]);
                        unsigned int G2 = RdlxHexNibble(Value[4]);
                        unsigned int G1 = RdlxHexNibble(Value[3]);
                        unsigned int B2 = RdlxHexNibble(Value[6]);
                        unsigned int B1 = RdlxHexNibble(Value[5]);
                        unsigned int SwappedR = (R2 * 0x10 + R1) & 0xff;
                        unsigned int SwappedG = (G2 * 0x10 + G1) & 0xff;
                        unsigned int SwappedB = (B2 * 0x10 + B1) & 0xff;
                        ColorValue = SwappedR | (SwappedG << 8) | (SwappedB << 16);
                    }
                    else
                    {
                        FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                              "E:\\FL\\Scratch\\Source\\Common\\RichDisplayList.cpp", 0xd67,
                              "Invalid color name in RDLX file. Assuming white.\n");
                        ColorValue = 0xffffff;
                    }
                }
                else if (!bExplicitValue)
                {
                    ApplyDefaultBit(0xffffff00u);
                    bApply = false;
                }
                else if (!iswdigit(Value[0]))
                {
                    bool bFound = false;
                    for (int c = 0; c < 8; ++c)
                    {
                        if (wcsncmp(Value, GRdlxNamedColors[c].Name, ValueLen) == 0)
                        {
                            ColorValue = GRdlxNamedColors[c].Value;
                            bFound = true;
                            break;
                        }
                    }
                    if (!bFound)
                    {
                        FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                              "E:\\FL\\Scratch\\Source\\Common\\RichDisplayList.cpp", 0xd94,
                              "Invalid color name in RDLX file. Assuming white.\n");
                        ColorValue = 0xffffff;
                    }
                }
                else
                {
                    ColorValue = ParseRdlxNumber(Value, ValueLen) - 1;
                }

                if (bApply)
                {
                    AttrsIn = (AttrsIn & 0xffu) | (ColorValue << 8);
                    Mask |= 0xffffff00u;
                }
            }
            else if (wcsncmp(Name, L"font", NameLen) == 0)
            {
                if (bExplicitValue)
                {
                    // 0632b45b-0632b46d, byte arithmetic in AL/CL: byte0 = (N*8 - 1) ^ (~byte0 & 7).
                    // The font index lands in bits 3-7 (1-based, hence -1) and bits 0-2 keep
                    // bold/italic/underline; the result is a byte, so an index of 0 or above 32
                    // wraps inside bits 3-7 and never reaches the colour bits.
                    unsigned char const FontByte = (unsigned char)ParseRdlxNumber(Value, ValueLen);
                    unsigned char const OldByte0 = (unsigned char)(AttrsIn & 0xffu);
                    unsigned char const Byte0 =
                        (unsigned char)((unsigned char)(FontByte * 8 - 1) ^ (~OldByte0 & 7));
                    AttrsIn = (AttrsIn & ~0xffu) | Byte0;
                    Mask |= 0xf8u;
                }
                else
                {
                    ApplyDefaultBit(0xf8u);
                }
            }
            else if (wcsncmp(Name, L"bold", NameLen) == 0)
            {
                if (bExplicitValue)
                {
                    bool bSet = ParseRdlxBool(Value, ValueLen);
                    unsigned int Byte0 = AttrsIn & 0xffu;
                    Byte0 = bSet ? (Byte0 | 1u) : (Byte0 & ~1u);
                    AttrsIn = (AttrsIn & ~0xffu) | Byte0;
                    Mask |= 1u;
                }
                else
                {
                    ApplyDefaultBit(1u);
                }
            }
            else if (wcsncmp(Name, L"italic", NameLen) == 0)
            {
                if (bExplicitValue)
                {
                    bool bSet = ParseRdlxBool(Value, ValueLen);
                    unsigned int Byte0 = AttrsIn & 0xffu;
                    Byte0 = bSet ? (Byte0 | 2u) : (Byte0 & ~2u);
                    AttrsIn = (AttrsIn & ~0xffu) | Byte0;
                    Mask |= 2u;
                }
                else
                {
                    ApplyDefaultBit(2u);
                }
            }
            else if (wcsncmp(Name, L"underline", NameLen) == 0)
            {
                if (bExplicitValue)
                {
                    bool bSet = ParseRdlxBool(Value, ValueLen);
                    unsigned int Byte0 = AttrsIn & 0xffu;
                    Byte0 = bSet ? (Byte0 | 4u) : (Byte0 & ~4u);
                    AttrsIn = (AttrsIn & ~0xffu) | Byte0;
                    Mask |= 4u;
                }
                else
                {
                    ApplyDefaultBit(4u);
                }
            }
            // Any other attribute name: retail falls straight through to the next attribute,
            // no-op (Common.dll.c:178745-178771).
        }

        TextRenderAttributes RawAttrs;
        RawAttrs.Packed = AttrsIn;
        TRANode Temp(RawAttrs, Mask, DefBits); // ctor masks Packed by Mask internally
        RdlxAppendNode(*Rdl, Temp);
        return;
    }

    if (wcsncmp(TagName, L"TEXT", TagNameLen) == 0)
    {
        // Common.dll.txt:0632b6b2-0632b744. Builds one EMPTY TextNode the first time a TEXT tag
        // is seen for this RDL (guarded by bTextNodeOpen); the characters are appended by
        // ParseRdlxCharacters and the guard is cleared on the end tag by ParseRdlxEndElement.
        if (!Ctx.bTextNodeOpen)
        {
            TextNode Temp;
            RdlxAppendNode(*Rdl, Temp);
            Ctx.bTextNodeOpen = true;
        }
        return;
    }

    if (wcsncmp(TagName, L"PARA", TagNameLen) == 0)
    {
        // Common.dll.txt:0632b744-0632b79a.
        ParagraphNode Temp;
        RdlxAppendNode(*Rdl, Temp);
        return;
    }

    if (wcsncmp(TagName, L"POS", TagNameLen) == 0)
    {
        // Common.dll.txt:0632b7a3-0632bad5. "v"/"h" (plain base-10 ints) and "relV"/"relH"
        // (booleans) attributes.
        PositionNode Temp;
        // Retail keeps both as floats (FILD of the wcstol result, FSTP float).
        float PosH = 0.0f, PosV = 0.0f;
        int AttrCount = 0;
        Attributes->getLength(&AttrCount);
        for (int i = 0; i < AttrCount; ++i)
        {
            wchar_t const* Name = nullptr; int NameLen = 0;
            wchar_t const* Value = nullptr; int ValueLen = 0;
            Attributes->getLocalName(i, &Name, &NameLen);
            Attributes->getValue(i, &Value, &ValueLen);

            // "v"/"h": wcstol straight on the attribute pointer, no copy and no "0x" case
            // (0632b82b / 0632b85f), FILD + FSTP float.
            if (wcsncmp(Name, L"v", NameLen) == 0)
            {
                PosV = (float)wcstol(Value, nullptr, 10);
            }
            else if (wcsncmp(Name, L"h", NameLen) == 0)
            {
                PosH = (float)wcstol(Value, nullptr, 10);
            }
            else if (wcsncmp(Name, L"relV", NameLen) == 0)
            {
                Temp.set_rel_v(ParseRdlxBool(Value, ValueLen));
            }
            else if (wcsncmp(Name, L"relH", NameLen) == 0)
            {
                Temp.set_rel_h(ParseRdlxBool(Value, ValueLen));
            }
        }
        Temp.set_pos(PosH, PosV);
        RdlxAppendNode(*Rdl, Temp);
        return;
    }

    if (wcsncmp(TagName, L"STYLE", TagNameLen) == 0)
    {
        // Common.dll.txt:0632bb6f-0632bbb4. "id" attribute (hex/decimal); "name" is recognized
        // but retail discards the comparison result -- a vestigial no-op (Common.dll.c:179063
        // -179065), nothing to port for it.
        StyleNode Temp;
        unsigned short StyleId = 0;
        int AttrCount = 0;
        Attributes->getLength(&AttrCount);
        for (int i = 0; i < AttrCount; ++i)
        {
            wchar_t const* Name = nullptr; int NameLen = 0;
            wchar_t const* Value = nullptr; int ValueLen = 0;
            Attributes->getLocalName(i, &Name, &NameLen);
            Attributes->getValue(i, &Value, &ValueLen);
            if (wcsncmp(Name, L"id", NameLen) == 0)
            {
                StyleId = (unsigned short)ParseRdlxNumber(Value, ValueLen);
            }
        }
        Temp.set_style_id(StyleId);
        RdlxAppendNode(*Rdl, Temp);
        return;
    }

    if (wcsncmp(TagName, L"JUST", TagNameLen) == 0)
    {
        // Common.dll.txt:0632baf4-0632bbc2. "loc" attribute: 'C'/'c'=Center, 'R'/'r'=Right,
        // anything else (including absent)=Left. See the TextJustify enum comment in
        // common_text.h for how these numeric values were pinned down.
        TextJustify Mode = TextJustifyLeft;
        int AttrCount = 0;
        Attributes->getLength(&AttrCount);
        for (int i = 0; i < AttrCount; ++i)
        {
            wchar_t const* Name = nullptr; int NameLen = 0;
            wchar_t const* Value = nullptr; int ValueLen = 0;
            Attributes->getLocalName(i, &Name, &NameLen);
            Attributes->getValue(i, &Value, &ValueLen);
            if (wcsncmp(Name, L"loc", NameLen) == 0)
            {
                wchar_t C = Value[0];
                if (C == L'C' || C == L'c') Mode = TextJustifyCenter;
                else if (C == L'R' || C == L'r') Mode = TextJustifyRight;
                else Mode = TextJustifyLeft;
            }
        }
        JustifyNode Temp(Mode);
        RdlxAppendNode(*Rdl, Temp);
        return;
    }

    if (wcsncmp(TagName, L"PUSH", TagNameLen) == 0)
    {
        // Common.dll.txt:0632bbd3-0632bc32. Just another leaf node -- the actual attribute-stack
        // push happens later, at AttribStackNode::Execute() time.
        AttribStackNode Temp(AttribStackNode::Op_Push);
        RdlxAppendNode(*Rdl, Temp);
        return;
    }

    if (wcsncmp(TagName, L"POP", TagNameLen) == 0)
    {
        // Common.dll.txt:0632bc34-0632bc9f.
        AttribStackNode Temp(AttribStackNode::Op_Pop);
        RdlxAppendNode(*Rdl, Temp);
        return;
    }

    // Unrecognized tag name: retail falls through to the shared epilogue and returns
    // (Common.dll.txt:0632bcb2-0632bcba). No-op.
}

// ParseRdlxEndElement -- retail LAB_0632bdd0 (Common.dll.txt:364112-364172), sink slot 9
// (endElement). Only a LocalName match against "TEXT" has an observable effect (clears
// bTextNodeOpen).
// "RDL" and "TRA" are compared FIRST and return on a match (0632bdef / 0632bdff), which matters:
// wcsncmp over TagNameLen also matches prefixes, so a length of 0 or a name such as "T" stops
// at those probes and never clears the flag. The POS/PARA/STYLE probes come after TEXT and have
// no effect (STYLE's result is not even tested), so they are not reproduced.
void __cdecl ParseRdlxEndElement(RdlxParseContext& Ctx, wchar_t const* TagName, unsigned int TagNameLen)
{
    if (wcsncmp(TagName, L"RDL", TagNameLen) == 0)
        return;
    if (wcsncmp(TagName, L"TRA", TagNameLen) == 0)
        return;
    if (wcsncmp(TagName, L"TEXT", TagNameLen) == 0)
    {
        Ctx.bTextNodeOpen = false;
    }
}

// ParseRdlxCharacters -- retail LAB_0632be60 (Common.dll.txt:364184-364289), sink slot 10
// (characters).
void __cdecl ParseRdlxCharacters(RdlxParseContext& Ctx, wchar_t const* Text, unsigned int Len)
{
    // 0632be60-0632be7b: no-op unless the count (unsigned JBE) is non-zero AND bTextNodeOpen.
    // Ctx+0x0C is used without a test.
    RenderDisplayList* Rdl = Ctx.Rdl;
    if (Rdl->Count == 0 || !Ctx.bTextNodeOpen)
    {
        return;
    }

    // 0632be81-0632be87: sentinel -> tail entry (Sentinel[1]) -> its RDLNode* (TailEntry[2]),
    // none of them tested.
    RDLNode* LastNode = Rdl->NodeList->Prev->Node;

    // Common.dll.txt:364201-364203 -- vtable+0x20, RDLNode::operator TextNode*(); null means
    // the last node is not a TextNode.
    TextNode* AsText = LastNode->operator TextNode*();
    if (AsText == nullptr)
    {
        return;
    }

    // Common.dll.txt:364204-364281 -- retail inlines the wstring reserve/_Xlen/_Grow/copy
    // sequence directly here; TextNode::append_text (RVA 0xC8C60/0xC8D30) already implements
    // the exact same observable append, so call it rather than re-inlining MSVCP60 a second
    // time.
    AsText->append_text(Text, (int)Len);
}

// ============================================================================
// BinaryRDLReader / BinaryRDLWriter / XMLReader / XMLWriter
// The record format is documented on BinaryRDLReader in common_text.h.
// ============================================================================

namespace
{
    // Every binary RDL record starts with this 8-byte header; Length payload bytes follow.
    struct FRdlRecordHeader
    {
        int Type;               // +0x00  1 TRA, 2 text, 4 position, 5 paragraph, 6 style
        unsigned int Length;    // +0x04  payload bytes after the header
    };
    static_assert(sizeof(FRdlRecordHeader) == 8, "FRdlRecordHeader must be 8 bytes");

    // The fixed-size records (write_buffer 0632aa0e / 0632aa42 / 0632ab5d, read_buffer
    // 0632a5c5 / 0632a6eb / 0632a7d1). Type 2 (text) is a header plus Length bytes of
    // NUL-terminated UTF-16; type 5 (paragraph) is a bare header.
#pragma pack(push, 1)
    struct FRdlTraRecord        // type 1, Length 8
    {
        FRdlRecordHeader Header;
        unsigned long Attrs;    // +0x08  TRANode +0x04
        unsigned int Mask;      // +0x0c  TRANode +0x0c
    };
    struct FRdlPositionRecord   // type 4, Length 9
    {
        FRdlRecordHeader Header;
        unsigned char Flags;    // +0x08  bit 0 relH (+0x04), bit 1 relV (+0x05)
        float PosV;             // +0x09  PositionNode +0x08
        float PosH;             // +0x0d  PositionNode +0x0c
    };
    struct FRdlStyleRecord      // type 6, Length 2
    {
        FRdlRecordHeader Header;
        unsigned short StyleId; // +0x08  StyleNode +0x04
    };
#pragma pack(pop)
    static_assert(sizeof(FRdlTraRecord) == 0x10, "TRA record is 0x10 bytes");
    static_assert(sizeof(FRdlPositionRecord) == 0x11, "position record is 0x11 bytes");
    static_assert(sizeof(FRdlStyleRecord) == 0xa, "style record is 0xa bytes");
}

// 0x06325ee0 / 0x06325ef0: store the vtable, nothing else.
BinaryRDLReader::BinaryRDLReader() {}
BinaryRDLReader::BinaryRDLReader(const BinaryRDLReader&) {}

// 0x0632a560 ?read_buffer@BinaryRDLReader@@UAE_NAAVRenderDisplayList@@PBDI@Z (RET 0xC),
// Common.dll.txt 361469-361718. Traced instruction by instruction:
//   0632a578..0632a58f  Ok = true; Size == 0 -> return true.
//   0632a5a7..0632a5be  per record: Length ([cur+4]) == 0 -> skip to the advance; otherwise
//                       switch (Type - 1) unsigned, table 0632a884 = {1, 2, fail, 4, 5, 6}:
//                       Type 3, Type 0 and anything above 6 all land on 0632a843 (Ok = false).
//   case 1 0632a5c5     stack TRANode (vtable only), set_attributes(&payload[0], payload[4])
//                       (0x063286a0: +0x0C = mask, +0x04 = attrs & mask), then +0x10 = 0 at
//                       0632a5e8 (same slot as local_10); +0x08 is never written and never read
//                       (the port's TRANode() zeroes it).
//   case 2 0632a643     stack TextNode, TextNode::set_text(payload, -1) (wcslen).
//   case 4 0632a6eb     stack PositionNode: +0x04 = flags & 1, +0x05 = (flags >> 1) & 1,
//                       +0x08 = float payload[1], +0x0C = float payload[5] (FLD/FSTP pair).
//   case 5 0632a76c     stack ParagraphNode.
//   case 6 0632a7d1     stack StyleNode, +0x04 = word payload[0] (+0x06 not written; the
//                       port's StyleNode(unsigned short) zeroes it).
//   each case           RdlxAppendNode: node.Clone() -> FUN_0632c490(Rdl, end, &clone), which
//                       stores a second clone -> the first clone deleted (FUN_063272c0 /
//                       inlined at 0632a7aa / 0632a818).
//   0632a848..0632a860  cur += Length + 8; Size -= Length + 8; loop while Size != 0.
//   0632a866            return Ok.
bool BinaryRDLReader::read_buffer(RenderDisplayList& Rdl, const char* Buffer, unsigned int Size)
{
    bool bOk = true;
    if (Size == 0)
    {
        return bOk;
    }

    const unsigned char* Cur = (const unsigned char*)Buffer;
    do
    {
        const FRdlRecordHeader* const Header = (const FRdlRecordHeader*)Cur;
        const unsigned int Length = Header->Length;
        if (Length != 0)
        {
            const unsigned char* Payload = Cur + 8;
            switch (Header->Type)
            {
            case 1:
            {
                const FRdlTraRecord* const Tra = (const FRdlTraRecord*)Cur;
                TextRenderAttributes Attrs;
                Attrs.Packed = Tra->Attrs;
                TRANode Node;
                Node.set_attributes(Attrs, Tra->Mask);
                RdlxAppendNode(Rdl, Node);
                break;
            }
            case 2:
            {
                TextNode Node;
                Node.set_text((const wchar_t*)Payload, -1);
                RdlxAppendNode(Rdl, Node);
                break;
            }
            case 4:
            {
                const FRdlPositionRecord* const Pos = (const FRdlPositionRecord*)Cur;
                PositionNode Node;
                Node.bRelH = (Pos->Flags & 1) != 0;
                Node.bRelV = ((Pos->Flags >> 1) & 1) != 0;
                Node.PosV = Pos->PosV; // FLD / FSTP float, not a raw copy
                Node.PosH = Pos->PosH;
                RdlxAppendNode(Rdl, Node);
                break;
            }
            case 5:
            {
                ParagraphNode Node;
                RdlxAppendNode(Rdl, Node);
                break;
            }
            case 6:
            {
                StyleNode Node(((const FRdlStyleRecord*)Cur)->StyleId);
                RdlxAppendNode(Rdl, Node);
                break;
            }
            default:
                bOk = false;
                break;
            }
        }
        Cur += Length + 8;
        Size -= Length + 8;
    } while (Size != 0);

    return bOk;
}

// 0x0632c0c0 ?read_file@BinaryRDLReader@@UAE_NAAVRenderDisplayList@@PBD@Z: `XOR AL,AL; RET 8`.
bool BinaryRDLReader::read_file(RenderDisplayList&, const char*)
{
    return false;
}

// Common.dll.c/.txt 0xca8a0 -- fully ported. Concatenates the wide-char payload of
// every Type==2 (Text) record in the tagged-record stream documented on read_buffer
// above into Dest, truncating to MaxSize wide characters and always NUL-terminating.
// Types 1/3/4/5 contribute nothing. Everything else -- INCLUDING 6, the StyleNode record
// the writer emits -- clears the success flag, but parsing continues: the jump table at
// 0x0632a954 covers only Type-1 in 0..4 (`CMP EAX,4; JA` at 0x0632a8d0), so a styled
// message extracts its full text and still returns false, exactly as in retail. Retail's
// third parameter (`unsigned int&`) is part of the exported signature but the body never
// touches it (0xca8a0 reads only param_1/2/4/5).
bool BinaryRDLReader::extract_text_from_buffer(wchar_t* Dest, unsigned int MaxSize,
                                               unsigned int& /*OutSize*/,
                                               const char* Buffer, unsigned int Size)
{
    wchar_t* OutBuf = Dest;
    const unsigned char* Cur = (const unsigned char*)Buffer;
    unsigned int Written = 0;
    bool Ok = true;

    // Retail's early-out checks the INPUT buffer size (Size), not the output
    // capacity (MaxSize) -- an empty input just yields an empty output string.
    if (Size == 0)
    {
        OutBuf[0] = 0;
        return true;
    }

    while (Size != 0 && Written < MaxSize)
    {
        const FRdlRecordHeader* const Header = (const FRdlRecordHeader*)Cur;
        int Type = Header->Type;
        unsigned int Length = Header->Length;
        if (Length != 0)
        {
            if (Type == 2)
            {
                const wchar_t* Src = (const wchar_t*)(Cur + 8);
                size_t SrcLen = wcslen(Src);
                unsigned int Count = (unsigned int)(SrcLen + 1);
                if (MaxSize - Written < Count)
                {
                    Count = MaxSize - Written;
                }
                wcsncpy(OutBuf + Written, Src, Count);
                Written = Written + Count - 1;
            }
            else if (Type != 1 && Type != 3 && Type != 4 && Type != 5)
            {
                Ok = false;
            }
        }
        Cur += Length + 8;
        Size -= (Length + 8);
    }

    OutBuf[Written] = 0;
    return Ok;
}

// 0x06325f00 / 0x06325f10: store the vtable, nothing else.
BinaryRDLWriter::BinaryRDLWriter() {}
BinaryRDLWriter::BinaryRDLWriter(const BinaryRDLWriter&) {}

// 0x0632a970 ?write_buffer@BinaryRDLWriter@@UAE_NABVRenderDisplayList@@PADIAAI@Z (RET 0x10),
// Common.dll.txt 361832-362100, 242 instructions. Traced with ESP tracked through the four
// register pushes (args at +0x34 Rdl, +0x38 Dest, +0x3C MaxSize, +0x40 OutSize; EBX = bytes
// written, EBP = Dest + EBX; [ESP+0x10] = list cursor):
//   0632a97c..0632a99d  cursor = sentinel[0]; empty list -> *OutSize = 0, return true.
//   0632a9a4..0632a9f2  node = cursor[2]; SEVEN type queries, all made before any test and in
//                       this order: +0x28 TRANode, +0x30 PositionNode, +0x20 TextNode,
//                       +0x60 RDLRefNode, +0x38 ParagraphNode, +0x50 StyleNode, +0x68 TextPtrNode.
//   tests, first match wins (NullNode/JustifyNode/ClipNode/ImageNode/AttribStackNode match none
//   and are skipped without a record):
//   TRANode   0632a9fb  EBX+0x10 > MaxSize -> fail; {1, 8, [+4], [+0xC]}; EBX += 0x10.
//   Position  0632aa2f  EBX+0x11 > MaxSize -> fail; {4, 9}; byte = (+5 != 0 ? 2 : 0) |
//                       (+4 != 0); float +8 -> [9], float +0xC -> [0xD] (FLD/FSTP); EBX += 0x11.
//   TextPtr   0632aa7d  L = 2*[+8]+2; EBX+L+8 > MaxSize -> fail; {2, L}; copy 2*[+8] bytes from
//                       [+4]; word NUL at [8+L-2]; EBX += L+8.
//   TextNode  0632aacf  L = 2*len+2 (wstring _Len at +0xC); EBX+L+8 > MaxSize -> fail; {2, L};
//                       copy L bytes from _Ptr (+8; MSVCP60's empty-string rep when null), i.e.
//                       the text AND its NUL; EBX += L+8.
//   Paragraph 0632ab23  EBX+8 > MaxSize -> fail; {5, 0}; EBX += 8.
//   Style     0632ab46  EBX+0xA > MaxSize -> fail; {6, 2}; word [+4] -> [8]; EBX += 0xA.
//   RDLRef    0632ab87  sub = 0; this->vtable[0] (write_buffer itself, virtual) on *[+4] with
//                       (Dest+EBX, MaxSize-EBX, &sub); false -> *OutSize = sub + EBX, return
//                       false; else EBX += sub.
//   0632abb4..0632abc5  cursor = cursor[0]; loop until it equals Rdl's sentinel (re-read).
//   0632abcb            *OutSize = EBX, return true.
//   every "fail"        *OutSize = EBX (the records already written), return false.
// All size arithmetic is 32-bit unsigned with unsigned (JA) compares, reproduced as such.
bool BinaryRDLWriter::write_buffer(const RenderDisplayList& Rdl, char* Dest, unsigned int MaxSize,
                                   unsigned int& OutSize)
{
    FRdlListNode* Cur = Rdl.NodeList->Next;
    unsigned int Written = 0;
    if (Cur == Rdl.NodeList)
    {
        OutSize = Written;
        return true;
    }

    do
    {
        RDLNode* Node = Cur->Node;
        char* Rec = Dest + Written;
        FRdlRecordHeader* const Header = (FRdlRecordHeader*)Rec;

        TRANode* AsTra = Node->operator TRANode*();
        PositionNode* AsPos = Node->operator PositionNode*();
        TextNode* AsText = Node->operator TextNode*();
        RDLRefNode* AsRef = Node->operator RDLRefNode*();
        ParagraphNode* AsPara = Node->operator ParagraphNode*();
        StyleNode* AsStyle = Node->operator StyleNode*();
        TextPtrNode* AsTextPtr = Node->operator TextPtrNode*();

        if (AsTra != nullptr)
        {
            if (Written + sizeof(FRdlTraRecord) > MaxSize)
            {
                OutSize = Written;
                return false;
            }
            FRdlTraRecord* const Tra = (FRdlTraRecord*)Rec;
            Tra->Header.Type = 1;
            Tra->Header.Length = 8;
            Tra->Attrs = AsTra->Attrs.Packed;
            Tra->Mask = AsTra->Mask;
            Written += sizeof(FRdlTraRecord);
        }
        else if (AsPos != nullptr)
        {
            if (Written + sizeof(FRdlPositionRecord) > MaxSize)
            {
                OutSize = Written;
                return false;
            }
            FRdlPositionRecord* const Pos = (FRdlPositionRecord*)Rec;
            Pos->Header.Type = 4;
            Pos->Header.Length = 9;
            Pos->Flags = (AsPos->bRelV ? 2 : 0) | (AsPos->bRelH ? 1 : 0);
            Pos->PosV = AsPos->PosV; // FLD / FSTP float
            Pos->PosH = AsPos->PosH;
            Written += sizeof(FRdlPositionRecord);
        }
        else if (AsTextPtr != nullptr)
        {
            const unsigned int RecLen = AsTextPtr->TextLen * 2 + 2;
            if (RecLen + Written + 8 > MaxSize)
            {
                OutSize = Written;
                return false;
            }
            Header->Length = RecLen;
            Header->Type = 2;
            memcpy(Rec + 8, AsTextPtr->TextPtr, RecLen - 2);
            *(wchar_t*)(Rec + 8 + RecLen - 2) = 0;
            Written += RecLen + 8;
        }
        else if (AsText != nullptr)
        {
            const unsigned int RecLen = (unsigned int)AsText->Text.size() * 2 + 2;
            if (RecLen + Written + 8 > MaxSize)
            {
                OutSize = Written;
                return false;
            }
            Header->Type = 2;
            Header->Length = RecLen;
            // The string's terminator is copied too (RecLen includes it).
            const wchar_t* Src = AsText->Text.c_str();
            memcpy(Rec + 8, Src, RecLen);
            Written += RecLen + 8;
        }
        else if (AsPara != nullptr)
        {
            if (Written + 8 > MaxSize)
            {
                OutSize = Written;
                return false;
            }
            Header->Type = 5;
            Header->Length = 0;
            Written += 8;
        }
        else if (AsStyle != nullptr)
        {
            if (Written + sizeof(FRdlStyleRecord) > MaxSize)
            {
                OutSize = Written;
                return false;
            }
            FRdlStyleRecord* const StyleRec = (FRdlStyleRecord*)Rec;
            StyleRec->Header.Length = 2;
            StyleRec->Header.Type = 6;
            StyleRec->StyleId = AsStyle->get_style_id();
            Written += sizeof(FRdlStyleRecord);
        }
        else if (AsRef != nullptr)
        {
            unsigned int SubSize = 0;
            if (!write_buffer(AsRef->List, Rec, MaxSize - Written, SubSize))
            {
                OutSize = SubSize + Written;
                return false;
            }
            Written += SubSize;
        }

        Cur = Cur->Next;
    } while (Cur != Rdl.NodeList);

    OutSize = Written;
    return true;
}

// 0x0632c0c0 ?write_file@BinaryRDLWriter@@UAE_NABVRenderDisplayList@@PBD@Z: `XOR AL,AL; RET 8`.
bool BinaryRDLWriter::write_file(const RenderDisplayList&, const char*)
{
    return false;
}

// Common.dll.c 0xc5ea0
XMLReader::XMLReader() {}
XMLReader::XMLReader(const XMLReader&) {}

// ============================================================================
// XMLReader::read_buffer / read_file (0632ac30 / 0632bf80):
//  - retail CoCreateInstance()s Microsoft's real MSXML2 SAXXMLReader COM object, not a DOM
//    document -- the CLSID/IID pair at DAT_063ed96c/DAT_063ed97c (Common.dll.txt:639857-639891)
//    decodes byte-for-byte to the well-known public constants CLSID_SAXXMLReader
//    {079AA557-4A18-424A-8EEE-E39F0A8D41B9} and IID_ISAXXMLReader
//    {A4F96ED0-F829-476E-81C0-CDC7BD2A0802};
//  - every one of the 16 slots in the sink vtable at .rdata:063a4644 has a RET-immediate byte
//    count matching Microsoft's real ISAXContentHandler vtable slot-for-slot, and the 3
//    attribute-list slots ParseRdlxTag calls (0x0C/0x14/0x34) are ISAXAttributes'
//    getLength/getLocalName/getValue -- the list is the real ISAXAttributes* MSXML hands to
//    startElement;
//  - the sink object retail builds on its own stack (Common.dll.txt:362134-362140 for
//    read_buffer, 364344-364348 for read_file) is functionally the very same RdlxParseContext
//    ParseRdlxTag/ParseRdlxEndElement/ParseRdlxCharacters already take as their first parameter
//    (vtable ptr at +0, the reader at +4, the skip byte at +8, Rdl at +0xC and bTextNodeOpen at
//    +0x10 -- RdlxParseContext's layout).
//
// Precondition (matches retail exactly): the calling thread must already have COM initialized
// (CoInitialize/CoInitializeEx). Neither function calls it itself in the disassembly either --
// some earlier engine startup path is expected to have done so already; if it hasn't,
// CoCreateInstance fails with CO_E_NOTINITIALIZED and both functions correctly return false,
// same as retail would.
// ============================================================================

namespace
{
    // Concrete sink -- the C++ equivalent of retail's stack-built {vtable, 0, 0, Rdl*,
    // bTextNodeOpen} struct. Constructed on XMLReader::read_buffer/read_file's own stack and
    // handed to ISAXXMLReader::putContentHandler; its lifetime is bounded by the enclosing
    // parse()/parseURL() call, exactly like retail's. Only startElement/endElement/characters
    // (call-table indices 8/9/10 -- ParseRdlxTag/ParseRdlxEndElement/ParseRdlxCharacters) have
    // any effect; every other slot is a confirmed no-op in retail (see item 73110's notes) and
    // is implemented as a trivial S_OK stub here.
    // Retail's base layer of the sink, RTTI .?AVContentHandler@@, vftable .rdata:063a4684 (15 slots):
    // every interface slot is an ICF-folded "return 0" stub sized to its argument count --
    // QueryInterface LAB_0632af80 (`XOR EAX,EAX; RET 0xc`: S_OK WITHOUT writing *ppv), AddRef /
    // Release / startDocument / endDocument LAB_0632b010 (RET 4), putDocumentLocator 0632ed40
    // (RET 8), startPrefixMapping / processingInstruction LAB_0632af70 (RET 0x14),
    // endPrefixMapping / characters / ignorableWhitespace / skippedEntity LAB_0632af80 (RET 0xc),
    // startElement LAB_0632af50 (RET 0x20), endElement LAB_0632af60 (RET 0x1c) -- and slot 14 is
    // its scalar deleting destructor LAB_0632af90, whose body (FUN_0632af40, also run by the
    // read_buffer / read_file unwind funclets) only re-stamps this vftable. No reference count.
    class ContentHandler : public ISAXContentHandlerRdlx
    {
    public:
        virtual ~ContentHandler() {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return 0; }
        ULONG STDMETHODCALLTYPE AddRef() override { return 0; }
        ULONG STDMETHODCALLTYPE Release() override { return 0; }
        HRESULT STDMETHODCALLTYPE putDocumentLocator(IUnknown*) override { return 0; }
        HRESULT STDMETHODCALLTYPE startDocument() override { return 0; }
        HRESULT STDMETHODCALLTYPE endDocument() override { return 0; }
        HRESULT STDMETHODCALLTYPE startPrefixMapping(const wchar_t*, int, const wchar_t*, int) override { return 0; }
        HRESULT STDMETHODCALLTYPE endPrefixMapping(const wchar_t*, int) override { return 0; }
        HRESULT STDMETHODCALLTYPE startElement(const wchar_t*, int, const wchar_t*, int, const wchar_t*, int,
                                               ISAXAttributesRdlx*) override { return 0; }
        HRESULT STDMETHODCALLTYPE endElement(const wchar_t*, int, const wchar_t*, int, const wchar_t*, int) override { return 0; }
        HRESULT STDMETHODCALLTYPE characters(const wchar_t*, int) override { return 0; }
        HRESULT STDMETHODCALLTYPE ignorableWhitespace(const wchar_t*, int) override { return 0; }
        HRESULT STDMETHODCALLTYPE processingInstruction(const wchar_t*, int, const wchar_t*, int) override { return 0; }
        HRESULT STDMETHODCALLTYPE skippedEntity(const wchar_t*, int) override { return 0; }
    };

    // The sink itself, RTTI .?AVRDLXParser@@ (base ContentHandler), vftable .rdata:063a4644: the
    // base layer with startElement / endElement /
    // characters (slots 8/9/10) and the destructor (slot 14, FUN_0632bf60 -> FUN_0632afb0)
    // overridden; every other slot is the same stub as the base.
    class RDLXParser : public ContentHandler
    {
    public:
        // Retail's field order, read straight off the stack-struct initialisation in
        // XMLReader::read_buffer (.text:0632ac5a-0632ac6a, with the object's base pinned by the
        // `LEA EDX,[ESP+0x24]` handed to putContentHandler at .text:0632ac98):
        //   +0x00 vptr (.rdata:063a4644)      .text:0632ac62
        //   +0x04 ISAXXMLReader*  == 0        .text:0632ac5a
        //   +0x08 byte            == 0        .text:0632ac66
        //   +0x0c RenderDisplayList*          .text:0632ac5e  (read_buffer's param_1)
        //   +0x10 byte            == 0        .text:0632ac6a  (bTextNodeOpen)
        // Ctx (RdlxParseContext) holds +0x04..+0x10 right behind the vptr. It is public because
        // read_file/read_buffer pass &Ctx.Reader straight to CoCreateInstance, exactly as
        // retail does.
        RdlxParseContext Ctx;

        explicit RDLXParser(RenderDisplayList& Rdl)
        {
            Ctx.Reader = nullptr;
            Ctx.bSkipParse = 0;
            Ctx.Rdl = &Rdl;
            Ctx.bTextNodeOpen = false;
        }

        // Slot 14 of .rdata:063a4644 -- the scalar deleting destructor FUN_0632bf60
        // (.text:0632bf60), whose body is FUN_0632afb0. The virtual destructor is declared by
        // the base layer after the 14 interface slots (3 IUnknown + 11 ISAXContentHandler), so
        // it is the 15th entry, matching retail's width.
        // FUN_0632afb0 body, .text:0632afb0-0632affa: set the vptr to the derived table
        // (.text:0632afcd), then `if (this+0x04 != 0) { this+0x04->Release(); this+0x04 = 0; }`
        // (.text:0632afd3-0632afe8), then revert the vptr to the base layer .rdata:063a4684
        // (.text:0632aff3). The two Release call sites inlined into read_buffer's own epilogues
        // (.text:0632adc7 and .text:0632adf8, both `MOV EDX,[EAX]; PUSH EAX; CALL [EDX+8]` on
        // [ESP+0x28] == sink+0x04) are this same destructor inlined, which is why retail never
        // releases the reader through a separate local.
        virtual ~RDLXParser()
        {
            if (Ctx.Reader != nullptr)
            {
                Ctx.Reader->Release();
                Ctx.Reader = nullptr;
            }
        }

        // IUnknown and the other no-op slots come from the base layer: retail's sink has no
        // reference count (AddRef/Release return 0, QueryInterface returns 0 without writing
        // *ppv); it lives on the stack of read_buffer / read_file.
        //
        // NOTE 2026-09-17 on +0x04. The previous revision left an open question here: retail's
        // sink owns a refcounted COM interface at +0x04 that the constructor never writes, and
        // the port had no member for it. ANSWER: it is written by CoCreateInstance itself.
        // XMLReader::read_buffer zero-initialises the whole sink struct on its stack first
        // (.text:0632ac5a-0632ac6a), then at .text:0632ac6e does `LEA EDX,[ESP+0x28]` -- the
        // sink's +0x04 -- and passes that as CoCreateInstance's ppv out-parameter
        // (.text:0632ac84). So the sink takes ownership of the very reference CoCreateInstance
        // creates; nothing AddRefs it a second time. The same slot is then read back at
        // .text:0632ac92 to make the putContentHandler call (vtable+0x28, .text:0632ac9e) and at
        // .text:0632adb7/0632ade8 to release it on the two exit paths. XMLReader::read_file
        // (0x0632bf80) does the same. It was invisible to a MOV-write grep for the usual reason:
        // interface fields are filled through an out-parameter address, not by a store.

        // The 3 slots with real behavior -- call table indices 8/9/10, ported as
        // ParseRdlxTag/ParseRdlxEndElement/ParseRdlxCharacters (items 2416/2419/2420); all
        // three return 0 (XOR EAX,EAX before each RET).
        HRESULT STDMETHODCALLTYPE startElement(const wchar_t*, int, const wchar_t* pwchLocalName,
                                               int cchLocalName, const wchar_t*, int,
                                               ISAXAttributesRdlx* pAttributes) override
        {
            ParseRdlxTag(Ctx, pwchLocalName, (unsigned int)cchLocalName, pAttributes);
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE endElement(const wchar_t*, int, const wchar_t* pwchLocalName,
                                             int cchLocalName, const wchar_t*, int) override
        {
            ParseRdlxEndElement(Ctx, pwchLocalName, (unsigned int)cchLocalName);
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE characters(const wchar_t* pwchChars, int cchChars) override
        {
            ParseRdlxCharacters(Ctx, pwchChars, (unsigned int)cchChars);
            return S_OK;
        }
    };
}

// Common.dll.c/.txt 0xcbf80 (Common.dll.txt:364321-364417). Ported 2026-09-09 (D109). Retail
// flow, traced instruction-by-instruction:
//   1. CoCreateInstance(CLSID_SAXXMLReader, NULL, 0x7 [CLSCTX_INPROC_SERVER|
//      CLSCTX_INPROC_HANDLER|CLSCTX_LOCAL_SERVER], IID_ISAXXMLReader, &reader)
//      (Common.dll.txt:364356) -- on failure, release (no-op, reader still null) and return false.
//   2. reader->putContentHandler(&sink) via vtable+0x28 (364364) -- on non-S_OK, release+return
//      false.
//   3. CMP byte ptr[ESP+0x10],0 / JZ (364369-364370): this tests the sink struct's zeroed byte at
//      relative +0x8 (retail's Unknown00, never written again after the zero-init above) -- always
//      0, so this branch is provably dead and always falls through to step 4 (the alternate path
//      would just skip straight to the epilogue with a fixed false result, unreachable here).
//   4. mbstowcs the ANSI filename into a wide buffer (364375-364379), then
//      reader->parseURL(wideFilename) via vtable+0x50 (364386).
//   5. Release the reader (364395-364397 / 364407-364409 on either path); return
//      (parseURL result == 0), matching the SETZ-on-EAX==0 at 364388.
bool XMLReader::read_file(RenderDisplayList& Rdl, const char* Filename)
{
    // CORRECTED 2026-09-17: the sink is constructed FIRST and the reader interface is created
    // straight into the sink's own +0x04 field, which is what retail does (see the field and
    // destructor comments on RDLXParser). The sink's destructor is the only thing
    // that releases it -- retail has no separate local holding the reference, and its two
    // explicit Release sites are that destructor inlined.
    RDLXParser Handler(Rdl);
    HRESULT Hr = CoCreateInstance(CLSID_SAXXMLReader_Rdlx, nullptr,
                                  CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER,
                                  IID_ISAXXMLReader_Rdlx, (void**)&Handler.Ctx.Reader);
    // `CMP EAX,EBX; JL` (.text:0632bfdc) -- only a negative HRESULT fails; the reader is then
    // used without a null check.
    if (Hr < 0)
    {
        return false;
    }

    // putContentHandler (vtbl+0x28): `CMP EAX,EBX; SETZ` -- anything but exactly S_OK fails.
    if (Handler.Ctx.Reader->putContentHandler(&Handler) != S_OK)
    {
        return false;
    }

    // .text:0632bff8-0632c002: the sink's +0x08 byte (zeroed at .text:0632bfb5, never written
    // again) set -> result false without parsing.
    if (Handler.Ctx.bSkipParse != 0)
    {
        return false;
    }

    // .text:0632c004-0632c016: mbstowcs into a 1000-wchar stack buffer with a 999 cap. Retail
    // does not terminate a name that fills the cap (UB in retail); the port terminates the last
    // slot to stay deterministic.
    wchar_t WideFilename[1000];
    mbstowcs(WideFilename, Filename, 0x3e7);
    WideFilename[999] = L'\0';

    // parseURL (vtbl+0x50); `CMP EAX,EBX; SETZ` -- the result is exactly S_OK.
    return Handler.Ctx.Reader->parseURL(WideFilename) == S_OK;
}

// Common.dll.txt 0xcc0a0 / item 73130: retail default is `XOR AL,AL; RET 0x14` — always false.
// Same five parameters as BinaryRDLReader's (retail mangling ..._NPAGIAAIPBDI@Z).
bool XMLReader::extract_text_from_buffer(wchar_t* /*Dest*/, unsigned int /*MaxSize*/,
                                         unsigned int& /*OutSize*/,
                                         const char* /*Buffer*/, unsigned int /*Size*/)
{
    return false;
}

bool XMLReader::read_buffer(RenderDisplayList& Rdl, const char* Buffer, unsigned int Size)
{
    // .text:0632ac30-0632ae10. Same sink / reader ownership as read_file: the sink is zeroed
    // on the stack (.text:0632ac5a-0632ac6a), the reader is created into its +0x04 and the
    // sink's destructor releases it (.text:0632adb7 / 0632ade8).
    RDLXParser Handler(Rdl);
    HRESULT Hr = CoCreateInstance(CLSID_SAXXMLReader_Rdlx, nullptr,
                                  CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER,
                                  IID_ISAXXMLReader_Rdlx, (void**)&Handler.Ctx.Reader);
    // `TEST EAX,EAX; JL` -- only a negative HRESULT fails; no null check on the reader.
    if (Hr < 0)
    {
        return false;
    }

    // putContentHandler (vtbl+0x28): `TEST EAX,EAX; SETZ` -- anything but exactly S_OK fails.
    if (Handler.Ctx.Reader->putContentHandler(&Handler) != S_OK)
    {
        return false;
    }

    // .text:0632acae-0632acbb: the sink's +0x08 byte (zeroed at .text:0632ac66, never written
    // again) set -> result false without parsing.
    if (Handler.Ctx.bSkipParse != 0)
    {
        return false;
    }

    // .text:0632acc0-0632ad0e: `_bstr_t Raw(SysAllocStringByteLen(Buffer, Size), false)` --
    // operator new(0xc) + FUN_0632ae70 (_bstr_t::Data_t(BSTR, false)); _com_issue_error
    // (FUN_06392140) with E_OUTOFMEMORY when the holder is not allocated.
    _bstr_t Raw(SysAllocStringByteLen(Buffer, Size), false);

    // .text:0632ad0e-0632ad45: VC6's inline _variant_t(const _bstr_t&): vt = VT_BSTR, bstrVal = a
    // byte-for-byte copy of Raw's BSTR, _com_issue_error(E_OUTOFMEMORY) if the copy is null.
    // Retail writes only vt and bstrVal; the reserved words are zeroed here rather than left as
    // stack contents.
    VARIANT Input;
    memset(&Input, 0, sizeof(Input));
    Input.vt = VT_BSTR;
    wchar_t* RawChars = (wchar_t*)Raw;
    Input.bstrVal = SysAllocStringByteLen((const char*)RawChars, SysStringByteLen(RawChars));
    if (Input.bstrVal == nullptr)
    {
        _com_issue_error(E_OUTOFMEMORY);
    }

    // parse (vtbl+0x4c) takes the VARIANT by value (.text:0632ad53-0632ad75).
    HRESULT ParseHr = Handler.Ctx.Reader->parse(Input);

    // VC6's ~_variant_t: VariantClear, _com_issue_error on a negative result (.text:0632ad7a-0632ad8f).
    HRESULT ClearHr = VariantClear(&Input);
    if (ClearHr < 0)
    {
        _com_issue_error(ClearHr);
    }

    // `TEST EDI,EDI; SETZ` -- exactly S_OK. Raw is released next (FUN_0632aed0), then the sink.
    return ParseHr == S_OK;
}

// Common.dll.c 0xc5ec0
XMLWriter::XMLWriter() {}
XMLWriter::XMLWriter(const XMLWriter&) {}

// Common.dll.c/.txt 0xcc0b0: concrete default -- always fails.
bool XMLWriter::write_buffer(const RenderDisplayList&, char*, unsigned int, unsigned int&)
{
    return false;
}

// .text:0632c0c0: XOR AL,AL; RET 8 -- always fails (ICF stub, also BinaryRDLWriter::write_file).
bool XMLWriter::write_file(const RenderDisplayList&, const char*)
{
    return false;
}

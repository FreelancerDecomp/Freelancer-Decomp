// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MoviePlay.h"
#include "MediaPlayer.h"
#include "MovieGraph.h"

#include <dshow.h>
#include <servprov.h>
#include <wmsecure.h>
#include <olectl.h>
#include <new>
#include <stdlib.h>

// Bytes read from EXE\movie.dll (image base 0x06ac0000).

// CLSID_FilterGraph {E436EBB3-524F-11CE-9F53-0020AF0BA770} DAT_06adb22c
static const GUID GClsidFilterGraph =
	{0xE436EBB3, 0x524F, 0x11CE, {0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}};

// CLSID_WMAsfReader {187463A0-5BB7-11D3-ACBE-0080C75E246E} DAT_06adb21c
static const GUID GClsidWmAsfReader =
	{0x187463A0, 0x5BB7, 0x11D3, {0xAC, 0xBE, 0x00, 0x80, 0xC7, 0x5E, 0x24, 0x6E}};

// IID_IGraphBuilder {56A868A9-0AD4-11CE-B03A-0020AF0BA770} DAT_06adb1ac
static const GUID GIidGraphBuilder =
	{0x56A868A9, 0x0AD4, 0x11CE, {0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}};

// IID_IBaseFilter {56A86895-0AD4-11CE-B03A-0020AF0BA770} DAT_06adb1cc
static const GUID GIidBaseFilter =
	{0x56A86895, 0x0AD4, 0x11CE, {0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}};

// IID_IFileSourceFilter {56A868A6-0AD4-11CE-B03A-0020AF0BA770} DAT_06adb1bc
static const GUID GIidFileSourceFilter =
	{0x56A868A6, 0x0AD4, 0x11CE, {0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}};

// IID_IMediaControl {56A868B1-0AD4-11CE-B03A-0020AF0BA770} DAT_06adb20c
static const GUID GIidMediaControl =
	{0x56A868B1, 0x0AD4, 0x11CE, {0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}};

// IID_IVideoWindow {56A868B4-0AD4-11CE-B03A-0020AF0BA770} DAT_06adb1ec
static const GUID GIidVideoWindow =
	{0x56A868B4, 0x0AD4, 0x11CE, {0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}};

// IID_IBasicVideo {56A868B5-0AD4-11CE-B03A-0020AF0BA770} DAT_06adb1dc
static const GUID GIidBasicVideo =
	{0x56A868B5, 0x0AD4, 0x11CE, {0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}};

// IID_IMediaEventEx {56A868C0-0AD4-11CE-B03A-0020AF0BA770} DAT_06adb1fc
static const GUID GIidMediaEventEx =
	{0x56A868C0, 0x0AD4, 0x11CE, {0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}};

// IID_IObjectWithSite {FC4801A3-2BA9-11CF-A229-00AA003D7352} DAT_06ada970
static const GUID GIidObjectWithSite =
	{0xFC4801A3, 0x2BA9, 0x11CF, {0xA2, 0x29, 0x00, 0xAA, 0x00, 0x3D, 0x73, 0x52}};

// IID_IUnknown {00000000-0000-0000-C000-000000000046} DAT_06ada960
static const GUID GIidUnknown =
	{0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// IID_IServiceProvider {6D5140C1-7436-11CE-8034-00AA006009FA} DAT_06ada980
static const GUID GIidServiceProvider =
	{0x6D5140C1, 0x7436, 0x11CE, {0x80, 0x34, 0x00, 0xAA, 0x00, 0x60, 0x09, 0xFA}};

// IID_IWMReader {96406BD6-2B2B-11D3-B36B-00C04F6108FF} DAT_06add108
static const GUID GIidWmReader =
	{0x96406BD6, 0x2B2B, 0x11D3, {0xB3, 0x6B, 0x00, 0xC0, 0x4F, 0x61, 0x08, 0xFF}};

// IID_IWMAuthorizer {D9B67D36-A9AD-4EB4-BAEF-DB284EF5504C} DAT_06ada760
static const GUID GIidWmAuthorizer =
	{0xD9B67D36, 0xA9AD, 0x4EB4, {0xBA, 0xEF, 0xDB, 0x28, 0x4E, 0xF5, 0x50, 0x4C}};

// DAT_06add1d0. Four certificate records follow at DAT_06add1d8, stride 0x78.
// DAT_06add23c is DAT_06add1d8 + 0x64.
static int GAuthorizerCertCount = 4;

static const unsigned char GAuthorizerCerts[4 * 0x78] =
{
	0x00, 0x01, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x4D, 0x9F, 0x93, 0x85, 0x06, 0x73, 0x5B, 0xAF,
	0xD5, 0xA2, 0xD8, 0x17, 0x7C, 0xC8, 0x62, 0x7B, 0x00, 0xD5, 0xFB, 0x36, 0x03, 0x6D, 0x93, 0x1C,
	0xD4, 0x9A, 0x0C, 0x4A, 0x0A, 0xC7, 0xC2, 0xA3, 0x94, 0xDD, 0xF0, 0x37, 0xF9, 0x04, 0xAF, 0x6F,
	0xE4, 0x91, 0x06, 0x5A, 0x1F, 0x65, 0xA7, 0xE8, 0x60, 0x19, 0x1E, 0xCB, 0x69, 0x86, 0x94, 0xC6,
	0xCE, 0x46, 0x8D, 0x25, 0x74, 0xB8, 0x60, 0x81, 0x0A, 0x40, 0xFC, 0x94, 0x14, 0xE4, 0xFF, 0xCB,
	0x63, 0x65, 0xDE, 0xA3, 0x17, 0x9F, 0x24, 0x0F, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96,
	0x00, 0x00, 0x00, 0xFE, 0xE8, 0xD9, 0xC5, 0x50, 0x3F, 0xCB, 0x2F, 0xB2, 0x35, 0x62, 0x08, 0xEF,
	0x45, 0x38, 0x79, 0x29, 0xE8, 0x17, 0x97, 0x3E, 0x00, 0x01, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00,
	0x73, 0x75, 0x8A, 0xA9, 0x3C, 0x44, 0x86, 0x8E, 0x52, 0x2A, 0x6B, 0x14, 0x60, 0x98, 0x2B, 0x8F,
	0xB3, 0xB8, 0xC6, 0x7D, 0x17, 0x38, 0x93, 0x22, 0xBB, 0xA4, 0xC5, 0x4A, 0xE2, 0x04, 0x44, 0x9E,
	0x30, 0x9E, 0x95, 0xCC, 0x9C, 0x77, 0x6C, 0x1F, 0xD9, 0x28, 0xD4, 0x2D, 0x35, 0x6C, 0x0D, 0xA5,
	0x09, 0x74, 0x76, 0xC9, 0x16, 0xD0, 0x6B, 0xF6, 0x03, 0xB0, 0xAE, 0x32, 0xC9, 0x93, 0xAC, 0x06,
	0x8F, 0x13, 0xDD, 0x29, 0xA0, 0x6B, 0x64, 0x74, 0xE5, 0x55, 0xC4, 0xEF, 0x4C, 0x42, 0x9A, 0x60,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0x00, 0x00, 0x00, 0xFE, 0x4D, 0xF8, 0xC1, 0x3E,
	0x9D, 0xAB, 0x3A, 0x8B, 0x66, 0xB1, 0xF8, 0xB7, 0x75, 0x20, 0xC7, 0xF0, 0xDB, 0xA0, 0xFE, 0x1C,
	0x00, 0x01, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x39, 0x14, 0x0E, 0xD8, 0x4B, 0x50, 0x79, 0xE4,
	0xA0, 0xEE, 0x58, 0xE2, 0x6B, 0xDD, 0x50, 0x10, 0x8E, 0xF6, 0xB8, 0x23, 0xD7, 0xB3, 0xB3, 0xCE,
	0xEB, 0xA8, 0x16, 0x20, 0xDD, 0xA4, 0x13, 0x12, 0xBB, 0x15, 0x2E, 0xBA, 0x8A, 0x56, 0xA3, 0x57,
	0x36, 0xE9, 0xE1, 0xA1, 0x85, 0x75, 0xFC, 0xD3, 0x57, 0xC2, 0xD8, 0xBF, 0x60, 0xD5, 0x8B, 0x69,
	0x05, 0x0D, 0x47, 0x83, 0x2E, 0x53, 0xD2, 0x6E, 0x09, 0x70, 0xA1, 0x68, 0x85, 0xBE, 0x6F, 0x3B,
	0x08, 0x14, 0x28, 0x76, 0x3B, 0xFF, 0x00, 0x54, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96,
	0x00, 0x00, 0x00, 0xFE, 0x6E, 0x80, 0x42, 0xF5, 0x8E, 0x4E, 0x7E, 0x65, 0xA6, 0x8D, 0x3D, 0x56,
	0x4C, 0xF4, 0xFC, 0xDB, 0xC6, 0x34, 0x67, 0x67, 0x00, 0x01, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00,
	0x67, 0xD3, 0xAB, 0xC2, 0x50, 0xCF, 0xA2, 0xC5, 0x42, 0x34, 0x94, 0xD7, 0x13, 0x66, 0x83, 0xFB,
	0x67, 0xF6, 0x1A, 0x5B, 0x84, 0x9B, 0xFD, 0x44, 0x1B, 0xF2, 0xEA, 0x2C, 0x5B, 0x47, 0xFB, 0xB6,
	0xD8, 0xCE, 0x39, 0x35, 0x5F, 0x98, 0x12, 0x41, 0x95, 0x48, 0x4B, 0xFB, 0x5B, 0xA2, 0xF9, 0x28,
	0x32, 0x8A, 0x1B, 0xE4, 0x42, 0x3C, 0xCB, 0x47, 0x90, 0xAA, 0xEE, 0x13, 0xF2, 0x19, 0x8C, 0xAF,
	0xC8, 0x96, 0xFC, 0x11, 0xC3, 0x76, 0x98, 0x83, 0x34, 0x89, 0x9A, 0x10, 0x99, 0xED, 0xDA, 0x6F,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0x00, 0x00, 0x00, 0xFE, 0x76, 0x02, 0x9D, 0x7E,
	0xAC, 0x5E, 0x06, 0xA6, 0x6F, 0xA0, 0x4A, 0x64, 0x26, 0xAF, 0xA4, 0x93, 0xB1, 0xD9, 0xFC, 0x46
};

// __FILE__ baked into FUN_06ac1e50 at 06ada718.
static const char GMoviePlayerSourceFile[] = "E:\\FL\\Scratch\\Source\\Movie\\MoviePlayer.cpp";

// IServiceProvider is IUnknown plus QueryService. IWMAuthorizer is IUnknown plus the three cert slots.
// Both are stdcall: Quartz PUSHes the object and the method RETs 4/8/0xC/0x10.
struct FServiceProvider : IServiceProvider
{
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Iid, void** Out) override;
	ULONG STDMETHODCALLTYPE AddRef() override;
	ULONG STDMETHODCALLTYPE Release() override;
	HRESULT STDMETHODCALLTYPE QueryService(REFGUID Service, REFIID Iid, void** Out) override;

	FServiceProvider();

	long RefCount;
};

struct FAuthorizer : IWMAuthorizer
{
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Iid, void** Out) override;
	ULONG STDMETHODCALLTYPE AddRef() override;
	ULONG STDMETHODCALLTYPE Release() override;
	HRESULT STDMETHODCALLTYPE GetCertCount(DWORD* Count) override;
	HRESULT STDMETHODCALLTYPE GetCert(DWORD Index, BYTE** CertOut) override;
	HRESULT STDMETHODCALLTYPE GetSharedData(DWORD CertIndex, const BYTE* SharedData, BYTE* Cert, BYTE** OutShared) override;

	void ClearInner();

	FAuthorizer();

	long RefCount;
	void* Inner;
};

static_assert(sizeof(FServiceProvider) == 8, "operator new(8)");
static_assert(offsetof(FServiceProvider, RefCount) == 4, "refcount");
static_assert(sizeof(FAuthorizer) == 0xC, "operator new(0xC)");
static_assert(offsetof(FAuthorizer, RefCount) == 4, "refcount");
static_assert(offsetof(FAuthorizer, Inner) == 8, "inner");

static bool GuidsMatch(const GUID* Left, const GUID* Right)
{
	const unsigned long* LeftWords = (const unsigned long*)Left;
	const unsigned long* RightWords = (const unsigned long*)Right;
	return LeftWords[0] == RightWords[0]
		&& LeftWords[1] == RightWords[1]
		&& LeftWords[2] == RightWords[2]
		&& LeftWords[3] == RightWords[3];
}

// FUN_06ac23fc. thiscall ctor. RefCount starts at 1. Inner is 0.
FAuthorizer::FAuthorizer()
{
	Inner = nullptr;
	RefCount = 1;
}

// FUN_06ac2410. Drops Inner when Release hits zero.
// Retail rewrites the vfptr with PTR_FUN_06ada770 first. The compiler vfptr is already that table.
void FAuthorizer::ClearInner()
{
	void* Held = Inner;
	if (Held != nullptr)
	{
		((IUnknown*)Held)->Release();
		Inner = nullptr;
	}
}

// FUN_06ac242c. IUnknown::AddRef. RET 4.
ULONG STDMETHODCALLTYPE FAuthorizer::AddRef()
{
	return (unsigned long)InterlockedIncrement(&RefCount);
}

// FUN_06ac243d. IUnknown::Release. RET 4. Deletes on a zero return from the decrement.
ULONG STDMETHODCALLTYPE FAuthorizer::Release()
{
	long Count = InterlockedDecrement(&RefCount);
	if (Count != 0)
	{
		return (unsigned long)Count;
	}
	ClearInner();
	::operator delete(this);
	return 0;
}

// FUN_06ac2468. QueryInterface. IID_IUnknown or IID_IWMAuthorizer. RET 0xC.
// Failure stores NULL through Out.
HRESULT STDMETHODCALLTYPE FAuthorizer::QueryInterface(REFIID Iid, void** Out)
{
	const unsigned long* IidWords = (const unsigned long*)&Iid;
	const unsigned long* UnknownWords = (const unsigned long*)&GIidUnknown;
	long Index = 4;
	bool bMatch = true;
	for (long Word = 0; Word < Index; Word += 1)
	{
		if (IidWords[Word] != UnknownWords[Word])
		{
			bMatch = false;
			break;
		}
	}
	if (!bMatch)
	{
		const unsigned long* AuthorizerWords = (const unsigned long*)&GIidWmAuthorizer;
		bMatch = true;
		for (long Word = 0; Word < Index; Word += 1)
		{
			if (IidWords[Word] != AuthorizerWords[Word])
			{
				bMatch = false;
				break;
			}
		}
	}
	if (bMatch)
	{
		*Out = this;
		AddRef();
		return 0;
	}
	*Out = nullptr;
	return (long)0x80004002;
}

static long ForwardInnerAuthorizer(void* Inner, int Slot, void* Arg0, void* Arg1)
{
	FAuthorizer* Authorizer = (FAuthorizer*)Inner;
	if (Slot == 3)
	{
		return Authorizer->GetCertCount((unsigned long*)Arg0);
	}
	return Authorizer->GetCert((unsigned long)(ULONG_PTR)Arg0, (unsigned char**)Arg1);
}

// FUN_06ac24b2. IWMAuthorizer::GetCertCount. RET 8.
HRESULT STDMETHODCALLTYPE FAuthorizer::GetCertCount(DWORD* Count)
{
	if (Count == nullptr)
	{
		return (long)0x80004003;
	}
	if (Inner != nullptr)
	{
		return ForwardInnerAuthorizer(Inner, 3, Count, nullptr);
	}
	*Count = (unsigned long)GAuthorizerCertCount;
	return 0;
}

// FUN_06ac24e1. IWMAuthorizer::GetCert. RET 0xC.
// Copies 0x19 dwords from DAT_06add1d8 + Index * 0x78.
HRESULT STDMETHODCALLTYPE FAuthorizer::GetCert(DWORD Index, BYTE** CertOut)
{
	if (CertOut == nullptr)
	{
		return (long)0x80004003;
	}
	if (Inner != nullptr)
	{
		return ForwardInnerAuthorizer(Inner, 4, (void*)(ULONG_PTR)Index, CertOut);
	}
	if (Index >= (unsigned long)GAuthorizerCertCount)
	{
		return (long)0x80070057;
	}
	unsigned char* Blob = (unsigned char*)CoTaskMemAlloc(0x64);
	*CertOut = Blob;
	if (Blob == nullptr)
	{
		return (long)0x8007000E;
	}
	const unsigned long* CertDwords = (const unsigned long*)(GAuthorizerCerts + Index * 0x78);
	unsigned long* DestDwords = (unsigned long*)Blob;
	for (int Count = 0x19; Count != 0; Count -= 1)
	{
		*DestDwords = *CertDwords;
		DestDwords += 1;
		CertDwords += 1;
	}
	return 0;
}

// FUN_06ac2549. IWMAuthorizer::GetSharedData. RET 0x14.
// Calls Movie_06ac2ae0 / 28e0 / 3060 / 2e40 / 2b30 (MovieGraph.h).
HRESULT STDMETHODCALLTYPE FAuthorizer::GetSharedData(DWORD CertIndex, const BYTE* SharedData, BYTE* Cert, BYTE** OutShared)
{
	if (Cert == nullptr || SharedData == nullptr || OutShared == nullptr)
	{
		return (long)0x80004003;
	}

	void* Context;
	Movie_CreateFieldContext(&Context);
	long Status = Movie_HresultIfNonZeroLocked((int*)&Context);
	if (Status < 0)
	{
		Movie_ReleaseFieldContext(&Context);
		return Status;
	}

	unsigned char* SharedScratch = (unsigned char*)CoTaskMemAlloc(0x10);
	if (SharedScratch == nullptr)
	{
		Movie_ReleaseFieldContext(&Context);
		return (long)0x8007000E;
	}

	if (Inner != nullptr)
	{
		unsigned char* InnerOut = nullptr;
		FAuthorizer* InnerAuthorizer = (FAuthorizer*)Inner;
		long InnerHr = InnerAuthorizer->GetSharedData(CertIndex, SharedData, (unsigned char*)GAuthorizerCerts, &InnerOut);
		if (InnerHr < 0)
		{
			CoTaskMemFree(SharedScratch);
			Movie_ReleaseFieldContext(&Context);
			return InnerHr;
		}
		long StepHr = Movie_RecoverFieldLimbsLocked(&Context, (unsigned int*)(GAuthorizerCerts + 0x64), (unsigned int*)InnerOut, (unsigned int*)SharedScratch);
		if (StepHr < 0)
		{
			CoTaskMemFree(SharedScratch);
			SharedScratch = nullptr;
		}
		CoTaskMemFree(InnerOut);
	}
	else
	{
		if (CertIndex >= (unsigned long)GAuthorizerCertCount)
		{
			CoTaskMemFree(SharedScratch);
			Movie_ReleaseFieldContext(&Context);
			return (long)0x80070057;
		}
		const unsigned char* Record = GAuthorizerCerts + 0x64 + CertIndex * 0x78;
		long StepHr = Movie_RecoverFieldLimbsLocked(&Context, (unsigned int*)Record, (unsigned int*)SharedData, (unsigned int*)SharedScratch);
		if (StepHr < 0)
		{
			CoTaskMemFree(SharedScratch);
			SharedScratch = nullptr;
		}
	}

	if (SharedScratch == nullptr)
	{
		Movie_ReleaseFieldContext(&Context);
		return (long)0x8000FFFF;
	}

	unsigned char* SharedResult = (unsigned char*)CoTaskMemAlloc(0x50);
	if (SharedResult == nullptr)
	{
		Movie_ReleaseFieldContext(&Context);
		return (long)0x8007000E;
	}

	long StepHr = Movie_MapFieldLimbsLocked(&Context, (unsigned int*)(Cert + 0x30), (unsigned int*)SharedScratch, (unsigned int*)SharedResult);
	if (StepHr < 0)
	{
		Movie_ReleaseFieldContext(&Context);
		return (long)0x8000FFFF;
	}

	*OutShared = SharedResult;
	CoTaskMemFree(SharedScratch);
	Movie_ReleaseFieldContext(&Context);
	return 0;
}

// FUN_06ac23a0. Builds an IWMAuthorizer and QI's it for IID_IUnknown into *OutUnknown.
// A failed QI becomes 0x8000FFFF. RET 4.
static long __stdcall CreateAuthorizer(void** OutUnknown)
{
	if (OutUnknown == nullptr)
	{
		return (long)0x80004003;
	}
	FAuthorizer* Object = new (std::nothrow) FAuthorizer;
	if (Object == nullptr)
	{
		return (long)0x8007000E;
	}
	long Hr = Object->QueryInterface(GIidUnknown, OutUnknown);
	if (Hr < 0)
	{
		Hr = (long)0x8000FFFF;
	}
	Object->Release();
	return Hr;
}

// FUN_06ac1ea3. RefCount starts at 0; the following AddRef stores 1. The vfptr is the compiler table.
FServiceProvider::FServiceProvider()
{
	RefCount = 0;
}

// FUN_06ac1aa0. AddRef. JMP InterlockedIncrement(&RefCount).
ULONG STDMETHODCALLTYPE FServiceProvider::AddRef()
{
	return (unsigned long)InterlockedIncrement(&RefCount);
}

// FUN_06ac1ac0. Release. operator delete when the stored count is 0. RET 4.
ULONG STDMETHODCALLTYPE FServiceProvider::Release()
{
	long Count = InterlockedDecrement(&RefCount);
	if (RefCount != 0)
	{
		return (unsigned long)Count;
	}
	::operator delete(this);
	return 0;
}

// FUN_06ac1af0. QueryInterface. IID_IUnknown or IID_IServiceProvider. RET 0xC.
// Does not write Out on E_NOINTERFACE.
HRESULT STDMETHODCALLTYPE FServiceProvider::QueryInterface(REFIID Iid, void** Out)
{
	if (Out == nullptr)
	{
		return (long)0x80004003;
	}
	if (GuidsMatch(&Iid, &GIidUnknown) || GuidsMatch(&Iid, &GIidServiceProvider))
	{
		*Out = this;
		AddRef();
		return 0;
	}
	return (long)0x80004002;
}

// FUN_06ac1b60. IServiceProvider::QueryService. RET 0x10.
// Succeeds only for service IID_IWMReader and riid IID_IUnknown.
HRESULT STDMETHODCALLTYPE FServiceProvider::QueryService(REFGUID Service, REFIID Iid, void** Out)
{
	if (Out == nullptr)
	{
		return (long)0x80004003;
	}
	if (!GuidsMatch(&Service, &GIidWmReader) || !GuidsMatch(&Iid, &GIidUnknown))
	{
		return (long)0x80004002;
	}
	void* Authorizer = Out;
	long Hr = CreateAuthorizer(&Authorizer);
	if (Hr < 0)
	{
		return Hr;
	}
	*Out = Authorizer;
	return Hr;
}

static LRESULT CALLBACK MoviePlaybackWndProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam);

// FUN_06ac1e50. Approach 0. Run() is inside this function; it returns while playback continues.
long __cdecl BuildAndRunWmAsfGraph(HWND Hwnd, int Fullscreen, const char* Path)
{
	long Hr = 0;
	FServiceProvider* ServiceProvider = nullptr;
	IObjectWithSite* Site = nullptr;
	wchar_t WidePath[0x400];
	IBaseFilter* AsfReader = nullptr;
	IFileSourceFilter* FileSource = nullptr;
	IEnumPins* EnumPins = nullptr;
	IPin* Pin = nullptr;
	bool bRenderFailed = false;
	IVideoWindow* Video = nullptr;
	IMediaControl* Control = nullptr;
	bool bPaused = false;
	MSG Message;
	RECT Client;
	HWND FrameWindow = nullptr;
	long IdealWidth = 0;
	long IdealHeight = 0;
	long UsedWidth = 0;
	long UsedHeight = 0;
	long Left = 0;
	long Top = 0;
	long Style = 0;
	OAFilterState State = (OAFilterState)0;
	long NextHr = 0;
	long StateHr = 0;
	long RenderHr = 0;
	PIN_DIRECTION Direction = PINDIR_INPUT;

	g_playing = 0;
	g_hwndParent = Hwnd;
	g_originalWndProc = (WNDPROC)SetWindowLongA(Hwnd, -4, (LONG)(LONG_PTR)MoviePlaybackWndProc);

	Hr = CoCreateInstance(GClsidFilterGraph, nullptr, 1, GIidGraphBuilder, (void**)&g_graphBuilder);
	if (Hr < 0)
	{
		goto Epilogue;
	}

	ServiceProvider = new (std::nothrow) FServiceProvider;
	g_serviceProvider = (IUnknown*)ServiceProvider;
	ServiceProvider->AddRef();

	Hr = g_graphBuilder->QueryInterface(GIidObjectWithSite, (void**)&Site);
	if (Hr >= 0)
	{
		Site->SetSite((IUnknown*)(void*)ServiceProvider);
		Site->Release();
	}

	mbstowcs(WidePath, Path, 0x400);

	if (g_graphBuilder == nullptr)
	{
		Hr = (long)0x80004003;
		goto Epilogue;
	}

	AsfReader = nullptr;
	Hr = CoCreateInstance(GClsidWmAsfReader, nullptr, 1, GIidBaseFilter, (void**)&AsfReader);
	if (Hr < 0)
	{
		goto Epilogue;
	}

	Hr = g_graphBuilder->AddFilter(AsfReader, L"MovieSource");
	if (Hr < 0)
	{
		AsfReader->Release();
		goto Epilogue;
	}

	Hr = AsfReader->QueryInterface(GIidFileSourceFilter, (void**)&FileSource);
	if (Hr < 0)
	{
		AsfReader->Release();
		goto Epilogue;
	}

	Hr = FileSource->Load(WidePath, nullptr);
	if (Hr < 0)
	{
		FileSource->Release();
		AsfReader->Release();
		goto Epilogue;
	}

	Hr = AsfReader->EnumPins(&EnumPins);
	if (Hr < 0)
	{
		FileSource->Release();
		AsfReader->Release();
		goto Epilogue;
	}

	bRenderFailed = false;
	NextHr = EnumPins->Next(1, &Pin, nullptr);
	if (NextHr < 0)
	{
		Hr = NextHr;
		FileSource->Release();
		AsfReader->Release();
		goto Epilogue;
	}

	while (NextHr == 0)
	{
		Pin->QueryDirection(&Direction);
		if (Direction == PINDIR_OUTPUT)
		{
			RenderHr = g_graphBuilder->Render(Pin);
			if (RenderHr != 0)
			{
				bRenderFailed = true;
			}
		}
		Pin->Release();
		NextHr = EnumPins->Next(1, &Pin, nullptr);
	}
	EnumPins->Release();
	if (bRenderFailed)
	{
		Hr = (long)0x80004004;
	}
	FileSource->Release();
	AsfReader->Release();
	if (Hr < 0)
	{
		goto Epilogue;
	}

	g_graphBuilder->QueryInterface(GIidMediaControl, (void**)&g_mediaControl);
	g_graphBuilder->QueryInterface(GIidVideoWindow, (void**)&g_videoWindow);
	g_graphBuilder->QueryInterface(GIidBasicVideo, (void**)&g_basicVideo);
	g_initSuccess = 1;

	Video = g_videoWindow;
	Video->put_AutoShow(0);
	Video->put_Visible(0);
	Video->put_Owner((OAHWND)g_hwndParent);

	Control = g_mediaControl;
	Hr = Control->Pause();
	Movie_LogNegativeCode(Hr, GMoviePlayerSourceFile, 0x21D);
	if (Hr == 1)
	{
		bPaused = false;
		for (;;)
		{
			Control = g_mediaControl;
			StateHr = Control->GetState(0xA, &State);
			Movie_LogNegativeCode(StateHr, GMoviePlayerSourceFile, 0x227);
			if (StateHr == (long)0x80004005 || StateHr == (long)0x00040268)
			{
				return (long)0x80004005;
			}
			if (StateHr == 0 && (long)State == 1)
			{
				bPaused = true;
			}

			if (PeekMessageA(&Message, nullptr, 0, 0, 0) != 0)
			{
				do
				{
					if (GetMessageA(&Message, nullptr, 0, 0) == 0)
					{
						return (long)0x80004005;
					}
					TranslateMessage(&Message);
					DispatchMessageA(&Message);
					Sleep(0);
				}
				while (PeekMessageA(&Message, nullptr, 0, 0, 0) != 0);
			}

			if (g_graphBuilder == nullptr)
			{
				return (long)0x80004005;
			}
			if (!bPaused)
			{
				continue;
			}
			break;
		}
	}

	LockWindowUpdate(g_hwndParent);
	Video = g_videoWindow;
	Video->put_Visible(-1);
	Video->get_WindowStyle(&Style);
	Style = (Style & (long)0xFF3BFFFF) | (long)0x46000000;
	Video->put_WindowStyle(Style);
	LockWindowUpdate(nullptr);
	Hr = Video->SetWindowForeground(-1);
	Movie_LogNegativeCode(Hr, GMoviePlayerSourceFile, 0x263);

	if ((unsigned char)Fullscreen != 0)
	{
		FrameWindow = GetDesktopWindow();
	}
	else
	{
		FrameWindow = g_hwndParent;
	}
	GetClientRect(FrameWindow, &Client);

	Hr = Video->GetMaxIdealImageSize(&IdealWidth, &IdealHeight);
	Movie_LogNegativeCode(Hr, GMoviePlayerSourceFile, 0x2AD);
	Hr = Video->SetWindowPosition(0, 0, Client.right, Client.bottom);
	Movie_LogNegativeCode(Hr, GMoviePlayerSourceFile, 0x2B1);

	UsedWidth = IdealWidth;
	if (Client.right < UsedWidth)
	{
		UsedWidth = Client.right;
	}
	UsedHeight = IdealHeight;
	if (Client.bottom < UsedHeight)
	{
		UsedHeight = Client.bottom;
	}
	Left = (Client.right - UsedWidth) / 2;
	Top = (Client.bottom - UsedHeight) / 2;

	// control.h: SetDefaultSourcePosition sits at 0x78, so SetDestinationPosition is slot 0x7C.
	// Listing 06ac231f is CALL [vtable+0x7C] with Left, Top, Width, Height.
	Hr = g_basicVideo->SetDestinationPosition(Left, Top, UsedWidth, UsedHeight);
	Movie_LogNegativeCode(Hr, GMoviePlayerSourceFile, 0x2C6);

	g_graphBuilder->QueryInterface(GIidMediaEventEx, (void**)&g_mediaEventEx);
	g_mediaEventEx->SetNotifyWindow((OAHWND)g_hwndParent, 0x8001, 0);

	Control = g_mediaControl;
	Hr = Control->Run();
	if (Hr >= 0)
	{
		g_playing = 1;
	}
	else
	{
		g_playing = 0;
	}

Epilogue:
	if (g_playing == 0)
	{
		Movie_ReleaseComAndRestoreWndProc();
	}
	return Hr;
}

// LAB_06ac1cf0. Subclass proc installed by BuildAndRunWmAsfGraph. RET 0x10.
static LRESULT CALLBACK MoviePlaybackWndProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam)
{
	if (Message <= 0x86)
	{
		if (Message == 0x86 || Message == 0x6)
		{
			return 0;
		}
		if (Message == 0xF)
		{
			if (GetUpdateRect(Hwnd, nullptr, FALSE) == 0)
			{
				return 0;
			}
			PAINTSTRUCT Paint;
			BeginPaint(Hwnd, &Paint);
			EndPaint(Hwnd, &Paint);
			return 0;
		}
		if (Message == 0x14)
		{
			return 0;
		}
	}
	else
	{
		UINT Shifted = Message - 0x100;
		if (Shifted == 0)
		{
			if (WParam == 0x10 || WParam == 0x1B)
			{
				Movie_ReleaseComAndRestoreWndProc();
			}
			return 0;
		}
		Shifted -= 0x12;
		if (Shifted == 0)
		{
			return 0;
		}
		Shifted -= 0x7EEF;
		if (Shifted == 0)
		{
			IMediaEventEx* Events = g_mediaEventEx;
			if (Events == nullptr)
			{
				return 0;
			}
			long Code = 0;
			LONG_PTR Param1 = 0;
			LONG_PTR Param2 = 0;
			if (Events->GetEvent(&Code, &Param1, &Param2, 0) < 0)
			{
				return 0;
			}
			for (;;)
			{
				Events = g_mediaEventEx;
				Events->FreeEventParams(Code, Param1, Param2);
				if (Code == 1 || Code == 2)
				{
					Movie_ReleaseComAndRestoreWndProc();
					return 0;
				}
				Events = g_mediaEventEx;
				if (Events->GetEvent(&Code, &Param1, &Param2, 0) < 0)
				{
					return 0;
				}
			}
		}
	}

	if (g_videoWindow != nullptr && g_initSuccess != 0)
	{
		g_videoWindow->NotifyOwnerMessage((OAHWND)Hwnd, (long)Message, (LONG_PTR)WParam, (LONG_PTR)LParam);
	}
	return DefWindowProcA(Hwnd, Message, WParam, LParam);
}

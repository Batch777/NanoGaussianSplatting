// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

// Some UE 5.4 engine headers (e.g. ConcurrentLinearAllocator.h) use the Clang-only
// __has_feature() operator in #if/#elif. Under MSVC it is undefined and, with this
// plugin's include order, can be hit before UE's own compiler shim defines it,
// producing C4668/C4067 (warnings-as-errors). Define it defensively (matches what
// UE's MSVCPlatformCompilerPreSetup.h does). On Clang it's a builtin -> #ifndef skips.
#ifndef __has_feature
	#define __has_feature(x) 0
#endif

#include "CoreMinimal.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "Misc/EngineVersionComparison.h"

// NanoGS is written against the UE 5.5+ buffer-creation API
// (FRHIBufferCreateDesc + RHICmdList.CreateBuffer(Desc)). UE 5.4 lacks
// FRHIBufferCreateDesc, so back-port a minimal version here and route all buffer
// creation through GSCreateBuffer(). Views (FRHIViewDesc), LockBuffer and
// EBufferType already exist in 5.4, so only buffer creation needs a shim.

#if UE_VERSION_OLDER_THAN(5, 5, 0)

struct FRHIBufferCreateDesc
{
	const TCHAR*      DebugName    = TEXT("Buffer");
	uint32            Size         = 0;
	uint32            Stride       = 0;
	EBufferUsageFlags Usage        = EBufferUsageFlags::None;
	ERHIAccess        InitialState = ERHIAccess::SRVMask;

	static FRHIBufferCreateDesc Create(const TCHAR* InName, uint32 InSize, uint32 InStride, EBufferUsageFlags InUsage)
	{
		FRHIBufferCreateDesc D;
		D.DebugName = InName;
		D.Size      = InSize;
		D.Stride    = InStride;
		D.Usage     = InUsage;
		return D;
	}

	FRHIBufferCreateDesc& SetInitialState(ERHIAccess InState) { InitialState = InState; return *this; }
};

inline FBufferRHIRef GSCreateBuffer(FRHICommandListBase& RHICmdList, const FRHIBufferCreateDesc& Desc)
{
	FRHIResourceCreateInfo CreateInfo(Desc.DebugName);
	// Always allow ShaderResource: NanoGS reads several of its UAV buffers as SRVs too.
	// UE 5.5+'s Desc-based CreateBuffer permits this; the legacy 5.4 CreateBuffer would
	// otherwise set D3D12 DENY_SHADER_RESOURCE on UAV-only buffers and assert when they
	// are bound as an SRV during compute dispatch.
	const EBufferUsageFlags Usage = Desc.Usage | EBufferUsageFlags::ShaderResource;
	return RHICmdList.CreateBuffer(Desc.Size, Usage, Desc.Stride, Desc.InitialState, CreateInfo);
}

#else // UE 5.5+

inline FBufferRHIRef GSCreateBuffer(FRHICommandListBase& RHICmdList, const FRHIBufferCreateDesc& Desc)
{
	return RHICmdList.CreateBuffer(Desc);
}

#endif

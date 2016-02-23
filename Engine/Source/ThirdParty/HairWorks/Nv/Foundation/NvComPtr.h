// This code contains NVIDIA Confidential Information and is disclosed 
// under the Mutual Non-Disclosure Agreement. 
// 
// Notice 
// ALL NVIDIA DESIGN SPECIFICATIONS AND CODE ("MATERIALS") ARE PROVIDED "AS IS" NVIDIA MAKES 
// NO REPRESENTATIONS, WARRANTIES, EXPRESSED, IMPLIED, STATUTORY, OR OTHERWISE WITH RESPECT TO 
// THE MATERIALS, AND EXPRESSLY DISCLAIMS ANY IMPLIED WARRANTIES OF NONINFRINGEMENT, 
// MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. 
// 
// NVIDIA Corporation assumes no responsibility for the consequences of use of such 
// information or for any infringement of patents or other rights of third parties that may 
// result from its use. No license is granted by implication or otherwise under any patent 
// or patent rights of NVIDIA Corporation. No third party distribution is allowed unless 
// expressly authorized by NVIDIA.  Details are subject to change without notice. 
// This code supersedes and replaces all information previously supplied. 
// NVIDIA Corporation products are not authorized for use as critical 
// components in life support devices or systems without express written approval of 
// NVIDIA Corporation. 
// 
// Copyright (c) 2013-2015 NVIDIA Corporation. All rights reserved.
//
// NVIDIA Corporation and its licensors retain all intellectual property and proprietary
// rights in and to this software and related documentation and any modifications thereto.
// Any use, reproduction, disclosure or distribution of this software and related
// documentation without an express license agreement from NVIDIA Corporation is
// strictly prohibited.
//

#ifndef NV_COM_PTR_H
#define NV_COM_PTR_H

#include "NvComTypes.h"

namespace Nv {

template <class T>
class ComPtr
{
public:
	typedef T Type;
	typedef ComPtr ThisType;
	typedef IForwardUnknown* Ptr;

		/// Constructors
		/// Default Ctor. Sets to NV_NULL
	NV_FORCE_INLINE ComPtr() :m_ptr(NV_NULL) {}
		/// Sets, and ref counts.
	NV_FORCE_INLINE explicit ComPtr(T* ptr) :m_ptr(ptr) { if (ptr) ((Ptr)ptr)->AddRef(); }
		/// The copy ctor
	NV_FORCE_INLINE ComPtr(const ThisType& rhs) : m_ptr(rhs.m_ptr) { if (m_ptr) ((Ptr)m_ptr)->AddRef(); }

#ifdef NV_HAS_MOVE_SEMANTICS
		/// Move Ctor
	NV_FORCE_INLINE ComPtr(ThisType&& rhs) : m_ptr(rhs.m_ptr) { rhs.m_ptr = NV_NULL; }
		/// Move assign
	NV_FORCE_INLINE ComPtr& operator=(ThisType&& rhs) { T* swap = m_ptr; m_ptr = rhs.m_ptr; rhs.m_ptr = swap; return *this; }
#endif

	/// Destructor releases the pointer, assuming it is set
	NV_FORCE_INLINE ~ComPtr() { if (m_ptr) ((Ptr)m_ptr)->Release(); }

	// !!! Operators !!!

	  /// Returns the dumb pointer
	NV_FORCE_INLINE operator T *() const { return m_ptr; }

	NV_FORCE_INLINE T& operator*() { return *m_ptr; }
		/// For making method invocations through the smart pointer work through the dumb pointer
	NV_FORCE_INLINE T* operator->() const { return m_ptr; }

		/// Assign 
	NV_FORCE_INLINE const ThisType &operator=(const ThisType& rhs);
		/// Assign from dumb ptr
	NV_FORCE_INLINE T* operator=(T* in);

		/// Get the pointer and don't ref
	NV_FORCE_INLINE T* get() const { return m_ptr; }
		/// Release a contained NV_NULL pointer if set
	NV_FORCE_INLINE void setNull();

		/// Detach
	NV_FORCE_INLINE T* detach() { T* ptr = m_ptr; m_ptr = NV_NULL; return ptr; }

		/// Get ready for writing (nulls contents)
	NV_FORCE_INLINE T** writeRef() { setNull(); return &m_ptr; }
		/// Get for read access
	NV_FORCE_INLINE T*const* readRef() const { return &m_ptr; }

		/// Swap
	Void swap(ThisType& rhs);

protected:
	/// Gets the address of the dumb pointer.
	NV_FORCE_INLINE T** operator&();

	T* m_ptr;
};

//----------------------------------------------------------------------------
template <typename T>
Void ComPtr<T>::setNull()
{
	if (m_ptr)
	{
		((Ptr)m_ptr)->Release();
		m_ptr = NV_NULL;
	}
}
//----------------------------------------------------------------------------
template <typename T>
T** ComPtr<T>::operator&()
{
	NV_ASSERT(m_ptr == NV_NULL);
	return &m_ptr;
}
//----------------------------------------------------------------------------
template <typename T>
const ComPtr<T>& ComPtr<T>::operator=(const ThisType& rhs)
{
	if (rhs.m_ptr) ((Ptr)rhs.m_ptr)->AddRef();
	if (m_ptr) ((Ptr)m_ptr)->Release();
	m_ptr = rhs.m_ptr;
	return *this;
}
//----------------------------------------------------------------------------
template <typename T>
T* ComPtr<T>::operator=(T* ptr)
{
	if (ptr) ((Ptr)ptr)->AddRef();
	if (m_ptr) ((Ptr)m_ptr)->Release();
	m_ptr = ptr;           
	return m_ptr;
}
//----------------------------------------------------------------------------
template <typename T>
Void ComPtr<T>::swap(ThisType& rhs)
{
	T* tmp = m_ptr;
	m_ptr = rhs.m_ptr;
	rhs.m_ptr = tmp;
}

} // namespace Nv

#endif // NV_COM_PTR_H

////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Project:  Embedded Learning Library (ELL)
//  File:     ValueTensorOperations.h (value)
//  Authors:  Kern Handa
//
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "ValueScalar.h"

#include <utilities/include/MemoryLayout.h>

#include <functional>

namespace ell
{
namespace value
{

    class Tensor;

    enum class Slice
    {
        All
    };

   Scalar Accumulate(Tensor tensor, Scalar initialValue);

    /// <summary> Creates a for loop over the tensor </summary>
    /// <param name="tensor"> The instance of Tensor that references the data over which to iterate </param>
    /// <param name="fn"> The function to be called for each coordinate where there is an active element </param>
    void For(Tensor tensor, std::function<void(Scalar, Scalar, Scalar)> fn);

} // namespace value
} // namespace ell
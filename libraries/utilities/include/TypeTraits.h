////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Project:  Embedded Machine Learning Library (EMLL)
//  File:     TypeTraits.h (utilities)
//  Authors:  Chuck Jacobs, Ofer Dekel
//
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <type_traits>

namespace emll
{
namespace utilities
{
    /// <summary> Utility class to test if a type is a specialization of std::vector. Used by IsVector/IsNotVector, below. </summary>
    template <typename ValueType>
    class IsVectorType
    {
    private:
        template <typename VectorType>
        static constexpr bool IsVectorChecker(typename VectorType::value_type*, typename std::enable_if_t<std::is_base_of<VectorType, typename std::vector<typename VectorType::value_type>>::value, int> = 0);

        template <typename VectorType>
        static constexpr bool IsVectorChecker(...);

    public:
        static const bool value = IsVectorChecker<ValueType>(0);
    };

    /// <summary> Enabled if ValueType is a fundamental value. </summary>
    template <typename ValueType>
    using IsFundamental = typename std::enable_if_t<std::is_fundamental<typename std::decay<ValueType>::type>::value, int>;

    /// <summary> Enabled if ValueType is not a fundamental value. </summary>
    template <typename ValueType>
    using IsNotFundamental = typename std::enable_if_t<!std::is_fundamental<typename std::decay<ValueType>::type>::value, int>;

    template <typename ValueType>
    using IsIntegral = typename std::enable_if_t<std::is_integral<typename std::decay<ValueType>::type>::value && !std::is_same<typename std::decay<ValueType>::type , bool>::value && !std::is_same<typename std::decay<ValueType>::type , char>::value, bool>;

    template <typename ValueType>
    using IsFloatingPoint = typename std::enable_if_t<std::is_floating_point<typename std::decay<ValueType>::type>::value, bool>;

    /// <summary> Enabled if ValueType is a class. </summary>
    template <typename ValueType>
    using IsClass = typename std::enable_if_t<std::is_class<ValueType>::value, int>;

    /// <summary> Enabled if ValueType is a specialization of std::vector. </summary>
    template <typename ValueType>
    using IsVector = typename std::enable_if_t<IsVectorType<typename std::decay<ValueType>::type>::value, int>;

    /// <summary> Enabled if ValueType is not a specialization of std::vector. </summary>
    template <typename ValueType>
    using IsNotVector = typename std::enable_if_t<!IsVectorType<typename std::decay<ValueType>::type>::value, int>;
}
}

#include "../tcc/TypeTraits.tcc"